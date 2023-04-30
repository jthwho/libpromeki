/*****************************************************************************
 * image.cpp
 * April 29, 2023
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

#include <promeki/image.h>
#include <promeki/logger.h>
#include <promeki/util.h>

namespace promeki {

Image::Data::Data(const ImageDesc &desc, const MemSpace &ms) : desc(desc) {
        allocate(desc, ms);
}

void Image::Data::clear() {
        desc = ImageDesc();
        for(int i = 0; i < PROMEKI_ARRAY_SIZE(plane); i++) plane[i] = Buffer();
        return;
}

bool Image::Data::allocate(const ImageDesc &desc, const MemSpace &ms) {
        const PixelFormat &pfmt = desc.pixelFormat();
        int planes = desc.pixelFormat().planes();
        for(int i = 0; i < planes; i++) {
                size_t size = pfmt.size(desc.size(), i);
                plane[i] = Buffer(size, Buffer::DefaultAlign, ms);
                if(!plane[i].isValid()) {
                        promekiErr("Image(%s) plane %d allocate failed", desc.toString().cstr(), i);
                        clear();
                        return false;
                }

        }
        return true;
}

} // namespace promeki

