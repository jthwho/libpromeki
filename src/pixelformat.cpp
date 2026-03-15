/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/pixelformat.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/image.h>
#include <promeki/core/map.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

using PixelFormatMap = Map<int, PixelFormat *>;

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

