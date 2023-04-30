/*****************************************************************************
 * image.h
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

#include <promeki/shareddata.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>

namespace promeki {

class Image {
        public:
                class Data : public SharedData {
                        public:
                                ImageDesc       desc;
                                Buffer          plane[PixelFormat::MaxComponents];

                                Data() = default;
                                Data(const ImageDesc &desc, const MemSpace &ms);
                                void clear();

                        private:
                                bool allocate(const ImageDesc &desc, const MemSpace &ms);
                };

                Image() : d(new Data()) { }
                Image(const ImageDesc &d, const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(d, ms)) { }
                Image(const Size2D &s, const PixelFormat &fmt, const MemSpace &ms = MemSpace::Default) :
                        d(new Data(ImageDesc(s, fmt), ms)) { }
                Image(size_t w, size_t h, const PixelFormat &fmt, const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(ImageDesc(w, h, fmt), ms)) { }

                bool isValid() const {
                        return d->desc.isValid();
                }

                const ImageDesc &desc() const {
                        return d->desc;
                }

                const Size2D &size() const {
                        return d->desc.size();
                }

                size_t width() const {
                        return d->desc.width();
                }

                size_t height() const {
                        return d->desc.height();
                }

        private:
                SharedDataPtr<Data> d;
};

} // namespace promeki
