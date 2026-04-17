/**
 * @file      sdlaudioclock/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Functional test for SDLAudioClock.
 *
 * Opens the default SDL audio device, plays silence through it for a
 * configurable duration, and samples the clock's reported time at a
 * rate well below the device callback period.  The point is to
 * exercise the real SDL audio path — hardware callback cadence,
 * driver latency, the actual drift characteristics of the output
 * device — rather than the deterministic stand-in used by the
 * unittest-sdl build.
 *
 * Usage:
 *   sdlaudioclock-functest [seconds]
 *
 * Defaults to 10 seconds.  Exits non-zero if the clock misbehaves
 * (long stall, large regression, or rate drift outside a loose
 * envelope) so the program can be chained into a CI workflow on a
 * machine that has audio hardware available.
 */

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/logger.h>
#include <promeki/sdl/sdlaudioclock.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

// Stream format.  48 kHz stereo float is the most common real-world
// configuration and exercises the bytes-per-sec math the clock uses.
constexpr float        kSampleRate   = 48000.0f;
constexpr unsigned int kChannels     = 2;

// One push buffer of silence.  Roughly 50 ms — safely longer than
// any callback period we'd expect from a real backend, so the
// device never runs dry between feeder wake-ups.
constexpr size_t  kChunkSamples        = 2400;

// Feeder wake interval.  Half the chunk length leaves generous
// headroom against scheduling jitter.
constexpr int64_t kFeedIntervalMicros  = 25000;

// nowNs() poll cadence.  ~2 kHz — far denser than any reasonable
// SDL callback rate, so the sampler will see interpolation at work
// between consumed-byte updates.
constexpr int64_t kPollIntervalMicros  = 500;

// Pass/fail envelopes.
//
// - Stall: a prolonged plateau in the reported time means
//   interpolation has stopped.  Must stay well below the SDL
//   callback period.
// - Regressions: nowNs() must never report backwards.  Any
//   downward step is a contract violation — downstream consumers
//   (media-rate converters, sleep-until loops, timestamp
//   generators) assume monotonic time and will misbehave when the
//   clock walks back, even briefly.
// - Rate drift: over the run, the clock should track wall time
//   within a few percent.
constexpr int64_t kMaxAllowedStallMs   = 5;
constexpr double  kMaxAllowedRateDrift = 0.05;  // 5%

struct Stats {
        int64_t samples         = 0;
        int64_t stepCount       = 0;
        int64_t regressions     = 0;
        int64_t maxStallNs      = 0;
        int64_t minStepNs       = INT64_MAX;
        int64_t maxStepNs       = 0;
        int64_t sumStepNs       = 0;
        int64_t maxRegressionNs = 0;
};

// Keeps the stream fed with zeros so the device's consumed-byte
// counter stays live and the clock has something to derive time
// from.  Any underrun briefly stalls SDLAudioClock because
// consumed stops advancing — deliberately pushing well ahead of
// the drain rate prevents that from contaminating the measurement.
void feedSilence(SDLAudioOutput *out, std::atomic<bool> *running) {
        const AudioDesc desc = out->desc();
        while(running->load(std::memory_order_relaxed)) {
                Audio silence(desc, kChunkSamples);
                silence.resize(kChunkSamples);
                std::memset(silence.data<float>(), 0,
                            kChunkSamples * desc.channels() * sizeof(float));
                out->pushAudio(silence);
                std::this_thread::sleep_for(
                        std::chrono::microseconds(kFeedIntervalMicros));
        }
}

} // namespace

int main(int argc, char **argv) {
        int durationSecs = 10;
        if(argc > 1) {
                int v = std::atoi(argv[1]);
                if(v > 0) durationSecs = v;
        }

        if(!SDL_Init(SDL_INIT_AUDIO)) {
                std::fprintf(stderr, "SDL_Init(SDL_INIT_AUDIO) failed: %s\n",
                             SDL_GetError());
                return 1;
        }

        SDLAudioOutput output;
        if(!output.configure(AudioDesc(kSampleRate, kChannels))) {
                std::fprintf(stderr, "SDLAudioOutput::configure() failed\n");
                SDL_Quit();
                return 1;
        }
        if(!output.open()) {
                std::fprintf(stderr, "SDLAudioOutput::open() failed\n");
                SDL_Quit();
                return 1;
        }

        const double bytesPerSec =
                (double)kSampleRate * (double)kChannels * (double)sizeof(float);
        SDLAudioClock clock(&output, bytesPerSec, String("functest"));

        std::atomic<bool> running{true};
        std::thread feeder(feedSilence, &output, &running);

        // Wait for SDL to actually start consuming before we begin
        // sampling.  A cold audio stack can take several hundred
        // milliseconds to deliver its first callback; polling until
        // the clock reports a non-zero value is far more robust
        // than a fixed sleep.  Cap the wait so a broken device
        // doesn't hang the test.
        {
                const int64_t kMaxWarmupMs = 2000;
                int64_t warmupStart = TimeStamp::now().nanoseconds();
                while(clock.nowNs() == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        if((TimeStamp::now().nanoseconds() - warmupStart)
                           / 1000000LL >= kMaxWarmupMs) {
                                std::fprintf(stderr,
                                             "warmup: clock never advanced past zero — "
                                             "audio device never started feeding back\n");
                                break;
                        }
                }
        }

        std::printf("sdlaudioclock-functest: playing silence for %d s...\n",
                    durationSecs);

        const int64_t durationMs   = (int64_t)durationSecs * 1000;
        const int64_t startWallNs  = TimeStamp::now().nanoseconds();
        const int64_t startClockNs = clock.nowNs();

        Stats s;
        int64_t lastClockNs    = startClockNs;
        int64_t stallStartWall = startWallNs;

        while(true) {
                std::this_thread::sleep_for(
                        std::chrono::microseconds(kPollIntervalMicros));
                int64_t wallNs = TimeStamp::now().nanoseconds();
                if((wallNs - startWallNs) / 1000000LL >= durationMs) break;

                int64_t clkNs = clock.nowNs();
                ++s.samples;

                if(clkNs < lastClockNs) {
                        ++s.regressions;
                        int64_t drop = lastClockNs - clkNs;
                        if(drop > s.maxRegressionNs) s.maxRegressionNs = drop;
                        lastClockNs = clkNs;
                        stallStartWall = wallNs;
                        continue;
                }
                if(clkNs == lastClockNs) {
                        int64_t stall = wallNs - stallStartWall;
                        if(stall > s.maxStallNs) s.maxStallNs = stall;
                } else {
                        int64_t step = clkNs - lastClockNs;
                        if(step < s.minStepNs) s.minStepNs = step;
                        if(step > s.maxStepNs) s.maxStepNs = step;
                        s.sumStepNs += step;
                        ++s.stepCount;
                        lastClockNs = clkNs;
                        stallStartWall = wallNs;
                }
        }

        running.store(false, std::memory_order_relaxed);
        feeder.join();

        const int64_t endWallNs  = TimeStamp::now().nanoseconds();
        const int64_t endClockNs = clock.nowNs();
        const double  wallSecs   = (double)(endWallNs  - startWallNs ) / 1e9;
        const double  clkSecs    = (double)(endClockNs - startClockNs) / 1e9;
        const double  ratio      = wallSecs > 0.0 ? clkSecs / wallSecs : 0.0;
        const double  rateRatio  = clock.rateRatio();

        output.close();
        SDL_Quit();

        const int64_t maxStallMs   = s.maxStallNs      / 1000000LL;

        std::printf("\n=== SDLAudioClock functional test report ===\n");
        std::printf("duration_wall:    %.6f s\n", wallSecs);
        std::printf("duration_clock:   %.6f s\n", clkSecs);
        std::printf("clock / wall:     %.6f\n",   ratio);
        std::printf("rateRatio():      %.6f\n",   rateRatio);
        std::printf("samples:          %lld\n",   (long long)s.samples);
        std::printf("forward steps:    %lld\n",   (long long)s.stepCount);
        std::printf("min step  (ns):   %lld\n",
                    (long long)(s.stepCount ? s.minStepNs : 0));
        std::printf("avg step  (ns):   %lld\n",
                    (long long)(s.stepCount ? s.sumStepNs / s.stepCount : 0));
        std::printf("max step  (ns):   %lld\n",   (long long)s.maxStepNs);
        std::printf("max stall (ns):   %lld  (%lld ms)\n",
                    (long long)s.maxStallNs, (long long)maxStallMs);
        std::printf("regressions:      %lld\n",   (long long)s.regressions);
        std::printf("max regress(ns):  %lld  (%lld ms)\n",
                    (long long)s.maxRegressionNs,
                    (long long)(s.maxRegressionNs / 1000000LL));

        const SDLAudioClock::Stats cs = clock.stats();
        std::printf("--- internal SDLAudioClock stats ---\n");
        std::printf("updateCount:      %lld\n",   (long long)cs.updateCount);
        std::printf("resyncs:          %lld\n",   (long long)cs.checkpointResyncs);
        std::printf("forward snaps:    %lld\n",   (long long)cs.forwardSnaps);
        std::printf("back-dates:       %lld\n",   (long long)cs.backDates);
        std::printf("clamped regs:     %lld\n",   (long long)cs.clampedRegressions);
        std::printf("max step (ns):    %lld\n",   (long long)cs.maxStepNs);
        std::printf("max back-date:    %lld ns (%lld ms)\n",
                    (long long)cs.maxBackDateNs,
                    (long long)(cs.maxBackDateNs / 1000000LL));
        std::printf("max cb gap (ns):  %lld ns (%lld ms)\n",
                    (long long)cs.maxCallbackGapNs,
                    (long long)(cs.maxCallbackGapNs / 1000000LL));
        std::printf("=============================================\n");

        bool pass = true;
        if(maxStallMs > kMaxAllowedStallMs) {
                std::fprintf(stderr, "FAIL: max stall %lld ms exceeds limit %lld ms\n",
                             (long long)maxStallMs,
                             (long long)kMaxAllowedStallMs);
                pass = false;
        }
        if(s.regressions > 0) {
                std::fprintf(stderr,
                             "FAIL: clock reported backwards %lld times "
                             "(max %lld ns / %lld ms) — must be zero\n",
                             (long long)s.regressions,
                             (long long)s.maxRegressionNs,
                             (long long)(s.maxRegressionNs / 1000000LL));
                pass = false;
        }
        if(cs.clampedRegressions > 0) {
                // The external monotonicity check already catches
                // real backward steps; this fires when the internal
                // clamp had to rescue the back-date arithmetic from
                // floating-point rounding.  Non-zero means the math
                // has a reproducible rounding bug even if the
                // contract is still held.
                std::fprintf(stderr,
                             "FAIL: internal clamp tripped %lld times "
                             "— back-date FP rounding regression\n",
                             (long long)cs.clampedRegressions);
                pass = false;
        }
        if(wallSecs > 0.0 && (ratio < 1.0 - kMaxAllowedRateDrift ||
                              ratio > 1.0 + kMaxAllowedRateDrift)) {
                std::fprintf(stderr, "FAIL: clock/wall ratio %.6f outside [%.3f, %.3f]\n",
                             ratio,
                             1.0 - kMaxAllowedRateDrift,
                             1.0 + kMaxAllowedRateDrift);
                pass = false;
        }
        if(s.stepCount == 0) {
                std::fprintf(stderr, "FAIL: no forward steps observed\n");
                pass = false;
        }

        std::printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
        return pass ? 0 : 1;
}
