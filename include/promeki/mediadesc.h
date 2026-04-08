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

PROMEKI_NAMESPACE_BEGIN

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
