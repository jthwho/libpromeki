/*****************************************************************************
 * util.cpp
 * May 03, 2023
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

#include <random>
#include <algorithm>
#include <promeki/util.h>
#include <promeki/error.h>

namespace promeki {

static Error fallbackRand(uint8_t *data, size_t bytes) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for(int i = 0; i < bytes; i++) data[i] = dis(gen);
        return Error();
}

Error promekiRand(uint8_t *data, size_t bytes) {
        // FIXME: Move over platform specific RNG stuff
        return fallbackRand(data, bytes);
}

} // namespace promeki

