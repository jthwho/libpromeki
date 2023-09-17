/**
 * @file      codec.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

class Codec {
    public:
        class Instance {
            public:
                Instance(Codec *codec) : _codec(codec) {};
                virtual ~Instance() {};

                Image convert(const Image &input) {
                    return Image();
                }

            private:
                Codec *_codec = nullptr;
        };

        Codec() {};
        virtual ~Codec() {};

        bool canConvert(const ImageDesc &inDesc, PixelFormat::ID outID, const Metadata &outMeta) const {
            return false;
        }

        Instance *createInstance() {
            return nullptr;
        }
};

PROMEKI_NAMESPACE_END

