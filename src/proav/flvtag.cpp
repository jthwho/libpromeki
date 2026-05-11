/**
 * @file      flvtag.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * FLV body-frame serialization for video / audio / script payloads as
 * carried by RTMP messages.  The FLV file-level tag header
 * (@c TagType / @c DataSize / @c Timestamp / @c StreamID) is replaced
 * by RTMP's chunk-stream framing and is not emitted here.
 */

#include <cstring>
#include <promeki/flvtag.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ------------------------------------------------------------------
        // Buffer-append helper (duplicate of the one in amf0.cpp; the
        // proav layer can't link a private helper out of core, and the
        // alternative — public Buffer growth API — is out of scope for
        // this changeset).
        // ------------------------------------------------------------------
        Error appendToBuffer(Buffer &out, const void *bytes, size_t len) {
                if (len == 0) return Error::Ok;
                size_t cur = out.size();
                if (cur + len > out.availSize()) {
                        size_t newAlloc = out.allocSize() * 2;
                        if (newAlloc < cur + len) newAlloc = cur + len;
                        if (newAlloc < 256) newAlloc = 256;
                        Buffer bigger(newAlloc);
                        if (cur > 0 && out.data() != nullptr) {
                                std::memcpy(bigger.data(), out.data(), cur);
                        }
                        bigger.setSize(cur);
                        out = bigger;
                }
                uint8_t *base = static_cast<uint8_t *>(out.data());
                if (base == nullptr) return Error::NotHostAccessible;
                std::memcpy(base + cur, bytes, len);
                out.setSize(cur + len);
                return Error::Ok;
        }

        // Big-endian helpers — minimal, file-local, mirroring amf0.cpp's
        // patterns.
        inline uint16_t loadU16BE(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }

        inline uint32_t loadU24BE(const uint8_t *p) {
                return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }

        inline uint32_t loadU32BE(const uint8_t *p) {
                return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        }

        inline void storeU24BE(uint8_t *p, uint32_t v) {
                p[0] = static_cast<uint8_t>(v >> 16);
                p[1] = static_cast<uint8_t>(v >> 8);
                p[2] = static_cast<uint8_t>(v);
        }

        inline void storeU32BE(uint8_t *p, uint32_t v) {
                p[0] = static_cast<uint8_t>(v >> 24);
                p[1] = static_cast<uint8_t>(v >> 16);
                p[2] = static_cast<uint8_t>(v >> 8);
                p[3] = static_cast<uint8_t>(v);
        }

        // Map enhanced-codec enum to FourCC.
        uint32_t enhancedCodecFourCc(FlvVideoTag::Codec c) {
                switch (c) {
                        case FlvVideoTag::ExHevc: return FlvVideoTag::FourCcHvc1;
                        case FlvVideoTag::ExVp9:  return FlvVideoTag::FourCcVp09;
                        case FlvVideoTag::ExAv1:  return FlvVideoTag::FourCcAv01;
                        default:                  return 0;
                }
        }

        FlvVideoTag::Codec fourCcToEnhancedCodec(uint32_t fcc) {
                switch (fcc) {
                        case FlvVideoTag::FourCcHvc1: return FlvVideoTag::ExHevc;
                        case FlvVideoTag::FourCcVp09: return FlvVideoTag::ExVp9;
                        case FlvVideoTag::FourCcAv01: return FlvVideoTag::ExAv1;
                        default:                      return FlvVideoTag::CodecUnknown;
                }
        }

        bool isEnhancedCodec(FlvVideoTag::Codec c) {
                return c == FlvVideoTag::ExHevc || c == FlvVideoTag::ExVp9 || c == FlvVideoTag::ExAv1;
        }

        // Map our AvcPacketType to enhanced PacketType (and back).  The
        // values happen to coincide for the three we care about
        // (SequenceHeader=0, CodedFrames=1, EndOfSequence=2), so the
        // mapping is identity — but parses of CodedFramesX (3) collapse
        // to Nalu with CTO=0 to keep the public enum tight.
        uint8_t avcPacketTypeToExPacketType(FlvVideoTag::AvcPacketType pt) {
                switch (pt) {
                        case FlvVideoTag::SequenceHeader: return 0;
                        case FlvVideoTag::Nalu:           return 1;
                        case FlvVideoTag::EndOfSequence:  return 2;
                }
                return 1;
        }

        Error exPacketTypeToAvcPacketType(uint8_t exPt, FlvVideoTag::AvcPacketType &out, bool &hasCto) {
                switch (exPt) {
                        case 0:
                                out    = FlvVideoTag::SequenceHeader;
                                hasCto = false;
                                return Error::Ok;
                        case 1:
                                out    = FlvVideoTag::Nalu;
                                hasCto = true;
                                return Error::Ok;
                        case 2:
                                out    = FlvVideoTag::EndOfSequence;
                                hasCto = false;
                                return Error::Ok;
                        case 3:
                                // CodedFramesX — same payload semantics as CodedFrames
                                // but no composition-time field.
                                out    = FlvVideoTag::Nalu;
                                hasCto = false;
                                return Error::Ok;
                        default:
                                // PacketTypeMetadata (4), PacketTypeMPEG2TSSequenceStart (5)
                                // are not modeled in v1.
                                return Error::NotSupported;
                }
        }

} // anonymous namespace

// ============================================================================
// FlvVideoTag
// ============================================================================

Error FlvVideoTag::pack(Buffer &out) const {
        if (isEnhancedCodec(codec)) {
                // Enhanced-RTMP layout: 1 byte (isEx | PacketType<<3 | FrameType&7),
                // 4-byte FourCC, optional 3-byte signed CTO, payload.
                if ((frameType & ~0x07) != 0) return Error::OutOfRange;
                uint8_t exPt   = avcPacketTypeToExPacketType(packetType);
                uint8_t header = static_cast<uint8_t>(0x80 | ((exPt & 0x0F) << 3) | (frameType & 0x07));
                Error   err    = appendToBuffer(out, &header, 1);
                if (err.isError()) return err;

                uint8_t fcc[4];
                storeU32BE(fcc, enhancedCodecFourCc(codec));
                err = appendToBuffer(out, fcc, 4);
                if (err.isError()) return err;

                if (packetType == Nalu) {
                        if (compositionTimeOffsetMs > 0x7FFFFF || compositionTimeOffsetMs < -0x800000)
                                return Error::OutOfRange;
                        uint8_t cto[3];
                        storeU24BE(cto, static_cast<uint32_t>(compositionTimeOffsetMs) & 0xFFFFFF);
                        err = appendToBuffer(out, cto, 3);
                        if (err.isError()) return err;
                }

                if (data.size() > 0) {
                        err = appendToBuffer(out, data.data(), data.size());
                        if (err.isError()) return err;
                }
                return Error::Ok;
        }

        // Legacy layout: 1 byte (FrameType<<4 | CodecID), [AvcPacketType,
        // CompositionTime] for AVC, payload.
        if ((frameType & ~0x0F) != 0) return Error::OutOfRange;
        if ((codec & ~0x0F) != 0) return Error::OutOfRange;
        uint8_t header = static_cast<uint8_t>(((frameType & 0x0F) << 4) | (codec & 0x0F));
        Error   err    = appendToBuffer(out, &header, 1);
        if (err.isError()) return err;

        if (codec == Avc) {
                uint8_t pt = static_cast<uint8_t>(packetType);
                err        = appendToBuffer(out, &pt, 1);
                if (err.isError()) return err;
                if (compositionTimeOffsetMs > 0x7FFFFF || compositionTimeOffsetMs < -0x800000)
                        return Error::OutOfRange;
                uint8_t cto[3];
                storeU24BE(cto, static_cast<uint32_t>(compositionTimeOffsetMs) & 0xFFFFFF);
                err = appendToBuffer(out, cto, 3);
                if (err.isError()) return err;
        }

        if (data.size() > 0) {
                err = appendToBuffer(out, data.data(), data.size());
                if (err.isError()) return err;
        }
        return Error::Ok;
}

Error FlvVideoTag::unpack(const BufferView &in, FlvVideoTag &out) {
        if (in.count() > 1) return Error::NotSupported;
        if (in.size() < 1) return Error::OutOfRange;
        const uint8_t *p   = in.data();
        size_t         len = in.size();
        uint8_t        b0  = p[0];

        if ((b0 & 0x80) != 0) {
                // Enhanced RTMP path.
                uint8_t exPt = (b0 >> 3) & 0x0F;
                uint8_t ft   = b0 & 0x07;
                if (len < 1 + 4) return Error::OutOfRange;
                uint32_t fcc = loadU32BE(p + 1);
                Codec    cd  = fourCcToEnhancedCodec(fcc);
                if (cd == CodecUnknown) return Error::NotSupported;

                FlvVideoTag tag;
                tag.codec     = cd;
                tag.frameType = static_cast<FrameType>(ft);

                bool hasCto = false;
                Error err   = exPacketTypeToAvcPacketType(exPt, tag.packetType, hasCto);
                if (err.isError()) return err;

                size_t off = 5;
                if (hasCto) {
                        if (len < off + 3) return Error::OutOfRange;
                        uint32_t raw = loadU24BE(p + off);
                        // sign-extend 24-bit
                        if (raw & 0x800000) raw |= 0xFF000000;
                        tag.compositionTimeOffsetMs = static_cast<int32_t>(raw);
                        off += 3;
                } else {
                        tag.compositionTimeOffsetMs = 0;
                }

                size_t payloadLen = len - off;
                if (payloadLen > 0) {
                        Buffer payload(payloadLen);
                        std::memcpy(payload.data(), p + off, payloadLen);
                        payload.setSize(payloadLen);
                        tag.data = payload;
                }
                out = tag;
                return Error::Ok;
        }

        // Legacy path.
        FrameType ft = static_cast<FrameType>((b0 >> 4) & 0x0F);
        Codec     cd = static_cast<Codec>(b0 & 0x0F);
        FlvVideoTag tag;
        tag.frameType = ft;
        tag.codec     = cd;

        size_t off = 1;
        if (cd == Avc) {
                if (len < off + 4) return Error::OutOfRange;
                tag.packetType   = static_cast<AvcPacketType>(p[off]);
                uint32_t raw     = loadU24BE(p + off + 1);
                if (raw & 0x800000) raw |= 0xFF000000;
                tag.compositionTimeOffsetMs = static_cast<int32_t>(raw);
                off += 4;
        }

        size_t payloadLen = len - off;
        if (payloadLen > 0) {
                Buffer payload(payloadLen);
                std::memcpy(payload.data(), p + off, payloadLen);
                payload.setSize(payloadLen);
                tag.data = payload;
        }
        out = tag;
        return Error::Ok;
}

// ============================================================================
// FlvAudioTag
// ============================================================================

Error FlvAudioTag::pack(Buffer &out) const {
        if ((format & ~0x0F) != 0) return Error::OutOfRange;
        uint8_t header = static_cast<uint8_t>(((format & 0x0F) << 4) |
                                              ((rate & 0x03) << 2) |
                                              ((size & 0x01) << 1) |
                                              (channelType & 0x01));
        Error err = appendToBuffer(out, &header, 1);
        if (err.isError()) return err;

        if (format == Aac) {
                uint8_t pt = static_cast<uint8_t>(aacPacketType);
                err        = appendToBuffer(out, &pt, 1);
                if (err.isError()) return err;
        }

        if (data.size() > 0) {
                err = appendToBuffer(out, data.data(), data.size());
                if (err.isError()) return err;
        }
        return Error::Ok;
}

Error FlvAudioTag::unpack(const BufferView &in, FlvAudioTag &out) {
        if (in.count() > 1) return Error::NotSupported;
        if (in.size() < 1) return Error::OutOfRange;
        const uint8_t *p   = in.data();
        size_t         len = in.size();

        FlvAudioTag tag;
        tag.format      = static_cast<SoundFormat>((p[0] >> 4) & 0x0F);
        tag.rate        = static_cast<SoundRate>((p[0] >> 2) & 0x03);
        tag.size        = static_cast<SoundSize>((p[0] >> 1) & 0x01);
        tag.channelType = static_cast<SoundType>(p[0] & 0x01);

        size_t off = 1;
        if (tag.format == Aac) {
                if (len < 2) return Error::OutOfRange;
                tag.aacPacketType = static_cast<AacPacketType>(p[1]);
                off               = 2;
        }

        size_t payloadLen = len - off;
        if (payloadLen > 0) {
                Buffer payload(payloadLen);
                std::memcpy(payload.data(), p + off, payloadLen);
                payload.setSize(payloadLen);
                tag.data = payload;
        }
        out = tag;
        return Error::Ok;
}

// ============================================================================
// FlvScriptTag
// ============================================================================

Error FlvScriptTag::pack(Buffer &out) const {
        Amf0Writer w(out);
        Error      err = w.writeString(name);
        if (err.isError()) return err;
        return w.writeValue(body);
}

Error FlvScriptTag::unpack(const BufferView &in, FlvScriptTag &out) {
        Result<Amf0Value::List> r = Amf0Reader::read(in);
        if (r.second().isError()) return r.second();
        const Amf0Value::List &vals = r.first();
        if (vals.size() < 2) return Error::CorruptData;
        if (!vals[0].isString()) return Error::CorruptData;
        out.name = vals[0].asString();
        out.body = vals[1];
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
