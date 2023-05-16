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

#include <promeki/namespace.h>
#include <promeki/shareddata.h>
#include <promeki/size2d.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

class ImageDesc {
        class Data : public SharedData {
                public:
                        Size2D                  size;
                        size_t                  linePad = 0;
                        size_t                  lineAlign = 1;
                        bool                    interlaced = false;
                        const PixelFormat       *pixelFormat;
                        Metadata                metadata;

                        Data() : pixelFormat(PixelFormat::lookup(PixelFormat::Invalid)) {}
                        Data(const Size2D &s, int pf) : size(s), pixelFormat(PixelFormat::lookup(pf)) {}

                        bool isValid() const {
                                return size.isValid() && pixelFormat->isValid();
                        }

                        String toString() const {
                                String ret = size.toString();
                                ret += ' ';
                                ret += pixelFormat->name();
                                return ret;
                        }
        };

        public:
                ImageDesc() : d(new Data) { }
                ImageDesc(const Size2D &sz, int pixfmt) :
                        d(new Data(sz, pixfmt)) { }
                ImageDesc(size_t w, size_t h, int pixfmt) :
                        d(new Data(Size2D(w, h), pixfmt)) { }

                int pixelFormatID() const {
                        return d->pixelFormat->id();
                }

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

                size_t linePad() const {
                        return d->linePad;
                }

                void setLinePad(size_t val) {
                        d->linePad = val;
                        return;
                }

                size_t lineAlign() const {
                        return d->lineAlign;
                }

                void setLineAlign(size_t val) {
                        d->lineAlign = val;
                        return;
                }

                bool interlaced() const {
                        return d->interlaced;
                }

                void setInterlaced(bool val) {
                        d->interlaced = val;
                        return;
                }

                const PixelFormat *pixelFormat() const {
                        return d->pixelFormat;
                }

                void setPixelFormat(int pixfmt) {
                        d->pixelFormat = PixelFormat::lookup(pixfmt);
                        return;
                }

                const Metadata &metadata() const {
                        return d->metadata;
                }

                Metadata &metadata() {
                        return d->metadata;
                }

                int planeCount() const {
                        return d->pixelFormat->planeCount();
                }

                String toString() const {
                        return d->toString();
                }

                operator String() const {
                        return toString();
                }

        private:
                SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END

