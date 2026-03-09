/**
 * @file      audiofilefactory.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

