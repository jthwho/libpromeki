/**
 * @file      imagedesc.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/size2d.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

class ImageDesc {
        class Data {
                PROMEKI_SHARED_FINAL(Data)
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
                ImageDesc() : d(SharedPtr<Data>::create()) { }
                ImageDesc(const Size2D &sz, int pixfmt) :
                        d(SharedPtr<Data>::create(sz, pixfmt)) { }
                ImageDesc(size_t w, size_t h, int pixfmt) :
                        d(SharedPtr<Data>::create(Size2D(w, h), pixfmt)) { }

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
                        d.modify()->size = val;
                        return;
                }

                void setSize(int width, int height) {
                        d.modify()->size.set(width, height);
                        return;
                }

                size_t linePad() const {
                        return d->linePad;
                }

                void setLinePad(size_t val) {
                        d.modify()->linePad = val;
                        return;
                }

                size_t lineAlign() const {
                        return d->lineAlign;
                }

                void setLineAlign(size_t val) {
                        d.modify()->lineAlign = val;
                        return;
                }

                bool interlaced() const {
                        return d->interlaced;
                }

                void setInterlaced(bool val) {
                        d.modify()->interlaced = val;
                        return;
                }

                const PixelFormat *pixelFormat() const {
                        return d->pixelFormat;
                }

                void setPixelFormat(int pixfmt) {
                        d.modify()->pixelFormat = PixelFormat::lookup(pixfmt);
                        return;
                }

                const Metadata &metadata() const {
                        return d->metadata;
                }

                Metadata &metadata() {
                        return d.modify()->metadata;
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

                int referenceCount() const { return d.referenceCount(); }

        private:
                SharedPtr<Data> d;
};

PROMEKI_NAMESPACE_END

