/**
 * @file      mediaioclock.cpp
 * @copyright Howard Logic. All rights reserved.
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

                // Read a frame; currentFrame() advances and the clock tracks.
                REQUIRE(io->source(0)->readFrame().wait().isOk());
                CHECK(group->currentFrame().value() >= 1);

                const int64_t periodNs = fps.frameDuration().nanoseconds();
                auto          r1 = clock->nowNs();
                REQUIRE(isOk(r1));
                CHECK(value(r1) == group->currentFrame().value() * periodNs);

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

        TEST_CASE("cannot be paused") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *group = io->portGroup(0);
                REQUIRE(group != nullptr);
                Clock::Ptr clock = group->clock();
                REQUIRE(clock.isValid());
                CHECK(clock->canPause() == false);
                CHECK(clock.modify()->setPause(true) == Error::NotSupported);
                io->close().wait();
                delete io;
        }

} // TEST_SUITE
