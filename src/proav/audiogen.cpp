/**
 * @file      audiogen.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/audiogen.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

void AudioGen::genSilence(size_t chan, float *data, size_t samples) const {
        size_t chans = _desc.channels();
        for (size_t i = 0; i < samples; ++i) {
                *data = 0;
                data += chans;
        }
        return;
}

void AudioGen::genSine(size_t chan, float *data, size_t samples) const {
        const Config &config = _chanConfig[chan];
        size_t        end = _sampleCount + samples;
        float         freq = config.freq;
        float         amp = config.linearGain;
        size_t        chans = _desc.channels();
        for (size_t i = _sampleCount; i < end; ++i) {
                *data = amp * sin(freq * i);
                data += chans;
        }
        return;
}

AudioGen::AudioGen(const AudioDesc &desc) : _desc(desc) {
        for (unsigned int i = 0; i < _desc.channels(); i++) {
                _chanConfig += {Silence, 0.0, AudioLevel(), 0.0, 0.0, 0.0};
        }
}

void AudioGen::setConfig(size_t chan, Config config) {
        if (chan >= _desc.channels()) {
                promekiWarn("Can't set channel %d config, only %d allowed", (int)chan, (int)_desc.channels());
                return;
        }
        // Convert the frequency to the more useful radians per sample
        config.freq = M_PI * 2 * config.freq / _desc.sampleRate();
        // Cache the linear gain to avoid repeated dB-to-linear conversion
        config.linearGain = config.level.toLinearFloat();
        _chanConfig[chan] = config;
        return;
}

bool AudioGen::generate(float *out, size_t samples) {
        if (out == nullptr) return false;
        float *data = out;
        for (size_t chan = 0; chan < _desc.channels(); ++chan) {
                switch (_chanConfig[chan].type) {
                        case Silence: genSilence(chan, data, samples); break;
                        case Sine: genSine(chan, data, samples); break;
                }
                data++; // FIXME: Need to set to new plane for planar.
        }
        _sampleCount += samples;
        return true;
}

PROMEKI_NAMESPACE_END
