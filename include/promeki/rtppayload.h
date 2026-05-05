/**
 * @file      rtppayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/buffer.h>
#include <promeki/rtppacket.h>

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
                 * share a single Buffer.
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
                 * headers within a 1500-byte Ethernet MTU).  Callers
                 * that need tight per-packet sizing (e.g. AES67 audio
                 * where each packet must carry exactly one packet-time
                 * interval worth of samples) can override this via
                 * @ref setMaxPayloadSize().
                 *
                 * @return Maximum payload bytes per packet.
                 */
                virtual size_t maxPayloadSize() const { return _maxPayloadSize; }

                /**
                 * @brief Overrides the maximum payload size per packet.
                 *
                 * Used by the audio RTP path to force exact AES67 packet
                 * sizes (e.g. 192 bytes for stereo L16 @ 1ms at 48 kHz).
                 * Must be called before @ref pack().  Passing 0 restores
                 * the default MTU-safe size.
                 *
                 * @param bytes Maximum payload bytes per packet (0 = default).
                 */
                void setMaxPayloadSize(size_t bytes) { _maxPayloadSize = (bytes == 0) ? 1200 : bytes; }

        protected:
                /** @brief Default constructor (protected; use a concrete subclass). */
                RtpPayload() = default;

                /** @brief Maximum bytes per RTP payload (default: 1200, MTU-safe). */
                size_t _maxPayloadSize = 1200;
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
                 * @param pgroupBytes RFC 4175 pixel group (pgroup) size in bytes.
                 *        A pgroup is the smallest byte-aligned unit of pixel
                 *        data — e.g. 3 for RGB 8-bit, 4 for YCbCr-4:2:2 8-bit.
                 *        Chunk sizes are aligned to this boundary so that a
                 *        pixel is never split across packets.  If 0, the pgroup
                 *        is inferred as bitsPerPixel/8 (correct for formats
                 *        where one pixel equals one pgroup).
                 */
                RtpPayloadRawVideo(int width, int height, int bitsPerPixel, int pgroupBytes = 0);

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

                /** @brief Returns the pgroup size in bytes. */
                int pgroupBytes() const { return _pgroupBytes; }

        private:
                int     _width;
                int     _height;
                int     _bitsPerPixel;
                int     _pgroupBytes;
                uint8_t _payloadType = 96;
};

/**
 * @brief RTP payload handler for JSON blobs.
 * @ingroup network
 *
 * Implements packing/unpacking of arbitrary JSON-serialized
 * messages over RTP.  This is the fallback metadata-stream payload
 * used by @c MediaIO_Rtp when the user wants to ship the
 * per-frame @ref Metadata object as-is without committing to the
 * SMPTE ST 2110-40 Ancillary Data wire format.  It is deliberately
 * simple: each RTP packet carries a raw fragment of the JSON bytes,
 * in order, and the RTP marker bit on the last packet of each
 * message signals end-of-message to the receiver.
 *
 * No custom in-payload header is added — the only framing is the
 * RTP sequence number (for ordering) and the marker bit (for
 * message boundaries).  A full message is reassembled by
 * concatenating the payloads of consecutive packets sharing the
 * same RTP timestamp, terminated by the marker bit.
 *
 * @par Wire format
 * @code
 * +--12 bytes--+------ up to maxPayloadSize bytes ------+
 * | RTP Header | JSON bytes (fragment of the message)    |
 * +------------+----------------------------------------+
 * @endcode
 *
 * Since there is no payload-level header, the RTP payload type
 * must be a dynamic type (96-127); the default is 98.  The clock
 * rate matches the video reference clock (90000 Hz) so the
 * metadata stream timestamps can be cross-correlated with a video
 * RTP stream.
 */
class RtpPayloadJson : public RtpPayload {
        public:
                /**
                 * @brief Constructs a JSON payload handler.
                 * @param payloadType Dynamic payload type (96-127, default 98).
                 * @param clockRate   RTP clock rate in Hz (default 90000).
                 */
                RtpPayloadJson(uint8_t payloadType = 98, uint32_t clockRate = 90000);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return _clockRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /** @brief Sets the RTP payload type number. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Sets the RTP clock rate. */
                void setClockRate(uint32_t hz) { _clockRate = hz; }

        private:
                uint8_t  _payloadType;
                uint32_t _clockRate;
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

/**
 * @brief RTP payload handler for RFC 9134 JPEG XS.
 * @ingroup network
 *
 * Implements the RTP payload format for JPEG XS (ISO/IEC 21122)
 * as defined in RFC 9134.  The first-pass implementation runs in
 * codestream packetization mode (K=0): the whole JPEG XS
 * codestream is handed to @ref pack() as one blob, fragmented
 * into MTU-sized chunks, and each fragment gets a 4-byte payload
 * header prepended.  Slice packetization mode (K=1) and
 * interlaced framing are followups.
 *
 * @par RFC 9134 payload header (4 bytes)
 * @code
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |T|K|L| I |F counter|     SEP counter     |     P counter       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * @endcode
 *
 * - @b T  (1 bit):  Transmission mode. 1 = sequential (default).
 * - @b K  (1 bit):  Packetization mode. 0 = codestream (this class),
 *                   1 = slice (followup).
 * - @b L  (1 bit):  Last packet of the current packetization unit.
 *                   Set on the final packet of each frame.
 * - @b I  (2 bits): Interlace info.  00 = progressive (this class).
 * - @b F  (5 bits): Frame counter modulo 32.  Incremented on every
 *                   call to @ref pack(); identical across all packets
 *                   of the same frame.
 * - @b SEP(11 bits): Slice/Extended-Packet counter.  In codestream
 *                   mode, this increments every time the P counter
 *                   wraps from 2047 to 0 within a single frame.
 * - @b P  (11 bits): Packet counter within the current packetization
 *                   unit.  Starts at 0 for each new frame and wraps
 *                   at 2048.
 *
 * The RTP marker bit (set by @ref RtpSession::sendPackets) signals
 * the last packet of the video frame and is independent of the L
 * bit in codestream mode (where both mark the same packet).  The
 * RTP timestamp uses a 90 kHz clock per the RFC and is constant
 * across all packets of one frame.
 *
 * @par Example
 * @code
 * RtpPayloadJpegXs payload(1920, 1080);
 * auto packets = payload.pack(jxsData, jxsSize);
 * // packets[0..N-1] carry the 4-byte header + bitstream fragment;
 * // payload.frameCounter() advances after each pack() call.
 * @endcode
 */
class RtpPayloadJpegXs : public RtpPayload {
        public:
                /// @brief RFC 9134 payload header size in bytes.
                static constexpr size_t HeaderSize = 4;

                /// @brief RTP clock rate for JPEG XS (fixed at 90 kHz per RFC).
                static constexpr uint32_t ClockRate = 90000;

                /**
                 * @brief Constructs a JPEG XS payload handler.
                 * @param width       Frame width in pixels.
                 * @param height      Frame height in pixels.
                 * @param payloadType RTP payload type (dynamic, default 96).
                 */
                RtpPayloadJpegXs(int width, int height, uint8_t payloadType = 96);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return ClockRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /// @brief Overrides the RTP payload type number.
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /// @brief Returns the frame width.
                int width() const { return _width; }
                /// @brief Returns the frame height.
                int height() const { return _height; }

                /**
                 * @brief Returns the next frame counter value that will be
                 * written into the F field, modulo 32.  Advances on each
                 * successful @ref pack() call.
                 */
                uint8_t frameCounter() const { return _frameCounter; }

                /// @brief Resets the frame counter to 0 (testing hook).
                void resetFrameCounter() { _frameCounter = 0; }

        private:
                int     _width;
                int     _height;
                uint8_t _payloadType;
                uint8_t _frameCounter = 0;
};

PROMEKI_NAMESPACE_END
