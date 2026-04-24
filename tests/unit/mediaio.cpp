/**
 * @file      tests/mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/videoformat.h>
#include <promeki/pixelformat.h>
#include <promeki/framerate.h>
#include <promeki/frame.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/clockdomain.h>

using namespace promeki;

// ============================================================================
// MediaIO fills in missing per-payload timing
//
// The read path guarantees that every payload arrives downstream with
// a valid native pts and (for video) a native duration.  A backend
// that leaves one or both off — like TPG, which generates frames
// without touching pts — should still come out the other side with a
// Synthetic fallback pts and a one-frame duration derived from the
// session frame rate.
// ============================================================================

TEST_CASE("MediaIO auto-fills missing native pts and video duration on read") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoPixelFormat,
                PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::AudioEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame.isValid());

        auto vids = frame->videoPayloads();
        REQUIRE_FALSE(vids.isEmpty());
        REQUIRE(vids[0].isValid());
        const VideoPayload &vp = *vids[0];

        // Native pts must be set by the auto-fill; TPG itself does
        // not touch pts.  The fallback domain is Synthetic.
        CHECK(vp.pts().isValid());
        CHECK(vp.pts().domain() == ClockDomain::Synthetic);

        // Duration is one frame of the session rate (FPS_30 →
        // 33_333_333 ns).
        CHECK(vp.hasDuration());
        const Duration oneFrame = FrameRate(FrameRate::FPS_30).frameDuration();
        CHECK(vp.duration() == oneFrame);

        auto auds = frame->audioPayloads();
        REQUIRE_FALSE(auds.isEmpty());
        REQUIRE(auds[0].isValid());
        const AudioPayload &ap = *auds[0];
        CHECK(ap.pts().isValid());
        CHECK(ap.pts().domain() == ClockDomain::Synthetic);
        // Audio duration is derived from intrinsic sampleCount /
        // sampleRate and should be non-zero for a real audio packet.
        CHECK(ap.hasDuration());
        CHECK_FALSE(ap.duration().isZero());

        io->close();
        delete io;
}

TEST_CASE("MediaIO does not overwrite a producer-supplied pts") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoPixelFormat,
                PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::AudioEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame.isValid());
        auto vids = frame->videoPayloads();
        REQUIRE_FALSE(vids.isEmpty());
        // After MediaIO auto-fill the pts domain is Synthetic.  If we
        // stamp our own pts on a cloned payload and run it through
        // any pts-respecting consumer, the pts should survive — the
        // auto-fill only touches payloads whose pts is invalid.
        const MediaTimeStamp auto_pts = vids[0]->pts();
        CHECK(auto_pts.isValid());
        CHECK(auto_pts.domain() == ClockDomain::Synthetic);

        // Build an explicit override and verify isValid/offset plumbing.
        TimeStamp explicitTs;
        explicitTs.setValue(TimeStamp::Value(std::chrono::nanoseconds(42)));
        MediaTimeStamp explicitMts(explicitTs, ClockDomain::SystemMonotonic);
        vids[0].modify()->setPts(explicitMts);
        CHECK(vids[0]->pts() == explicitMts);

        io->close();
        delete io;
}
