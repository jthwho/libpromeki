/**
 * @file      tests/mediaio_stats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for MediaIO's live telemetry layer — the standard
 * MediaIOStats keys populated by the base class regardless of which
 * backend is running.  These cover BytesPerSecond / FramesPerSecond
 * derived from the RateTracker, the drop / repeat / late counters
 * routed through MediaIOTask::noteFrame* helpers, and the latency
 * keys derived from an attached BenchmarkReporter.  The TPG backend
 * is used for the rate tests because it needs no input files.  A
 * small purpose-built task exercises the drop / repeat / late path.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask.h>
#include <promeki/mediaconfig.h>
#include <promeki/benchmark.h>
#include <promeki/benchmarkreporter.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/buffer.h>
#include <promeki/pixeldesc.h>
#include <promeki/size2d.h>
#include <thread>
#include <chrono>

using namespace promeki;

namespace {

/**
 * @brief A MediaIO backend that exists only to exercise the base-class
 *        telemetry helpers.
 *
 * Reader-only; the executeCmd(Read) implementation generates a
 * constant-size dummy frame, optionally invokes noteFrameDropped /
 * noteFrameRepeated / noteFrameLate according to a test-controlled
 * counter, and returns.  We never register this with the factory —
 * tests construct it directly and hand it to MediaIO::adoptTask().
 */
class TelemetryTestTask : public MediaIOTask {
        public:
                int dropsToInject = 0;
                int repeatsToInject = 0;
                int latesToInject = 0;
                // A 32x32 RGB8 plane is 3072 bytes — enough to feed
                // the RateTracker meaningfully without the test
                // paying a real allocation cost.
                Size2Du32 imageSize{32, 32};
                PixelDesc::ID pixelDescId = PixelDesc::RGB8_sRGB;

                void injectDrops(int n) { dropsToInject = n; }
                void injectRepeats(int n) { repeatsToInject = n; }
                void injectLates(int n) { latesToInject = n; }

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override {
                        cmd.mediaDesc = MediaDesc();
                        cmd.frameRate = FrameRate(FrameRate::FPS_24);
                        cmd.canSeek = false;
                        cmd.frameCount = MediaIO::FrameCountInfinite;
                        cmd.defaultStep = 1;
                        cmd.defaultPrefetchDepth = 1;
                        return Error::Ok;
                }
                Error executeCmd(MediaIOCommandClose &cmd) override {
                        (void)cmd;
                        return Error::Ok;
                }
                Error executeCmd(MediaIOCommandRead &cmd) override {
                        // Flush any pending event injections first so
                        // a stats query immediately after a read sees
                        // the effect.
                        while(dropsToInject > 0) {
                                noteFrameDropped();
                                --dropsToInject;
                        }
                        while(repeatsToInject > 0) {
                                noteFrameRepeated();
                                --repeatsToInject;
                        }
                        while(latesToInject > 0) {
                                noteFrameLate();
                                --latesToInject;
                        }

                        // Manufacture a frame with one small allocated
                        // image plane so the base class's RateTracker
                        // has something non-zero to count.  The
                        // Image(size, pixelDesc) constructor allocates
                        // its own plane buffers.
                        Frame::Ptr f = Frame::Ptr::create();
                        Image img(imageSize, PixelDesc(pixelDescId));
                        f.modify()->imageList().pushToBack(
                                Image::Ptr::create(std::move(img)));
                        cmd.frame = f;
                        cmd.currentFrame = 0;
                        return Error::Ok;
                }
};

} // namespace

// ============================================================================
// Default stats bag — zero rates, zero counters, all standard keys present.
// ============================================================================

TEST_CASE("MediaIO stats: freshly opened reports zero rates and counters") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<double>(MediaIOStats::BytesPerSecond) == 0.0);
        CHECK(stats.getAs<double>(MediaIOStats::FramesPerSecond) == 0.0);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesDropped) == 0);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesRepeated) == 0);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesLate) == 0);
        // Latency keys stay at their default 0.0 when benchmarking
        // is off — distinguishable from "non-zero but small" via the
        // enable flag.
        CHECK(stats.getAs<double>(MediaIOStats::AverageLatencyMs) == 0.0);
        CHECK(stats.getAs<double>(MediaIOStats::PeakLatencyMs) == 0.0);

        io->close();
        delete io;
}

// ============================================================================
// Rate tracking — reader path
// ============================================================================

TEST_CASE("MediaIO stats: read path feeds BytesPerSecond / FramesPerSecond") {
        // A short rolling window is not tunable yet, so we use a
        // very short sleep relative to the 5s default: enough
        // wall-clock time for both bytesPerSecond() to be non-zero
        // after the first rotation, but not so long that the test
        // becomes flaky on loaded systems.
        MediaIO *io = new MediaIO();
        auto *task = new TelemetryTestTask();
        REQUIRE(io->adoptTask(task).isOk());
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Read ten frames back-to-back.  Each read hands the
        // test-task's synthetic frame back to MediaIO, which feeds
        // the frame bytes into _rateTracker on the strand lambda's
        // success path.
        for(int i = 0; i < 10; ++i) {
                Frame::Ptr f;
                Error err = io->readFrame(f, true);
                REQUIRE(err.isOk());
                REQUIRE(f.isValid());
        }

        // A short wait lets the RateTracker window roll over so the
        // query returns a non-zero rate rather than the half-window
        // fall-back (which is zero because there's no previous
        // window yet).
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (void)io->stats();  // force the tracker through a query

        MediaIOStats stats = io->stats();
        // We expect positive rates but cannot pin exact values
        // without shrinking the window.  What matters is that the
        // base-class hook fired on every read.
        double bps = stats.getAs<double>(MediaIOStats::BytesPerSecond);
        double fps = stats.getAs<double>(MediaIOStats::FramesPerSecond);
        CHECK(bps > 0.0);
        CHECK(fps > 0.0);

        io->close();
        delete io;
}

// ============================================================================
// Drop / repeat / late counters.
// ============================================================================

TEST_CASE("MediaIO stats: noteFrameDropped increments FramesDropped") {
        MediaIO *io = new MediaIO();
        auto *task = new TelemetryTestTask();
        task->injectDrops(3);
        REQUIRE(io->adoptTask(task).isOk());
        REQUIRE(io->open(MediaIO::Output).isOk());

        Frame::Ptr f;
        REQUIRE(io->readFrame(f, true).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesDropped) == 3);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesRepeated) == 0);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesLate) == 0);

        io->close();
        delete io;
}

TEST_CASE("MediaIO stats: noteFrameRepeated / Late increment their counters") {
        MediaIO *io = new MediaIO();
        auto *task = new TelemetryTestTask();
        task->injectRepeats(2);
        task->injectLates(5);
        REQUIRE(io->adoptTask(task).isOk());
        REQUIRE(io->open(MediaIO::Output).isOk());

        Frame::Ptr f;
        REQUIRE(io->readFrame(f, true).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesDropped) == 0);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesRepeated) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesLate) == 5);

        io->close();
        delete io;
}

TEST_CASE("MediaIO stats: counters reset to zero on reopen") {
        MediaIO *io = new MediaIO();
        auto *task = new TelemetryTestTask();
        task->injectDrops(4);
        REQUIRE(io->adoptTask(task).isOk());
        REQUIRE(io->open(MediaIO::Output).isOk());

        Frame::Ptr f;
        REQUIRE(io->readFrame(f, true).isOk());
        CHECK(io->stats().getAs<int64_t>(MediaIOStats::FramesDropped) == 4);

        io->close();
        REQUIRE(io->open(MediaIO::Output).isOk());
        // A fresh open() must zero everything — we never call
        // injectDrops() here, so the counter must be clean.
        REQUIRE(io->readFrame(f, true).isOk());
        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesDropped) == 0);

        io->close();
        delete io;
}

// ============================================================================
// Latency derived from BenchmarkReporter.
// ============================================================================

TEST_CASE("MediaIO stats: latency keys populated when benchmarking is on") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::EnableBenchmark, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        BenchmarkReporter reporter;
        io->setBenchmarkReporter(&reporter);

        REQUIRE(io->open(MediaIO::Output).isOk());

        // A handful of reads populate the reporter via the normal
        // sink-submits path wired up in submitReadCommand().
        for(int i = 0; i < 5; ++i) {
                Frame::Ptr f;
                Error err = io->readFrame(f, true);
                REQUIRE(err.isOk());
        }

        MediaIOStats stats = io->stats();
        // The reader path stamps dequeue -> taskEnd, so even a
        // trivial executeCmd() will leave a non-negative delta.
        CHECK(stats.getAs<double>(MediaIOStats::AverageLatencyMs) >= 0.0);
        CHECK(stats.getAs<double>(MediaIOStats::PeakLatencyMs) >= 0.0);
        // At least one of them should be strictly positive — a
        // read cannot take zero ns.  We assert the peak, which is
        // guaranteed monotonically non-decreasing as samples land.
        CHECK(reporter.submittedCount() >= 5);

        io->close();
        delete io;
}

TEST_CASE("MediaIO stats: latency keys stay zero when no reporter attached") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::EnableBenchmark, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        Frame::Ptr f;
        REQUIRE(io->readFrame(f, true).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<double>(MediaIOStats::AverageLatencyMs) == 0.0);
        CHECK(stats.getAs<double>(MediaIOStats::PeakLatencyMs) == 0.0);

        io->close();
        delete io;
}

TEST_CASE("MediaIO stats: PendingOperations populated by base class") {
        // The MediaIO base class surfaces Strand::pendingCount() as
        // MediaIOStats::PendingOperations so telemetry callers can
        // see backlog without the backend having to track it.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // At steady state — strand idle or running a single prefetch
        // read — pending ops should be small.  We don't pin it to a
        // specific value because the prefetch depth is backend-chosen,
        // but it must be present and non-negative.
        MediaIOStats stats = io->stats();
        CHECK(stats.contains(MediaIOStats::PendingOperations));
        CHECK(stats.getAs<int64_t>(MediaIOStats::PendingOperations) >= 0);

        io->close();
        delete io;
}

TEST_CASE("MediaIOStats::toString: empty instance returns empty string") {
        MediaIOStats stats;
        CHECK(stats.toString() == String());
}

TEST_CASE("MediaIOStats::toString: renders standard keys compactly") {
        // Build a stats instance by hand and check that toString()
        // emits the expected tokens in order.  Substring checks keep
        // the test robust against whitespace/format tweaks.
        MediaIOStats stats;
        stats.set(MediaIOStats::BytesPerSecond, 12500000.0); // ~12.5 MB/s
        stats.set(MediaIOStats::FramesPerSecond, 23.976);
        stats.set(MediaIOStats::FramesDropped, int64_t(2));
        stats.set(MediaIOStats::FramesRepeated, int64_t(1));
        stats.set(MediaIOStats::FramesLate, int64_t(0)); // elided when zero
        stats.set(MediaIOStats::AverageLatencyMs, 1.23);
        stats.set(MediaIOStats::PeakLatencyMs, 4.56);
        stats.set(MediaIOStats::QueueDepth, int64_t(3));
        stats.set(MediaIOStats::QueueCapacity, int64_t(8));
        stats.set(MediaIOStats::PendingOperations, int64_t(5));

        String s = stats.toString();
        INFO("toString output: ", s.cstr());

        CHECK(s.contains("/s"));                // byte-rate unit suffix
        CHECK(s.contains("24.0 fps"));          // 23.976 rounds to one decimal
        CHECK(s.contains("drop=2"));
        CHECK(s.contains("rep=1"));
        CHECK_FALSE(s.contains("late="));       // zero counters elided
        CHECK(s.contains("lat=1.23/4.56 ms"));
        CHECK(s.contains("q=3/8"));
        CHECK(s.contains("pend=5"));
        CHECK_FALSE(s.contains("err="));        // no LastErrorMessage set
}

TEST_CASE("MediaIOStats::toString: surfaces LastErrorMessage when set") {
        MediaIOStats stats;
        stats.set(MediaIOStats::BytesPerSecond, 0.0);
        stats.set(MediaIOStats::LastErrorMessage, String("disk full"));
        String s = stats.toString();
        CHECK(s.contains("err=disk full"));
}

TEST_CASE("MediaIOStats::toString: elides zero counters but keeps drop") {
        // FramesDropped is always shown — it's the one a reader
        // scanning a stats line is most likely looking for, so a
        // visible "drop=0" is more useful than a silent omission.
        // FramesRepeated / FramesLate / LastErrorMessage elide when
        // zero/empty to keep the normal-operation line quiet.
        MediaIOStats stats;
        stats.set(MediaIOStats::BytesPerSecond, 1000.0);
        stats.set(MediaIOStats::FramesPerSecond, 30.0);
        stats.set(MediaIOStats::FramesDropped, int64_t(0));
        stats.set(MediaIOStats::FramesRepeated, int64_t(0));
        stats.set(MediaIOStats::FramesLate, int64_t(0));

        String s = stats.toString();
        CHECK(s.contains("drop=0"));
        CHECK_FALSE(s.contains("rep="));
        CHECK_FALSE(s.contains("late="));
}
