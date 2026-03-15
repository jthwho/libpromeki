/**
 * @file      imagefileio.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/imagefileio.h>
#include <promeki/core/error.h>
#include <promeki/core/map.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

using ImageFileIOMap = Map<int, ImageFileIO *>;

static ImageFileIOMap &imageFileIOMap() {
        static ImageFileIOMap ret;
        return ret;
}

static ImageFileIO &imageFileIOInvalid() {
        static ImageFileIO ret;
        return ret;
}

int ImageFileIO::registerImageFileIO(ImageFileIO *p) {
        if(p == nullptr) {
                promekiWarn("ImageFileIO::registerImageFileIO() called with null pointer");
                return -1;
        }
        ImageFileIOMap &map = imageFileIOMap();
        int ret = map.size();
        map[p->id()] = p;
        promekiInfo("Registered ImageFileIO %d '%s'", p->id(), p->name().cstr());
        return ret;
}

const ImageFileIO *ImageFileIO::lookup(int id) {
        ImageFileIOMap &map = imageFileIOMap();
        auto it = map.find(id);
        return it == map.end() ? &imageFileIOInvalid() : it->second;
}

Error ImageFileIO::load(ImageFile &imageFile) const {
        return Error::NotImplemented;
}

Error ImageFileIO::save(ImageFile &imageFile) const {
        return Error::NotImplemented;
}

PROMEKI_NAMESPACE_END

