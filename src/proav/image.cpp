/**
 * @file      image.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/image.h>
#include <promeki/config.h>
#include <promeki/logger.h>
#include <promeki/util.h>
#if PROMEKI_ENABLE_CSC
#include <promeki/cscpipeline.h>
#endif

PROMEKI_NAMESPACE_BEGIN

Image::Image(const ImageDesc &desc, const MemSpace &ms) : _desc(desc) {
        allocate(ms);
}

bool Image::allocate(const MemSpace &ms) {
        const PixelDesc &pd = _desc.pixelDesc();
        int planes = pd.planeCount();
        Buffer::PtrList list;
        for(int i = 0; i < planes; i++) {
                size_t size = pd.planeSize(i, _desc);
                auto buf = Buffer::Ptr::create(size, Buffer::DefaultAlign, ms);
                if(!buf->isValid()) {
                        promekiErr("Image(%s) plane %d allocate failed", _desc.toString().cstr(), i);
                        return false;
                }
                buf->setSize(size);
                list.pushToBack(buf);
        }
        _planeList = list;
        return true;
}

Image Image::fromCompressedData(const void *data, size_t size,
                                size_t width, size_t height, const PixelDesc &pd,
                                const Metadata &metadata) {
        ImageDesc desc(width, height, pd);
        desc.metadata() = metadata;
        desc.metadata().set(Metadata::CompressedSize, (int)size);
        Image img(desc);
        if(img._planeList.isEmpty()) return Image();
        img._planeList[0]->copyFrom(data, size);
        img._planeList[0]->setSize(size);
        return img;
}

Image Image::fromBuffer(const Buffer::Ptr &buffer,
                        size_t width, size_t height, const PixelDesc &pd,
                        const Metadata &metadata) {
        if(!buffer.isValid() || buffer->size() == 0) return Image();
        // Compressed codecs always have a single contiguous bitstream
        // regardless of what their semantic plane count suggests (ProRes
        // for example uses planar YCbCr for its *decoded* format hint,
        // but the encoded sample in a file is always one blob). For
        // uncompressed (raster) formats, multi-plane layouts would need
        // one Buffer per plane — fromBuffer refuses those since it only
        // knows about one buffer.
        if(!pd.isCompressed() && pd.planeCount() != 1) {
                promekiWarn("Image::fromBuffer: uncompressed pixel description '%s' has %d planes; "
                            "fromBuffer only supports single-plane uncompressed formats",
                            pd.name().cstr(), pd.planeCount());
                return Image();
        }

        Image img;
        img._desc = ImageDesc(width, height, pd);
        img._desc.metadata() = metadata;
        if(pd.isCompressed()) {
                img._desc.metadata().set(Metadata::CompressedSize,
                                         static_cast<int>(buffer->size()));
        }
        // Adopt the buffer directly as plane 0 — no allocation, no copy.
        img._planeList.pushToBack(buffer);
        return img;
}

Image Image::convert(const PixelDesc &pd, const Metadata &metadata,
                     const MediaNodeConfig &config) const {
#if PROMEKI_ENABLE_CSC
        if(!isValid() || isCompressed()) return Image();
        if(!pd.isValid() || pd.isCompressed()) return Image();
        if(pixelDesc() == pd) return *this;

        ImageDesc dstDesc(size(), pd);
        dstDesc.metadata() = metadata;
        Image dst(dstDesc);
        if(!dst.isValid()) return Image();

        // Pull a shared, pre-compiled pipeline from the global cache.
        // For repeat conversions between the same format pair, this
        // skips CSCPipeline::compile() entirely on every call after
        // the first.
        CSCPipeline::Ptr pipeline = CSCPipeline::cached(pixelDesc(), pd, config);
        if(!pipeline.isValid() || !pipeline->isValid()) return Image();

        Error err = pipeline->execute(*this, dst);
        if(err.isError()) return Image();
        return dst;
#else
        return Image();
#endif
}

PROMEKI_NAMESPACE_END
