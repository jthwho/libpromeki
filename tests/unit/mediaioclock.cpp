/**
 * @file      mediaioclock.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/clock.h>
#include <promeki/framerate.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaioclock.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosource.h>
#include <promeki/tpgmediaio.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

        // Builds a TPG-backed MediaIO at the requested frame rate and opens
        // it as a source.  Used to drive MediaIOClock tests with real frame
        // advancement.
        MediaIO *makeOpenTPG(const FrameRate &fps) {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "TPG");
                cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), fps));
                cfg.set(MediaConfig::VideoEnabled, true);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open().wait().isOk());
                return io;
        }

} // namespace

TEST_SUITE("MediaIOClock") {

        TEST_CASE("group clock tracks currentFrame x framePeriod") {
                const FrameRate fps(FrameRate::FPS_25);
                MediaIO        *io = makeOpenTPG(fps);

                MediaIOPortGroup *group = io->portGroup(0);
                REQUIRE(group != nullptr);
                Clock::Ptr clock = group->clock();
                REQUIRE(clock.isValid());
                CHECK(clock->domain() == ClockDomain(ClockDomain::Synthetic));

                // No frames read yet — currentFrame() is 0, raw = 0.
                auto r0 = clock->nowNs();
                REQUIRE(isOk(r0));
                CHECK(value(r0) == 0);

                // Read the first frame.  The TPG reports the frame
                // number it just produced (0-indexed), so after one
                // read currentFrame() is 0 and the clock is still at 0.
                REQUIRE(io->source(0)->readFrame().wait().isOk());
                CHECK(group->currentFrame().value() == 0);

                const int64_t periodNs = fps.frameDuration().nanoseconds();
                auto          r1 = clock->nowNs();
                REQUIRE(isOk(r1));
                CHECK(value(r1) == 0);

                // Read a second frame; currentFrame advances to 1 and
                // the clock tracks one period.
                REQUIRE(io->source(0)->readFrame().wait().isOk());
                CHECK(group->currentFrame().value() == 1);
                auto r2 = clock->nowNs();
                REQUIRE(isOk(r2));
                CHECK(value(r2) == periodNs);

                io->close().wait();
                delete io;
        }

        TEST_CASE("resolutionNs reflects the configured frame period") {
                const FrameRate   fps(FrameRate::FPS_25);
                MediaIO          *io = makeOpenTPG(fps);
                MediaIOPortGroup *group = io->portGroup(0);
                REQUIRE(group != nullptr);
                Clock::Ptr clock = group->clock();
                REQUIRE(clock.isValid());

                CHECK(clock->resolutionNs() == fps.frameDuration().nanoseconds());

                io->close().wait();
                delete io;
        }

        TEST_CASE("pause freezes now while raw keeps reflecting frame counter") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *group = io->portGroup(0);
                REQUIRE(group != nullptr);
                Clock::Ptr clock = group->clock();
                REQUIRE(clock.isValid());

                CHECK(clock->canPause() == true);
                CHECK(clock->pauseMode() == ClockPauseMode::PausesRawKeepsRunning);
                CHECK(clock->isPaused() == false);

                // Read two frames so the group's currentFrame advances
                // past 0 (TPG reports the frame just produced, 0-indexed,
                // so after one read currentFrame == 0 and nowNs == 0).
                // Two reads gives us currentFrame == 1 and a non-zero
                // baseline for now().
                io->source(0)->readFrame().wait();
                io->source(0)->readFrame().wait();
                auto beforePauseRes = clock->nowNs();
                REQUIRE(isOk(beforePauseRes));
                int64_t beforePause = value(beforePauseRes);

                // Pause: setPause must succeed and isPaused flips true.
                Error err = clock.modify()->setPause(true);
                CHECK(err == Error::Ok);
                CHECK(clock->isPaused() == true);

                // Advance the underlying group counter while paused;
                // now() must hold steady because the pause-bookkeeping
                // offset cancels the raw advance.
                io->source(0)->readFrame().wait();
                auto duringPauseRes = clock->nowNs();
                REQUIRE(isOk(duringPauseRes));
                CHECK(value(duringPauseRes) == beforePause);

                // Resume: post-resume reads of now() pick up further
                // raw advances.
                err = clock.modify()->setPause(false);
                CHECK(err == Error::Ok);
                CHECK(clock->isPaused() == false);

                io->source(0)->readFrame().wait();
                auto afterResumeRes = clock->nowNs();
                REQUIRE(isOk(afterResumeRes));
                CHECK(value(afterResumeRes) > beforePause);

                io->close().wait();
                delete io;
        }

} // TEST_SUITE
