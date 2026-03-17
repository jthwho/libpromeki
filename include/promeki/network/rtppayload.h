/**
 * @file      network/rtppayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/core/buffer.h>
#include <promeki/network/rtppacket.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base class for RTP payload type handlers.
 * @ingroup network
 *
 * RtpPayload defines the interface for packing media data into
 * RTP payload packets and unpacking packets back into media data.
 * Packing produces a list of RtpPackets that share a single
 * buffer allocation for efficiency.
 *
 * Concrete subclasses implement specific payload formats:
 * - RtpPayloadL24 — 24-bit linear audio (AES67)
 * - RtpPayloadL16 — 16-bit linear audio
 * - RtpPayloadRawVideo — RFC 4175 raw video (ST 2110-20)
 * - RtpPayloadJpeg — RFC 2435 JPEG (Motion JPEG)
 */
class RtpPayload {
        public:
                /** @brief Destructor. */
                virtual ~RtpPayload() = default;

                /** @brief Returns the RTP payload type number. */
                virtual uint8_t payloadType() const = 0;

                /** @brief Returns the RTP timestamp clock rate in Hz. */
                virtual uint32_t clockRate() const = 0;

                /**
                 * @brief Fragments media data into RTP payload packets.
                 *
                 * Each returned RtpPacket includes space for the 12-byte RTP
                 * header at the start, followed by any payload-specific header,
                 * followed by the media data fragment. All returned packets
                 * share a single Buffer::Ptr.
                 *
                 * @param mediaData Pointer to the media data.
                 * @param size Size of the media data in bytes.
                 * @return List of RtpPackets sharing one buffer.
                 */
                virtual RtpPacket::List pack(const void *mediaData, size_t size) = 0;

                /**
                 * @brief Reassembles RTP payload packets into media data.
                 * @param packets The list of packets to reassemble.
                 * @return Buffer containing the reassembled media data.
                 */
                virtual Buffer unpack(const RtpPacket::List &packets) = 0;

                /**
                 * @brief Returns the maximum payload size per packet.
                 *
                 * Default is 1200 bytes (MTU-safe, accounts for IP/UDP
                 * headers within a 1500-byte Ethernet MTU).
                 *
                 * @return Maximum payload bytes per packet.
                 */
                virtual size_t maxPayloadSize() const { return 1200; }

        protected:
                /** @brief Default constructor (protected; use a concrete subclass). */
                RtpPayload() = default;
};

/**
 * @brief RTP payload handler for 24-bit linear audio (L24).
 * @ingroup network
 *
 * Implements packing/unpacking of 24-bit linear PCM audio samples
 * as used by AES67 and ST 2110-30. Samples are packed in network
 * byte order (big-endian), interleaved by channel.
 *
 * The payload type defaults to 97 (dynamic range). Clock rate
 * matches the audio sample rate (typically 48000 Hz).
 *
 * @par Example
 * @code
 * RtpPayloadL24 payload(48000, 2); // 48kHz, stereo
 * auto packets = payload.pack(audioData, audioSize);
 * @endcode
 */
class RtpPayloadL24 : public RtpPayload {
        public:
                /**
                 * @brief Constructs an L24 payload handler.
                 * @param sampleRate Audio sample rate in Hz (default 48000).
                 * @param channels Number of audio channels (default 2).
                 */
                RtpPayloadL24(uint32_t sampleRate = 48000, int channels = 2);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return _sampleRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /** @brief Sets the RTP payload type number. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Returns the number of audio channels. */
                int channels() const { return _channels; }

                /** @brief Returns the audio sample rate. */
                uint32_t sampleRate() const { return _sampleRate; }

        private:
                uint32_t _sampleRate;
                int      _channels;
                uint8_t  _payloadType = 97;
};

/**
 * @brief RTP payload handler for 16-bit linear audio (L16).
 * @ingroup network
 *
 * Implements packing/unpacking of 16-bit linear PCM audio samples.
 * Samples are in network byte order (big-endian), interleaved by channel.
 *
 * @par Example
 * @code
 * RtpPayloadL16 payload(48000, 2);
 * auto packets = payload.pack(audioData, audioSize);
 * @endcode
 */
class RtpPayloadL16 : public RtpPayload {
        public:
                /**
                 * @brief Constructs an L16 payload handler.
                 * @param sampleRate Audio sample rate in Hz (default 48000).
                 * @param channels Number of audio channels (default 2).
                 */
                RtpPayloadL16(uint32_t sampleRate = 48000, int channels = 2);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return _sampleRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /** @brief Sets the RTP payload type number. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Returns the number of audio channels. */
                int channels() const { return _channels; }

                /** @brief Returns the audio sample rate. */
                uint32_t sampleRate() const { return _sampleRate; }

        private:
                uint32_t _sampleRate;
                int      _channels;
                uint8_t  _payloadType = 96;
};

/**
 * @brief RTP payload handler for RFC 4175 raw video (ST 2110-20).
 * @ingroup network
 *
 * Implements packing/unpacking of uncompressed video frames as
 * defined by RFC 4175 for use with SMPTE ST 2110-20. Each packet
 * carries one or more scan line segments with a per-line header.
 *
 * @par RFC 4175 Packet Layout
 * @code
 * +--12 bytes--+--2 bytes--+--per-line headers--+--pixel data--+
 * | RTP Header | Extended  | Line 1 Header (6B) | Pixel Data   |
 * |            | Seq Num   | [Line 2 Header...]  |              |
 * +------------+-----------+--------------------+--------------+
 * @endcode
 *
 * Each per-line header is 6 bytes:
 * - 2 bytes: data length for this line segment
 * - 2 bytes: line number
 * - 2 bytes: field ID (1 bit) + offset (15 bits) + continuation (1 bit)
 *
 * @par Example
 * @code
 * RtpPayloadRawVideo payload(1920, 1080, 24); // 1080p, 24 bits/pixel
 * auto packets = payload.pack(frameData, frameSize);
 * @endcode
 */
class RtpPayloadRawVideo : public RtpPayload {
        public:
                /**
                 * @brief Constructs a raw video payload handler.
                 * @param width Frame width in pixels.
                 * @param height Frame height in pixels.
                 * @param bitsPerPixel Bits per pixel (e.g. 24 for RGB8).
                 */
                RtpPayloadRawVideo(int width, int height, int bitsPerPixel);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return 90000; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /** @brief Sets the RTP payload type number. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Returns the frame width. */
                int width() const { return _width; }

                /** @brief Returns the frame height. */
                int height() const { return _height; }

                /** @brief Returns bits per pixel. */
                int bitsPerPixel() const { return _bitsPerPixel; }

        private:
                int      _width;
                int      _height;
                int      _bitsPerPixel;
                uint8_t  _payloadType = 96;
};

/**
 * @brief RTP payload handler for RFC 2435 JPEG (Motion JPEG).
 * @ingroup network
 *
 * Implements packing/unpacking of JPEG-compressed video frames
 * as defined by RFC 2435. Each packet carries an 8-byte JPEG
 * header followed by a fragment of the JPEG data.
 *
 * @par RFC 2435 JPEG Header (8 bytes)
 * @code
 * +---------+---------+---------+---------+
 * | Type-   | Fragment Offset   | Type    |
 * | specific|  (24 bits)        |         |
 * +---------+---------+---------+---------+
 * | Q       | Width/8 | Height/8|         |
 * +---------+---------+---------+---------+
 * @endcode
 *
 * @par Example
 * @code
 * RtpPayloadJpeg payload(1920, 1080);
 * auto packets = payload.pack(jpegData, jpegSize);
 * @endcode
 */
class RtpPayloadJpeg : public RtpPayload {
        public:
                /**
                 * @brief Constructs a JPEG payload handler.
                 * @param width Image width in pixels.
                 * @param height Image height in pixels.
                 * @param quality JPEG quality parameter for RTP header (1-99, default 85).
                 */
                RtpPayloadJpeg(int width, int height, int quality = 85);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return 26; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return 90000; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /** @brief Returns the image width. */
                int width() const { return _width; }

                /** @brief Returns the image height. */
                int height() const { return _height; }

                /** @brief Returns the quality parameter. */
                int quality() const { return _quality; }

                /** @brief Sets the quality parameter. */
                void setQuality(int q) { _quality = q; }

        private:
                int _width;
                int _height;
                int _quality;
};

PROMEKI_NAMESPACE_END
