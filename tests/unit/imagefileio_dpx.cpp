/**
 * @file      imagefileio_dpx.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/timecode.h>
#include <promeki/metadata.h>
#include <promeki/audio.h>

using namespace promeki;

// ============================================================================
// Registration
// ============================================================================

TEST_CASE("ImageFileIO DPX: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::DPX);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "DPX");
}

// ============================================================================
// Helper: fill an image with a deterministic pattern
// ============================================================================

static void fillPattern(Image &img) {
        for(size_t p = 0; p < img.pixelFormat().planeCount(); ++p) {
                uint8_t *data = static_cast<uint8_t *>(img.data(p));
                size_t bytes = img.pixelFormat().memLayout().planeSize(p, img.width(), img.height());
                for(size_t i = 0; i < bytes; ++i) {
                        data[i] = static_cast<uint8_t>((i * 7 + p * 37) & 0xFF);
                }
        }
}

static bool verifyPattern(const Image &src, const Image &dst) {
        for(size_t p = 0; p < src.pixelFormat().planeCount(); ++p) {
                size_t bytes = src.pixelFormat().memLayout().planeSize(p, src.width(), src.height());
                if(std::memcmp(src.data(p), dst.data(p), bytes) != 0) return false;
        }
        return true;
}

// ============================================================================
// Helper: round-trip save + load
// ============================================================================

static void dpxRoundTrip(const char *fn, size_t w, size_t h, PixelFormat::ID pdId) {
        Image src(w, h, PixelFormat(pdId));
        REQUIRE(src.isValid());
        fillPattern(src);

        // Save
        ImageFile sf(ImageFile::DPX);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        // Load
        ImageFile lf(ImageFile::DPX);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.width() == w);
        CHECK(dst.height() == h);
        CHECK(dst.pixelFormat().id() == pdId);
        CHECK(verifyPattern(src, dst));

        std::remove(fn);
}

// ============================================================================
// Pixel format round-trips
// ============================================================================

TEST_CASE("ImageFileIO DPX: RGBA8 round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_rgba8.dpx", 64, 48, PixelFormat::RGBA8_sRGB);
}

TEST_CASE("ImageFileIO DPX: ARGB8 save and reload as RGBA8") {
        // DPX descriptor 51 (RGBA) is used for both RGBA8 and ARGB8.
        // Saving ARGB8 reorders to RGBA; loading always yields RGBA8.
        const char *fn = "/tmp/promeki_dpx_argb8.dpx";
        const size_t w = 64, h = 48;
        Image src(w, h, PixelFormat(PixelFormat::ARGB8_sRGB));
        REQUIRE(src.isValid());
        // Fill with known pattern: A=0x10, R=0x20, G=0x30, B=0x40 per pixel
        uint8_t *sp = static_cast<uint8_t *>(src.data());
        for(size_t i = 0; i < w * h; ++i) {
                sp[i * 4 + 0] = 0x10; // A
                sp[i * 4 + 1] = 0x20; // R
                sp[i * 4 + 2] = 0x30; // G
                sp[i * 4 + 3] = 0x40; // B
        }

        ImageFile sf(ImageFile::DPX);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::DPX);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        // Verify channels were reordered: ARGB → RGBA
        const uint8_t *dp = static_cast<const uint8_t *>(dst.data());
        CHECK(dp[0] == 0x20); // R
        CHECK(dp[1] == 0x30); // G
        CHECK(dp[2] == 0x40); // B
        CHECK(dp[3] == 0x10); // A

        std::remove(fn);
}

TEST_CASE("ImageFileIO DPX: YUV8 4:4:4 round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_yuv8.dpx", 64, 48, PixelFormat::YUV8_Rec709);
}

TEST_CASE("ImageFileIO DPX: RGB10 DPX LE round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_rgb10le.dpx", 64, 48, PixelFormat::RGB10_DPX_LE_sRGB);
}

TEST_CASE("ImageFileIO DPX: RGB10 DPX BE round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_rgb10be.dpx", 64, 48, PixelFormat::RGB10_DPX_sRGB);
}

TEST_CASE("ImageFileIO DPX: YUV10 DPX method A round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_yuv10a.dpx", 64, 48, PixelFormat::YUV10_DPX_Rec709);
}

TEST_CASE("ImageFileIO DPX: YUV10 DPX method B round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_yuv10b.dpx", 64, 48, PixelFormat::YUV10_DPX_B_Rec709);
}

TEST_CASE("ImageFileIO DPX: RGB16 BE round-trip") {
        dpxRoundTrip("/tmp/promeki_dpx_rgb16.dpx", 64, 48, PixelFormat::RGB16_BE_sRGB);
}

TEST_CASE("ImageFileIO DPX: non-aligned image size round-trip") {
        // 50x50 RGBA8 = 10000 bytes, not a multiple of 4096.
        // Exercises the DIO write padding path where imagePadded > imageBytes.
        dpxRoundTrip("/tmp/promeki_dpx_unaligned.dpx", 50, 50, PixelFormat::RGBA8_sRGB);
}

// ============================================================================
// Metadata round-trip
// ============================================================================

TEST_CASE("ImageFileIO DPX: metadata round-trip") {
        const char *fn = "/tmp/promeki_dpx_meta.dpx";
        Image src(64, 48, PixelFormat::RGBA8_sRGB);
        REQUIRE(src.isValid());
        src.fill(0x80);

        ImageFile sf(ImageFile::DPX);
        sf.setFilename(fn);
        sf.setImage(src);

        Metadata &meta = sf.metadata();
        meta.set(Metadata::Copyright, String("Test Copyright"));
        meta.set(Metadata::Project, String("Test Project"));
        meta.set(Metadata::Software, String("promeki test"));
        meta.set(Metadata::Gamma, 2.2);
        meta.set(Metadata::FrameRate, 24.0);
        meta.set(Metadata::FilmFormat, String("Academy"));
        meta.set(Metadata::FilmSlate, String("Scene 1 Take 3"));
        meta.set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::DPX);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        const Metadata &loaded = lf.metadata();
        CHECK(loaded.get(Metadata::Copyright).get<String>() == "Test Copyright");
        CHECK(loaded.get(Metadata::Project).get<String>() == "Test Project");
        CHECK(loaded.get(Metadata::Software).get<String>() == "promeki test");

        // Gamma round-trip (float precision)
        double gamma = loaded.get(Metadata::Gamma).get<double>();
        CHECK(gamma > 2.1);
        CHECK(gamma < 2.3);

        // Frame rate round-trip
        double fps = loaded.get(Metadata::FrameRate).get<double>();
        CHECK(fps > 23.9);
        CHECK(fps < 24.1);

        // Film info
        CHECK(loaded.get(Metadata::FilmFormat).get<String>() == "Academy");
        CHECK(loaded.get(Metadata::FilmSlate).get<String>() == "Scene 1 Take 3");

        // Timecode round-trip
        Timecode tc = loaded.get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.isValid());
        CHECK(tc.hour() == 1);
        CHECK(tc.min() == 0);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);

        std::remove(fn);
}

// ============================================================================
// Embedded audio round-trip
// ============================================================================

TEST_CASE("ImageFileIO DPX: embedded audio round-trip") {
        const char *fn = "/tmp/promeki_dpx_audio.dpx";

        // Create image
        Image img(64, 48, PixelFormat::RGBA8_sRGB);
        REQUIRE(img.isValid());
        img.fill(0x42);

        // Create audio: 480 samples, stereo, 16-bit LE, 48kHz
        AudioDesc adesc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        Audio audio(adesc, 480);
        REQUIRE(audio.isValid());
        int16_t *samps = static_cast<int16_t *>(audio.buffer()->data());
        for(size_t i = 0; i < 480 * 2; ++i) {
                samps[i] = static_cast<int16_t>(i * 13);
        }

        // Build frame with image + audio
        Frame frame;
        frame.imageList().pushToBack(Image::Ptr::create(img));
        frame.audioList().pushToBack(Audio::Ptr::create(audio));

        ImageFile sf(ImageFile::DPX);
        sf.setFilename(fn);
        sf.setFrame(frame);
        CHECK(sf.save() == Error::Ok);

        // Load
        ImageFile lf(ImageFile::DPX);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        // Verify image
        Image dstImg = lf.image();
        REQUIRE(dstImg.isValid());
        CHECK(dstImg.width() == 64);
        CHECK(dstImg.height() == 48);

        // Verify audio
        REQUIRE(!lf.frame().audioList().isEmpty());
        const Audio &dstAudio = *lf.frame().audioList()[0];
        REQUIRE(dstAudio.isValid());
        CHECK(dstAudio.samples() == 480);
        CHECK(dstAudio.desc().channels() == 2);
        CHECK(dstAudio.desc().format().id() == AudioFormat::PCMI_S16LE);

        const int16_t *dstSamps = static_cast<const int16_t *>(dstAudio.buffer()->data());
        size_t audioBytes = adesc.bufferSize(480);
        CHECK(std::memcmp(samps, dstSamps, audioBytes) == 0);

        std::remove(fn);
}

// ============================================================================
// Error handling
// ============================================================================

TEST_CASE("ImageFileIO DPX: load non-DPX file returns error") {
        const char *fn = "/tmp/promeki_dpx_bad.dpx";

        // Write garbage
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "This is not a DPX file at all.";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::DPX);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO DPX: load nonexistent file returns error") {
        ImageFile lf(ImageFile::DPX);
        lf.setFilename("/tmp/promeki_dpx_nonexistent_file_12345.dpx");
        CHECK(lf.load() != Error::Ok);
}
