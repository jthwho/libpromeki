/**
 * @file      mediapacket.h
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
#include <promeki/pixeldesc.h>
#include <promeki/mediatimestamp.h>
#include <promeki/duration.h>
#include <promeki/metadata.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Encoded bitstream access unit produced by a @ref VideoEncoder or
 *        consumed by a @ref VideoDecoder.
 * @ingroup proav
 *
 * A @ref MediaPacket represents one compressed access unit (AU) in a video
 * or audio bitstream.  For video codecs this is typically one encoded
 * frame — one or more concatenated NAL units for H.264 / HEVC, or one
 * coded picture for any other frame-based codec.  For block-based audio
 * codecs it is one encoded audio frame.
 *
 * A packet carries:
 * - A @ref BufferView onto the raw encoded bytes.  The view owns a
 *   shared @ref Buffer::Ptr internally, so the single-allocation path
 *   (whole buffer = one packet) is as cheap as wrapping a
 *   @c Buffer::Ptr directly.  The view form additionally lets several
 *   packets reference non-overlapping regions of one buffer — what an
 *   NVENC output access-unit with concatenated SPS / PPS / IDR NAL
 *   units naturally produces, and what MP4 sample reads and RTP
 *   fragmentation both want.
 * - A @ref PixelDesc identifying the codec format (e.g. @c PixelDesc::H264,
 *   @c PixelDesc::HEVC).  The PixelDesc's @c codecName field is what the
 *   @ref VideoEncoder / @ref VideoDecoder registries key off.
 * - Separate presentation and decode @ref MediaTimeStamp values.  With
 *   B-frames the decode order differs from the presentation order, so
 *   both are recorded; callers that only care about PTS can ignore DTS.
 * - A nominal @ref Duration for the packet (one frame period for video,
 *   the sample-count duration for audio).
 * - A bitmask of @ref Flag values (keyframe / end-of-stream / discardable /
 *   corrupt / parameter-set).
 * - A freeform @ref Metadata container for codec-specific annotations such
 *   as out-of-band SPS / PPS / VPS, HDR metadata, or private extradata.
 *
 * MediaPacket follows the library's shareable data-object convention:
 * instances are plain values by default, and a @ref MediaPacket::Ptr
 * alias is provided when shared ownership is desired (typical in
 * pipelines that queue packets across threads).
 *
 * @par Example — single-allocation case
 * @code
 * MediaPacket::Ptr pkt = MediaPacket::Ptr::create();
 * pkt.modify()->setBuffer(encodedBuffer);   // whole buffer == one packet
 * pkt.modify()->setPixelDesc(PixelDesc::H264);
 * pkt.modify()->setPts(pts);
 * pkt.modify()->setDuration(Duration::fromSeconds(1) / 30);
 * pkt.modify()->addFlag(MediaPacket::Keyframe);
 * @endcode
 *
 * @par Example — one buffer shared by three packets
 * @code
 * // NVENC emitted a single locked bitstream buffer that actually
 * // contains three concatenated NAL units: SPS, PPS, and an IDR
 * // slice.  Each one becomes its own MediaPacket as a view.
 * MediaPacket sps (BufferView(au, 0,           spsLen), PixelDesc::H264);
 * MediaPacket pps (BufferView(au, spsLen,      ppsLen), PixelDesc::H264);
 * MediaPacket idr (BufferView(au, spsLen+ppsLen, sliceLen), PixelDesc::H264);
 * sps.addFlag(MediaPacket::ParameterSet);
 * pps.addFlag(MediaPacket::ParameterSet);
 * idr.addFlag(MediaPacket::Keyframe);
 * @endcode
 */
class MediaPacket {
        PROMEKI_SHARED_FINAL(MediaPacket)
        public:
                /** @brief Shared pointer type for MediaPacket. */
                using Ptr = SharedPtr<MediaPacket>;

                /** @brief Plain value list of MediaPacket objects. */
                using List = promeki::List<MediaPacket>;

                /** @brief List of shared pointers to MediaPacket objects. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Bit flags describing properties of a packet.
                 *
                 * Values are bitwise-OR'able; store the combined mask via
                 * @ref setFlags or manipulate individual bits via
                 * @ref addFlag / @ref removeFlag / @ref hasFlag.
                 */
                enum Flag : uint32_t {
                        None          = 0,          ///< No flags set.
                        Keyframe      = 1u << 0,    ///< IDR / I-frame; a self-contained decode entry point.
                        EndOfStream   = 1u << 1,    ///< Final packet in the stream (emitted after @c flush()).
                        Discardable   = 1u << 2,    ///< Non-reference (e.g. B-frame); safe to drop.
                        Corrupt       = 1u << 3,    ///< Bitstream is known to be corrupt.
                        ParameterSet  = 1u << 4,    ///< Packet carries only parameter sets (SPS / PPS / VPS / etc.).
                };

                /** @brief Constructs an empty, invalid packet. */
                MediaPacket() = default;

                /**
                 * @brief Constructs a packet from a pre-built view and a pixel description.
                 *
                 * The common case for backends that slice one backing buffer
                 * into multiple logical packets.
                 *
                 * @param view      Byte range on a shared buffer.
                 * @param pixelDesc Codec identity (e.g. @c PixelDesc::H264).
                 */
                MediaPacket(const BufferView &view, const PixelDesc &pixelDesc) :
                        _view(view), _pixelDesc(pixelDesc) { }

                /**
                 * @brief Constructs a packet that owns a whole buffer as its payload.
                 *
                 * Convenience overload for the "one allocation per packet"
                 * case — internally wraps @p buffer as a @ref BufferView
                 * spanning @c [0, buffer->size()).
                 *
                 * @param buffer    Encoded-byte payload.
                 * @param pixelDesc Codec identity (e.g. @c PixelDesc::H264).
                 */
                MediaPacket(Buffer::Ptr buffer, const PixelDesc &pixelDesc) :
                        _view(buffer ? BufferView(buffer, 0, buffer->size()) : BufferView()),
                        _pixelDesc(pixelDesc) { }

                /**
                 * @brief Returns true when the packet has a backing buffer and a valid PixelDesc.
                 * @return True when the packet is ready to use.
                 */
                bool isValid() const {
                        return _view.isValid() && _pixelDesc.isValid();
                }

                /** @brief Returns the view onto the encoded bytes. */
                const BufferView &view() const { return _view; }

                /** @brief Returns a mutable reference to the view. */
                BufferView &view() { return _view; }

                /** @brief Replaces the view onto the encoded bytes.
                 *  @param v New view. */
                void setView(const BufferView &v) { _view = v; }

                /** @brief Returns the underlying shared buffer (may be null for an empty packet). */
                const Buffer::Ptr &buffer() const { return _view.buffer(); }

                /**
                 * @brief Replaces the payload with a view spanning the entire given buffer.
                 *
                 * Shorthand for @c setView(BufferView(b, 0, b->size())) —
                 * the one-allocation-per-packet form that matches the
                 * pre-BufferView API.
                 *
                 * @param b New buffer pointer (may be null to clear the payload).
                 */
                void setBuffer(Buffer::Ptr b) {
                        if(b) _view = BufferView(b, 0, b->size());
                        else  _view = BufferView();
                }

                /** @brief Returns the logical payload size in bytes. */
                size_t size() const { return _view.size(); }

                /** @brief Returns the pixel description identifying the codec format. */
                const PixelDesc &pixelDesc() const { return _pixelDesc; }

                /** @brief Sets the pixel description.
                 *  @param pd The new pixel description. */
                void setPixelDesc(const PixelDesc &pd) { _pixelDesc = pd; }

                /** @brief Returns the presentation timestamp. */
                const MediaTimeStamp &pts() const { return _pts; }

                /** @brief Sets the presentation timestamp.
                 *  @param ts The new PTS. */
                void setPts(const MediaTimeStamp &ts) { _pts = ts; }

                /** @brief Returns the decode timestamp. */
                const MediaTimeStamp &dts() const { return _dts; }

                /** @brief Sets the decode timestamp.
                 *  @param ts The new DTS. */
                void setDts(const MediaTimeStamp &ts) { _dts = ts; }

                /** @brief Returns the nominal duration of the packet. */
                const Duration &duration() const { return _duration; }

                /** @brief Sets the nominal duration of the packet.
                 *  @param d The new duration. */
                void setDuration(const Duration &d) { _duration = d; }

                /** @brief Returns the raw flag bitmask. */
                uint32_t flags() const { return _flags; }

                /** @brief Replaces the entire flag bitmask.
                 *  @param f New bitmask value. */
                void setFlags(uint32_t f) { _flags = f; }

                /** @brief Sets a single flag in the bitmask.
                 *  @param f Flag to set. */
                void addFlag(Flag f) { _flags |= static_cast<uint32_t>(f); }

                /** @brief Clears a single flag from the bitmask.
                 *  @param f Flag to clear. */
                void removeFlag(Flag f) { _flags &= ~static_cast<uint32_t>(f); }

                /** @brief Returns true when the given flag is set.
                 *  @param f Flag to test. */
                bool hasFlag(Flag f) const {
                        return (_flags & static_cast<uint32_t>(f)) != 0;
                }

                /** @brief Convenience: true when the @c Keyframe flag is set. */
                bool isKeyframe() const { return hasFlag(Keyframe); }

                /** @brief Convenience: true when the @c EndOfStream flag is set. */
                bool isEndOfStream() const { return hasFlag(EndOfStream); }

                /** @brief Convenience: true when the @c ParameterSet flag is set. */
                bool isParameterSet() const { return hasFlag(ParameterSet); }

                /** @brief Returns the metadata container. */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns a mutable reference to the metadata container. */
                Metadata &metadata() { return _metadata; }

        private:
                BufferView      _view;
                PixelDesc       _pixelDesc;
                MediaTimeStamp  _pts;
                MediaTimeStamp  _dts;
                Duration        _duration;
                uint32_t        _flags = 0;
                Metadata        _metadata;
};

PROMEKI_NAMESPACE_END
