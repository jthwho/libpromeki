/**
 * @file      audiobuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/config.h>
#include <promeki/audiobuffer.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiodesc.h>
#include <promeki/audiometer.h>
#include <promeki/audiostreamdesc.h>

using namespace promeki;

namespace {

        AudioDesc s16LE48k2ch() {
                return AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        }
        AudioDesc f32LE48k2ch() {
                return AudioDesc(AudioFormat::PCMI_Float32LE, 48000.0f, 2);
        }

} // namespace

// ============================================================================
// Basic construction and capacity
// ============================================================================

TEST_CASE("AudioBuffer: default construct is invalid") {
        AudioBuffer ab;
        CHECK_FALSE(ab.isValid());
        CHECK(ab.capacity() == 0);
        CHECK(ab.available() == 0);
}

TEST_CASE("AudioBuffer: construct with format is valid but empty") {
        AudioBuffer ab(s16LE48k2ch());
        CHECK(ab.isValid());
        CHECK(ab.capacity() == 0);
        CHECK(ab.available() == 0);
        CHECK(ab.isEmpty());
}

TEST_CASE("AudioBuffer: reserve grows capacity") {
        AudioBuffer ab(s16LE48k2ch());
        CHECK(ab.reserve(1024).isOk());
        CHECK(ab.capacity() == 1024);
        CHECK(ab.free() == 1024);
}

TEST_CASE("AudioBuffer: constructor with capacity") {
        AudioBuffer ab(s16LE48k2ch(), 2048);
        CHECK(ab.capacity() == 2048);
        CHECK(ab.isEmpty());
}

// ============================================================================
// Push / pop same-format (fast path)
// ============================================================================

TEST_CASE("AudioBuffer: push and pop in same format") {
        AudioBuffer ab(s16LE48k2ch(), 128);
        int16_t     samples[8] = {100, -100, 200, -200, 300, -300, 400, -400};
        // 8 bytes per 4-sample stereo s16 frame → 4 samples.
        CHECK(ab.push(samples, 4, s16LE48k2ch()).isOk());
        CHECK(ab.available() == 4);

        int16_t out[8] = {};
        auto [popped, popErr] = ab.pop(out, 4);
        CHECK(popErr.isOk());
        CHECK(popped == 4);
        CHECK(ab.available() == 0);
        for (int i = 0; i < 8; ++i) CHECK(out[i] == samples[i]);
}

TEST_CASE("AudioBuffer: pop more than available returns partial") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        int16_t     samples[4] = {1, 2, 3, 4};
        CHECK(ab.push(samples, 2, s16LE48k2ch()).isOk());
        int16_t out[8] = {0};
        auto [popped, popErr] = ab.pop(out, 8);
        CHECK(popErr.isOk());
        CHECK(popped == 2);
        CHECK(ab.isEmpty());
}

TEST_CASE("AudioBuffer: pop from empty returns 0") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t     out[4] = {1, 2, 3, 4};
        auto [popped, popErr] = ab.pop(out, 4);
        CHECK(popErr.isOk());
        CHECK(popped == 0);
        // Destination untouched.
        CHECK(out[0] == 1);
}

TEST_CASE("AudioBuffer: push over capacity returns NoSpace") {
        AudioBuffer ab(s16LE48k2ch(), 4);
        int16_t     samples[16] = {};
        Error       err = ab.push(samples, 5, s16LE48k2ch());
        CHECK(err == Error::NoSpace);
        CHECK(ab.isEmpty());
}

// ============================================================================
// Ring wraparound
// ============================================================================

TEST_CASE("AudioBuffer: push/pop across the wraparound boundary") {
        AudioBuffer ab(s16LE48k2ch(), 4);
        int16_t     a[4] = {1, 2, 3, 4};                // 2 samples
        int16_t     b[8] = {5, 6, 7, 8, 9, 10, 11, 12}; // 4 samples

        REQUIRE(ab.push(a, 2, s16LE48k2ch()).isOk());
        CHECK(ab.available() == 2);

        // Pop one sample to move head forward.
        int16_t out[2] = {};
        auto [n1, e1] = ab.pop(out, 1);
        CHECK(n1 == 1);
        CHECK(out[0] == 1);
        CHECK(out[1] == 2);
        CHECK(ab.available() == 1);

        // Push enough to wrap: capacity 4, currently 1 sample at head=1,
        // tail=2. Push 3 more samples → wraps around.
        REQUIRE(ab.push(b, 3, s16LE48k2ch()).isOk());
        CHECK(ab.available() == 4);

        // Pop all 4 and verify order: original sample at index 1 (3, 4),
        // then first 3 samples of b (5, 6, 7, 8, 9, 10).
        int16_t full[8] = {};
        auto [n4, e4] = ab.pop(full, 4);
        CHECK(n4 == 4);
        CHECK(full[0] == 3);
        CHECK(full[1] == 4);
        CHECK(full[2] == 5);
        CHECK(full[3] == 6);
        CHECK(full[4] == 7);
        CHECK(full[5] == 8);
        CHECK(full[6] == 9);
        CHECK(full[7] == 10);
}

// ============================================================================
// Push via raw buffer with matching format (same code path as the legacy
// push(Audio) overload)
// ============================================================================

TEST_CASE("AudioBuffer: push raw matching format, pop raw") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        int16_t     in[16];
        for (size_t i = 0; i < 16; ++i) in[i] = static_cast<int16_t>(i * 100);

        REQUIRE(ab.push(in, 8, s16LE48k2ch()).isOk());
        CHECK(ab.available() == 8);

        int16_t out[16] = {};
        auto [popped, popErr] = ab.pop(out, 8);
        CHECK(popErr.isOk());
        CHECK(popped == 8);
        for (size_t i = 0; i < 16; ++i) CHECK(out[i] == static_cast<int16_t>(i * 100));
}

// ============================================================================
// Format conversion (float32 → int16 at the same rate/channels)
// ============================================================================

TEST_CASE("AudioBuffer: push float32, pop int16 at same rate") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        ab.setInputFormat(f32LE48k2ch());

        // 4 stereo samples (8 float values)
        float src[8] = {0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.25f, -0.25f, 0.0f};
        REQUIRE(ab.push(src, 4, f32LE48k2ch()).isOk());
        CHECK(ab.available() == 4);

        int16_t out[8] = {};
        auto [got, gotErr] = ab.pop(out, 4);
        CHECK(gotErr.isOk());
        CHECK(got == 4);
        // After round-trip, values are clamped + quantized to s16 range.
        CHECK(out[0] == 0);
        CHECK(out[1] == 32767);
        CHECK(out[2] == -32768);
        CHECK(out[3] > 16000); // ~0.5 × 32767
        CHECK(out[3] < 17000);
        CHECK(out[4] < -16000);
        CHECK(out[4] > -17000);
}

// ============================================================================
// Non-supported conversions
// ============================================================================

TEST_CASE("AudioBuffer: push with mismatched sample rate resamples") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        AudioDesc   src44(AudioFormat::PCMI_S16LE, 44100.0f, 2);
        int16_t     samples[4] = {};
        // With PROMEKI_ENABLE_SRC the push resamples; without it,
        // it returns NotSupported.
#if PROMEKI_ENABLE_SRC
        CHECK(ab.push(samples, 2, src44).isOk());
#else
        CHECK(ab.push(samples, 2, src44) == Error::NotSupported);
#endif
}

TEST_CASE("AudioBuffer: push with mismatched channel count returns NotSupported") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        AudioDesc   srcMono(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        int16_t     samples[4] = {};
        CHECK(ab.push(samples, 2, srcMono) == Error::NotSupported);
}

// ============================================================================
// Drop / peek / clear
// ============================================================================

TEST_CASE("AudioBuffer: drop advances head without copying") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t     samples[16] = {1, 2, 3, 4, 5, 6, 7, 8};
        ab.push(samples, 4, s16LE48k2ch());
        auto [dropped, dropErr] = ab.drop(2);
        CHECK(dropErr.isOk());
        CHECK(dropped == 2);
        CHECK(ab.available() == 2);
        int16_t out[4] = {};
        auto [popped, popErr] = ab.pop(out, 4);
        CHECK(popped == 2);
        CHECK(out[0] == 5);
        CHECK(out[1] == 6);
        CHECK(out[2] == 7);
        CHECK(out[3] == 8);
}

TEST_CASE("AudioBuffer: peek does not consume") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t     samples[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        ab.push(samples, 4, s16LE48k2ch());
        int16_t out[8] = {};
        auto [peeked, peekErr] = ab.peek(out, 4);
        CHECK(peekErr.isOk());
        CHECK(peeked == 4);
        CHECK(ab.available() == 4);
        CHECK(out[0] == 1);
        CHECK(out[7] == 8);
}

TEST_CASE("AudioBuffer: clear resets the FIFO") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t     samples[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        ab.push(samples, 4, s16LE48k2ch());
        ab.clear();
        CHECK(ab.available() == 0);
        CHECK(ab.isEmpty());
        // Capacity is preserved.
        CHECK(ab.capacity() == 16);
}

TEST_CASE("AudioBuffer: grow preserves existing samples") {
        AudioBuffer ab(s16LE48k2ch(), 4);
        int16_t     samples[4] = {1, 2, 3, 4};
        ab.push(samples, 2, s16LE48k2ch());
        REQUIRE(ab.reserve(32).isOk());
        CHECK(ab.capacity() == 32);
        CHECK(ab.available() == 2);
        int16_t out[4] = {};
        auto [popped, popErr] = ab.pop(out, 2);
        CHECK(popped == 2);
        CHECK(out[0] == 1);
        CHECK(out[3] == 4);
}

TEST_CASE("AudioBuffer: move semantics transfer ownership") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t     samples[4] = {10, 20, 30, 40};
        ab.push(samples, 2, s16LE48k2ch());
        AudioBuffer moved = std::move(ab);
        CHECK(moved.capacity() == 16);
        CHECK(moved.available() == 2);
        // After move, the source has been reset; its _count is 0.
        CHECK(ab.available() == 0);
        int16_t out[4] = {};
        auto [popped, popErr] = moved.pop(out, 2);
        CHECK(popped == 2);
        CHECK(out[0] == 10);
        CHECK(out[3] == 40);
}

TEST_CASE("AudioBuffer: move transfers gains, remap, meter, drift state") {
        AudioBuffer ab(f32LE48k2ch(), 64);
        REQUIRE(ab.setChannelGains({0.5f, 2.0f}).isOk());
        REQUIRE(ab.setChannelRemap({1, 0}).isOk());
        AudioPeakRmsMeter::Ptr meter = AudioPeakRmsMeter::Ptr::create(2u);
        ab.setMeter(meter);
#if PROMEKI_ENABLE_SRC
        REQUIRE(ab.enableDriftCorrection(32, 0.005).isOk());
        CHECK(ab.driftCorrectionEnabled());
#endif

        AudioBuffer moved = std::move(ab);
        CHECK(moved.channelGains().size() == 2);
        CHECK(moved.channelGains()[0] == doctest::Approx(0.5f));
        CHECK(moved.channelGains()[1] == doctest::Approx(2.0f));
        CHECK(moved.channelRemap().size() == 2);
        CHECK(moved.channelRemap()[0] == 1);
        CHECK(moved.channelRemap()[1] == 0);
        CHECK(moved.meter().isValid());
        CHECK(moved.meter().ptr() == meter.ptr());
#if PROMEKI_ENABLE_SRC
        CHECK(moved.driftCorrectionEnabled());
#endif

        // Move-assignment leaves the destination's prior state replaced.
        AudioBuffer assigned(f32LE48k2ch(), 32);
        assigned = std::move(moved);
        CHECK(assigned.channelGains().size() == 2);
        CHECK(assigned.channelRemap().size() == 2);
        CHECK(assigned.meter().isValid());
#if PROMEKI_ENABLE_SRC
        CHECK(assigned.driftCorrectionEnabled());
#endif
}

// ============================================================================
// Channel gain
// ============================================================================

TEST_CASE("AudioBuffer: setChannelGains rejects mismatched length") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(64);
        List<float> badLength = {1.0f, 1.0f, 1.0f};
        CHECK(ab.setChannelGains(badLength) == Error::InvalidArgument);
        // Empty list disables gain — should succeed.
        CHECK(ab.setChannelGains({}).isOk());
}

TEST_CASE("AudioBuffer: per-channel gain scales push samples") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        // Halve channel 0, double channel 1 (clamped to ±1.0 by floatToSamples).
        REQUIRE(ab.setChannelGains({0.5f, 1.0f}).isOk());

        // Push two stereo frames of 1.0 on both channels.
        const float in[] = {1.0f, 1.0f, 1.0f, 1.0f};
        REQUIRE(ab.push(in, 2, f32LE48k2ch()).isOk());

        float out[4] = {};
        auto [n, err] = ab.pop(out, 2);
        CHECK(n == 2);
        // Channel 0 was halved, channel 1 unchanged.
        CHECK(out[0] == doctest::Approx(0.5f));
        CHECK(out[1] == doctest::Approx(1.0f));
        CHECK(out[2] == doctest::Approx(0.5f));
        CHECK(out[3] == doctest::Approx(1.0f));
}

// ============================================================================
// Channel remap
// ============================================================================

TEST_CASE("AudioBuffer: setChannelRemap rejects mismatched length") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(64);
        List<int> badLength = {0, 1, 2};
        CHECK(ab.setChannelRemap(badLength) == Error::InvalidArgument);
        CHECK(ab.setChannelRemap({}).isOk());
}

TEST_CASE("AudioBuffer: remap swaps channels") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        // Output ch 0 from input ch 1, output ch 1 from input ch 0.
        REQUIRE(ab.setChannelRemap({1, 0}).isOk());

        const float in[] = {0.25f, 0.75f, 0.10f, 0.90f}; // 2 stereo frames
        REQUIRE(ab.push(in, 2, f32LE48k2ch()).isOk());

        float out[4] = {};
        auto [n, err] = ab.pop(out, 2);
        CHECK(n == 2);
        CHECK(out[0] == doctest::Approx(0.75f)); // was input ch 1
        CHECK(out[1] == doctest::Approx(0.25f)); // was input ch 0
        CHECK(out[2] == doctest::Approx(0.90f));
        CHECK(out[3] == doctest::Approx(0.10f));
}

TEST_CASE("AudioBuffer: remap with -1 fills silence") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        REQUIRE(ab.setChannelRemap({0, -1}).isOk());

        const float in[] = {0.5f, 0.6f, 0.7f, 0.8f};
        REQUIRE(ab.push(in, 2, f32LE48k2ch()).isOk());

        float out[4] = {};
        auto [n, err] = ab.pop(out, 2);
        CHECK(n == 2);
        CHECK(out[0] == doctest::Approx(0.5f));
        CHECK(out[1] == doctest::Approx(0.0f));
        CHECK(out[2] == doctest::Approx(0.7f));
        CHECK(out[3] == doctest::Approx(0.0f));
}

TEST_CASE("AudioBuffer: remap accepts mismatched input channel count") {
        // Output is stereo, input is mono — remap directs both output channels
        // to the same source channel (mono → stereo upmix).
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        REQUIRE(ab.setChannelRemap({0, 0}).isOk());

        AudioDesc   monoIn(AudioFormat::PCMI_Float32LE, 48000.0f, 1);
        const float in[] = {0.3f, 0.4f};
        REQUIRE(ab.push(in, 2, monoIn).isOk());

        float out[4] = {};
        auto [n, err] = ab.pop(out, 2);
        CHECK(n == 2);
        CHECK(out[0] == doctest::Approx(0.3f));
        CHECK(out[1] == doctest::Approx(0.3f));
        CHECK(out[2] == doctest::Approx(0.4f));
        CHECK(out[3] == doctest::Approx(0.4f));
}

// ============================================================================
// Metering
// ============================================================================

TEST_CASE("AudioBuffer: AudioPeakRmsMeter observes per-channel peaks") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        auto meter = AudioPeakRmsMeter::Ptr::create(2);
        ab.setMeter(meter);

        // Channel 0: peak 0.7 (after intermediate 0.3).
        // Channel 1: peak 0.4 (after intermediate -0.4).
        const float in[] = {0.3f, -0.4f, 0.7f, 0.2f};
        REQUIRE(ab.push(in, 2, f32LE48k2ch()).isOk());

        CHECK(meter->peak(0) == doctest::Approx(0.7f));
        CHECK(meter->peak(1) == doctest::Approx(0.4f));
}

TEST_CASE("AudioBuffer: meter sees post-gain samples") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        REQUIRE(ab.setChannelGains({0.5f, 1.0f}).isOk());
        auto meter = AudioPeakRmsMeter::Ptr::create(2);
        ab.setMeter(meter);

        const float in[] = {1.0f, 1.0f}; // 1 stereo frame, both channels at full scale
        REQUIRE(ab.push(in, 1, f32LE48k2ch()).isOk());

        // Channel 0 was scaled to 0.5; channel 1 stayed at 1.0.
        CHECK(meter->peak(0) == doctest::Approx(0.5f));
        CHECK(meter->peak(1) == doctest::Approx(1.0f));
}

TEST_CASE("AudioBuffer: meter reset clears peaks") {
        auto meter = AudioPeakRmsMeter::Ptr::create(2);
        meter.modify()->process((const float[]){0.9f, 0.5f}, 1, 2);
        CHECK(meter->peak(0) == doctest::Approx(0.9f));
        meter.modify()->reset();
        CHECK(meter->peak(0) == doctest::Approx(0.0f));
}

TEST_CASE("AudioBuffer: setMeter(nullptr) clears the meter") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        auto meter = AudioPeakRmsMeter::Ptr::create(2);
        ab.setMeter(meter);
        CHECK(ab.meter().isValid());
        ab.setMeter(AudioMeter::Ptr());
        CHECK_FALSE(ab.meter().isValid());
}

// ============================================================================
// Channel-count mismatch enforcement (legacy contract preserved)
// ============================================================================

TEST_CASE("AudioBuffer: push refuses mismatched channel count without remap") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        AudioDesc   monoIn(AudioFormat::PCMI_Float32LE, 48000.0f, 1);
        const float in[] = {0.5f};
        Error       err = ab.push(in, 1, monoIn);
        CHECK(err == Error::NotSupported);
}

// ============================================================================
// Stream-aware channel map propagation
// ============================================================================

TEST_CASE("AudioBuffer: push copies input stream/role pairs to output map") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);

        AudioStreamDesc main("Main");
        AudioDesc       in(AudioFormat::PCMI_Float32LE, 48000.0f,
                           AudioChannelMap(main, {ChannelRole::FrontLeft, ChannelRole::FrontRight}));
        const float     buf[] = {0.1f, 0.2f, 0.3f, 0.4f};
        REQUIRE(ab.push(buf, 2, in).isOk());

        // Output map mirrors the input pairs after a 1-to-1 push.
        CHECK(ab.format().channelMap().stream(0) == main);
        CHECK(ab.format().channelMap().role(1) == ChannelRole::FrontRight);
}

TEST_CASE("AudioBuffer: remap reorders the stream/role pairs in the output map") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        REQUIRE(ab.setChannelRemap({1, 0}).isOk()); // swap the channels

        AudioStreamDesc main("Main");
        AudioStreamDesc com("Commentary");
        AudioDesc       in(AudioFormat::PCMI_Float32LE, 48000.0f,
                           AudioChannelMap{
                             AudioChannelMap::Entry(main, ChannelRole::FrontLeft),
                             AudioChannelMap::Entry(com, ChannelRole::Mono),
                     });
        const float     buf[] = {0.1f, 0.2f};
        REQUIRE(ab.push(buf, 1, in).isOk());

        // After remap {1, 0}: output[0] picks up input[1], output[1] picks up input[0].
        CHECK(ab.format().channelMap().stream(0) == com);
        CHECK(ab.format().channelMap().role(0) == ChannelRole::Mono);
        CHECK(ab.format().channelMap().stream(1) == main);
        CHECK(ab.format().channelMap().role(1) == ChannelRole::FrontLeft);
}

TEST_CASE("AudioBuffer: remap with -1 yields (Undefined, Unused) on the output map") {
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(16);
        REQUIRE(ab.setChannelRemap({0, -1}).isOk());

        AudioStreamDesc main("Main");
        AudioDesc       in(AudioFormat::PCMI_Float32LE, 48000.0f,
                           AudioChannelMap(main, {ChannelRole::FrontLeft, ChannelRole::FrontRight}));
        const float     buf[] = {0.5f, 0.6f};
        REQUIRE(ab.push(buf, 1, in).isOk());

        CHECK(ab.format().channelMap().stream(0) == main);
        CHECK(ab.format().channelMap().role(0) == ChannelRole::FrontLeft);
        CHECK(ab.format().channelMap().stream(1).isUndefined());
        CHECK(ab.format().channelMap().role(1) == ChannelRole::Unused);
}

#if PROMEKI_ENABLE_SRC
TEST_CASE("AudioBuffer: resampler push still propagates the channel map") {
        // Output 48k stereo, input 44.1k stereo with explicit Main stream.
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(2048);

        AudioStreamDesc main("Main");
        AudioDesc       in(AudioFormat::PCMI_Float32LE, 44100.0f,
                           AudioChannelMap(main, {ChannelRole::FrontLeft, ChannelRole::FrontRight}));
        // 64 frames of silence is enough to exercise the resampler path
        // — we're checking the descriptor refresh, not the audio.
        float buf[64 * 2] = {};
        REQUIRE(ab.push(buf, 64, in).isOk());

        CHECK(ab.format().channelMap().stream(0) == main);
        CHECK(ab.format().channelMap().role(0) == ChannelRole::FrontLeft);
        CHECK(ab.format().channelMap().stream(1) == main);
}
#endif
