/**
 * @file      audiobuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/audio.h>

using namespace promeki;

namespace {

AudioDesc s16LE48k2ch() { return AudioDesc(AudioDesc::PCMI_S16LE, 48000.0f, 2); }
AudioDesc f32LE48k2ch() { return AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2); }

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
        int16_t samples[8] = { 100, -100, 200, -200, 300, -300, 400, -400 };
        // 8 bytes per 4-sample stereo s16 frame → 4 samples.
        CHECK(ab.push(samples, 4, s16LE48k2ch()).isOk());
        CHECK(ab.available() == 4);

        int16_t out[8] = {};
        size_t popped = ab.pop(out, 4);
        CHECK(popped == 4);
        CHECK(ab.available() == 0);
        for(int i = 0; i < 8; ++i) CHECK(out[i] == samples[i]);
}

TEST_CASE("AudioBuffer: pop more than available returns partial") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        int16_t samples[4] = { 1, 2, 3, 4 };
        CHECK(ab.push(samples, 2, s16LE48k2ch()).isOk());
        int16_t out[8] = { 0 };
        size_t popped = ab.pop(out, 8);
        CHECK(popped == 2);
        CHECK(ab.isEmpty());
}

TEST_CASE("AudioBuffer: pop from empty returns 0") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t out[4] = { 1, 2, 3, 4 };
        CHECK(ab.pop(out, 4) == 0);
        // Destination untouched.
        CHECK(out[0] == 1);
}

TEST_CASE("AudioBuffer: push over capacity returns NoSpace") {
        AudioBuffer ab(s16LE48k2ch(), 4);
        int16_t samples[16] = {};
        Error err = ab.push(samples, 5, s16LE48k2ch());
        CHECK(err == Error::NoSpace);
        CHECK(ab.isEmpty());
}

// ============================================================================
// Ring wraparound
// ============================================================================

TEST_CASE("AudioBuffer: push/pop across the wraparound boundary") {
        AudioBuffer ab(s16LE48k2ch(), 4);
        int16_t a[4] = { 1, 2, 3, 4 }; // 2 samples
        int16_t b[8] = { 5, 6, 7, 8, 9, 10, 11, 12 }; // 4 samples

        REQUIRE(ab.push(a, 2, s16LE48k2ch()).isOk());
        CHECK(ab.available() == 2);

        // Pop one sample to move head forward.
        int16_t out[2] = {};
        CHECK(ab.pop(out, 1) == 1);
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
        CHECK(ab.pop(full, 4) == 4);
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
// Push through an Audio object
// ============================================================================

TEST_CASE("AudioBuffer: push via Audio, pop via Audio") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        Audio in(s16LE48k2ch(), 8);
        int16_t *data = in.data<int16_t>();
        for(size_t i = 0; i < 16; ++i) data[i] = static_cast<int16_t>(i * 100);

        REQUIRE(ab.push(in).isOk());
        CHECK(ab.available() == 8);

        Audio out(s16LE48k2ch(), 16);
        size_t popped = ab.pop(out, 8);
        CHECK(popped == 8);
        CHECK(out.samples() == 8);
        const int16_t *outData = out.data<int16_t>();
        for(size_t i = 0; i < 16; ++i) CHECK(outData[i] == static_cast<int16_t>(i * 100));
}

// ============================================================================
// Format conversion (float32 → int16 at the same rate/channels)
// ============================================================================

TEST_CASE("AudioBuffer: push float32, pop int16 at same rate") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        ab.setInputFormat(f32LE48k2ch());

        // 4 stereo samples (8 float values)
        float src[8] = { 0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.25f, -0.25f, 0.0f };
        REQUIRE(ab.push(src, 4, f32LE48k2ch()).isOk());
        CHECK(ab.available() == 4);

        int16_t out[8] = {};
        CHECK(ab.pop(out, 4) == 4);
        // After round-trip, values are clamped + quantized to s16 range.
        CHECK(out[0] == 0);
        CHECK(out[1] == 32767);
        CHECK(out[2] == -32768);
        CHECK(out[3] > 16000);     // ~0.5 × 32767
        CHECK(out[3] < 17000);
        CHECK(out[4] < -16000);
        CHECK(out[4] > -17000);
}

// ============================================================================
// Non-supported conversions
// ============================================================================

TEST_CASE("AudioBuffer: push with mismatched sample rate returns NotSupported") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        AudioDesc src44(AudioDesc::PCMI_S16LE, 44100.0f, 2);
        int16_t samples[4] = {};
        CHECK(ab.push(samples, 2, src44) == Error::NotSupported);
}

TEST_CASE("AudioBuffer: push with mismatched channel count returns NotSupported") {
        AudioBuffer ab(s16LE48k2ch(), 64);
        AudioDesc srcMono(AudioDesc::PCMI_S16LE, 48000.0f, 1);
        int16_t samples[4] = {};
        CHECK(ab.push(samples, 2, srcMono) == Error::NotSupported);
}

// ============================================================================
// Drop / peek / clear
// ============================================================================

TEST_CASE("AudioBuffer: drop advances head without copying") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t samples[16] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        ab.push(samples, 4, s16LE48k2ch());
        CHECK(ab.drop(2) == 2);
        CHECK(ab.available() == 2);
        int16_t out[4] = {};
        CHECK(ab.pop(out, 4) == 2);
        CHECK(out[0] == 5);
        CHECK(out[1] == 6);
        CHECK(out[2] == 7);
        CHECK(out[3] == 8);
}

TEST_CASE("AudioBuffer: peek does not consume") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t samples[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        ab.push(samples, 4, s16LE48k2ch());
        int16_t out[8] = {};
        CHECK(ab.peek(out, 4) == 4);
        CHECK(ab.available() == 4);
        CHECK(out[0] == 1);
        CHECK(out[7] == 8);
}

TEST_CASE("AudioBuffer: clear resets the FIFO") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t samples[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        ab.push(samples, 4, s16LE48k2ch());
        ab.clear();
        CHECK(ab.available() == 0);
        CHECK(ab.isEmpty());
        // Capacity is preserved.
        CHECK(ab.capacity() == 16);
}

TEST_CASE("AudioBuffer: grow preserves existing samples") {
        AudioBuffer ab(s16LE48k2ch(), 4);
        int16_t samples[4] = { 1, 2, 3, 4 };
        ab.push(samples, 2, s16LE48k2ch());
        REQUIRE(ab.reserve(32).isOk());
        CHECK(ab.capacity() == 32);
        CHECK(ab.available() == 2);
        int16_t out[4] = {};
        CHECK(ab.pop(out, 2) == 2);
        CHECK(out[0] == 1);
        CHECK(out[3] == 4);
}

TEST_CASE("AudioBuffer: move semantics transfer ownership") {
        AudioBuffer ab(s16LE48k2ch(), 16);
        int16_t samples[4] = { 10, 20, 30, 40 };
        ab.push(samples, 2, s16LE48k2ch());
        AudioBuffer moved = std::move(ab);
        CHECK(moved.capacity() == 16);
        CHECK(moved.available() == 2);
        // After move, the source has been reset; its _count is 0.
        CHECK(ab.available() == 0);
        int16_t out[4] = {};
        CHECK(moved.pop(out, 2) == 2);
        CHECK(out[0] == 10);
        CHECK(out[3] == 40);
}
