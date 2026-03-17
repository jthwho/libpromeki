/**
 * @file      net/rtppayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/rtppayload.h>
#include <cstring>
#include <algorithm>

PROMEKI_NAMESPACE_BEGIN

// Helper: compute number of packets needed for a given data size and max payload
static size_t packetCount(size_t dataSize, size_t maxPayload) {
        if(maxPayload == 0) return 0;
        return (dataSize + maxPayload - 1) / maxPayload;
}

// ============================================================================
// RtpPayloadL24
// ============================================================================

RtpPayloadL24::RtpPayloadL24(uint32_t sampleRate, int channels)
        : _sampleRate(sampleRate), _channels(channels) { }

RtpPacket::List RtpPayloadL24::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if(size == 0 || mediaData == nullptr) return packets;

        const size_t maxPayload = maxPayloadSize();
        // Align payload to 3 * channels bytes (one complete sample frame)
        const size_t sampleFrameSize = static_cast<size_t>(3 * _channels);
        const size_t alignedPayload = (maxPayload / sampleFrameSize) * sampleFrameSize;
        if(alignedPayload == 0) return packets;

        const size_t numPackets = packetCount(size, alignedPayload);
        const size_t totalBufSize = numPackets * (RtpPacket::HeaderSize + alignedPayload);
        auto buf = Buffer::Ptr::create(totalBufSize);
        buf->setSize(totalBufSize);

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t remaining = size;
        size_t bufOffset = 0;
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        for(size_t i = 0; i < numPackets; i++) {
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
        for(const auto &pkt : packets) {
                if(!pkt.isNull() && pkt.payloadSize() > 0) {
                        totalSize += pkt.payloadSize();
                }
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());
        for(const auto &pkt : packets) {
                if(!pkt.isNull() && pkt.payloadSize() > 0) {
                        std::memcpy(dst, pkt.payload(), pkt.payloadSize());
                        dst += pkt.payloadSize();
                }
        }
        return result;
}

// ============================================================================
// RtpPayloadL16
// ============================================================================

RtpPayloadL16::RtpPayloadL16(uint32_t sampleRate, int channels)
        : _sampleRate(sampleRate), _channels(channels) { }

RtpPacket::List RtpPayloadL16::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if(size == 0 || mediaData == nullptr) return packets;

        const size_t maxPayload = maxPayloadSize();
        // Align payload to 2 * channels bytes (one complete sample frame)
        const size_t sampleFrameSize = static_cast<size_t>(2 * _channels);
        const size_t alignedPayload = (maxPayload / sampleFrameSize) * sampleFrameSize;
        if(alignedPayload == 0) return packets;

        const size_t numPackets = packetCount(size, alignedPayload);
        const size_t totalBufSize = numPackets * (RtpPacket::HeaderSize + alignedPayload);
        auto buf = Buffer::Ptr::create(totalBufSize);
        buf->setSize(totalBufSize);

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t remaining = size;
        size_t bufOffset = 0;
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        for(size_t i = 0; i < numPackets; i++) {
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
        for(const auto &pkt : packets) {
                if(!pkt.isNull() && pkt.payloadSize() > 0) {
                        totalSize += pkt.payloadSize();
                }
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());
        for(const auto &pkt : packets) {
                if(!pkt.isNull() && pkt.payloadSize() > 0) {
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

RtpPayloadRawVideo::RtpPayloadRawVideo(int width, int height, int bitsPerPixel)
        : _width(width), _height(height), _bitsPerPixel(bitsPerPixel) { }

RtpPacket::List RtpPayloadRawVideo::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if(size == 0 || mediaData == nullptr) return packets;

        const size_t bytesPerLine = static_cast<size_t>(_width) * _bitsPerPixel / 8;
        const size_t maxPayload = maxPayloadSize();
        // Available space for pixel data per packet (after ext seq num and one line header)
        const size_t overhead = Rfc4175ExtSeqSize + Rfc4175LineHeaderSize;
        if(maxPayload <= overhead) return packets;
        const size_t maxPixelsPerPacket = maxPayload - overhead;

        // Estimate total packets needed
        size_t totalPackets = 0;
        for(int line = 0; line < _height; line++) {
                size_t lineRemaining = bytesPerLine;
                while(lineRemaining > 0) {
                        size_t chunk = std::min(lineRemaining, maxPixelsPerPacket);
                        totalPackets++;
                        lineRemaining -= chunk;
                }
        }

        // Allocate single shared buffer
        const size_t maxPktSize = RtpPacket::HeaderSize + maxPayload;
        auto buf = Buffer::Ptr::create(totalPackets * maxPktSize);
        buf->setSize(totalPackets * maxPktSize);
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t bufOffset = 0;
        uint16_t extSeq = 0;

        for(int line = 0; line < _height; line++) {
                size_t lineOffset = 0;
                size_t lineRemaining = bytesPerLine;

                while(lineRemaining > 0) {
                        size_t chunk = std::min(lineRemaining, maxPixelsPerPacket);
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
                        // Line number
                        uint16_t lineNum = static_cast<uint16_t>(line);
                        pkt[hdrOff + 2] = static_cast<uint8_t>((lineNum >> 8) & 0x7F);
                        pkt[hdrOff + 3] = static_cast<uint8_t>(lineNum & 0xFF);
                        // Field ID (0) + offset (pixel offset) + continuation
                        uint16_t pixelOffset = static_cast<uint16_t>(lineOffset * 8 / _bitsPerPixel);
                        bool continuation = (lineRemaining - chunk) > 0;
                        uint16_t offsetField = (pixelOffset & 0x7FFF);
                        if(continuation) offsetField |= 0x8000;
                        pkt[hdrOff + 4] = static_cast<uint8_t>((offsetField >> 8) & 0xFF);
                        pkt[hdrOff + 5] = static_cast<uint8_t>(offsetField & 0xFF);

                        // Pixel data
                        size_t dataOff = RtpPacket::HeaderSize + overhead;
                        size_t srcOff = static_cast<size_t>(line) * bytesPerLine + lineOffset;
                        if(srcOff + chunk <= size) {
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
        Buffer result(frameSize);
        result.setSize(frameSize);
        std::memset(result.data(), 0, frameSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());

        const size_t overhead = Rfc4175ExtSeqSize + Rfc4175LineHeaderSize;

        for(const auto &pkt : packets) {
                if(pkt.isNull() || pkt.payloadSize() <= overhead) continue;
                const uint8_t *pl = pkt.payload();

                // Parse per-line header (ext seq + line header within payload)
                size_t lineHdrOff = Rfc4175ExtSeqSize;
                uint16_t dataLen = (static_cast<uint16_t>(pl[lineHdrOff + 0]) << 8) | pl[lineHdrOff + 1];
                uint16_t lineNum = ((static_cast<uint16_t>(pl[lineHdrOff + 2]) & 0x7F) << 8) | pl[lineHdrOff + 3];
                uint16_t offsetField = (static_cast<uint16_t>(pl[lineHdrOff + 4]) << 8) | pl[lineHdrOff + 5];
                uint16_t pixelOffset = offsetField & 0x7FFF;
                size_t byteOffset = static_cast<size_t>(pixelOffset) * _bitsPerPixel / 8;

                const uint8_t *pixelData = pl + overhead;
                size_t payloadAvail = pkt.payloadSize() - overhead;
                size_t copySize = std::min(static_cast<size_t>(dataLen), payloadAvail);

                size_t dstOff = static_cast<size_t>(lineNum) * bytesPerLine + byteOffset;
                if(dstOff + copySize <= frameSize) {
                        std::memcpy(dst + dstOff, pixelData, copySize);
                }
        }
        return result;
}

// ============================================================================
// RtpPayloadJpeg
// ============================================================================

// RFC 2435 JPEG header size: 8 bytes
static constexpr size_t Rfc2435HeaderSize = 8;

RtpPayloadJpeg::RtpPayloadJpeg(int width, int height, int quality)
        : _width(width), _height(height), _quality(quality) { }

RtpPacket::List RtpPayloadJpeg::pack(const void *mediaData, size_t size) {
        RtpPacket::List packets;
        if(size == 0 || mediaData == nullptr) return packets;

        const size_t maxPayload = maxPayloadSize();
        if(maxPayload <= Rfc2435HeaderSize) return packets;
        const size_t maxJpegPerPacket = maxPayload - Rfc2435HeaderSize;

        const size_t numPackets = packetCount(size, maxJpegPerPacket);
        const size_t maxPktSize = RtpPacket::HeaderSize + maxPayload;
        auto buf = Buffer::Ptr::create(numPackets * maxPktSize);
        buf->setSize(numPackets * maxPktSize);
        uint8_t *bufData = static_cast<uint8_t *>(buf->data());

        const uint8_t *src = static_cast<const uint8_t *>(mediaData);
        size_t remaining = size;
        size_t fragmentOffset = 0;
        size_t bufOffset = 0;

        // Width and height in 8-pixel units, clamped to uint8_t
        uint8_t w8 = static_cast<uint8_t>(std::min(_width / 8, 255));
        uint8_t h8 = static_cast<uint8_t>(std::min(_height / 8, 255));

        for(size_t i = 0; i < numPackets; i++) {
                size_t jpegChunk = std::min(maxJpegPerPacket, remaining);
                uint8_t *pkt = bufData + bufOffset;

                // RTP header placeholder
                std::memset(pkt, 0, RtpPacket::HeaderSize);

                // RFC 2435 JPEG header (8 bytes)
                size_t hdr = RtpPacket::HeaderSize;
                // Type-specific: 0
                pkt[hdr + 0] = 0;
                // Fragment offset (24 bits, big-endian)
                pkt[hdr + 1] = static_cast<uint8_t>((fragmentOffset >> 16) & 0xFF);
                pkt[hdr + 2] = static_cast<uint8_t>((fragmentOffset >> 8) & 0xFF);
                pkt[hdr + 3] = static_cast<uint8_t>(fragmentOffset & 0xFF);
                // Type: 1 (JPEG type for YUV 4:2:0)
                pkt[hdr + 4] = 1;
                // Q: quality factor
                pkt[hdr + 5] = static_cast<uint8_t>(_quality & 0x7F);
                // Width/8, Height/8
                pkt[hdr + 6] = w8;
                pkt[hdr + 7] = h8;

                // JPEG data
                std::memcpy(pkt + hdr + Rfc2435HeaderSize, src + fragmentOffset, jpegChunk);

                size_t pktSize = RtpPacket::HeaderSize + Rfc2435HeaderSize + jpegChunk;
                packets.pushToBack(RtpPacket(buf, bufOffset, pktSize));
                bufOffset += pktSize;
                fragmentOffset += jpegChunk;
                remaining -= jpegChunk;
        }
        return packets;
}

Buffer RtpPayloadJpeg::unpack(const RtpPacket::List &packets) {
        // Calculate total JPEG data size
        size_t totalSize = 0;
        for(const auto &pkt : packets) {
                if(!pkt.isNull() && pkt.payloadSize() > Rfc2435HeaderSize) {
                        totalSize += pkt.payloadSize() - Rfc2435HeaderSize;
                }
        }
        Buffer result(totalSize);
        result.setSize(totalSize);
        uint8_t *dst = static_cast<uint8_t *>(result.data());

        for(const auto &pkt : packets) {
                if(pkt.isNull() || pkt.payloadSize() <= Rfc2435HeaderSize) continue;
                const uint8_t *pl = pkt.payload();

                // Extract fragment offset from JPEG header
                uint32_t fragOff = (static_cast<uint32_t>(pl[1]) << 16) |
                                   (static_cast<uint32_t>(pl[2]) << 8) |
                                   static_cast<uint32_t>(pl[3]);
                const uint8_t *jpegData = pl + Rfc2435HeaderSize;
                size_t jpegSize = pkt.payloadSize() - Rfc2435HeaderSize;
                if(fragOff + jpegSize <= totalSize) {
                        std::memcpy(dst + fragOff, jpegData, jpegSize);
                }
        }
        return result;
}

PROMEKI_NAMESPACE_END
