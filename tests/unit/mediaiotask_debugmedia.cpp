/**
 * @file      mediaiotask_debugmedia.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/debugmediafile.h>
#include <promeki/dir.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/uncompressedaudiopayload.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

// Builds a minimal Frame with one video + one audio payload so
// DebugMediaFile has something non-trivial to serialise on the
// write side and the reader has a real desc set to recover.
Frame::Ptr makeSmokeFrame() {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate,
                                   FrameRate(FrameRate::FPS_30));

        UncompressedVideoPayload::Ptr vp = UncompressedVideoPayload::allocate(
                ImageDesc(Size2Du32(16, 8), PixelFormat(PixelFormat::RGB8_sRGB)));
        if(!vp.isValid()) return Frame::Ptr();
        f.modify()->addPayload(vp);

        AudioDesc adesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        const size_t samples = 32;
        Buffer::Ptr abuf = Buffer::Ptr::create(adesc.bufferSize(samples));
        abuf.modify()->setSize(adesc.bufferSize(samples));
        BufferView aview(abuf, 0, abuf->size());
        UncompressedAudioPayload::Ptr ap = UncompressedAudioPayload::Ptr::create(
                adesc, samples, aview);
        if(!ap.isValid()) return Frame::Ptr();
        f.modify()->addPayload(ap);

        return f;
}

// Writes a one-frame PMDF via the DebugMediaFile helper so the
// source-open test has a known-good file to open.  Path is under
// Dir::temp() so it honours LibraryOptions::TempDir overrides.
String writeSinglePmdf() {
        const String path = Dir::temp().path().toString()
                + String("/promeki-pmdf-sourceopen-test.pmdf");
        DebugMediaFile df;
        REQUIRE(df.open(path, DebugMediaFile::Write).isOk());
        Frame::Ptr f = makeSmokeFrame();
        REQUIRE(f.isValid());
        REQUIRE(df.writeFrame(f).isOk());
        REQUIRE(df.close().isOk());
        return path;
}

} // namespace

// Regression: MediaIOTask_DebugMedia::executeCmd(MediaIOCommandOpen)
// used to leave @c cmd.mediaDesc / @c cmd.audioDesc default-constructed
// in Source mode.  That left @ref MediaIO::mediaDesc() invalid after
// @c open(), which broke @ref MediaPipelinePlanner::discoverSourceDesc
// — the planner's "open briefly and read mediaDesc" fallback found
// nothing, so any pipeline with a PMDF source aborted at build().
//
// The fix peeks frame 0 at open time, builds MediaDesc/AudioDesc from
// its payloads, and rewinds the file cursor so the caller's first
// @c readFrame() still returns frame 0.  This test pins both halves:
// the descs must be populated immediately after open, AND the first
// readFrame must return the frame we wrote (proves the rewind).
TEST_CASE("MediaIOTask_DebugMedia: source open populates mediaDesc/audioDesc") {
        const String path = writeSinglePmdf();

        MediaIO::Config cfg = MediaIO::defaultConfig("PMDF");
        cfg.set(MediaConfig::Type, "PMDF");
        cfg.set(MediaConfig::Filename, path);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        REQUIRE(io->open(MediaIO::Source).isOk());

        // The descs must be valid *before* any readFrame — the
        // pipeline planner relies on this to build a plan without
        // consuming frames from the source.
        const MediaDesc &md = io->mediaDesc();
        CHECK(md.isValid());
        CHECK(md.frameRate() == FrameRate(FrameRate::FPS_30));
        REQUIRE_FALSE(md.imageList().isEmpty());
        CHECK(md.imageList()[0].width() == 16);
        CHECK(md.imageList()[0].height() == 8);
        CHECK(md.imageList()[0].pixelFormat()
              == PixelFormat(PixelFormat::RGB8_sRGB));

        const AudioDesc &adesc = io->audioDesc();
        CHECK(adesc.isValid());
        CHECK(adesc.sampleRate() == doctest::Approx(48000.0f));
        CHECK(adesc.channels() == 2u);
        CHECK(adesc.format() == AudioFormat(AudioFormat::PCMI_S16LE));

        CHECK(io->frameRate() == FrameRate(FrameRate::FPS_30));

        // Rewind sanity: first readFrame after open must still yield
        // frame 0 — the peek-and-rewind would otherwise eat it.
        Frame::Ptr first;
        Error re = io->readFrame(first);
        REQUIRE(re.isOk());
        REQUIRE(first.isValid());
        CHECK(first->videoPayloads().size() == 1u);
        CHECK(first->audioPayloads().size() == 1u);

        (void)io->close();
        delete io;
        std::remove(path.cstr());
}
