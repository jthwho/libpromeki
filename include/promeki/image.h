/**
 * @file      image.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

class Image {
        public:
                class Data {
                        PROMEKI_SHARED_FINAL(Data)
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
                                        return !planeList.isEmpty();
                                }
                                Image convert(PixelFormat::ID pixelFormat, const Metadata &metadata) const;

                        private:
                                bool allocate(const ImageDesc &desc, const MemSpace &ms);
                };

                Image() : d(SharedPtr<Data>::create()) { }
                Image(const ImageDesc &d, const MemSpace &ms = MemSpace::Default) :
                        d(SharedPtr<Data>::create(d, ms)) { }
                Image(const Size2D &s, int pixfmt, const MemSpace &ms = MemSpace::Default) :
                        d(SharedPtr<Data>::create(ImageDesc(s, pixfmt), ms)) { }
                Image(size_t w, size_t h, int pixfmt, const MemSpace &ms = MemSpace::Default) :
                        d(SharedPtr<Data>::create(ImageDesc(w, h, pixfmt), ms)) { }

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
                        return d.modify()->desc.metadata();
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

                int referenceCount() const { return d.referenceCount(); }

        private:
                SharedPtr<Data> d;
};

PROMEKI_NAMESPACE_END

