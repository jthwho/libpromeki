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
 */
class MediaDesc {
    PROMEKI_SHARED_FINAL(MediaDesc)
    public:
        /** @brief Shared pointer type for MediaDesc. */
        using Ptr = SharedPtr<MediaDesc>;

        /** @brief List of ImageDesc values describing each image layer. */
        using ImageDescList = List<ImageDesc>;

        /** @brief List of AudioDesc values describing each audio channel group. */
        using AudioDescList = List<AudioDesc>;

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
        bool isValid() const { return _frameRate.isValid() && (_imageList.size() > 0 || _audioList.size() > 0); }

        /** @brief Returns the frame rate. */
        const FrameRate &frameRate() const { return _frameRate; }
        /** @brief Sets the frame rate.
         *  @param val The new frame rate. */
        void setFrameRate(const FrameRate &val) { _frameRate = val; }

        /** @brief Returns a const reference to the list of image descriptions. */
        const ImageDescList &imageList() const { return _imageList; }
        /** @brief Returns a mutable reference to the list of image descriptions. */
        ImageDescList &imageList() { return _imageList; }

        /**
         * @brief Returns the VideoFormat for the image at @p index.
         *
         * Composes the MediaDesc-wide @ref frameRate with the
         * per-image raster (@ref ImageDesc::size) and scan mode
         * (@ref ImageDesc::videoScanMode).  Returns a default
         * (invalid) @ref VideoFormat when @p index is out of range.
         *
         * @param index Zero-based image layer index.
         */
        VideoFormat videoFormat(size_t index) const {
                if(index >= _imageList.size()) return VideoFormat();
                const ImageDesc &img = _imageList[index];
                return VideoFormat(img.size(), _frameRate, img.videoScanMode());
        }

        /** @brief Returns a const reference to the list of audio descriptions. */
        const AudioDescList &audioList() const { return _audioList; }
        /** @brief Returns a mutable reference to the list of audio descriptions. */
        AudioDescList &audioList() { return _audioList; }

        /** @brief Returns a const reference to the metadata. */
        const Metadata &metadata() const { return _metadata; }
        /** @brief Returns a mutable reference to the metadata. */
        Metadata &metadata() { return _metadata; }

    private:
        FrameRate           _frameRate;
        ImageDescList       _imageList;
        AudioDescList       _audioList;
        Metadata            _metadata;
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
        if(!stream.readTag(DataStream::TypeMediaDesc)) { desc = MediaDesc(); return stream; }
        FrameRate fr;
        MediaDesc::ImageDescList imgs;
        MediaDesc::AudioDescList auds;
        Metadata meta;
        stream >> fr >> imgs >> auds >> meta;
        if(stream.status() != DataStream::Ok) { desc = MediaDesc(); return stream; }
        desc = MediaDesc();
        desc.setFrameRate(fr);
        desc.imageList() = std::move(imgs);
        desc.audioList() = std::move(auds);
        desc.metadata() = std::move(meta);
        return stream;
}

PROMEKI_NAMESPACE_END
