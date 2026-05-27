/**
 * @file      mediaioportgroup.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/framerate.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosource.h>
#include <promeki/tpgmediaio.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

        // Builds a minimally-configured TPG MediaIO and opens it so we
        // have a real MediaIOPortGroup to drive setRate / nextStep
        // against.  Mirrors the helper in mediaioclock.cpp.
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

TEST_SUITE("MediaIOPortGroup_Rate") {

        TEST_CASE("default rate is 1.0 forward") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                CHECK(g->rate() == 1.0);
                io->close().wait();
                delete io;
        }

        TEST_CASE("setRate forwards from the source helper") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                MediaIOSource *src = io->source(0);
                REQUIRE(src != nullptr);

                src->setRate(0.5);
                CHECK(g->rate() == 0.5);
                CHECK(src->rate() == 0.5);

                src->setRate(2.0);
                CHECK(g->rate() == 2.0);

                io->close().wait();
                delete io;
        }

        TEST_CASE("setRate ignores non-finite values") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);

                g->setRate(2.0);
                CHECK(g->rate() == 2.0);

                // NaN and infinities are silently ignored in release
                // builds (they trip an assert in debug).  Either way
                // the previously-stored value must survive.
                g->setRate(std::numeric_limits<double>::quiet_NaN());
                CHECK(g->rate() == 2.0);
                g->setRate(std::numeric_limits<double>::infinity());
                CHECK(g->rate() == 2.0);

                io->close().wait();
                delete io;
        }
} // TEST_SUITE

TEST_SUITE("MediaIOPortGroup_NextStep") {

        TEST_CASE("rate=1.0 advances by 1 every tick") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(1.0);
                for (int i = 0; i < 8; ++i) CHECK(g->nextStep() == 1);
                io->close().wait();
                delete io;
        }

        TEST_CASE("rate=0.0 always reports hold") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(0.0);
                for (int i = 0; i < 8; ++i) CHECK(g->nextStep() == 0);
                io->close().wait();
                delete io;
        }

        TEST_CASE("rate=0.5 alternates 0,1 (slow motion)") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(0.5);
                // Across a long horizon the average advance must
                // converge to 0.5 — counted total over 1000 ticks.
                int total = 0;
                int holds = 0;
                int singles = 0;
                for (int i = 0; i < 1000; ++i) {
                        int s = g->nextStep();
                        total += s;
                        if (s == 0) ++holds;
                        else if (s == 1) ++singles;
                        else FAIL("rate=0.5 must only emit 0 or 1");
                }
                CHECK(total == 500);
                CHECK(holds == 500);
                CHECK(singles == 500);
                io->close().wait();
                delete io;
        }

        TEST_CASE("rate=2.5 alternates 2,3 (fast forward)") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(2.5);
                int total = 0;
                for (int i = 0; i < 1000; ++i) {
                        int s = g->nextStep();
                        total += s;
                        CHECK((s == 2 || s == 3));
                }
                CHECK(total == 2500);
                io->close().wait();
                delete io;
        }

        TEST_CASE("rate=-0.5 alternates 0,-1 (slow reverse)") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(-0.5);
                int total = 0;
                for (int i = 0; i < 1000; ++i) {
                        int s = g->nextStep();
                        total += s;
                        CHECK((s == 0 || s == -1));
                }
                CHECK(total == -500);
                io->close().wait();
                delete io;
        }

        TEST_CASE("setRate resets the accumulator so the next tick is fresh") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(0.5);
                // First call accumulates to 0.5 → nextStep returns 0.
                CHECK(g->nextStep() == 0);
                // Switching to rate=1.0 must wipe the accumulator so
                // the next tick is exactly 1, not 1.5.
                g->setRate(1.0);
                CHECK(g->nextStep() == 1);
                io->close().wait();
                delete io;
        }

        TEST_CASE("seekToFrame resets the accumulator") {
                MediaIO          *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
                MediaIOPortGroup *g = io->portGroup(0);
                REQUIRE(g != nullptr);
                g->setRate(0.5);
                // Build up some accumulator carry then seek.  Seek on
                // a non-seekable group resolves IllegalSeek but the
                // accumulator-reset path runs only when canSeek is
                // true; force it via the public canSeek setter.
                g->setCanSeek(true);
                CHECK(g->nextStep() == 0); // accumulator now 0.5
                g->seekToFrame(FrameNumber(10), MediaIO_SeekDefault);
                // After seek the accumulator is back to 0; rate=0.5
                // produces 0 on the first tick again rather than 1.
                CHECK(g->nextStep() == 0);
                io->close().wait();
                delete io;
        }
} // TEST_SUITE
