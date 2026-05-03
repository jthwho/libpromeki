/**
 * @file      videopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediapayload.h>
#include <promeki/imagedesc.h>
#include <promeki/duration.h>
#include <promeki/variant.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Abstract base for any video payload — compressed or
 *        uncompressed.
 * @ingroup proav
 *
 * VideoPayload binds a @ref MediaPayload's plane list to an
 * @ref ImageDesc that describes the video's format, geometry,
 * scan mode, colour interpretation, and per-stream metadata.
 * The descriptor is the single source of truth for "what is this
 * video?"; concrete leaves (@ref UncompressedVideoPayload and
 * @ref CompressedVideoPayload) extend this base with per-payload
 * state that only makes sense for their side of the compressed /
 * uncompressed split.
 *
 * For compressed video the descriptor's @ref PixelFormat is a
 * compressed entry (e.g. @c PixelFormat::H264, @c PixelFormat::HEVC,
 * @c PixelFormat::JPEG_XS_*); for uncompressed video it is a raster
 * pixel format.  A few structural @ref ImageDesc fields (line
 * padding and alignment) are meaningless on the compressed side,
 * which is accepted as a harmless consequence of keeping a single
 * descriptor type across both families.
 *
 * VideoPayload is abstract — @c _promeki_clone and @ref kind
 * remain pure-virtual so the concrete leaves must supply a
 * covariant clone.  Subclassing concrete leaves for codec-specific
 * specializations (a future @c NALBitstreamPayload for H.264 / HEVC
 * access units, or a @c ProResPayload carrying frame-type-A/B
 * metadata) is an intentional extension point.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref MediaPayload.
 * @c VideoPayload::Ptr is safe to hand off across threads (atomic
 * refcount); concurrent mutation of a single instance must be
 * externally synchronized.
 */
class VideoPayload : public MediaPayload {
                PROMEKI_SHARED_ABSTRACT(VideoPayload)
        public:
                /** @brief Shared-pointer alias for VideoPayload ownership. */
                using Ptr = SharedPtr<VideoPayload, /*CopyOnWrite=*/true, VideoPayload>;

                /** @brief List of shared pointers to VideoPayload instances. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Constructs an empty video payload with no descriptor and no data. */
                VideoPayload() = default;

                /**
                 * @brief Constructs a video payload with the given descriptor.
                 *
                 * The plane list is left empty; callers attach planes via
                 * @ref setData or by constructing the payload with the
                 * two-argument form below.
                 */
                explicit VideoPayload(const ImageDesc &desc) : _desc(desc) {}

                /**
                 * @brief Constructs a video payload with a descriptor and planes.
                 */
                VideoPayload(const ImageDesc &desc, const BufferView &data) : MediaPayload(data), _desc(desc) {}

                /** @brief Returns @c MediaPayloadKind::Video. */
                const MediaPayloadKind &kind() const override { return MediaPayloadKind::Video; }

                /** @brief Returns the image descriptor. */
                const ImageDesc &desc() const { return _desc; }

                /** @brief Returns a mutable reference to the image descriptor. */
                ImageDesc &desc() { return _desc; }

                /** @brief Replaces the image descriptor. */
                void setDesc(const ImageDesc &d) { _desc = d; }

                /**
                 * @brief Returns the per-payload wall-clock duration,
                 *        or a zero @ref Duration when unset.
                 *
                 * Video payloads have no intrinsic rate — the enclosing
                 * pipeline (MediaIO / frame-rate converter) is the one
                 * that knows how long a frame represents.  A
                 * zero-valued duration is the "not yet stamped"
                 * sentinel; @ref MediaIO treats that case as a fill
                 * site and assigns one frame of the session rate.
                 */
                Duration duration() const override { return _duration; }

                /** @brief Stores @p val as the payload's duration. */
                Error setDuration(const Duration &val) override {
                        _duration = val;
                        return Error::Ok;
                }

                /**
                 * @brief Video payloads always support a duration —
                 *        always returns @c true.
                 *
                 * @ref hasDuration is a type-level predicate that
                 * answers "is a duration meaningful for this payload
                 * kind?" rather than "has one been assigned?"  Callers
                 * that want the latter should test
                 * @c duration().isZero().
                 */
                bool hasDuration() const override { return true; }

                /**
                 * @brief Forwards to the descriptor's metadata.
                 *
                 * Every video payload keeps its per-stream metadata
                 * (@c FrameRate, colorimetry, origination, …) on the
                 * @ref ImageDesc that already travels with it.
                 * Implementing @ref MediaPayload::metadata by
                 * forwarding here keeps one authoritative store per
                 * payload and lets generic consumers reach it
                 * uniformly via the base API.
                 */
                const Metadata &metadata() const override { return _desc.metadata(); }

                /** @copydoc metadata() const */
                Metadata &metadata() override { return _desc.metadata(); }

                VideoPayload(const VideoPayload &) = default;
                VideoPayload(VideoPayload &&) = default;
                VideoPayload &operator=(const VideoPayload &) = default;
                VideoPayload &operator=(VideoPayload &&) = default;

        protected:
                /**
                 * @brief Writes the VideoPayload-level common tail
                 *        (duration presence + value) to @p s.
                 *
                 * Called from each concrete leaf's @ref serialisePayload
                 * after the leaf has written its @ref ImageDesc so the
                 * duration rides on every video subclass without
                 * duplicating the logic.
                 */
                void serialiseVideoCommon(DataStream &s) const;

                /**
                 * @brief Reads the VideoPayload-level common tail
                 *        written by @ref serialiseVideoCommon.
                 */
                void deserialiseVideoCommon(DataStream &s);

        private:
                ImageDesc _desc;
                Duration  _duration;
};

PROMEKI_NAMESPACE_END
