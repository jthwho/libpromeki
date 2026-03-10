/**
 * @file      videodesc.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
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

/** @brief Describes a video format including frame rate, image layers, audio channels, and metadata. */
class VideoDesc {
    PROMEKI_SHARED_FINAL(VideoDesc)
    public:
        /** @brief Shared pointer type for VideoDesc. */
        using Ptr = SharedPtr<VideoDesc>;

        /** @brief List of ImageDesc values describing each image layer. */
        using ImageDescList = List<ImageDesc>;

        /** @brief List of AudioDesc values describing each audio channel group. */
        using AudioDescList = List<AudioDesc>;

        /** @brief Constructs a default (invalid) video description. */
        VideoDesc() = default;

        /** @brief Returns true if the video description is valid (has a valid frame rate and at least one image or audio description). */
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

PROMEKI_NAMESPACE_END

