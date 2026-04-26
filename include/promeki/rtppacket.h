/**
 * @file      rtppacket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <cstring>
#include <promeki/bufferview.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A BufferView that interprets its data as an RTP packet.
 * @ingroup network
 *
 * RtpPacket extends BufferView with accessors that read and write
 * the standard 12-byte RTP header directly in the buffer data.
 * No fields are stored separately — the buffer is the source of
 * truth.
 *
 * @par RTP Header Layout (RFC 3550)
 * @code
 * Byte 0:    V(2) | P(1) | X(1) | CC(4)
 * Byte 1:    M(1) | PT(7)
 * Bytes 2-3: Sequence number (big-endian)
 * Bytes 4-7: Timestamp (big-endian)
 * Bytes 8-11: SSRC (big-endian)
 * [Bytes 12..(12+CC*4-1): CSRC list, if CC > 0]
 * [Extension header, if X=1:
 *   2 bytes profile-specific ID
 *   2 bytes extension length (in 32-bit words)
 *   length*4 bytes extension data]
 * @endcode
 *
 * @par Construction
 * The convenience constructor RtpPacket(size_t) allocates a
 * standalone buffer for a single packet.  This is simple but
 * incurs one allocation per packet — for bulk work, use
 * createList() which packs N packets into one shared buffer.
 *
 * @par Example
 * @code
 * // Single packet (convenience)
 * RtpPacket pkt(1400);
 * pkt.setPayloadType(96);
 * pkt.setSequenceNumber(1000);
 *
 * // Bulk allocation — 100 packets sharing one buffer
 * auto pkts = RtpPacket::createList(100, 1400);
 * for(size_t i = 0; i < pkts.size(); ++i) {
 *         pkts[i].setSequenceNumber(i);
 * }
 * @endcode
 */
class RtpPacket : public BufferView {
        public:
                /** @brief List of RtpPacket values. */
                using List = promeki::List<RtpPacket>;

                /** @brief List of sizes. */
                using SizeList = promeki::List<size_t>;

                /** @brief Minimum RTP fixed header size in bytes. */
                static constexpr size_t HeaderSize = 12;

                /** @brief Default constructor. */
                RtpPacket() = default;

                /**
                 * @brief Constructs an RtpPacket referencing a region of a shared buffer.
                 * @param buf The shared backing buffer.
                 * @param offset Byte offset into the buffer where this packet begins.
                 * @param size Byte size of this packet.
                 */
                RtpPacket(Buffer::Ptr buf, size_t offset, size_t size) : BufferView(std::move(buf), offset, size) {}

                /**
                 * @brief Returns true if the buffer pointer is null.
                 *
                 * A null packet has no backing buffer and cannot be read or written.
                 */
                bool isNull() const { return data() == nullptr; }

                /**
                 * @brief Returns true if this is a valid RTP packet.
                 *
                 * A valid packet has a non-null buffer, is large enough to hold
                 * the fixed 12-byte header, has RTP version 2, and is large
                 * enough to contain any CSRC entries and extension header that
                 * it declares.
                 */
                bool isValid() const {
                        if (data() == nullptr) return false;
                        if (size() < HeaderSize) return false;
                        if (((data()[0] >> 6) & 0x03) != 2) return false;
                        return headerSize() != 0;
                }

                /**
                 * @brief Convenience constructor that allocates a buffer for a single packet.
                 * @param packetSize Total packet size in bytes (header + payload).
                 *
                 * Allocates a dedicated buffer, zeroes it, and sets the RTP
                 * version to 2.  This is convenient for one-off packets but
                 * allocates a separate buffer per packet — for bulk packet
                 * construction, prefer createList().
                 */
                explicit RtpPacket(size_t packetSize) : BufferView(Buffer::Ptr::create(packetSize), 0, packetSize) {
                        std::memset(data(), 0, packetSize);
                        setVersion(2);
                }

                /**
                 * @brief Creates a list of N RtpPackets packed into a single shared buffer.
                 * @param count Number of packets to create.
                 * @param packetSize Total size of each packet in bytes (header + payload).
                 * @return A List of RtpPacket objects sharing one buffer.
                 *
                 * All packets are zeroed and have their RTP version set to 2.
                 * This is the preferred way to allocate packets in bulk, as
                 * it avoids per-packet allocation overhead.
                 */
                static List createList(size_t count, size_t packetSize) {
                        List ret;
                        if (count == 0 || packetSize == 0) return ret;
                        size_t totalSize = count * packetSize;
                        auto   buf = Buffer::Ptr::create(totalSize);
                        std::memset(buf->data(), 0, totalSize);
                        ret.reserve(count);
                        for (size_t i = 0; i < count; ++i) {
                                RtpPacket pkt(buf, i * packetSize, packetSize);
                                pkt.setVersion(2);
                                ret.pushToBack(std::move(pkt));
                        }
                        return ret;
                }

                /**
                 * @brief Creates a list of RtpPackets with varying sizes, packed into a single shared buffer.
                 * @param sizes List of per-packet sizes in bytes (header + payload each).
                 * @return A List of RtpPacket objects sharing one buffer.
                 *
                 * Each packet gets the size specified by the corresponding entry
                 * in @p sizes.  All packets are zeroed and have their RTP version
                 * set to 2.  Like the uniform-size overload, this allocates a
                 * single shared buffer to avoid per-packet allocation overhead.
                 */
                static List createList(const SizeList &sizes) {
                        List ret;
                        if (sizes.isEmpty()) return ret;
                        size_t totalSize = 0;
                        for (size_t i = 0; i < sizes.size(); ++i) totalSize += sizes[i];
                        if (totalSize == 0) return ret;
                        auto buf = Buffer::Ptr::create(totalSize);
                        std::memset(buf->data(), 0, totalSize);
                        ret.reserve(sizes.size());
                        size_t offset = 0;
                        for (size_t i = 0; i < sizes.size(); ++i) {
                                RtpPacket pkt(buf, offset, sizes[i]);
                                if (sizes[i] >= HeaderSize) pkt.setVersion(2);
                                ret.pushToBack(std::move(pkt));
                                offset += sizes[i];
                        }
                        return ret;
                }

                /** @brief Returns the RTP version (normally 2). */
                uint8_t version() const { return (hdr()[0] >> 6) & 0x03; }

                /** @brief Sets the RTP version. */
                void setVersion(uint8_t v) { hdr()[0] = (hdr()[0] & 0x3F) | ((v & 0x03) << 6); }

                /** @brief Returns the padding flag. */
                bool padding() const { return (hdr()[0] >> 5) & 0x01; }

                /** @brief Sets the padding flag. */
                void setPadding(bool p) { hdr()[0] = (hdr()[0] & 0xDF) | (p ? 0x20 : 0x00); }

                /** @brief Returns the extension flag. */
                bool extension() const { return (hdr()[0] >> 4) & 0x01; }

                /** @brief Sets the extension flag. */
                void setExtension(bool x) { hdr()[0] = (hdr()[0] & 0xEF) | (x ? 0x10 : 0x00); }

                /** @brief Returns the CSRC count. */
                uint8_t csrcCount() const { return hdr()[0] & 0x0F; }

                /** @brief Returns the marker bit. */
                bool marker() const { return (hdr()[1] >> 7) & 0x01; }

                /** @brief Sets the marker bit. */
                void setMarker(bool m) { hdr()[1] = (hdr()[1] & 0x7F) | (m ? 0x80 : 0x00); }

                /** @brief Returns the payload type (7 bits). */
                uint8_t payloadType() const { return hdr()[1] & 0x7F; }

                /** @brief Sets the payload type. */
                void setPayloadType(uint8_t pt) { hdr()[1] = (hdr()[1] & 0x80) | (pt & 0x7F); }

                /** @brief Returns the sequence number. */
                uint16_t sequenceNumber() const { return (static_cast<uint16_t>(hdr()[2]) << 8) | hdr()[3]; }

                /** @brief Sets the sequence number. */
                void setSequenceNumber(uint16_t seq) {
                        hdr()[2] = static_cast<uint8_t>(seq >> 8);
                        hdr()[3] = static_cast<uint8_t>(seq & 0xFF);
                }

                /** @brief Returns the timestamp. */
                uint32_t timestamp() const {
                        return (static_cast<uint32_t>(hdr()[4]) << 24) | (static_cast<uint32_t>(hdr()[5]) << 16) |
                               (static_cast<uint32_t>(hdr()[6]) << 8) | static_cast<uint32_t>(hdr()[7]);
                }

                /** @brief Sets the timestamp. */
                void setTimestamp(uint32_t ts) {
                        hdr()[4] = static_cast<uint8_t>((ts >> 24) & 0xFF);
                        hdr()[5] = static_cast<uint8_t>((ts >> 16) & 0xFF);
                        hdr()[6] = static_cast<uint8_t>((ts >> 8) & 0xFF);
                        hdr()[7] = static_cast<uint8_t>(ts & 0xFF);
                }

                /** @brief Returns the SSRC. */
                uint32_t ssrc() const {
                        return (static_cast<uint32_t>(hdr()[8]) << 24) | (static_cast<uint32_t>(hdr()[9]) << 16) |
                               (static_cast<uint32_t>(hdr()[10]) << 8) | static_cast<uint32_t>(hdr()[11]);
                }

                /** @brief Sets the SSRC. */
                void setSsrc(uint32_t s) {
                        hdr()[8] = static_cast<uint8_t>((s >> 24) & 0xFF);
                        hdr()[9] = static_cast<uint8_t>((s >> 16) & 0xFF);
                        hdr()[10] = static_cast<uint8_t>((s >> 8) & 0xFF);
                        hdr()[11] = static_cast<uint8_t>(s & 0xFF);
                }

                /**
                 * @brief Returns the full header size including CSRC list and extension.
                 * @return The header size in bytes, or 0 if the packet is too small
                 *         to contain the header fields it declares.
                 *
                 * Computes: 12 + CC*4 + (if X=1: 4 + extensionLength*4).
                 * Returns 0 if the packet's size() is too small to hold the
                 * declared CSRC entries or extension data.
                 */
                size_t headerSize() const {
                        if (size() < HeaderSize) return 0;
                        size_t hs = HeaderSize + csrcCount() * 4;
                        if (hs > size()) return 0;
                        if (extension()) {
                                // Need at least 4 bytes for the extension header
                                if (hs + 4 > size()) return 0;
                                const uint8_t *ext = data() + hs;
                                uint16_t       extLen = (static_cast<uint16_t>(ext[2]) << 8) | ext[3];
                                hs += 4 + extLen * 4;
                                if (hs > size()) return 0;
                        }
                        return hs;
                }

                /**
                 * @brief Returns the extension profile ID, or 0 if no extension is present.
                 * @return The 16-bit profile-specific extension identifier.
                 */
                uint16_t extensionProfile() const {
                        if (!extension()) return 0;
                        size_t extOffset = HeaderSize + csrcCount() * 4;
                        if (extOffset + 4 > size()) return 0;
                        const uint8_t *ext = data() + extOffset;
                        return (static_cast<uint16_t>(ext[0]) << 8) | ext[1];
                }

                /**
                 * @brief Returns the extension data length in 32-bit words, or 0 if no extension.
                 * @return Extension length in 32-bit words.
                 */
                uint16_t extensionLength() const {
                        if (!extension()) return 0;
                        size_t extOffset = HeaderSize + csrcCount() * 4;
                        if (extOffset + 4 > size()) return 0;
                        const uint8_t *ext = data() + extOffset;
                        return (static_cast<uint16_t>(ext[2]) << 8) | ext[3];
                }

                /**
                 * @brief Returns a pointer to the payload data (after the full header).
                 * @return Pointer to the payload, or nullptr if the packet is too small.
                 *
                 * Accounts for CSRC entries and the extension header when present.
                 */
                const uint8_t *payload() const {
                        size_t hs = headerSize();
                        if (hs == 0 || hs >= size()) return nullptr;
                        return data() + hs;
                }

                /** @copydoc payload() const */
                uint8_t *payload() {
                        size_t hs = headerSize();
                        if (hs == 0 || hs >= size()) return nullptr;
                        return data() + hs;
                }

                /**
                 * @brief Returns the payload size in bytes.
                 * @return Payload size, or 0 if the packet is too small for its header.
                 *
                 * Accounts for CSRC entries and the extension header when present.
                 */
                size_t payloadSize() const {
                        size_t hs = headerSize();
                        if (hs == 0 || hs >= size()) return 0;
                        return size() - hs;
                }

                /**
                 * @brief Zeroes the packet data and resets the RTP version to 2.
                 *
                 * Useful for reusing packets from a pre-allocated pool.
                 */
                void clear() {
                        if (isNull()) return;
                        std::memset(data(), 0, size());
                        setVersion(2);
                }

        private:
                const uint8_t *hdr() const {
                        assert(data() != nullptr && size() >= HeaderSize);
                        return data();
                }
                uint8_t *hdr() {
                        assert(data() != nullptr && size() >= HeaderSize);
                        return data();
                }
};

PROMEKI_NAMESPACE_END
