/**
 * @file      audioresampler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/audioresampler.h>
#include <promeki/audiodesc.h>
#include <promeki/audio.h>
#include <promeki/audiobuffer.h>

using namespace promeki;

namespace {

AudioDesc nativeDesc(float rate, unsigned int ch) {
        return AudioDesc(AudioFormat::NativeFloat, rate, ch);
}

void fillSine(Audio &audio, float freq, float rate) {
        float *d = audio.data<float>();
        unsigned int ch = audio.desc().channels();
        for(size_t i = 0; i < audio.samples(); ++i) {
                float val = std::sin(2.0f * static_cast<float>(M_PI) * freq *
                                     static_cast<float>(i) / rate);
                for(unsigned int c = 0; c < ch; ++c) {
                        d[i * ch + c] = val;
                }
        }
}

size_t countZeroCrossings(const float *data, size_t samples, unsigned int ch) {
        size_t crossings = 0;
        for(size_t i = 1; i < samples; ++i) {
                float prev = data[(i - 1) * ch];
                float cur  = data[i * ch];
                if((prev >= 0.0f && cur < 0.0f) || (prev < 0.0f && cur >= 0.0f)) {
                        ++crossings;
                }
        }
        return crossings;
}

} // namespace

// ============================================================================
// Basic lifecycle
// ============================================================================

TEST_CASE("AudioResampler: default construct is not valid") {
        AudioResampler r;
        CHECK_FALSE(r.isValid());
        CHECK(r.channels() == 0);
        CHECK(r.ratio() == doctest::Approx(1.0));
}

TEST_CASE("AudioResampler: setup and teardown") {
        AudioResampler r;
        Error err = r.setup(2, SrcQuality::SincMedium);
        CHECK(err.isOk());
        CHECK(r.isValid());
        CHECK(r.channels() == 2);
        CHECK(r.quality().value() == SrcQuality::SincMedium.value());
}

TEST_CASE("AudioResampler: setup with zero channels fails") {
        AudioResampler r;
        Error err = r.setup(0, SrcQuality::SincMedium);
        CHECK(err == Error::InvalidArgument);
        CHECK_FALSE(r.isValid());
}

TEST_CASE("AudioResampler: reinitialize with different params") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::Linear).isOk());
        CHECK(r.channels() == 1);
        CHECK(r.quality().value() == SrcQuality::Linear.value());

        CHECK(r.setup(4, SrcQuality::SincBest).isOk());
        CHECK(r.channels() == 4);
        CHECK(r.quality().value() == SrcQuality::SincBest.value());
}

// ============================================================================
// Ratio
// ============================================================================

TEST_CASE("AudioResampler: setRatio basic") {
        AudioResampler r;
        CHECK(r.setup(1).isOk());

        CHECK(r.setRatio(48000.0 / 44100.0).isOk());
        CHECK(r.ratio() == doctest::Approx(48000.0 / 44100.0));
}

TEST_CASE("AudioResampler: setRatio rejects non-positive") {
        AudioResampler r;
        CHECK(r.setup(1).isOk());

        CHECK(r.setRatio(0.0) == Error::InvalidArgument);
        CHECK(r.setRatio(-1.0) == Error::InvalidArgument);
}

TEST_CASE("AudioResampler: setRatio before setup returns NotSupported") {
        AudioResampler r;
        CHECK(r.setRatio(1.5) == Error::NotSupported);
}

TEST_CASE("AudioResampler: setRatio convenience (inputRate, outputRate)") {
        AudioResampler r;
        CHECK(r.setup(1).isOk());
        CHECK(r.setRatio(44100.0f, 48000.0f).isOk());
        CHECK(r.ratio() == doctest::Approx(48000.0 / 44100.0));
}

TEST_CASE("AudioResampler: setRatio convenience rejects non-positive rates") {
        AudioResampler r;
        CHECK(r.setup(1).isOk());
        CHECK(r.setRatio(0.0f, 48000.0f) == Error::InvalidArgument);
        CHECK(r.setRatio(44100.0f, 0.0f) == Error::InvalidArgument);
}

// ============================================================================
// Process - fixed ratio
// ============================================================================

TEST_CASE("AudioResampler: 1:1 passthrough") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(1.0).isOk());

        const size_t N = 1024;
        Audio in(nativeDesc(48000.0f, 1), N);
        fillSine(in, 1000.0f, 48000.0f);
        Audio out(nativeDesc(48000.0f, 1), N + 64);

        auto [gen, err] = r.process(in, out);
        CHECK(err.isOk());
        CHECK(gen > 0);

        // Output should closely match input (within resampler filter delay).
        // Skip the first few samples due to filter startup.
        const float *inData  = in.data<const float>();
        const float *outData = out.data<const float>();
        size_t start = gen > 32 ? 32 : 0;
        for(size_t i = start; i < gen && i < N; ++i) {
                CHECK(outData[i] == doctest::Approx(inData[i]).epsilon(0.01));
        }
}

TEST_CASE("AudioResampler: downsample 48000 -> 44100") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincMedium).isOk());
        CHECK(r.setRatio(44100.0f, 48000.0f).isOk());

        // Feed 1 second of 48000 Hz input.
        const size_t inCount = 48000;
        Audio in(nativeDesc(48000.0f, 1), inCount);
        fillSine(in, 1000.0f, 48000.0f);

        // Output buffer for ~1 second at 44100.
        const size_t outMax = 44100 + 256;
        Audio out(nativeDesc(44100.0f, 1), outMax);

        auto [gen, err] = r.process(in, out, true);
        CHECK(err.isOk());

        // Should produce approximately 44100 samples.
        CHECK(gen > 43800);
        CHECK(gen < 44400);

        // Verify the 1kHz signal is preserved (expect ~2000 zero crossings
        // for 1 second of a 1kHz sine: 2 crossings per cycle).  Allow wide
        // tolerance for the sinc filter startup/flush transient.
        size_t crossings = countZeroCrossings(out.data<const float>(), gen, 1);
        CHECK(crossings > 1500);
        CHECK(crossings < 2100);
}

TEST_CASE("AudioResampler: upsample 44100 -> 48000") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincMedium).isOk());
        CHECK(r.setRatio(48000.0 / 44100.0).isOk());

        const size_t inCount = 44100;
        Audio in(nativeDesc(44100.0f, 1), inCount);
        fillSine(in, 1000.0f, 44100.0f);

        const size_t outMax = 48000 + 256;
        Audio out(nativeDesc(48000.0f, 1), outMax);

        auto [gen, err] = r.process(in, out, true);
        CHECK(err.isOk());
        CHECK(gen > 47700);
        CHECK(gen < 48300);

        size_t crossings = countZeroCrossings(out.data<const float>(), gen, 1);
        CHECK(crossings > 1900);
        CHECK(crossings < 2100);
}

// ============================================================================
// Process - variable ratio
// ============================================================================

TEST_CASE("AudioResampler: variable ratio change is smooth") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(1.0).isOk());

        const size_t chunkSize = 1024;
        Audio in(nativeDesc(48000.0f, 1), chunkSize);
        fillSine(in, 440.0f, 48000.0f);
        Audio out(nativeDesc(48000.0f, 1), chunkSize + 256);

        // Process at ratio 1.0.
        auto [gen1, err1] = r.process(in, out);
        CHECK(err1.isOk());
        float lastSample = out.data<const float>()[gen1 > 0 ? gen1 - 1 : 0];

        // Change ratio to 1.01 (1% speedup) and process again.
        CHECK(r.setRatio(1.01).isOk());
        auto [gen2, err2] = r.process(in, out);
        CHECK(err2.isOk());

        // Verify no click: the first output sample after the ratio change
        // should be close to the last output sample of the previous chunk.
        if(gen2 > 0) {
                float firstSample = out.data<const float>()[0];
                float diff = std::fabs(firstSample - lastSample);
                CHECK(diff < 0.5f);
        }
}

// ============================================================================
// Multi-channel
// ============================================================================

TEST_CASE("AudioResampler: stereo") {
        AudioResampler r;
        CHECK(r.setup(2, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(48000.0 / 44100.0).isOk());

        const size_t inCount = 4410;
        Audio in(nativeDesc(44100.0f, 2), inCount);
        float *d = in.data<float>();
        // Left: 440 Hz, Right: 880 Hz.
        for(size_t i = 0; i < inCount; ++i) {
                d[i * 2]     = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f *
                                        static_cast<float>(i) / 44100.0f);
                d[i * 2 + 1] = std::sin(2.0f * static_cast<float>(M_PI) * 880.0f *
                                        static_cast<float>(i) / 44100.0f);
        }

        const size_t outMax = 4800 + 256;
        Audio out(nativeDesc(48000.0f, 2), outMax);

        auto [gen, err] = r.process(in, out, true);
        CHECK(err.isOk());
        CHECK(gen > 4600);
        CHECK(gen < 5100);

        // Verify both channels independently via zero crossings.
        // 0.1 seconds of audio: 440Hz -> ~88 crossings, 880Hz -> ~176.
        // Allow wide tolerance for filter transient.
        const float *od = out.data<const float>();
        size_t leftCross = 0, rightCross = 0;
        for(size_t i = 1; i < gen; ++i) {
                float lp = od[(i - 1) * 2];
                float lc = od[i * 2];
                if((lp >= 0.0f && lc < 0.0f) || (lp < 0.0f && lc >= 0.0f)) ++leftCross;
                float rp = od[(i - 1) * 2 + 1];
                float rc = od[i * 2 + 1];
                if((rp >= 0.0f && rc < 0.0f) || (rp < 0.0f && rc >= 0.0f)) ++rightCross;
        }
        CHECK(leftCross > 70);
        CHECK(leftCross < 110);
        CHECK(rightCross > 140);
        CHECK(rightCross < 220);
}

// ============================================================================
// Channel/format mismatch errors
// ============================================================================

TEST_CASE("AudioResampler: channel mismatch returns FormatMismatch") {
        AudioResampler r;
        CHECK(r.setup(2, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(1.0).isOk());

        Audio in(nativeDesc(48000.0f, 1), 256);
        Audio out(nativeDesc(48000.0f, 2), 256);
        auto [gen, err] = r.process(in, out);
        CHECK(err == Error::FormatMismatch);
        CHECK(gen == 0);
}

TEST_CASE("AudioResampler: non-native format returns FormatMismatch") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(1.0).isOk());

        Audio in(AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 1), 256);
        Audio out(nativeDesc(48000.0f, 1), 256);
        auto [gen, err] = r.process(in, out);
        CHECK(err == Error::FormatMismatch);
}

// ============================================================================
// All quality modes
// ============================================================================

TEST_CASE("AudioResampler: all quality modes produce valid output") {
        const SrcQuality modes[] = {
                SrcQuality::SincBest,
                SrcQuality::SincMedium,
                SrcQuality::SincFastest,
                SrcQuality::Linear,
                SrcQuality::ZeroOrderHold
        };

        for(const auto &mode : modes) {
                CAPTURE(mode.value());
                AudioResampler r;
                CHECK(r.setup(1, mode).isOk());
                CHECK(r.setRatio(48000.0 / 44100.0).isOk());

                Audio in(nativeDesc(44100.0f, 1), 4410);
                fillSine(in, 1000.0f, 44100.0f);
                Audio out(nativeDesc(48000.0f, 1), 5120);

                auto [gen, err] = r.process(in, out, true);
                CHECK(err.isOk());
                CHECK(gen > 4600);
                CHECK(gen < 5100);
        }
}

// ============================================================================
// Reset
// ============================================================================

TEST_CASE("AudioResampler: reset clears state") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(2.0).isOk());

        Audio in(nativeDesc(24000.0f, 1), 1024);
        fillSine(in, 500.0f, 24000.0f);
        Audio out(nativeDesc(48000.0f, 1), 2560);

        auto [gen1, err1] = r.process(in, out);
        CHECK(err1.isOk());
        CHECK(gen1 > 0);

        CHECK(r.reset().isOk());
        CHECK(r.ratio() == doctest::Approx(2.0));
        CHECK(r.channels() == 1);

        // Process again after reset — should produce valid output.
        auto [gen2, err2] = r.process(in, out);
        CHECK(err2.isOk());
        CHECK(gen2 > 0);
}

// ============================================================================
// Raw buffer process
// ============================================================================

TEST_CASE("AudioResampler: raw buffer interface") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincFastest).isOk());
        CHECK(r.setRatio(2.0).isOk());

        const long inFrames = 512;
        float inBuf[inFrames];
        for(long i = 0; i < inFrames; ++i) {
                inBuf[i] = std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f *
                                    static_cast<float>(i) / 24000.0f);
        }

        const long outFrames = 1280;
        float outBuf[outFrames];

        long inputUsed = 0, outputGen = 0;
        Error err = r.process(inBuf, inFrames, outBuf, outFrames,
                              inputUsed, outputGen, true);
        CHECK(err.isOk());
        CHECK(inputUsed > 0);
        CHECK(inputUsed <= inFrames);
        CHECK(outputGen > 0);
        CHECK(outputGen <= outFrames);
}

// ============================================================================
// End-of-input flush
// ============================================================================

TEST_CASE("AudioResampler: end-of-input flushes remaining samples") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincMedium).isOk());
        CHECK(r.setRatio(2.0).isOk());

        // Process a tiny input with endOfInput=false, then flush with empty input.
        Audio in(nativeDesc(24000.0f, 1), 64);
        fillSine(in, 500.0f, 24000.0f);
        Audio out1(nativeDesc(48000.0f, 1), 256);

        auto [gen1, err1] = r.process(in, out1, false);
        CHECK(err1.isOk());

        // Flush: empty input, endOfInput=true.
        Audio empty(nativeDesc(24000.0f, 1), 1);
        empty.resize(0);
        Audio out2(nativeDesc(48000.0f, 1), 256);

        long inputUsed = 0, outputGen = 0;
        Error err = r.process(static_cast<const float *>(nullptr), 0,
                              out2.data<float>(), static_cast<long>(out2.maxSamples()),
                              inputUsed, outputGen, true);
        CHECK(err.isOk());
        // The flush may produce a few trailing samples from the filter tail.
        // We just verify it doesn't error.
}

// ============================================================================
// AudioBuffer with resampling
// ============================================================================

TEST_CASE("AudioBuffer: push with resampling (rate mismatch)") {
        AudioDesc outFormat(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioBuffer fifo(outFormat, 96000);

        AudioDesc inFormat(AudioFormat::NativeFloat, 44100.0f, 1);
        fifo.setInputFormat(inFormat);

        // Push 44100 samples of a 1kHz sine at 44100 Hz.
        const size_t inCount = 44100;
        Audio in(inFormat, inCount);
        fillSine(in, 1000.0f, 44100.0f);

        Error err = fifo.push(in);
        CHECK(err.isOk());

        // Should have approximately 48000 samples in the buffer.
        size_t avail = fifo.available();
        CHECK(avail > 47500);
        CHECK(avail < 48500);

        // Pop and verify signal integrity.
        Audio out(outFormat, avail);
        auto [got, popErr] = fifo.pop(out, avail);
        CHECK(popErr.isOk());
        CHECK(got == avail);

        size_t crossings = countZeroCrossings(out.data<const float>(), got, 1);
        CHECK(crossings > 1900);
        CHECK(crossings < 2100);
}

TEST_CASE("AudioBuffer: push with resampling and format conversion") {
        AudioDesc outFormat(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        AudioBuffer fifo(outFormat, 96000);

        AudioDesc inFormat(AudioFormat::NativeFloat, 44100.0f, 1);
        fifo.setInputFormat(inFormat);

        const size_t inCount = 4410;
        Audio in(inFormat, inCount);
        fillSine(in, 1000.0f, 44100.0f);

        Error err = fifo.push(in);
        CHECK(err.isOk());

        size_t avail = fifo.available();
        CHECK(avail > 4700);
        CHECK(avail < 5000);
}

// ============================================================================
// AudioBuffer drift correction
// ============================================================================

TEST_CASE("AudioBuffer: drift correction enable/disable/query") {
        AudioDesc fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioBuffer fifo(fmt, 48000);

        CHECK_FALSE(fifo.driftCorrectionEnabled());
        CHECK(fifo.driftRatio() == doctest::Approx(1.0));

        fifo.enableDriftCorrection(24000, 0.005);
        CHECK(fifo.driftCorrectionEnabled());

        fifo.disableDriftCorrection();
        CHECK_FALSE(fifo.driftCorrectionEnabled());
        CHECK(fifo.driftRatio() == doctest::Approx(1.0));
}

TEST_CASE("AudioBuffer: drift correction same-rate drives fill toward target") {
        // Same input and output rate (48000 Hz).  Drift correction is
        // used to keep the fill level near targetSamples.
        AudioDesc fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        const size_t capacity = 48000;
        AudioBuffer fifo(fmt, capacity);
        fifo.setInputFormat(fmt);

        const size_t target = capacity / 2;  // 24000
        fifo.enableDriftCorrection(target, 0.01);

        // Push a bunch of chunks into an empty buffer.  The buffer starts
        // underfull so the drift ratio should be > 1.0 (produce slightly
        // MORE output to fill faster).
        const size_t chunkSize = 1024;
        Audio chunk(fmt, chunkSize);
        fillSine(chunk, 440.0f, 48000.0f);

        // Push several chunks.
        for(int i = 0; i < 10; ++i) {
                Error err = fifo.push(chunk);
                CHECK(err.isOk());
        }

        // The drift ratio should have been > 1.0 because the buffer was
        // underfull (available < target).
        double r = fifo.driftRatio();
        CHECK(r > 1.0);

        // Now pop samples to verify the buffer has data.
        size_t avail = fifo.available();
        CHECK(avail > 0);
}

TEST_CASE("AudioBuffer: drift correction adjusts ratio based on fill level") {
        AudioDesc fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        const size_t capacity = 96000;
        AudioBuffer fifo(fmt, capacity);
        fifo.setInputFormat(fmt);

        const size_t target = 24000;
        const double gain = 0.01;
        fifo.enableDriftCorrection(target, gain);

        // Pre-fill the buffer to exactly the target level by pushing
        // without drift correction, then enable it.
        fifo.disableDriftCorrection();
        {
                Audio prefill(fmt, target);
                fillSine(prefill, 440.0f, 48000.0f);
                CHECK(fifo.push(prefill).isOk());
        }
        CHECK(fifo.available() == target);
        fifo.enableDriftCorrection(target, gain);

        // Push a chunk.  Since available == target, the drift adjustment
        // should be negligible: ratio ≈ 1.0.
        Audio chunk(fmt, 1024);
        fillSine(chunk, 440.0f, 48000.0f);
        CHECK(fifo.push(chunk).isOk());

        double r = fifo.driftRatio();
        CHECK(r == doctest::Approx(1.0).epsilon(0.02));
}

TEST_CASE("AudioBuffer: drift correction with rate mismatch") {
        // Different input and output rates with drift correction on top.
        AudioDesc outFmt(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioDesc inFmt(AudioFormat::NativeFloat, 44100.0f, 1);
        const size_t capacity = 96000;
        AudioBuffer fifo(outFmt, capacity);
        fifo.setInputFormat(inFmt);

        const size_t target = 24000;
        fifo.enableDriftCorrection(target, 0.005);

        // Push 4410 samples at 44100 Hz.  Nominal ratio is 48000/44100 ≈ 1.088.
        // With an empty buffer (available < target), drift pushes ratio
        // slightly above nominal.
        const size_t inCount = 4410;
        Audio in(inFmt, inCount);
        fillSine(in, 1000.0f, 44100.0f);
        CHECK(fifo.push(in).isOk());

        double r = fifo.driftRatio();
        double nominal = 48000.0 / 44100.0;
        CHECK(r > nominal);  // underfull → ratio above nominal

        size_t avail = fifo.available();
        CHECK(avail > 4700);
        CHECK(avail < 5100);
}

TEST_CASE("AudioBuffer: drift correction ratio decreases when overfull") {
        AudioDesc fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        const size_t capacity = 96000;
        AudioBuffer fifo(fmt, capacity);
        fifo.setInputFormat(fmt);

        const size_t target = 10000;
        const double gain = 0.01;

        // Pre-fill well above target.
        fifo.disableDriftCorrection();
        {
                Audio prefill(fmt, 40000);
                fillSine(prefill, 440.0f, 48000.0f);
                CHECK(fifo.push(prefill).isOk());
        }
        CHECK(fifo.available() == 40000);
        fifo.enableDriftCorrection(target, gain);

        // Push a chunk.  Buffer is very overfull so ratio should be < 1.0
        // (produce fewer output samples to drain).
        Audio chunk(fmt, 1024);
        fillSine(chunk, 440.0f, 48000.0f);
        CHECK(fifo.push(chunk).isOk());

        double r = fifo.driftRatio();
        CHECK(r < 1.0);
}

TEST_CASE("AudioBuffer: no SRC overhead when drift correction is off and rates match") {
        // Verify that same-rate pushes without drift correction use the
        // fast path (no resampler) by confirming exact sample counts.
        AudioDesc fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioBuffer fifo(fmt, 48000);
        fifo.setInputFormat(fmt);

        Audio chunk(fmt, 1000);
        fillSine(chunk, 440.0f, 48000.0f);
        CHECK(fifo.push(chunk).isOk());

        // Without resampling, available should be exactly 1000.
        CHECK(fifo.available() == 1000);
}
