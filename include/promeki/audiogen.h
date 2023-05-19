/*****************************************************************************
 * audiogen.h
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
