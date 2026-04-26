/**
 * @file      mediadesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

class SdpSession;

/**
 * @brief Describes a media format including frame rate, image layers, audio channels, and metadata.
 * @ingroup proav
 *
 * MediaDesc is the complete descriptor for a media resource.  It carries
 * the frame rate, zero or more image layers (ImageDesc), zero or more
 * audio channel groups (AudioDesc), and container-level metadata.
 * A valid MediaDesc has a valid frame rate and at least one image or
 * audio description.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * @c MediaDesc::Ptr uses an atomic refcount and is safe to share across
 * threads.
 */
class MediaDesc {
                PROMEKI_SHARED_FINAL(MediaDesc)
        public:
                /** @brief Shared pointer type for MediaDesc. */
                using Ptr = SharedPtr<MediaDesc>;

                /** @brief List of MediaDesc values. */
                using List = promeki::List<MediaDesc>;

                /** @brief List of shared MediaDesc pointers. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs a default (invalid) media description. */
                MediaDesc() = default;

                /**
         * @brief Aggregates the per-@c m= descriptors from an SDP session.
         *
         * Walks every media description in @p session, calls
         * @ref ImageDesc::fromSdp or @ref AudioDesc::fromSdp on
         * each one (based on its @c mediaType), and pushes any
         * successful result into the corresponding list on the
         * returned MediaDesc.  The frame rate is left unset —
         * SDP does not carry a frame rate attribute, so callers
         * that need one must fill it in separately.
         *
         * Media descriptions whose encoding name is not recognised
         * by @ref ImageDesc::fromSdp / @ref AudioDesc::fromSdp are
         * silently skipped, so the returned MediaDesc may be
         * partial (or empty) for streams that use encodings
         * libpromeki does not yet know about.
         *
         * @param session The parsed SDP session.
         * @return A MediaDesc with the image and audio lists
         *         populated from @p session.
         */
                static MediaDesc fromSdp(const SdpSession &session);

                /**
         * @brief Builds an SdpSession from this MediaDesc.
         *
         * The inverse of @ref fromSdp.  Walks the image and audio
         * lists and calls @ref ImageDesc::toSdp /
         * @ref AudioDesc::toSdp on each entry, appending the
         * resulting media descriptions to a new SdpSession.
         *
         * Video payload types start at @p videoPayloadType (default
         * 96) and increment for each image entry.  Audio payload
         * types start at @p audioPayloadType (default one past the
         * last video PT).
         *
         * @param videoPayloadType Starting PT for video streams.
         * @return A populated SdpSession.
         */
                SdpSession toSdp(uint8_t videoPayloadType = 96) const;

                /** @brief Returns true if the media description is valid (has a valid frame rate and at least one image or audio description). */
                bool isValid() const {
                        return _frameRate.isValid() && (_imageList.size() > 0 || _audioList.size() > 0);
                }

                /** @brief Returns the frame rate. */
                const FrameRate &frameRate() const { return _frameRate; }
                /** @brief Sets the frame rate.
         *  @param val The new frame rate. */
                void setFrameRate(const FrameRate &val) { _frameRate = val; }

                /** @brief Returns a const reference to the list of image descriptions. */
                const ImageDesc::List &imageList() const { return _imageList; }
                /** @brief Returns a mutable reference to the list of image descriptions. */
                ImageDesc::List &imageList() { return _imageList; }

                /**
         * @brief Returns the VideoFormat for the image at @p index.
         *
         * Composes the MediaDesc-wide @ref frameRate with the
         * per-image raster (@ref ImageDesc::size) and scan mode
         * (@ref ImageDesc::videoScanMode).  Returns a default
         * (invalid) @ref VideoFormat when @p index is out of range.
         *
         * @param index Zero-based image layer index.
         * @return The composed VideoFormat, or an invalid
         *         VideoFormat when @p index is out of range.
         */
                VideoFormat videoFormat(size_t index) const {
                        if (index >= _imageList.size()) return VideoFormat();
                        const ImageDesc &img = _imageList[index];
                        return VideoFormat(img.size(), _frameRate, img.videoScanMode());
                }

                /** @brief Returns a const reference to the list of audio descriptions. */
                const AudioDesc::List &audioList() const { return _audioList; }
                /** @brief Returns a mutable reference to the list of audio descriptions. */
                AudioDesc::List &audioList() { return _audioList; }

                /** @brief Returns a const reference to the metadata. */
                const Metadata &metadata() const { return _metadata; }
                /** @brief Returns a mutable reference to the metadata. */
                Metadata &metadata() { return _metadata; }

                /** @brief Returns true if every member of both descriptors is equal. */
                bool operator==(const MediaDesc &other) const {
                        return _frameRate == other._frameRate && _imageList == other._imageList &&
                               _audioList == other._audioList && _metadata == other._metadata;
                }

                /** @brief Returns true if any member differs. */
                bool operator!=(const MediaDesc &other) const { return !(*this == other); }

                /**
         * @brief Compares structural media fields, ignoring metadata on
         *        the MediaDesc itself and on every ImageDesc / AudioDesc.
         *
         * Returns true when frame rate, image-list structure (per
         * @ref ImageDesc::formatEquals), and audio-list structure (per
         * @ref AudioDesc::formatEquals) all match.  Used by the
         * pipeline planner so routes are compared on the shape that
         * actually matters for format compatibility rather than on
         * cosmetic metadata (colour tags, creator fields, user data)
         * that one side may carry and the other may not.
         *
         * @param other The MediaDesc to compare against.
         * @return true if every structural field matches.
         */
                bool formatEquals(const MediaDesc &other) const {
                        if (_frameRate != other._frameRate) return false;
                        if (_imageList.size() != other._imageList.size()) return false;
                        if (_audioList.size() != other._audioList.size()) return false;
                        for (size_t i = 0; i < _imageList.size(); ++i) {
                                if (!_imageList[i].formatEquals(other._imageList[i])) return false;
                        }
                        for (size_t i = 0; i < _audioList.size(); ++i) {
                                if (!_audioList[i].formatEquals(other._audioList[i])) return false;
                        }
                        return true;
                }

        private:
                FrameRate       _frameRate;
                ImageDesc::List _imageList;
                AudioDesc::List _audioList;
                Metadata        _metadata;
};

/**
 * @brief Writes a MediaDesc as tag + frameRate + imageList + audioList + metadata.
 * @param stream The stream to write to.
 * @param desc   The MediaDesc to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const MediaDesc &desc) {
        stream.writeTag(DataStream::TypeMediaDesc);
        stream << desc.frameRate();
        stream << desc.imageList();
        stream << desc.audioList();
        stream << desc.metadata();
        return stream;
}

/**
 * @brief Reads a MediaDesc from its tagged wire format.
 * @param stream The stream to read from.
 * @param desc   The MediaDesc to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, MediaDesc &desc) {
        if (!stream.readTag(DataStream::TypeMediaDesc)) {
                desc = MediaDesc();
                return stream;
        }
        FrameRate       fr;
        ImageDesc::List imgs;
        AudioDesc::List auds;
        Metadata        meta;
        stream >> fr >> imgs >> auds >> meta;
        if (stream.status() != DataStream::Ok) {
                desc = MediaDesc();
                return stream;
        }
        desc = MediaDesc();
        desc.setFrameRate(fr);
        desc.imageList() = std::move(imgs);
        desc.audioList() = std::move(auds);
        desc.metadata() = std::move(meta);
        return stream;
}

PROMEKI_NAMESPACE_END
