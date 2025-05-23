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

#include <promeki/namespace.h>
#include <promeki/shareddata.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

class Image {
        public:
                class Data : public SharedData {
                        public:
                                ImageDesc       desc;
                                Buffer::List    planeList;

                                Data() = default;
                                Data(const ImageDesc &desc, const MemSpace &ms);
                                void clear();
                                bool fill(char value) const {
                                        for(auto &p : planeList) {
                                                if(!p.fill(value)) return false;
                                        }
                                        return !planeList.empty();
                                }
                                Image convert(PixelFormat::ID pixelFormat, const Metadata &metadata) const;

                        private:
                                bool allocate(const ImageDesc &desc, const MemSpace &ms);
                };

                Image() : d(new Data()) { }
                Image(const ImageDesc &d, const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(d, ms)) { }
                Image(const Size2D &s, int pixfmt, const MemSpace &ms = MemSpace::Default) :
                        d(new Data(ImageDesc(s, pixfmt), ms)) { }
                Image(size_t w, size_t h, int pixfmt, const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(ImageDesc(w, h, pixfmt), ms)) { }

                bool isValid() const {
                        return d->desc.isValid();
                }

                const ImageDesc &desc() const {
                        return d->desc;
                }

                int pixelFormatID() const {
                        return d->desc.pixelFormatID();
                }

                const PixelFormat *pixelFormat() const {
                        return d->desc.pixelFormat();
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

                const Metadata &metadata() const {
                        return d->desc.metadata();
                }

                Metadata &metadata() {
                        return d->desc.metadata();
                }

                size_t lineStride(int plane = 0) const {
                        return d->desc.pixelFormat()->lineStride(plane, d->desc);
                }

                Buffer plane(int index = 0) const {
                        return d->planeList[index];
                }

                Buffer::List planes() const {
                        return d->planeList;
                }

                void *data(int index = 0) const {
                        return d->planeList[index].data();
                }

                bool fill(char value) const {
                        return d->fill(value);
                }

                PaintEngine createPaintEngine() const {
                        return d->desc.pixelFormat()->createPaintEngine(*this);
                }

                Image convert(PixelFormat::ID pixelFormat, const Metadata &metadata) const {
                    return d->convert(pixelFormat, metadata);
                }

        private:
                SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END

