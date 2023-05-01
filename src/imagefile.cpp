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
#include <promeki/logger.h>
#include <promeki/structdatabase.h>
#include <promeki/util.h>

namespace promeki {

#ifdef PROMEKI_ENABLE_PNG
// Implementation for these live in src/png.cpp
Error imageFileSavePNG(const String &fn, const Image &img);
//Image imageFileLoadPNG(const String &fn, Error &err);
#endif

#define DEFINE_FORMAT(item) \
        .id = ImageFile::item, \
        .name = PROMEKI_STRINGIFY(item)

static StructDatabase<ImageFile::ID, ImageFile::Data> db = {
        {
                DEFINE_FORMAT(Invalid),
                .load = nullptr,
                .save = nullptr
        },
#ifdef PROMEKI_ENABLE_PNG
        {
                DEFINE_FORMAT(PNG),
                .load = nullptr,
                .save = imageFileSavePNG
        }
#endif
};


const ImageFile::Data *ImageFile::lookup(ID id) {
        return &db.get(id);
}

} // namespace promeki

