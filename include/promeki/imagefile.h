/*****************************************************************************
 * imagefile.h
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

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

class ImageFile {
        public:
                enum ID {
                        Invalid = 0,
                        PNG
                };

                typedef Error (*LoadFunc)(const String &filename, Image &img);
                typedef Error (*SaveFunc)(const String &filename, const Image &image);

                struct Data {
                        ID              id;
                        String          name;
                        LoadFunc        load;
                        SaveFunc        save;
                };

                ImageFile(ID id = Invalid) : d(lookup(id)) { }

                Error load(const String &filename, Image &img) {
                        if(d->load == nullptr) return Error::NotImplemented;
                        return d->load(filename, img);
                }

                Error save(const String &filename, const Image &img) {
                        if(d->save == nullptr) {
                                return Error::NotImplemented;
                        }
                        return d->save(filename, img);
                }

        private:
                const Data *d = nullptr;
                static const Data *lookup(ID id);
};

PROMEKI_NAMESPACE_END


