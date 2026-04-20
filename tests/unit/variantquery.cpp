/**
 * @file      variantquery.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/variantquery.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/metadata.h>
#include <promeki/mediaconfig.h>
#include <promeki/pixeldesc.h>
#include <promeki/enums.h>

using namespace promeki;

namespace {

Frame::Ptr richFrame() {
        Frame::Ptr f = Frame::Ptr::create();
        Frame *raw = f.modify();
        raw->metadata().set(Metadata::Title, String("clip"));
        raw->metadata().set(Metadata::Timecode,
                            Timecode(Timecode::NDF24, 1, 0, 0, 0));
        raw->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_24));
        raw->metadata().set(Metadata::FrameNumber, int64_t(42));
        raw->metadata().set(Metadata::Comment,
                            String("dropped 5 frames during capture"));

        ImageDesc idesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        idesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(idesc);
        img.modify()->metadata().set(Metadata::FrameNumber, int64_t(42));
        raw->imageList().pushToBack(img);

        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio::Ptr aud = Audio::Ptr::create(adesc, 1024);
        aud.modify()->metadata().set(Metadata::Album, String("LiveSet"));
        raw->audioList().pushToBack(aud);

        return f;
}

bool matchFrame(const String &expr, const Frame &f) {
        auto [q, err] = VariantQuery<Frame>::parse(expr);
        REQUIRE_MESSAGE(err.isOk(), expr.cstr(), ": ", q.errorDetail().cstr());
        return q.match(f);
}

bool parseFails(const String &expr) {
        auto [q, err] = VariantQuery<Frame>::parse(expr);
        return err.isError();
}

}  // namespace

TEST_CASE("VariantQuery<Frame>: parse rejects empty/invalid input") {
        CHECK(parseFails(""));
        CHECK(parseFails("(("));
        CHECK(parseFails("Meta.Title =="));
        CHECK(parseFails("&&"));
        CHECK(parseFails("Meta.FrameNumber > "));
        CHECK(parseFails("has("));
        CHECK(parseFails("has()"));
        CHECK(parseFails("Meta.Comment ~ \"not-a-regex\""));
}

TEST_CASE("VariantQuery<Frame>: equality on metadata keys") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("Meta.Title == \"clip\"", *f));
        CHECK_FALSE(matchFrame("Meta.Title == \"other\"", *f));
        CHECK(matchFrame("Meta.Title != \"other\"", *f));
        CHECK(matchFrame("Meta.FrameNumber == 42", *f));
        CHECK_FALSE(matchFrame("Meta.FrameNumber == 41", *f));
}

TEST_CASE("VariantQuery<Frame>: numeric ordering") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("Meta.FrameNumber > 40", *f));
        CHECK(matchFrame("Meta.FrameNumber >= 42", *f));
        CHECK_FALSE(matchFrame("Meta.FrameNumber > 42", *f));
        CHECK(matchFrame("Meta.FrameNumber < 43", *f));
        CHECK(matchFrame("Meta.FrameNumber <= 42", *f));
}

TEST_CASE("VariantQuery<Frame>: Timecode comparison with string literal coerces via spec") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("Meta.Timecode == \"01:00:00:00\"", *f));
        CHECK(matchFrame("Meta.Timecode >= \"00:59:59:00\"", *f));
        CHECK(matchFrame("Meta.Timecode <  \"01:00:00:01\"", *f));
        CHECK_FALSE(matchFrame("Meta.Timecode >  \"01:00:00:00\"", *f));
}

TEST_CASE("VariantQuery<Frame>: scalar keys and subscripted keys") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("ImageCount == 1", *f));
        CHECK(matchFrame("AudioCount == 1", *f));
        CHECK(matchFrame("Image[0].Width == 1920", *f));
        CHECK(matchFrame("Image[0].Height == 1080", *f));
        CHECK(matchFrame("Image[0].PixelDesc == \"RGBA8_sRGB\"", *f));
        CHECK(matchFrame("Audio[0].Channels == 2", *f));
        CHECK(matchFrame("Audio[0].Meta.Album == \"LiveSet\"", *f));
}

TEST_CASE("VariantQuery<Frame>: has() returns true for present, false for missing") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("has(Meta.Title)", *f));
        CHECK(matchFrame("has(ImageCount)", *f));
        CHECK(matchFrame("has(Image[0].Width)", *f));
        CHECK(matchFrame("has(Audio[0].Meta.Album)", *f));
        CHECK_FALSE(matchFrame("has(Meta.License)", *f));
        CHECK_FALSE(matchFrame("has(Image[9].Width)", *f));
        CHECK_FALSE(matchFrame("has(Audio[9].Meta.Album)", *f));
}

TEST_CASE("VariantQuery<Frame>: regex and substring") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("Meta.Comment ~ /drop.*[0-9]+/", *f));
        CHECK_FALSE(matchFrame("Meta.Comment ~ /entirely-missing-pattern/", *f));
        CHECK(matchFrame("Meta.Comment ~~ \"dropped\"", *f));
        CHECK(matchFrame("Meta.Title ~~ \"cli\"", *f));
        CHECK_FALSE(matchFrame("Meta.Title ~~ \"xyz\"", *f));
}

TEST_CASE("VariantQuery<Frame>: logical operators and precedence") {
        Frame::Ptr f = richFrame();
        CHECK(matchFrame("Meta.Title == \"clip\" && Meta.FrameNumber == 42", *f));
        CHECK_FALSE(matchFrame("Meta.Title == \"clip\" && Meta.FrameNumber == 99", *f));
        CHECK(matchFrame("Meta.Title == \"clip\" || Meta.FrameNumber == 99", *f));
        CHECK(matchFrame("!has(Meta.License)", *f));
        CHECK(matchFrame("!(Meta.Title == \"other\")", *f));

        // && binds tighter than ||: true || (false && false) == true
        CHECK(matchFrame("Meta.Title == \"clip\" || Meta.Title == \"x\" && Meta.FrameNumber == 0", *f));
        // Parens flip the meaning: (true || true) && false == false
        CHECK_FALSE(matchFrame("(Meta.Title == \"clip\" || Meta.Title == \"x\") && Meta.FrameNumber == 0", *f));
}

TEST_CASE("VariantQuery<Frame>: missing keys evaluate to false (except !=)") {
        Frame::Ptr f = richFrame();
        // Nonexistent key in relation: always false, even if RHS "matches".
        CHECK_FALSE(matchFrame("Meta.License == \"MIT\"", *f));
        CHECK_FALSE(matchFrame("Meta.License <  \"zzz\"", *f));
        // != of present vs missing is true (existence differs).
        CHECK(matchFrame("Meta.License != \"\"", *f));
}

TEST_CASE("VariantQuery<Frame>: bare key is equivalent to has()") {
        Frame::Ptr f = richFrame();
        auto [q1, e1] = VariantQuery<Frame>::parse("Meta.Title");
        REQUIRE(e1.isOk());
        CHECK(q1.match(*f));

        auto [q2, e2] = VariantQuery<Frame>::parse("Meta.License");
        REQUIRE(e2.isOk());
        CHECK_FALSE(q2.match(*f));
}

TEST_CASE("VariantQuery<Frame>: reusable across many frames") {
        auto [q, err] = VariantQuery<Frame>::parse("Meta.FrameNumber > 100");
        REQUIRE(err.isOk());

        for(int i = 0; i < 5; ++i) {
                Frame::Ptr f = Frame::Ptr::create();
                f.modify()->metadata().set(Metadata::FrameNumber,
                                           int64_t(50 + i * 50));
                bool expected = (50 + i * 50) > 100;
                CHECK(q.match(*f) == expected);
        }
}

TEST_CASE("VariantQuery<Frame>: parse error carries a diagnostic") {
        auto [q, err] = VariantQuery<Frame>::parse("Meta.Title &&");
        CHECK(err.isError());
        CHECK_FALSE(q.isValid());
        CHECK_FALSE(q.errorDetail().isEmpty());
}

TEST_CASE("VariantQuery<Frame>: source() round-trips the input") {
        String src = "FrameNumber > 100 && has(Timecode)";
        auto [q, err] = VariantQuery<Frame>::parse(src);
        REQUIRE(err.isOk());
        CHECK(q.source() == src);
}

// ============================================================
// VariantQuery<Image>
// ============================================================

namespace {

Image::Ptr richImage() {
        ImageDesc idesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        idesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(idesc);
        img.modify()->metadata().set(Metadata::FrameNumber, int64_t(7));
        img.modify()->metadata().set(Metadata::Title, String("hero"));
        return img;
}

bool matchImage(const String &expr, const Image &img) {
        auto [q, err] = VariantQuery<Image>::parse(expr);
        REQUIRE_MESSAGE(err.isOk(), expr.cstr(), ": ", q.errorDetail().cstr());
        return q.match(img);
}

}  // namespace

TEST_CASE("VariantQuery<Image>: scalar dimensions and typed literal") {
        Image::Ptr img = richImage();
        CHECK(matchImage("Width == 1920", *img));
        CHECK(matchImage("Height == 1080", *img));
        CHECK(matchImage("Width >= 1920 && Height >= 1080", *img));
        CHECK(matchImage("PixelDesc == \"RGBA8_sRGB\"", *img));
}

TEST_CASE("VariantQuery<Image>: metadata via database handler") {
        Image::Ptr img = richImage();
        CHECK(matchImage("Meta.Title == \"hero\"", *img));
        CHECK(matchImage("Meta.FrameNumber == 7", *img));
        CHECK(matchImage("has(Meta.Title) && !has(Meta.License)", *img));
}

// ============================================================
// VariantQuery<Audio>
// ============================================================

namespace {

Audio::Ptr richAudio() {
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio::Ptr aud = Audio::Ptr::create(adesc, 1024);
        aud.modify()->metadata().set(Metadata::Album, String("LiveSet"));
        aud.modify()->metadata().set(Metadata::FrameNumber, int64_t(11));
        return aud;
}

bool matchAudio(const String &expr, const Audio &aud) {
        auto [q, err] = VariantQuery<Audio>::parse(expr);
        REQUIRE_MESSAGE(err.isOk(), expr.cstr(), ": ", q.errorDetail().cstr());
        return q.match(aud);
}

}  // namespace

TEST_CASE("VariantQuery<Audio>: scalar properties") {
        Audio::Ptr aud = richAudio();
        CHECK(matchAudio("Channels == 2", *aud));
        CHECK(matchAudio("SampleRate >= 48000", *aud));
        CHECK(matchAudio("has(Samples) && has(Frames)", *aud));
}

TEST_CASE("VariantQuery<Audio>: metadata via database handler") {
        Audio::Ptr aud = richAudio();
        CHECK(matchAudio("Meta.Album == \"LiveSet\"", *aud));
        CHECK(matchAudio("Meta.FrameNumber == 11", *aud));
        CHECK(matchAudio("Meta.Album ~~ \"Live\"", *aud));
}
