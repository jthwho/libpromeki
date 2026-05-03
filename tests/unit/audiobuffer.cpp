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

// ============================================================================
// Planar src → interleaved dst (no float processing — uses the byte-transpose
// fastpath in AudioFormat::convertTo)
// ============================================================================

TEST_CASE("AudioBuffer: push planar float, pop interleaved float (transpose fastpath)") {
        AudioBuffer ab(f32LE48k2ch(), 64);
        AudioDesc   planarIn(AudioFormat::PCMP_Float32LE, 48000.0f, 2);

        // 3 stereo frames, planar layout: [L0,L1,L2, R0,R1,R2].
        const float in[6] = {1.0f, 2.0f, 3.0f, 10.0f, 20.0f, 30.0f};
        REQUIRE(ab.push(in, 3, planarIn).isOk());

        float out[6] = {};
        auto [popped, err] = ab.pop(out, 3);
        CHECK(err.isOk());
        CHECK(popped == 3);
        // Expected interleaved: [L0,R0, L1,R1, L2,R2].
        CHECK(out[0] == 1.0f);
        CHECK(out[1] == 10.0f);
        CHECK(out[2] == 2.0f);
        CHECK(out[3] == 20.0f);
        CHECK(out[4] == 3.0f);
        CHECK(out[5] == 30.0f);
}

TEST_CASE("AudioBuffer: push planar S16, pop interleaved S16 (byte transpose)") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        AudioDesc   planarIn(AudioFormat::PCMP_S16LE, 48000.0f, 2);

        // 3 stereo frames, planar S16: [L0,L1,L2, R0,R1,R2].
        const int16_t in[6] = {100, 200, 300, 1000, 2000, 3000};
        REQUIRE(ab.push(in, 3, planarIn).isOk());

        int16_t out[6] = {};
        auto [popped, err] = ab.pop(out, 3);
        CHECK(err.isOk());
        CHECK(popped == 3);
        // Expected interleaved: [L0,R0, L1,R1, L2,R2].
        CHECK(out[0] == 100);
        CHECK(out[1] == 1000);
        CHECK(out[2] == 200);
        CHECK(out[3] == 2000);
        CHECK(out[4] == 300);
        CHECK(out[5] == 3000);
}

TEST_CASE("AudioBuffer: push planar S16, pop interleaved float (cross-format + transpose)") {
        // Cross-layout AND cross-byte-format — exercises the via-float
        // path inside AudioFormat::convertTo with explicit transpose.
        AudioBuffer ab(f32LE48k2ch(), 64);
        AudioDesc   planarIn(AudioFormat::PCMP_S16LE, 48000.0f, 2);

        // 2 stereo frames, planar S16:
        //   ch0 = [0x4000, 0x0000]  → ~0.5,  ~0.0
        //   ch1 = [0xC000, 0x7FFF]  → ~-0.5, ~+1.0
        const int16_t in[4] = {0x4000, 0x0000, static_cast<int16_t>(0xC000), 0x7FFF};
        REQUIRE(ab.push(in, 2, planarIn).isOk());

        float out[4] = {};
        auto [popped, err] = ab.pop(out, 2);
        CHECK(err.isOk());
        CHECK(popped == 2);
        // Expected interleaved: [L0,R0, L1,R1] = [~0.5,~-0.5, ~0.0,~+1.0].
        CHECK(out[0] == doctest::Approx(0.5f).epsilon(0.001));
        CHECK(out[1] == doctest::Approx(-0.5f).epsilon(0.001));
        CHECK(out[2] == doctest::Approx(0.0f).epsilon(0.001));
        CHECK(out[3] == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("AudioBuffer: planar push wraps the ring correctly") {
        // Force a wrap by leaving the ring half-full and then pushing
        // enough planar samples to cross the boundary.  The planar
        // converter has to spill through a temp buffer (it can't split
        // a planar source at frame boundaries) — verify that the spill
        // is then memcpy'd through the wrap properly.
        AudioBuffer ab(f32LE48k2ch(), 4);
        AudioDesc   planarIn(AudioFormat::PCMP_Float32LE, 48000.0f, 2);

        // Push 2 frames first (no wrap), then pop 1 to advance head.
        const float warmup[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        REQUIRE(ab.push(warmup, 2, f32LE48k2ch()).isOk());
        float discard[2] = {};
        auto [popDiscard, popDiscardErr] = ab.pop(discard, 1);
        REQUIRE(popDiscardErr.isOk());

        // Now push 3 planar frames into a ring with head=1, tail=2,
        // capacity=4 — the 3rd frame wraps to slot 0.
        const float in[6] = {1.0f, 2.0f, 3.0f, 10.0f, 20.0f, 30.0f};
        REQUIRE(ab.push(in, 3, planarIn).isOk());
        CHECK(ab.available() == 4);

        // Drain everything.  The first pop returns the leftover warmup
        // frame (zero), then the three transposed frames in order.
        float out[8] = {};
        auto [popped, err] = ab.pop(out, 4);
        CHECK(err.isOk());
        CHECK(popped == 4);
        CHECK(out[0] == 0.0f); // warmup
        CHECK(out[1] == 0.0f);
        CHECK(out[2] == 1.0f);  // L0
        CHECK(out[3] == 10.0f); // R0
        CHECK(out[4] == 2.0f);
        CHECK(out[5] == 20.0f);
        CHECK(out[6] == 3.0f);
        CHECK(out[7] == 30.0f);
}

TEST_CASE("AudioBuffer: planar push with gain forces via-float path and stays correct") {
        // With gain active, planar src takes the via-float path (the
        // no-processing fastpath is bypassed).  Step 1 of the via-float
        // path must transpose planar → interleaved before the gain loop
        // runs, otherwise channel 0 gets channel-1's gain and vice-versa.
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(64);
        REQUIRE(ab.setChannelGains({0.5f, 2.0f}).isOk());

        AudioDesc   planarIn(AudioFormat::PCMP_Float32LE, 48000.0f, 2);
        // 2 stereo frames, planar: ch0=[0.4,0.6], ch1=[0.1,0.3].
        const float in[4] = {0.4f, 0.6f, 0.1f, 0.3f};
        REQUIRE(ab.push(in, 2, planarIn).isOk());

        float out[4] = {};
        auto [n, err] = ab.pop(out, 2);
        CHECK(n == 2);
        // After gain: ch0 ×0.5, ch1 ×2.0, interleaved.
        CHECK(out[0] == doctest::Approx(0.4f * 0.5f));
        CHECK(out[1] == doctest::Approx(0.1f * 2.0f));
        CHECK(out[2] == doctest::Approx(0.6f * 0.5f));
        CHECK(out[3] == doctest::Approx(0.3f * 2.0f));
}

TEST_CASE("AudioBuffer: planar push with remap routes through interleaved float") {
        // Remap forces via-float; with a planar source, step 1 must
        // transpose to interleaved before the remap loop reads frames.
        AudioBuffer ab(f32LE48k2ch());
        ab.reserve(64);
        REQUIRE(ab.setChannelRemap({1, 0}).isOk()); // swap channels

        AudioDesc   planarIn(AudioFormat::PCMP_Float32LE, 48000.0f, 2);
        const float in[4] = {0.4f, 0.6f, 0.1f, 0.3f}; // ch0=[0.4,0.6], ch1=[0.1,0.3]
        REQUIRE(ab.push(in, 2, planarIn).isOk());

        float out[4] = {};
        auto [n, err] = ab.pop(out, 2);
        CHECK(n == 2);
        // Output[0] ← input ch1, output[1] ← input ch0 — interleaved.
        CHECK(out[0] == doctest::Approx(0.1f));
        CHECK(out[1] == doctest::Approx(0.4f));
        CHECK(out[2] == doctest::Approx(0.3f));
        CHECK(out[3] == doctest::Approx(0.6f));
}

// ============================================================================
// pushSilence
// ============================================================================

TEST_CASE("AudioBuffer: pushSilence on invalid format fails") {
        AudioBuffer ab;
        CHECK(ab.pushSilence(10) == Error::InvalidArgument);
}

TEST_CASE("AudioBuffer: pushSilence with zero samples is a no-op") {
        AudioBuffer ab(f32LE48k2ch(), 32);
        CHECK(ab.pushSilence(0).isOk());
        CHECK(ab.available() == 0);
}

TEST_CASE("AudioBuffer: pushSilence beyond capacity returns NoSpace") {
        AudioBuffer ab(f32LE48k2ch(), 16);
        CHECK(ab.pushSilence(32) == Error::NoSpace);
        CHECK(ab.available() == 0);
}

TEST_CASE("AudioBuffer: pushSilence writes float zeros") {
        AudioBuffer ab(f32LE48k2ch(), 16);
        REQUIRE(ab.pushSilence(8).isOk());
        CHECK(ab.available() == 8);
        float out[16] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
                         9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f};
        auto [n, err] = ab.pop(out, 8);
        CHECK(n == 8);
        for (int i = 0; i < 16; ++i) CHECK(out[i] == 0.0f);
}

TEST_CASE("AudioBuffer: pushSilence into S16 storage writes 0x0000") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        REQUIRE(ab.pushSilence(4).isOk());
        int16_t out[8] = {123, -456, 789, -1011, 1213, -1415, 1617, -1819};
        auto [n, err]  = ab.pop(out, 4);
        CHECK(n == 4);
        for (int i = 0; i < 8; ++i) CHECK(out[i] == 0);
}

TEST_CASE("AudioBuffer: pushSilence into U8 storage writes the format's silence midpoint") {
        // Unsigned PCM silence is the midpoint, not zero bits.  Verify
        // pushSilence routes through floatToSamples so the conversion
        // is correct.  The exact midpoint value is whatever
        // floatToSamples(0.0f) produces — for u8 with Min=0 Max=255
        // that's truncation of 127.5 → 127.  Reading the value back
        // through samplesToFloat / floatToSamples (i.e. via push of a
        // zero-float reference) ensures we compare against the
        // library's own silence, not WAV's 0x80 convention.
        AudioDesc      u8desc(AudioFormat::PCMI_U8, 48000.0f, 2);
        AudioBuffer    ab(u8desc, 16);
        REQUIRE(ab.pushSilence(4).isOk());
        uint8_t silenceOut[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        auto [n, err]         = ab.pop(silenceOut, 4);
        CHECK(n == 4);

        // Reference: push 4 frames of native float zero into a fresh
        // ring and read back its u8 representation.  That's the
        // ground truth the library considers "silence" for u8.
        AudioBuffer    ref(u8desc, 16);
        AudioDesc      floatDesc(AudioFormat::PCMI_Float32LE, 48000.0f, 2);
        const float    zeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        REQUIRE(ref.push(zeros, 4, floatDesc).isOk());
        uint8_t refOut[8] = {0};
        auto [rn, rerr]   = ref.pop(refOut, 4);
        REQUIRE(rn == 4);

        for (int i = 0; i < 8; ++i) CHECK(silenceOut[i] == refOut[i]);
}

TEST_CASE("AudioBuffer: pushSilence handles wrap across the ring boundary") {
        AudioBuffer ab(f32LE48k2ch(), 8);
        // Push 6 frames (12 floats), pop 4 frames (8 floats) — leaves
        // _head at frame 4, _tail at frame 6, _count = 2.  pushSilence(4)
        // must wrap from frame 6 around to frame 2.
        AudioDesc   srcDesc(AudioFormat::PCMI_Float32LE, 48000.0f, 2);
        const float seed[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        REQUIRE(ab.push(seed, 6, srcDesc).isOk());
        float drain[8] = {};
        auto [drained, derr] = ab.pop(drain, 4);
        CHECK(drained == 4);
        REQUIRE(ab.pushSilence(4).isOk());
        CHECK(ab.available() == 6);

        // Pop everything: first 2 frames are the seed remainder
        // (frames 4 and 5, floats 9..12), next 4 frames are silence.
        float out[12] = {};
        auto [n, err] = ab.pop(out, 6);
        CHECK(n == 6);
        CHECK(out[0] == 9.0f);
        CHECK(out[1] == 10.0f);
        CHECK(out[2] == 11.0f);
        CHECK(out[3] == 12.0f);
        for (int i = 4; i < 12; ++i) CHECK(out[i] == 0.0f);
}

