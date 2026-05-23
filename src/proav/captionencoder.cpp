/**
 * @file      captionencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/captionencoder.h>
#include <promeki/cea608encoder.h>
#include <promeki/cea708encoder.h>
#include <promeki/enums.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

UniquePtr<CaptionEncoder> CaptionEncoder::create(CaptionCodec codec, const Config &cfg) {
        if (codec == CaptionCodec::Cea608) {
                Cea608Encoder::Config c608;
                c608.frameRate = cfg.frameRate;
                return UniquePtr<CaptionEncoder>::takeOwnership(new Cea608Encoder(c608));
        }
        if (codec == CaptionCodec::Cea708) {
                Cea708Encoder::Config c708;
                c708.frameRate = cfg.frameRate;
                c708.serviceNumber = cfg.serviceNumber;
                c708.windowCols = cfg.windowCols;
                return UniquePtr<CaptionEncoder>::takeOwnership(new Cea708Encoder(c708));
        }
        // CaptionCodec::Both is not a single-codec wire format; callers
        // wanting dual carriage construct one encoder per codec and
        // merge their nextFrame outputs into the same CDP.
        promekiWarn("CaptionEncoder::create: unsupported codec value %d", codec.value());
        return UniquePtr<CaptionEncoder>();
}

PROMEKI_NAMESPACE_END
