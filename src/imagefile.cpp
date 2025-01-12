/*****************************************************************************
 * imagefile.cpp
 * April 29, 2023
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

#include <promeki/imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/logger.h>

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


