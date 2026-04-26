/**
 * @file      framesync/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Functional test for FrameSync end-to-end.
 *
 * Drives a real capture source — V4L2 by default — directly into a
 * @ref FrameSync paced by a @ref WallClock, and discards the pulled
 * output after accounting.  No SDL window or audio device is opened:
 * the point is to exercise FrameSync against hardware-shaped source
 * jitter (real V4L2 dequeue cadence, real audio timestamp deltas)
 * without pulling in the SDL display stack.
 *
 * The source backend name is a CLI argument so other capture
 * MediaIOs can be swapped in as they come online — the test proper
 * does not hard-code anything V4L2-specific beyond mapping
 * @c --device onto @ref MediaConfig::V4l2DevicePath.  Any future
 * capture backend just needs its own mapping block (or a generic
 * @c --sc Key:Value pass-through) in @ref configureSource.
 *
 * Usage:
 *   framesync-functest [--backend NAME] [--device PATH] [--seconds N]
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <promeki/audiodesc.h>
#include <promeki/clock.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/framesync.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/string.h>

using namespace promeki;

namespace {

        struct Options {
                        String backend = String("V4L2");
                        String device = String("/dev/video0");
                        double seconds = 10.0;
                        bool   verbose = false;
        };

        void usage() {
                std::fprintf(stderr, "Usage: framesync-functest [OPTIONS]\n"
                                     "\n"
                                     "  --backend NAME     MediaIO source backend (default V4L2).\n"
                                     "                     Any MediaIO registry entry that acts\n"
                                     "                     as a frame source works here — the\n"
                                     "                     test does not assume V4L2.\n"
                                     "  --device PATH      V4L2 device node (default /dev/video0).\n"
                                     "                     Mapped to MediaConfig::V4l2DevicePath\n"
                                     "                     when the V4L2 backend is selected;\n"
                                     "                     ignored for other backends.\n"
                                     "  --seconds N        Run time in seconds (default 10).\n"
                                     "  --verbose          Raise logger to Debug so FrameSync's\n"
                                     "                     periodic drop/repeat/drift log is\n"
                                     "                     emitted while the test runs.\n"
                                     "  -h, --help         Show this help.\n");
        }

        bool parseOptions(int argc, char **argv, Options &o) {
                for (int i = 1; i < argc; ++i) {
                        String a(argv[i]);
                        if (a == "-h" || a == "--help") {
                                usage();
                                std::exit(0);
                        }
                        auto needValue = [&](const String &flag) {
                                if (i + 1 >= argc) {
                                        std::fprintf(stderr, "Error: %s expects an argument\n", flag.cstr());
                                        return false;
                                }
                                return true;
                        };
                        if (a == "--backend") {
                                if (!needValue(a)) return false;
                                o.backend = argv[++i];
                                continue;
                        }
                        if (a == "--device") {
                                if (!needValue(a)) return false;
                                o.device = argv[++i];
                                continue;
                        }
                        if (a == "--seconds") {
                                if (!needValue(a)) return false;
                                o.seconds = std::atof(argv[++i]);
                                if (o.seconds <= 0.0) {
                                        std::fprintf(stderr, "Error: --seconds must be > 0\n");
                                        return false;
                                }
                                continue;
                        }
                        if (a == "--verbose") {
                                o.verbose = true;
                                continue;
                        }
                        std::fprintf(stderr, "Error: unknown option '%s'\n", a.cstr());
                        return false;
                }
                return true;
        }

        // Applies any backend-specific config before the source is opened.
        // Today this is a single switch on backend name; when more capture
        // backends come online each one can plug its device-path mapping
        // in here without touching the rest of the test.
        void configureSource(MediaIO *io, const Options &o) {
                MediaIO::Config cfg = io->config();
                if (o.backend == String("V4L2")) {
                        cfg.set(MediaConfig::V4l2DevicePath, o.device);
                }
                io->setConfig(cfg);
        }

} // namespace

int main(int argc, char **argv) {
        Options opts;
        if (!parseOptions(argc, argv, opts)) return 1;

        if (opts.verbose) {
                Logger::defaultLogger().setLogLevel(Logger::LogLevel::Debug);
        }

        MediaIO::Config srcCfg = MediaIO::defaultConfig(opts.backend);
        srcCfg.set(MediaConfig::Type, opts.backend);
        MediaIO *source = MediaIO::create(srcCfg);
        if (source == nullptr) {
                std::fprintf(stderr, "Error: failed to create MediaIO backend '%s'\n", opts.backend.cstr());
                return 1;
        }
        configureSource(source, opts);

        Error err = source->open(MediaIO::Source);
        if (err.isError()) {
                std::fprintf(stderr, "Source open failed: %s\n", err.name().cstr());
                delete source;
                return 1;
        }

        const MediaDesc mdesc = source->mediaDesc();
        const AudioDesc adesc = source->audioDesc();
        if (mdesc.imageList().isEmpty()) {
                std::fprintf(stderr, "Error: source has no video — FrameSync smoke test "
                                     "requires a video stream\n");
                source->close();
                delete source;
                return 1;
        }

        const ImageDesc &id0 = mdesc.imageList()[0];
        std::printf("Source:  %s  %s @ %s fps\n", opts.backend.cstr(), id0.toString().cstr(),
                    mdesc.frameRate().toString().cstr());
        if (adesc.isValid()) {
                std::printf("  Audio: %s\n", adesc.toString().cstr());
        } else {
                std::printf("  Audio: (none)\n");
        }

        // FrameSync paced by the system monotonic wall clock.  Using
        // WallClock (instead of SyntheticClock) keeps the test
        // honest: pull deadlines are real time, so if FrameSync
        // miscounts or its pull loop busy-waits the regression will
        // show up in the frames-per-second math below.
        Clock::Ptr clock = Clock::Ptr::takeOwnership(new WallClock());
        FrameSync  sync(String("functest"));
        sync.setTargetFrameRate(mdesc.frameRate());
        if (adesc.isValid()) sync.setTargetAudioDesc(adesc);
        sync.setClock(clock);
        sync.reset();

        std::atomic<bool>     running{true};
        std::atomic<uint64_t> framesIn{0};
        std::atomic<uint64_t> readErrors{0};
        std::atomic<uint64_t> pushErrors{0};
        std::atomic<uint64_t> framesOut{0};
        std::atomic<uint64_t> pullErrors{0};
        std::atomic<uint64_t> emptyPulls{0};

        // Producer: blocking readFrame on the source feeds FrameSync
        // with real capture cadence.  No overflow handling here —
        // FrameSync is in its default DropOldest mode, so producer
        // bursts collapse into overflow drops rather than back-pressure.
        std::thread producer([&]() {
                while (running.load(std::memory_order_relaxed)) {
                        Frame::Ptr frame;
                        Error      e = source->readFrame(frame, true);
                        if (e == Error::Cancelled || e == Error::EndOfFile) break;
                        // TryAgain is the V4L2 task's 200 ms internal
                        // timeout reaching us through block=true reads
                        // — not a real error, just "no frame yet on a
                        // slow / stalling camera."  Retry silently.
                        if (e == Error::TryAgain) continue;
                        if (e.isError()) {
                                readErrors.fetch_add(1, std::memory_order_relaxed);
                                std::fprintf(stderr, "Source read error: %s\n", e.name().cstr());
                                continue;
                        }
                        framesIn.fetch_add(1, std::memory_order_relaxed);
                        Error pe = sync.pushFrame(frame);
                        if (pe.isError()) {
                                pushErrors.fetch_add(1, std::memory_order_relaxed);
                                std::fprintf(stderr, "Sync push error: %s\n", pe.name().cstr());
                        }
                }
                sync.pushEndOfStream();
        });

        // Consumer: pulls one frame per WallClock deadline and
        // discards it.  The pull itself is the work being exercised;
        // dropping the output is deliberate — we want FrameSync's
        // behaviour in isolation, not whatever a sink would do.
        std::thread consumer([&]() {
                int64_t pullIdx = 0;
                while (running.load(std::memory_order_relaxed)) {
                        Result<FrameSync::PullResult> r = sync.pullFrame();
                        const Error                  &e = r.second();
                        if (e.isError()) {
                                // Interrupt and Cancelled are the
                                // orderly shutdown signals from
                                // sync.interrupt() and source cancel
                                // — drain out, don't count them.
                                if (e == Error::Cancelled || e == Error::Interrupt) break;
                                pullErrors.fetch_add(1, std::memory_order_relaxed);
                                std::fprintf(stderr, "Sync pull error: %s\n", e.name().cstr());
                                continue;
                        }
                        framesOut.fetch_add(1, std::memory_order_relaxed);
                        if (!r.first().frame) {
                                emptyPulls.fetch_add(1, std::memory_order_relaxed);
                        }
                        if (opts.verbose) {
                                std::printf("[pull %5lld] frameIndex=%lld "
                                            "repeated=%lld dropped=%lld errNs=%lld\n",
                                            (long long)pullIdx, (long long)r.first().frameIndex.value(),
                                            (long long)r.first().framesRepeated.value(),
                                            (long long)r.first().framesDropped.value(),
                                            (long long)r.first().error.nanoseconds());
                                std::fflush(stdout);
                        }
                        ++pullIdx;
                }
        });

        const auto startWall = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)(opts.seconds * 1000.0)));
        const auto endWall = std::chrono::steady_clock::now();

        // Shut down: stop the source first so producer unblocks,
        // then interrupt the consumer's clock sleep.  Join in order.
        running.store(false, std::memory_order_relaxed);
        source->cancelPending();
        if (producer.joinable()) producer.join();
        sync.interrupt();
        if (consumer.joinable()) consumer.join();

        source->close();
        delete source;

        const double wallSecs = std::chrono::duration<double>(endWall - startWall).count();

        std::printf("\n=== FrameSync functional test report ===\n");
        std::printf("backend:           %s\n", opts.backend.cstr());
        std::printf("duration_wall:     %.3f s\n", wallSecs);
        std::printf("frames pushed:     %llu\n", (unsigned long long)framesIn.load());
        std::printf("frames pulled:     %llu\n", (unsigned long long)framesOut.load());
        std::printf("empty pulls:       %llu\n", (unsigned long long)emptyPulls.load());
        std::printf("read errors:       %llu\n", (unsigned long long)readErrors.load());
        std::printf("push errors:       %llu\n", (unsigned long long)pushErrors.load());
        std::printf("pull errors:       %llu\n", (unsigned long long)pullErrors.load());
        std::printf("-- FrameSync --\n");
        std::printf("  framesIn:        %lld\n", (long long)sync.framesIn().value());
        std::printf("  framesOut:       %lld\n", (long long)sync.framesOut().value());
        std::printf("  framesRepeated:  %lld\n", (long long)sync.framesRepeated().value());
        std::printf("  framesDropped:   %lld\n", (long long)sync.framesDropped().value());
        std::printf("  overflowDrops:   %lld\n", (long long)sync.overflowDrops().value());
        std::printf("  srcVideoRateHz:  %.3f\n", sync.currentSourceVideoRate());
        std::printf("  srcAudioRateHz:  %.3f\n", sync.currentSourceAudioRate());
        std::printf("  resampleRatio:   %.6f\n", sync.currentResampleRatio());
        std::printf("  accumErrorNs:    %lld\n", (long long)sync.accumulatedError().nanoseconds());
        std::printf("=========================================\n");

        bool pass = true;
        if (framesIn.load() == 0) {
                std::fprintf(stderr, "FAIL: no frames read from source\n");
                pass = false;
        }
        if (framesOut.load() == 0) {
                std::fprintf(stderr, "FAIL: FrameSync produced no output frames\n");
                pass = false;
        }
        if (readErrors.load() > 0) {
                std::fprintf(stderr, "FAIL: %llu source read errors\n", (unsigned long long)readErrors.load());
                pass = false;
        }
        if (pushErrors.load() > 0) {
                std::fprintf(stderr, "FAIL: %llu FrameSync push errors\n", (unsigned long long)pushErrors.load());
                pass = false;
        }
        if (pullErrors.load() > 0) {
                std::fprintf(stderr, "FAIL: %llu FrameSync pull errors\n", (unsigned long long)pullErrors.load());
                pass = false;
        }

        // At WallClock pacing the consumer should emit roughly
        // framesOut ≈ framePeriod × wallSecs.  A very loose envelope
        // catches catastrophic miscounts without flagging a couple
        // of dropped deadlines as a failure.
        const double expectedOut = mdesc.frameRate().toDouble() * wallSecs;
        if (expectedOut > 0.0) {
                const double ratio = (double)framesOut.load() / expectedOut;
                if (ratio < 0.5 || ratio > 1.5) {
                        std::fprintf(stderr,
                                     "FAIL: framesOut=%llu outside half/double "
                                     "envelope around expected %.1f\n",
                                     (unsigned long long)framesOut.load(), expectedOut);
                        pass = false;
                }
        }

        std::printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
        return pass ? 0 : 1;
}
