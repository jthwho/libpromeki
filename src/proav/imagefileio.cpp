/**
 * @file      imagefileio.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imagefileio.h>
#include <promeki/error.h>
#include <promeki/map.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_imagefile.h>
#include <promeki/logger.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(ImageFileIO)

// Owning map: the registry adopts each backend (allocated via
// PROMEKI_REGISTER_IMAGEFILEIO) and destroys it at process exit when
// the function-local static unwinds.  Previously the map stored raw
// pointers and the backends leaked for the lifetime of the process.
using ImageFileIOMap = Map<int, UniquePtr<ImageFileIO>>;

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
        int id = p->id();
        map[id] = UniquePtr<ImageFileIO>::takeOwnership(p);
        promekiDebug("Registered ImageFileIO %d '%s'", p->id(), p->name().cstr());

        // Piggy-back a MediaIO FormatDesc registration onto every
        // ImageFileIO registration so the MediaIO registry exposes
        // one entry per backend (@c "ImgSeqDPX", @c "ImgSeqPNG", …)
        // with the correct per-format extensions and canBeSource /
        // canBeSink flags.  Skipping backends whose constructor did
        // not set any extensions keeps us from emitting empty
        // FormatDescs for future ImageFileIO subclasses that are
        // driven by explicit ID only.
        if(!p->extensions().isEmpty()) {
                MediaIO::registerFormat(
                        MediaIOTask_ImageFile::buildFormatDescFor(p));
        }
        return ret;
}

const ImageFileIO *ImageFileIO::lookup(int id) {
        ImageFileIOMap &map = imageFileIOMap();
        auto it = map.find(id);
        return it == map.end() ? &imageFileIOInvalid() : it->second.get();
}

ImageFileIO::IDList ImageFileIO::registeredIDs() {
        ImageFileIOMap &map = imageFileIOMap();
        IDList out;
        for(auto it = map.cbegin(); it != map.cend(); ++it) {
                out.pushToBack(it->first);
        }
        return out;
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

