/*****************************************************************************
 * pixelformat.cpp
 * May 15, 2023
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

#include <map>
#include <promeki/pixelformat.h>
#include <promeki/paintengine.h>
#include <promeki/image.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

using PixelFormatMap = std::map<int, PixelFormat *>;

static PixelFormatMap &pixelFormatMap() {
        static PixelFormatMap ret;
        return ret;
}

static PixelFormat &pixelFormatInvalid() {
        static PixelFormat ret;
        return ret;
}

int PixelFormat::registerPixelFormat(PixelFormat *p) {
        if(p == nullptr) {
                promekiWarn("PixelFormat::registerPixelFormat() called with null pointer");
                return -1;
        }
        PixelFormatMap &map = pixelFormatMap();
        int ret = map.size();
        map[p->id()] = p;
        promekiInfo("Registered PixelFormat %d '%s' - '%s'", p->id(), p->name().cstr(), p->desc().cstr());
        return ret;
}

const PixelFormat *PixelFormat::lookup(int id) {
        PixelFormatMap &map = pixelFormatMap();
        auto it = map.find(id);
        return it == map.end() ? &pixelFormatInvalid() : it->second;
}

size_t PixelFormat::lineStride(size_t planeIndex, const ImageDesc &desc) const {
        return isValidPlane(planeIndex) ? __lineStride(planeIndex, desc) : 0;
}

size_t PixelFormat::planeSize(size_t planeIndex, const ImageDesc &size) const {
        return isValidPlane(planeIndex) ? __planeSize(planeIndex, size) : 0;
}

PaintEngine PixelFormat::createPaintEngine(const Image &img) const {
        return __createPaintEngine(img);
}

size_t PixelFormat::__lineStride(size_t planeIndex, const ImageDesc &desc) const {
        return 0;
}

size_t PixelFormat::__planeSize(size_t planeIndex, const ImageDesc &desc) const {
        return 0;
}

PaintEngine PixelFormat::__createPaintEngine(const Image &img) const {
        return PaintEngine();
}

PROMEKI_NAMESPACE_END

