/**
 * @file      proav/frame.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/proav/image.h>
#include <promeki/proav/audio.h>
#include <promeki/core/metadata.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A media frame containing images, audio, and metadata.
 *
 * Aggregates one or more image planes, one or more audio tracks, and
 * a metadata container into a single unit that represents a frame of
 * media content.
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

        private:
                Image::PtrList  _imageList;
                Audio::PtrList  _audioList;
                Metadata        _metadata;
};

PROMEKI_NAMESPACE_END
