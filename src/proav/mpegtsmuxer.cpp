/**
 * @file      mpegtsmuxer.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mpegtsmuxer.h>
#include <promeki/logger.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Per-PID continuity counter is a 4-bit field.  Helper wraps
        // a uint8_t with the modulo.
        inline uint8_t advanceCc(uint8_t cc) {
                return static_cast<uint8_t>((cc + 1) & 0x0F);
        }

        // Returns @c true when @p streamType is unambiguously an
        // audio format whose @c stream_id lives in the 0xC0..0xDF
        // range.  Note: @c StreamTypePrivatePes (0x06) and
        // @c StreamTypeAc3 (0x81) carry audio but ride on
        // @c PesStreamIdPrivate1 (0xBD), so they are not in this set.
        inline bool isAudioStreamType(MpegTs::StreamType t) {
                switch (t) {
                        case MpegTs::StreamTypeMpeg1Audio:
                        case MpegTs::StreamTypeMpeg2Audio:
                        case MpegTs::StreamTypeAacAdts:
                        case MpegTs::StreamTypeAacLatm: return true;
                        default: return false;
                }
        }

        // Returns @c true when @p streamType is unambiguously a video
        // format whose @c stream_id lives in the 0xE0..0xEF range.
        // @c StreamTypePrivatePes (used by AV1) is ambiguous and
        // disambiguated via @ref MpegTs::StreamKind by the caller.
        inline bool isVideoStreamType(MpegTs::StreamType t) {
                switch (t) {
                        case MpegTs::StreamTypeMpeg1Video:
                        case MpegTs::StreamTypeMpeg2Video:
                        case MpegTs::StreamTypeH264:
                        case MpegTs::StreamTypeHevc:
                        case MpegTs::StreamTypeJpegXs: return true;
                        default: return false;
                }
        }

        // Derive a StreamKind from a stream_type when the caller
        // passed StreamKind::Auto.  Returns StreamKind::Auto unchanged
        // for the ambiguous 0x06 / metadata cases so the caller's
        // explicit hint (or the eventual fallback) carries through.
        inline MpegTs::StreamKind deriveStreamKind(MpegTs::StreamType t) {
                if (isVideoStreamType(t)) return MpegTs::StreamKind::Video;
                if (isAudioStreamType(t)) return MpegTs::StreamKind::Audio;
                if (t == MpegTs::StreamTypeAc3) return MpegTs::StreamKind::Audio;
                if (t == MpegTs::StreamTypeMetadata) return MpegTs::StreamKind::Metadata;
                if (t == MpegTs::StreamTypeScte35) return MpegTs::StreamKind::Data;
                return MpegTs::StreamKind::Auto;
        }

} // namespace

MpegTsMuxer::MpegTsMuxer() = default;
MpegTsMuxer::~MpegTsMuxer() = default;

void MpegTsMuxer::setTransportStreamId(uint16_t v) {
        _transportStreamId = v;
        ++_patVersion;
        _forcePatPmt = true;
}

void MpegTsMuxer::setProgramNumber(uint16_t v) {
        _programNumber = v;
        ++_patVersion;
        ++_pmtVersion;
        _forcePatPmt = true;
}

void MpegTsMuxer::setPmtPid(uint16_t v) {
        _pmtPid = v;
        ++_patVersion;
        _forcePatPmt = true;
}

void MpegTsMuxer::setPcrPid(uint16_t v) {
        _pcrPid = v;
        ++_pmtVersion;
        _forcePatPmt = true;
}

void MpegTsMuxer::setPatPmtIntervalMs(int v) {
        _patPmtIntervalMs = v < 0 ? 0 : v;
}

void MpegTsMuxer::setPcrIntervalMs(int v) {
        _pcrIntervalMs = v < 0 ? 0 : v;
}

uint8_t MpegTsMuxer::pickPesStreamId(MpegTs::StreamType streamType, MpegTs::StreamKind kind,
                                     size_t indexOfKind) const {
        // AV1 rides on stream_type 0x06 but per the AOMedia
        // AV1-in-MPEG-TS spec lives in the video stream_id range
        // (0xE0..0xEF), so respect the caller's kind hint before
        // falling through to the streamType-based switch.
        if (kind == MpegTs::StreamKind::Video) {
                return static_cast<uint8_t>(MpegTs::PesStreamIdVideoFirst + (indexOfKind & 0x0F));
        }
        if (kind == MpegTs::StreamKind::Audio) {
                // MPEG-1/2 / AAC audio gets a true audio stream_id
                // (0xC0..0xDF); everything else (AC-3, Opus, SMPTE
                // 302M) rides on private_stream_1.
                if (isAudioStreamType(streamType)) {
                        return static_cast<uint8_t>(MpegTs::PesStreamIdAudioFirst + (indexOfKind & 0x1F));
                }
                return MpegTs::PesStreamIdPrivate1;
        }
        if (kind == MpegTs::StreamKind::Metadata || streamType == MpegTs::StreamTypeMetadata) {
                return MpegTs::PesStreamIdMetadata;
        }
        // Auto / Data fallback — try to derive from streamType, then
        // settle on private_stream_1 for anything still ambiguous.
        if (isVideoStreamType(streamType)) {
                return static_cast<uint8_t>(MpegTs::PesStreamIdVideoFirst + (indexOfKind & 0x0F));
        }
        if (isAudioStreamType(streamType)) {
                return static_cast<uint8_t>(MpegTs::PesStreamIdAudioFirst + (indexOfKind & 0x1F));
        }
        return MpegTs::PesStreamIdPrivate1;
}

Error MpegTsMuxer::addStream(uint16_t pid, MpegTs::StreamType streamType, const BufferView &descriptors,
                             MpegTs::StreamKind kind) {
        // Reserved PID range — anything in 0x0000..0x000F is owned by
        // PSI tables and must not collide with an ES PID.  We also
        // reject PidNull because the null packet is not a stream.
        if (pid <= MpegTs::PidIpmp || pid == MpegTs::PidNull) {
                return Error::InvalidArgument;
        }
        if (pid > MpegTs::MaxPid) return Error::InvalidArgument;
        if (pid == _pmtPid) return Error::InvalidArgument;
        if (findStream(pid) != nullptr) return Error::Exists;

        StreamRec rec;
        rec.pid = pid;
        rec.streamType = streamType;
        rec.kind = (kind == MpegTs::StreamKind::Auto) ? deriveStreamKind(streamType) : kind;
        if (rec.kind == MpegTs::StreamKind::Video) {
                rec.pesStreamId = pickPesStreamId(streamType, rec.kind, _videoStreamCount);
                ++_videoStreamCount;
        } else if (rec.kind == MpegTs::StreamKind::Audio) {
                rec.pesStreamId = pickPesStreamId(streamType, rec.kind, _audioStreamCount);
                ++_audioStreamCount;
        } else {
                rec.pesStreamId = pickPesStreamId(streamType, rec.kind, 0);
        }

        if (descriptors.isValid() && descriptors.size() > 0) {
                rec.descriptors = Buffer(descriptors.size());
                if (!rec.descriptors.isValid()) return Error::NoMem;
                rec.descriptors.setSize(descriptors.size());
                std::memcpy(rec.descriptors.data(), descriptors.data(), descriptors.size());
        }

        _streams.pushToBack(std::move(rec));
        ++_pmtVersion;
        _forcePatPmt = true;
        return Error::Ok;
}

List<uint16_t> MpegTsMuxer::registeredPids() const {
        List<uint16_t> out;
        for (const StreamRec &s : _streams) out.pushToBack(s.pid);
        return out;
}

bool MpegTsMuxer::hasStream(uint16_t pid) const {
        return findStream(pid) != nullptr;
}

MpegTsMuxer::StreamRec *MpegTsMuxer::findStream(uint16_t pid) {
        for (StreamRec &s : _streams) {
                if (s.pid == pid) return &s;
        }
        return nullptr;
}

const MpegTsMuxer::StreamRec *MpegTsMuxer::findStream(uint16_t pid) const {
        for (const StreamRec &s : _streams) {
                if (s.pid == pid) return &s;
        }
        return nullptr;
}

void MpegTsMuxer::forcePatPmt() {
        _forcePatPmt = true;
}

Error MpegTsMuxer::markNextAccessUnitDiscontinuous(uint16_t pid) {
        StreamRec *s = findStream(pid);
        if (s == nullptr) return Error::IdNotFound;
        s->pendingDiscontinuity = true;
        return Error::Ok;
}

Error MpegTsMuxer::writePsiSectionPacket(uint16_t pid, const Buffer &section, uint8_t &cc, const EmitCallback &emit) {
        // PSI in a TS packet (ISO/IEC 13818-1 §2.4.4.1):
        //   sync(0x47)
        //   PUSI=1 (first packet only), TEI=0, TP=0, PID
        //   TSC=00, AFC=01 (payload only), CC
        //   pointer_field (1 byte) — first packet only
        //   section bytes (split across multiple packets if needed)
        //   stuffing 0xFF on the final packet's tail
        //
        // pointer_field exists only on the packet that begins a new
        // section.  Continuation packets carry section bytes directly
        // from byte 4 onward.

        const size_t sectionSize = section.isValid() ? section.size() : 0;
        const uint8_t *src = sectionSize > 0 ? static_cast<const uint8_t *>(section.data()) : nullptr;

        // Compute packet count: first packet carries 183 bytes of
        // section (after the pointer_field), each subsequent packet
        // carries 184.
        size_t       remaining = sectionSize;
        const size_t firstPacketCapacity = MpegTs::PacketSize - 4 - 1; // 183
        const size_t contPacketCapacity = MpegTs::PacketSize - 4;       // 184
        size_t       packetCount = 1;
        if (sectionSize > firstPacketCapacity) {
                packetCount += (sectionSize - firstPacketCapacity + contPacketCapacity - 1) / contPacketCapacity;
        }

        Buffer outBuf(packetCount * MpegTs::PacketSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(packetCount * MpegTs::PacketSize);
        uint8_t *out = static_cast<uint8_t *>(outBuf.data());

        size_t srcOff = 0;
        for (size_t pi = 0; pi < packetCount; ++pi) {
                uint8_t *p = out + pi * MpegTs::PacketSize;
                const bool firstPacket = (pi == 0);
                p[0] = MpegTs::SyncByte;
                p[1] = static_cast<uint8_t>((firstPacket ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
                p[2] = static_cast<uint8_t>(pid & 0xFF);
                p[3] = static_cast<uint8_t>(0x10 | (cc & 0x0F)); // AFC=01, payload only
                size_t writePos = 4;
                if (firstPacket) {
                        p[writePos++] = 0x00; // pointer_field
                }
                const size_t roomAfter = MpegTs::PacketSize - writePos;
                const size_t copyBytes = remaining < roomAfter ? remaining : roomAfter;
                if (copyBytes > 0 && src != nullptr) {
                        std::memcpy(p + writePos, src + srcOff, copyBytes);
                        srcOff += copyBytes;
                        remaining -= copyBytes;
                        writePos += copyBytes;
                }
                // Stuffing on the final packet only — middle packets
                // always fill 184 bytes from the section.
                for (size_t i = writePos; i < MpegTs::PacketSize; ++i) p[i] = 0xFF;
                cc = advanceCc(cc);
        }

        BufferView view(outBuf, 0, packetCount * MpegTs::PacketSize);
        return emit(view);
}

Error MpegTsMuxer::emitPatPmtIfDue(uint64_t now27mhz, bool force, const EmitCallback &emit) {
        if (!force && _havePsiTime) {
                const uint64_t interval27mhz = static_cast<uint64_t>(_patPmtIntervalMs) * 27000;
                if (now27mhz - _lastPsiTime27mhz < interval27mhz) return Error::Ok;
        }
        // PAT.
        Buffer patSection;
        Error  err = MpegTs::buildPat(_transportStreamId, _programNumber, _pmtPid, _patVersion, patSection);
        if (err.isError()) return err;
        err = writePsiSectionPacket(MpegTs::PidPat, patSection, _patCc, emit);
        if (err.isError()) return err;

        // PMT.
        List<MpegTs::PmtStream> streams;
        for (const StreamRec &s : _streams) {
                MpegTs::PmtStream pe;
                pe.streamType = static_cast<uint8_t>(s.streamType);
                pe.pid = s.pid;
                pe.descriptors = s.descriptors;
                streams.pushToBack(std::move(pe));
        }
        Buffer pmtSection;
        err = MpegTs::buildPmt(_programNumber, _pcrPid, _pmtVersion, BufferView(), streams, pmtSection);
        if (err.isError()) return err;
        err = writePsiSectionPacket(_pmtPid, pmtSection, _pmtCc, emit);
        if (err.isError()) return err;

        _havePsiTime = true;
        _lastPsiTime27mhz = now27mhz;
        return Error::Ok;
}

Error MpegTsMuxer::writeAccessUnit(uint16_t pid, const BufferView &payload, uint64_t pts90k, uint64_t dts90k,
                                   bool isKeyframe, const EmitCallback &emit) {
        StreamRec *stream = findStream(pid);
        if (stream == nullptr) {
                promekiErr("MpegTsMuxer: writeAccessUnit for unregistered PID 0x%04x", pid);
                return Error::IdNotFound;
        }
        if (!payload.isValid() || payload.size() == 0) {
                return Error::InvalidArgument;
        }
        if (_streams.isEmpty()) {
                return Error::InvalidArgument;
        }
        if (!emit) return Error::InvalidArgument;

        // Anchor "now" to DTS so the PCR / PSI interval logic
        // advances with the elementary-stream clock — for a file
        // writer there is no separate wall clock to compare against.
        const uint64_t now27mhz = dts90k * 300;

        // CBR accounting — wrap the caller's emit so we tally bytes
        // emitted by this writeAccessUnit (PAT + PMT + payload + any
        // future NULL padding).
        int64_t bytesEmittedThisCall = 0;
        const EmitCallback wrappedEmit = [&bytesEmittedThisCall, &emit](const BufferView &v) -> Error {
                if (v.isValid()) bytesEmittedThisCall += static_cast<int64_t>(v.size());
                return emit(v);
        };

        // Emit PAT / PMT first if due or forced.
        const bool forcePsi = _forcePatPmt;
        if (forcePsi) {
                _forcePatPmt = false;
        }
        Error err = emitPatPmtIfDue(now27mhz, forcePsi, wrappedEmit);
        if (err.isError()) return err;

        // Build the PES packet (header + payload) in a contiguous
        // scratch buffer so we can slice it into 184-byte TS payloads
        // below.
        MpegTs::PesHeader ph;
        ph.streamId = stream->pesStreamId;
        ph.dataAlignmentIndicator = true; // every AU is aligned by definition.
        ph.hasPts = true;
        ph.hasDts = (dts90k != pts90k);
        ph.pts90k = pts90k;
        ph.dts90k = dts90k;
        // PES_packet_length covers the bytes *after* the length field
        // (i.e. flags + optional header data + payload).  For video
        // streams that may exceed 65525 bytes we set 0 (unbounded);
        // for everything else we emit the literal length.
        const size_t pesHdrSize = MpegTs::pesHeaderSize(ph);
        const size_t pesBodySize = pesHdrSize - 6 + payload.size();
        if (stream->kind == MpegTs::StreamKind::Video || pesBodySize > 0xFFFF) {
                ph.pesPacketLength = 0;
        } else {
                ph.pesPacketLength = static_cast<uint16_t>(pesBodySize);
        }

        const size_t pesTotalSize = pesHdrSize + payload.size();
        Buffer       pesBuf(pesHdrSize + payload.size());
        if (!pesBuf.isValid()) return Error::NoMem;
        pesBuf.setSize(pesTotalSize);
        uint8_t *pesData = static_cast<uint8_t *>(pesBuf.data());
        MpegTs::writePesHeader(ph, pesData);
        std::memcpy(pesData + pesHdrSize, payload.data(), payload.size());

        // Now packetize.  Each packet:
        //   header(4) [+ adaptation_field(N)] + payload(184-N)
        //
        // The first packet (PUSI=1) optionally carries an adaptation
        // field with the random-access indicator and, on the PCR PID,
        // the PCR group.  Subsequent packets carry payload only,
        // until the final packet, which fills any leftover space via
        // adaptation-field stuffing.
        const bool emitPcrThisCall =
                (pid == _pcrPid) &&
                (isKeyframe || !_havePcrTime ||
                 (now27mhz - _lastPcrTime27mhz) >= static_cast<uint64_t>(_pcrIntervalMs) * 27000);

        // Packetisation.  Each TS packet is exactly 188 bytes:
        //   header(4) + adaptation_field(0..184) + payload(0..184)
        //   where afTotal + payloadSize == 184.
        //
        // Mapping the PES bytes onto packets is driven by two rules:
        //  1. Only the first packet may carry an adaptation field
        //     with flags (PCR / random_access_indicator).  Middle
        //     packets are payload-only (AFC=01).  The last packet
        //     may carry a stuffing-only AF when payload doesn't
        //     fill 184 bytes.
        //  2. ISO/IEC 13818-1 §2.4.3.3: the
        //     adaptation_field_length byte alone counts as 1 byte
        //     of AF (with no body) — the magic "1-byte stuffing"
        //     form (afLength == 0).  Any larger AF needs a flags
        //     byte, so afTotal == 2 is illegal; the smallest AF
        //     bigger than 1 byte is afTotal == 2 with afLength=1
        //     and a single flags byte = 0.
        constexpr size_t kPayloadRoom = MpegTs::PacketSize - 4; // 184

        // Upper bound on packet count.  The first packet may carry
        // as little as (184 - 8) = 176 payload bytes when PCR + RA
        // are both present; every subsequent packet carries up to
        // 184.  ceil(pesTotalSize / 176) + 1 is always an upper
        // bound on the real count.
        const size_t maxPackets = (pesTotalSize + 175) / 176 + 1;
        Buffer       outBuf(maxPackets * MpegTs::PacketSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(maxPackets * MpegTs::PacketSize);
        uint8_t *out = static_cast<uint8_t *>(outBuf.data());
        size_t   outBytes = 0;

        size_t pesOffset = 0;
        bool   firstPacket = true;

        while (pesOffset < pesTotalSize) {
                const bool   needPcr = firstPacket && emitPcrThisCall;
                const bool   needRandomAccess = firstPacket && isKeyframe;
                const bool   needDiscontinuity = firstPacket && stream->pendingDiscontinuity;
                const size_t pesRemaining = pesTotalSize - pesOffset;

                // Minimum AF total bytes when we need flags/PCR/RA/Disc
                // on this packet: length(1) + flags(1) + PCR(6 if
                // present).  When none of those are needed, baseline
                // AF is 0 (no AF unless stuffing forces it).
                size_t afTotalMin = 0;
                if (needPcr || needRandomAccess || needDiscontinuity) {
                        afTotalMin = 2 + (needPcr ? 6 : 0);
                }

                // Maximum payload bytes this packet can carry.
                const size_t payloadCap = kPayloadRoom - afTotalMin;

                // Bytes of PES we will actually pack into this packet.
                const size_t payloadSize = pesRemaining < payloadCap ? pesRemaining : payloadCap;

                // The leftover bytes (184 - payloadSize - afTotalMin)
                // become extra stuffing in the AF.  Combined with
                // afTotalMin, that yields the final AF total.
                const size_t afStuff = kPayloadRoom - payloadSize - afTotalMin;
                size_t       afTotal = afTotalMin + afStuff;

                // If we have no AF flags / PCR but still need
                // stuffing (last-packet underflow), promote a bare
                // AF to carry the stuffing.  afTotal == 1 is the
                // 1-byte stuffing form (afLength = 0).  afTotal >= 2
                // requires a flags byte.
                if (afTotal == 0) {
                        // No AF needed; AFC = 01 (payload only).
                } else if (afTotal == 1) {
                        // Single-byte AF: afLength = 0, no flags byte.
                } else if (afTotalMin == 0 && afTotal >= 2) {
                        // Stuffing-only AF: needs a flags byte.
                        // Already accounted in afTotal — body holds
                        // 1 flags byte (= 0) plus (afTotal - 2)
                        // stuffing bytes.
                }

                uint8_t *p = out + outBytes;
                p[0] = MpegTs::SyncByte;
                const uint8_t pusi = firstPacket ? 0x40 : 0x00;
                p[1] = static_cast<uint8_t>(pusi | ((pid >> 8) & 0x1F));
                p[2] = static_cast<uint8_t>(pid & 0xFF);

                uint8_t afc;
                if (afTotal == 0) {
                        afc = 0x10; // payload only
                } else if (payloadSize == 0) {
                        afc = 0x20; // AF only
                } else {
                        afc = 0x30; // AF + payload
                }
                p[3] = static_cast<uint8_t>(afc | (stream->continuityCounter & 0x0F));

                size_t writePos = 4;
                if (afTotal > 0) {
                        // adaptation_field_length is the size of the
                        // body that follows the length byte.
                        const uint8_t afLength = static_cast<uint8_t>(afTotal - 1);
                        p[writePos++] = afLength;
                        if (afLength > 0) {
                                uint8_t flags = 0;
                                if (needDiscontinuity) flags |= 0x80;
                                if (needRandomAccess) flags |= 0x40;
                                if (needPcr) flags |= 0x10;
                                p[writePos++] = flags;
                                if (needPcr) {
                                        uint8_t pcr[6];
                                        MpegTs::encodePcr(now27mhz, pcr);
                                        std::memcpy(p + writePos, pcr, 6);
                                        writePos += 6;
                                        _havePcrTime = true;
                                        _lastPcrTime27mhz = now27mhz;
                                }
                                // Stuffing fills the rest of the AF body.
                                const size_t afBodyEnd = 4 + 1 + afLength;
                                while (writePos < afBodyEnd) p[writePos++] = 0xFF;
                        }
                }

                if (payloadSize > 0) {
                        std::memcpy(p + writePos, pesData + pesOffset, payloadSize);
                        pesOffset += payloadSize;
                        writePos += payloadSize;
                }

                // The packet must be exactly 188 bytes.  If we got
                // the math right, writePos == 188 here.
                if (writePos != MpegTs::PacketSize) {
                        promekiErr("MpegTsMuxer: internal packetization underflow (%zu / 188)", writePos);
                        return Error::Invalid;
                }

                // ISO/IEC 13818-1 §2.4.3.3: continuity_counter
                // increments only for packets that carry payload
                // (AFC=01 or AFC=11).  AF-only packets (AFC=10)
                // hold the CC unchanged.
                if (payloadSize > 0) {
                        stream->continuityCounter = advanceCc(stream->continuityCounter);
                }
                outBytes += MpegTs::PacketSize;
                firstPacket = false;

                if (outBytes >= maxPackets * MpegTs::PacketSize) {
                        promekiErr("MpegTsMuxer: ran out of scratch space (%zu/%zu)",
                                   outBytes, maxPackets * MpegTs::PacketSize);
                        return Error::Invalid;
                }
        }

        // Pending discontinuity has been emitted (or no AF was
        // generated, in which case the flag had no carrier — the
        // caller may set it again before the next writeAccessUnit if
        // they need it preserved).
        stream->pendingDiscontinuity = false;

        BufferView view(outBuf, 0, outBytes);
        Error      e = wrappedEmit(view);
        if (e.isError()) return e;

        // CBR top-up: if a non-zero target is set, count NULL packets
        // needed to hit the running average and emit them.  The
        // formula is straight ratio: expected_bytes = rate * dt /
        // 8; we top up the deficit.  Both bytes and time are
        // anchored on the first writeAccessUnit so the running
        // average tracks the wire rate regardless of startup transient.
        if (_muxRateBps > 0) {
                if (!_haveCbrAnchor) {
                        _haveCbrAnchor = true;
                        _cbrAnchor27mhz = now27mhz;
                        _bytesSinceAnchor = bytesEmittedThisCall;
                } else {
                        _bytesSinceAnchor += bytesEmittedThisCall;
                        // dt in seconds = (now - anchor) / 27_000_000.
                        const uint64_t dt27 = now27mhz - _cbrAnchor27mhz;
                        // expectedBytes = rate * dt / 27e6 / 8.
                        // Use 128-bit-safe arithmetic by ordering
                        // multiplication before division and capping
                        // dt27 to a sane range (uint64 dt27 fits
                        // 2^64 / 27e6 ≈ 6.8 × 10^11 seconds, so
                        // overflow is academic).
                        const uint64_t expectedBytes = (static_cast<uint64_t>(_muxRateBps) * dt27) / 27'000'000ull / 8ull;
                        if (expectedBytes > static_cast<uint64_t>(_bytesSinceAnchor)) {
                                const uint64_t deficit = expectedBytes - static_cast<uint64_t>(_bytesSinceAnchor);
                                const size_t   nullPackets = static_cast<size_t>(deficit / MpegTs::PacketSize);
                                if (nullPackets > 0) {
                                        Buffer nullBlob(nullPackets * MpegTs::PacketSize);
                                        if (!nullBlob.isValid()) return Error::NoMem;
                                        nullBlob.setSize(nullPackets * MpegTs::PacketSize);
                                        uint8_t *np = static_cast<uint8_t *>(nullBlob.data());
                                        for (size_t i = 0; i < nullPackets; ++i) {
                                                uint8_t *q = np + i * MpegTs::PacketSize;
                                                q[0] = MpegTs::SyncByte;
                                                q[1] = static_cast<uint8_t>((MpegTs::PidNull >> 8) & 0x1F);
                                                q[2] = static_cast<uint8_t>(MpegTs::PidNull & 0xFF);
                                                q[3] = 0x10; // AFC=01, CC ignored on null packets.
                                                for (int b = 4; b < MpegTs::PacketSize; ++b) q[b] = 0xFF;
                                        }
                                        _bytesSinceAnchor += static_cast<int64_t>(nullPackets * MpegTs::PacketSize);
                                        BufferView vv(nullBlob, 0, nullPackets * MpegTs::PacketSize);
                                        Error      nerr = emit(vv);
                                        if (nerr.isError()) return nerr;
                                }
                        }
                }
        }

        return Error::Ok;
}

void MpegTsMuxer::setMuxRateBps(int64_t bps) {
        _muxRateBps = bps < 0 ? 0 : bps;
        if (_muxRateBps == 0) {
                _haveCbrAnchor = false;
                _bytesSinceAnchor = 0;
        }
}

Error MpegTsMuxer::emitNullPacket(const EmitCallback &emit) {
        if (!emit) return Error::InvalidArgument;
        Buffer pkt(MpegTs::PacketSize);
        if (!pkt.isValid()) return Error::NoMem;
        pkt.setSize(MpegTs::PacketSize);
        uint8_t *p = static_cast<uint8_t *>(pkt.data());
        p[0] = MpegTs::SyncByte;
        p[1] = static_cast<uint8_t>((MpegTs::PidNull >> 8) & 0x1F);
        p[2] = static_cast<uint8_t>(MpegTs::PidNull & 0xFF);
        p[3] = 0x10; // AFC=01, payload only, CC ignored on null packets.
        for (size_t i = 4; i < MpegTs::PacketSize; ++i) p[i] = 0xFF;
        BufferView view(pkt, 0, MpegTs::PacketSize);
        return emit(view);
}

PROMEKI_NAMESPACE_END
