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

StringList Image::dump(const String &indent) const {
        StringList out;
        VariantLookup<Image>::forEachScalar([this, &out, &indent](const String &name) {
                auto v = VariantLookup<Image>::resolve(*this, name);
                if(v.has_value()) {
                        out += indent + name + ": " + v->format(String());
                }
        });
        for(size_t i = 0; i < _planeList.size(); ++i) {
                const Buffer::Ptr &p = _planeList[i];
                if(p.isValid()) {
                        out += indent + String::sprintf("Plane[%zu]: size=%zu bytes (alloc=%zu, align=%zu)",
                                                        i, p->size(), p->allocSize(), p->align());
                } else {
                        out += indent + String::sprintf("Plane[%zu]: <null>", i);
                }
        }
        if(_packet.isValid()) {
                out += indent + String::sprintf("Packet: pts=%s dts=%s flags=0x%08x size=%zu",
                                                _packet->pts().toString().cstr(),
                                                _packet->dts().toString().cstr(),
                                                static_cast<unsigned>(_packet->flags()),
                                                _packet->size());
        }
        StringList mdLines = _desc.metadata().dump();
        if(!mdLines.isEmpty()) {
                out += indent + "Meta:";
                String sub = indent + "  ";
                for(const String &ln : mdLines) out += sub + ln;
        }
        return out;
}

PROMEKI_LOOKUP_REGISTER(Image)
        .scalar("Width",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(static_cast<uint32_t>(i.desc().width()));
                })
        .scalar("Height",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(static_cast<uint32_t>(i.desc().height()));
                })
        .scalar("Size",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.desc().size());
                })
        .scalar("PixelDesc",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.desc().pixelDesc());
                })
        .scalar("PixelFormat",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.desc().pixelFormat());
                })
        .scalar("ColorModel",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.desc().colorModel());
                })
        .scalar("LinePad",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(i.desc().linePad()));
                })
        .scalar("LineAlign",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(i.desc().lineAlign()));
                })
        .scalar("ScanMode",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(String(i.desc().videoScanMode().valueName()));
                })
        .scalar("PlaneCount",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(static_cast<int32_t>(i.desc().planeCount()));
                })
        .scalar("IsValid",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.isValid());
                })
        .scalar("IsCompressed",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.isCompressed());
                })
        .scalar("IsExclusive",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(i.isExclusive());
                })
        .scalar("CompressedSize",
                [](const Image &i) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(i.compressedSize()));
                })
        .database<"Metadata">("Meta",
                [](const Image &i) -> const VariantDatabase<"Metadata"> * {
                        return &i.metadata();
                },
                [](Image &i) -> VariantDatabase<"Metadata"> * {
                        return &i.metadata();
                });

PROMEKI_NAMESPACE_END
