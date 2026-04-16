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
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

class MediaDesc;

/**
 * @brief A media frame containing images, audio, compressed packets, and metadata.
 * @ingroup proav
 *
 * Aggregates one or more uncompressed image planes, one or more
 * audio tracks, one or more @ref MediaPacket "compressed packets",
 * and a metadata container into a single unit that represents a
 * frame of media content.
 *
 * The @ref packetList companion to @ref imageList / @ref audioList
 * holds encoded bitstream access units — the output of a @ref
 * VideoEncoder, the input to a @ref VideoDecoder, or the on-disk
 * samples read from a muxed container.  A single Frame may carry
 * uncompressed essence (images / audio) and compressed packets
 * simultaneously — for example, while a pipeline is mid-encode the
 * encoder task's output can be spliced back onto the same Frame
 * that carried the source image for that PTS.
 *
 * @par Example
 * @code
 * MediaDesc mdesc;
 * Frame::Ptr frame = Frame::Ptr::create(mdesc);
 * frame->setTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));
 * Image img = frame->image(0);
 *
 * // After encoding, attach a compressed packet alongside the image:
 * frame.modify()->packetList().pushToBack(encoded);
 * @endcode
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
                 * @brief Returns a const reference to the list of compressed packets.
                 *
                 * Each entry is a @ref MediaPacket carrying one encoded
                 * access unit (typically one coded video frame or one
                 * encoded audio frame).  Empty by default — backends
                 * that only deal in uncompressed essence never touch
                 * this list.
                 *
                 * @return The packet pointer list.
                 */
                const MediaPacket::PtrList &packetList() const { return _packetList; }

                /**
                 * @brief Returns a mutable reference to the list of compressed packets.
                 * @return The packet pointer list.
                 */
                MediaPacket::PtrList &packetList() { return _packetList; }

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
                 * @brief Resolves a single template key against this frame's structure.
                 *
                 * Used by @ref makeString.  Returns a value when the
                 * key names something this frame can describe and
                 * @c std::nullopt otherwise.  The recognised forms
                 * mirror @ref makeString:
                 *
                 *  - @c "@<Pseudo>" — frame-level introspection (see
                 *    @ref makeString for the list).
                 *  - @c "Image[N].<sub>" — delegates to
                 *    @ref Image::resolveTemplateKey on the image at
                 *    index @c N.  When @c N is out of range or the
                 *    image is null, returns @c std::nullopt.
                 *  - @c "Audio[N].<sub>" — delegates to
                 *    @ref Audio::resolveTemplateKey on the audio at
                 *    index @c N.
                 *  - Any registered metadata key — looked up in the
                 *    frame's @ref metadata and rendered with
                 *    @ref Variant::format.
                 *
                 * @param key  The placeholder key (no braces, no colon).
                 * @param spec The format spec (may be empty).
                 */
                std::optional<String> resolveTemplateKey(const String &key, const String &spec) const;

                /**
                 * @brief Substitutes @c {Key[:spec]} placeholders against this frame.
                 *
                 * Built on top of @ref Metadata::format with a custom
                 * resolver that adds three layers of introspection on
                 * top of the bare metadata lookup:
                 *
                 *  1. Frame-level pseudo keys (prefixed with @c "@" so
                 *     they cannot collide with metadata keys):
                 *     - @c \@ImageCount, @c \@AudioCount,
                 *       @c \@PacketCount — list sizes (uint64).
                 *     - @c \@HasBenchmark — bool.
                 *     - @c \@VideoFormat / @c \@VideoFormat[N] —
                 *       @ref VideoFormat for image @c N (default 0).
                 *       Combines the frame's @c Metadata::FrameRate
                 *       with the image's raster and scan mode and
                 *       honours @ref VideoFormat format specs (e.g.
                 *       @c :smpte).  Out-of-range @c N falls through
                 *       to the user resolver / unknown-key path.
                 *  2. Subscripted descent into images and audio — any
                 *     placeholder of the form @c "Image[N].<sub>" or
                 *     @c "Audio[N].<sub>" is dispatched to the
                 *     corresponding @ref Image::resolveTemplateKey or
                 *     @ref Audio::resolveTemplateKey, so the full
                 *     vocabulary of those classes (their pseudo keys
                 *     and their own metadata) is available with no
                 *     extra plumbing.
                 *  3. The user-supplied @p resolver is consulted last
                 *     for anything else.
                 *
                 * @par Example
                 * @code
                 * Frame::Ptr frame = Frame::Ptr::create();
                 * frame.modify()->imageList().pushToBack(Image::Ptr::create(1920, 1080,
                 *     PixelDesc::RGBA8_sRGB));
                 * frame.modify()->metadata().set(Metadata::Timecode,
                 *     Timecode(Timecode::NDF24, 1, 0, 0, 0));
                 * String s = frame->makeString(
                 *     "[{Timecode:smpte}] {@ImageCount}img {Image[0].@Size}");
                 * // "[01:00:00:00] 1img 1920x1080"
                 * @endcode
                 *
                 * @tparam Resolver Callable returning @c std::optional<String>.  Pass
                 *                  @c nullptr to disable the user fallback.
                 * @param tmpl     Template string with @c {Key[:spec]} placeholders.
                 * @param resolver Optional fallback resolver consulted for any key
                 *                 that is not a frame-level pseudo, an
                 *                 @c Image[N] / @c Audio[N] subscript, or a key
                 *                 present in @ref metadata.
                 * @param err      Optional error output.
                 */
                template <typename Resolver>
                String makeString(const String &tmpl, Resolver &&resolver, Error *err = nullptr) const {
                        return _metadata.format(tmpl,
                                [this, &resolver](const String &key, const String &spec) -> std::optional<String> {
                                        auto v = resolveTemplateKey(key, spec);
                                        if(v.has_value()) return v;
                                        if constexpr (!std::is_same_v<std::decay_t<Resolver>, std::nullptr_t>) {
                                                return resolver(key, spec);
                                        }
                                        return std::nullopt;
                                }, err);
                }

                /** @brief Convenience overload of @ref makeString with no fallback resolver. */
                String makeString(const String &tmpl, Error *err = nullptr) const {
                        return makeString(tmpl, nullptr, err);
                }

        private:
                Image::PtrList         _imageList;
                Audio::PtrList         _audioList;
                MediaPacket::PtrList   _packetList;
                Metadata               _metadata;
                Benchmark::Ptr         _benchmark;

                std::optional<String> resolvePseudoKey(const String &key, const String &spec) const;
};

PROMEKI_NAMESPACE_END
