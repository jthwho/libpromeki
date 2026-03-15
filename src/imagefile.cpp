/**
 * @file      imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/imagefile.h>
#include <promeki/proav/imagefileio.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

ImageFile::ImageFile(int id) : 
        _io(ImageFileIO::lookup(id)) 
{

}

Error ImageFile::load() {
        return _io->load(*this);
}

Error ImageFile::save() {
        return _io->save(*this);
}

PROMEKI_NAMESPACE_END


