/*****************************************************************************
 * audiogen.cpp
 * May 17, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <cmath>
#include <promeki/audiogen.h>
#include <promeki/audio.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

void AudioGen::genSilence(size_t chan, float *data, size_t samples) const {
        size_t chans = _desc.channels();
        for(size_t i = 0; i < samples; ++i) {
                *data = 0;
                data += chans;
        }
        return;
}

void AudioGen::genSine(size_t chan, float *data, size_t samples) const {
        const Config &config = _chanConfig[chan];
	size_t end = _sampleCount + samples;
        float freq = config.freq;
        float amp = config.amplitude;
        size_t chans = _desc.channels();
	for(size_t i = _sampleCount; i < end; ++i) {
		*data = amp * sin(freq * i);
                data += chans;
	}
        return;
}

AudioGen::AudioGen(const AudioDesc &desc) : _desc(desc) {
        for(int i = 0; i < _desc.channels(); i++) {
                _chanConfig += { Silence, 0.0, 0.0, 0.0, 0.0 };
        }
}

void AudioGen::setConfig(size_t chan, Config config) {
        if(chan >= _desc.channels()) {
                promekiWarn("Can't set channel %d config, only %d allowed", (int)chan, (int)_desc.channels());
                return;
        }
        // Convert the frequency to the more useful radians per sample
	config.freq = M_PI * 2 * config.freq / _desc.sampleRate();
        _chanConfig[chan] = config;
        return;
}

Audio AudioGen::generate(size_t samples) {
        AudioDesc workingDesc = _desc.workingDesc();
        Audio ret = Audio(workingDesc, samples);
        if(!ret.isValid()) return Audio();
        float *data = ret.data<float>();
        for(size_t chan = 0; chan < _desc.channels(); ++chan) {
                switch(_chanConfig[chan].type) {
                        case Silence: genSilence(chan, data, samples); break;
                        case Sine: genSine(chan, data, samples); break;
                }
                data++; // FIXME: Need to set to new plane for planar.
        }
        return ret;
}

PROMEKI_NAMESPACE_END

