/**
 * @file      proav/codec.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/proav/image.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base class for image codecs.
 *
 * Provides an interface for querying conversion capabilities and creating
 * codec instances that perform image format conversions. Subclasses implement
 * specific encoding/decoding algorithms.
 */
class Codec {
    public:
        /** @brief Represents an active codec instance used for performing conversions. */
        class Instance {
            public:
                /**
                 * @brief Constructs a codec instance associated with the given codec.
                 * @param codec Pointer to the parent codec.
                 */
                Instance(Codec *codec) : _codec(codec) {};

                /** @brief Destructor. */
                virtual ~Instance() {};

                /**
                 * @brief Converts the given input image and returns the result.
                 * @param input The source image to convert.
                 * @return The converted image.
                 */
                Image convert(const Image &input) {
                    return Image();
                }

            private:
                Codec *_codec = nullptr;
        };

        /** @brief Default constructor. */
        Codec() {};

        /** @brief Destructor. */
        virtual ~Codec() {};

        /**
         * @brief Returns whether this codec can convert the given input to the specified output format.
         * @param inDesc  The input image description.
         * @param outID   The desired output pixel format ID.
         * @param outMeta The desired output metadata.
         * @return true if the codec supports the requested conversion.
         */
        bool canConvert(const ImageDesc &inDesc, PixelFormat::ID outID, const Metadata &outMeta) const {
            return false;
        }

        /**
         * @brief Creates and returns a new codec instance.
         * @return A pointer to a new Instance, or nullptr if creation fails.
         */
        Instance *createInstance() {
            return nullptr;
        }
};

PROMEKI_NAMESPACE_END

