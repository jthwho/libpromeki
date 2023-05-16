/*****************************************************************************
 * imagefileio.h
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

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

#define PROMEKI_REGISTER_IMAGEFILEIO(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_imagefileio_, PROMEKI_UNIQUE_ID) = \
        ImageFileIO::registerImageFileIO(new name);


PROMEKI_NAMESPACE_BEGIN

class Error;
class ImageFile;

class ImageFileIO {
        public:
                static int registerImageFileIO(ImageFileIO *object);
                static const ImageFileIO *lookup(int id);

                ImageFileIO() = default;
                virtual ~ImageFileIO() {};

                int id() const {
                        return _id;
                }

                bool isValid() const {
                        return _id != 0;
                }

                bool canLoad() const {
                        return _canLoad;
                }

                bool canSave() const {
                        return _canSave;
                }

                String name() const {
                        return _name;
                }

                virtual Error load(ImageFile &imageFile) const;
                virtual Error save(ImageFile &imageFile) const;

        protected:
                int             _id = 0;
                bool            _canLoad = false;
                bool            _canSave = false;
                String          _name;

};

PROMEKI_NAMESPACE_END

