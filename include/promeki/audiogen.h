/**
 * @file      audiogen.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/audiodesc.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class Audio;

class AudioGen {
	public:
                enum Type {
                        Silence = 0,
                        Sine
                };

                struct Config {
                        Type            type;
                        float           freq;
                        float           amplitude;
                        float           phase;
                        float           dutyCycle;
                };

		AudioGen(const AudioDesc &desc);
		Audio generate(size_t samples);

                const Config &config(size_t chan) const {
                        return _chanConfig[chan];
                }

                void setConfig(size_t chan, Config val);

	private:
                AudioDesc       _desc;
                List<Config>    _chanConfig;
                size_t          _sampleCount = 0;

                void genSilence(size_t chan, float *data, size_t samples) const;
                void genSine(size_t chan, float *data, size_t samples) const;
};

PROMEKI_NAMESPACE_END
