/**
 * @file      mediaport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/mediaport.h>

PROMEKI_NAMESPACE_BEGIN

bool MediaPort::isCompatible(const MediaPort &other) const {
        // One must be output, the other input
        if(_direction == other._direction) return false;

        const MediaPort &out = (_direction == Output) ? *this : other;
        const MediaPort &in = (_direction == Input) ? *this : other;

        // Encoded is only compatible with Encoded
        if(out._mediaType == Encoded || in._mediaType == Encoded) {
                if(out._mediaType != Encoded || in._mediaType != Encoded) return false;
                return out._encodedDesc.formatEquals(in._encodedDesc);
        }

        // Same media type: check descriptors match
        if(out._mediaType == in._mediaType) {
                switch(out._mediaType) {
                        case Frame:
                                // Both video and audio descriptors should be compatible
                                return true;
                        case Image:
                                return out._imageDesc.width() == in._imageDesc.width() &&
                                       out._imageDesc.height() == in._imageDesc.height() &&
                                       out._imageDesc.pixelFormatID() == in._imageDesc.pixelFormatID();
                        case Audio:
                                return out._audioDesc.formatEquals(in._audioDesc);
                        default:
                                return false;
                }
        }

        // Frame output can feed Image or Audio inputs (extraction)
        if(out._mediaType == Frame) {
                if(in._mediaType == Image) return true;
                if(in._mediaType == Audio) return true;
        }

        // Image/Audio cannot feed a Frame input
        return false;
}

PROMEKI_NAMESPACE_END
