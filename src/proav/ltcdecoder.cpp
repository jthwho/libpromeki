/**
 * @file      ltcdecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/ltcdecoder.h>
#include <promeki/audiodesc.h>

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

LtcDecoder::DecodedList LtcDecoder::decodeInterleaved(const AudioDesc &desc, const uint8_t *data, size_t samples,
                                                      int channelIndex) {
        const int channels = static_cast<int>(desc.channels());
        if (channels <= 0 || channelIndex < 0 || channelIndex >= channels) return DecodedList();
        if (static_cast<int>(desc.sampleRate()) != _decoder.sample_rate) return DecodedList();

        if (samples == 0) {
                _results.clear();
                return _results;
        }

        // Fast path: native int8 mono on channel 0 — feed straight
        // through to libvtc with no conversion.
        if (channels == 1 && channelIndex == 0 && desc.format().id() == AudioFormat::PCMI_S8) {
                return decode(reinterpret_cast<const int8_t *>(data), samples);
        }

        // Format-agnostic path: convert every interleaved sample frame
        // to normalised float via @ref AudioDesc::samplesToFloat (which
        // is the same per-format helper @ref AudioBuffer uses internally
        // for its own data-type conversions), then pull the requested
        // channel out and quantise to int8 for libvtc.  Scratch buffers
        // are members of LtcDecoder so this stays allocation-free in
        // steady state across many calls.
        const size_t totalScalars = samples * static_cast<size_t>(channels);
        if (_floatScratch.size() < totalScalars) _floatScratch.resize(totalScalars);
        if (_int8Scratch.size() < samples) _int8Scratch.resize(samples);

        desc.samplesToFloat(_floatScratch.data(), data, samples);

        for (size_t s = 0; s < samples; s++) {
                float v = _floatScratch[s * static_cast<size_t>(channels) + static_cast<size_t>(channelIndex)];
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                _int8Scratch[s] = static_cast<int8_t>(std::lround(v * 127.0f));
        }

        return decode(_int8Scratch.data(), samples);
}

LtcDecoder::DecodedList LtcDecoder::decode(const PcmAudioPayload &audio, int channelIndex) {
        if (!audio.isValid()) return DecodedList();
        if (audio.planeCount() == 0) return DecodedList();
        // Only interleaved layouts are supported — the first (and only)
        // plane must cover every channel.  Planar payloads fall out
        // here rather than silently decoding channel 0 only.
        if (audio.desc().format().isPlanar()) return DecodedList();
        auto view = audio.plane(0);
        if (!view.isValid()) return DecodedList();
        return decodeInterleaved(audio.desc(), view.data(), audio.sampleCount(), channelIndex);
}

void LtcDecoder::reset() {
        vtc_ltc_decoder_reset(&_decoder);
        return;
}

void LtcDecoder::decoderCallback(const VtcTimecode *tc, int64_t sampleStart, int64_t sampleLength, void *userData) {
        LtcDecoder     *self = static_cast<LtcDecoder *>(userData);
        DecodedTimecode result;
        result.timecode = Timecode(Timecode::Mode(tc->format), tc->hour, tc->min, tc->sec, tc->frame);
        result.sampleStart = sampleStart;
        result.sampleLength = sampleLength;
        self->_results.pushToBack(result);
        return;
}

PROMEKI_NAMESPACE_END
