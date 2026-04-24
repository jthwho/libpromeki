/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/frame.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedaudiopayload.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiocodec.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/stringlist.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

// Returns true when @p haystack contains a line of the form
// "<prefix>: <any>" — matches what Frame::dump emits for each payload
// scalar so tests can assert on the key without pinning the value.
bool containsKeyLine(const StringList &haystack, const String &key) {
        const String needle = key + String(":");
        for(const String &line : haystack) {
                if(line.find(needle) != String::npos) return true;
        }
        return false;
}

} // namespace

// Regression: Frame::dump used to emit "VideoPayload[0]:" /
// "AudioPayload[0]:" header lines with no per-payload detail
// underneath.  pmdf-inspect therefore printed what looked like
// "incomplete frames".  The fix walks VariantLookup<VideoPayload> /
// VariantLookup<AudioPayload> and emits every registered scalar —
// this test pins a handful of those scalars so a future regression
// (e.g. silently dropping the subdump again) is caught here rather
// than in pmdf-inspect's output.
TEST_CASE("Frame::dump: video + audio payload subdumps") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_30));

        UncompressedVideoPayload::Ptr vp = UncompressedVideoPayload::allocate(
                ImageDesc(Size2Du32(16, 8), PixelFormat(PixelFormat::RGB8_sRGB)));
        REQUIRE(vp.isValid());
        f.modify()->addPayload(vp);

        AudioDesc adesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        const size_t samples = 32;
        Buffer::Ptr abuf = Buffer::Ptr::create(adesc.bufferSize(samples));
        abuf.modify()->setSize(adesc.bufferSize(samples));
        BufferView aview(abuf, 0, abuf->size());
        UncompressedAudioPayload::Ptr ap = UncompressedAudioPayload::Ptr::create(
                adesc, samples, aview);
        REQUIRE(ap.isValid());
        f.modify()->addPayload(ap);

        const StringList lines = f->dump();

        // Header lines still show up.
        CHECK(containsKeyLine(lines, String("VideoPayload[0]")));
        CHECK(containsKeyLine(lines, String("AudioPayload[0]")));

        // Video subdump — scalars registered in
        // uncompressedvideopayload.cpp's PROMEKI_LOOKUP_REGISTER block.
        CHECK(containsKeyLine(lines, String("Width")));
        CHECK(containsKeyLine(lines, String("Height")));
        CHECK(containsKeyLine(lines, String("PixelFormat")));
        CHECK(containsKeyLine(lines, String("PlaneCount")));

        // Audio subdump — scalars registered in
        // uncompressedaudiopayload.cpp's PROMEKI_LOOKUP_REGISTER block.
        CHECK(containsKeyLine(lines, String("SampleRate")));
        CHECK(containsKeyLine(lines, String("Channels")));
        CHECK(containsKeyLine(lines, String("Format")));
        CHECK(containsKeyLine(lines, String("Samples")));
}

// ---------------------------------------------------------------
// Helper: construct a small UncompressedVideoPayload
// ---------------------------------------------------------------
namespace {

UncompressedVideoPayload::Ptr makeVideoPayload(uint32_t w = 16, uint32_t h = 8) {
        return UncompressedVideoPayload::allocate(
                ImageDesc(Size2Du32(w, h), PixelFormat(PixelFormat::RGB8_sRGB)));
}

UncompressedAudioPayload::Ptr makeAudioPayload(size_t samples = 32) {
        AudioDesc adesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        Buffer::Ptr buf = Buffer::Ptr::create(adesc.bufferSize(samples));
        buf.modify()->setSize(adesc.bufferSize(samples));
        BufferView view(buf, 0, buf->size());
        return UncompressedAudioPayload::Ptr::create(adesc, samples, view);
}

} // namespace

TEST_CASE("Frame: default construction is empty") {
        Frame::Ptr f = Frame::Ptr::create();
        CHECK(f->payloadList().isEmpty());
        CHECK(f->videoPayloads().isEmpty());
        CHECK(f->audioPayloads().isEmpty());
}

TEST_CASE("Frame::addPayload / payloadList: round-trips payloads") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload();
        auto ap = makeAudioPayload();
        REQUIRE(vp.isValid());
        REQUIRE(ap.isValid());

        f.modify()->addPayload(vp);
        f.modify()->addPayload(ap);

        CHECK(f->payloadList().size() == 2u);
        CHECK(f->payloadList()[0].ptr() == vp.ptr());
        CHECK(f->payloadList()[1].ptr() == ap.ptr());
}

TEST_CASE("Frame::videoPayloads: filters only video-kind entries") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp1 = makeVideoPayload(16, 8);
        auto vp2 = makeVideoPayload(32, 16);
        auto ap  = makeAudioPayload();
        REQUIRE(vp1.isValid());
        REQUIRE(vp2.isValid());
        REQUIRE(ap.isValid());

        f.modify()->addPayload(vp1);
        f.modify()->addPayload(ap);
        f.modify()->addPayload(vp2);

        auto vids = f->videoPayloads();
        CHECK(vids.size() == 2u);
        // Order is preserved.
        CHECK(vids[0].ptr() == vp1.ptr());
        CHECK(vids[1].ptr() == vp2.ptr());

        auto auds = f->audioPayloads();
        CHECK(auds.size() == 1u);
        CHECK(auds[0].ptr() == ap.ptr());
}

TEST_CASE("Frame::audioPayloads: empty when no audio present") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->addPayload(makeVideoPayload());
        CHECK(f->audioPayloads().isEmpty());
        CHECK(f->videoPayloads().size() == 1u);
}

TEST_CASE("Frame::videoFormat: valid when frame-rate metadata is set") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_30));
        f.modify()->addPayload(makeVideoPayload(1920, 1080));

        VideoFormat vf = f->videoFormat(0);
        CHECK(vf.isValid());
        CHECK(vf.raster() == Size2Du32(1920, 1080));
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_30));
}

TEST_CASE("Frame::videoFormat: out-of-range index returns invalid VideoFormat") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_30));
        f.modify()->addPayload(makeVideoPayload(1920, 1080));

        CHECK_FALSE(f->videoFormat(1).isValid());
        CHECK_FALSE(f->videoFormat(99).isValid());
}

TEST_CASE("Frame::videoFormat: returns invalid when no frame-rate in metadata") {
        Frame::Ptr f = Frame::Ptr::create();
        // No FrameRate set in metadata.
        f.modify()->addPayload(makeVideoPayload(1920, 1080));

        // A missing frame rate makes the VideoFormat invalid.
        CHECK_FALSE(f->videoFormat(0).isValid());
}

TEST_CASE("Frame::videoFormat: skips audio payloads to find the nth video") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_25));
        f.modify()->addPayload(makeAudioPayload());
        f.modify()->addPayload(makeVideoPayload(640, 480));
        f.modify()->addPayload(makeAudioPayload());
        f.modify()->addPayload(makeVideoPayload(1280, 720));

        // index 0 is the 640x480 video, index 1 is 1280x720.
        VideoFormat vf0 = f->videoFormat(0);
        CHECK(vf0.isValid());
        CHECK(vf0.raster() == Size2Du32(640, 480));

        VideoFormat vf1 = f->videoFormat(1);
        CHECK(vf1.isValid());
        CHECK(vf1.raster() == Size2Du32(1280, 720));

        CHECK_FALSE(f->videoFormat(2).isValid());
}
