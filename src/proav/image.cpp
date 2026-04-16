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
#include <promeki/mediaconfig.h>
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
                promekiWarn("Image::fromBuffer: uncompressed pixel description '%s' has %zu planes; "
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
                     const MediaConfig &config) const {
        if(!isValid() || !pd.isValid()) return Image();
        if(pixelDesc() == pd) return *this;

        // Image::convert is the CSC entry point — strictly uncompressed
        // → uncompressed.  Compression / decompression flows through
        // VideoEncoder / VideoDecoder sessions (typically driven by
        // MediaIOTask_VideoEncoder / MediaIOTask_VideoDecoder), which
        // can amortise codec state across frames.  Refusing
        // compressed PixelDescs here keeps the abstraction layered:
        // CSC for pixel-format work, codec sessions for compression.
        if(isCompressed() || pd.isCompressed()) {
                promekiErr("Image::convert: compressed pixel descriptions are not "
                           "supported here (src='%s', dst='%s'); use a VideoEncoder / "
                           "VideoDecoder session instead",
                           pixelDesc().name().cstr(), pd.name().cstr());
                return Image();
        }

#if PROMEKI_ENABLE_CSC
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

std::optional<String> Image::resolveTemplateKey(const String &key, const String &spec) const {
        if(!key.isEmpty() && key.cstr()[0] == '@') {
                return resolvePseudoKey(key, spec);
        }
        Metadata::ID id = Metadata::ID::find(key);
        if(id.isValid() && _desc.metadata().contains(id)) {
                return _desc.metadata().get(id).format(spec);
        }
        return std::nullopt;
}

std::optional<String> Image::resolvePseudoKey(const String &key, const String &spec) const {
        // Build a Variant for the requested introspection value and let
        // Variant::format apply the spec — this gives templates the full
        // std::format vocabulary for free (e.g. "{@Width:05}").
        Variant v;
        if(key == String("@Width"))                v = static_cast<uint32_t>(_desc.width());
        else if(key == String("@Height"))          v = static_cast<uint32_t>(_desc.height());
        else if(key == String("@Size"))            v = _desc.size();
        else if(key == String("@PixelDesc"))       v = _desc.pixelDesc();
        else if(key == String("@PixelFormat"))     v = _desc.pixelFormat();
        else if(key == String("@ColorModel"))      v = _desc.colorModel();
        else if(key == String("@LinePad"))         v = static_cast<uint64_t>(_desc.linePad());
        else if(key == String("@LineAlign"))       v = static_cast<uint64_t>(_desc.lineAlign());
        else if(key == String("@ScanMode"))        v = _desc.videoScanMode().valueName();
        else if(key == String("@PlaneCount"))      v = static_cast<int32_t>(_desc.planeCount());
        else if(key == String("@IsValid"))         v = isValid();
        else if(key == String("@IsCompressed"))    v = isCompressed();
        else if(key == String("@IsExclusive"))     v = isExclusive();
        else if(key == String("@CompressedSize"))  v = static_cast<uint64_t>(compressedSize());
        else return std::nullopt;
        return v.format(spec);
}

PROMEKI_NAMESPACE_END
