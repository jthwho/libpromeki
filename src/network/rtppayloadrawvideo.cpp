/**
 * @file      rtppayloadrawvideo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtppayloadrawvideo.h>
#include <promeki/logger.h>
#include <promeki/list.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

// Plain record describing one Sample Row Data Header / Segment pair
// gathered in pass 1 of pack(): which row of the active sample array
// it covers, the pixel offset of its first sample within the row, and
// the byte length of the sample data that follows in the payload.
namespace {
struct PendingSrd {
                size_t  rowIndex;          // Zero-based SRD Row Number (within its field/segment).
                size_t  pixelOffset;       // Pixel offset within the row.
                size_t  srcByteOffset;     // Source-buffer byte offset for the chunk.
                size_t  dataBytes;         // Sample data byte length (pgroup-aligned).
                uint8_t fieldIndex;        // 0 for first field/segment, 1 for second; F-bit on emit.
};

// (sourceWireRow → fieldIndex, srdRowInField) mapping for one frame.
// Built once in pack() based on the active VideoScanMode and walked
// to produce PendingSrd records.
struct FieldRowAssignment {
                int sourceWireRow;   // Index into the flat source buffer (0-based).
                int fieldIndex;      // 0 or 1.
                int srdRowInField;   // SRD Row Number stamped on the wire.
};
} // namespace

RtpPayloadRawVideo::RtpPayloadRawVideo(int width, int height, int bitsPerPixel, int pgroupBytes, int rowsPerSrd)
    : _width(width), _height(height), _bitsPerPixel(bitsPerPixel),
      _pgroupBytes(pgroupBytes > 0 ? pgroupBytes : (bitsPerPixel / 8)), _rowsPerSrd(rowsPerSrd > 0 ? rowsPerSrd : 1) {}

size_t RtpPayloadRawVideo::bpmBlocksPerPacket(size_t maxPayloadSize) {
        // ST 2110-20 §6.3.3 forbids Extended UDP Size Limit in BPM, so
        // the block count is capped at the Standard UDP value of 7
        // even when a larger maxPayloadSize is offered.
        const size_t blocksByBudget = maxPayloadSize / BpmBlockOctets;
        if (blocksByBudget == 0) return 0;
        return blocksByBudget > BpmStandardBlocksPerPacket ? BpmStandardBlocksPerPacket : blocksByBudget;
}

size_t RtpPayloadRawVideo::bpmTargetPayloadSize(size_t maxPayloadSize) {
        return bpmBlocksPerPacket(maxPayloadSize) * BpmBlockOctets;
}

RtpPacket RtpPayloadRawVideo::makeKeepAlive(uint32_t packetCounter, uint16_t srdRow, bool fieldBit,
                                            uint8_t payloadType) {
        // Wire layout: ESN (2) + one SRD Header (6).  C=0 since this is
        // the only SRD; Length=0 since no sample data follows; F-bit and
        // Row Number per the caller.  The marker bit is set so the
        // receiver's frame-boundary detector fires on this packet just
        // like on the tail packet of a normal field/frame (§6.1.2).
        const size_t payloadBytes = ExtSeqSize + SrdHeaderSize;
        const size_t pktBytes = RtpPacket::HeaderSize + payloadBytes;
        Buffer       buf(pktBytes);
        buf.setSize(pktBytes);
        uint8_t *data = static_cast<uint8_t *>(buf.data());
        std::memset(data, 0, RtpPacket::HeaderSize); // RTP header placeholder

        uint8_t       *payload = data + RtpPacket::HeaderSize;
        const uint16_t esn = static_cast<uint16_t>((packetCounter >> 16) & 0xFFFFu);
        payload[0] = static_cast<uint8_t>((esn >> 8) & 0xFFu);
        payload[1] = static_cast<uint8_t>(esn & 0xFFu);

        const uint16_t rowField = static_cast<uint16_t>(srdRow & 0x7FFFu);
        // SRD Length = 0.
        payload[2] = 0;
        payload[3] = 0;
        // F (1) + Row Number (15).
        payload[4] = static_cast<uint8_t>((rowField >> 8) & 0x7Fu) | (fieldBit ? 0x80u : 0x00u);
        payload[5] = static_cast<uint8_t>(rowField & 0xFFu);
        // C (1) + SRD Offset (15) = 0.
        payload[6] = 0;
        payload[7] = 0;

        RtpPacket pkt(buf, 0, pktBytes);
        pkt.setPayloadType(payloadType);
        pkt.setMarker(true);
        return pkt;
}

RtpPacket::List RtpPayloadRawVideo::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) {
                promekiWarnThrottled(5000, "RtpPayloadRawVideo::pack invalid input (size=%zu data=%p)", size,
                                     mediaData);
                return packets;
        }

        const size_t maxPayload = maxPayloadSize();
        const size_t minOverhead = ExtSeqSize + SrdHeaderSize; // ESN + one SRD header
        if (maxPayload <= minOverhead) {
                promekiWarnOnce("RtpPayloadRawVideo::pack maxPayloadSize=%zu too small for ST 2110-20 overhead=%zu",
                                maxPayload, minOverhead);
                return packets;
        }

        const size_t pg = static_cast<size_t>(_pgroupBytes);
        if (pg == 0) {
                promekiWarnOnce("RtpPayloadRawVideo::pack pgroupBytes=0 — stream not initialised");
                return packets;
        }

        const size_t bytesPerLine = static_cast<size_t>(_width) * static_cast<size_t>(_bitsPerPixel) / 8;
        if (bytesPerLine == 0 || (bytesPerLine % pg) != 0) {
                // Last pgroup of a row may be partial per RFC 4175
                // §4.3 / ST 2110-20 §6.2.1 — the wire data length
                // must still be pgroup-aligned and the sender pads
                // the trailing fractional pgroup with zeros.  E20a
                // keeps the simpler precondition (whole-pgroup row)
                // until the pgroup-matrix work lands in E20b — only
                // 8-bit RGB / 8-bit UYVY hit this path today and
                // both align cleanly.
                promekiWarnOnce("RtpPayloadRawVideo::pack bytesPerLine=%zu is not a multiple of pgroup=%zu",
                                bytesPerLine, pg);
                return packets;
        }

        // Block Packing Mode (§6.3.3) — every packet's RTP payload is
        // an integer multiple of @ref BpmBlockOctets; the last packet
        // of the field/frame is zero-padded to the same size.  BPM
        // requires the pgroup size to divide the block size; for
        // formats where it does not (pgroup-8: 4:2:2/16, 4:2:2/16f)
        // we fall back to GPM with a one-shot warning per
        // §6.3.3 / §6.2.1.
        bool         bpm = (_packingMode == St2110PackingMode::Bpm);
        if (bpm && (BpmBlockOctets % pg) != 0) {
                promekiWarnOnce("RtpPayloadRawVideo::pack BPM requested but pgroup=%zu does not divide %zu — "
                                "falling back to GPM",
                                pg, BpmBlockOctets);
                bpm = false;
        }
        const size_t bpmTarget = bpm ? bpmTargetPayloadSize(maxPayload) : 0;
        if (bpm && bpmTarget < BpmBlockOctets) {
                promekiWarnOnce("RtpPayloadRawVideo::pack BPM requested but maxPayload=%zu < %zu — "
                                "falling back to GPM",
                                maxPayload, BpmBlockOctets);
                bpm = false;
        }

        // The packet-size budget the planner uses to pack SRDs.  In
        // GPM this is the MTU-derived maxPayload; in BPM it is the
        // chosen 180-octet block multiple (≤ maxPayload) and every
        // packet is *exactly* that size on the wire (zero-padded to
        // fill).
        const size_t effectivePayload = bpm ? bpmTarget : maxPayload;

        // Single-SRD-per-packet pixel-data ceiling: every chunk is at
        // most this many bytes and is a whole number of pgroups.  Used
        // both as the per-chunk cap in pass 1 (when fragmenting a long
        // line) and as the upper bound on a packet's sample data when
        // the packet ends up with only one SRD.
        const size_t maxChunkBytesSingleSrd = ((effectivePayload - minOverhead) / pg) * pg;
        if (maxChunkBytesSingleSrd == 0) {
                promekiWarnOnce("RtpPayloadRawVideo::pack pgroup=%zu > payloadBudget-overhead=%zu — MTU too small",
                                pg, effectivePayload - minOverhead);
                return packets;
        }

        // -----------------------------------------------------------
        // Pass 0 — assign source wire-rows to fields/segments per the
        // active @ref VideoScanMode (§6.1.5).
        //
        //  - Progressive (or Unknown): single field, source rows in
        //    natural order.
        //  - Interlaced / InterlacedEvenFirst: field 0 = even-parity
        //    rows (0, 2, 4, …); field 1 = odd-parity (1, 3, 5, …).
        //    For odd height, even-parity has ⌈H/2⌉ rows so the
        //    temporally-first field carries the extra line.
        //  - InterlacedOddFirst: field 0 = odd-parity; field 1 =
        //    even-parity (note: odd-height + OddFirst is degenerate —
        //    interlaced source heights are always even in practice).
        //  - PsF: field 0 = rows 0..⌈H/2⌉−1; field 1 = ⌈H/2⌉..H−1;
        //    odd-height first segment carries the extra row.
        //
        // §6.2.5: 4:2:0 (rowsPerSrd > 1) is progressive-only.  If a
        // non-progressive scan mode is combined with 4:2:0 we log a
        // one-shot warning and treat the frame as Progressive — the
        // SDP-level guard in St2110Video catches this at session-
        // configure time; this is just defensive at the packetizer.
        // -----------------------------------------------------------
        const int wireRows = _height / _rowsPerSrd;
        const bool scanIsInterlaced = _scanMode.isInterlaced();
        const bool scanIsPsf = (_scanMode == VideoScanMode::PsF);
        bool       splitFrame = (scanIsInterlaced || scanIsPsf);
        if (splitFrame && _rowsPerSrd > 1) {
                promekiWarnOnce("RtpPayloadRawVideo::pack 4:2:0 (rowsPerSrd=%d) cannot use interlaced/PsF "
                                "per §6.2.5 — packing this frame as Progressive",
                                _rowsPerSrd);
                splitFrame = false;
        }
        const bool oddFirst = splitFrame && scanIsInterlaced
                              && (_scanMode == VideoScanMode::InterlacedOddFirst);

        promeki::List<FieldRowAssignment> assignments;
        if (!splitFrame) {
                for (int r = 0; r < wireRows; r++) {
                        assignments.pushToBack({r, 0, r});
                }
        } else if (scanIsInterlaced) {
                // Two passes: field 0 first, then field 1.  Parity
                // selects which source rows are in which field.
                const int field0Parity = oddFirst ? 1 : 0;
                for (int field = 0; field < 2; field++) {
                        const int parity = (field == 0) ? field0Parity : (1 - field0Parity);
                        int       srdRow = 0;
                        for (int r = parity; r < wireRows; r += 2) {
                                assignments.pushToBack({r, field, srdRow++});
                        }
                }
        } else { // PsF
                const int topCount = (wireRows + 1) / 2; // ⌈H/2⌉ — first segment gets extra.
                int       srdRow = 0;
                for (int r = 0; r < topCount; r++) {
                        assignments.pushToBack({r, 0, srdRow++});
                }
                srdRow = 0;
                for (int r = topCount; r < wireRows; r++) {
                        assignments.pushToBack({r, 1, srdRow++});
                }
        }

        // -----------------------------------------------------------
        // Pass 1 — gather one PendingSrd record per (wire-row, chunk)
        // span.
        //
        // Long rows that exceed maxChunkBytesSingleSrd are split into
        // multiple SRDs in offset order; short rows produce one SRD
        // each.  Pass 2 then greedily packs SRDs into packets up to
        // the 3-per-packet ceiling and the maxPayloadSize byte cap.
        //
        // For 4:2:0 (rowsPerSrd=2 per §6.2.5) one wire row = one row
        // pair.  SRD Row Number maps to the *first luma row* of the
        // pair (= wire_row × rowsPerSrd).  In progressive scan
        // (the only valid mode for 4:2:0) the field index is always
        // 0 so the F-bit is 0.  In interlaced/PsF the SRD Row Number
        // is the field/segment-relative index (assignment.srdRowInField).
        // -----------------------------------------------------------
        promeki::List<PendingSrd> srds;
        for (size_t ai = 0; ai < assignments.size(); ai++) {
                const FieldRowAssignment &a = assignments.at(ai);
                size_t remaining = bytesPerLine;
                size_t lineOffset = 0; // Byte offset into the wire row.
                const size_t rowBase = static_cast<size_t>(a.sourceWireRow) * bytesPerLine;
                const size_t srdRowNumber =
                        static_cast<size_t>(a.srdRowInField) * static_cast<size_t>(_rowsPerSrd);
                while (remaining > 0) {
                        const size_t chunk = (remaining > maxChunkBytesSingleSrd)
                                                     ? maxChunkBytesSingleSrd
                                                     : remaining;
                        PendingSrd s;
                        s.rowIndex = srdRowNumber;
                        s.pixelOffset = lineOffset * 8 / static_cast<size_t>(_bitsPerPixel);
                        s.srcByteOffset = rowBase + lineOffset;
                        s.dataBytes = chunk;
                        s.fieldIndex = static_cast<uint8_t>(a.fieldIndex);
                        srds.pushToBack(s);
                        lineOffset += chunk;
                        remaining -= chunk;
                }
        }

        if (srds.isEmpty()) return packets;

        // -----------------------------------------------------------
        // Pass 2 — group SRDs into packets greedily.  Each packet
        // ranges over [packetStart, packetEnd) indices in @c srds.
        // packetByteEstimate keeps the running total (excluding RTP
        // header) so we can decide whether the next SRD still fits.
        // The pixel-data layout is contiguous within the packet so
        // we sum srd.dataBytes too.
        // -----------------------------------------------------------
        promeki::List<size_t> packetStarts;
        packetStarts.pushToBack(0);
        size_t curOverhead = ExtSeqSize + SrdHeaderSize; // bytes consumed by ESN + first SRD hdr
        size_t curData = srds.at(0).dataBytes;
        size_t curSrdCount = 1;
        uint8_t curField = srds.at(0).fieldIndex;
        for (size_t i = 1; i < srds.size(); i++) {
                const size_t nextHdr = SrdHeaderSize;
                const size_t nextData = srds.at(i).dataBytes;
                const bool   srdCapHit = (curSrdCount >= MaxSrdsPerPacket);
                const bool   byteCapHit = (curOverhead + nextHdr + curData + nextData > effectivePayload);
                // Never mix SRDs from different fields/segments in
                // one packet — the F-bit is per-SRD on the wire but
                // marker-bit semantics (§6.1.2) require a clean
                // field/segment boundary at the packet level.
                const bool fieldBoundary = (srds.at(i).fieldIndex != curField);
                if (srdCapHit || byteCapHit || fieldBoundary) {
                        packetStarts.pushToBack(i);
                        curOverhead = ExtSeqSize + SrdHeaderSize;
                        curData = nextData;
                        curSrdCount = 1;
                        curField = srds.at(i).fieldIndex;
                } else {
                        curOverhead += nextHdr;
                        curData += nextData;
                        curSrdCount++;
                }
        }

        // -----------------------------------------------------------
        // Pass 3 — allocate one shared Buffer covering every emitted
        // packet, write the headers + sample data into it, and
        // construct one RtpPacket per packetStarts entry.
        //
        // The Buffer is sized to the worst case (every packet maxed
        // out at maxPayload bytes including the RTP header
        // placeholder) and trimmed to the actual emitted size for
        // each individual packet via the RtpPacket constructor's
        // size argument.
        // -----------------------------------------------------------
        const size_t maxPktSize = RtpPacket::HeaderSize + effectivePayload;
        const size_t totalPackets = packetStarts.size();
        auto         buf = Buffer(totalPackets * maxPktSize);
        buf.setSize(totalPackets * maxPktSize);
        uint8_t       *bufData = static_cast<uint8_t *>(buf.data());
        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t         bufOffset = 0;

        for (size_t p = 0; p < totalPackets; p++) {
                const size_t srdStart = packetStarts.at(p);
                const size_t srdEnd = (p + 1 < totalPackets) ? packetStarts.at(p + 1) : srds.size();
                const size_t srdCount = srdEnd - srdStart;

                uint8_t *pkt = bufData + bufOffset;
                std::memset(pkt, 0, RtpPacket::HeaderSize); // RTP header placeholder
                uint8_t *payload = pkt + RtpPacket::HeaderSize;

                // Extended Sequence Number — high 16 bits of the
                // 32-bit packet counter (§6.1.4).
                const uint16_t esn = static_cast<uint16_t>((_packetCounter >> 16) & 0xFFFFu);
                payload[0] = static_cast<uint8_t>((esn >> 8) & 0xFFu);
                payload[1] = static_cast<uint8_t>(esn & 0xFFu);

                // SRD Headers (1..3) followed by their sample data.
                // Layout: ESN(2) + [SRD hdr(6)] * N + concatenated
                // sample data segments in SRD order.  F-bit is per-
                // SRD (carries the assigned field index in
                // Interlaced/PsF; otherwise the manual @ref _fieldBit
                // override is used uniformly in Progressive).
                uint8_t *hdrCursor = payload + ExtSeqSize;
                size_t   dataCursor = ExtSeqSize + srdCount * SrdHeaderSize;
                for (size_t k = 0; k < srdCount; k++) {
                        const PendingSrd &s = srds.at(srdStart + k);
                        const bool        isLast = (k + 1 == srdCount);
                        const uint16_t    lenField = static_cast<uint16_t>(s.dataBytes & 0xFFFFu);
                        const uint16_t    rowField = static_cast<uint16_t>(s.rowIndex & 0x7FFFu);
                        const uint16_t    offField = static_cast<uint16_t>(s.pixelOffset & 0x7FFFu);
                        const uint8_t     fBitMask =
                                splitFrame ? (s.fieldIndex ? 0x80u : 0x00u) : (_fieldBit ? 0x80u : 0x00u);
                        // Length (16 bits, big-endian).
                        hdrCursor[0] = static_cast<uint8_t>((lenField >> 8) & 0xFFu);
                        hdrCursor[1] = static_cast<uint8_t>(lenField & 0xFFu);
                        // F (1) + Row Number (15).
                        hdrCursor[2] = static_cast<uint8_t>((rowField >> 8) & 0x7Fu) | fBitMask;
                        hdrCursor[3] = static_cast<uint8_t>(rowField & 0xFFu);
                        // C (1) + Offset (15).  C is set on every
                        // SRD except the last one in the packet
                        // (§6.1.4).
                        const uint8_t cBit = isLast ? 0x00u : 0x80u;
                        hdrCursor[4] = static_cast<uint8_t>((offField >> 8) & 0x7Fu) | cBit;
                        hdrCursor[5] = static_cast<uint8_t>(offField & 0xFFu);
                        hdrCursor += SrdHeaderSize;

                        // Sample data segment for this SRD.
                        if (s.srcByteOffset + s.dataBytes <= size) {
                                std::memcpy(payload + dataCursor, src + s.srcByteOffset, s.dataBytes);
                        } else {
                                // Source truncated mid-row — emit
                                // zeros so the wire payload is still
                                // pgroup-aligned and Length-honoured;
                                // the receiver's pgroup fill rule
                                // (§6.2.1) accepts trailing zero
                                // sample positions.
                                std::memset(payload + dataCursor, 0, s.dataBytes);
                        }
                        dataCursor += s.dataBytes;
                }

                // Block Packing Mode: pad every packet's RTP payload
                // (including the last one of the field/frame) up to
                // @ref bpmTarget octets with zeros (§6.3.3).  The
                // depacketizer walks SRD Length fields and ignores
                // anything past the last SRD's data, so the padding
                // is invisible on the receive side.
                size_t finalPayloadBytes = dataCursor;
                if (bpm && dataCursor < bpmTarget) {
                        std::memset(payload + dataCursor, 0, bpmTarget - dataCursor);
                        finalPayloadBytes = bpmTarget;
                }

                // §6.1.2: marker bit fires on the last packet of each
                // field/segment for interlaced/PsF.  Pre-stamp here
                // when this packet is the tail of its field — the
                // TX-thread setMarker(isLast && markerOnLast) check
                // OR-merges with the pre-stamp so a per-frame "last
                // packet" mark from the session is preserved too.
                //
                // Tail-of-field detection also drives the §6.3.2 GPM
                // short-packet guard below.
                const uint8_t thisField = srds.at(srdEnd - 1).fieldIndex;
                const bool    nextStartsNewField =
                        (srdEnd < srds.size()) && (srds.at(srdEnd).fieldIndex != thisField);
                const bool    endOfFrame = (srdEnd == srds.size());
                const bool    tailOfFieldOrFrame = endOfFrame || (splitFrame && nextStartsNewField);

                const size_t pktSize = RtpPacket::HeaderSize + finalPayloadBytes;
                RtpPacket    rtpPkt(buf, bufOffset, pktSize);
                if (splitFrame && (nextStartsNewField || endOfFrame)) rtpPkt.setMarker(true);

                // §6.3.2: GPM packets below GpmShortPacketFloor octets
                // should be avoided except at the end of a field/frame.
                // BPM packets are fixed-size by definition and exempt;
                // a tail packet may be arbitrarily short.  Anything
                // else is a configuration smell (typically MTU set
                // below ~1100 octets) — log once per session so the
                // operator sees the diagnostic.
                if (!bpm && !tailOfFieldOrFrame && finalPayloadBytes < GpmShortPacketFloor) {
                        promekiWarnOnce("RtpPayloadRawVideo::pack GPM emitted a %zu-octet non-tail packet "
                                        "(< §6.3.2 floor of %zu) — MTU=%zu likely too small for the "
                                        "stream's pgroup=%zu / row length=%zu",
                                        finalPayloadBytes, GpmShortPacketFloor, maxPayload, pg, bytesPerLine);
                }

                packets.pushToBack(rtpPkt);
                bufOffset += pktSize;
                _packetCounter += static_cast<uint32_t>(srdCount > 0 ? 1u : 0u);
        }

        return packets;
}

Buffer RtpPayloadRawVideo::unpack(const RtpPacket::List &packets) {
        const size_t bytesPerLine = static_cast<size_t>(_width) * static_cast<size_t>(_bitsPerPixel) / 8;
        const int    wireRows = _height / _rowsPerSrd;
        const size_t frameSize = bytesPerLine * static_cast<size_t>(wireRows);
        Buffer       result(frameSize);
        result.setSize(frameSize);
        std::memset(result.data(), 0, frameSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());

        // Receiver-side mirror of pack()'s field/segment assignment
        // (see §6.1.5 + §6.2.5 in pack()).  In Interlaced / PsF the
        // F-bit on each SRD tells us which field/segment the row
        // belongs to; combined with the SRD Row Number (which is
        // field-relative on the wire) we recover the original source
        // wire-row index.
        const bool scanIsInterlaced = _scanMode.isInterlaced();
        const bool scanIsPsf = (_scanMode == VideoScanMode::PsF);
        const bool splitFrame = (scanIsInterlaced || scanIsPsf) && _rowsPerSrd <= 1;
        const bool oddFirst = splitFrame && scanIsInterlaced
                              && (_scanMode == VideoScanMode::InterlacedOddFirst);
        // PsF top-segment row count (= ⌈H/2⌉) — needed to map field-1
        // SRDs back to source wire rows.
        const int psfTopCount = (wireRows + 1) / 2;

        for (const auto &pkt : packets) {
                if (pkt.isNull() || pkt.payloadSize() <= ExtSeqSize) continue;
                const uint8_t *pl = pkt.payload();
                const size_t   payloadAvail = pkt.payloadSize();

                // First pass over this packet's payload: walk SRD
                // headers in order, gathering (row, offset, length,
                // F-bit) tuples and the total header bytes consumed.
                // The C bit on each SRD signals whether another
                // header follows in this packet (§6.1.4); cap at
                // the §6.2.1 3-SRD limit so a malformed sender can't
                // induce an unbounded walk.
                struct SrdRec {
                                uint16_t length;
                                uint16_t row;
                                uint16_t pixelOffset;
                                uint8_t  fBit;
                };
                SrdRec srds[MaxSrdsPerPacket];
                size_t srdCount = 0;
                size_t cursor = ExtSeqSize;
                bool   continueWalk = true;
                while (continueWalk && srdCount < MaxSrdsPerPacket && cursor + SrdHeaderSize <= payloadAvail) {
                        const uint8_t *h = pl + cursor;
                        SrdRec        &s = srds[srdCount];
                        s.length = static_cast<uint16_t>((static_cast<uint16_t>(h[0]) << 8) | h[1]);
                        s.fBit = (h[2] & 0x80u) ? 1u : 0u;
                        s.row = static_cast<uint16_t>(((static_cast<uint16_t>(h[2]) & 0x7Fu) << 8) | h[3]);
                        const uint8_t cBit = h[4] & 0x80u;
                        s.pixelOffset = static_cast<uint16_t>(((static_cast<uint16_t>(h[4]) & 0x7Fu) << 8) | h[5]);
                        srdCount++;
                        cursor += SrdHeaderSize;
                        continueWalk = (cBit != 0);
                }

                // Sample data begins immediately after the last SRD
                // header.  Each SRD's data follows in declaration
                // order, length given by the header's Length field.
                //
                // For 4:2:0 (rowsPerSrd > 1) the SRD Row Number
                // indexes image rows in pairs (0, 2, 4, ...) and is
                // always in the field-0 sense (§6.2.5 forbids 4:2:0
                // interlaced/PsF); the wire-row index in our single-
                // plane wire buffer is row / rowsPerSrd.  For
                // Interlaced/PsF the SRD Row Number is field-relative
                // and the F-bit picks which source wire-row family it
                // belongs to (§6.1.5).
                for (size_t k = 0; k < srdCount; k++) {
                        const SrdRec &s = srds[k];
                        if (cursor + s.length > payloadAvail) break;
                        const size_t byteOffset =
                                static_cast<size_t>(s.pixelOffset) * static_cast<size_t>(_bitsPerPixel) / 8;
                        size_t wireRow;
                        if (!splitFrame) {
                                wireRow = static_cast<size_t>(s.row) / static_cast<size_t>(_rowsPerSrd);
                        } else if (scanIsInterlaced) {
                                // Reconstruct sourceRow from
                                // (fieldIndex, srdRowInField).  Field
                                // 0 parity is 0 for even-first (default
                                // / unspecified), 1 for odd-first.
                                const int parity0 = oddFirst ? 1 : 0;
                                const int fieldParity =
                                        (s.fBit == 0) ? parity0 : (1 - parity0);
                                wireRow = static_cast<size_t>(s.row) * 2u + static_cast<size_t>(fieldParity);
                        } else { // PsF
                                wireRow = (s.fBit == 0)
                                                  ? static_cast<size_t>(s.row)
                                                  : static_cast<size_t>(psfTopCount) +
                                                            static_cast<size_t>(s.row);
                        }
                        const size_t dstOff = wireRow * bytesPerLine + byteOffset;
                        const size_t copySize = (dstOff + s.length <= frameSize) ? s.length : 0;
                        if (copySize > 0) {
                                std::memcpy(dst + dstOff, pl + cursor, copySize);
                        }
                        cursor += s.length;
                }
        }
        return result;
}

PROMEKI_NAMESPACE_END
