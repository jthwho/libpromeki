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
#include <promeki/codec.h>
#include <promeki/mediaconfig.h>
#if PROMEKI_ENABLE_CSC
#include <promeki/cscpipeline.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

// RAII wrapper around the raw @ref ImageCodec returned by
// @ref ImageCodec::createCodec.  The codec registry hands ownership to
// the caller; this guard makes sure we always @c delete on every exit
// path without having to repeat the cleanup at each return site.
class CodecHandle {
        public:
                explicit CodecHandle(const String &name)
                        : _codec(ImageCodec::createCodec(name)) { }
                ~CodecHandle() { delete _codec; }

                CodecHandle(const CodecHandle &) = delete;
                CodecHandle &operator=(const CodecHandle &) = delete;

                ImageCodec *operator->() const { return _codec; }
                ImageCodec &operator*() const { return *_codec; }
                bool isValid() const { return _codec != nullptr; }

        private:
                ImageCodec *_codec = nullptr;
};

} // anonymous

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

        const bool srcCompressed = isCompressed();
        const bool dstCompressed = pd.isCompressed();

        // Compressed → compressed: decode to an uncompressed intermediate
        // shared by both codecs, then re-encode.  We prefer a format that
        // is listed in both the source codec's decodeTargets and the
        // destination codec's encodeSources so neither step has to run an
        // extra CSC.  If no overlap exists we fall back to RGBA8_sRGB,
        // which every codec and CSC path knows how to produce.
        if(srcCompressed && dstCompressed) {
                PixelDesc::ID intermediate = PixelDesc::Invalid;
                for(PixelDesc::ID t : pixelDesc().decodeTargets()) {
                        for(PixelDesc::ID s : pd.encodeSources()) {
                                if(t == s) { intermediate = t; break; }
                        }
                        if(intermediate != PixelDesc::Invalid) break;
                }
                if(intermediate == PixelDesc::Invalid) intermediate = PixelDesc::RGBA8_sRGB;

                Image mid = convert(PixelDesc(intermediate), Metadata(), config);
                if(!mid.isValid()) return Image();
                return mid.convert(pd, metadata, config);
        }

        // Compressed → uncompressed: pull the right codec out of the
        // registry by name and decode.  We never reference a concrete
        // codec subclass here — Image::convert is codec-agnostic.
        if(srcCompressed) {
                CodecHandle codec(pixelDesc().codecName());
                if(!codec.isValid() || !codec->canDecode()) {
                        promekiErr("Image::convert: no decoder registered for codec '%s'",
                                   pixelDesc().codecName().cstr());
                        return Image();
                }
                codec->configure(config);

                Image decoded = codec->decode(*this, pd.id());
                if(!decoded.isValid()) {
                        promekiErr("Image::convert: %s decode failed: %s",
                                   codec->name().cstr(),
                                   codec->lastErrorMessage().cstr());
                        return Image();
                }
                decoded.metadata() = metadata;
                if(decoded.pixelDesc() == pd) return decoded;
                // Codec landed on a preferred native target rather than
                // the caller's — finish with an uncompressed CSC.
                return decoded.convert(pd, metadata, config);
        }

        // Uncompressed → compressed: pull the right codec out of the
        // registry, configure it from the caller's MediaConfig, and let
        // it encode.  If the source format isn't one the codec can
        // ingest directly, run a preparatory CSC into the first listed
        // encode source.
        if(dstCompressed) {
                CodecHandle codec(pd.codecName());
                if(!codec.isValid() || !codec->canEncode()) {
                        promekiErr("Image::convert: no encoder registered for codec '%s'",
                                   pd.codecName().cstr());
                        return Image();
                }
                codec->configure(config);

                Image encodeInput = *this;
                const promeki::List<PixelDesc::ID> &sources = pd.encodeSources();
                bool supported = false;
                for(PixelDesc::ID s : sources) {
                        if(pixelDesc().id() == s) { supported = true; break; }
                }
                if(!supported && !sources.isEmpty()) {
                        encodeInput = convert(PixelDesc(sources[0]), Metadata(), config);
                        if(!encodeInput.isValid()) return Image();
                }

                Image encoded = codec->encode(encodeInput);
                if(!encoded.isValid()) {
                        promekiErr("Image::convert: %s encode failed: %s",
                                   codec->name().cstr(),
                                   codec->lastErrorMessage().cstr());
                        return Image();
                }
                encoded.metadata() = metadata;
                return encoded;
        }

        // Uncompressed → uncompressed: CSC path.
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

PROMEKI_NAMESPACE_END
