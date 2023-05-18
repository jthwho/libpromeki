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

#include <promeki/audiogen.h>
#include <promeki/audio.h>

PROMEKI_NAMESPACE_BEGIN

AudioGen::AudioGen(const AudioDesc &desc) : _desc(desc) {
        for(int i = 0; i < _desc.channels(); i++) {
                _chanConfig += { Silence, 1000.0, 0.3, 0.0, 0.5 };
        }
}

Audio AudioGen::generate(size_t samples) {
        return Audio();
}

PROMEKI_NAMESPACE_END

