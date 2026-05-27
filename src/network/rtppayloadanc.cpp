/**
 * @file      rtppayloadanc.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtppayloadanc.h>

#include <algorithm>
#include <cstring>

#include <promeki/ancformat.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/rtppacket.h>
#include <promeki/st291packet.h>

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

        // Picks the payload-header F-bit from the consensus across
        // the St291 records indexed by orderedIdx[start..start+n).
        // Conservative: only emits @c InterlacedField2 when *every*
        // St291 packet declares @c FieldB = true; otherwise emits
        // @c Progressive (which RFC 8331 §2.1 also uses for
        // unknown/mixed cases).
        RtpPayloadAnc::FieldIndication inferFieldIndication(
                const AncPacket *packets, const List<size_t> &orderedIdx,
                size_t start, size_t n) {
                if (n == 0) return RtpPayloadAnc::FieldIndication::Progressive;
                for (size_t k = start; k < start + n && k < orderedIdx.size(); ++k) {
                        if (!packets[orderedIdx[k]].st291FieldB()) {
                                return RtpPayloadAnc::FieldIndication::Progressive;
                        }
                }
                return RtpPayloadAnc::FieldIndication::InterlacedField2;
        }

        // Returns true when @p line is a real (non-sentinel) line
        // number per RFC 8331 §2.2 — anything below 0x7FD is a
        // proposed SDI location for ST 2110-40 §5.2.2 ordering.
        bool isExactLine(uint16_t line) {
                return line < St291Packet::LineLargerThan11Bits;
        }

        // Builds a stably-sorted list of indices into @p packets that
        // points at every eligible St291 entry, ordered by ascending
        // (Line, HOffset) per ST 2110-40 §5.2.2.  Non-St291 entries
        // are skipped (they cannot ride on RFC 8331).  ST 2110-40
        // §5.2.1 explicitly forbids audio-metadata and EDH packets on
        // this transport, so AncCategory::AudioMetadata-classified
        // packets are filtered with a warning.  EDH packets (DID 0xF4
        // / 0xF8 in ST 291-1 §11.3) are not yet modelled as a first-
        // class AncFormat; the filter is documented here so the
        // future EDH registry entry plugs in cleanly.  Sentinel
        // location values sort to the end because their numeric
        // value is larger than any real coordinate; stable sort
        // preserves input order within the sentinel group.
        List<size_t> orderedSt291Indices(const AncPacket *packets, size_t total) {
                List<size_t> idx;
                idx.reserve(total);
                for (size_t i = 0; i < total; ++i) {
                        if (packets[i].transport() != AncTransport::St291) continue;
                        const AncFormat fmt = packets[i].format();
                        if (fmt.isValid() && fmt.category() == AncCategory::AudioMetadata) {
                                promekiWarn("RtpPayloadAnc: dropping %s packet on "
                                            "ST 2110-40 egress (§5.2.1 forbids audio "
                                            "metadata on this transport)",
                                            fmt.name().cstr());
                                continue;
                        }
                        idx.pushToBack(i);
                }
                std::stable_sort(idx.begin(), idx.end(),
                                 [packets](size_t a, size_t b) {
                                         const uint16_t la = packets[a].st291Line();
                                         const uint16_t lb = packets[b].st291Line();
                                         if (la != lb) return la < lb;
                                         return packets[a].st291HOffset() < packets[b].st291HOffset();
                                 });
                (void)isExactLine; // tagged here for future StrictValidate use
                return idx;
        }

        // Writes one RFC 8331 §2.2 per-packet record at @p dst.  @p dst
        // must have at least @ref recordSize(pkt) bytes of available
        // space; the function zero-pads the trailing word-align bytes.
        void writeRecord(uint8_t *dst, const AncPacket &pkt) {
                const uint16_t line = pkt.st291Line();
                const uint16_t hOffset = pkt.st291HOffset();
                const bool     cBit = pkt.st291CBit();
                const uint8_t  streamNum = pkt.st291StreamNum();
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

        // Greedy-plans RTP packet shapes for one frame: walks the
        // ordered St291 index list, accumulates records into a
        // current RTP packet, closes off when the next record would
        // push past @p maxPayload or when @c ANC_Count would overflow
        // 255.  Returns the per-RTP-packet record-byte totals; the
        // count of returned entries is the number of RTP packets
        // required.
        //
        // @c firstIdx / @c count refer to positions inside the
        // caller-supplied @p orderedIdx list, NOT raw AncPacket
        // indices.  The emit path then walks @c orderedIdx[firstIdx
        // .. firstIdx + count) to materialise records in
        // ST 2110-40 §5.2.2 ascending (Line, HOffset) order.
        struct RtpPlan {
                size_t recordBytes = 0;  // sum of recordSize across this RTP packet
                size_t firstIdx = 0;     // first position in orderedIdx
                size_t count = 0;        // ANC_Count in this RTP packet
        };

        List<RtpPlan> planRtpPackets(const AncPacket *packets,
                                     const List<size_t> &orderedIdx,
                                     size_t              maxPayload) {
                List<RtpPlan> plans;
                if (orderedIdx.isEmpty()) return plans;
                if (maxPayload <= RtpPayloadAnc::PayloadHeaderSize) return plans;
                const size_t roomPerRtp = maxPayload - RtpPayloadAnc::PayloadHeaderSize;

                RtpPlan cur{0, 0, 0};
                for (size_t k = 0; k < orderedIdx.size(); ++k) {
                        const size_t i = orderedIdx[k];
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
                                cur = RtpPlan{0, k, 0};
                        }
                        if (cur.count == 0) cur.firstIdx = k;
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
        // ANC_Count=0 / Length=0 is the ST 2110-40 §5.5 keep-alive
        // shape — accept and let unpackAncPackets surface it as an
        // empty-records payload (Marker bit set, end-of-frame).
        // ANC_Count=0 with non-zero Length is malformed.
        if (ancCount == 0) {
                return length == 0 ? ValidateResult::Accept : ValidateResult::DropSilently;
        }
        // Reciprocal of the §5.5 keep-alive rule: ANC_Count != 0 with
        // Length == 0 advertises records but carries zero bytes of
        // record data — malformed.
        if (length == 0) return ValidateResult::DropSilently;
        if (PayloadHeaderSize + length > unpacked.size()) {
                return ValidateResult::DropSilently;
        }
        return ValidateResult::Accept;
}

void RtpPayloadAnc::setKeepAliveField(FieldIndication f) {
        if (f == FieldIndication::Invalid) {
                promekiWarn("RtpPayloadAnc::setKeepAliveField: rejecting "
                            "FieldIndication::Invalid (RFC 8331 §2.1 reserved value)");
                return;
        }
        _keepAliveField = f;
}

RtpPacket::List RtpPayloadAnc::packAncFrame(
        const AncPacket::List &packets, uint32_t rtpTimestamp) {
        RtpPacket::List out;

        const size_t maxPayload = maxPayloadSize();
        if (maxPayload <= PayloadHeaderSize) return out;

        // Plan how many RTP packets we need and which AncPackets land in
        // each.  Records that don't fit in any RTP packet (over-MTU)
        // are silently skipped by planRtpPackets.
        //
        // ST 2110-40 §5.2.2: senders that signal proposed SDI
        // location SHALL emit ANC packets in ascending
        // (Line_Number, Horizontal_Offset) order across the frame.
        // We stable-sort once up front; sentinel-location packets
        // (default 0x7FE / 0xFFF) end up at the tail because their
        // numeric value exceeds any real coordinate, with their
        // relative input order preserved.
        const size_t       total = packets.size();
        const List<size_t> orderedIdx = orderedSt291Indices(packets.data(), total);
        List<RtpPlan>      plans = planRtpPackets(packets.data(), orderedIdx, maxPayload);

        // ST 2110-40 §5.5 keep-alive: when the planner produced no
        // records (empty input or no St291 packets), emit a single
        // RTP packet with ANC_Count=0, Length=0, Marker=1, F set to
        // the session's keep-alive value, and no per-packet records
        // (RFC 8331 §2.2 forbids word_align padding on this shape).
        if (plans.isEmpty()) {
                RtpPacket::SizeList sizes;
                sizes.pushToBack(RtpPacket::HeaderSize + PayloadHeaderSize);
                RtpPacket::List ka = RtpPacket::createList(sizes);
                if (ka.size() != 1) return out;
                RtpPacket &rtp = ka[0];
                rtp.setPayloadType(_payloadType);
                rtp.setTimestamp(rtpTimestamp);
                rtp.setMarker(true);
                uint8_t *pl = rtp.payload();
                if (pl == nullptr) return out;
                pl[0] = 0;  // ESN high
                pl[1] = 0;  // ESN low
                pl[2] = 0;  // Length hi
                pl[3] = 0;  // Length lo
                pl[4] = 0;  // ANC_Count
                pl[5] = static_cast<uint8_t>(
                        (static_cast<uint8_t>(_keepAliveField) & 0x03u) << 6);
                pl[6] = 0;
                pl[7] = 0;
                return ka;
        }

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
                        packets.data(), orderedIdx, plan.firstIdx, plan.count);
                pl[0] = 0;  // ESN high (deferred — see header notes)
                pl[1] = 0;  // ESN low
                pl[2] = static_cast<uint8_t>((plan.recordBytes >> 8) & 0xFFu);
                pl[3] = static_cast<uint8_t>(plan.recordBytes & 0xFFu);
                pl[4] = static_cast<uint8_t>(plan.count & 0xFFu);
                pl[5] = static_cast<uint8_t>((static_cast<uint8_t>(f) & 0x03u) << 6);
                pl[6] = 0;
                pl[7] = 0;

                // Records walked through the sorted index list so the
                // proposed SDI locations land on the wire in ascending
                // (Line, HOffset) order (ST 2110-40 §5.2.2).
                uint8_t *cur = pl + PayloadHeaderSize;
                for (size_t k = plan.firstIdx;
                     k < plan.firstIdx + plan.count && k < orderedIdx.size(); ++k) {
                        const size_t   srcIdx = orderedIdx[k];
                        const AncPacket &pkt = packets[srcIdx];
                        const size_t   rs = recordSize(pkt);
                        // Defensive: planner already enforced fit; skip
                        // anything that would overflow.
                        if (cur + rs > pl + PayloadHeaderSize + plan.recordBytes) break;
                        writeRecord(cur, pkt);
                        cur += rs;
                }
        }
        return out;
}

Error RtpPayloadAnc::unpackAncPackets(const RtpPacket::List &in, AncPacket::List &out, AncChecksumPolicy policy) {
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

                // RFC 8331 §2.1: F-bit value 0b01 is explicitly reserved.
                // Receivers SHOULD ignore an ANC data packet with this
                // F value and continue processing the rest of the
                // datagram list.  We skip the entire RTP packet's
                // records — there is no per-record F-bit in §2.2, so
                // the §2.1 invalid value taints the whole payload.
                if (fBits == static_cast<uint8_t>(FieldIndication::Invalid)) {
                        promekiWarn("RtpPayloadAnc::unpackAncPackets: F-bit = 0b01 "
                                    "(RFC 8331 §2.1 reserved); skipping payload");
                        continue;
                }

                // RFC 8331 §2.1 / ST 2110-40 §5.5: ANC_Count=0 with
                // Length=0 is the keep-alive shape — no records to
                // decode, just acknowledge end-of-frame.  Non-zero
                // Length with ANC_Count=0 is malformed.
                if (ancCount == 0) {
                        if (length != 0) {
                                promekiWarn("RtpPayloadAnc::unpackAncPackets: ANC_Count=0 "
                                            "with non-zero Length=%u (malformed)",
                                            static_cast<unsigned>(length));
                                return Error::OutOfRange;
                        }
                        continue;
                }

                const bool fieldB = (fBits == static_cast<uint8_t>(FieldIndication::InterlacedField2));

                if (PayloadHeaderSize + length > payloadBytes) {
                        promekiWarn("RtpPayloadAnc::unpackAncPackets: declared Length %u "
                                    "exceeds RTP payload size %zu",
                                    static_cast<unsigned>(length), payloadBytes);
                        return Error::OutOfRange;
                }
                // Reserve output capacity for this datagram's records so
                // pushToBack doesn't trigger a List reallocation halfway
                // through the loop.  Worst case the reservation is
                // slightly larger than needed (records that fail the
                // padded-overrun check exit the loop early), but it
                // never overshoots ANC_Count itself.
                out.reserve(out.size() + ancCount);
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
                                // RFC 8331 §2.1 / §2.2: the payload's
                                // Length field already covers all
                                // word_align padding.  A record whose
                                // padded extent runs past the declared
                                // Length is malformed wire bytes — warn
                                // and keep parsing the record body (the
                                // un-padded bytes are still valid).
                                promekiWarn("RtpPayloadAnc::unpackAncPackets: record "
                                            "trailing pad overruns declared Length "
                                            "(record needs %zu bytes, %td remaining)",
                                            recordPadded,
                                            static_cast<ptrdiff_t>(recEnd - recStart));
                        }

                        // F9 hot-path alloc: ANC records are tiny (DC ≤
                        // 255 → max 328 bytes).  Buffer's default
                        // alignment is page-size (4 KB) which produces
                        // a 4 KB allocation per record; on a 20-packet
                        // HD-60 frame that is 80 KB of allocation
                        // churn per RTP unpack.  16-byte alignment is
                        // plenty for the trailing SIMD reads the
                        // codecs do and matches malloc's natural
                        // alignment, so the allocator falls into its
                        // fast small-block path.
                        Buffer data(bodyBytes, /*align=*/16);
                        Error  cpyErr = data.copyFrom(body, bodyBytes, 0);
                        if (cpyErr.isError()) return cpyErr;
                        data.setSize(bodyBytes);

                        const AncFormat fmt = AncFormat::fromSt291DidSdid(didByte, sdidByte);

                        AncPacket pkt(fmt, AncTransport::St291, std::move(data));
                        // F9 hot-path: one CoW detach for all five
                        // framing fields instead of five.
                        pkt.setSt291Framing(line, hOffset, fieldB, cBit, streamNum);
                        // Apply the §6.4 Checksum_Word policy at the
                        // promotion boundary so a StrictValidate session
                        // refuses records whose stored checksum does not
                        // match the recomputed one (RFC 8331 §7 SHOULD
                        // check).  PreserveOrRecompute and AlwaysRecompute
                        // tolerate the wire bytes as-is on the parse
                        // path; the distinction matters only when the
                        // packet is later re-emitted, so we skip the
                        // full St291Packet::from() wrapper under those
                        // policies (the loop already validated structure
                        // and length).
                        if (policy == AncChecksumPolicy::StrictValidate) {
                                Result<St291Packet> rp = St291Packet::from(pkt, policy);
                                if (isError(rp)) {
                                        promekiWarn("RtpPayloadAnc::unpackAncPackets: record "
                                                    "rejected by checksum policy %s "
                                                    "(DID=0x%02X SDID=0x%02X)",
                                                    policy.toString().cstr(),
                                                    static_cast<unsigned>(didByte),
                                                    static_cast<unsigned>(sdidByte));
                                        return error(rp);
                                }
                        }
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
                // RFC 8331 §7 SHOULD-check: the loop may have exited
                // via the pad-overrun break before decoding every
                // record the payload claimed.  Surface that as a warn
                // (the records we did decode are valid; the sender
                // miscounted or truncated).
                if (decoded != ancCount) {
                        promekiWarn("RtpPayloadAnc::unpackAncPackets: decoded %zu "
                                    "records but ANC_Count=%u declared",
                                    decoded, static_cast<unsigned>(ancCount));
                }
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
