/**
 * @file      rtppayloadanc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtppayloadanc.h>

#include <cstring>

#include <promeki/ancmeta.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/rtppacket.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace {

        // Returns the on-wire RFC 8331 §2.2 record size for a single ANC
        // packet: 4-byte per-packet header + the canonical 10-bit packed
        // (DID, SDID, DataCount, UDW, Checksum) bytes from the packet's
        // @c data() buffer, rounded up to the next 32-bit boundary.
        size_t recordSize(const AncPacket &pkt) {
                const size_t body = pkt.data().size();
                const size_t total = RtpPayloadAnc::PerPacketHeaderSize + body;
                return (total + 3u) & ~size_t{3u};
        }

        // Picks the payload-header F-bit from the consensus across the
        // St291 packets in [startIdx, startIdx + n) (n is the *St291
        // count*, so non-St291 entries in that range are simply
        // skipped, not counted).  Conservative: only emits
        // @c InterlacedField2 when *every* St291 packet declares
        // @c FieldB = true; otherwise emits @c Progressive (which
        // RFC 8331 §2.1 also uses for unknown/mixed cases).
        RtpPayloadAnc::FieldIndication inferFieldIndication(
                const AncPacket *packets, size_t total, size_t startIdx, size_t n) {
                if (n == 0) return RtpPayloadAnc::FieldIndication::Progressive;
                bool   allFieldB = true;
                size_t seen = 0;
                for (size_t i = startIdx; i < total && seen < n; ++i) {
                        if (packets[i].transport() != AncTransport::St291) continue;
                        const bool fb = packets[i].meta()
                                                .get(AncMeta::St291::FieldB)
                                                .get<bool>();
                        if (!fb) {
                                allFieldB = false;
                                break;
                        }
                        ++seen;
                }
                return allFieldB ? RtpPayloadAnc::FieldIndication::InterlacedField2
                                 : RtpPayloadAnc::FieldIndication::Progressive;
        }

        // Writes one RFC 8331 §2.2 per-packet record at @p dst.  @p dst
        // must have at least @ref recordSize(pkt) bytes of available
        // space; the function zero-pads the trailing word-align bytes.
        void writeRecord(uint8_t *dst, const AncPacket &pkt) {
                const Metadata &meta = pkt.meta();
                const uint16_t  line = meta.get(AncMeta::St291::Line).get<uint16_t>();
                const uint16_t  hOffset = meta.get(AncMeta::St291::HOffset).get<uint16_t>();
                const bool      cBit = meta.get(AncMeta::St291::CBit).get<bool>();
                const uint8_t   streamNum = meta.get(AncMeta::St291::StreamNum).get<uint8_t>();
                // S-bit on iff the meta explicitly carries a non-zero
                // StreamNum.  Receivers that ignore S simply key off
                // StreamNum, which is preserved either way.
                const uint8_t sBit = streamNum != 0 ? 1u : 0u;

                // byte 0: C (1 bit) | Line[10:4] (7 bits)
                dst[0] = static_cast<uint8_t>(((cBit ? 1u : 0u) << 7) |
                                              ((line >> 4) & 0x7Fu));
                // byte 1: Line[3:0] (4 bits) | HOffset[11:8] (4 bits)
                dst[1] = static_cast<uint8_t>(((line & 0x0Fu) << 4) |
                                              ((hOffset >> 8) & 0x0Fu));
                // byte 2: HOffset[7:0]
                dst[2] = static_cast<uint8_t>(hOffset & 0xFFu);
                // byte 3: S (1 bit) | StreamNum (7 bits)
                dst[3] = static_cast<uint8_t>((sBit << 7) | (streamNum & 0x7Fu));

                const Buffer  &body = pkt.data();
                const size_t   bodyBytes = body.size();
                const uint8_t *bodyData = static_cast<const uint8_t *>(body.data());
                if (bodyBytes > 0 && bodyData != nullptr) {
                        std::memcpy(dst + RtpPayloadAnc::PerPacketHeaderSize, bodyData, bodyBytes);
                }
                // Zero the word-align trailing pad bytes (always 0..3
                // bytes wide; recordSize accounts for it).
                const size_t total = recordSize(pkt);
                const size_t pad = total - (RtpPayloadAnc::PerPacketHeaderSize + bodyBytes);
                if (pad > 0) {
                        std::memset(dst + RtpPayloadAnc::PerPacketHeaderSize + bodyBytes,
                                    0, pad);
                }
        }

        // Extracts a single 10-bit word from @p src starting at the
        // bit offset @p bitOffset (MSB-first stream order — matches
        // the packing in @c st291packet.cpp).
        uint16_t readWord10(const uint8_t *src, size_t bitOffset) {
                uint16_t v = 0;
                for (unsigned i = 0; i < 10u; ++i) {
                        const size_t   pos = bitOffset + i;
                        const size_t   byteIdx = pos >> 3;
                        const unsigned bitIdx = 7u - (pos & 7u);
                        v = static_cast<uint16_t>(
                                (v << 1) | ((src[byteIdx] >> bitIdx) & 1u));
                }
                return v;
        }

        // Greedy-plans RTP packet shapes for one frame: walks @p packets,
        // accumulates records into a current RTP packet, closes off when
        // the next record would push past @p maxPayload or when
        // @c ANC_Count would overflow 255.  Returns the per-RTP-packet
        // record-byte totals; the count of returned entries is the
        // number of RTP packets required.
        struct RtpPlan {
                size_t recordBytes = 0;  // sum of recordSize across this RTP packet
                size_t firstIdx = 0;     // first AncPacket index
                size_t count = 0;        // ANC_Count in this RTP packet
        };

        List<RtpPlan> planRtpPackets(const AncPacket *packets, size_t total, size_t maxPayload) {
                List<RtpPlan> plans;
                if (total == 0) return plans;
                if (maxPayload <= RtpPayloadAnc::PayloadHeaderSize) return plans;
                const size_t roomPerRtp = maxPayload - RtpPayloadAnc::PayloadHeaderSize;

                RtpPlan cur{0, 0, 0};
                for (size_t i = 0; i < total; ++i) {
                        if (packets[i].transport() != AncTransport::St291) continue;
                        const size_t rs = recordSize(packets[i]);
                        if (rs > roomPerRtp) {
                                // Record exceeds a whole RTP payload — skip
                                // it.  RFC 8331 records cannot span packets;
                                // at maxDataCount=255 a record is at most
                                // 328 bytes so this only fires on absurdly
                                // small MTUs.
                                continue;
                        }
                        const bool needsFlush = (cur.count > 0) &&
                                                ((cur.recordBytes + rs > roomPerRtp) ||
                                                 (cur.count >= 0xFFu));
                        if (needsFlush) {
                                plans.pushToBack(cur);
                                cur = RtpPlan{0, i, 0};
                        }
                        if (cur.count == 0) cur.firstIdx = i;
                        cur.recordBytes += rs;
                        cur.count += 1;
                }
                if (cur.count > 0) plans.pushToBack(cur);
                return plans;
        }

} // namespace

// ---------------------------------------------------------------------------
// RtpPayloadAnc — public surface
// ---------------------------------------------------------------------------

RtpPayloadAnc::RtpPayloadAnc(uint8_t payloadType)
    : _payloadType(payloadType) {}

RtpPacket::List RtpPayloadAnc::pack(const void *, size_t) {
        promekiWarn("RtpPayloadAnc::pack(void*, size_t) is not used; "
                    "call packAncFrame(AncPacket::List, ts) instead");
        return RtpPacket::List();
}

Buffer RtpPayloadAnc::unpack(const RtpPacket::List &) {
        promekiWarn("RtpPayloadAnc::unpack(RtpPacket::List) is not used; "
                    "call unpackAncPackets(in, out) instead");
        return Buffer();
}

RtpPayload::ValidateResult RtpPayloadAnc::validate(const Buffer &unpacked) {
        if (unpacked.size() < PayloadHeaderSize) return ValidateResult::DropSilently;
        const uint8_t *p = static_cast<const uint8_t *>(unpacked.data());
        if (p == nullptr) return ValidateResult::DropSilently;
        const uint16_t length = static_cast<uint16_t>((p[2] << 8) | p[3]);
        const uint8_t  ancCount = p[4];
        if (ancCount == 0) return ValidateResult::DropSilently;
        if (PayloadHeaderSize + length > unpacked.size()) {
                return ValidateResult::DropSilently;
        }
        return ValidateResult::Accept;
}

RtpPacket::List RtpPayloadAnc::packAncFrame(
        const AncPacket::List &packets, uint32_t rtpTimestamp) {
        RtpPacket::List out;
        if (packets.isEmpty()) return out;

        const size_t maxPayload = maxPayloadSize();
        if (maxPayload <= PayloadHeaderSize) return out;

        // Plan how many RTP packets we need and which AncPackets land in
        // each.  Records that don't fit in any RTP packet (over-MTU)
        // are silently skipped by planRtpPackets.
        const size_t total = packets.size();
        List<RtpPlan> plans = planRtpPackets(packets.data(), total, maxPayload);
        if (plans.isEmpty()) return out;

        // One shared Buffer for the whole frame's RTP packets.
        RtpPacket::SizeList sizes;
        sizes.reserve(plans.size());
        for (const auto &p : plans) {
                sizes.pushToBack(RtpPacket::HeaderSize + PayloadHeaderSize + p.recordBytes);
        }
        out = RtpPacket::createList(sizes);
        if (out.size() != plans.size()) return RtpPacket::List();

        for (size_t pi = 0; pi < plans.size(); ++pi) {
                const RtpPlan &plan = plans[pi];
                RtpPacket     &rtp = out[pi];
                rtp.setPayloadType(_payloadType);
                rtp.setTimestamp(rtpTimestamp);
                rtp.setMarker(pi + 1 == plans.size());

                uint8_t *pl = rtp.payload();
                if (pl == nullptr) return RtpPacket::List();

                // RFC 8331 §2.1 payload header.
                const FieldIndication f = inferFieldIndication(
                        packets.data(), total, plan.firstIdx, plan.count);
                pl[0] = 0;  // ESN high (deferred — see header notes)
                pl[1] = 0;  // ESN low
                pl[2] = static_cast<uint8_t>((plan.recordBytes >> 8) & 0xFFu);
                pl[3] = static_cast<uint8_t>(plan.recordBytes & 0xFFu);
                pl[4] = static_cast<uint8_t>(plan.count & 0xFFu);
                pl[5] = static_cast<uint8_t>((static_cast<uint8_t>(f) & 0x03u) << 6);
                pl[6] = 0;
                pl[7] = 0;

                // Records.
                uint8_t *cur = pl + PayloadHeaderSize;
                size_t   recsWritten = 0;
                for (size_t i = plan.firstIdx; i < total && recsWritten < plan.count; ++i) {
                        if (packets[i].transport() != AncTransport::St291) continue;
                        const size_t rs = recordSize(packets[i]);
                        // Defensive: planner already enforced fit; skip
                        // anything that would overflow.
                        if (cur + rs > pl + PayloadHeaderSize + plan.recordBytes) break;
                        writeRecord(cur, packets[i]);
                        cur += rs;
                        recsWritten += 1;
                }
        }
        return out;
}

Error RtpPayloadAnc::unpackAncPackets(const RtpPacket::List &in, AncPacket::List &out) {
        for (const RtpPacket &rtp : in) {
                if (rtp.isNull() || rtp.payloadSize() < PayloadHeaderSize) {
                        promekiWarn("RtpPayloadAnc::unpackAncPackets: payload shorter than header");
                        return Error::OutOfRange;
                }
                const uint8_t *p = rtp.payload();
                if (p == nullptr) {
                        promekiWarn("RtpPayloadAnc::unpackAncPackets: null payload pointer");
                        return Error::OutOfRange;
                }
                const size_t   payloadBytes = rtp.payloadSize();
                const uint16_t length = static_cast<uint16_t>((p[2] << 8) | p[3]);
                const uint8_t  ancCount = p[4];
                const uint8_t  fBits = static_cast<uint8_t>((p[5] >> 6) & 0x03u);
                const bool     fieldB = (fBits == static_cast<uint8_t>(FieldIndication::InterlacedField2));

                if (PayloadHeaderSize + length > payloadBytes) {
                        promekiWarn("RtpPayloadAnc::unpackAncPackets: declared Length %u "
                                    "exceeds RTP payload size %zu",
                                    static_cast<unsigned>(length), payloadBytes);
                        return Error::OutOfRange;
                }
                const uint8_t *recStart = p + PayloadHeaderSize;
                const uint8_t *recEnd = recStart + length;
                size_t         decoded = 0;
                while (decoded < ancCount) {
                        if (recStart + PerPacketHeaderSize > recEnd) {
                                promekiWarn("RtpPayloadAnc::unpackAncPackets: per-packet header "
                                            "runs past end of payload");
                                return Error::OutOfRange;
                        }
                        // RFC 8331 §2.2 per-packet header.
                        const bool     cBit = (recStart[0] & 0x80u) != 0;
                        const uint16_t line = static_cast<uint16_t>(
                                ((recStart[0] & 0x7Fu) << 4) | ((recStart[1] >> 4) & 0x0Fu));
                        const uint16_t hOffset = static_cast<uint16_t>(
                                ((recStart[1] & 0x0Fu) << 8) | recStart[2]);
                        const bool    sBit = (recStart[3] & 0x80u) != 0;
                        const uint8_t streamNum = static_cast<uint8_t>(
                                sBit ? (recStart[3] & 0x7Fu) : 0u);

                        // Need at least DID + SDID + DataCount + Checksum
                        // = 4 ten-bit words = 5 bytes of payload body
                        // before we can read DataCount.
                        const uint8_t *body = recStart + PerPacketHeaderSize;
                        if (body + 5 > recEnd) {
                                promekiWarn("RtpPayloadAnc::unpackAncPackets: record body "
                                            "shorter than header words");
                                return Error::OutOfRange;
                        }
                        const uint16_t didWord = readWord10(body, 0);
                        const uint16_t sdidWord = readWord10(body, 10);
                        const uint16_t dcWord = readWord10(body, 20);
                        const uint8_t  didByte = static_cast<uint8_t>(didWord & 0xFFu);
                        const uint8_t  sdidByte = static_cast<uint8_t>(sdidWord & 0xFFu);
                        const uint8_t  dataCount = static_cast<uint8_t>(dcWord & 0xFFu);

                        // Total body bytes for (DID, SDID, DC, UDWs, CS):
                        //   ceil((4 + DataCount) * 10 / 8)
                        const size_t wordCount = 4u + static_cast<size_t>(dataCount);
                        const size_t bodyBytes = (wordCount * 10u + 7u) / 8u;
                        if (body + bodyBytes > recEnd) {
                                promekiWarn("RtpPayloadAnc::unpackAncPackets: record body "
                                            "shorter than DataCount implies");
                                return Error::OutOfRange;
                        }
                        const size_t recordTotal = PerPacketHeaderSize + bodyBytes;
                        const size_t recordPadded = (recordTotal + 3u) & ~size_t{3u};
                        if (recStart + recordPadded > recEnd) {
                                // The record fits but its trailing pad
                                // overruns the declared length — accept
                                // anyway and treat the remainder as
                                // padding consumed by the payload's
                                // Length field.
                        }

                        Buffer data(bodyBytes);
                        Error  cpyErr = data.copyFrom(body, bodyBytes, 0);
                        if (cpyErr.isError()) return cpyErr;
                        data.setSize(bodyBytes);

                        Metadata meta;
                        meta.set(AncMeta::St291::Line, line);
                        meta.set(AncMeta::St291::HOffset, hOffset);
                        meta.set(AncMeta::St291::FieldB, fieldB);
                        meta.set(AncMeta::St291::CBit, cBit);
                        meta.set(AncMeta::St291::StreamNum, streamNum);

                        const AncFormat fmt = AncFormat::fromSt291DidSdid(didByte, sdidByte);

                        AncPacket pkt(fmt, AncTransport::St291, std::move(data), std::move(meta));
                        out.pushToBack(std::move(pkt));

                        recStart += recordPadded;
                        // If the padded record overran the declared end
                        // we just decoded the last record — break.
                        if (recStart >= recEnd) {
                                decoded += 1;
                                break;
                        }
                        decoded += 1;
                }
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
