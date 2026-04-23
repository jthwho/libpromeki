/**
 * @file      imagedata.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * @ref ImageDataEncoder / @ref ImageDataDecoder benchmark cases for
 * promeki-bench.  Each (PixelFormat, dimensions) pair is registered as
 * an individual @ref BenchmarkCase so baseline tracking and
 * regression comparisons can attribute deltas to the right format.
 *
 * Encoder cases measure the cost of stamping the standard TPG-style
 * two-item band (frame ID + BCD timecode) into a freshly-allocated
 * image, which is the same hot path the TPG executes per frame.
 * Decoder cases pre-encode the image once outside the timed window
 * and measure the cost of pulling the bands back out — the
 * format-agnostic CSC-based decode path is what's being measured
 * here, since hand-rolled fast paths are a future enhancement.
 *
 * ### BenchParams keys read by this suite
 *
 * | Key                  | Type        | Default      | Description                                       |
 * |----------------------|-------------|--------------|---------------------------------------------------|
 * | `imagedata.format+=` | StringList  | (see below)  | PixelFormat names to bench                          |
 * | `imagedata.size+=`   | StringList  | "1920x1080"  | Image dimensions, repeatable, e.g. "3840x2160"    |
 *
 * Defaults: the four formats the encoder + decoder unit tests
 * exercise — RGBA8_sRGB, YUV8_422_Rec709, YUV8_422_Planar_Rec709,
 * YUV10_422_v210_Rec709 — at 1920×1080.
 */

#include "cases.h"
#include "../benchparams.h"

#include <promeki/config.h>

#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC

#include <promeki/benchmarkrunner.h>
#include <promeki/imagedataencoder.h>
#include <promeki/imagedatadecoder.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <cstdio>
#include <cstdlib>

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

namespace {

struct ImageSize {
        uint32_t width;
        uint32_t height;
};

struct CaseSpec {
        PixelFormat::ID pd;
        ImageSize     size;
};

/**
 * @brief Looks up a PixelFormat by name and warns on miss.
 */
PixelFormat::ID resolveImageDataFormat(const String &name) {
        PixelFormat pd = PixelFormat::lookup(name);
        if(pd.isValid()) return pd.id();
        std::fprintf(stderr,
                     "promeki-bench: imagedata: unknown PixelFormat '%s'\n",
                     name.cstr());
        return PixelFormat::Invalid;
}

/**
 * @brief Parses a "WxH" string (e.g. "1920x1080") into an ImageSize.
 */
bool parseSize(const String &s, ImageSize &out) {
        const char *p = s.cstr();
        char *end = nullptr;
        long w = std::strtol(p, &end, 10);
        if(end == p || *end != 'x') return false;
        const char *q = end + 1;
        char *end2 = nullptr;
        long h = std::strtol(q, &end2, 10);
        if(end2 == q || w <= 0 || h <= 0) return false;
        out.width  = static_cast<uint32_t>(w);
        out.height = static_cast<uint32_t>(h);
        return true;
}

/**
 * @brief Resolves the active size list from BenchParams (default 1920×1080).
 */
List<ImageSize> resolveSizes() {
        List<ImageSize> out;
        StringList names = benchParams().getStringList(String("imagedata.size"));
        if(names.isEmpty()) names.pushToBack(String("1920x1080"));
        for(const auto &n : names) {
                ImageSize sz{0, 0};
                if(!parseSize(n, sz)) {
                        std::fprintf(stderr,
                                     "promeki-bench: imagedata: bad size '%s' "
                                     "(expected WxH)\n", n.cstr());
                        continue;
                }
                out.pushToBack(sz);
        }
        return out;
}

/**
 * @brief Resolves the active format list from BenchParams.
 *
 * Defaults to the four formats the unit tests exercise — RGBA8,
 * YUYV, planar 4:2:2, and v210 — chosen so the bench covers the
 * cheapest (interleaved 8-bit) and most expensive (v210 packed
 * 10-bit) paths the encoder / decoder support.
 */
List<PixelFormat::ID> resolveFormats() {
        List<PixelFormat::ID> out;
        StringList names = benchParams().getStringList(String("imagedata.format"));
        if(names.isEmpty()) {
                names.pushToBack(String("RGBA8_sRGB"));
                names.pushToBack(String("YUV8_422_Rec709"));
                names.pushToBack(String("YUV8_422_Planar_Rec709"));
                names.pushToBack(String("YUV10_422_v210_Rec709"));
        }
        for(const auto &n : names) {
                PixelFormat::ID id = resolveImageDataFormat(n);
                if(id == PixelFormat::Invalid) continue;
                out.pushToBack(id);
        }
        return out;
}

/**
 * @brief Builds the cross product of formats × sizes.
 */
List<CaseSpec> resolveSpecs() {
        List<CaseSpec> out;
        for(auto pd : resolveFormats()) {
                for(auto sz : resolveSizes()) {
                        out.pushToBack({pd, sz});
                }
        }
        return out;
}

String sizeLabel(ImageSize sz) {
        return String::number(sz.width) + "x" + String::number(sz.height);
}

String caseName(const char *prefix, const CaseSpec &cs) {
        return String(prefix) + "_" + sizeLabel(cs.size) + "_" +
               PixelFormat(cs.pd).name();
}

/**
 * @brief Builds a default two-item band (frame ID + BCD TC).
 *
 * Same shape the TPG produces: item 0 covers lines [0, 16) with a
 * synthetic 64-bit "(streamID << 32) | frameNumber" payload, item 1
 * covers lines [16, 32) with a fixed BCD timecode word.  We don't
 * need real frame numbers — the bench is measuring the encode cost,
 * not the value semantics.
 */
List<ImageDataEncoder::Item> buildDefaultItems() {
        List<ImageDataEncoder::Item> items;
        items.pushToBack({  0, 16, 0xC0FFEEAA00000000ull });
        items.pushToBack({ 16, 16, 0x0001000000000000ull });
        return items;
}

/**
 * @brief Encoder hot-path benchmark closure for one (format, size) spec.
 */
BenchmarkCase::Function buildEncoderCase(CaseSpec spec) {
        return [spec](BenchmarkState &state) {
                ImageDesc desc(spec.size.width, spec.size.height,
                               PixelFormat(spec.pd));
                ImageDataEncoder encoder(desc);
                if(!encoder.isValid()) {
                        // Image too narrow for the format's alignment
                        // quantum — surface as an "invalid" counter so
                        // the row in the report is recognisable, then
                        // run an empty loop so the runner sees iterations.
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                Image img(spec.size.width, spec.size.height,
                          PixelFormat(spec.pd));
                if(!img.isValid()) {
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }
                img.fill(0);

                List<ImageDataEncoder::Item> items = buildDefaultItems();

                // One untimed warmup pass to fault in any per-call
                // allocations the encoder may make on the first
                // invocation.
                encoder.encode(img, items);

                for(auto _ : state) {
                        (void)_;
                        encoder.encode(img, items);
                }

                // Throughput is per *band region* — only the rows the
                // encoder actually writes count, not the whole image.
                // For the default 32-line band that's 32 × width × bpp
                // for the luma plane plus chroma overhead, so we use
                // the band's plane-0 line stride × line count as a
                // representative byte figure.
                const size_t lumaStride = img.lineStride(0);
                const size_t bandRows   = 32;
                const size_t bytesPerIter = lumaStride * bandRows;

                state.setItemsProcessed(state.iterations());
                state.setBytesProcessed(state.iterations() * bytesPerIter);
                state.setCounter(String("bit_width_px"),
                                 static_cast<double>(encoder.bitWidth()));
                state.setLabel(sizeLabel(spec.size) + " " +
                               PixelFormat(spec.pd).name() +
                               " encode (32-line band)");
        };
}

/**
 * @brief Decoder hot-path benchmark closure for one (format, size) spec.
 *
 * The decoder cases encode once outside the timed window so they
 * isolate the decode cost — there's no point conflating encoder and
 * decoder timing in a single number.
 */
BenchmarkCase::Function buildDecoderCase(CaseSpec spec) {
        return [spec](BenchmarkState &state) {
                ImageDesc desc(spec.size.width, spec.size.height,
                               PixelFormat(spec.pd));
                ImageDataEncoder encoder(desc);
                if(!encoder.isValid()) {
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                Image img(spec.size.width, spec.size.height,
                          PixelFormat(spec.pd));
                if(!img.isValid()) {
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }
                img.fill(0);

                // Pre-encode once.  The image is reused on every
                // iteration of the timed loop, so the decoder always
                // sees the same valid input.
                List<ImageDataEncoder::Item> items = buildDefaultItems();
                if(encoder.encode(img, items).isError()) {
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                ImageDataDecoder decoder(desc);
                if(!decoder.isValid()) {
                        state.setCounter(String("invalid"), 1.0);
                        for(auto _ : state) (void)_;
                        return;
                }

                List<ImageDataDecoder::Band> bands;
                bands.pushToBack({ 0, 16 });
                bands.pushToBack({ 16, 16 });
                ImageDataDecoder::DecodedList out;

                // Untimed warmup so the CSC pipeline's per-format
                // pair cache is populated before the timed loop
                // starts.
                decoder.decode(img, bands, out);

                for(auto _ : state) {
                        (void)_;
                        decoder.decode(img, bands, out);
                }

                const size_t lumaStride = img.lineStride(0);
                const size_t bandRows   = 32;
                const size_t bytesPerIter = lumaStride * bandRows;

                state.setItemsProcessed(state.iterations());
                state.setBytesProcessed(state.iterations() * bytesPerIter);
                state.setCounter(String("bit_width_px"),
                                 static_cast<double>(decoder.expectedBitWidth()));
                state.setLabel(sizeLabel(spec.size) + " " +
                               PixelFormat(spec.pd).name() +
                               " decode (32-line band)");
        };
}

}  // namespace

void registerImageDataCases() {
        const String suite("imagedata");
        for(const auto &spec : resolveSpecs()) {
                BenchmarkRunner::registerCase(BenchmarkCase(
                        suite,
                        caseName("encode", spec),
                        String("ImageDataEncoder.encode — ") +
                                sizeLabel(spec.size) + " " +
                                PixelFormat(spec.pd).name(),
                        buildEncoderCase(spec)));
                BenchmarkRunner::registerCase(BenchmarkCase(
                        suite,
                        caseName("decode", spec),
                        String("ImageDataDecoder.decode — ") +
                                sizeLabel(spec.size) + " " +
                                PixelFormat(spec.pd).name(),
                        buildDecoderCase(spec)));
        }
}

String imageDataParamHelp() {
        return String(
                "imagedata suite parameters:\n"
                "  imagedata.format+=<name> Add a PixelFormat to the bench set.  Default:\n"
                "                             RGBA8_sRGB, YUV8_422_Rec709,\n"
                "                             YUV8_422_Planar_Rec709, YUV10_422_v210_Rec709\n"
                "  imagedata.size+=WxH      Add an image size (default: 1920x1080).  May\n"
                "                             be repeated, e.g.  -p imagedata.size+=3840x2160\n"
                "\n"
                "  Cases run as the cross product of formats × sizes.  Each (format,size)\n"
                "  registers two cases: encode_<size>_<fmt> and decode_<size>_<fmt>.\n"
                "  Encoder cases measure the cost of stamping the standard 32-line band\n"
                "  (TPG convention: frame ID + BCD timecode); decoder cases pre-encode\n"
                "  once outside the timed window and measure the format-agnostic CSC\n"
                "  decode path (which is what the inspector currently uses).  Throughput\n"
                "  is reported per band region (32 lines), not per whole image.\n");
}

}  // namespace benchutil
PROMEKI_NAMESPACE_END

#else  // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

void registerImageDataCases() {
        // PROAV / CSC disabled at configure time — nothing to register.
}

String imageDataParamHelp() {
        return String(
                "imagedata suite parameters: (disabled — built without PROMEKI_ENABLE_CSC)\n");
}

}  // namespace benchutil
PROMEKI_NAMESPACE_END

#endif  // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC
