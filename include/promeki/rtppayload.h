/**
 * @file      rtppayload.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
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
 * - RtpPayloadJpegXs — RFC 9134 JPEG XS
 * - RtpPayloadH264 — RFC 6184 H.264 / AVC
 * - RtpPayloadH265 — RFC 7798 H.265 / HEVC
 * - RtpPayloadJson — JSON metadata
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
                 * @brief Outcome of @ref validate.
                 *
                 * Codec-aware mid-stream-join gating: the
                 * depacketizer thread calls @ref validate after
                 * each successful @ref unpack to decide whether the
                 * reassembled payload should be emitted to
                 * downstream stages.  Three values:
                 * - @c Accept — the payload is decodable; emit it.
                 * - @c DropSilently — the payload is incomplete or
                 *   pre-decodable (e.g. the receiver joined
                 *   mid-frame); drop without logging.
                 * - @c Wait — the codec needs out-of-band
                 *   information (paramSets / IDR) before this
                 *   payload makes sense; drop and log once per
                 *   distinct cause so an operator can see the
                 *   decoder is waiting.
                 *
                 * The base @ref validate returns @c Accept for any
                 * non-empty buffer and @c DropSilently for empty.
                 * Codec subclasses override with format-specific
                 * gating in Phase 4 (H.264 / H.265 paramSet + IDR).
                 */
                enum class ValidateResult {
                        Accept,
                        DropSilently,
                        Wait,
                };

                /**
                 * @brief Codec-aware payload validation hook.
                 *
                 * Called by the per-stream depacketizer thread on
                 * each reassembled @c Buffer the @ref unpack call
                 * produced.  Default implementation accepts any
                 * non-empty buffer; codec subclasses override
                 * (@ref RtpPayloadH264 / @ref RtpPayloadH265) to
                 * enforce mid-stream-join requirements such as
                 * SPS/PPS observed before the first IDR access
                 * unit is shipped to the decoder.
                 *
                 * Non-const because the override may update
                 * internal mid-join state — e.g. latching
                 * @c paramSets-observed once an SPS / PPS is seen
                 * in-band, or arming the IDR-required gate.  The
                 * depacketizer that owns the @ref RtpPayload calls
                 * this through a non-const pointer.
                 *
                 * @param unpacked The buffer returned by @ref unpack.
                 * @return One of @ref ValidateResult.
                 */
                virtual ValidateResult validate(const Buffer &unpacked) {
                        return unpacked.size() > 0 ? ValidateResult::Accept
                                                   : ValidateResult::DropSilently;
                }

                /**
                 * @brief Clears any out-of-band parameter-set state
                 *        the codec is tracking.
                 *
                 * Called by the depacketizer on SSRC reset / stream
                 * restart so the codec's mid-join gate (e.g.
                 * H.264 SPS/PPS observed) re-arms.  Default is a
                 * no-op for codecs without paramSet state
                 * (RFC 4175, RFC 2435).
                 */
                virtual void clearParamSets() {}

                /**
                 * @brief Returns @c true if this payload carries JFIF
                 *        / RFC 2435 MJPEG bitstreams.
                 *
                 * The video depacketizer uses this to gate
                 * @ref JpegGeometryProbe — only RFC 2435 streams need
                 * the JFIF marker walk to discover dimensions.  Other
                 * codecs either carry geometry in the SDP fmtp line
                 * (RFC 4175, RFC 9134) or in their bitstream's SPS
                 * (H.264 / HEVC) which @c configureVideoStream parses
                 * separately.
                 *
                 * Default is @c false; @ref RtpPayloadJpeg overrides.
                 */
                virtual bool isJpeg() const { return false; }

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

// RFC 4175 / ST 2110-20 uncompressed video lives in its own header
// (rtppayloadrawvideo.h) so the full SRD / ESN / pgroup machinery can
// be reviewed independently of the other RTP payload classes.

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
                /** @copydoc RtpPayload::isJpeg() */
                bool isJpeg() const override { return true; }

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

                /**
                 * @brief Selects RFC 9134 §4.3 packetization mode.
                 *
                 * @c false (default) — codestream mode (K=0): the
                 * codestream is split into MTU-sized fragments
                 * without regard to slice boundaries.  Today's
                 * behaviour.
                 *
                 * @c true — slice mode (K=1): the packetizer walks
                 * the codestream's @c SLH markers via
                 * @ref JxsMarker::parse and emits one or more
                 * complete slices per RTP packet (never fragmenting
                 * a slice across packets).  The first packet of
                 * each frame additionally carries the main-header
                 * marker segments.  Falls back to codestream mode
                 * silently when the codestream is malformed or has
                 * no SLH markers.
                 *
                 * @param sliceMode @c true to enable K=1.
                 */
                void setSliceMode(bool sliceMode) { _sliceMode = sliceMode; }

                /// @brief Returns the active packetization mode.
                bool isSliceMode() const { return _sliceMode; }

        private:
                int     _width;
                int     _height;
                uint8_t _payloadType;
                uint8_t _frameCounter = 0;
                bool    _sliceMode = false;
};

/**
 * @brief RTP payload handler for RFC 6184 H.264 / AVC.
 * @ingroup network
 *
 * Implements the H.264 RTP payload format defined in RFC 6184.
 * Operates in **non-interleaved mode** (@c packetization-mode=1):
 * each access unit is delivered as a sequence of single-NAL packets
 * and FU-A fragmentation units, in coding order, with the RTP marker
 * bit set on the last packet of the access unit.
 *
 * @par Input format (writer side)
 * @ref pack expects an Annex-B byte stream — one or more NAL units,
 * each preceded by a 3- or 4-byte start code (@c 00 00 01 or
 * @c 00 00 00 01).  This is what NVENC, x264, and most software
 * encoders emit by default.  AVCC (length-prefixed) inputs are not
 * accepted; convert with @ref H264Bitstream::avccToAnnexB first.
 *
 * @par Wire format (writer side)
 * For each NAL in the input:
 *  - **Single-NAL packet** when @c nalSize ≤ @ref maxPayloadSize:
 *    the NAL bytes are copied verbatim into the RTP payload (the
 *    first byte is the original H.264 NAL header — @c F | @c NRI |
 *    @c type, with @c type in @c 1..23).
 *  - **FU-A fragmentation** when @c nalSize > @ref maxPayloadSize:
 *    the NAL is split into two or more fragments, each prefixed
 *    with a 2-byte FU header.  The original NAL header byte is
 *    consumed by the FU; only the NAL payload bytes are
 *    re-distributed across fragments:
 *    @code
 *    +---------------+---------------+----- payload bytes -----+
 *    | FU indicator  |  FU header    |  NAL payload fragment   |
 *    | F NRI 28      | S E R type    |   (NAL[1 .. ])          |
 *    +---------------+---------------+-------------------------+
 *    @endcode
 *    @c S=1 marks the first fragment, @c E=1 the last; the
 *    reconstructed NAL header on the receive side is
 *    @c (FU.indicator & 0xE0) | (FU.header & 0x1F).
 *
 * STAP-A aggregation (multiple small NALs in one packet) is *not*
 * emitted by this writer in v1 — every NAL becomes its own packet
 * pair (single or FU-A).  @ref unpack still recognises STAP-A on
 * receive for interop with senders that aggregate.
 *
 * @par Reader behaviour
 * @ref unpack reassembles a packet list (which the caller has
 * collected up to the RTP marker bit) back into an Annex-B access
 * unit with 4-byte start codes.  Recognised packet types:
 *  - @c type @c 1..23 — single NAL.
 *  - @c type @c 24 — STAP-A aggregation; inner NALs are extracted
 *    using the standard 2-byte length-prefix layout.
 *  - @c type @c 28 — FU-A; fragments are accumulated until @c E=1
 *    delivers the assembled NAL.
 *  - Other types (STAP-B, MTAP16, MTAP24, FU-B) are silently
 *    skipped — no real-world sender uses them in
 *    @c packetization-mode=1, but rejecting outright would refuse
 *    streams that have a single stray packet.
 *
 * @par RTP timing
 * All packets that belong to one access unit share the same RTP
 * timestamp (90 kHz clock per RFC 6184 §5.1).  The timestamp and
 * marker-bit policy are managed by @ref RtpSession; @ref pack only
 * builds the payload bytes.
 *
 * @par Parameter sets
 * SPS / PPS NAL units carried in the access unit are packetised the
 * same way as VCL NALs (single-NAL or FU-A), so an in-band IDR with
 * its own SPS/PPS round-trips without special handling.  Out-of-band
 * delivery via the SDP @c sprop-parameter-sets fmtp parameter is a
 * separate path managed by @ref RtpMediaIO and does not pass through
 * this class.
 *
 * @par Example
 * @code
 * RtpPayloadH264 payload(96);
 * auto packets = payload.pack(annexBBytes, annexBSize);
 * @endcode
 */
class RtpPayloadH264 : public RtpPayload {
        public:
                /// @brief RTP clock rate for H.264 (fixed at 90 kHz per RFC 6184).
                static constexpr uint32_t ClockRate = 90000;

                /// @brief NAL unit type for FU-A fragmentation units (RFC 6184 §5.8).
                static constexpr uint8_t NalTypeFuA = 28;

                /// @brief NAL unit type for STAP-A aggregation packets (RFC 6184 §5.7.1).
                static constexpr uint8_t NalTypeStapA = 24;

                /// @brief NAL unit type for an IDR-coded slice (RFC 6184 / ISO 14496-10 §7.4.1.2.1).
                static constexpr uint8_t NalTypeIdr = 5;

                /// @brief NAL unit type for a Sequence Parameter Set.
                static constexpr uint8_t NalTypeSps = 7;

                /// @brief NAL unit type for a Picture Parameter Set.
                static constexpr uint8_t NalTypePps = 8;

                /**
                 * @brief Constructs an H.264 RTP payload handler.
                 * @param payloadType Dynamic RTP payload type (96-127, default 96).
                 */
                explicit RtpPayloadH264(uint8_t payloadType = 96);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return ClockRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /**
                 * @brief Codec-aware mid-stream-join gate.
                 *
                 * Walks @p unpacked for SPS (NAL type 7), PPS (NAL
                 * type 8), and IDR (NAL type 5) NALs, updating the
                 * per-stream paramSet-observed and IDR-latched
                 * flags.  Returns:
                 *  - @c Wait if either SPS or PPS has not yet been
                 *    observed (in-band or via
                 *    @ref setSpropParameterSets).  Logs once per
                 *    distinct cause.
                 *  - @c DropSilently if the AU is pre-IDR — the
                 *    decoder cannot start here.
                 *  - @c Accept once the first IDR has been seen and
                 *    paramSets are present, plus all subsequent
                 *    AUs (P / B frames) until @ref clearParamSets
                 *    re-arms the latch.
                 */
                ValidateResult validate(const Buffer &unpacked) override;

                /**
                 * @brief Re-arms the mid-stream-join gate.
                 *
                 * Called by the depacketizer on SSRC reset / stream
                 * restart so a brand-new GOP starts under the same
                 * paramSet-and-IDR gate as a fresh open.
                 */
                void clearParamSets() override;

                /**
                 * @brief Seeds @ref validate paramSet state from an
                 *        SDP @c sprop-parameter-sets fmtp value.
                 *
                 * RFC 6184 §8.1: comma-separated list of base64-
                 * encoded NAL units (typically one SPS and one
                 * PPS, in either order).  Decoding errors and
                 * unrecognised NAL types are silently ignored —
                 * the in-band path will fill in any gap on the
                 * first IDR.
                 *
                 * @param csv The raw value of the
                 *            @c sprop-parameter-sets fmtp parameter
                 *            (no @c "sprop-parameter-sets=" prefix).
                 */
                void setSpropParameterSets(const String &csv);

                /// @brief Returns @c true once an SPS has been
                ///        observed (in-band or via
                ///        @ref setSpropParameterSets).
                bool spsObserved() const { return _spsObserved; }

                /// @brief Returns @c true once a PPS has been
                ///        observed.
                bool ppsObserved() const { return _ppsObserved; }

                /// @brief Returns @c true once the first IDR has
                ///        been latched and subsequent AUs are
                ///        being accepted.
                bool idrLatched() const { return _idrLatched; }

                /// @brief Overrides the RTP payload type number.
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

        private:
                uint8_t _payloadType;
                bool    _spsObserved = false;
                bool    _ppsObserved = false;
                bool    _idrLatched = false;
                bool    _loggedWaitForParamSets = false;
};

/**
 * @brief RTP payload handler for RFC 7798 H.265 / HEVC.
 * @ingroup network
 *
 * Implements the HEVC RTP payload format defined in RFC 7798.  The
 * writer operates in **non-interleaved, no-DON mode** (the receiver
 * is expected to advertise @c sprop-max-don-diff=0 in its SDP) and
 * emits one of two packet shapes per source NAL:
 *  - **Single NAL** when @c nalSize ≤ @ref maxPayloadSize: NAL bytes
 *    copied verbatim; payload type field is the original 6-bit HEVC
 *    @c nal_unit_type (@c 0..47).
 *  - **Fragmentation Unit (FU)** when @c nalSize > @ref maxPayloadSize:
 *    split into two or more fragments.  HEVC's NAL header is two
 *    bytes (versus H.264's one), so an FU packet's header is three
 *    bytes total:
 *    @code
 *    +-------+-------+-------+----- payload bytes -----+
 *    | PHdr0 | PHdr1 | FUHdr |  NAL payload fragment   |
 *    | F  49 layerId | S E T |   (NAL[2 .. ])          |
 *    +---------------+-------+-------------------------+
 *    @endcode
 *    The 2-byte payload header carries the same @c F /
 *    @c nuh_layer_id / @c nuh_temporal_id_plus1 as the source NAL,
 *    with @c nal_unit_type replaced by 49 (FU).  The 1-byte FU
 *    header has @c S=1 on the first fragment, @c E=1 on the last,
 *    and the original NAL type in the low 6 bits.  Reassembly
 *    rebuilds the original 2-byte NAL header as
 *    @c byte0 = (PHdr0 & 0x81) | ((FUHdr & 0x3F) << 1) and
 *    @c byte1 = PHdr1.
 *
 * @par Reader behaviour
 * @ref unpack reassembles a packet list back into an Annex-B access
 * unit with 4-byte start codes.  Recognised packet types:
 *  - @c type @c 0..47 — single NAL.
 *  - @c type @c 48 — Aggregation Packet (AP); inner NALs are
 *    extracted using the standard 2-byte length-prefix layout, with
 *    no DONL / DOND bytes (sprop-max-don-diff=0 assumed).
 *  - @c type @c 49 — Fragmentation Unit; assembled across
 *    @c S..E fragments.
 *  - @c type @c 50 — PACI; not implemented (rare).
 *
 * @par RTP timing
 * All packets that belong to one access unit share the same RTP
 * timestamp (90 kHz clock per RFC 7798 §4.1).
 *
 * @par Example
 * @code
 * RtpPayloadH265 payload(96);
 * auto packets = payload.pack(annexBBytes, annexBSize);
 * @endcode
 */
class RtpPayloadH265 : public RtpPayload {
        public:
                /// @brief RTP clock rate for HEVC (fixed at 90 kHz per RFC 7798).
                static constexpr uint32_t ClockRate = 90000;

                /// @brief NAL unit type for HEVC Fragmentation Unit packets (RFC 7798 §4.4.3).
                static constexpr uint8_t NalTypeFu = 49;

                /// @brief NAL unit type for HEVC Aggregation Packets (RFC 7798 §4.4.2).
                static constexpr uint8_t NalTypeAp = 48;

                /// @brief NAL unit type for a Video Parameter Set (HEVC §7.3.2.1).
                static constexpr uint8_t NalTypeVps = 32;

                /// @brief NAL unit type for a Sequence Parameter Set (HEVC §7.3.2.2).
                static constexpr uint8_t NalTypeSps = 33;

                /// @brief NAL unit type for a Picture Parameter Set (HEVC §7.3.2.3).
                static constexpr uint8_t NalTypePps = 34;

                /// @brief Lowest NAL unit type in the IRAP range (HEVC §7.4.2.2: BLA_W_LP).
                static constexpr uint8_t IrapNalTypeMin = 16;

                /// @brief Highest NAL unit type in the IRAP range (HEVC §7.4.2.2: reserved
                ///        random-access type, treated as IRAP for keyframe gating).
                static constexpr uint8_t IrapNalTypeMax = 23;

                /**
                 * @brief Constructs an H.265 / HEVC RTP payload handler.
                 * @param payloadType Dynamic RTP payload type (96-127, default 96).
                 */
                explicit RtpPayloadH265(uint8_t payloadType = 96);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return ClockRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /**
                 * @brief Codec-aware mid-stream-join gate (HEVC).
                 *
                 * Walks @p unpacked for VPS (NAL type 32), SPS
                 * (33), PPS (34), and IRAP (16-23) NALs.  Returns:
                 *  - @c Wait until VPS, SPS, and PPS have all been
                 *    observed.  Logs once per distinct cause.
                 *  - @c DropSilently for pre-IRAP AUs.
                 *  - @c Accept once the first IRAP has been seen
                 *    and all three paramSets are present, plus all
                 *    subsequent AUs until @ref clearParamSets
                 *    re-arms the latch.
                 */
                ValidateResult validate(const Buffer &unpacked) override;

                /**
                 * @brief Re-arms the mid-stream-join gate.
                 *
                 * Called by the depacketizer on SSRC reset.
                 */
                void clearParamSets() override;

                /**
                 * @brief Seeds VPS-observed state from an SDP
                 *        @c sprop-vps fmtp value.
                 *
                 * RFC 7798 §7.1: comma-separated list of base64-
                 * encoded VPS NALs.  Decoding errors and NALs of
                 * an unexpected type are ignored.
                 */
                void setSpropVps(const String &csv);

                /// @brief Like @ref setSpropVps, for SPS NALs
                ///        (sprop-sps fmtp).
                void setSpropSps(const String &csv);

                /// @brief Like @ref setSpropVps, for PPS NALs
                ///        (sprop-pps fmtp).
                void setSpropPps(const String &csv);

                /// @brief Returns @c true once a VPS has been
                ///        observed.
                bool vpsObserved() const { return _vpsObserved; }

                /// @brief Returns @c true once an SPS has been
                ///        observed.
                bool spsObserved() const { return _spsObserved; }

                /// @brief Returns @c true once a PPS has been
                ///        observed.
                bool ppsObserved() const { return _ppsObserved; }

                /// @brief Returns @c true once the first IRAP has
                ///        been latched and subsequent AUs are
                ///        being accepted.
                bool irapLatched() const { return _irapLatched; }

                /// @brief Overrides the RTP payload type number.
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

        private:
                uint8_t _payloadType;
                bool    _vpsObserved = false;
                bool    _spsObserved = false;
                bool    _ppsObserved = false;
                bool    _irapLatched = false;
                bool    _loggedWaitForParamSets = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
