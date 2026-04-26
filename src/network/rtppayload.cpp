/**
 * @file      rtppayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtppayload.h>
#include <cstring>
#include <algorithm>

PROMEKI_NAMESPACE_BEGIN

// Helper: compute number of packets needed for a given data size and max payload
static size_t packetCount(size_t dataSize, size_t maxPayload) {
        if (maxPayload == 0) return 0;
        return (dataSize + maxPayload - 1) / maxPayload;
}

// ============================================================================
// RtpPayloadL24
// ============================================================================

RtpPayloadL24::RtpPayloadL24(uint32_t sampleRate, int channels) : _sampleRate(sampleRate), _channels(channels) {}

RtpPacket::List RtpPayloadL24::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) return packets;

        const size_t maxPayload = maxPayloadSize();
        // Align payload to 3 * channels bytes (one complete sample frame)
        const size_t sampleFrameSize = static_cast<size_t>(3 * _channels);
        const size_t alignedPayload = (maxPayload / sampleFrameSize) * sampleFrameSize;
        if (alignedPayload == 0) return packets;

        const size_t numPackets = packetCount(size, alignedPayload);
        const size_t totalBufSize = numPackets * (RtpPacket::HeaderSize + alignedPayload);
        auto         buf = Buffer::Ptr::create(totalBufSize);
        buf->setSize(totalBufSize);

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t         remaining = size;
        size_t         bufOffset = 0;
        uint8_t       *bufData = static_cast<uint8_t *>(buf->data());

        for (size_t i = 0; i < numPackets; i++) {
                size_t payloadSize = std::min(alignedPayload, remaining);
                size_t pktSize = RtpPacket::HeaderSize + payloadSize;

                // Clear RTP header space
                std::memset(bufData + bufOffset, 0, RtpPacket::HeaderSize);

                // Copy payload data after header
                std::memcpy(bufData + bufOffset + RtpPacket::HeaderSize, src, payloadSize);

                packets.pushToBack(RtpPacket(buf, bufOffset, pktSize));
                bufOffset += pktSize;
                src += payloadSize;
                remaining -= payloadSize;
        }
        return packets;
}

Buffer RtpPayloadL24::unpack(const RtpPacket::List &packets) {
        // Calculate total payload size
        size_t totalSize = 0;
        for (const auto &pkt : packets) {
                if (!pkt.isNull() && pkt.payloadSize() > 0) {
                        totalSize += pkt.payloadSize();
                }
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());
        for (const auto &pkt : packets) {
                if (!pkt.isNull() && pkt.payloadSize() > 0) {
                        std::memcpy(dst, pkt.payload(), pkt.payloadSize());
                        dst += pkt.payloadSize();
                }
        }
        return result;
}

// ============================================================================
// RtpPayloadL16
// ============================================================================

RtpPayloadL16::RtpPayloadL16(uint32_t sampleRate, int channels) : _sampleRate(sampleRate), _channels(channels) {}

RtpPacket::List RtpPayloadL16::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) return packets;

        const size_t maxPayload = maxPayloadSize();
        // Align payload to 2 * channels bytes (one complete sample frame)
        const size_t sampleFrameSize = static_cast<size_t>(2 * _channels);
        const size_t alignedPayload = (maxPayload / sampleFrameSize) * sampleFrameSize;
        if (alignedPayload == 0) return packets;

        const size_t numPackets = packetCount(size, alignedPayload);
        const size_t totalBufSize = numPackets * (RtpPacket::HeaderSize + alignedPayload);
        auto         buf = Buffer::Ptr::create(totalBufSize);
        buf->setSize(totalBufSize);

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t         remaining = size;
        size_t         bufOffset = 0;
        uint8_t       *bufData = static_cast<uint8_t *>(buf->data());

        for (size_t i = 0; i < numPackets; i++) {
                size_t payloadSize = std::min(alignedPayload, remaining);
                size_t pktSize = RtpPacket::HeaderSize + payloadSize;

                std::memset(bufData + bufOffset, 0, RtpPacket::HeaderSize);
                std::memcpy(bufData + bufOffset + RtpPacket::HeaderSize, src, payloadSize);

                packets.pushToBack(RtpPacket(buf, bufOffset, pktSize));
                bufOffset += pktSize;
                src += payloadSize;
                remaining -= payloadSize;
        }
        return packets;
}

Buffer RtpPayloadL16::unpack(const RtpPacket::List &packets) {
        size_t totalSize = 0;
        for (const auto &pkt : packets) {
                if (!pkt.isNull() && pkt.payloadSize() > 0) {
                        totalSize += pkt.payloadSize();
                }
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());
        for (const auto &pkt : packets) {
                if (!pkt.isNull() && pkt.payloadSize() > 0) {
                        std::memcpy(dst, pkt.payload(), pkt.payloadSize());
                        dst += pkt.payloadSize();
                }
        }
        return result;
}

// ============================================================================
// RtpPayloadRawVideo
// ============================================================================

// RFC 4175 per-line header size: 2 (length) + 2 (line number) + 2 (offset/field/continuation)
static constexpr size_t Rfc4175LineHeaderSize = 6;
// Extended sequence number field (2 bytes after RTP header)
static constexpr size_t Rfc4175ExtSeqSize = 2;

RtpPayloadRawVideo::RtpPayloadRawVideo(int width, int height, int bitsPerPixel, int pgroupBytes)
    : _width(width), _height(height), _bitsPerPixel(bitsPerPixel),
      _pgroupBytes(pgroupBytes > 0 ? pgroupBytes : (bitsPerPixel / 8)) {}

RtpPacket::List RtpPayloadRawVideo::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) return packets;

        const size_t bytesPerLine = static_cast<size_t>(_width) * _bitsPerPixel / 8;
        const size_t maxPayload = maxPayloadSize();
        // Available space for pixel data per packet (after ext seq num and one line header)
        const size_t overhead = Rfc4175ExtSeqSize + Rfc4175LineHeaderSize;
        if (maxPayload <= overhead) return packets;
        const size_t rawMax = maxPayload - overhead;

        // Align the maximum chunk size down to a whole number of
        // pgroups so that a pixel group is never split across two
        // packets.  Without this, formats like RGB 8-bit (pgroup =
        // 3 bytes) would produce chunks of 1192 bytes which is not
        // a multiple of 3, causing byte↔pixel offset rounding
        // errors and garbled pixels on the receiver.
        const size_t pg = static_cast<size_t>(_pgroupBytes);
        const size_t maxChunkBytes = (pg > 0) ? (rawMax / pg) * pg : rawMax;
        if (maxChunkBytes == 0) return packets;

        // Estimate total packets needed
        size_t totalPackets = 0;
        for (int line = 0; line < _height; line++) {
                size_t lineRemaining = bytesPerLine;
                while (lineRemaining > 0) {
                        size_t chunk = std::min(lineRemaining, maxChunkBytes);
                        totalPackets++;
                        lineRemaining -= chunk;
                }
        }

        // Allocate single shared buffer
        const size_t maxPktSize = RtpPacket::HeaderSize + overhead + maxChunkBytes;
        auto         buf = Buffer::Ptr::create(totalPackets * maxPktSize);
        buf->setSize(totalPackets * maxPktSize);
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t         bufOffset = 0;
        uint16_t       extSeq = 0;

        for (int line = 0; line < _height; line++) {
                size_t lineOffset = 0;
                size_t lineRemaining = bytesPerLine;

                while (lineRemaining > 0) {
                        size_t   chunk = std::min(lineRemaining, maxChunkBytes);
                        uint8_t *pkt = bufData + bufOffset;

                        // RTP header placeholder (12 bytes)
                        std::memset(pkt, 0, RtpPacket::HeaderSize);

                        // Extended sequence number (2 bytes)
                        pkt[RtpPacket::HeaderSize + 0] = static_cast<uint8_t>(extSeq >> 8);
                        pkt[RtpPacket::HeaderSize + 1] = static_cast<uint8_t>(extSeq & 0xFF);

                        // Per-line header (6 bytes)
                        size_t hdrOff = RtpPacket::HeaderSize + Rfc4175ExtSeqSize;
                        // Data length
                        pkt[hdrOff + 0] = static_cast<uint8_t>((chunk >> 8) & 0xFF);
                        pkt[hdrOff + 1] = static_cast<uint8_t>(chunk & 0xFF);
                        // Line number (F=0 for progressive)
                        uint16_t lineNum = static_cast<uint16_t>(line);
                        pkt[hdrOff + 2] = static_cast<uint8_t>((lineNum >> 8) & 0x7F);
                        pkt[hdrOff + 3] = static_cast<uint8_t>(lineNum & 0xFF);
                        // C=0 (no additional line header in this packet) + pixel offset.
                        // RFC 4175 §4: C is set when another scan line header
                        // follows in the SAME packet.  We emit one line segment
                        // per packet, so C is always 0.
                        uint16_t pixelOffset = static_cast<uint16_t>(lineOffset * 8 / _bitsPerPixel);
                        uint16_t offsetField = (pixelOffset & 0x7FFF);
                        pkt[hdrOff + 4] = static_cast<uint8_t>((offsetField >> 8) & 0xFF);
                        pkt[hdrOff + 5] = static_cast<uint8_t>(offsetField & 0xFF);

                        // Pixel data
                        size_t dataOff = RtpPacket::HeaderSize + overhead;
                        size_t srcOff = static_cast<size_t>(line) * bytesPerLine + lineOffset;
                        if (srcOff + chunk <= size) {
                                std::memcpy(pkt + dataOff, src + srcOff, chunk);
                        }

                        size_t pktSize = dataOff + chunk;
                        packets.pushToBack(RtpPacket(buf, bufOffset, pktSize));
                        bufOffset += pktSize;
                        lineOffset += chunk;
                        lineRemaining -= chunk;
                        extSeq++;
                }
        }
        return packets;
}

Buffer RtpPayloadRawVideo::unpack(const RtpPacket::List &packets) {
        const size_t bytesPerLine = static_cast<size_t>(_width) * _bitsPerPixel / 8;
        const size_t frameSize = bytesPerLine * static_cast<size_t>(_height);
        Buffer       result(frameSize);
        result.setSize(frameSize);
        std::memset(result.data(), 0, frameSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());

        const size_t overhead = Rfc4175ExtSeqSize + Rfc4175LineHeaderSize;

        for (const auto &pkt : packets) {
                if (pkt.isNull() || pkt.payloadSize() <= overhead) continue;
                const uint8_t *pl = pkt.payload();

                // Parse per-line header (ext seq + line header within payload)
                size_t   lineHdrOff = Rfc4175ExtSeqSize;
                uint16_t dataLen = (static_cast<uint16_t>(pl[lineHdrOff + 0]) << 8) | pl[lineHdrOff + 1];
                uint16_t lineNum = ((static_cast<uint16_t>(pl[lineHdrOff + 2]) & 0x7F) << 8) | pl[lineHdrOff + 3];
                uint16_t offsetField = (static_cast<uint16_t>(pl[lineHdrOff + 4]) << 8) | pl[lineHdrOff + 5];
                uint16_t pixelOffset = offsetField & 0x7FFF;
                size_t   byteOffset = static_cast<size_t>(pixelOffset) * _bitsPerPixel / 8;

                const uint8_t *pixelData = pl + overhead;
                size_t         payloadAvail = pkt.payloadSize() - overhead;
                size_t         copySize = std::min(static_cast<size_t>(dataLen), payloadAvail);

                size_t dstOff = static_cast<size_t>(lineNum) * bytesPerLine + byteOffset;
                if (dstOff + copySize <= frameSize) {
                        std::memcpy(dst + dstOff, pixelData, copySize);
                }
        }
        return result;
}

// ============================================================================
// RtpPayloadJpeg
// ============================================================================

// RFC 2435 JPEG header size: 8 bytes (type-specific + frag offset + type + Q + W/8 + H/8)
static constexpr size_t Rfc2435HeaderSize = 8;

// RFC 2435 Quantization Table Header: 4 bytes + table data
// Used when Q >= 128 to carry actual quantization tables.
//   MBZ (1 byte) + Precision (1 byte) + Length (2 bytes) + table data
static constexpr size_t Rfc2435QtHeaderSize = 4;

// JPEG marker codes
static constexpr uint8_t JpegMarkerPrefix = 0xFF;
static constexpr uint8_t JpegSOI = 0xD8;
static constexpr uint8_t JpegEOI = 0xD9;
static constexpr uint8_t JpegDQT = 0xDB;
static constexpr uint8_t JpegSOF0 = 0xC0;
static constexpr uint8_t JpegDHT = 0xC4;
static constexpr uint8_t JpegSOS = 0xDA;

// Scan a JPEG byte stream for the next marker, returning its position.
// Returns size (past end) if no marker found.
static size_t findJpegMarker(const uint8_t *data, size_t size, uint8_t marker) {
        for (size_t i = 0; i + 1 < size; i++) {
                if (data[i] == JpegMarkerPrefix && data[i + 1] == marker) return i;
        }
        return size;
}

// Read the 2-byte big-endian length field at data[pos+2..pos+3].
// The length includes itself (2 bytes) but not the marker (2 bytes).
static uint16_t jpegSegmentLength(const uint8_t *data, size_t pos) {
        return (static_cast<uint16_t>(data[pos + 2]) << 8) | data[pos + 3];
}

// Extract all DQT table data from the JPEG stream.  Returns concatenated
// raw 64-byte tables (for 8-bit precision) in the order they appear.
// RFC 2435 quantization table data does NOT include the JPEG Pq/Tq byte —
// only the 64-byte table values.
static size_t extractDqtTables(const uint8_t *data, size_t size, uint8_t *out, size_t outCap) {
        size_t written = 0;
        size_t pos = 0;
        for (;;) {
                size_t found = findJpegMarker(data + pos, size - pos, JpegDQT);
                if (found >= size - pos) break;
                found += pos; // absolute position
                uint16_t segLen = jpegSegmentLength(data, found);
                // Parse individual tables within this DQT segment.
                // Each table: 1 byte Pq/Tq + 64 bytes data (8-bit) or 128 bytes (16-bit).
                size_t segEnd = found + 2 + segLen;
                size_t tpos = found + 4; // skip marker (2) + length (2)
                while (tpos < segEnd) {
                        uint8_t pqtq = data[tpos];
                        int     precision = (pqtq >> 4) & 0x0F; // 0 = 8-bit, 1 = 16-bit
                        size_t  tableBytes = (precision == 0) ? 64 : 128;
                        tpos++; // skip Pq/Tq byte
                        if (tpos + tableBytes <= segEnd && written + tableBytes <= outCap) {
                                std::memcpy(out + written, data + tpos, tableBytes);
                                written += tableBytes;
                        }
                        tpos += tableBytes;
                }
                pos = segEnd;
        }
        return written;
}

// Find the byte offset of the entropy-coded data (immediately after the SOS
// marker's header).  This is the data that RFC 2435 transmits.
static size_t findEntropyCoded(const uint8_t *data, size_t size) {
        size_t pos = 0;
        for (;;) {
                size_t found = findJpegMarker(data + pos, size - pos, JpegSOS);
                if (found >= size - pos) return size; // not found
                found += pos;
                uint16_t segLen = jpegSegmentLength(data, found);
                return found + 2 + segLen; // skip marker (2) + header (segLen)
        }
}

RtpPayloadJpeg::RtpPayloadJpeg(int width, int height, int quality)
    : _width(width), _height(height), _quality(quality) {}

RtpPacket::List RtpPayloadJpeg::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) return packets;

        const uint8_t *jpeg = static_cast<const uint8_t *>(mediaData);

        // Locate the entropy-coded segment (after SOS header)
        size_t ecsStart = findEntropyCoded(jpeg, size);
        if (ecsStart >= size) return packets;

        // The entropy-coded data runs from ecsStart to just before the EOI
        // marker.  The receiver reconstructs its own EOI after the last RTP
        // fragment (keyed by the RTP marker bit).
        const uint8_t *ecsData = jpeg + ecsStart;
        size_t         ecsSize = size - ecsStart;

        // Strip trailing EOI marker (0xFF 0xD9) if present
        if (ecsSize >= 2 && ecsData[ecsSize - 2] == 0xFF && ecsData[ecsSize - 1] == 0xD9) {
                ecsSize -= 2;
        }

        // Extract quantization tables for the Q-table header (Q >= 128).
        // We always use Q=255 with explicit tables so the receiver doesn't
        // need to guess or compute tables — maximum compatibility.
        uint8_t dqtBuf[512]; // room for up to 4 tables
        size_t  dqtLen = extractDqtTables(jpeg, size, dqtBuf, sizeof(dqtBuf));

        // The first packet carries an extra Quantization Table Header
        size_t qtHdrSize = Rfc2435QtHeaderSize + dqtLen;

        const size_t maxPayload = maxPayloadSize();
        if (maxPayload <= Rfc2435HeaderSize + qtHdrSize) return packets;

        // Max JPEG data per packet (first packet has less room due to QT header)
        const size_t maxJpegFirst = maxPayload - Rfc2435HeaderSize - qtHdrSize;
        const size_t maxJpegRest = maxPayload - Rfc2435HeaderSize;

        // Count packets
        size_t numPackets = 1;
        size_t firstChunk = std::min(maxJpegFirst, ecsSize);
        size_t restSize = ecsSize - firstChunk;
        if (restSize > 0) numPackets += packetCount(restSize, maxJpegRest);

        const size_t maxPktSize = RtpPacket::HeaderSize + maxPayload;
        auto         buf = Buffer::Ptr::create(numPackets * maxPktSize);
        buf->setSize(numPackets * maxPktSize);
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        uint8_t w8 = static_cast<uint8_t>(std::min(_width / 8, 255));
        uint8_t h8 = static_cast<uint8_t>(std::min(_height / 8, 255));

        size_t remaining = ecsSize;
        size_t fragmentOffset = 0;
        size_t bufOffset = 0;

        for (size_t i = 0; i < numPackets; i++) {
                bool     isFirst = (i == 0);
                size_t   maxChunk = isFirst ? maxJpegFirst : maxJpegRest;
                size_t   jpegChunk = std::min(maxChunk, remaining);
                uint8_t *pkt = bufData + bufOffset;

                // RTP header placeholder
                std::memset(pkt, 0, RtpPacket::HeaderSize);

                // RFC 2435 JPEG header (8 bytes)
                size_t hdr = RtpPacket::HeaderSize;
                pkt[hdr + 0] = 0; // type-specific
                pkt[hdr + 1] = static_cast<uint8_t>((fragmentOffset >> 16) & 0xFF);
                pkt[hdr + 2] = static_cast<uint8_t>((fragmentOffset >> 8) & 0xFF);
                pkt[hdr + 3] = static_cast<uint8_t>(fragmentOffset & 0xFF);
                // RFC 2435 says Type 0 = 4:2:0, Type 1 = 4:2:2.
                // FFmpeg's rtpdec_jpeg.c has these reversed: it maps
                // Type 0 → Y(2x1) = 4:2:2, Type 1 → Y(2x2) = 4:2:0.
                // We use Type 0 here for FFmpeg compatibility since our
                // encoder produces 4:2:2 JPEG.
                pkt[hdr + 4] = 0;   // Type 0 (FFmpeg: 4:2:2)
                pkt[hdr + 5] = 255; // Q=255: quantization tables in first packet
                pkt[hdr + 6] = w8;
                pkt[hdr + 7] = h8;

                size_t dataOff = hdr + Rfc2435HeaderSize;

                // First packet: insert Quantization Table Header
                if (isFirst) {
                        pkt[dataOff + 0] = 0; // MBZ
                        pkt[dataOff + 1] = 0; // Precision (0 = 8-bit)
                        uint16_t qtLen = static_cast<uint16_t>(dqtLen);
                        pkt[dataOff + 2] = static_cast<uint8_t>((qtLen >> 8) & 0xFF);
                        pkt[dataOff + 3] = static_cast<uint8_t>(qtLen & 0xFF);
                        std::memcpy(pkt + dataOff + Rfc2435QtHeaderSize, dqtBuf, dqtLen);
                        dataOff += qtHdrSize;
                }

                // Entropy-coded data fragment
                std::memcpy(pkt + dataOff, ecsData + fragmentOffset, jpegChunk);

                size_t pktSize = dataOff + jpegChunk;
                packets.pushToBack(RtpPacket(buf, bufOffset, pktSize));
                bufOffset += pktSize;
                fragmentOffset += jpegChunk;
                remaining -= jpegChunk;
        }
        return packets;
}

// -----------------------------------------------------------------
// JFIF rebuild helpers for RtpPayloadJpeg::unpack()
//
// RFC 2435 transmits only the entropy-coded segment (ECS) of the
// JPEG frame; every structural marker (SOI, DQT, SOF0, DHT, SOS,
// EOI) must be reconstructed on the receive side from:
//   - the W/H/Type fields in the 8-byte RFC 2435 header
//   - quantization tables from the Q-table header when Q >= 128
//   - default quantization tables from Annex A when Q < 128
//   - canonical Huffman tables from Annex K (the standard DHT used
//     by every RFC 2435 encoder)
//
// This lets the reassembled Buffer be a fully decodable JFIF file
// that libjpeg-turbo / ffmpeg / browsers all accept verbatim.
// -----------------------------------------------------------------

// Annex K baseline JPEG Huffman tables as reproduced in RFC 2435
// Appendix A (Figure 7 / Figure 8).  These are the same tables every
// MJPEG encoder ships and every conforming decoder understands.
//
// Layout: "lum_dc_codelens" is the count of codes for each 1..16 bit
// length; "lum_dc_symbols" is the symbols sorted by code length.
static const uint8_t jpegLumDcCodelens[] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t jpegLumDcSymbols[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t jpegLumAcCodelens[] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
static const uint8_t jpegLumAcSymbols[] = {
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
        0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
        0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
        0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
        0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
        0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
static const uint8_t jpegChromDcCodelens[] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t jpegChromDcSymbols[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t jpegChromAcCodelens[] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
static const uint8_t jpegChromAcSymbols[] = {
        0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
        0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
        0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
        0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
        0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
        0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
        0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
        0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
        0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

// Default luminance quantization table from RFC 2435 Appendix A —
// used when Q < 128 and no in-stream QT header is present.  The
// makeDefaultQuantTables() helper scales these by Q per RFC 2435
// Appendix A pseudocode.
static const uint8_t jpegDefaultLumQuant[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                                14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                                18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                                49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
static const uint8_t jpegDefaultChromQuant[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                                  24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

// Scale the default Annex A luminance + chrominance quant tables by
// the Q factor (1-99).  Output is two 64-byte tables back-to-back.
static void makeDefaultQuantTables(uint8_t q, uint8_t *out128) {
        int factor = q;
        if (factor < 1) factor = 1;
        if (factor > 99) factor = 99;
        int scale = (factor < 50) ? (5000 / factor) : (200 - factor * 2);
        for (int i = 0; i < 64; i++) {
                int lq = (static_cast<int>(jpegDefaultLumQuant[i]) * scale + 50) / 100;
                int cq = (static_cast<int>(jpegDefaultChromQuant[i]) * scale + 50) / 100;
                if (lq < 1) lq = 1;
                if (lq > 255) lq = 255;
                if (cq < 1) cq = 1;
                if (cq > 255) cq = 255;
                out128[i] = static_cast<uint8_t>(lq);
                out128[i + 64] = static_cast<uint8_t>(cq);
        }
}

// Appends a complete DHT segment for one Huffman table (class + id +
// length counts + symbols).  tableClass: 0 = DC, 1 = AC.  tableId:
// 0 = luminance, 1 = chrominance.
static void appendDhtSegment(List<uint8_t> &out, uint8_t tableClass, uint8_t tableId, const uint8_t *codelens,
                             const uint8_t *symbols, size_t symbolCount) {
        const size_t   segDataLen = 1 /*Tc/Th*/ + 16 /*code counts*/ + symbolCount;
        const uint16_t segLen = static_cast<uint16_t>(segDataLen + 2);
        out.pushToBack(0xFF);
        out.pushToBack(JpegDHT);
        out.pushToBack(static_cast<uint8_t>((segLen >> 8) & 0xFF));
        out.pushToBack(static_cast<uint8_t>(segLen & 0xFF));
        out.pushToBack(static_cast<uint8_t>(((tableClass & 0x0F) << 4) | (tableId & 0x0F)));
        for (int i = 0; i < 16; i++) out.pushToBack(codelens[i]);
        for (size_t i = 0; i < symbolCount; i++) out.pushToBack(symbols[i]);
}

Buffer RtpPayloadJpeg::unpack(const RtpPacket::List &packets) {
        if (packets.isEmpty()) return Buffer();

        // -- Pull the RFC 2435 fields from the first packet --
        //
        // Type, Q, W/8, H/8 come from the 8-byte payload header.
        // Width and height are encoded in 8-pixel blocks (hence the
        // /8) with W=0 / H=0 reserved for "2040 or unknown".  The
        // caller is responsible for passing us only packets from a
        // single RTP frame (same timestamp + marker-terminated), so
        // the first packet's fields describe the whole frame.
        const RtpPacket &firstPkt = packets[0];
        if (firstPkt.isNull() || firstPkt.payloadSize() < Rfc2435HeaderSize) {
                return Buffer();
        }
        const uint8_t *firstPl = firstPkt.payload();
        const uint8_t  rtpType = firstPl[4];
        const uint8_t  rtpQ = firstPl[5];
        const uint32_t width = static_cast<uint32_t>(firstPl[6]) * 8;
        const uint32_t height = static_cast<uint32_t>(firstPl[7]) * 8;
        if (width == 0 || height == 0) {
                // Zero width/height reserved by RFC 2435 — we could
                // fall back to _width/_height, but rejecting is
                // safer than producing a broken JPEG.
                return Buffer();
        }

        // -- Extract quantization tables --
        //
        // Q < 128 → use the scaled default Annex A tables.
        // Q >= 128 → the first packet carries a QT header with the
        // explicit tables.  Either way we end up with a 128-byte
        // array (two 64-byte 8-bit tables: luminance then
        // chrominance).  RFC 2435 §3.1.7 allows the QT header to
        // carry one or more tables; real encoders typically emit
        // either a single luminance table (when the chrominance
        // table is the same, e.g. high-quality 4:4:4) or two
        // distinct tables.  We support qtLen == 64 (single table,
        // duplicated for chrominance) and qtLen == 128 (two
        // distinct tables) so we round-trip both the canonical
        // RFC 2435 writer layout and encoders that drop the
        // chrominance table for space.
        uint8_t quantTables[128];
        size_t  qtSkipBytes = 0; // payload bytes taken by the QT header + data
        if (rtpQ < 128) {
                makeDefaultQuantTables(rtpQ, quantTables);
        } else {
                if (firstPkt.payloadSize() < Rfc2435HeaderSize + Rfc2435QtHeaderSize) {
                        return Buffer();
                }
                const uint8_t *qtHdr = firstPl + Rfc2435HeaderSize;
                const uint8_t  qtMbz = qtHdr[0];
                const uint8_t  qtPrecision = qtHdr[1];
                const uint16_t qtLen = (static_cast<uint16_t>(qtHdr[2]) << 8) | qtHdr[3];
                (void)qtMbz;
                if (qtPrecision != 0 || (qtLen != 64 && qtLen != 128)) {
                        // 16-bit precision (precision != 0) is legal
                        // per RFC 2435 but no common encoder uses
                        // it, and real-world encoders only ever emit
                        // one or two 8-bit tables.  Reject anything
                        // else so a malformed / truncated QT header
                        // does not wander into uninitialised memory.
                        return Buffer();
                }
                if (firstPkt.payloadSize() < Rfc2435HeaderSize + Rfc2435QtHeaderSize + static_cast<size_t>(qtLen)) {
                        return Buffer();
                }
                std::memcpy(quantTables, firstPl + Rfc2435HeaderSize + Rfc2435QtHeaderSize, qtLen);
                if (qtLen == 64) {
                        // Duplicate the luminance table as the
                        // chrominance table — matches what libjpeg
                        // does when an encoder omits the second
                        // table from a 4:2:2 / 4:2:0 scan.
                        std::memcpy(quantTables + 64, quantTables, 64);
                }
                qtSkipBytes = Rfc2435QtHeaderSize + static_cast<size_t>(qtLen);
        }

        // -- Reassemble the entropy-coded segment --
        //
        // Each packet after the first is pure payload data; the
        // first packet's payload data starts after the optional
        // QT header (when Q >= 128).  Fragment offsets come from
        // the 24-bit offset in the RFC 2435 header, so we can
        // honour out-of-order packets (though the per-stream RTP
        // receiver already sorts by sequence number).
        size_t totalEntropy = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                const RtpPacket &pkt = packets[i];
                if (pkt.isNull() || pkt.payloadSize() <= Rfc2435HeaderSize) continue;
                size_t payBytes = pkt.payloadSize() - Rfc2435HeaderSize;
                if (i == 0 && qtSkipBytes > 0) {
                        if (payBytes <= qtSkipBytes) continue;
                        payBytes -= qtSkipBytes;
                }
                totalEntropy += payBytes;
        }
        List<uint8_t> entropy;
        entropy.resize(totalEntropy);
        // Walk the packets a second time, honouring fragment
        // offsets.  For the first packet the payload data starts
        // after the QT header (when Q >= 128), so its "fragment
        // offset" from RFC 2435 is 0 but the payload pointer is
        // advanced past the QT header.
        for (size_t i = 0; i < packets.size(); i++) {
                const RtpPacket &pkt = packets[i];
                if (pkt.isNull() || pkt.payloadSize() <= Rfc2435HeaderSize) continue;
                const uint8_t *pl = pkt.payload();
                const uint32_t fragOff = (static_cast<uint32_t>(pl[1]) << 16) | (static_cast<uint32_t>(pl[2]) << 8) |
                                         static_cast<uint32_t>(pl[3]);
                const uint8_t *dataPtr = pl + Rfc2435HeaderSize;
                size_t         dataLen = pkt.payloadSize() - Rfc2435HeaderSize;
                if (i == 0 && qtSkipBytes > 0) {
                        if (dataLen <= qtSkipBytes) continue;
                        dataPtr += qtSkipBytes;
                        dataLen -= qtSkipBytes;
                }
                if (fragOff + dataLen > entropy.size()) continue;
                std::memcpy(entropy.data() + fragOff, dataPtr, dataLen);
        }

        // -- Assemble the JFIF bitstream --
        //
        // Order per JFIF: SOI / DQT(luma) / DQT(chroma) / SOF0 /
        // DHT(luma DC+AC) / DHT(chroma DC+AC) / SOS / entropy / EOI.
        // The DHT tables are the canonical Annex K tables used by
        // every MJPEG encoder.  The SOF0 component sampling factors
        // depend on the RFC 2435 Type field:
        //   Type 0  → 4:2:2 (per FFmpeg's rtpenc_jpeg.c mapping)
        //   Type 1  → 4:2:0
        // We follow FFmpeg's convention to interop with its encoder.
        const bool is422 = (rtpType == 0);

        List<uint8_t> out;
        // Reserve a conservative upper bound to avoid per-append
        // reallocations.
        out.reserve(entropy.size() + 1024);

        // SOI
        out.pushToBack(0xFF);
        out.pushToBack(JpegSOI);

        // DQT (luminance)  — segment: FF DB | len(2) | Pq/Tq | 64 bytes
        out.pushToBack(0xFF);
        out.pushToBack(JpegDQT);
        out.pushToBack(0x00);
        out.pushToBack(67);   // len = 3 + 64
        out.pushToBack(0x00); // 8-bit precision, table id 0 (luminance)
        for (int i = 0; i < 64; i++) out.pushToBack(quantTables[i]);
        // DQT (chrominance)
        out.pushToBack(0xFF);
        out.pushToBack(JpegDQT);
        out.pushToBack(0x00);
        out.pushToBack(67);
        out.pushToBack(0x01); // 8-bit precision, table id 1 (chrominance)
        for (int i = 0; i < 64; i++) out.pushToBack(quantTables[64 + i]);

        // SOF0 — 17 bytes total: FF C0 | 00 11 | 08 | H H | W W | 03 |
        //         Ci Hi/Vi Tq  × 3 components
        out.pushToBack(0xFF);
        out.pushToBack(JpegSOF0);
        out.pushToBack(0x00);
        out.pushToBack(17);
        out.pushToBack(0x08); // sample precision
        out.pushToBack(static_cast<uint8_t>((height >> 8) & 0xFF));
        out.pushToBack(static_cast<uint8_t>(height & 0xFF));
        out.pushToBack(static_cast<uint8_t>((width >> 8) & 0xFF));
        out.pushToBack(static_cast<uint8_t>(width & 0xFF));
        out.pushToBack(0x03); // components: Y, Cb, Cr
        // Y sampling factor depends on subsampling
        out.pushToBack(0x01);                // Component 1: Y
        out.pushToBack(is422 ? 0x21 : 0x22); // Sampling 2x1 or 2x2
        out.pushToBack(0x00);                // Quant table 0
        out.pushToBack(0x02);                // Component 2: Cb
        out.pushToBack(0x11);                // 1x1
        out.pushToBack(0x01);                // Quant table 1
        out.pushToBack(0x03);                // Component 3: Cr
        out.pushToBack(0x11);                // 1x1
        out.pushToBack(0x01);                // Quant table 1

        // DHT — four standard Annex K tables
        appendDhtSegment(out, 0, 0, jpegLumDcCodelens, jpegLumDcSymbols, sizeof(jpegLumDcSymbols));
        appendDhtSegment(out, 1, 0, jpegLumAcCodelens, jpegLumAcSymbols, sizeof(jpegLumAcSymbols));
        appendDhtSegment(out, 0, 1, jpegChromDcCodelens, jpegChromDcSymbols, sizeof(jpegChromDcSymbols));
        appendDhtSegment(out, 1, 1, jpegChromAcCodelens, jpegChromAcSymbols, sizeof(jpegChromAcSymbols));

        // SOS — 14 bytes total: FF DA | 00 0C | 03 | (Cs Td/Ta)*3 | Ss Se Ah/Al
        out.pushToBack(0xFF);
        out.pushToBack(JpegSOS);
        out.pushToBack(0x00);
        out.pushToBack(12);
        out.pushToBack(0x03); // components in scan
        out.pushToBack(0x01);
        out.pushToBack(0x00); // Y  — DC/AC table 0
        out.pushToBack(0x02);
        out.pushToBack(0x11); // Cb — DC/AC table 1
        out.pushToBack(0x03);
        out.pushToBack(0x11); // Cr — DC/AC table 1
        out.pushToBack(0x00); // Ss start of spectral selection
        out.pushToBack(0x3F); // Se end of spectral selection
        out.pushToBack(0x00); // Ah/Al

        // Entropy-coded segment
        for (size_t i = 0; i < entropy.size(); i++) out.pushToBack(entropy[i]);

        // EOI
        out.pushToBack(0xFF);
        out.pushToBack(JpegEOI);

        Buffer result(out.size());
        result.setSize(out.size());
        std::memcpy(result.data(), out.data(), out.size());
        return result;
}

// ============================================================================
// RtpPayloadJpegXs (RFC 9134)
// ============================================================================

// Pack the RFC 9134 4-byte payload header into network byte order.
// See the bit diagram in rtppayload.h — this is the inverse of
// readJxsHeader below.  Individual fields are clamped / masked to
// their documented widths so a caller that passes e.g. frameCounter
// >= 32 still produces a well-formed header.
static void writeJxsHeader(uint8_t *hdr, bool T, bool K, bool L, uint8_t I, uint8_t frameCounter, uint16_t sepCounter,
                           uint16_t packetCounter) {
        const uint8_t  f = frameCounter & 0x1F;    // 5-bit
        const uint16_t se = sepCounter & 0x7FF;    // 11-bit
        const uint16_t pc = packetCounter & 0x7FF; // 11-bit
        hdr[0] = (uint8_t)(((T ? 1 : 0) << 7) | ((K ? 1 : 0) << 6) | ((L ? 1 : 0) << 5) | ((I & 0x03) << 3) |
                           ((f >> 2) & 0x07));
        hdr[1] = (uint8_t)(((f & 0x03) << 6) | ((se >> 5) & 0x3F));
        hdr[2] = (uint8_t)(((se & 0x1F) << 3) | ((pc >> 8) & 0x07));
        hdr[3] = (uint8_t)(pc & 0xFF);
}

// Inverse of writeJxsHeader — extract the seven fields from a
// 4-byte RFC 9134 header.  Used by unpack() to walk incoming
// packets and by the unit test to verify bit layout.
static void readJxsHeader(const uint8_t *hdr, bool &T, bool &K, bool &L, uint8_t &I, uint8_t &frameCounter,
                          uint16_t &sepCounter, uint16_t &packetCounter) {
        T = ((hdr[0] >> 7) & 0x01) != 0;
        K = ((hdr[0] >> 6) & 0x01) != 0;
        L = ((hdr[0] >> 5) & 0x01) != 0;
        I = (uint8_t)((hdr[0] >> 3) & 0x03);
        frameCounter = (uint8_t)(((hdr[0] & 0x07) << 2) | ((hdr[1] >> 6) & 0x03));
        sepCounter = (uint16_t)(((uint16_t)(hdr[1] & 0x3F) << 5) | ((hdr[2] >> 3) & 0x1F));
        packetCounter = (uint16_t)(((uint16_t)(hdr[2] & 0x07) << 8) | hdr[3]);
}

RtpPayloadJpegXs::RtpPayloadJpegXs(int width, int height, uint8_t payloadType)
    : _width(width), _height(height), _payloadType(payloadType) {}

RtpPacket::List RtpPayloadJpegXs::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) return packets;

        const uint8_t *jxs = static_cast<const uint8_t *>(mediaData);

        // Fragment the codestream into MTU-sized chunks.  Codestream
        // packetization mode (K=0) does not care about slice / header
        // boundaries — it's a byte-stream splitter.
        const size_t maxPayload = maxPayloadSize();
        if (maxPayload <= HeaderSize) return packets;
        const size_t maxData = maxPayload - HeaderSize;

        const size_t numPackets = (size + maxData - 1) / maxData;
        const size_t maxPktSize = RtpPacket::HeaderSize + maxPayload;

        // Single shared buffer for all packets so the RtpSession can
        // emit them without additional copies — same pattern as
        // RtpPayloadJpeg / RtpPayloadRawVideo.
        auto buf = Buffer::Ptr::create(numPackets * maxPktSize);
        buf->setSize(numPackets * maxPktSize);
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        // Frame counter is per-frame state on the payload instance.
        // Advances once per successful pack() call so each frame gets
        // a distinct 5-bit F value (mod 32) that the receiver can use
        // to detect reordering / loss across frame boundaries.
        const uint8_t thisFrame = _frameCounter;
        _frameCounter = (uint8_t)((_frameCounter + 1) & 0x1F);

        size_t   remaining = size;
        size_t   srcOffset = 0;
        size_t   bufOffset = 0;
        uint16_t packetCounter = 0;
        uint16_t sepCounter = 0;

        for (size_t i = 0; i < numPackets; i++) {
                const bool   isLast = (i == numPackets - 1);
                const size_t chunk = std::min(maxData, remaining);

                uint8_t *pkt = bufData + bufOffset;
                std::memset(pkt, 0, RtpPacket::HeaderSize);

                // 4-byte RFC 9134 header at the start of the payload
                writeJxsHeader(pkt + RtpPacket::HeaderSize,
                               /*T=*/true,
                               /*K=*/false, // codestream mode
                               /*L=*/isLast,
                               /*I=*/0, // progressive
                               thisFrame, sepCounter, packetCounter);

                // Copy the data fragment immediately after the header
                std::memcpy(pkt + RtpPacket::HeaderSize + HeaderSize, jxs + srcOffset, chunk);

                const size_t pktSize = RtpPacket::HeaderSize + HeaderSize + chunk;
                packets.pushToBack(RtpPacket(buf, bufOffset, pktSize));

                bufOffset += pktSize;
                srcOffset += chunk;
                remaining -= chunk;

                // Advance counters.  The P counter is the packet index
                // within the current frame modulo 2048; when it wraps,
                // the SEP counter increments (also mod 2048).  For
                // typical broadcast bitrates we stay well under 2048
                // packets per frame and SEP stays at 0.
                if (packetCounter == 0x7FF) {
                        packetCounter = 0;
                        sepCounter = (uint16_t)((sepCounter + 1) & 0x7FF);
                } else {
                        packetCounter++;
                }
        }
        return packets;
}

Buffer RtpPayloadJpegXs::unpack(const RtpPacket::List &packets) {
        // Sum up all fragment sizes so we can allocate the output
        // in one shot, then walk the packets in order and append.
        // RFC 9134 codestream mode is a pure byte stream; there is
        // no per-fragment offset header, the order comes from the
        // RTP sequence number (which the caller has already sorted
        // by handing us the list in arrival order).
        size_t totalSize = 0;
        for (const auto &pkt : packets) {
                if (pkt.isNull()) continue;
                if (pkt.payloadSize() <= HeaderSize) continue;
                totalSize += pkt.payloadSize() - HeaderSize;
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        if (totalSize == 0) return result;

        uint8_t *dst = static_cast<uint8_t *>(result.data());
        size_t   pos = 0;
        for (const auto &pkt : packets) {
                if (pkt.isNull()) continue;
                if (pkt.payloadSize() <= HeaderSize) continue;
                const uint8_t *pl = pkt.payload();
                const size_t   plSize = pkt.payloadSize();
                const size_t   frag = plSize - HeaderSize;
                std::memcpy(dst + pos, pl + HeaderSize, frag);
                pos += frag;
        }
        return result;
}

// ============================================================================
// RtpPayloadJson
// ============================================================================

RtpPayloadJson::RtpPayloadJson(uint8_t payloadType, uint32_t clockRate)
    : _payloadType(payloadType), _clockRate(clockRate) {}

RtpPacket::List RtpPayloadJson::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if (size == 0 || mediaData == nullptr) return packets;

        const size_t maxPayload = maxPayloadSize();
        if (maxPayload == 0) return packets;

        const size_t numPackets = packetCount(size, maxPayload);
        // Each packet gets space for the 12-byte RTP header plus up
        // to maxPayload of JSON bytes.  A single shared buffer holds
        // the whole batch.
        const size_t totalBufSize = numPackets * (RtpPacket::HeaderSize + maxPayload);
        auto         buf = Buffer::Ptr::create(totalBufSize);
        buf->setSize(totalBufSize);

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t         remaining = size;
        size_t         bufOffset = 0;
        uint8_t       *bufData = static_cast<uint8_t *>(buf->data());

        for (size_t i = 0; i < numPackets; i++) {
                const size_t chunk = std::min(maxPayload, remaining);
                const size_t pktLen = RtpPacket::HeaderSize + chunk;

                // Clear the header slot; the RtpSession fills it in
                // before transmission.
                std::memset(bufData + bufOffset, 0, RtpPacket::HeaderSize);
                // Payload fragment.
                std::memcpy(bufData + bufOffset + RtpPacket::HeaderSize, src, chunk);

                packets.pushToBack(RtpPacket(buf, bufOffset, pktLen));
                bufOffset += pktLen;
                src += chunk;
                remaining -= chunk;
        }
        return packets;
}

Buffer RtpPayloadJson::unpack(const RtpPacket::List &packets) {
        // Calculate total payload size across all fragments.
        size_t totalSize = 0;
        for (const auto &pkt : packets) {
                if (!pkt.isNull() && pkt.payloadSize() > 0) {
                        totalSize += pkt.payloadSize();
                }
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        if (totalSize == 0) return result;
        uint8_t *dst = static_cast<uint8_t *>(result.data());
        for (const auto &pkt : packets) {
                if (!pkt.isNull() && pkt.payloadSize() > 0) {
                        std::memcpy(dst, pkt.payload(), pkt.payloadSize());
                        dst += pkt.payloadSize();
                }
        }
        return result;
}

PROMEKI_NAMESPACE_END
