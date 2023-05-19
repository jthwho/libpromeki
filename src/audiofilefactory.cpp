/*****************************************************************************
 * audiofilefactory.h
 * May 18, 2023
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

#include <promeki/audiofilefactory.h>
#include <promeki/audiofile.h>
#include <promeki/list.h>
#include <promeki/fileinfo.h>

PROMEKI_NAMESPACE_BEGIN

using FactoryList = List<AudioFileFactory *>;

static FactoryList &audioFileFactoryList() {
        static FactoryList ret;
        return ret;
}

int AudioFileFactory::registerFactory(AudioFileFactory *object) {
        if(object == nullptr) return -1;
        auto &list = audioFileFactoryList();
        int ret = list.size();
        list += object;
        promekiInfo("Registered AudioFileFactory %s", object->name().cstr());
        return ret;
}

const AudioFileFactory *AudioFileFactory::lookup(int op, const String &filename) {
        auto &list = audioFileFactoryList();
        for(auto i : list) {
                if(i->canDoOperation(op, filename)) return i;
        }
        return nullptr;
}

bool AudioFileFactory::canDoOperation(int op, const String &filename) const {
        return false;
}

AudioFile AudioFileFactory::createForOperation(int op) const {
        return AudioFile();
}

bool AudioFileFactory::isExtensionSupported(const String &fn) const {
        String ext = FileInfo(fn).suffix().toLower();
        for(const auto &item : _exts) {
                if(ext == item) return true;
        }
        return false;
}

PROMEKI_NAMESPACE_END

