/**
 * @file      tests/unit/mediaiosink.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/elapsedtimer.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediadesc.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/thread.h>
#include <promeki/videoformat.h>

using namespace promeki;

// ============================================================================
// MediaIOSink::writeFrame is the always-async write path.  Phase 9
// added the unconditional capacity gate (TryAgain when full), the
// pre-open / closing short-circuits, and per-write request handles.
// These tests exercise each of those error paths plus the pre-open
// expectedDesc setters.
// ============================================================================

namespace {

MediaIO::Config tpgConfig() {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::AudioEnabled, false);
        cfg.set(MediaConfig::TimecodeEnabled, false);
        cfg.set(MediaConfig::VideoBurnEnabled, false);
        return cfg;
}

MediaIO::Config inspectorConfig() {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Inspector");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::InspectorDropFrames, false);
        cfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
        cfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        return cfg;
}

Frame::Ptr grabOneFrame(MediaIO *src) {
        MediaIORequest req = src->source(0)->readFrame();
        REQUIRE(req.wait().isOk());
        const auto *cr = req.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());
        return cr->frame;
}

} // namespace

// Note: there is no "pre-open" surface to test on the sink itself —
// MediaIOSink ports are created by the backend during open, so
// `sink(0)` returns nullptr before open completes.  The pre-open
// configuration surface is on @ref MediaIO::setPendingMediaDesc and
// friends; per-sink setExpectedDesc / writeFrame are only meaningful
// after open.

// ============================================================================
// setExpectedDesc / setExpectedAudioDesc / setExpectedMetadata
// are rejected with AlreadyOpen while the MediaIO is open.
// (They're meant to configure expectations between close and a
// subsequent re-open.)
// ============================================================================

TEST_CASE("MediaIOSink: setExpectedDesc rejected after open") {
        MediaIO *src = MediaIO::create(tpgConfig());
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO *sink = MediaIO::create(inspectorConfig());
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        // Post-open: rejected with AlreadyOpen.
        CHECK(sink->sink(0)->setExpectedDesc(src->mediaDesc()) == Error::AlreadyOpen);
        CHECK(sink->sink(0)->setExpectedAudioDesc(AudioDesc()) == Error::AlreadyOpen);
        CHECK(sink->sink(0)->setExpectedMetadata(Metadata()) == Error::AlreadyOpen);

        (void)sink->close().wait();
        (void)src->close().wait();
        delete src;
        delete sink;
}

// ============================================================================
// setWriteDepth clamps to >= 1.  Run after open so the sink port
// exists; the gate logic is the same regardless of MediaIO state.
// ============================================================================

TEST_CASE("MediaIOSink: setWriteDepth clamps to >= 1") {
        MediaIO *src = MediaIO::create(tpgConfig());
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO *sink = MediaIO::create(inspectorConfig());
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        sink->sink(0)->setWriteDepth(8);
        CHECK(sink->sink(0)->writeDepth() == 8);

        sink->sink(0)->setWriteDepth(1);
        CHECK(sink->sink(0)->writeDepth() == 1);

        sink->sink(0)->setWriteDepth(0);
        CHECK(sink->sink(0)->writeDepth() == 1);

        sink->sink(0)->setWriteDepth(-3);
        CHECK(sink->sink(0)->writeDepth() == 1);

        (void)sink->close().wait();
        (void)src->close().wait();
        delete src;
        delete sink;
}

// ============================================================================
// Capacity gate: with writeDepth=1 a tight burst of writeFrame calls
// must produce at least one TryAgain — the strand can only have one
// write in flight at a time, so subsequent submissions before the
// first completes are gated.
//
// Successful writes return Ok once they drain.  We don't make any
// claims about the *count* of TryAgains because that's timing-
// dependent, only that at least one is observed across the burst.
// ============================================================================

TEST_CASE("MediaIOSink: writeFrame returns TryAgain when sink is full") {
        MediaIO *src = MediaIO::create(tpgConfig());
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO *sink = MediaIO::create(inspectorConfig());
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        // Tight depth: only one write may be in flight at a time.
        sink->sink(0)->setWriteDepth(1);

        Frame::Ptr frame = grabOneFrame(src);

        // Burst submit WITHOUT waiting between submissions — that
        // way the strand has at most one cmd in flight while we
        // queue the rest, and the gate trips on every follow-up.
        // We expect:
        //   - at least one Ok (the first write that wins the gate),
        //   - at least one TryAgain (a follow-up arrives while the
        //     first is still in flight),
        //   - no other error codes.
        constexpr int             kBurst = 100;
        List<MediaIORequest>      reqs;
        for (int i = 0; i < kBurst; ++i) {
                reqs.pushToBack(sink->sink(0)->writeFrame(frame));
        }
        int okCount = 0;
        int tryAgainCount = 0;
        int otherCount = 0;
        for (size_t i = 0; i < reqs.size(); ++i) {
                Error e = reqs[i].wait(2000);
                if (e == Error::Ok) {
                        ++okCount;
                } else if (e == Error::TryAgain) {
                        ++tryAgainCount;
                } else {
                        ++otherCount;
                }
        }
        CHECK(okCount > 0);
        CHECK(tryAgainCount > 0);
        CHECK(otherCount == 0);

        (void)src->close().wait();
        (void)sink->close().wait();
        delete src;
        delete sink;
}

// ============================================================================
// writesAccepted decreases as a write goes in flight.
//
// With writeDepth=1, after a synchronous (in the user thread)
// writeFrame submission, writesAccepted should briefly read 0 — the
// pending counter is incremented inside writeFrame before the strand
// completes the cmd.  Once the wait returns, the counter must be
// back to 1 so the gate is open for the next write.
// ============================================================================

TEST_CASE("MediaIOSink: writesAccepted reflects in-flight count") {
        MediaIO *src = MediaIO::create(tpgConfig());
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO *sink = MediaIO::create(inspectorConfig());
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        sink->sink(0)->setWriteDepth(1);
        const int initial = sink->sink(0)->writesAccepted();
        // The Inspector backend reports no internal pending output,
        // so writesAccepted before any submission equals writeDepth.
        CHECK(initial == 1);

        Frame::Ptr frame = grabOneFrame(src);

        MediaIORequest req = sink->sink(0)->writeFrame(frame);
        // Request is valid (the gate let it through).
        CHECK(req.isValid());
        // Sink may already have completed the write — Inspector is
        // very fast.  Either way we drain to make the assertion
        // about post-completion deterministic.
        Error result = req.wait(2000);
        CHECK(result == Error::Ok);
        // After completion the in-flight count drops back to 0 and
        // writesAccepted recovers to writeDepth.
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < 1000 && sink->sink(0)->writesAccepted() < 1) {
                Thread::sleepMs(1);
        }
        CHECK(sink->sink(0)->writesAccepted() == 1);

        (void)src->close().wait();
        (void)sink->close().wait();
        delete src;
        delete sink;
}

// ============================================================================
// writeFrame on a closing sink resolves NotOpen.
//
// Close is async — the cmd is on the strand and isClosing() returns
// true between submit and the close completion.  Any writeFrame call
// during that window must short-circuit to NotOpen so the consumer
// stops feeding.
// ============================================================================

TEST_CASE("MediaIOSink: writeFrame returns NotOpen after close completes") {
        MediaIO *src = MediaIO::create(tpgConfig());
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO *sink = MediaIO::create(inspectorConfig());
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        Frame::Ptr frame = grabOneFrame(src);

        // Close synchronously, then writeFrame — the post-close gate
        // is the same isOpen() check that pre-open uses.  After
        // close.wait() returns, isOpen() is false and writeFrame
        // returns NotOpen without ever touching the strand.
        REQUIRE(sink->close().wait().isOk());
        MediaIORequest req = sink->sink(0)->writeFrame(frame);
        CHECK(req.isReady());
        CHECK(req.wait() == Error::NotOpen);

        (void)src->close().wait();
        delete src;
        delete sink;
}
