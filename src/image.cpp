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

PROMEKI_NAMESPACE_BEGIN

Image::Data::Data(const ImageDesc &desc, const MemSpace &ms) : desc(desc) {
        allocate(desc, ms);
}

void Image::Data::clear() {
        desc = ImageDesc();
        planeList.clear();
        return;
}

bool Image::Data::allocate(const ImageDesc &desc, const MemSpace &ms) {
        const PixelFormat &pfmt = desc.pixelFormat();
        int planes = desc.pixelFormat().planes();
        Buffer::List list(planes);
        for(int i = 0; i < planes; i++) {
                size_t size = pfmt.size(desc.size(), i);
                Buffer b = Buffer(size, Buffer::DefaultAlign, ms);
                if(!b.isValid()) {
                        promekiErr("Image(%s) plane %d allocate failed", desc.toString().cstr(), i);
                        return false;
                }
                list[i] = b;
        }
        planeList = list;
        return true;
}

PROMEKI_NAMESPACE_END

