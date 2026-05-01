/**
 * @file      mediaiotask_nullpacing.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include <promeki/duration.h>
#include <promeki/elapsedtimer.h>
#include <promeki/enums.h>
#include <promeki/eventloop.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/nullpacingmediaio.h>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/timestamp.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

        // Standalone TPG → NullPacing rig: a TPG MediaIO is opened as a
        // source and frames are pumped through a NullPacing MediaIO opened
        // as a sink.  Both MediaIOs are owned by the rig and torn down in
        // the destructor, including a close() pass to make sure any strand
        // activity is fully drained before the task is freed.
        struct NullPacingRig {
                        MediaIO            *tpg = nullptr;
                        NullPacingMediaIO  *sinkIo = nullptr;

                        ~NullPacingRig() {
                                if (tpg) {
                                        tpg->close().wait();
                                        delete tpg;
                                }
                                if (sinkIo) {
                                        sinkIo->close().wait();
                                        delete sinkIo;
                                }
                        }
        };

        // Builds a TPG → NullPacing pair.  @p sourceFormat picks the TPG's
        // reported frame rate (and hence the rate the source will emit when
        // pulled).  @p mode selects Wallclock / Free.  @p targetFps is the
        // pacing target; pass an invalid Rational (0/1) to test the
        // "follow source descriptor" fallback.
        void buildRig(NullPacingRig &rig, VideoFormat::WellKnownFormat sourceFormat, promeki::NullPacingMode mode,
                      const Rational<int> &targetFps, bool burnTimings = false) {
                // ---- TPG source ----
                MediaIO::Config tpgCfg = MediaIOFactory::defaultConfig("TPG");
                tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(sourceFormat));
                tpgCfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
                tpgCfg.set(MediaConfig::VideoEnabled, true);
                tpgCfg.set(MediaConfig::AudioEnabled, false);
                rig.tpg = MediaIO::create(tpgCfg);
                REQUIRE(rig.tpg != nullptr);
                REQUIRE(rig.tpg->open().wait().isOk());

                // Reconstruct the source desc explicitly — the multi-port
                // refactor leaves @c io->mediaDesc() empty after open()
                // for backends that haven't been ported to populate
                // @c cmd.mediaDesc yet (TPG is one of them).  Until that
                // lands, build the upstream-shape hint from the same
                // VideoFormat the rig configured the TPG with.
                const VideoFormat vfmt(sourceFormat);
                MediaDesc         srcDesc;
                srcDesc.setFrameRate(vfmt.frameRate());
                srcDesc.imageList().pushToBack(ImageDesc(vfmt.raster(), PixelFormat(PixelFormat::RGBA8_sRGB)));

                // ---- NullPacing sink ----
                rig.sinkIo = new NullPacingMediaIO();
                MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("NullPacing");
                sinkCfg.set(MediaConfig::NullPacingMode, mode);
                sinkCfg.set(MediaConfig::NullPacingTargetFps, targetFps);
                sinkCfg.set(MediaConfig::NullPacingBurnTimings, burnTimings);
                sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                rig.sinkIo->setConfig(sinkCfg);
                REQUIRE(rig.sinkIo->setPendingMediaDesc(srcDesc).isOk());
                REQUIRE(rig.sinkIo->open().wait().isOk());
        }

        // Pulls one frame from TPG and pushes it through the sink.
        // Returns the writeFrame error (so the caller can decide what to do
        // with TryAgain etc.).  Both calls are blocking so the strand stays
        // in lockstep with the test thread — this keeps the timing math
        // inside the tests deterministic.
        Error pumpOne(NullPacingRig &rig) {
                MediaIORequest readReq = rig.tpg->source(0)->readFrame();
                Error          rerr = readReq.wait();
                if (rerr.isError()) return rerr;
                Frame::Ptr frame;
                if (const auto *cr = readReq.commandAs<MediaIOCommandRead>()) {
                        frame = cr->frame;
                }
                REQUIRE(frame.isValid());
                return rig.sinkIo->sink(0)->writeFrame(frame).wait();
        }

        // Pumps frames into the sink for the specified wall-clock duration.
        // We pump at the upstream's natural pull rate (each pumpOne does a
        // blocking read + blocking write), so the loop exit timing is bounded
        // by ms-precision wall-clock — exactly the granularity the pacing
        // tests need.
        void pumpForMs(NullPacingRig &rig, int64_t durationMs) {
                ElapsedTimer t;
                t.start();
                while (t.elapsed() < durationMs) {
                        Error werr = pumpOne(rig);
                        if (werr.isError() && werr != Error::TryAgain) {
                                FAIL("pumpOne failed: " << werr.desc().cstr());
                        }
                }
        }

} // namespace

// ============================================================================
// Format descriptor / registration plumbing
// ============================================================================

TEST_CASE("NullPacingMediaIO_FactoryIsRegistered") {
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("NullPacing"));
        REQUIRE(factory != nullptr);
        CHECK(factory->name() == String("NullPacing"));
        CHECK(factory->canBeSink());
        CHECK_FALSE(factory->canBeSource());
        CHECK_FALSE(factory->canBeTransform());

        // Factory path: the registry should know the type and build
        // a default-config sink that opens cleanly.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("NullPacing");
        // Registry default for TargetFps is 0/1 ("follow source"),
        // which fails open() without an upstream desc — set a real
        // target here so this round-trip stays meaningful.
        cfg.set(MediaConfig::NullPacingTargetFps, Rational<int>(30, 1));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());
        CHECK(io->isOpen());
        CHECK(io->close().wait().isOk());
        delete io;
}

// ============================================================================
// Wallclock mode pacing
// ============================================================================

TEST_CASE("NullPacingMediaIO_WallclockDropsFramesBetweenTicks") {
        // TPG @ 60 fps → NullPacing @ 24 fps.  Over ~500 ms wall time
        // the sink should consume on the order of 12 frames (24 fps
        // × 0.5 s).  Drops should account for everything else the
        // upstream pushed.
        NullPacingRig rig;
        buildRig(rig, VideoFormat::Smpte1080p60, promeki::NullPacingMode::Wallclock, Rational<int>(24, 1));

        pumpForMs(rig, 500);

        const NullPacingSnapshot snap = rig.sinkIo->snapshot();
        // Every frame upstream produced was either consumed or
        // dropped — there's no other terminal state for a frame in
        // the sink.
        CHECK(snap.latencySamples == snap.framesConsumed + snap.framesDropped);
        // Allow ±25% slack on the consumed count because timer
        // jitter is real on a busy CI machine.
        CHECK(snap.framesConsumed >= 8);
        CHECK(snap.framesConsumed <= 18);
        // We pumped at 60 fps (TPG's reported rate) and consumed at
        // 24 fps; drops should track the difference, with a healthy
        // floor on the count to confirm pacing actually fired.
        CHECK(snap.framesDropped > 0);
        CHECK(snap.framesDropped > snap.framesConsumed);
        // Latency totals are sanity checks — every frame should
        // record some non-negative latency.
        CHECK(snap.peakLatencyUs >= 0);
        CHECK(snap.totalLatencyUs >= 0);
}

// ============================================================================
// Free mode (no pacing, never drops)
// ============================================================================

TEST_CASE("NullPacingMediaIO_FreeModeAcceptsEveryFrame") {
        // In Free mode the sink ignores TargetFps entirely; every
        // frame the upstream feeds should be consumed and the drop
        // counter should stay at zero.
        NullPacingRig rig;
        buildRig(rig, VideoFormat::Smpte1080p60, promeki::NullPacingMode::Free, Rational<int>(24, 1)); // ignored

        const int frames = 12;
        for (int i = 0; i < frames; ++i) {
                CHECK(pumpOne(rig).isOk());
        }

        const NullPacingSnapshot snap = rig.sinkIo->snapshot();
        CHECK(snap.framesConsumed == frames);
        CHECK(snap.framesDropped == 0);
        CHECK(snap.latencySamples == frames);
}

// ============================================================================
// TargetFps = 0/1 → follow upstream MediaDesc
// ============================================================================

TEST_CASE("NullPacingMediaIO_TargetFpsZeroFollowsSourceDesc") {
        // Configure the sink with NullPacingTargetFps = 0/1; the
        // upstream is TPG @ 30 fps.  The sink should latch 30 fps
        // from the descriptor at open() time and pace at that rate
        // — there's no upstream-side pacing in this rig (TPG is
        // pulled as fast as the test loop runs), so the actual
        // useful invariant is "sink consumes about 30 fps on the
        // wall clock and drops everything else".
        NullPacingRig rig;
        buildRig(rig, VideoFormat::Smpte1080p30, promeki::NullPacingMode::Wallclock,
                 Rational<int>(0, 1)); // follow source

        pumpForMs(rig, 500);

        const NullPacingSnapshot snap = rig.sinkIo->snapshot();
        // 30 fps × 0.5 s = 15 consumed frames; allow ±25% slack.
        CHECK(snap.framesConsumed >= 11);
        CHECK(snap.framesConsumed <= 22);
        // Some drops are expected because the test pumps faster
        // than the source rate (no real pacing on the read side),
        // but the *consumed* rate is the load-bearing assertion.
}

// ============================================================================
// BurnTimings = true: confirms log path is wired without crashing
// ============================================================================

TEST_CASE("NullPacingMediaIO_BurnTimingsRunsCleanly") {
        // BurnTimings is intentionally noisy in real use (one
        // promekiDebug per frame), but we don't depend on log
        // content — we just confirm the option doesn't crash the
        // sink or alter its observable counter behaviour.
        NullPacingRig rig;
        buildRig(rig, VideoFormat::Smpte1080p60, promeki::NullPacingMode::Wallclock, Rational<int>(30, 1),
                 /*burnTimings=*/true);

        for (int i = 0; i < 6; ++i) {
                CHECK(pumpOne(rig).isOk());
        }

        const NullPacingSnapshot snap = rig.sinkIo->snapshot();
        CHECK(snap.latencySamples == 6);
        CHECK(snap.framesConsumed >= 1);
}

// ============================================================================
// Open-time validation: missing both TargetFps and source descriptor
// must reject open() rather than silently dividing by zero.
// ============================================================================

TEST_CASE("NullPacingMediaIO_WallclockWithoutRateRejectsOpen") {
        NullPacingMediaIO io;
        MediaIO::Config   cfg = MediaIOFactory::defaultConfig("NullPacing");
        cfg.set(MediaConfig::NullPacingMode, promeki::NullPacingMode::Wallclock);
        cfg.set(MediaConfig::NullPacingTargetFps, Rational<int>(0, 1));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        io.setConfig(cfg);

        // No setPendingMediaDesc → no upstream rate → no resolved rate.
        const Error err = io.open().wait();
        CHECK(err == Error::InvalidArgument);
}
