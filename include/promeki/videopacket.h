/**
 * @file      videopacket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/pixelformat.h>
#include <promeki/mediapacket.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Encoded video access unit produced by a @ref VideoEncoder or
 *        consumed by a @ref VideoDecoder.
 * @ingroup proav
 *
 * VideoPacket specializes @ref MediaPacket for compressed video access
 * units — one access unit per packet for frame-based codecs, or one
 * access-unit slice for the NAL-carried codecs (H.264, HEVC, AV1).
 * The payload is carried in the base's @ref BufferView, so multiple
 * VideoPackets can cheaply reference non-overlapping regions of a
 * single encoder output buffer (for example, the concatenated
 * SPS / PPS / IDR bytes NVENC produces as one locked bitstream).
 *
 * @par Codec identity
 *
 * The compressed @ref PixelFormat (e.g. @c PixelFormat::H264,
 * @c PixelFormat::HEVC) is attached to each packet so downstream
 * dispatch doesn't have to consult the emitting encoder.  Intra-only
 * single-image codecs (JPEG, JPEG-XS) use their own compressed
 * PixelFormat values.
 *
 * @par Flags
 *
 * A bitmask captures properties inherent to video bitstreams:
 * @c Keyframe (IDR / I-frame), @c ParameterSet (SPS/PPS/VPS only),
 * and @c Discardable (non-reference B-frame).  Flags specific to a
 * particular codec family would be added on further subclasses
 * (@c H264VideoPacket, @c HevcVideoPacket, …).  Stream-level status —
 * end-of-stream, corruption — lives on the base's metadata.
 *
 * @par Extension
 *
 * VideoPacket is intentionally not @c final.  Codec-specific
 * subclasses may derive from it to carry NAL-type identification,
 * tile / slice indices, or other codec-private structure.
 *
 * @par Example — single allocation
 * @code
 * VideoPacket::Ptr pkt = VideoPacket::Ptr::create(
 *         encodedBuffer, PixelFormat(PixelFormat::H264));
 * pkt.modify()->setPts(pts);
 * pkt.modify()->setDuration(Duration::fromSeconds(1) / 30);
 * pkt.modify()->addFlag(VideoPacket::Keyframe);
 * @endcode
 */
class VideoPacket : public MediaPacket {
        public:
                virtual VideoPacket *_promeki_clone() const override {
                        return new VideoPacket(*this);
                }

                /** @brief Shared-pointer alias for VideoPacket ownership. */
                using Ptr = SharedPtr<VideoPacket, /*CopyOnWrite=*/true, VideoPacket>;

                /** @brief List of shared pointers to VideoPacket instances. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Bit flags describing properties of a video packet.
                 *
                 * Values are bitwise-OR'able; store the combined mask via
                 * @ref setFlags or manipulate individual bits via
                 * @ref addFlag / @ref removeFlag / @ref hasFlag.
                 */
                enum Flag : uint32_t {
                        None         = 0,          ///< No flags set.
                        Keyframe     = 1u << 0,    ///< IDR / I-frame; a self-contained decode entry point.
                        Discardable  = 1u << 1,    ///< Non-reference (e.g. B-frame); safe to drop.
                        ParameterSet = 1u << 2,    ///< Carries only parameter sets (SPS / PPS / VPS).
                };

                /** @brief Constructs an empty, invalid video packet. */
                VideoPacket() = default;

                /**
                 * @brief Constructs a packet from a pre-built view and a compressed pixel format.
                 *
                 * The common case for backends that slice one backing
                 * buffer into multiple logical packets.
                 */
                VideoPacket(const BufferView &view, const PixelFormat &pixelFormat) :
                        MediaPacket(view), _pixelFormat(pixelFormat) { }

                /**
                 * @brief Constructs a packet that owns a whole buffer as its payload.
                 *
                 * Convenience overload — internally wraps @p buffer as a
                 * @ref BufferView spanning @c [0, buffer->size()).
                 */
                VideoPacket(Buffer::Ptr buffer, const PixelFormat &pixelFormat) :
                        MediaPacket(buffer ? BufferView(buffer, 0, buffer->size())
                                           : BufferView()),
                        _pixelFormat(pixelFormat) { }

                Kind kind() const override { return Video; }

                /** @brief True when the payload and pixel format are both set. */
                bool isValid() const override {
                        return MediaPacket::isValid() && _pixelFormat.isValid();
                }

                // ---- Codec identity ---------------------------------------

                /** @brief Returns the compressed pixel format identifying the codec. */
                const PixelFormat &pixelFormat() const { return _pixelFormat; }

                /** @brief Sets the compressed pixel format. */
                void setPixelFormat(const PixelFormat &pf) { _pixelFormat = pf; }

                // ---- Flags ------------------------------------------------

                /** @brief Returns the raw flag bitmask. */
                uint32_t flags() const { return _flags; }

                /** @brief Replaces the entire flag bitmask. */
                void setFlags(uint32_t f) { _flags = f; }

                /** @brief Sets a single flag in the bitmask. */
                void addFlag(Flag f) { _flags |= static_cast<uint32_t>(f); }

                /** @brief Clears a single flag from the bitmask. */
                void removeFlag(Flag f) { _flags &= ~static_cast<uint32_t>(f); }

                /** @brief Returns true when the given flag is set. */
                bool hasFlag(Flag f) const {
                        return (_flags & static_cast<uint32_t>(f)) != 0;
                }

                /** @brief Convenience: true when the @c Keyframe flag is set. */
                bool isKeyframe() const { return hasFlag(Keyframe); }

                /** @brief Convenience: true when the @c ParameterSet flag is set. */
                bool isParameterSet() const { return hasFlag(ParameterSet); }

                /** @brief Convenience: true when the @c Discardable flag is set. */
                bool isDiscardable() const { return hasFlag(Discardable); }

                VideoPacket(const VideoPacket &) = default;
                VideoPacket(VideoPacket &&) = default;
                VideoPacket &operator=(const VideoPacket &) = default;
                VideoPacket &operator=(VideoPacket &&) = default;

        private:
                PixelFormat _pixelFormat;
                uint32_t    _flags = 0;
};

PROMEKI_NAMESPACE_END
