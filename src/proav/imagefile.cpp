/**
 * @file      imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

ImageFile::ImageFile(int id) : 
        _io(ImageFileIO::lookup(id)) 
{

}

Error ImageFile::load(const MediaConfig &config) {
        return _io->load(*this, config);
}

Error ImageFile::save(const MediaConfig &config) {
        return _io->save(*this, config);
}

PROMEKI_NAMESPACE_END


