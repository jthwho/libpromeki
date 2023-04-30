/*****************************************************************************
 * imagedesc.h
 * April 27, 2023
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
#include <promeki/size2d.h>
#include <promeki/pixelformat.h>

namespace promeki {

class ImageDesc {
        class Data : public SharedData {
                public:
                        Size2D          size;
                        PixelFormat     pixelFormat;

                        Data() = default;
                        Data(const Size2D &s, const PixelFormat &p) :
                                size(s), pixelFormat(p) { }

                        bool isValid() const {
                                return size.isValid() && pixelFormat.isValid();
                        }

                        String toString() const {
                                String ret = size.toString();
                                ret += ' ';
                                ret += pixelFormat.name();
                                return ret;
                        }
        };

        public:
                ImageDesc() : d(new Data) { }
                ImageDesc(const Size2D &sz, const PixelFormat &pfmt) :
                        d(new Data(sz, pfmt)) { }
                ImageDesc(size_t w, size_t h, const PixelFormat &pfmt) :
                        d(new Data(Size2D(w, h), pfmt)) { }

                bool isValid() const {
                        return d->isValid();
                }

                const Size2D &size() const {
                        return d->size;
                }

                size_t width() const {
                        return d->size.width();
                }

                size_t height() const {
                        return d->size.height();
                }

                void setSize(const Size2D &val) {
                        d->size = val;
                        return;
                }

                void setSize(int width, int height) {
                        d->size.set(width, height);
                        return;
                }

                const PixelFormat &pixelFormat() const {
                        return d->pixelFormat;
                }

                void setPixelFormat(const PixelFormat &val) {
                        d->pixelFormat = val;
                        return;
                }

                int planes() const {
                        return d->pixelFormat.planes();
                }

                String toString() const {
                        return d->toString();
                }

        private:
                SharedDataPtr<Data> d;
};

} // namespace promeki

