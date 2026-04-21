/**
 * @file      mediaioclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioclock.h>
#include <promeki/mediaiotask.h>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/clock.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

// Builds a TPG-backed MediaIO at the requested frame rate and opens
// it as a source.  Used to drive MediaIOClock tests with real frame
// advancement.
MediaIO *makeOpenTPG(const FrameRate &fps) {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat,
                VideoFormat(Size2Du32(16, 16), fps));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());
        return io;
}

} // namespace

TEST_SUITE("MediaIOClock") {

TEST_CASE("raw() reads currentFrame × framePeriod") {
        const FrameRate fps(FrameRate::FPS_25);
        MediaIO *io = makeOpenTPG(fps);

        MediaIOClock clock(io);
        CHECK(clock.domain() == ClockDomain(ClockDomain::Synthetic));

        // No frames read yet — currentFrame() is 0, raw = 0.
        auto r0 = clock.nowNs();
        REQUIRE(isOk(r0));
        CHECK(value(r0) == 0);

        // Read a frame; currentFrame() advances and the clock tracks.
        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        CHECK(io->currentFrame() >= 1);

        const int64_t periodNs = fps.frameDuration().nanoseconds();
        auto r1 = clock.nowNs();
        REQUIRE(isOk(r1));
        CHECK(value(r1) == io->currentFrame() * periodNs);

        io->close();
        delete io;
}

TEST_CASE("raw() returns ObjectGone after owner destroyed") {
        MediaIO *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
        MediaIOClock clock(io);

        // Sanity: clock reads OK while owner is alive.
        CHECK(isOk(clock.nowNs()));

        io->close();
        delete io;

        auto r = clock.nowNs();
        REQUIRE(isError(r));
        CHECK(error(r) == Error::ObjectGone);

        auto r2 = clock.now();
        REQUIRE(isError(r2));
        CHECK(error(r2) == Error::ObjectGone);
}

TEST_CASE("resolutionNs reflects the configured frame period") {
        const FrameRate fps(FrameRate::FPS_25);
        MediaIO *io = makeOpenTPG(fps);
        MediaIOClock clock(io);

        CHECK(clock.resolutionNs() == fps.frameDuration().nanoseconds());

        io->close();
        delete io;
}

TEST_CASE("cannot be paused") {
        MediaIO *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
        MediaIOClock clock(io);
        CHECK(clock.canPause() == false);
        CHECK(clock.setPause(true) == Error::NotSupported);
        io->close();
        delete io;
}

} // TEST_SUITE

TEST_SUITE("MediaIO::createClock") {

TEST_CASE("falls back to MediaIOClock when task supplies none") {
        MediaIO *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));

        Clock::Ptr clock = Clock::Ptr::takeOwnership(io->createClock());
        REQUIRE(clock.isValid());
        CHECK(clock->domain() == ClockDomain(ClockDomain::Synthetic));

        // Should track currentFrame × framePeriod — same behaviour as
        // a directly-constructed MediaIOClock.
        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        auto r = clock->nowNs();
        REQUIRE(isOk(r));
        const int64_t periodNs = FrameRate(FrameRate::FPS_25)
                .frameDuration().nanoseconds();
        CHECK(value(r) == io->currentFrame() * periodNs);

        io->close();
        delete io;
}

TEST_CASE("returns a clock whose raw() propagates ObjectGone when MediaIO destroyed") {
        MediaIO *io = makeOpenTPG(FrameRate(FrameRate::FPS_25));
        Clock::Ptr clock = Clock::Ptr::takeOwnership(io->createClock());
        REQUIRE(clock.isValid());

        io->close();
        delete io;

        auto r = clock->nowNs();
        REQUIRE(isError(r));
        CHECK(error(r) == Error::ObjectGone);
}

namespace {

// Minimal Clock stub used to verify that the task-supplied clock
// wins over the default MediaIOClock.
class StubClock : public Clock {
        public:
                StubClock() : Clock(ClockDomain(ClockDomain::SystemMonotonic)) {}
                int64_t     resolutionNs() const override { return 1; }
                ClockJitter jitter() const override {
                        return ClockJitter{Duration(), Duration()};
                }
        protected:
                Result<int64_t> raw() const override {
                        return makeResult<int64_t>(0xC10CC10C);
                }
                Error sleepUntilNs(int64_t) const override { return {}; }
};

// Task that supplies a StubClock from createClock().  All the other
// MediaIOTask virtuals fall through to their defaults — we never
// drive this task through the full open/read lifecycle, we only
// probe its clock hook via MediaIO::createClock.
class StubClockTask : public MediaIOTask {
        public:
                StubClock *lastCreated = nullptr;

                Clock *createClock() override {
                        auto *c = new StubClock();
                        lastCreated = c;
                        return c;
                }
};

}  // namespace

TEST_CASE("task's createClock wins over MediaIOClock fallback") {
        MediaIO *io = new MediaIO(nullptr);
        auto *task = new StubClockTask();
        REQUIRE(io->adoptTask(task).isOk());

        Clock::Ptr clock = Clock::Ptr::takeOwnership(io->createClock());
        REQUIRE(clock.isValid());
        // Domain identifies the task clock's source, not the synthetic
        // fallback, so we know the delegation fired.
        CHECK(clock->domain() == ClockDomain(ClockDomain::SystemMonotonic));
        CHECK(clock.ptr() == task->lastCreated);

        delete io;  // deletes the task too
}

} // TEST_SUITE
