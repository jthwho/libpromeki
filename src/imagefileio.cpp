/*****************************************************************************
 * imagefileio.cpp
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
#include <promeki/imagefileio.h>
#include <promeki/error.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

using ImageFileIOMap = std::map<int, ImageFileIO *>;

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

