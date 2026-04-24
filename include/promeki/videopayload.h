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
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

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
 * VideoPayload is abstract — @ref _promeki_clone and @ref kind
 * remain pure-virtual so the concrete leaves must supply a
 * covariant clone.  Subclassing concrete leaves for codec-specific
 * specializations (a future @c NALBitstreamPayload for H.264 / HEVC
 * access units, or a @c ProResPayload carrying frame-type-A/B
 * metadata) is an intentional extension point.
 */
class VideoPayload : public MediaPayload {
        PROMEKI_SHARED_ABSTRACT(VideoPayload)
        public:
                /** @brief Shared-pointer alias for VideoPayload ownership. */
                using Ptr = SharedPtr<VideoPayload, /*CopyOnWrite=*/true, VideoPayload>;

                /** @brief List of shared pointers to VideoPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty video payload with no descriptor and no data. */
                VideoPayload() = default;

                /**
                 * @brief Constructs a video payload with the given descriptor.
                 *
                 * The plane list is left empty; callers attach planes via
                 * @ref setData or by constructing the payload with the
                 * two-argument form below.
                 */
                explicit VideoPayload(const ImageDesc &desc) : _desc(desc) { }

                /**
                 * @brief Constructs a video payload with a descriptor and planes.
                 */
                VideoPayload(const ImageDesc &desc, const BufferView &data) :
                        MediaPayload(data), _desc(desc) { }

                /** @brief Returns @ref MediaPayloadKind::Video. */
                const MediaPayloadKind &kind() const override { return MediaPayloadKind::Video; }

                /** @brief Returns the image descriptor. */
                const ImageDesc &desc() const { return _desc; }

                /** @brief Returns a mutable reference to the image descriptor. */
                ImageDesc &desc() { return _desc; }

                /** @brief Replaces the image descriptor. */
                void setDesc(const ImageDesc &d) { _desc = d; }

                VideoPayload(const VideoPayload &) = default;
                VideoPayload(VideoPayload &&) = default;
                VideoPayload &operator=(const VideoPayload &) = default;
                VideoPayload &operator=(VideoPayload &&) = default;

        private:
                ImageDesc _desc;
};

PROMEKI_NAMESPACE_END
