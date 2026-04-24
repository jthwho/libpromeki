/**
 * @file      imagefileio_jpegxs.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <doctest/doctest.h>
#include "codectesthelpers.h"
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/timecode.h>

using namespace promeki;

// ============================================================================
// Helpers
// ============================================================================

// Fill a planar YUV payload with a deterministic pattern.  Works for
// 8-bit (uint8_t samples) and 10/12-bit LE (uint16_t samples).
// Luma is a horizontal ramp, chroma planes are flat mid-grey.
static UncompressedVideoPayload::Ptr makePlanarYUV(int w, int h,
                                                   PixelFormat::ID pd,
                                                   int bitDepth, bool is420) {
        auto img = UncompressedVideoPayload::allocate(
                ImageDesc(w, h, PixelFormat(pd)));
        if(!img.isValid()) return img;
        const int chromaMid = (bitDepth == 8) ? 128 : (bitDepth == 10) ? 512 : 2048;
        const int lumaMax   = (bitDepth == 8) ? 219 : (bitDepth == 10) ? 876 : 3504;
        const size_t pixSize = (bitDepth > 8) ? 2 : 1;
        auto store = [&](uint8_t *row, int x, int val) {
                if(pixSize == 1) {
                        row[x] = (uint8_t)val;
                } else {
                        row[x * 2 + 0] = (uint8_t)(val & 0xFF);
                        row[x * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
                }
        };
        const auto &ml = img->desc().pixelFormat().memLayout();
        // Luma plane.
        {
                uint8_t *plane = img.modify()->data()[0].data();
                const size_t stride = ml.lineStride(0, w);
                for(int y = 0; y < h; y++) {
                        uint8_t *row = plane + y * stride;
                        for(int x = 0; x < w; x++) {
                                store(row, x, ((x * lumaMax) / w) + 16);
                        }
                }
        }
        // Chroma planes.
        const int chromaW = w / 2;
        const int chromaH = is420 ? (h / 2) : h;
        for(size_t p = 1; p < img->desc().pixelFormat().planeCount(); p++) {
                uint8_t *plane = img.modify()->data()[p].data();
                const size_t stride = ml.lineStride(p, w);
                for(int y = 0; y < chromaH; y++) {
                        uint8_t *row = plane + y * stride;
                        for(int x = 0; x < chromaW; x++) {
                                store(row, x, chromaMid);
                        }
                }
        }
        return img;
}

// Convenience: 8-bit 4:2:2 planar.
static UncompressedVideoPayload::Ptr makePlanarYUV422_8(int w, int h) {
        return makePlanarYUV(w, h, PixelFormat::YUV8_422_Planar_Rec709, 8, false);
}

// Mean absolute difference on the luma plane.  Works for 8-bit only.
static double lumaMeanAbsDiff8(const UncompressedVideoPayload &a,
                               const UncompressedVideoPayload &b) {
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        REQUIRE(a.desc().width() == b.desc().width());
        REQUIRE(a.desc().height() == b.desc().height());
        const int w = static_cast<int>(a.desc().width());
        const int h = static_cast<int>(a.desc().height());
        const uint8_t *pa = a.plane(0).data();
        const uint8_t *pb = b.plane(0).data();
        const size_t sa = a.desc().pixelFormat().memLayout().lineStride(0, w);
        const size_t sb = b.desc().pixelFormat().memLayout().lineStride(0, w);
        double sum = 0.0;
        for(int y = 0; y < h; ++y) {
                const uint8_t *ra = pa + y * sa;
                const uint8_t *rb = pb + y * sb;
                for(int x = 0; x < w; ++x) {
                        sum += std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x]));
                }
        }
        return sum / static_cast<double>(w * h);
}

// Compare two files on disk byte-for-byte.  Returns true if
// identical, false otherwise.  Sets la/lb to their sizes.
static bool filesIdentical(const char *pathA, const char *pathB,
                           long &laOut, long &lbOut) {
        FILE *fa = std::fopen(pathA, "rb");
        FILE *fb = std::fopen(pathB, "rb");
        if(!fa || !fb) {
                if(fa) std::fclose(fa);
                if(fb) std::fclose(fb);
                return false;
        }
        std::fseek(fa, 0, SEEK_END);
        std::fseek(fb, 0, SEEK_END);
        laOut = std::ftell(fa);
        lbOut = std::ftell(fb);
        std::fseek(fa, 0, SEEK_SET);
        std::fseek(fb, 0, SEEK_SET);
        if(laOut != lbOut || laOut <= 0) {
                std::fclose(fa);
                std::fclose(fb);
                return false;
        }
        std::vector<uint8_t> ba(laOut), bb(lbOut);
        size_t ra = std::fread(ba.data(), 1, laOut, fa);
        size_t rb = std::fread(bb.data(), 1, lbOut, fb);
        std::fclose(fa);
        std::fclose(fb);
        if(static_cast<long>(ra) != laOut || static_cast<long>(rb) != lbOut) return false;
        return std::memcmp(ba.data(), bb.data(), laOut) == 0;
}

// Returns the file size in bytes, or -1 on error.
static long fileSize(const char *path) {
        FILE *f = std::fopen(path, "rb");
        if(!f) return -1;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fclose(f);
        return sz;
}

// ============================================================================
// Handler registration
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::JpegXS);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "JPEG XS");
}

// ============================================================================
// Round-trip: save uncompressed → load compressed → decode → compare
// ============================================================================
//
// Exercises the full pipeline: the save path encodes the uncompressed
// payload via UncompressedVideoPayload::convert → JpegXsVideoEncoder,
// the load path reads the raw codestream as a zero-copy
// CompressedVideoPayload, and the final decode lands back on the
// original planar YUV format.

TEST_CASE("ImageFileIO JpegXS: YUV8 422 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_yuv8_422.jxs";
        auto src = makePlanarYUV422_8(320, 240);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(loaded->desc().width() == 320);
        CHECK(loaded->desc().height() == 240);

        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV8_422_Planar_Rec709));
        REQUIRE(decoded.isValid());
        REQUIRE(!decoded->isCompressed());
        CHECK(decoded->desc().width() == 320);
        CHECK(decoded->desc().height() == 240);

        const double mad = lumaMeanAbsDiff8(*src, *decoded);
        CHECK(mad < 2.0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: YUV8 420 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_yuv8_420.jxs";
        auto src = makePlanarYUV(320, 240,
                PixelFormat::YUV8_420_Planar_Rec709, 8, true);
        REQUIRE(src.isValid());

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV8_420_Rec709);

        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV8_420_Planar_Rec709));
        REQUIRE(decoded.isValid());
        CHECK(!decoded->isCompressed());
        CHECK(decoded->desc().width() == 320);
        CHECK(decoded->desc().height() == 240);
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::YUV8_420_Planar_Rec709);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: YUV10 422 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_yuv10_422.jxs";
        auto src = makePlanarYUV(320, 240,
                PixelFormat::YUV10_422_Planar_LE_Rec709, 10, false);
        REQUIRE(src.isValid());

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV10_422_Rec709);

        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV10_422_Planar_LE_Rec709));
        REQUIRE(decoded.isValid());
        CHECK(!decoded->isCompressed());
        CHECK(decoded->desc().width() == 320);
        CHECK(decoded->desc().height() == 240);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: YUV12 422 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_yuv12_422.jxs";
        auto src = makePlanarYUV(320, 240,
                PixelFormat::YUV12_422_Planar_LE_Rec709, 12, false);
        REQUIRE(src.isValid());

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV12_422_Rec709);

        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV12_422_Planar_LE_Rec709));
        REQUIRE(decoded.isValid());
        CHECK(!decoded->isCompressed());
        CHECK(decoded->desc().width() == 320);
        CHECK(decoded->desc().height() == 240);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: YUV10 420 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_yuv10_420.jxs";
        auto src = makePlanarYUV(320, 240,
                PixelFormat::YUV10_420_Planar_LE_Rec709, 10, true);
        REQUIRE(src.isValid());

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV10_420_Rec709);

        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV10_420_Planar_LE_Rec709));
        REQUIRE(decoded.isValid());
        CHECK(!decoded->isCompressed());

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: YUV12 420 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_yuv12_420.jxs";
        auto src = makePlanarYUV(320, 240,
                PixelFormat::YUV12_420_Planar_LE_Rec709, 12, true);
        REQUIRE(src.isValid());

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV12_420_Rec709);

        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV12_420_Planar_LE_Rec709));
        REQUIRE(decoded.isValid());
        CHECK(!decoded->isCompressed());

        std::remove(fn);
}

// ============================================================================
// Pass-through: load → save writes the original codestream verbatim
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: pass-through preserves bytes") {
        const char *fnA = "/tmp/promeki_jpegxs_pt_a.jxs";
        const char *fnB = "/tmp/promeki_jpegxs_pt_b.jxs";

        auto src = makePlanarYUV422_8(256, 192);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 4);
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fnA);
        sf.setVideoPayload(src);
        REQUIRE(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fnA);
        REQUIRE(lf.load() == Error::Ok);
        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        REQUIRE(loaded->isCompressed());

        ImageFile sf2(ImageFile::JpegXS);
        sf2.setFilename(fnB);
        sf2.setVideoPayload(loaded);
        REQUIRE(sf2.save() == Error::Ok);

        long la = 0, lb = 0;
        CHECK(filesIdentical(fnA, fnB, la, lb));
        CHECK(la > 0);

        std::remove(fnA);
        std::remove(fnB);
}

// ============================================================================
// Load produces correct compressed Image properties
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: loaded image has correct properties") {
        const char *fn = "/tmp/promeki_jpegxs_props.jxs";
        auto src = makePlanarYUV422_8(160, 120);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().width() == 160);
        CHECK(loaded->desc().height() == 120);
        CHECK(loaded->desc().pixelFormat().videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV8_422_Rec709);
        CHECK(loaded->plane(0).size() > 0);
        CHECK(loaded->plane(0).data() != nullptr);

        // The on-disk codestream should start with the SOC marker 0xFF10.
        const uint8_t *p = static_cast<const uint8_t *>(loaded->plane(0).data());
        CHECK(p[0] == 0xFF);
        CHECK(p[1] == 0x10);

        std::remove(fn);
}

// ============================================================================
// MediaConfig forwarding
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: save honours MediaConfig::JpegXsBpp") {
        const char *fnLo = "/tmp/promeki_jpegxs_bpp2.jxs";
        const char *fnHi = "/tmp/promeki_jpegxs_bpp8.jxs";
        auto src = makePlanarYUV422_8(256, 192);

        MediaConfig low;
        low.set(MediaConfig::JpegXsBpp, 2);
        ImageFile sfLo(ImageFile::JpegXS);
        sfLo.setFilename(fnLo);
        sfLo.setVideoPayload(src);
        REQUIRE(sfLo.save(low) == Error::Ok);

        MediaConfig high;
        high.set(MediaConfig::JpegXsBpp, 8);
        ImageFile sfHi(ImageFile::JpegXS);
        sfHi.setFilename(fnHi);
        sfHi.setVideoPayload(src);
        REQUIRE(sfHi.save(high) == Error::Ok);

        const long lo = fileSize(fnLo);
        const long hi = fileSize(fnHi);
        CHECK(lo > 0);
        CHECK(hi > lo);

        std::remove(fnLo);
        std::remove(fnHi);
}

TEST_CASE("ImageFileIO JpegXS: default bpp produces valid output") {
        const char *fn = "/tmp/promeki_jpegxs_default_bpp.jxs";
        auto src = makePlanarYUV422_8(320, 240);

        // Save with no MediaConfig — should use the codec's default
        // bpp of 3.
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        long sz = fileSize(fn);
        CHECK(sz > 0);

        // Verify the file is loadable.
        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        CHECK(lf.videoPayload().isValid());

        std::remove(fn);
}

// ============================================================================
// File content validation — SOC marker check
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: written file starts with SOC marker") {
        const char *fn = "/tmp/promeki_jpegxs_soc.jxs";
        auto src = makePlanarYUV422_8(64, 48);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        FILE *fp = std::fopen(fn, "rb");
        REQUIRE(fp);
        uint8_t header[4] = {};
        CHECK(std::fread(header, 1, 4, fp) == 4);
        std::fclose(fp);

        // JPEG XS SOC marker
        CHECK(header[0] == 0xFF);
        CHECK(header[1] == 0x10);

        std::remove(fn);
}

// ============================================================================
// Decode through UncompressedVideoPayload::convert dispatch — JPEG XS → RGBA8
// ============================================================================
//
// This is the path that SDLPlayerTask uses: the loaded
// CompressedVideoPayload must be decodable to RGBA8 via the payload
// conversion entry point which chains codec decode + CSC automatically.

TEST_CASE("ImageFileIO JpegXS: load then convert to RGBA8") {
        const char *fn = "/tmp/promeki_jpegxs_to_rgba.jxs";
        auto src = makePlanarYUV422_8(256, 192);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        REQUIRE(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        REQUIRE(loaded->isCompressed());

        // Two-hop path: JPEG XS → YUV8_422_Planar → RGBA8_sRGB
        auto rgba = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::RGBA8_sRGB));
        REQUIRE(rgba.isValid());
        CHECK(!rgba->isCompressed());
        CHECK(rgba->desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(rgba->desc().width() == 256);
        CHECK(rgba->desc().height() == 192);

        std::remove(fn);
}

// ============================================================================
// Multiple save/load cycles don't accumulate drift
// ============================================================================
//
// Compressed pass-through means load→save→load should produce
// bit-identical files at each generation.

TEST_CASE("ImageFileIO JpegXS: multi-generation pass-through") {
        const char *fn0 = "/tmp/promeki_jpegxs_gen0.jxs";
        const char *fn1 = "/tmp/promeki_jpegxs_gen1.jxs";
        const char *fn2 = "/tmp/promeki_jpegxs_gen2.jxs";

        auto src = makePlanarYUV422_8(192, 128);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 4);
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn0);
        sf.setVideoPayload(src);
        REQUIRE(sf.save(cfg) == Error::Ok);

        // Generation 1: load gen0, save gen1.
        ImageFile lf1(ImageFile::JpegXS);
        lf1.setFilename(fn0);
        REQUIRE(lf1.load() == Error::Ok);
        ImageFile sf1(ImageFile::JpegXS);
        sf1.setFilename(fn1);
        sf1.setVideoPayload(lf1.videoPayload());
        REQUIRE(sf1.save() == Error::Ok);

        // Generation 2: load gen1, save gen2.
        ImageFile lf2(ImageFile::JpegXS);
        lf2.setFilename(fn1);
        REQUIRE(lf2.load() == Error::Ok);
        ImageFile sf2(ImageFile::JpegXS);
        sf2.setFilename(fn2);
        sf2.setVideoPayload(lf2.videoPayload());
        REQUIRE(sf2.save() == Error::Ok);

        // All three files should be byte-identical.
        long la, lb;
        CHECK(filesIdentical(fn0, fn1, la, lb));
        CHECK(filesIdentical(fn1, fn2, la, lb));

        std::remove(fn0);
        std::remove(fn1);
        std::remove(fn2);
}

// ============================================================================
// Interleaved YUV input — exercises the CSC-then-encode save path
// ============================================================================
//
// The backend picks JPEG_XS_YUV8_422_Rec709 for interleaved 4:2:2
// inputs.  UncompressedVideoPayload::convert() inserts a deinterleave
// CSC before the JPEG XS encode.

TEST_CASE("ImageFileIO JpegXS: save from interleaved YUV8 422") {
        const char *fn = "/tmp/promeki_jpegxs_uyvy.jxs";
        auto src = UncompressedVideoPayload::allocate(
                ImageDesc(320, 240, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)));
        REQUIRE(src.isValid());

        // Fill with a simple pattern: alternating Y ramp, flat chroma.
        uint8_t *data = src.modify()->data()[0].data();
        const size_t stride = src->desc().pixelFormat().memLayout().lineStride(0, src->desc().width());
        for(int y = 0; y < 240; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < 320; x += 2) {
                        // UYVY: Cb Y0 Cr Y1
                        row[x * 2 + 0] = 128;   // Cb
                        row[x * 2 + 1] = static_cast<uint8_t>(16 + (x * 219) / 320); // Y0
                        row[x * 2 + 2] = 128;   // Cr
                        row[x * 2 + 3] = static_cast<uint8_t>(16 + ((x + 1) * 219) / 320); // Y1
                }
        }

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        // Verify it loads back as a valid JPEG XS image.
        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_YUV8_422_Rec709);
        CHECK(loaded->desc().width() == 320);
        CHECK(loaded->desc().height() == 240);

        std::remove(fn);
}

// ============================================================================
// Large frame — 1920x1080 (HD broadcast)
// ============================================================================
//
// Exercises the writeBulk DIO path on a payload large enough for the
// aligned interior to be non-trivial.

TEST_CASE("ImageFileIO JpegXS: 1920x1080 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_hd.jxs";
        auto src = makePlanarYUV422_8(1920, 1080);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 4);
        CHECK(sf.save(cfg) == Error::Ok);

        long sz = fileSize(fn);
        CHECK(sz > 0);
        // At 4 bpp, expect ~1920*1080*4/8 = 1,036,800 bytes.
        CHECK(sz > 500000);
        CHECK(sz < 2000000);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->desc().width() == 1920);
        CHECK(loaded->desc().height() == 1080);

        // Decode and spot-check dimensions.
        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::YUV8_422_Planar_Rec709));
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().width() == 1920);
        CHECK(decoded->desc().height() == 1080);

        const double mad = lumaMeanAbsDiff8(*src, *decoded);
        CHECK(mad < 2.0);

        std::remove(fn);
}

// ============================================================================
// Compressed size matches on-disk size
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: compressedSize matches file size") {
        const char *fn = "/tmp/promeki_jpegxs_csize.jxs";
        auto src = makePlanarYUV422_8(128, 96);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        long sz = fileSize(fn);
        REQUIRE(sz > 0);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(static_cast<long>(loaded->plane(0).size()) == sz);

        std::remove(fn);
}

// ============================================================================
// Error paths
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: load nonexistent file returns error") {
        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename("/tmp/promeki_jpegxs_nonexist.jxs");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO JpegXS: load garbage file returns error") {
        const char *fn = "/tmp/promeki_jpegxs_bad.jxs";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Definitely not a JPEG XS codestream.";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: load truncated codestream returns error") {
        const char *fnGood = "/tmp/promeki_jpegxs_trunc_full.jxs";
        const char *fnBad  = "/tmp/promeki_jpegxs_trunc_cut.jxs";

        // Write a valid codestream first.
        auto src = makePlanarYUV422_8(128, 96);
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fnGood);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        long sz = fileSize(fnGood);
        REQUIRE(sz > 64);

        // Write a truncated version — just the first 64 bytes.
        FILE *fi = std::fopen(fnGood, "rb");
        REQUIRE(fi);
        std::vector<uint8_t> buf(64);
        size_t rd = std::fread(buf.data(), 1, 64, fi);
        std::fclose(fi);
        REQUIRE(rd == 64);

        FILE *fo = std::fopen(fnBad, "wb");
        REQUIRE(fo);
        std::fwrite(buf.data(), 1, 64, fo);
        std::fclose(fo);

        // The load should fail because the SVT decoder can't parse
        // a truncated bitstream.
        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fnBad);
        CHECK(lf.load() != Error::Ok);

        std::remove(fnGood);
        std::remove(fnBad);
}

TEST_CASE("ImageFileIO JpegXS: load file with wrong SOC marker returns error") {
        const char *fn = "/tmp/promeki_jpegxs_wrong_soc.jxs";

        // Write something that starts with the JPEG SOI marker (FF D8)
        // instead of the JPEG XS SOC marker (FF 10).
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        uint8_t fake[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10 };
        std::fwrite(fake, 1, sizeof(fake), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: load 2-byte file returns error") {
        const char *fn = "/tmp/promeki_jpegxs_tiny.jxs";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        uint8_t tiny[] = { 0xFF, 0x10 };
        std::fwrite(tiny, 1, sizeof(tiny), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: save empty image returns error") {
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename("/tmp/promeki_jpegxs_empty.jxs");
        CHECK(sf.save() != Error::Ok);
}

TEST_CASE("ImageFileIO JpegXS: save rejects non-JPEG-XS compressed input") {
        // Create a compressed JPEG image and try to save it as JPEG XS.
        // The backend should reject it with NotSupported.
        auto src = UncompressedVideoPayload::allocate(
                ImageDesc(320, 240, PixelFormat(PixelFormat::YUV8_422_Planar_Rec709)));
        REQUIRE(src.isValid());
        // Fill luma with ramp for a non-trivial encode.
        uint8_t *luma = src.modify()->data()[0].data();
        const size_t stride = src->desc().pixelFormat().memLayout().lineStride(0, src->desc().width());
        for(int y = 0; y < 240; y++) {
                uint8_t *row = luma + y * stride;
                for(int x = 0; x < 320; x++) {
                        row[x] = static_cast<uint8_t>(16 + (x * 219) / 320);
                }
        }

        // Encode to JPEG (not JPEG XS).
        auto jpeg = promeki::tests::encodePayloadToCompressed(
                src, PixelFormat(PixelFormat::JPEG_YUV8_422_Rec709));
        if(!jpeg.isValid()) return;  // skip if JPEG codec not available
        REQUIRE(jpeg->desc().pixelFormat().videoCodec().id() == VideoCodec::JPEG);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename("/tmp/promeki_jpegxs_wrong_codec.jxs");
        sf.setVideoPayload(jpeg);
        CHECK(sf.save() == Error::NotSupported);
}

// ============================================================================
// RGB8 round-trip via packed RGB encode path
// ============================================================================
//
// The SVT encoder natively accepts packed RGB — the ImageFileIO save
// path routes RGB8_sRGB to JPEG_XS_RGB8_sRGB, which uses
// COLOUR_FORMAT_PACKED_YUV444_OR_RGB.  The decode chain goes:
// JPEG_XS_RGB8 → RGB8_Planar (codec) → RGB8 (CSC fast path).

static UncompressedVideoPayload::Ptr makeRGB8(int w, int h) {
        auto img = UncompressedVideoPayload::allocate(
                ImageDesc(w, h, PixelFormat(PixelFormat::RGB8_sRGB)));
        REQUIRE(img.isValid());
        uint8_t *data = img.modify()->data()[0].data();
        const size_t stride = img->desc().pixelFormat().memLayout().lineStride(0, w);
        for(int y = 0; y < h; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < w; x++) {
                        row[x * 3 + 0] = static_cast<uint8_t>(x * 255 / w);
                        row[x * 3 + 1] = static_cast<uint8_t>(y * 255 / h);
                        row[x * 3 + 2] = 128;
                }
        }
        return img;
}

static double rgb8MeanAbsDiff(const UncompressedVideoPayload &a,
                              const UncompressedVideoPayload &b) {
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        REQUIRE(a.desc().width() == b.desc().width());
        REQUIRE(a.desc().height() == b.desc().height());
        const int w = static_cast<int>(a.desc().width());
        const int h = static_cast<int>(a.desc().height());
        const uint8_t *pa = a.plane(0).data();
        const uint8_t *pb = b.plane(0).data();
        const size_t sa = a.desc().pixelFormat().memLayout().lineStride(0, w);
        const size_t sb = b.desc().pixelFormat().memLayout().lineStride(0, w);
        double sum = 0.0;
        for(int y = 0; y < h; y++) {
                const uint8_t *ra = pa + y * sa;
                const uint8_t *rb = pb + y * sb;
                for(int x = 0; x < w * 3; x++) {
                        sum += std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x]));
                }
        }
        return sum / static_cast<double>(w * h * 3);
}

TEST_CASE("ImageFileIO JpegXS: RGB8 round-trip") {
        const char *fn = "/tmp/promeki_jpegxs_rgb8.jxs";
        auto src = makeRGB8(256, 192);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_RGB8_sRGB);
        CHECK(loaded->desc().width() == 256);
        CHECK(loaded->desc().height() == 192);

        // Decode through the payload conversion chain:
        // JPEG_XS_RGB8 → RGB8_Planar (codec) → RGB8 (CSC fast path)
        auto decoded = promeki::tests::decodeCompressedPayload(sharedPointerCast<CompressedVideoPayload>(loaded), PixelFormat(PixelFormat::RGB8_sRGB));
        REQUIRE(decoded.isValid());
        CHECK(!decoded->isCompressed());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::RGB8_sRGB);
        CHECK(decoded->desc().width() == 256);
        CHECK(decoded->desc().height() == 192);

        const double mad = rgb8MeanAbsDiff(*src, *decoded);
        CHECK(mad < 2.0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: RGB8 pass-through preserves bytes") {
        const char *fnA = "/tmp/promeki_jpegxs_rgb_pt_a.jxs";
        const char *fnB = "/tmp/promeki_jpegxs_rgb_pt_b.jxs";

        auto src = makeRGB8(192, 128);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 4);
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fnA);
        sf.setVideoPayload(src);
        REQUIRE(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fnA);
        REQUIRE(lf.load() == Error::Ok);
        REQUIRE(lf.videoPayload()->desc().pixelFormat().id() == PixelFormat::JPEG_XS_RGB8_sRGB);

        ImageFile sf2(ImageFile::JpegXS);
        sf2.setFilename(fnB);
        sf2.setVideoPayload(lf.videoPayload());
        REQUIRE(sf2.save() == Error::Ok);

        long la = 0, lb = 0;
        CHECK(filesIdentical(fnA, fnB, la, lb));
        CHECK(la > 0);

        std::remove(fnA);
        std::remove(fnB);
}

TEST_CASE("ImageFileIO JpegXS: RGBA8 input encodes as RGB") {
        const char *fn = "/tmp/promeki_jpegxs_rgba_to_rgb.jxs";

        // RGBA8 → should route to JPEG_XS_RGB8_sRGB since it's sRGB family.
        auto src = UncompressedVideoPayload::allocate(
                ImageDesc(256, 192, PixelFormat(PixelFormat::RGBA8_sRGB)));
        REQUIRE(src.isValid());
        uint8_t *data = src.modify()->data()[0].data();
        const size_t stride = src->desc().pixelFormat().memLayout().lineStride(0, src->desc().width());
        for(int y = 0; y < 192; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < 256; x++) {
                        row[x * 4 + 0] = static_cast<uint8_t>(x);
                        row[x * 4 + 1] = static_cast<uint8_t>(y);
                        row[x * 4 + 2] = 128;
                        row[x * 4 + 3] = 255;
                }
        }

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        CHECK(sf.save(cfg) == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().id() == PixelFormat::JPEG_XS_RGB8_sRGB);

        std::remove(fn);
}

// ============================================================================
// CompressedVideoPayload emission — ImageFile::load emits a
// CompressedVideoPayload so a downstream MediaIOTask_VideoDecoder
// stage can submit it directly.
// ============================================================================

TEST_CASE("ImageFileIO JpegXS: load emits a CompressedVideoPayload") {
        const char *fn = "/tmp/promeki_jpegxs_packet.jxs";
        auto src = makePlanarYUV422_8(128, 96);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);

        const Frame &frame = lf.frame();
        auto vids = frame.videoPayloads();
        REQUIRE(vids.size() == 1);
        const auto *cvp = vids[0]->as<CompressedVideoPayload>();
        REQUIRE(cvp != nullptr);
        CHECK(cvp->desc().pixelFormat().isCompressed());
        CHECK(cvp->plane(0).size() > 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JpegXS: successive loads don't stack payloads") {
        const char *fn = "/tmp/promeki_jpegxs_packet_reload.jxs";
        auto src = makePlanarYUV422_8(64, 48);

        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);
        REQUIRE(lf.load() == Error::Ok);

        auto vids = lf.frame().videoPayloads();
        REQUIRE(vids.size() == 1);
        CHECK(vids[0]->as<CompressedVideoPayload>() != nullptr);

        std::remove(fn);
}
