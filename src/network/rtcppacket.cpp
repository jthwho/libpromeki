/**
 * @file      rtcppacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtcppacket.h>
#include <promeki/logger.h>
#include <algorithm>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

inline void writeU16BE(uint8_t *p, uint16_t v) {
        p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void writeU32BE(uint8_t *p, uint32_t v) {
        p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[3] = static_cast<uint8_t>(v & 0xFF);
}

// RFC 3550 RTCP common header.  V=2, P=0, RC = report-block count
// (or SDES chunk count for PT=202), PT = packet type, length =
// total packet length in 32-bit words minus one.
inline void writeCommonHeader(uint8_t *p, uint8_t rc, uint8_t pt, uint16_t lengthWordsMinus1) {
        // Byte 0: V=2 (top 2 bits = 0b10), P=0, RC in lower 5 bits.
        p[0] = static_cast<uint8_t>(0x80u | (rc & 0x1Fu));
        p[1] = pt;
        writeU16BE(p + 2, lengthWordsMinus1);
}

inline uint16_t readU16BE(const uint8_t *p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t readU32BE(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

} // namespace

Buffer RtcpPacket::buildSenderReport(uint32_t ssrc, const NtpTime &ntp, uint32_t rtpTimestamp,
                                     uint32_t senderPacketCount, uint32_t senderOctetCount) {
        // Fixed shape: 4 (header) + 24 (sender info) = 28 bytes.
        // Length field = (28 / 4) - 1 = 6.
        constexpr size_t kSize = 28;
        Buffer           buf(kSize);
        uint8_t         *p = static_cast<uint8_t *>(buf.data());
        writeCommonHeader(p, /*rc=*/0, /*pt=*/SenderReport, /*lengthWordsMinus1=*/6);
        writeU32BE(p + 4, ssrc);
        writeU32BE(p + 8, ntp.seconds());
        writeU32BE(p + 12, ntp.fraction());
        writeU32BE(p + 16, rtpTimestamp);
        writeU32BE(p + 20, senderPacketCount);
        writeU32BE(p + 24, senderOctetCount);
        buf.setSize(kSize);
        return buf;
}

Buffer RtcpPacket::buildReceiverReport(uint32_t ssrc, const List<ReportBlock> &blocks) {
        // RFC 3550 §6.4.2.  The RC field is 5 bits, so cap at 31
        // report blocks.  Total size is 8 (header + sender SSRC) +
        // 24 × RC.
        constexpr size_t kHeaderAndSsrc = 8;
        constexpr size_t kBlockSize = 24;
        const size_t     count = std::min<size_t>(blocks.size(), 31);
        const size_t     total = kHeaderAndSsrc + count * kBlockSize;
        const size_t     totalWords = total / 4;

        Buffer   buf(total);
        uint8_t *p = static_cast<uint8_t *>(buf.data());
        writeCommonHeader(p, /*rc=*/static_cast<uint8_t>(count), /*pt=*/ReceiverReport,
                          /*lengthWordsMinus1=*/static_cast<uint16_t>(totalWords - 1));
        writeU32BE(p + 4, ssrc);
        size_t off = 8;
        for (size_t i = 0; i < count; i++) {
                const ReportBlock &b = blocks[i];
                writeU32BE(p + off, b.ssrc);
                // Byte 4 is fractionLost (8 bits), bytes 5-7 carry the
                // 24-bit signed cumulativeLost.  Two's-complement
                // reduction matches RFC 3550 §A.3.
                const int32_t  cumClamped = std::max(-0x800000, std::min(0x7FFFFF, b.cumulativeLost));
                const uint32_t cumU = static_cast<uint32_t>(cumClamped) & 0x00FFFFFFu;
                p[off + 4] = b.fractionLost;
                p[off + 5] = static_cast<uint8_t>((cumU >> 16) & 0xFFu);
                p[off + 6] = static_cast<uint8_t>((cumU >> 8) & 0xFFu);
                p[off + 7] = static_cast<uint8_t>(cumU & 0xFFu);
                writeU32BE(p + off + 8, b.extendedHighestSeq);
                writeU32BE(p + off + 12, b.interarrivalJitter);
                writeU32BE(p + off + 16, b.lsr);
                writeU32BE(p + off + 20, b.dlsr);
                off += kBlockSize;
        }
        buf.setSize(total);
        return buf;
}

Buffer RtcpPacket::buildBye(uint32_t ssrc) {
        // Minimal BYE shape: 4-byte common header (RC=1, PT=203,
        // length=1) + 4-byte source SSRC.  Total 8 bytes; length
        // field carries (8/4) - 1 = 1.
        constexpr size_t kSize = 8;
        Buffer           buf(kSize);
        uint8_t         *p = static_cast<uint8_t *>(buf.data());
        writeCommonHeader(p, /*rc=*/1, /*pt=*/Goodbye, /*lengthWordsMinus1=*/1);
        writeU32BE(p + 4, ssrc);
        buf.setSize(kSize);
        return buf;
}

Buffer RtcpPacket::buildSourceDescriptionCname(uint32_t ssrc, const String &cname) {
        // SDES (RFC 3550 §6.5) shape for one chunk with one CNAME item:
        //
        //   4 bytes:  common header (RC=1 / SC=1, PT=202, length)
        //   4 bytes:  chunk SSRC
        //   1 byte:   item type (CNAME=1)
        //   1 byte:   item length (cname size, capped at 255)
        //   N bytes:  CNAME text
        //   1 byte:   item-list terminator (PT=0) — RFC 3550 says
        //             "the list of items in each chunk is terminated
        //              by one or more null octets"
        //   pad:      zero bytes to align to a 32-bit boundary.
        //
        // The total chunk size (after the SSRC) MUST be a multiple of
        // 4, with at least one zero terminator.  Compute padding so
        // the trailing zeros total >= 1 and (item-bytes + zeros) is
        // a multiple of 4.
        const size_t cnameLen = std::min<size_t>(cname.size(), 255);
        // Bytes consumed within the chunk after the SSRC, before
        // padding: type + length + cnameLen.
        const size_t itemBytes = 2 + cnameLen;
        // Need at least 1 NUL terminator; pad to 4-byte align.
        size_t pad = 4 - ((itemBytes + 1) & 3);
        if (pad == 4) pad = 0;
        const size_t terminatorBytes = 1 + pad;
        // Per-chunk byte count after the SSRC.
        const size_t chunkTail = itemBytes + terminatorBytes;
        // Total chunk size = 4 (SSRC) + chunkTail.
        const size_t chunkSize = 4 + chunkTail;
        // Total packet size = 4 (common header) + chunkSize.
        const size_t totalSize = 4 + chunkSize;
        // Length field is in 32-bit words minus one.
        const size_t totalWords = totalSize / 4;

        Buffer   buf(totalSize);
        uint8_t *p = static_cast<uint8_t *>(buf.data());
        std::memset(p, 0, totalSize);
        writeCommonHeader(p, /*rc=*/1, /*pt=*/SourceDescription,
                          /*lengthWordsMinus1=*/static_cast<uint16_t>(totalWords - 1));
        writeU32BE(p + 4, ssrc);
        size_t off = 8;
        p[off++] = static_cast<uint8_t>(SdesCname);
        p[off++] = static_cast<uint8_t>(cnameLen);
        if (cnameLen > 0) {
                std::memcpy(p + off, cname.cstr(), cnameLen);
                off += cnameLen;
        }
        // Terminator + pad bytes are already zero from memset.
        buf.setSize(totalSize);
        return buf;
}

RtcpPacket::Header RtcpPacket::parseHeader(const uint8_t *data, size_t size) {
        Header h;
        if (data == nullptr || size < 4) return h;
        h.version = static_cast<uint8_t>((data[0] >> 6) & 0x3u);
        h.padding = (data[0] & 0x20u) != 0;
        h.rc = static_cast<uint8_t>(data[0] & 0x1Fu);
        h.pt = data[1];
        // length field is in 32-bit words minus one.
        const uint16_t lenWords = readU16BE(data + 2);
        h.lengthBytes = (static_cast<size_t>(lenWords) + 1u) * 4u;
        return h;
}

bool RtcpPacket::parseSenderReport(const uint8_t *data, size_t size, SenderReportInfo *out) {
        if (data == nullptr || out == nullptr) {
                promekiWarn("RtcpPacket::parseSenderReport called with null data/out");
                return false;
        }
        const Header h = parseHeader(data, size);
        if (!h.isValid()) {
                promekiWarnThrottled(2000, "RtcpPacket::parseSenderReport invalid header (size=%zu)", size);
                return false;
        }
        if (h.pt != SenderReport) {
                promekiWarnThrottled(2000, "RtcpPacket::parseSenderReport wrong PT (got=%u expected=%u)",
                                     static_cast<unsigned>(h.pt), static_cast<unsigned>(SenderReport));
                return false;
        }
        // Sender info block is 24 bytes immediately after the 4-byte
        // common header.  Length must cover that minimum; report
        // blocks (RC × 24 bytes) extend the packet beyond it but
        // don't change the sender info offsets.
        constexpr size_t kSenderInfoEnd = 4u + 24u;
        if (h.lengthBytes < kSenderInfoEnd) {
                promekiWarnThrottled(2000,
                                     "RtcpPacket::parseSenderReport length field too small (lengthBytes=%zu < %zu)",
                                     h.lengthBytes, kSenderInfoEnd);
                return false;
        }
        if (size < kSenderInfoEnd) {
                promekiWarnThrottled(2000, "RtcpPacket::parseSenderReport truncated (size=%zu < %zu)", size,
                                     kSenderInfoEnd);
                return false;
        }
        out->ssrc = readU32BE(data + 4);
        const uint32_t ntpSec = readU32BE(data + 8);
        const uint32_t ntpFrac = readU32BE(data + 12);
        out->ntp = NtpTime(ntpSec, ntpFrac);
        out->rtpTimestamp = readU32BE(data + 16);
        out->senderPacketCount = readU32BE(data + 20);
        out->senderOctetCount = readU32BE(data + 24);
        return true;
}

List<uint32_t> RtcpPacket::findByeSources(const uint8_t *data, size_t size) {
        List<uint32_t> out;
        if (data == nullptr) return out;
        size_t off = 0;
        while (off + 4 <= size) {
                const Header h = parseHeader(data + off, size - off);
                if (!h.isValid()) break;
                if (h.lengthBytes == 0 || off + h.lengthBytes > size) break;
                if (h.pt == Goodbye) {
                        // BYE format: 4-byte common header + RC × 4-byte
                        // source SSRCs + optional reason text.  We
                        // surface the SSRCs only — reason is purely
                        // diagnostic and not consumed today.
                        const size_t srcBytes = static_cast<size_t>(h.rc) * 4u;
                        if (4u + srcBytes <= h.lengthBytes && off + 4u + srcBytes <= size) {
                                for (uint8_t i = 0; i < h.rc; i++) {
                                        const uint32_t ssrc =
                                                readU32BE(data + off + 4u + (i * 4u));
                                        out.pushToBack(ssrc);
                                }
                        }
                }
                off += h.lengthBytes;
        }
        return out;
}

List<RtcpPacket::SenderReportInfo> RtcpPacket::findSenderReports(const uint8_t *data, size_t size) {
        List<SenderReportInfo> out;
        if (data == nullptr) return out;
        size_t off = 0;
        while (off + 4 <= size) {
                const Header h = parseHeader(data + off, size - off);
                if (!h.isValid()) break;
                if (h.lengthBytes == 0 || off + h.lengthBytes > size) break;
                if (h.pt == SenderReport) {
                        SenderReportInfo info;
                        if (parseSenderReport(data + off, h.lengthBytes, &info)) {
                                out.pushToBack(info);
                        }
                }
                off += h.lengthBytes;
        }
        return out;
}

Buffer RtcpPacket::compound(const List<Buffer> &packets) {
        size_t total = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                total += packets[i].size();
        }
        Buffer out(total);
        if (total == 0) {
                out.setSize(0);
                return out;
        }
        uint8_t *p = static_cast<uint8_t *>(out.data());
        size_t   off = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                const Buffer &b = packets[i];
                if (b.size() == 0) continue;
                std::memcpy(p + off, b.data(), b.size());
                off += b.size();
        }
        out.setSize(off);
        return out;
}

PROMEKI_NAMESPACE_END
