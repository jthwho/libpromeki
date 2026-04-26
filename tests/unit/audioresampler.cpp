/**
 * @file      audioresampler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/audioresampler.h>
#include <promeki/audiodesc.h>
#include <promeki/audiobuffer.h>

using namespace promeki;

namespace {

        // Fills an interleaved float buffer with a sine wave.
        void fillSine(float *buf, size_t samples, unsigned int ch, float freq, float rate) {
                for (size_t i = 0; i < samples; ++i) {
                        float val = std::sin(2.0f * static_cast<float>(M_PI) * freq * static_cast<float>(i) / rate);
                        for (unsigned int c = 0; c < ch; ++c) {
                                buf[i * ch + c] = val;
                        }
                }
        }

        size_t countZeroCrossings(const float *data, size_t samples, unsigned int ch) {
                size_t crossings = 0;
                for (size_t i = 1; i < samples; ++i) {
                        float prev = data[(i - 1) * ch];
                        float cur = data[i * ch];
                        if ((prev >= 0.0f && cur < 0.0f) || (prev < 0.0f && cur >= 0.0f)) {
                                ++crossings;
                        }
                }
                return crossings;
        }

        // Convenience wrapper: run a single chunk through the resampler, mirroring the
        // legacy Audio-based process() overload against raw float buffers.
        struct RawResult {
                        size_t gen;
                        Error  err;
        };

        RawResult processRaw(AudioResampler &r, const std::vector<float> &in, unsigned int inCh,
                             std::vector<float> &out, unsigned int outCh, bool endOfInput = false) {
                if (inCh != outCh) return {0, Error::FormatMismatch};
                long  inFrames = static_cast<long>(in.size() / inCh);
                long  outMax = static_cast<long>(out.size() / outCh);
                long  inputUsed = 0, outputGen = 0;
                Error err = r.process(in.data(), inFrames, out.data(), outMax, inputUsed, outputGen, endOfInput);
                if (err.isError()) return {0, err};
                out.resize(static_cast<size_t>(outputGen) * outCh);
                return {static_cast<size_t>(outputGen), Error::Ok};
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
        Error          err = r.setup(2, SrcQuality::SincMedium);
        CHECK(err.isOk());
        CHECK(r.isValid());
        CHECK(r.channels() == 2);
        CHECK(r.quality().value() == SrcQuality::SincMedium.value());
}

TEST_CASE("AudioResampler: setup with zero channels fails") {
        AudioResampler r;
        Error          err = r.setup(0, SrcQuality::SincMedium);
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

        const size_t       N = 1024;
        std::vector<float> in(N);
        fillSine(in.data(), N, 1, 1000.0f, 48000.0f);
        std::vector<float> out(N + 64);

        auto [gen, err] = processRaw(r, in, 1, out, 1);
        CHECK(err.isOk());
        CHECK(gen > 0);

        size_t start = gen > 32 ? 32 : 0;
        for (size_t i = start; i < gen && i < N; ++i) {
                CHECK(out[i] == doctest::Approx(in[i]).epsilon(0.01));
        }
}

TEST_CASE("AudioResampler: downsample 48000 -> 44100") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincMedium).isOk());
        CHECK(r.setRatio(44100.0f, 48000.0f).isOk());

        const size_t       inCount = 48000;
        std::vector<float> in(inCount);
        fillSine(in.data(), inCount, 1, 1000.0f, 48000.0f);

        const size_t       outMax = 44100 + 256;
        std::vector<float> out(outMax);

        auto [gen, err] = processRaw(r, in, 1, out, 1, true);
        CHECK(err.isOk());

        CHECK(gen > 43800);
        CHECK(gen < 44400);

        size_t crossings = countZeroCrossings(out.data(), gen, 1);
        CHECK(crossings > 1500);
        CHECK(crossings < 2100);
}

TEST_CASE("AudioResampler: upsample 44100 -> 48000") {
        AudioResampler r;
        CHECK(r.setup(1, SrcQuality::SincMedium).isOk());
        CHECK(r.setRatio(48000.0 / 44100.0).isOk());

        const size_t       inCount = 44100;
        std::vector<float> in(inCount);
        fillSine(in.data(), inCount, 1, 1000.0f, 44100.0f);

        const size_t       outMax = 48000 + 256;
        std::vector<float> out(outMax);

        auto [gen, err] = processRaw(r, in, 1, out, 1, true);
        CHECK(err.isOk());
        CHECK(gen > 47700);
        CHECK(gen < 48300);

        size_t crossings = countZeroCrossings(out.data(), gen, 1);
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

        const size_t       chunkSize = 1024;
        std::vector<float> in(chunkSize);
        fillSine(in.data(), chunkSize, 1, 440.0f, 48000.0f);
        std::vector<float> out(chunkSize + 256);

        auto [gen1, err1] = processRaw(r, in, 1, out, 1);
        CHECK(err1.isOk());
        float lastSample = gen1 > 0 ? out[gen1 - 1] : 0.0f;

        CHECK(r.setRatio(1.01).isOk());
        out.assign(chunkSize + 256, 0.0f);
        auto [gen2, err2] = processRaw(r, in, 1, out, 1);
        CHECK(err2.isOk());

        if (gen2 > 0) {
                float firstSample = out[0];
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

        const size_t       inCount = 4410;
        std::vector<float> in(inCount * 2);
        for (size_t i = 0; i < inCount; ++i) {
                in[i * 2] = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * static_cast<float>(i) / 44100.0f);
                in[i * 2 + 1] = std::sin(2.0f * static_cast<float>(M_PI) * 880.0f * static_cast<float>(i) / 44100.0f);
        }

        const size_t       outMax = 4800 + 256;
        std::vector<float> out(outMax * 2);

        auto [gen, err] = processRaw(r, in, 2, out, 2, true);
        CHECK(err.isOk());
        CHECK(gen > 4600);
        CHECK(gen < 5100);

        size_t leftCross = 0, rightCross = 0;
        for (size_t i = 1; i < gen; ++i) {
                float lp = out[(i - 1) * 2];
                float lc = out[i * 2];
                if ((lp >= 0.0f && lc < 0.0f) || (lp < 0.0f && lc >= 0.0f)) ++leftCross;
                float rp = out[(i - 1) * 2 + 1];
                float rc = out[i * 2 + 1];
                if ((rp >= 0.0f && rc < 0.0f) || (rp < 0.0f && rc >= 0.0f)) ++rightCross;
        }
        CHECK(leftCross > 70);
        CHECK(leftCross < 110);
        CHECK(rightCross > 140);
        CHECK(rightCross < 220);
}

// ============================================================================
// All quality modes
// ============================================================================

TEST_CASE("AudioResampler: all quality modes produce valid output") {
        const SrcQuality modes[] = {SrcQuality::SincBest, SrcQuality::SincMedium, SrcQuality::SincFastest,
                                    SrcQuality::Linear, SrcQuality::ZeroOrderHold};

        for (const auto &mode : modes) {
                CAPTURE(mode.value());
                AudioResampler r;
                CHECK(r.setup(1, mode).isOk());
                CHECK(r.setRatio(48000.0 / 44100.0).isOk());

                std::vector<float> in(4410);
                fillSine(in.data(), 4410, 1, 1000.0f, 44100.0f);
                std::vector<float> out(5120);

                auto [gen, err] = processRaw(r, in, 1, out, 1, true);
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

        std::vector<float> in(1024);
        fillSine(in.data(), 1024, 1, 500.0f, 24000.0f);
        std::vector<float> out(2560);

        auto [gen1, err1] = processRaw(r, in, 1, out, 1);
        CHECK(err1.isOk());
        CHECK(gen1 > 0);

        CHECK(r.reset().isOk());
        CHECK(r.ratio() == doctest::Approx(2.0));
        CHECK(r.channels() == 1);

        out.assign(2560, 0.0f);
        auto [gen2, err2] = processRaw(r, in, 1, out, 1);
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
        float      inBuf[inFrames];
        for (long i = 0; i < inFrames; ++i) {
                inBuf[i] = std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f * static_cast<float>(i) / 24000.0f);
        }

        const long outFrames = 1280;
        float      outBuf[outFrames];

        long  inputUsed = 0, outputGen = 0;
        Error err = r.process(inBuf, inFrames, outBuf, outFrames, inputUsed, outputGen, true);
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

        std::vector<float> in(64);
        fillSine(in.data(), 64, 1, 500.0f, 24000.0f);
        std::vector<float> out1(256);

        auto [gen1, err1] = processRaw(r, in, 1, out1, 1, false);
        CHECK(err1.isOk());

        float outBuf[256] = {};
        long  inputUsed = 0, outputGen = 0;
        Error err = r.process(static_cast<const float *>(nullptr), 0, outBuf, 256, inputUsed, outputGen, true);
        CHECK(err.isOk());
}

// ============================================================================
// AudioBuffer with resampling
// ============================================================================

TEST_CASE("AudioBuffer: push with resampling (rate mismatch)") {
        AudioDesc   outFormat(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioBuffer fifo(outFormat, 96000);

        AudioDesc inFormat(AudioFormat::NativeFloat, 44100.0f, 1);
        fifo.setInputFormat(inFormat);

        const size_t       inCount = 44100;
        std::vector<float> in(inCount);
        fillSine(in.data(), inCount, 1, 1000.0f, 44100.0f);

        Error err = fifo.push(in.data(), inCount, inFormat);
        CHECK(err.isOk());

        size_t avail = fifo.available();
        CHECK(avail > 47500);
        CHECK(avail < 48500);

        std::vector<float> out(avail);
        auto [got, popErr] = fifo.pop(out.data(), avail);
        CHECK(popErr.isOk());
        CHECK(got == avail);

        size_t crossings = countZeroCrossings(out.data(), got, 1);
        CHECK(crossings > 1900);
        CHECK(crossings < 2100);
}

TEST_CASE("AudioBuffer: push with resampling and format conversion") {
        AudioDesc   outFormat(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        AudioBuffer fifo(outFormat, 96000);

        AudioDesc inFormat(AudioFormat::NativeFloat, 44100.0f, 1);
        fifo.setInputFormat(inFormat);

        const size_t       inCount = 4410;
        std::vector<float> in(inCount);
        fillSine(in.data(), inCount, 1, 1000.0f, 44100.0f);

        Error err = fifo.push(in.data(), inCount, inFormat);
        CHECK(err.isOk());

        size_t avail = fifo.available();
        CHECK(avail > 4700);
        CHECK(avail < 5000);
}

// ============================================================================
// AudioBuffer drift correction
// ============================================================================

TEST_CASE("AudioBuffer: drift correction enable/disable/query") {
        AudioDesc   fmt(AudioFormat::NativeFloat, 48000.0f, 1);
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
        AudioDesc    fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        const size_t capacity = 48000;
        AudioBuffer  fifo(fmt, capacity);
        fifo.setInputFormat(fmt);

        const size_t target = capacity / 2;
        fifo.enableDriftCorrection(target, 0.01);

        const size_t       chunkSize = 1024;
        std::vector<float> chunk(chunkSize);
        fillSine(chunk.data(), chunkSize, 1, 440.0f, 48000.0f);

        for (int i = 0; i < 10; ++i) {
                Error err = fifo.push(chunk.data(), chunkSize, fmt);
                CHECK(err.isOk());
        }

        double r = fifo.driftRatio();
        CHECK(r > 1.0);

        size_t avail = fifo.available();
        CHECK(avail > 0);
}

TEST_CASE("AudioBuffer: drift correction adjusts ratio based on fill level") {
        AudioDesc    fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        const size_t capacity = 96000;
        AudioBuffer  fifo(fmt, capacity);
        fifo.setInputFormat(fmt);

        const size_t target = 24000;
        const double gain = 0.01;
        fifo.enableDriftCorrection(target, gain);

        fifo.disableDriftCorrection();
        {
                std::vector<float> prefill(target);
                fillSine(prefill.data(), target, 1, 440.0f, 48000.0f);
                CHECK(fifo.push(prefill.data(), target, fmt).isOk());
        }
        CHECK(fifo.available() == target);
        fifo.enableDriftCorrection(target, gain);

        std::vector<float> chunk(1024);
        fillSine(chunk.data(), 1024, 1, 440.0f, 48000.0f);
        CHECK(fifo.push(chunk.data(), 1024, fmt).isOk());

        double r = fifo.driftRatio();
        CHECK(r == doctest::Approx(1.0).epsilon(0.02));
}

TEST_CASE("AudioBuffer: drift correction with rate mismatch") {
        AudioDesc    outFmt(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioDesc    inFmt(AudioFormat::NativeFloat, 44100.0f, 1);
        const size_t capacity = 96000;
        AudioBuffer  fifo(outFmt, capacity);
        fifo.setInputFormat(inFmt);

        const size_t target = 24000;
        fifo.enableDriftCorrection(target, 0.005);

        const size_t       inCount = 4410;
        std::vector<float> in(inCount);
        fillSine(in.data(), inCount, 1, 1000.0f, 44100.0f);
        CHECK(fifo.push(in.data(), inCount, inFmt).isOk());

        double r = fifo.driftRatio();
        double nominal = 48000.0 / 44100.0;
        CHECK(r > nominal);

        size_t avail = fifo.available();
        CHECK(avail > 4700);
        CHECK(avail < 5100);
}

TEST_CASE("AudioBuffer: drift correction ratio decreases when overfull") {
        AudioDesc    fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        const size_t capacity = 96000;
        AudioBuffer  fifo(fmt, capacity);
        fifo.setInputFormat(fmt);

        const size_t target = 10000;
        const double gain = 0.01;

        fifo.disableDriftCorrection();
        {
                std::vector<float> prefill(40000);
                fillSine(prefill.data(), 40000, 1, 440.0f, 48000.0f);
                CHECK(fifo.push(prefill.data(), 40000, fmt).isOk());
        }
        CHECK(fifo.available() == 40000);
        fifo.enableDriftCorrection(target, gain);

        std::vector<float> chunk(1024);
        fillSine(chunk.data(), 1024, 1, 440.0f, 48000.0f);
        CHECK(fifo.push(chunk.data(), 1024, fmt).isOk());

        double r = fifo.driftRatio();
        CHECK(r < 1.0);
}

TEST_CASE("AudioBuffer: no SRC overhead when drift correction is off and rates match") {
        AudioDesc   fmt(AudioFormat::NativeFloat, 48000.0f, 1);
        AudioBuffer fifo(fmt, 48000);
        fifo.setInputFormat(fmt);

        std::vector<float> chunk(1000);
        fillSine(chunk.data(), 1000, 1, 440.0f, 48000.0f);
        CHECK(fifo.push(chunk.data(), 1000, fmt).isOk());

        CHECK(fifo.available() == 1000);
}
