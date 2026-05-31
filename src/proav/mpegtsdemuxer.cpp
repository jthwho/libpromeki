/**
 * @file      mpegtsdemuxer.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mpegtsdemuxer.h>
#include <promeki/logger.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Returns @c true when @p streamId falls in the PES "video
        // stream" range 0xE0..0xEF.  Video PES typically carry
        // unbounded packet_length, so the demuxer must end them on
        // the next PUSI rather than at a literal byte count.
        inline bool isVideoStreamId(uint8_t streamId) {
                return streamId >= 0xE0 && streamId <= 0xEF;
        }

} // namespace

MpegTsDemuxer::MpegTsDemuxer() = default;
MpegTsDemuxer::~MpegTsDemuxer() = default;

List<MpegTsDemuxer::StreamInfo> MpegTsDemuxer::streams() const {
        List<StreamInfo> out;
        _streamTypeByPid.forEach([&](const uint16_t &pid, const MpegTs::StreamType &t) {
                StreamInfo si;
                si.pid = pid;
                si.streamType = t;
                auto rit = _registrationByPid.find(pid);
                if (rit != _registrationByPid.end()) si.registrationFormat = rit->second;
                out.pushToBack(si);
        });
        return out;
}

Error MpegTsDemuxer::push(const BufferView &data) {
        if (!data.isValid() || data.size() == 0) return Error::Ok;
        const uint8_t *in = data.data();
        size_t         inLen = data.size();

        // Concatenate any carry-over from a previous push with the
        // first bytes of this one, so a packet that straddles a
        // push boundary is still recovered as a single 188-byte
        // window.
        while (inLen > 0 || _carrySize >= MpegTs::PacketSize) {
                // Build a 188-byte window if possible.
                const uint8_t *windowStart = nullptr;
                bool           windowOwned = false;
                uint8_t        tmp[MpegTs::PacketSize];

                if (_carrySize == 0 && inLen >= MpegTs::PacketSize) {
                        windowStart = in;
                } else if (_carrySize + inLen >= MpegTs::PacketSize) {
                        const size_t take = MpegTs::PacketSize - _carrySize;
                        std::memcpy(tmp, _carry, _carrySize);
                        std::memcpy(tmp + _carrySize, in, take);
                        windowStart = tmp;
                        windowOwned = true;
                } else {
                        // Save what's left into the carry buffer.
                        std::memcpy(_carry + _carrySize, in, inLen);
                        _carrySize += inLen;
                        return Error::Ok;
                }

                // Sync-check.  If the window doesn't start with a
                // 0x47, search the next 188 bytes for one and slide
                // the window — counting the skipped bytes as
                // discarded.
                if (windowStart[0] != MpegTs::SyncByte) {
                        // Re-sync: walk forward until we find 0x47.
                        size_t advance = 1;
                        if (windowOwned) {
                                // The window is in tmp; scan inside it.
                                while (advance < MpegTs::PacketSize && tmp[advance] != MpegTs::SyncByte) ++advance;
                        } else {
                                while (advance < inLen && in[advance] != MpegTs::SyncByte) ++advance;
                                if (advance == inLen) {
                                        // Slid all the way through the
                                        // remaining input without finding
                                        // 0x47 — drop it all.
                                        _bytesDiscarded += inLen;
                                        in += inLen;
                                        inLen = 0;
                                        continue;
                                }
                        }
                        _bytesDiscarded += advance;
                        if (windowOwned) {
                                // Recompose: skip 'advance' bytes from
                                // the combined (carry+input) stream.
                                if (advance < _carrySize) {
                                        const size_t carryRemaining = _carrySize - advance;
                                        std::memmove(_carry, _carry + advance, carryRemaining);
                                        _carrySize = carryRemaining;
                                } else {
                                        const size_t inputConsumed = advance - _carrySize;
                                        _carrySize = 0;
                                        in += inputConsumed;
                                        inLen -= inputConsumed;
                                }
                        } else {
                                in += advance;
                                inLen -= advance;
                        }
                        continue; // Retry window assembly with new alignment.
                }

                // Window starts with 0x47; process it as a TS packet.
                Error err = processPacket(windowStart);
                if (err.isError()) return err;

                if (windowOwned) {
                        const size_t inputConsumed = MpegTs::PacketSize - _carrySize;
                        _carrySize = 0;
                        in += inputConsumed;
                        inLen -= inputConsumed;
                } else {
                        in += MpegTs::PacketSize;
                        inLen -= MpegTs::PacketSize;
                }
        }
        return Error::Ok;
}

Error MpegTsDemuxer::flush() {
        // Final pass: any video PES whose end-of-frame marker was the
        // next PUSI must be flushed now.
        Error firstErr = Error::Ok;
        for (auto it = _pes.begin(); it != _pes.end(); ++it) {
                PesReasm &pr = it->second;
                if (pr.inProgress && pr.writePos > 0 && pr.unbounded) {
                        Error err = finalizePes(it->first, pr);
                        if (err.isError() && firstErr.isOk()) firstErr = err;
                }
        }
        return firstErr;
}

Error MpegTsDemuxer::processPacket(const uint8_t *p) {
        // Header layout already validated to start with 0x47.
        const uint8_t  flags1 = p[1];
        const uint8_t  flags2 = p[2];
        const uint8_t  flags3 = p[3];
        const bool     pusi = (flags1 & 0x40) != 0;
        const bool     tei = (flags1 & 0x80) != 0;
        if (tei) {
                // Transport error indicator — drop the packet silently;
                // the upstream layer is responsible for surfacing line
                // errors.  Counted via continuity errors when we miss
                // the next CC tick.
                return Error::Ok;
        }
        const uint16_t pid = static_cast<uint16_t>(((flags1 & 0x1F) << 8) | flags2);
        const uint8_t  afc = static_cast<uint8_t>((flags3 >> 4) & 0x03);
        const uint8_t  cc = static_cast<uint8_t>(flags3 & 0x0F);

        if (pid == MpegTs::PidNull) return Error::Ok;

        size_t payloadOff = 4;
        bool   randomAccessIndicator = false;
        bool   discontinuityIndicator = false;
        if (afc == 0x02 || afc == 0x03) {
                // Adaptation field present.  AF length byte first;
                // capture the discontinuity_indicator and
                // random_access_indicator flags and then skip past
                // the AF body.
                const uint8_t afLen = p[4];
                if (afLen > 183) {
                        // Malformed.
                        return Error::Ok;
                }
                if (afLen >= 1) {
                        const uint8_t afFlags = p[5];
                        discontinuityIndicator = (afFlags & 0x80) != 0;
                        randomAccessIndicator = (afFlags & 0x40) != 0;
                        const bool    hasPcr = (afFlags & 0x10) != 0;
                        if (hasPcr && afLen >= 7 && _pcrCallback) {
                                // PCR group lives in bytes 6..11
                                // (right after the AF length and flags
                                // bytes).
                                _pcrCallback(pid, MpegTs::decodePcr(p + 6));
                        }
                }
                payloadOff = 5 + afLen;
                if (afc == 0x02 || payloadOff >= MpegTs::PacketSize) {
                        // AF-only packet: no payload to process.
                        return Error::Ok;
                }
        } else if (afc == 0x00) {
                // Reserved AFC; ignore.
                return Error::Ok;
        }

        const size_t payloadLen = MpegTs::PacketSize - payloadOff;

        // PSI vs PES dispatch.
        if (pid == MpegTs::PidPat || (pid == _pmtPid && _pmtPid != MpegTs::PidNull)) {
                return processPsiPacket(p + payloadOff, payloadLen, pid, pusi);
        }

        // PES payload — only if this PID is one we know about and
        // tracking continuity counters.
        auto pesIt = _pes.find(pid);
        if (pesIt == _pes.end()) {
                // PID not in PMT; ignore.
                return Error::Ok;
        }
        PesReasm &pr = pesIt->second;
        if (pr.haveCc) {
                const uint8_t expected = static_cast<uint8_t>((pr.continuityCounter + 1) & 0x0F);
                if (cc != expected) {
                        ++_continuityErrors;
                }
        }
        pr.continuityCounter = cc;
        pr.haveCc = true;

        // PES reassembly.
        const size_t   pesAvail = payloadLen;
        const uint8_t *pesData = p + payloadOff;

        if (pusi) {
                // If we had an in-progress unbounded video PES, this
                // marks its end — finalise it before starting the
                // new one.
                if (pr.inProgress && pr.unbounded && pr.writePos > 0) {
                        Error err = finalizePes(pid, pr);
                        if (err.isError()) return err;
                }
                return startNewPes(pid, pr, pesData, pesAvail, randomAccessIndicator, discontinuityIndicator);
        }

        // Continuation packet.
        if (!pr.inProgress) {
                // Discard payload bytes for a PID whose first PUSI
                // we never saw.
                return Error::Ok;
        }
        // Append to the reassembly buffer; grow as needed.
        const size_t newPos = pr.writePos + pesAvail;
        if (newPos > pr.buffer.size()) {
                size_t newCap = pr.buffer.size() * 2;
                if (newCap < newPos) newCap = newPos;
                Buffer grown(newCap);
                if (!grown.isValid()) return Error::NoMem;
                grown.setSize(newCap);
                if (pr.writePos > 0) {
                        std::memcpy(grown.data(), pr.buffer.data(), pr.writePos);
                }
                pr.buffer = std::move(grown);
        }
        std::memcpy(static_cast<uint8_t *>(pr.buffer.data()) + pr.writePos, pesData, pesAvail);
        pr.writePos += pesAvail;

        // If we know the expected total, finalise as soon as we have
        // it all.  expectedTotal counts payload bytes only — the
        // demuxer applies the PES-header skip in startNewPes.
        if (!pr.unbounded && pr.expectedTotal > 0 && pr.writePos >= pr.expectedTotal) {
                return finalizePes(pid, pr);
        }
        return Error::Ok;
}

Error MpegTsDemuxer::processPsiPacket(const uint8_t *p, size_t payloadLen, uint16_t pid, bool pusi) {
        if (payloadLen < 1) return Error::Ok;
        auto &reasm = _psi[pid];

        const uint8_t *bytes = p;
        size_t         bytesLen = payloadLen;

        if (pusi) {
                // A new section starts in this packet.  byte 0 is the
                // pointer_field — the number of bytes before the start
                // of the new section.  Any bytes before that point
                // belong to the *previous* section (continuation)
                // that this packet is closing out.
                if (bytesLen < 1) return Error::Ok;
                const uint8_t pointer = bytes[0];
                const size_t  carry = static_cast<size_t>(pointer);
                if (1 + carry > bytesLen) return Error::Ok;
                if (carry > 0 && reasm.writePos > 0 && reasm.expectedTotal > 0) {
                        // Carry-over bytes belong to the in-progress section.
                        const size_t need = reasm.expectedTotal - reasm.writePos;
                        const size_t copy = carry < need ? carry : need;
                        if (copy > 0) {
                                std::memcpy(static_cast<uint8_t *>(reasm.buffer.data()) + reasm.writePos, bytes + 1,
                                            copy);
                                reasm.writePos += copy;
                        }
                        if (reasm.writePos >= reasm.expectedTotal) {
                                Error err = dispatchPsiSection(
                                        pid, static_cast<const uint8_t *>(reasm.buffer.data()), reasm.expectedTotal);
                                if (err.isError()) return err;
                        }
                }
                // Reset for the new section that begins after the carry.
                reasm.writePos = 0;
                reasm.expectedTotal = 0;
                bytes += 1 + carry;
                bytesLen -= 1 + carry;
        }

        // Consume any number of contiguous sections (the muxer always
        // packs one per packet, but the standard allows several
        // back-to-back) plus a possible final continuation.
        while (bytesLen > 0) {
                if (reasm.expectedTotal == 0) {
                        // Starting a fresh section — need at least 3
                        // bytes of header to learn its length.  Real-
                        // world muxers never split the 3-byte section
                        // header across two packets, so on the off
                        // chance the data is malformed enough to land
                        // us here, drop and resync at the next PUSI.
                        if (bytesLen < 3) return Error::Ok;
                        // Pointer-field stuffing: a 0xFF table_id
                        // means no more sections in this packet —
                        // the rest is just padding.
                        if (bytes[0] == 0xFF) return Error::Ok;
                        const uint16_t sectionLen =
                                static_cast<uint16_t>(((bytes[1] & 0x0F) << 8) | bytes[2]);
                        const size_t   total = static_cast<size_t>(sectionLen) + 3;
                        if (total < 8 || total > 4096) {
                                // Malformed: section_length out of
                                // range (PAT/PMT spec-cap is 1024,
                                // private tables 4096).
                                return Error::Ok;
                        }
                        if (!reasm.buffer.isValid() || reasm.buffer.size() < total) {
                                reasm.buffer = Buffer(total);
                                if (!reasm.buffer.isValid()) return Error::NoMem;
                                reasm.buffer.setSize(total);
                        }
                        reasm.writePos = 0;
                        reasm.expectedTotal = total;
                }
                // Resume / continue a section in progress.
                const size_t need = reasm.expectedTotal - reasm.writePos;
                const size_t copy = bytesLen < need ? bytesLen : need;
                std::memcpy(static_cast<uint8_t *>(reasm.buffer.data()) + reasm.writePos, bytes, copy);
                reasm.writePos += copy;
                bytes += copy;
                bytesLen -= copy;
                if (reasm.writePos == reasm.expectedTotal) {
                        Error err = dispatchPsiSection(
                                pid, static_cast<const uint8_t *>(reasm.buffer.data()), reasm.expectedTotal);
                        reasm.writePos = 0;
                        reasm.expectedTotal = 0;
                        if (err.isError()) return err;
                }
        }
        return Error::Ok;
}

Error MpegTsDemuxer::dispatchPsiSection(uint16_t pid, const uint8_t *section, size_t len) {
        if (!MpegTs::isPsiSectionValid(section, len)) {
                // CRC mismatch — drop the section silently.
                return Error::Ok;
        }
        const uint8_t tableId = section[0];
        if (pid == MpegTs::PidPat && tableId == 0x00) {
                return parsePat(section, len);
        }
        if (pid == _pmtPid && tableId == 0x02) {
                return parsePmt(section, len);
        }
        return Error::Ok;
}

Error MpegTsDemuxer::parsePat(const uint8_t *section, size_t len) {
        MpegTs::ParsedPat pat;
        if (MpegTs::parsePat(section, len, &pat).isError()) return Error::Ok;
        // Single-program: pick the first non-zero program number.
        for (const MpegTs::PatEntry &e : pat.entries) {
                if (e.programNumber == 0) continue; // network_pid entry — skip.
                _programNumber = e.programNumber;
                _pmtPid = e.pid;
                _havePat = true;
                if (_programCallback) _programCallback();
                return Error::Ok;
        }
        return Error::Ok;
}

Error MpegTsDemuxer::parsePmt(const uint8_t *section, size_t len) {
        MpegTs::ParsedPmt pmt;
        if (MpegTs::parsePmt(section, len, &pmt).isError()) return Error::Ok;
        _pcrPid = pmt.pcrPid;
        // Rebuild the stream map from scratch (PMT updates may add
        // / remove streams).  Also rebuild the per-PID
        // registration_descriptor map so consumers can disambiguate
        // the codecs that share stream_type 0x06 (Opus / AV1 / SMPTE
        // 302M) via the PMT's @c format_identifier.
        Map<uint16_t, MpegTs::StreamType> newStreams;
        Map<uint16_t, uint32_t>           newRegistrations;
        for (const MpegTs::PmtStream &s : pmt.streams) {
                newStreams.insert(s.pid, static_cast<MpegTs::StreamType>(s.streamType));
                if (s.descriptors.isValid() && s.descriptors.size() > 0) {
                        uint32_t   regFmt = 0;
                        BufferView dv(s.descriptors, 0, s.descriptors.size());
                        if (MpegTs::findRegistrationDescriptor(dv, &regFmt).isOk()) {
                                newRegistrations.insert(s.pid, regFmt);
                        }
                }
        }
        const bool changed = (newStreams != _streamTypeByPid) ||
                             (newRegistrations != _registrationByPid);
        _streamTypeByPid = std::move(newStreams);
        _registrationByPid = std::move(newRegistrations);

        // Drop reassembly state for PIDs that disappeared.
        for (auto it = _pes.begin(); it != _pes.end();) {
                if (!_streamTypeByPid.contains(it->first)) {
                        it = _pes.remove(it);
                } else {
                        ++it;
                }
        }
        // Pre-allocate reassembly state for new PIDs.
        _streamTypeByPid.forEach([&](const uint16_t &pid, const MpegTs::StreamType &t) {
                auto [it, inserted] = _pes.tryEmplace(pid);
                if (inserted) {
                        it->second.streamType = t;
                } else {
                        it->second.streamType = t;
                }
        });

        _havePmt = true;
        if (changed && _programCallback) _programCallback();
        return Error::Ok;
}

Error MpegTsDemuxer::startNewPes(uint16_t pid, PesReasm &pr, const uint8_t *pesStart, size_t pesAvail,
                                 bool randomAccess, bool discontinuity) {
        MpegTs::PesHeader ph;
        size_t            headerSize = 0;
        Error             pe = MpegTs::readPesHeader(pesStart, pesAvail, &ph, &headerSize);
        if (pe.isError()) {
                pr.inProgress = false;
                pr.writePos = 0;
                return Error::Ok;
        }
        pr.dataAlignment = ph.dataAlignmentIndicator;
        pr.hasPts = ph.hasPts;
        pr.hasDts = ph.hasDts;
        pr.pts90k = ph.pts90k;
        pr.dts90k = ph.dts90k;
        pr.randomAccess = randomAccess;
        pr.discontinuity = discontinuity;
        pr.unbounded = (ph.pesPacketLength == 0) || isVideoStreamId(ph.streamId);
        // expectedTotal counts payload bytes (everything after the
        // PES header data), so subtract the in-header overhead.
        pr.expectedTotal = (ph.pesPacketLength == 0)
                                   ? 0
                                   : (static_cast<size_t>(ph.pesPacketLength) -
                                      (3 + (pr.hasPts ? 5 : 0) + (pr.hasDts ? 5 : 0)));

        // Skip the PES header bytes; only the payload goes into the
        // reassembly buffer.
        const size_t payloadStart = headerSize;
        const size_t payloadFirst = pesAvail - payloadStart;

        // Initial allocation: at least the expectedTotal when known.
        size_t initialCap = pr.expectedTotal > 0 ? pr.expectedTotal : 64 * 1024;
        if (initialCap < payloadFirst) initialCap = payloadFirst;
        if (pr.buffer.size() < initialCap) {
                pr.buffer = Buffer(initialCap);
                if (!pr.buffer.isValid()) return Error::NoMem;
                pr.buffer.setSize(initialCap);
        }
        std::memcpy(pr.buffer.data(), pesStart + payloadStart, payloadFirst);
        pr.writePos = payloadFirst;
        pr.inProgress = true;

        if (!pr.unbounded && pr.expectedTotal > 0 && pr.writePos >= pr.expectedTotal) {
                return finalizePes(pid, pr);
        }
        return Error::Ok;
}

Error MpegTsDemuxer::finalizePes(uint16_t pid, PesReasm &pr) {
        AccessUnit au;
        au.pid = pid;
        au.streamType = pr.streamType;
        {
                auto rit = _registrationByPid.find(pid);
                if (rit != _registrationByPid.end()) au.registrationFormat = rit->second;
        }
        au.hasPts = pr.hasPts;
        au.hasDts = pr.hasDts;
        au.pts90k = pr.pts90k;
        au.dts90k = pr.dts90k;
        au.randomAccess = pr.randomAccess;
        au.dataAlignment = pr.dataAlignment;
        au.discontinuity = pr.discontinuity;
        const size_t take = (pr.unbounded || pr.expectedTotal == 0) ? pr.writePos
                                                                    : (pr.writePos < pr.expectedTotal ? pr.writePos
                                                                                                      : pr.expectedTotal);
        au.payload = BufferView(pr.buffer, 0, take);
        pr.inProgress = false;
        pr.writePos = 0;
        pr.expectedTotal = 0;
        if (_streamCallback) return _streamCallback(au);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
