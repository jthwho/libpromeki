/**
 * @file      imagefileio_jpeg.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include "codectesthelpers.h"
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediapacket.h>
#include <promeki/frame.h>

using namespace promeki;

// ============================================================================
// Handler registration
// ============================================================================

TEST_CASE("ImageFileIO JPEG: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::JPEG);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "JPEG");
}

// ============================================================================
// Helpers — build a deterministic test image
// ============================================================================

static Image makeGradientRGB8(size_t w, size_t h) {
        Image img(w, h, PixelDesc(PixelDesc::RGB8_sRGB));
        REQUIRE(img.isValid());
        uint8_t *data = static_cast<uint8_t *>(img.data(0));
        const size_t stride = img.lineStride(0);
        for(size_t y = 0; y < h; ++y) {
                uint8_t *row = data + y * stride;
                for(size_t x = 0; x < w; ++x) {
                        row[3 * x + 0] = static_cast<uint8_t>(x * 255 / (w - 1));
                        row[3 * x + 1] = static_cast<uint8_t>(y * 255 / (h - 1));
                        row[3 * x + 2] = 128;
                }
        }
        return img;
}

// Average absolute difference per pixel between two RGB images of equal
// size.  JPEG is lossy, so we use this instead of byte-equality.
static double rgb8MeanAbsDiff(const Image &a, const Image &b) {
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        REQUIRE(a.width() == b.width());
        REQUIRE(a.height() == b.height());
        const size_t w = a.width();
        const size_t h = a.height();
        const uint8_t *pa = static_cast<const uint8_t *>(a.data(0));
        const uint8_t *pb = static_cast<const uint8_t *>(b.data(0));
        const size_t sa = a.lineStride(0);
        const size_t sb = b.lineStride(0);
        double sum = 0.0;
        for(size_t y = 0; y < h; ++y) {
                const uint8_t *ra = pa + y * sa;
                const uint8_t *rb = pb + y * sb;
                for(size_t x = 0; x < w * 3; ++x) {
                        sum += std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x]));
                }
        }
        return sum / static_cast<double>(w * h * 3);
}

// ============================================================================
// Round-trip: save an RGB image, load it back, verify pixels are close
// ============================================================================

TEST_CASE("ImageFileIO JPEG: RGB8 round-trip") {
        const char *fn = "/tmp/promeki_jpeg_rgb8.jpg";
        Image src = makeGradientRGB8(64, 48);

        ImageFile sf(ImageFile::JPEG);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        Image loaded = lf.image();
        REQUIRE(loaded.isValid());
        CHECK(loaded.isCompressed());
        CHECK(loaded.pixelDesc().videoCodec().id() == VideoCodec::JPEG);
        CHECK(loaded.width() == 64);
        CHECK(loaded.height() == 48);

        // Decode through a one-shot JpegVideoDecoder session
        // (Image::convert is CSC-only after task 36) and compare
        // against the original gradient.
        Image decoded = promeki::tests::decodeCompressedToImage(
                loaded, PixelDesc(PixelDesc::RGB8_sRGB));
        REQUIRE(decoded.isValid());
        REQUIRE(!decoded.isCompressed());
        CHECK(decoded.width() == 64);
        CHECK(decoded.height() == 48);

        const double mad = rgb8MeanAbsDiff(src, decoded);
        // Gradient + default quality (85) + 4:2:2 subsampling: the mean
        // absolute difference should stay comfortably below a few LSBs.
        CHECK(mad < 5.0);

        std::remove(fn);
}

// ============================================================================
// Pass-through: loading JPEG returns a compressed Image; saving it back
// writes the original bytes verbatim.
// ============================================================================

TEST_CASE("ImageFileIO JPEG: pass-through preserves bytes") {
        const char *fnA = "/tmp/promeki_jpeg_passthrough_a.jpg";
        const char *fnB = "/tmp/promeki_jpeg_passthrough_b.jpg";

        Image src = makeGradientRGB8(96, 64);
        ImageFile sf(ImageFile::JPEG);
        sf.setFilename(fnA);
        sf.setImage(src);
        REQUIRE(sf.save() == Error::Ok);

        // Load the compressed image and save it back under a new name.
        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fnA);
        REQUIRE(lf.load() == Error::Ok);
        Image loaded = lf.image();
        REQUIRE(loaded.isValid());
        REQUIRE(loaded.isCompressed());

        ImageFile sf2(ImageFile::JPEG);
        sf2.setFilename(fnB);
        sf2.setImage(loaded);
        REQUIRE(sf2.save() == Error::Ok);

        // Compare the two files byte-for-byte.
        FILE *fa = std::fopen(fnA, "rb");
        FILE *fb = std::fopen(fnB, "rb");
        REQUIRE(fa);
        REQUIRE(fb);
        std::fseek(fa, 0, SEEK_END);
        std::fseek(fb, 0, SEEK_END);
        const long la = std::ftell(fa);
        const long lb = std::ftell(fb);
        std::fseek(fa, 0, SEEK_SET);
        std::fseek(fb, 0, SEEK_SET);
        CHECK(la == lb);
        CHECK(la > 0);
        if(la == lb && la > 0) {
                std::vector<uint8_t> ba(la), bb(lb);
                CHECK(std::fread(ba.data(), 1, la, fa) == static_cast<size_t>(la));
                CHECK(std::fread(bb.data(), 1, lb, fb) == static_cast<size_t>(lb));
                CHECK(std::memcmp(ba.data(), bb.data(), la) == 0);
        }
        std::fclose(fa);
        std::fclose(fb);

        std::remove(fnA);
        std::remove(fnB);
}

// ============================================================================
// Image::convert() encode path — TPG-style use case
// ============================================================================
//
// This is the scenario the JPEG backend unblocks: a source that produces
// uncompressed frames are encoded through a one-shot JpegVideoEncoder
// session and decoded back through a JpegVideoDecoder session — the
// helpers in codectesthelpers.h hide the boilerplate.

TEST_CASE("VideoCodec JPEG: RGB8 -> JPEG round-trip via VideoEncoder/VideoDecoder") {
        Image src = makeGradientRGB8(80, 60);

        Image jpeg = promeki::tests::encodeImageToCompressed(
                src, PixelDesc(PixelDesc::JPEG_YUV8_422_Rec709));
        REQUIRE(jpeg.isValid());
        REQUIRE(jpeg.isCompressed());
        CHECK(jpeg.pixelDesc().videoCodec().id() == VideoCodec::JPEG);
        CHECK(jpeg.compressedSize() > 0);

        Image decoded = promeki::tests::decodeCompressedToImage(
                jpeg, PixelDesc(PixelDesc::RGB8_sRGB));
        REQUIRE(decoded.isValid());
        REQUIRE(!decoded.isCompressed());
        CHECK(decoded.width() == src.width());
        CHECK(decoded.height() == src.height());

        // A horizontal red gradient run through RGB → YUV 4:2:2
        // (averages chroma over pairs of samples) → JPEG quantization
        // → YUV → RGB picks up ~10 LSB of chroma smear before the
        // codec quantization even counts; bound below 20 stays noise-free.
        const double mad = rgb8MeanAbsDiff(src, decoded);
        CHECK(mad < 20.0);
}

TEST_CASE("VideoCodec JPEG: honours MediaConfig::JpegQuality") {
        Image src = makeGradientRGB8(128, 96);

        MediaConfig low;
        low.set(MediaConfig::JpegQuality, 10);
        Image lowQ = promeki::tests::encodeImageToCompressed(
                src, PixelDesc(PixelDesc::JPEG_RGB8_sRGB), low);
        REQUIRE(lowQ.isValid());
        REQUIRE(lowQ.isCompressed());

        MediaConfig high;
        high.set(MediaConfig::JpegQuality, 95);
        Image highQ = promeki::tests::encodeImageToCompressed(
                src, PixelDesc(PixelDesc::JPEG_RGB8_sRGB), high);
        REQUIRE(highQ.isValid());
        REQUIRE(highQ.isCompressed());

        // Quality 95 should produce a visibly larger file than quality
        // 10 for a non-trivial gradient.
        CHECK(highQ.compressedSize() > lowQ.compressedSize());
}

// ============================================================================
// MediaConfig forwarded through ImageFile / ImageFileIO_JPEG to the codec
// ============================================================================

TEST_CASE("ImageFileIO JPEG: save honours MediaConfig::JpegQuality") {
        const char *fnLo = "/tmp/promeki_jpeg_q10.jpg";
        const char *fnHi = "/tmp/promeki_jpeg_q95.jpg";
        Image src = makeGradientRGB8(128, 96);

        MediaConfig low;
        low.set(MediaConfig::JpegQuality, 10);
        ImageFile sfLo(ImageFile::JPEG);
        sfLo.setFilename(fnLo);
        sfLo.setImage(src);
        REQUIRE(sfLo.save(low) == Error::Ok);

        MediaConfig high;
        high.set(MediaConfig::JpegQuality, 95);
        ImageFile sfHi(ImageFile::JPEG);
        sfHi.setFilename(fnHi);
        sfHi.setImage(src);
        REQUIRE(sfHi.save(high) == Error::Ok);

        // Quality propagates through ImageFile → ImageFileIO_JPEG →
        // Image::convert → JpegImageCodec::configure: the on-disk
        // payloads must differ in size in the same direction the codec
        // would produce on its own.
        FILE *fa = std::fopen(fnLo, "rb");
        FILE *fb = std::fopen(fnHi, "rb");
        REQUIRE(fa);
        REQUIRE(fb);
        std::fseek(fa, 0, SEEK_END);
        std::fseek(fb, 0, SEEK_END);
        const long la = std::ftell(fa);
        const long lb = std::ftell(fb);
        std::fclose(fa);
        std::fclose(fb);
        CHECK(la > 0);
        CHECK(lb > la);

        std::remove(fnLo);
        std::remove(fnHi);
}

// ============================================================================
// Error paths
// ============================================================================

TEST_CASE("ImageFileIO JPEG: load nonexistent file returns error") {
        ImageFile lf(ImageFile::JPEG);
        lf.setFilename("/tmp/promeki_jpeg_nonexist.jpg");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO JPEG: load garbage file returns error") {
        const char *fn = "/tmp/promeki_jpeg_bad.jpg";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Definitely not a JPEG file, just some text.";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JPEG: save empty image returns error") {
        ImageFile sf(ImageFile::JPEG);
        sf.setFilename("/tmp/promeki_jpeg_empty.jpg");
        // No image set — frame is empty.
        CHECK(sf.save() != Error::Ok);
}

// ============================================================================
// MediaPacket attachment — a compressed-image load attaches a
// MediaPacket to the Image so a downstream MediaIOTask_VideoDecoder
// stage can read Image::packet() without having to re-wrap plane(0).
// ============================================================================

TEST_CASE("ImageFileIO JPEG: load attaches a MediaPacket to the Image") {
        const char *fn = "/tmp/promeki_jpeg_packet.jpg";
        Image src = makeGradientRGB8(64, 48);

        ImageFile sf(ImageFile::JPEG);
        sf.setFilename(fn);
        sf.setImage(src);
        REQUIRE(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);

        const Frame &frame = lf.frame();
        REQUIRE(frame.imageList().size() == 1);

        const Image &img = *frame.imageList()[0];
        REQUIRE(img.isCompressed());
        REQUIRE(img.packet().isValid());
        const MediaPacket &pkt = *img.packet();
        CHECK(pkt.isValid());
        CHECK(pkt.pixelDesc() == img.pixelDesc());
        CHECK(pkt.size() == img.compressedSize());
        CHECK(pkt.isKeyframe());
        CHECK(pkt.buffer().ptr() == img.plane(0).ptr());

        std::remove(fn);
}
