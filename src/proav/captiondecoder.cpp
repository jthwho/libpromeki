/**
 * @file      captiondecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/captiondecoder.h>
#include <promeki/cea608decoder.h>
#include <promeki/cea708decoder.h>
#include <promeki/enums.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

UniquePtr<CaptionDecoder> CaptionDecoder::create(CaptionCodec codec, const Config &cfg) {
        if (codec == CaptionCodec::Cea608) {
                // CEA-608 v1: channel is fixed to CC1.  The factory
                // ignores the cfg.serviceNumber field (608 has no
                // notion of services).
                Cea608Decoder::Config c608;
                return UniquePtr<CaptionDecoder>::takeOwnership(new Cea608Decoder(c608));
        }
        if (codec == CaptionCodec::Cea708) {
                Cea708Decoder::Config c708;
                c708.serviceNumber = cfg.serviceNumber;
                return UniquePtr<CaptionDecoder>::takeOwnership(new Cea708Decoder(c708));
        }
        // CaptionCodec::Both is not a single-codec wire format; callers
        // wanting dual decoding construct one decoder per codec and
        // feed the same CcDataList to each.
        promekiWarn("CaptionDecoder::create: unsupported codec value %d", codec.value());
        return UniquePtr<CaptionDecoder>();
}

PROMEKI_NAMESPACE_END
