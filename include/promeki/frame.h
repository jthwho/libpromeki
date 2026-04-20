/**
 * @file      frame.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <optional>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/benchmark.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/mediapacket.h>
#include <promeki/metadata.h>
#include <promeki/list.h>
#include <promeki/stringlist.h>
#include <promeki/mediaconfig.h>
#include <promeki/videoformat.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

class MediaDesc;

/**
 * @brief A media frame containing images, audio, and metadata.
 * @ingroup proav
 *
 * Aggregates one or more @ref Image entries (uncompressed or
 * compressed), one or more @ref Audio tracks, and a metadata
 * container into a single unit that represents a frame of media
 * content.
 *
 * Compressed bitstream access units are not stored as a separate
 * list on the Frame — they travel with their owning essence via
 * @ref Image::packet and @ref Audio::packet.  A compressed Image
 * carries its encoded @ref MediaPacket directly; that's the
 * canonical location a downstream @ref VideoDecoder reads from,
 * and the canonical location a @ref VideoEncoder writes to.
 */
class Frame {
        PROMEKI_SHARED_FINAL(Frame)
        public:
                /** @brief Shared pointer type for Frame. */
                using Ptr = SharedPtr<Frame>;
                /** @brief Plain value list of Frame objects. */
                using List = promeki::List<Frame>;
                /** @brief List of shared pointers to Frame objects. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty frame with no images, audio, or metadata. */
                Frame() = default;

                /**
                 * @brief Returns a const reference to the list of image planes.
                 * @return The image pointer list.
                 */
                const Image::PtrList &imageList() const { return _imageList; }

                /**
                 * @brief Returns a mutable reference to the list of image planes.
                 * @return The image pointer list.
                 */
                Image::PtrList &imageList() { return _imageList; }

                /**
                 * @brief Returns a const reference to the list of audio tracks.
                 * @return The audio pointer list.
                 */
                const Audio::PtrList &audioList() const { return _audioList; }

                /**
                 * @brief Returns a mutable reference to the list of audio tracks.
                 * @return The audio pointer list.
                 */
                Audio::PtrList &audioList() { return _audioList; }

                /**
                 * @brief Returns a const reference to the frame metadata.
                 * @return The metadata container.
                 */
                const Metadata &metadata() const { return _metadata; }

                /**
                 * @brief Returns a mutable reference to the frame metadata.
                 * @return The metadata container.
                 */
                Metadata &metadata() { return _metadata; }

                /**
                 * @brief Returns the VideoFormat for the image at @p index.
                 *
                 * Composes the image's raster and scan mode with the
                 * frame rate carried in @ref metadata under
                 * @c Metadata::FrameRate.  Returns a default
                 * (invalid) @ref VideoFormat when @p index is out of
                 * range or when the frame rate metadata is missing
                 * or not convertible to a FrameRate.
                 *
                 * @param index Zero-based image layer index.
                 * @return The composed VideoFormat, or an invalid
                 *         VideoFormat on failure.
                 */
                VideoFormat videoFormat(size_t index) const {
                        if(index >= _imageList.size()) return VideoFormat();
                        const Image::Ptr &img = _imageList[index];
                        if(!img) return VideoFormat();
                        const ImageDesc &d = img->desc();
                        return VideoFormat(d.size(),
                                           _metadata.getAs<FrameRate>(Metadata::FrameRate),
                                           d.videoScanMode());
                }

                /**
                 * @brief Assembles a MediaDesc describing this Frame.
                 *
                 * Returns a MediaDesc populated from the frame's
                 * own state: the frame rate is taken from
                 * @c Metadata::FrameRate (if set), the image list
                 * is built from the @ref Image::desc of each @ref Image in
                 * @ref imageList, the audio list is built from the
                 * @ref Audio::desc of each @ref Audio in @ref audioList,
                 * and the frame's metadata is copied across.
                 *
                 * The returned MediaDesc is only
                 * @ref MediaDesc::isValid "valid" if both a frame
                 * rate is present in metadata and the frame carries
                 * at least one image or audio entry.
                 */
                MediaDesc mediaDesc() const;

                /**
                 * @brief Returns a const reference to the frame benchmark.
                 * @return The benchmark pointer (may be null if benchmarking is disabled).
                 */
                const Benchmark::Ptr &benchmark() const { return _benchmark; }

                /**
                 * @brief Returns a mutable reference to the frame benchmark.
                 * @return The benchmark pointer.
                 */
                Benchmark::Ptr &benchmark() { return _benchmark; }

                /**
                 * @brief Sets the frame benchmark.
                 * @param bm The benchmark to attach.
                 */
                void setBenchmark(Benchmark::Ptr bm) { _benchmark = std::move(bm); return; }

                /**
                 * @brief Returns a const reference to the config update delta.
                 *
                 * When non-empty, the config update is applied by the
                 * receiving @ref MediaIOTask before the frame is
                 * processed.  This gives frame-level synchronisation
                 * for dynamic parameter changes (bitrate, quality,
                 * etc.) — the update travels with the write command
                 * through the strand queue, so a flush can never
                 * separate a config change from its frame.
                 */
                const MediaConfig &configUpdate() const { return _configUpdate; }

                /** @brief Returns a mutable reference to the config update delta. */
                MediaConfig &configUpdate() { return _configUpdate; }

                /** @brief Replaces the config update delta. */
                void setConfigUpdate(MediaConfig cfg) { _configUpdate = std::move(cfg); }

                /**
                 * @brief Returns a human-readable multi-line dump of
                 *        this frame's full contents.
                 *
                 * Emits the scalar-key block registered with
                 * @c VariantLookup<Frame> (@c ImageCount, @c AudioCount,
                 * @c HasBenchmark), the frame's metadata via
                 * @ref Metadata::dump, the @ref configUpdate delta
                 * when non-empty, and then @ref Image::dump /
                 * @ref Audio::dump for every entry indented by two
                 * spaces.
                 *
                 * No leading @c "Frame:" header is emitted — callers
                 * that need one (e.g. @c pmdf-inspect printing a
                 * frame index) prepend their own title and an
                 * additional indent so the output is uniform
                 * regardless of whether one frame or many are being
                 * printed.
                 *
                 * @param indent Leading whitespace to prefix every line.
                 * @return A @ref StringList (one entry per line).
                 */
                StringList dump(const String &indent = String()) const;

        private:
                Image::PtrList         _imageList;
                Audio::PtrList         _audioList;
                Metadata               _metadata;
                Benchmark::Ptr         _benchmark;
                MediaConfig            _configUpdate;
};

PROMEKI_NAMESPACE_END
