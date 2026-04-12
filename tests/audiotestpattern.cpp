/**
 * @file      tests/audiotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/audiotestpattern.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/audiolevel.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/timecode.h>

using namespace promeki;

namespace {

// Helper: peak amplitude of a single channel of an interleaved Audio.
float audioPeakChannel(const Audio &audio, int channel) {
        const float *data = audio.data<float>();
        if(data == nullptr) return 0.0f;
        const size_t nch = audio.desc().channels();
        const size_t n   = audio.samples();
        float peak = 0.0f;
        for(size_t s = 0; s < n; s++) {
                float v = data[s * nch + channel];
                if(v < 0) v = -v;
                if(v > peak) peak = v;
        }
        return peak;
}

// Helper: estimate dominant frequency of a channel via zero-crossings.
// Good enough for the single-tone tests in this file.
double estimateFrequencyHz(const Audio &audio, int channel, double sampleRate) {
        const float *data = audio.data<float>();
        if(data == nullptr) return 0.0;
        const size_t nch = audio.desc().channels();
        const size_t n   = audio.samples();
        if(n < 2) return 0.0;

        size_t crossings = 0;
        float prev = data[channel];
        for(size_t s = 1; s < n; s++) {
                float cur = data[s * nch + channel];
                if((prev < 0 && cur >= 0) || (prev >= 0 && cur < 0)) {
                        crossings++;
                }
                prev = cur;
        }
        // Two zero crossings per cycle for a sine.
        const double cycles  = static_cast<double>(crossings) / 2.0;
        const double seconds = static_cast<double>(n - 1) / sampleRate;
        return (seconds > 0.0) ? (cycles / seconds) : 0.0;
}

EnumList makeModes(std::initializer_list<AudioPattern> list) {
        EnumList modes = EnumList::forType<AudioPattern>();
        for(auto mode : list) modes.append(mode);
        return modes;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AudioTestPattern_Defaults") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        CHECK(gen.channelModes().isValid());
        CHECK(gen.channelModes().isEmpty());
        CHECK(gen.toneFrequency() == doctest::Approx(1000.0));
}

// ============================================================================
// Tone on every channel
// ============================================================================

TEST_CASE("AudioTestPattern_Tone") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Tone, AudioPattern::Tone}));
        gen.setToneFrequency(1000.0);
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(4800);
        REQUIRE(audio.isValid());
        CHECK(audio.samples() == 4800);

        // Both channels have a non-zero sine waveform.
        CHECK(audioPeakChannel(audio, 0) > 0.01f);
        CHECK(audioPeakChannel(audio, 1) > 0.01f);
}

// ============================================================================
// Silence on every channel
// ============================================================================

TEST_CASE("AudioTestPattern_Silence") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Silence}));
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(1000);
        REQUIRE(audio.isValid());
        CHECK(audio.samples() == 1000);
        CHECK(audioPeakChannel(audio, 0) == doctest::Approx(0.0f));
}

// ============================================================================
// Short mode list silences the trailing channels
// ============================================================================

TEST_CASE("AudioTestPattern_ShortListSilencesTail") {
        AudioDesc desc(48000, 4);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Tone, AudioPattern::Tone}));
        gen.setToneFrequency(1000.0);
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(4800);
        REQUIRE(audio.isValid());
        // Channels 0 and 1 carry tone...
        CHECK(audioPeakChannel(audio, 0) > 0.01f);
        CHECK(audioPeakChannel(audio, 1) > 0.01f);
        // ...channels 2 and 3 are silent (mode list ran out).
        CHECK(audioPeakChannel(audio, 2) == doctest::Approx(0.0f));
        CHECK(audioPeakChannel(audio, 3) == doctest::Approx(0.0f));
}

// ============================================================================
// Empty mode list silences every channel
// ============================================================================

TEST_CASE("AudioTestPattern_EmptyModeListAllSilent") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        // Channel list is empty but valid — every channel silenced.
        gen.setChannelModes(EnumList::forType<AudioPattern>());
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(1000);
        REQUIRE(audio.isValid());
        CHECK(audioPeakChannel(audio, 0) == doctest::Approx(0.0f));
        CHECK(audioPeakChannel(audio, 1) == doctest::Approx(0.0f));
}

// ============================================================================
// LTC on a single channel, silence on the rest
// ============================================================================

TEST_CASE("AudioTestPattern_LTC") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::LTC, AudioPattern::Silence}));
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());

        Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
        Audio audio = gen.create(2000, tc);
        REQUIRE(audio.isValid());
        CHECK(audioPeakChannel(audio, 0) > 0.01f);
        CHECK(audioPeakChannel(audio, 1) == doctest::Approx(0.0f));
}

// ============================================================================
// render() into existing buffer
// ============================================================================

TEST_CASE("AudioTestPattern_RenderIntoExisting") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Tone}));
        gen.setToneFrequency(440.0);
        REQUIRE(gen.configure().isOk());

        Audio audio(desc, 480);
        audio.zero();
        gen.render(audio);

        CHECK(audioPeakChannel(audio, 0) > 0.0f);
}

// ============================================================================
// AvSync — tone burst on tc.frame() == 0, silence otherwise
// ============================================================================

TEST_CASE("AudioTestPattern_AvSync") {
        AudioDesc desc(48000.0f, 2);
        AudioTestPattern gen(desc);
        // LTC on ch0, AvSync click on ch1 — matches the default TPG
        // configuration from MediaIOTask_TPG::formatDesc().
        gen.setChannelModes(makeModes({AudioPattern::LTC, AudioPattern::AvSync}));
        gen.setToneFrequency(1000.0);
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());

        const size_t samples = 1600;  // ~one frame at 48k / 30 fps

        // Marker frame: ch1 carries the click marker, ch0 carries LTC.
        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);
        REQUIRE(marker.isValid());
        Audio burst = gen.create(samples, marker);
        REQUIRE(burst.isValid());
        CHECK(audioPeakChannel(burst, 0) > 0.05f);
        CHECK(audioPeakChannel(burst, 1) > 0.05f);

        // Non-marker frame: ch1 is silent, ch0 still has LTC.
        Timecode nonMarker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 5);
        Audio quiet = gen.create(samples, nonMarker);
        REQUIRE(quiet.isValid());
        CHECK(audioPeakChannel(quiet, 1) == doctest::Approx(0.0f));
        CHECK(audioPeakChannel(quiet, 0) > 0.05f);

        // Invalid timecode: both the click and the LTC fall through to
        // silence (the encoder can't encode without a format).
        Audio fallback = gen.create(samples, Timecode());
        REQUIRE(fallback.isValid());
        CHECK(audioPeakChannel(fallback, 0) == doctest::Approx(0.0f));
        CHECK(audioPeakChannel(fallback, 1) == doctest::Approx(0.0f));
}

// ============================================================================
// SrcProbe — 997 Hz reference tone
// ============================================================================

TEST_CASE("AudioTestPattern_SrcProbe") {
        const double sampleRate = 48000.0;
        AudioDesc desc(static_cast<float>(sampleRate), 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::SrcProbe}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        REQUIRE(gen.configure().isOk());

        // One second of audio so the frequency estimator has lots of
        // cycles to work with.
        Audio audio = gen.create(static_cast<size_t>(sampleRate));
        REQUIRE(audio.isValid());
        CHECK(audioPeakChannel(audio, 0) > 0.01f);

        double freq = estimateFrequencyHz(audio, 0, sampleRate);
        CHECK(freq == doctest::Approx(AudioTestPattern::kSrcProbeFrequencyHz)
                         .epsilon(0.005));
}

// ============================================================================
// ChannelId — per-channel unique frequency
// ============================================================================

TEST_CASE("AudioTestPattern_ChannelIdPerChannelFrequency") {
        const double sampleRate = 48000.0;
        AudioDesc desc(static_cast<float>(sampleRate), 3);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({
                AudioPattern::ChannelId,
                AudioPattern::ChannelId,
                AudioPattern::ChannelId,
        }));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        gen.setChannelIdBaseFreq(1000.0);
        gen.setChannelIdStepFreq(100.0);
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(static_cast<size_t>(sampleRate));
        REQUIRE(audio.isValid());

        // Each channel should carry the frequency that the static
        // formula predicts.  The zero-crossing estimator is only
        // accurate to ~0.5% so we use a matching tolerance.
        for(int ch = 0; ch < 3; ++ch) {
                double expected = AudioTestPattern::channelIdFrequency(ch, 1000.0, 100.0);
                double observed = estimateFrequencyHz(audio, ch, sampleRate);
                CHECK(observed == doctest::Approx(expected).epsilon(0.005));
        }
}

TEST_CASE("AudioTestPattern_ChannelIdFrequencyFormula") {
        CHECK(AudioTestPattern::channelIdFrequency(0, 1000.0, 100.0)
                == doctest::Approx(1000.0));
        CHECK(AudioTestPattern::channelIdFrequency(1, 1000.0, 100.0)
                == doctest::Approx(1100.0));
        CHECK(AudioTestPattern::channelIdFrequency(7, 500.0, 50.0)
                == doctest::Approx(850.0));
}

// ============================================================================
// WhiteNoise — cached buffer, reproducible across runs, non-constant
// ============================================================================

TEST_CASE("AudioTestPattern_WhiteNoiseBasic") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({
                AudioPattern::WhiteNoise, AudioPattern::WhiteNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-6.0));  // ~0.5 peak
        // Use a short buffer so the test stays fast and so the 4800
        // samples we pull below mostly land outside the loop-seam
        // crossfade region (~1/32 of the buffer).
        gen.setNoiseBufferSeconds(0.5);
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(4800);
        REQUIRE(audio.isValid());

        // Noise is non-zero and — outside the crossfade region —
        // individual samples stay within ±peak.  The equal-power
        // crossfade at the loop boundary blends two uncorrelated
        // streams, so individual samples in that region can reach up
        // to peak * √2 ≈ 0.707 in the worst case; we set the bound
        // high enough to tolerate that.
        const float peak0 = audioPeakChannel(audio, 0);
        const float peak1 = audioPeakChannel(audio, 1);
        CHECK(peak0 > 0.1f);
        CHECK(peak1 > 0.1f);
        CHECK(peak0 <= 0.72f);
        CHECK(peak1 <= 0.72f);
}

TEST_CASE("AudioTestPattern_WhiteNoiseReproducible") {
        // Two generators with identical config produce byte-identical
        // output — reproducibility is the whole point of the cached
        // buffer + fixed seed design.
        AudioDesc desc(48000, 1);
        auto makeNoise = []() {
                AudioDesc d(48000, 1);
                AudioTestPattern g(d);
                g.setChannelModes(makeModes({AudioPattern::WhiteNoise}));
                g.setToneLevel(AudioLevel::fromDbfs(-20.0));
                REQUIRE(g.configure().isOk());
                return g.create(1024);
        };
        Audio a = makeNoise();
        Audio b = makeNoise();
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        const float *pa = a.data<float>();
        const float *pb = b.data<float>();
        bool identical = true;
        for(size_t i = 0; i < a.samples(); ++i) {
                if(pa[i] != pb[i]) { identical = false; break; }
        }
        CHECK(identical);
}

TEST_CASE("AudioTestPattern_WhiteNoiseSeamlessLoop") {
        // Regression: the cached noise buffer must wrap seamlessly at
        // its end so a looping reader doesn't hear a click once per
        // buffer period.  We verify this by pulling enough audio to
        // cross a buffer boundary and checking that the sample-to-
        // sample step at the wrap point is in the same ballpark as
        // every other step.  Use a short buffer (0.1 s at 48 kHz =
        // 4800 samples) so the test runs quickly.
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::WhiteNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        gen.setNoiseBufferSeconds(0.1);
        REQUIRE(gen.configure().isOk());

        const size_t bufferLen = 4800;  // 0.1 s at 48 kHz
        const size_t extra     = 480;   // enough to cross the wrap
        Audio audio = gen.create(bufferLen + extra);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();

        // Compute the median |step| across the whole chunk as a
        // noise-floor reference — we don't need to be clever because
        // the step at a genuine click is wildly larger than the
        // typical draw-to-draw delta.
        double stepSum = 0.0;
        double maxStep = 0.0;
        for(size_t s = 1; s < audio.samples(); ++s) {
                const double step = std::fabs(data[s] - data[s - 1]);
                stepSum += step;
                if(step > maxStep) maxStep = step;
        }
        const double meanStep = stepSum / static_cast<double>(audio.samples() - 1);

        // The worst step across the whole chunk must not be wildly
        // larger than the mean.  For uniform [-0.1, +0.1] noise the
        // mean |step| is ≈0.066 and the worst step is ≤0.2.  A
        // discontinuity at the wrap boundary would look like a step
        // of ~0.2 (peak-to-peak), which the bound below still
        // tolerates — the point is to catch the pathological case of
        // the loop "jumping" by an order of magnitude larger than a
        // normal step, not to nail the exact continuity.
        CHECK(maxStep < meanStep * 10.0);
}

TEST_CASE("AudioTestPattern_WhiteNoiseAdvancesAcrossFrames") {
        // Regression: successive create() calls must read successive
        // regions of the cached noise buffer — not re-read the same
        // slice — so the audible stream actually sounds like noise
        // rather than a per-frame repeating tone.
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::WhiteNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());

        Audio a = gen.create(1600);
        Audio b = gen.create(1600);
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        const float *pa = a.data<float>();
        const float *pb = b.data<float>();

        // If the cursor is working, the second chunk is drawn from a
        // different part of the buffer, so the two chunks must
        // disagree on a significant fraction of samples.  The old
        // (buggy) behaviour returned byte-identical chunks.
        size_t different = 0;
        for(size_t s = 0; s < 1600; ++s) {
                if(pa[s] != pb[s]) different++;
        }
        CHECK(different > 1500);
}

TEST_CASE("AudioTestPattern_WhiteNoiseChannelsDecorrelate") {
        // Two WhiteNoise channels that share the cached buffer must
        // still be decorrelated: the per-channel offset shifts the
        // read position so the two channels don't emit identical
        // samples.
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({
                AudioPattern::WhiteNoise, AudioPattern::WhiteNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(2048);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();

        // Count samples where the two channels disagree — at least
        // half the samples should differ for a real decorrelation.
        size_t different = 0;
        for(size_t s = 0; s < audio.samples(); ++s) {
                if(data[s * 2 + 0] != data[s * 2 + 1]) different++;
        }
        CHECK(different > audio.samples() / 2);
}

// ============================================================================
// PinkNoise
// ============================================================================

TEST_CASE("AudioTestPattern_WhiteNoiseZeroMean") {
        // White noise must have zero DC bias — the cached buffer is
        // mean-subtracted in configure() so the realized sample mean
        // across the full buffer is effectively zero.  A nonzero DC
        // offset eats headroom and can cause clicks on fade-in /
        // fade-out at downstream stages, so we check the mean stays
        // well below -90 dBFS worth of level.
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::WhiteNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-6.0));
        gen.setNoiseBufferSeconds(1.0);
        REQUIRE(gen.configure().isOk());

        // Read a full buffer's worth so the measured mean covers the
        // whole cached period, including the crossfade region.
        Audio audio = gen.create(48000);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();

        double sum = 0.0;
        for(size_t s = 0; s < audio.samples(); ++s) sum += data[s];
        const double mean = sum / static_cast<double>(audio.samples());
        CHECK(std::fabs(mean) < 1.0e-4);
}

TEST_CASE("AudioTestPattern_PinkNoiseBasic") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::PinkNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));  // ~0.316 peak
        // Short buffer so the test is quick.  We read past the
        // crossfade region (fadeLen ≈ bufLen/32) so the reported
        // peak reflects raw pink noise rather than the (potentially
        // amplified) seam samples.
        gen.setNoiseBufferSeconds(0.5);
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(8192);
        REQUIRE(audio.isValid());

        // Pink noise must be non-trivial.  The upper bound allows
        // up to the equal-power crossfade worst case (peak * √2)
        // for samples that land in the seam region.
        const float peak = audioPeakChannel(audio, 0);
        CHECK(peak > 0.05f);
        CHECK(peak <= 0.45f);
}

TEST_CASE("AudioTestPattern_PinkNoiseZeroMean") {
        // Pink noise is the bigger DC-bias offender because the
        // Kellet IIR filter has substantial DC gain — without the
        // explicit DC-removal pass in buildPinkNoiseBuffer() the
        // cached buffer would carry a real offset.  Same bound as
        // the white noise mean check.
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::PinkNoise}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        gen.setNoiseBufferSeconds(1.0);
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(48000);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();

        double sum = 0.0;
        for(size_t s = 0; s < audio.samples(); ++s) sum += data[s];
        const double mean = sum / static_cast<double>(audio.samples());
        CHECK(std::fabs(mean) < 1.0e-4);
}

// ============================================================================
// Chirp — phase is continuous across chunks, cursor wraps cleanly
// ============================================================================

TEST_CASE("AudioTestPattern_ChirpStartsAtStartFrequency") {
        // Regression: the sweep must actually start at chirpStartFreq
        // and not race straight to Nyquist because of a unit bug in
        // the phase integration.  A 200 Hz start frequency over a
        // 2-second sweep means the first 1024 samples (~21 ms) are
        // spent at almost-exactly 200 Hz — we can count zero
        // crossings to confirm.
        const double sampleRate = 48000.0;
        const double fStart     = 200.0;
        AudioDesc desc(static_cast<float>(sampleRate), 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Chirp}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        gen.setChirpStartFreq(fStart);
        gen.setChirpEndFreq(2000.0);     // decade sweep
        gen.setChirpDurationSec(2.0);    // slow enough that the first
                                         // window is "effectively constant"
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(1024);
        REQUIRE(audio.isValid());
        CHECK(audioPeakChannel(audio, 0) > 0.05f);

        // The first 1024 samples span 1024/48000 = 21.3 ms — across
        // that tiny slice the log sweep has barely moved, so the
        // observed frequency should be within a few percent of fStart.
        // The old broken code was aliasing above Nyquist and the
        // estimator would return something near 0 or near sampleRate/2,
        // neither of which is close to 200 Hz.
        double observed = estimateFrequencyHz(audio, 0, sampleRate);
        CHECK(observed == doctest::Approx(fStart).epsilon(0.1));
}

TEST_CASE("AudioTestPattern_ChirpContinuousAcrossPeriodWrap") {
        // Regression: the chirp sweep must stay sample-continuous
        // when the cursor wraps from the end of one period back to
        // the start of the next.  An earlier closed-form
        // implementation reset phase to zero at the wrap, which
        // produced a once-per-period click.  The incremental phase
        // accumulator carries phase across the wrap and only the
        // frequency jumps back to fStart.
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Chirp}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        gen.setChirpStartFreq(200.0);
        gen.setChirpEndFreq(2000.0);
        gen.setChirpDurationSec(0.05);  // 50 ms, 2400 samples
        REQUIRE(gen.configure().isOk());

        // Pull 4800 samples — exactly two sweep periods — so the
        // cursor wraps once in the middle of the chunk.
        Audio audio = gen.create(4800);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();

        // The pathological case we're guarding against is a
        // sample-level step (a click) at the wrap point.  Compute
        // mean |step| across the whole chunk and assert no single
        // step is more than 10x the mean — same shape of test as
        // the noise seam regression.  A sample-reset click would
        // show up as a step that's orders of magnitude bigger than
        // the typical per-sample delta.
        double stepSum = 0.0;
        double maxStep = 0.0;
        for(size_t s = 1; s < audio.samples(); ++s) {
                const double step = std::fabs(data[s] - data[s - 1]);
                stepSum += step;
                if(step > maxStep) maxStep = step;
        }
        const double meanStep = stepSum / static_cast<double>(audio.samples() - 1);
        CHECK(maxStep < meanStep * 10.0);
}

TEST_CASE("AudioTestPattern_ChirpContinuousAcrossChunks") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::Chirp}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        gen.setChirpStartFreq(500.0);
        gen.setChirpEndFreq(5000.0);
        gen.setChirpDurationSec(0.5);
        REQUIRE(gen.configure().isOk());

        Audio first = gen.create(2400);   // 50 ms
        Audio second = gen.create(2400);  // another 50 ms
        REQUIRE(first.isValid());
        REQUIRE(second.isValid());

        // Both chunks must carry a non-trivial signal.
        CHECK(audioPeakChannel(first, 0) > 0.05f);
        CHECK(audioPeakChannel(second, 0) > 0.05f);

        // Phase continuity across the chunk boundary: the jump between
        // the last sample of chunk 1 and the first sample of chunk 2
        // must be no larger than one sample's worth of sweep motion
        // (≈ 2π * f(t) / sampleRate at the current frequency).  The
        // peak amplitude is 0.316 at -10 dBFS, and the per-sample
        // step at 5 kHz @ 48 kHz is roughly sin step 0.64 * 0.316 ≈
        // 0.2, so anything under 0.25 is continuous.
        const float *pa = first.data<float>();
        const float *pb = second.data<float>();
        const float lastA  = pa[first.samples() - 1];
        const float firstB = pb[0];
        const float jump = std::fabs(firstB - lastA);
        CHECK(jump < 0.25f);
}

// ============================================================================
// DualTone — default SMPTE IMD-1 shape, non-trivial output
// ============================================================================

TEST_CASE("AudioTestPattern_DualToneDefault") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::DualTone}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        // Defaults: 60 Hz + 7 kHz at 4:1 ratio.
        REQUIRE(gen.configure().isOk());

        Audio audio = gen.create(9600);  // 200 ms — plenty for several cycles.
        REQUIRE(audio.isValid());

        // Non-trivial and within level.
        const float peak = audioPeakChannel(audio, 0);
        CHECK(peak > 0.1f);
        CHECK(peak <= 0.32f);
}

TEST_CASE("AudioTestPattern_DualToneContinuousAcrossChunks") {
        // Regression: DualTone phase must carry across create()
        // calls.  An earlier implementation reset both tones to
        // phase zero every call, which on a 30 fps pipeline
        // produced a 30 Hz buzz.  We test this by pulling two
        // small chunks in a row and asserting the sample at the
        // chunk boundary is no worse than a typical per-sample
        // step — a phase-reset click would be much larger.
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::DualTone}));
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        REQUIRE(gen.configure().isOk());

        Audio first  = gen.create(1600);
        Audio second = gen.create(1600);
        REQUIRE(first.isValid());
        REQUIRE(second.isValid());

        // Measure the typical per-sample step across both chunks
        // so we can compare the boundary step against the noise
        // floor of normal dual-tone motion.
        double stepSum = 0.0;
        const float *pa = first.data<float>();
        const float *pb = second.data<float>();
        for(size_t s = 1; s < first.samples(); ++s) {
                stepSum += std::fabs(pa[s] - pa[s - 1]);
        }
        for(size_t s = 1; s < second.samples(); ++s) {
                stepSum += std::fabs(pb[s] - pb[s - 1]);
        }
        const size_t stepCount = (first.samples() - 1) + (second.samples() - 1);
        const double meanStep = stepSum / static_cast<double>(stepCount);

        const double boundaryStep = std::fabs(pb[0] - pa[first.samples() - 1]);
        CHECK(boundaryStep < meanStep * 10.0);
}

// ============================================================================
// PcmMarker — deterministic framing landing in the first N samples
// ============================================================================

TEST_CASE("AudioTestPattern_PcmMarkerPreamble") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::PcmMarker}));
        REQUIRE(gen.configure().isOk());

        // No timecode → payload falls back to the monotonic counter.
        Audio audio = gen.create(4096);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();

        // First 16 samples are the alternating ±1.0 preamble.
        for(size_t i = 0; i < AudioTestPattern::kPcmMarkerPreambleSamples; ++i) {
                const float expected = (i & 1u) ? -1.0f : 1.0f;
                CHECK(data[i] == doctest::Approx(expected));
        }

        // Next 8 samples are four +1.0 followed by four -1.0 (start
        // of frame marker).
        const size_t startBase = AudioTestPattern::kPcmMarkerPreambleSamples;
        for(size_t i = 0; i < AudioTestPattern::kPcmMarkerStartSamples; ++i) {
                const float expected = (i < 4) ? 1.0f : -1.0f;
                CHECK(data[startBase + i] == doctest::Approx(expected));
        }
}

TEST_CASE("AudioTestPattern_PcmMarkerPayloadEncodesCounter") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::PcmMarker}));
        REQUIRE(gen.configure().isOk());

        // First create() call uses counter value 0.  Payload should be
        // 64 zero bits, all landing at -0.8.
        Audio first = gen.create(256);
        REQUIRE(first.isValid());
        const float *pf = first.data<float>();
        const size_t payloadBase =
                AudioTestPattern::kPcmMarkerPreambleSamples +
                AudioTestPattern::kPcmMarkerStartSamples;
        for(size_t i = 0; i < AudioTestPattern::kPcmMarkerPayloadBits; ++i) {
                CHECK(pf[payloadBase + i] == doctest::Approx(-0.8f));
        }

        // Second create() call uses counter value 1 — the LSB of the
        // MSB-first payload lands in the last payload sample.  All
        // other bits stay at -0.8.
        Audio second = gen.create(256);
        REQUIRE(second.isValid());
        const float *ps = second.data<float>();
        for(size_t i = 0; i < AudioTestPattern::kPcmMarkerPayloadBits - 1; ++i) {
                CHECK(ps[payloadBase + i] == doctest::Approx(-0.8f));
        }
        const size_t lastBit = payloadBase + AudioTestPattern::kPcmMarkerPayloadBits - 1;
        CHECK(ps[lastBit] == doctest::Approx(0.8f));
}

TEST_CASE("AudioTestPattern_PcmMarkerUsesTimecodeBcd") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setChannelModes(makeModes({AudioPattern::PcmMarker}));
        REQUIRE(gen.configure().isOk());

        Timecode tc(Timecode::Mode(FrameRate::FPS_30, false), 1, 2, 3, 4);
        REQUIRE(tc.isValid());
        const uint64_t expected = tc.toBcd64();

        Audio audio = gen.create(256, tc);
        REQUIRE(audio.isValid());
        const float *data = audio.data<float>();
        const size_t payloadBase =
                AudioTestPattern::kPcmMarkerPreambleSamples +
                AudioTestPattern::kPcmMarkerStartSamples;

        uint64_t decoded = 0;
        for(size_t i = 0; i < AudioTestPattern::kPcmMarkerPayloadBits; ++i) {
                const bool bit = data[payloadBase + i] > 0.0f;
                decoded = (decoded << 1) | (bit ? 1u : 0u);
        }
        CHECK(decoded == expected);
}
