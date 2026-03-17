/**
 * @file      ltcdecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/ltcdecoder.h>

PROMEKI_NAMESPACE_BEGIN

LtcDecoder::LtcDecoder(int sampleRate) {
        vtc_ltc_decoder_init(&_decoder, sampleRate, decoderCallback, this);
}

void LtcDecoder::setThresholds(int8_t lower, int8_t upper) {
        vtc_ltc_decoder_set_thresholds(&_decoder, lower, upper);
        return;
}

void LtcDecoder::setFuzz(int fuzz) {
        vtc_ltc_decoder_set_fuzz(&_decoder, fuzz);
        return;
}

LtcDecoder::DecodedList LtcDecoder::decode(const int8_t *samples, size_t count) {
        _results.clear();
        vtc_ltc_decoder_decode(&_decoder, samples, count);
        return _results;
}

LtcDecoder::DecodedList LtcDecoder::decode(const Audio &audio) {
        if(!audio.isValid()) return DecodedList();
        return decode(audio.data<int8_t>(), audio.samples());
}

void LtcDecoder::reset() {
        vtc_ltc_decoder_reset(&_decoder);
        return;
}

void LtcDecoder::decoderCallback(const VtcTimecode *tc,
        int64_t sampleStart, int64_t sampleLength, void *userData) {
        LtcDecoder *self = static_cast<LtcDecoder *>(userData);
        DecodedTimecode result;
        result.timecode = Timecode(
                Timecode::Mode(tc->format),
                tc->hour, tc->min, tc->sec, tc->frame
        );
        result.sampleStart = sampleStart;
        result.sampleLength = sampleLength;
        self->_results.pushToBack(result);
        return;
}

PROMEKI_NAMESPACE_END
