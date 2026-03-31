/**
 * @file      audiotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/audiotestpattern.h>
#include <promeki/proav/audiogen.h>
#include <promeki/proav/ltcencoder.h>
#include <promeki/proav/audio.h>
#include <promeki/core/timecode.h>

PROMEKI_NAMESPACE_BEGIN

AudioTestPattern::AudioTestPattern(const AudioDesc &desc) : _desc(desc) {
}

AudioTestPattern::~AudioTestPattern() {
        delete _audioGen;
        delete _ltcEncoder;
}

Error AudioTestPattern::configure() {
        delete _audioGen;
        _audioGen = nullptr;
        delete _ltcEncoder;
        _ltcEncoder = nullptr;

        if(_mode == LTC) {
                _ltcEncoder = new LtcEncoder((int)_desc.sampleRate(), _ltcLevel.toLinearFloat());
        } else {
                _audioGen = new AudioGen(_desc);
                for(size_t ch = 0; ch < _desc.channels(); ch++) {
                        AudioGen::Config cfg;
                        if(_mode == Tone) {
                                cfg.type = AudioGen::Sine;
                                cfg.freq = (float)_toneFreq;
                                cfg.level = _toneLevel;
                                cfg.phase = 0.0f;
                                cfg.dutyCycle = 0.0f;
                        } else {
                                cfg.type = AudioGen::Silence;
                                cfg.freq = 0.0f;
                                cfg.level = AudioLevel();
                                cfg.phase = 0.0f;
                                cfg.dutyCycle = 0.0f;
                        }
                        _audioGen->setConfig(ch, cfg);
                }
        }
        return Error::Ok;
}

Audio AudioTestPattern::create(size_t samples, const Timecode &tc) const {
        if(_mode == LTC && _ltcEncoder != nullptr) {
                Audio ltcAudio = _ltcEncoder->encode(tc);
                if(!ltcAudio.isValid()) return Audio();

                // If multi-channel and specific LTC channel, embed in silent multi-ch audio
                if(_desc.channels() > 1 && _ltcChannel >= 0) {
                        Audio audio(_desc, ltcAudio.samples());
                        audio.zero();
                        int8_t *ltcData = ltcAudio.data<int8_t>();
                        float *outData = audio.data<float>();
                        size_t channels = _desc.channels();
                        for(size_t s = 0; s < ltcAudio.samples(); s++) {
                                float val = (float)ltcData[s] / 127.0f;
                                outData[s * channels + (size_t)_ltcChannel] = val;
                        }
                        return audio;
                }
                return ltcAudio;
        }

        if(_audioGen != nullptr) {
                return _audioGen->generate(samples);
        }

        return Audio();
}

Audio AudioTestPattern::create(size_t samples) const {
        return create(samples, Timecode());
}

void AudioTestPattern::render(Audio &audio, const Timecode &tc) const {
        Audio generated = create(audio.samples(), tc);
        if(!generated.isValid()) return;

        // Copy generated data into the target buffer
        size_t bytes = audio.samples() * audio.desc().channels() * audio.desc().bytesPerSample();
        size_t genBytes = generated.samples() * generated.desc().channels() * generated.desc().bytesPerSample();
        size_t copyBytes = (bytes < genBytes) ? bytes : genBytes;
        memcpy(audio.data<uint8_t>(), generated.data<uint8_t>(), copyBytes);
}

void AudioTestPattern::render(Audio &audio) const {
        render(audio, Timecode());
}

Result<AudioTestPattern::Mode> AudioTestPattern::fromString(const String &name) {
        if(name == "tone")    return makeResult(Tone);
        if(name == "silence") return makeResult(Silence);
        if(name == "ltc")     return makeResult(LTC);
        return makeError<Mode>(Error::Invalid);
}

String AudioTestPattern::toString(Mode mode) {
        switch(mode) {
                case Tone:    return "tone";
                case Silence: return "silence";
                case LTC:     return "ltc";
        }
        return "unknown";
}

PROMEKI_NAMESPACE_END
