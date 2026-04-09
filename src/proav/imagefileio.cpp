/**
 * @file      imagefileio.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imagefileio.h>
#include <promeki/error.h>
#include <promeki/map.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(ImageFileIO)

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
        promekiDebug("Registered ImageFileIO %d '%s'", p->id(), p->name().cstr());
        return ret;
}

const ImageFileIO *ImageFileIO::lookup(int id) {
        ImageFileIOMap &map = imageFileIOMap();
        auto it = map.find(id);
        return it == map.end() ? &imageFileIOInvalid() : it->second;
}

Error ImageFileIO::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)imageFile;
        (void)config;
        return Error::NotImplemented;
}

Error ImageFileIO::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)imageFile;
        (void)config;
        return Error::NotImplemented;
}

PROMEKI_NAMESPACE_END

