/**
 * @file      st436m.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st436m.h>
#if PROMEKI_ENABLE_PROAV

#include <cstring>
#include <promeki/ancformat.h>
#include <promeki/st291packet.h>
#include <promeki/enums_video.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ----------------------------------------------------------------
        // Contiguous 10-bit MSB-first packing — the layout AncPacket::data()
        // uses for ST 291 packets (DID, SDID/DBN, DC, UDW…, CS as 10-bit
        // words packed tightly, most-significant-bit first).
        // ----------------------------------------------------------------

        List<uint16_t> unpackContig10(const uint8_t *data, size_t bytes) {
                List<uint16_t> words;
                const size_t   wordCount = (bytes * 8) / 10;
                size_t         bitPos = 0;
                for (size_t i = 0; i < wordCount; ++i) {
                        uint16_t w = 0;
                        for (int b = 0; b < 10; ++b) {
                                const size_t  bp = bitPos + static_cast<size_t>(b);
                                const uint8_t byte = data[bp >> 3];
                                const int     bitInByte = 7 - static_cast<int>(bp & 7);
                                w = static_cast<uint16_t>((w << 1) | ((byte >> bitInByte) & 1));
                        }
                        words.pushToBack(w);
                        bitPos += 10;
                }
                return words;
        }

        Buffer packContig10(const List<uint16_t> &words) {
                const size_t totalBits = words.size() * 10;
                const size_t bytes = (totalBits + 7) / 8;
                Buffer       buf(bytes);
                uint8_t     *out = static_cast<uint8_t *>(buf.data());
                std::memset(out, 0, bytes);
                size_t bitPos = 0;
                for (size_t i = 0; i < words.size(); ++i) {
                        const uint16_t w = words[i] & 0x3FF;
                        for (int b = 9; b >= 0; --b) {
                                if ((w >> b) & 1) out[bitPos >> 3] |= static_cast<uint8_t>(1 << (7 - (bitPos & 7)));
                                ++bitPos;
                        }
                }
                buf.setSize(bytes);
                return buf;
        }

        // ----------------------------------------------------------------
        // ST 436M-2006 §4.4.4 10-bit sample coding: three 10-bit samples
        // share one big-endian UInt32 in bits 31..2; the two low bits are
        // zero.  The byte array is therefore inherently UInt32-aligned.
        // ----------------------------------------------------------------

        Buffer packSt436_10bit(const List<uint16_t> &words) {
                const size_t n = words.size();
                const size_t groups = (n + 2) / 3;
                Buffer       buf(groups * 4);
                uint8_t     *out = static_cast<uint8_t *>(buf.data());
                for (size_t g = 0; g < groups; ++g) {
                        auto at = [&](size_t k) -> uint32_t {
                                const size_t idx = g * 3 + k;
                                return idx < n ? static_cast<uint32_t>(words[idx] & 0x3FF) : 0u;
                        };
                        const uint32_t v = (at(0) << 22) | (at(1) << 12) | (at(2) << 2);
                        out[g * 4 + 0] = static_cast<uint8_t>(v >> 24);
                        out[g * 4 + 1] = static_cast<uint8_t>(v >> 16);
                        out[g * 4 + 2] = static_cast<uint8_t>(v >> 8);
                        out[g * 4 + 3] = static_cast<uint8_t>(v);
                }
                buf.setSize(groups * 4);
                return buf;
        }

        List<uint16_t> unpackSt436_10bit(const uint8_t *bytes, size_t byteLen, size_t sampleCount) {
                List<uint16_t> words;
                const size_t   groups = byteLen / 4;
                for (size_t g = 0; g < groups && words.size() < sampleCount; ++g) {
                        const uint32_t v = (static_cast<uint32_t>(bytes[g * 4 + 0]) << 24) |
                                           (static_cast<uint32_t>(bytes[g * 4 + 1]) << 16) |
                                           (static_cast<uint32_t>(bytes[g * 4 + 2]) << 8) |
                                           static_cast<uint32_t>(bytes[g * 4 + 3]);
                        const uint16_t w0 = static_cast<uint16_t>((v >> 22) & 0x3FF);
                        const uint16_t w1 = static_cast<uint16_t>((v >> 12) & 0x3FF);
                        const uint16_t w2 = static_cast<uint16_t>((v >> 2) & 0x3FF);
                        if (words.size() < sampleCount) words.pushToBack(w0);
                        if (words.size() < sampleCount) words.pushToBack(w1);
                        if (words.size() < sampleCount) words.pushToBack(w2);
                }
                return words;
        }

        // Number of payload bytes a coded ANC packet occupies (incl. the
        // UInt32 padding), derived from the sample count and coding mode.
        size_t paddedPayloadBytes(uint8_t coding, size_t sampleCount) {
                const bool tenBit = (coding == St436m::Coding10BitLuma || coding == St436m::Coding10BitChroma ||
                                     coding == St436m::Coding10BitLumaChroma);
                if (tenBit) return ((sampleCount + 2) / 3) * 4; // 3 samples per UInt32
                return ((sampleCount + 3) / 4) * 4;             // 8-bit: pad to UInt32
        }

        bool isTenBitCoding(uint8_t coding) {
                return coding == St436m::Coding10BitLuma || coding == St436m::Coding10BitChroma ||
                       coding == St436m::Coding10BitLumaChroma;
        }

        bool isChromaCoding(uint8_t coding) {
                return coding == St436m::Coding8BitChroma || coding == St436m::Coding10BitChroma ||
                       coding == St436m::Coding8BitChromaParityErr;
        }

        // Big-endian UInt16 read/write helpers over a byte cursor.
        void putU16(List<uint8_t> &out, uint16_t v) {
                out.pushToBack(static_cast<uint8_t>(v >> 8));
                out.pushToBack(static_cast<uint8_t>(v));
        }

        uint16_t getU16(const uint8_t *p) {
                return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        }

} // namespace

Buffer St436m::encodeFrame(const AncPacket::List &packets, const AncDesc &desc) {
        // Determine wrapping type from scan mode.
        const VideoScanMode sm = desc.scanMode();
        const bool          interlaced = (sm == VideoScanMode::Interlaced || sm == VideoScanMode::InterlacedEvenFirst ||
                                 sm == VideoScanMode::InterlacedOddFirst);

        List<uint8_t> out;
        // Reserve the 2-byte packet count; patched after the loop.
        putU16(out, 0);
        uint16_t emitted = 0;

        for (size_t i = 0; i < packets.size(); ++i) {
                const AncPacket &pkt = packets[i];
                if (pkt.transport() != AncTransport::St291) continue;
                const Buffer &data = pkt.data();
                if (!data || data.size() == 0) continue;

                List<uint16_t> words =
                        unpackContig10(static_cast<const uint8_t *>(data.data()), data.size());
                if (words.isEmpty()) continue;

                const uint8_t wrapType = pkt.st291FieldB() ? VancField2 : (interlaced ? VancField1 : VancProgressive);
                const uint8_t coding = pkt.st291CBit() ? Coding10BitChroma : Coding10BitLuma;

                putU16(out, pkt.st291Line());
                out.pushToBack(wrapType);
                out.pushToBack(coding);
                putU16(out, static_cast<uint16_t>(words.size()));
                Buffer         payload = packSt436_10bit(words);
                const uint8_t *pb = static_cast<const uint8_t *>(payload.data());
                for (size_t b = 0; b < payload.size(); ++b) out.pushToBack(pb[b]);
                ++emitted;
        }

        out[0] = static_cast<uint8_t>(emitted >> 8);
        out[1] = static_cast<uint8_t>(emitted);

        Buffer result(out.size());
        if (out.size() > 0) std::memcpy(result.data(), out.data(), out.size());
        result.setSize(out.size());
        return result;
}

Result<AncPacket::List> St436m::decodeFrame(const Buffer &sample) {
        AncPacket::List packets;
        if (!sample || sample.size() < 2) return makeResult(packets); // empty / no packets

        const uint8_t *p = static_cast<const uint8_t *>(sample.data());
        const size_t   len = sample.size();
        size_t         pos = 0;

        const uint16_t count = getU16(p);
        pos = 2;

        for (uint16_t i = 0; i < count; ++i) {
                if (pos + 6 > len) return makeError<AncPacket::List>(Error::TruncatedData);
                const uint16_t line = getU16(p + pos);
                const uint8_t  wrapType = p[pos + 2];
                const uint8_t  coding = p[pos + 3];
                const uint16_t sampleCount = getU16(p + pos + 4);
                pos += 6;

                const size_t payloadBytes = paddedPayloadBytes(coding, sampleCount);
                if (pos + payloadBytes > len) return makeError<AncPacket::List>(Error::TruncatedData);

                const bool fieldB = (wrapType == VancField2 || wrapType == HancField2);
                const bool cBit = isChromaCoding(coding);

                AncPacket pkt;
                if (isTenBitCoding(coding)) {
                        // Pass-through: repack the 10-bit words verbatim into the
                        // contiguous form AncPacket::data expects.
                        List<uint16_t> words = unpackSt436_10bit(p + pos, payloadBytes, sampleCount);
                        if (words.size() >= 2) {
                                const uint8_t did = static_cast<uint8_t>(words[0] & 0xFF);
                                const uint8_t sdid = static_cast<uint8_t>(words[1] & 0xFF);
                                AncFormat     fmt = AncFormat::fromSt291DidSdid(did, sdid);
                                Buffer        wire = packContig10(words);
                                pkt = AncPacket(fmt, AncTransport::St291, wire);
                                pkt.setSt291Framing(line, AncPacket().st291HOffset(), fieldB, cBit, 0);
                        }
                } else {
                        // 8-bit coding: each byte is the low 8 bits of a word
                        // (DID, SDID/DBN, DC, UDW…).  Parity and checksum were
                        // dropped on encode, so rebuild a canonical packet (which
                        // recomputes both) via St291Packet.
                        if (sampleCount >= 3) {
                                const uint8_t  did = p[pos];
                                const uint8_t  sdid = p[pos + 1];
                                const uint8_t  dc = p[pos + 2];
                                List<uint16_t> udw;
                                const size_t   udwCount = (sampleCount >= 3) ? (sampleCount - 3) : 0;
                                for (size_t u = 0; u < udwCount && (pos + 3 + u) < len; ++u)
                                        udw.pushToBack(p[pos + 3 + u]);
                                (void)dc;
                                St291Packet sp = St291Packet::buildRaw(did, sdid, udw, line,
                                                                       AncPacket().st291HOffset(), fieldB, cBit, 0);
                                pkt = sp.packet();
                        }
                }

                if (pkt.isValid() || (pkt.data() && pkt.data().size() > 0)) packets.pushToBack(pkt);
                pos += payloadBytes;
        }

        return makeResult(packets);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
