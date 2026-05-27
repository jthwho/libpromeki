/**
 * @file      audioconformancelevel.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums_audio.h>

using namespace promeki;

// ============================================================================
// AudioConformanceLevel basics
// ============================================================================

TEST_CASE("AudioConformanceLevel: default is None") {
        AudioConformanceLevel l;
        CHECK(l == AudioConformanceLevel::None);
}

TEST_CASE("AudioConformanceLevel: value names match ST 2110-30:2025 §7") {
        CHECK(AudioConformanceLevel::None.valueName() == "None");
        CHECK(AudioConformanceLevel::A.valueName() == "A");
        CHECK(AudioConformanceLevel::AX.valueName() == "AX");
        CHECK(AudioConformanceLevel::B.valueName() == "B");
        CHECK(AudioConformanceLevel::BX.valueName() == "BX");
        CHECK(AudioConformanceLevel::C.valueName() == "C");
        CHECK(AudioConformanceLevel::CX.valueName() == "CX");
}

// ============================================================================
// AudioConformanceLevel::compute — Table 2 (Senders) rows
// ============================================================================

TEST_CASE("AudioConformanceLevel::compute Level A: 48 kHz / 1ms / 1-8 ch") {
        // Level A is mandatory for every ST 2110-30 sender and the
        // most commonly interoperable shape (broadcast IP plant
        // baseline).
        CHECK(AudioConformanceLevel::compute(48000, 1000, 1) == AudioConformanceLevel::A);
        CHECK(AudioConformanceLevel::compute(48000, 1000, 2) == AudioConformanceLevel::A);
        CHECK(AudioConformanceLevel::compute(48000, 1000, 8) == AudioConformanceLevel::A);
        // 9 channels at A's rate / ptime is outside Level A's range
        // and does not match any other sender row (Level C requires
        // 125 µs).
        CHECK(AudioConformanceLevel::compute(48000, 1000, 9) == AudioConformanceLevel::None);
}

TEST_CASE("AudioConformanceLevel::compute Level AX: 96 kHz / 1ms / 1-4 ch") {
        CHECK(AudioConformanceLevel::compute(96000, 1000, 1) == AudioConformanceLevel::AX);
        CHECK(AudioConformanceLevel::compute(96000, 1000, 4) == AudioConformanceLevel::AX);
        // 5 channels at 96k/1ms exceeds Level AX (1-4) and matches
        // nothing else — Level BX requires 125 µs.
        CHECK(AudioConformanceLevel::compute(96000, 1000, 5) == AudioConformanceLevel::None);
}

TEST_CASE("AudioConformanceLevel::compute Level B: 48 kHz / 125µs / 1-8 ch") {
        CHECK(AudioConformanceLevel::compute(48000, 125, 1) == AudioConformanceLevel::B);
        CHECK(AudioConformanceLevel::compute(48000, 125, 8) == AudioConformanceLevel::B);
}

TEST_CASE("AudioConformanceLevel::compute Level BX: 96 kHz / 125µs / 1-8 ch") {
        CHECK(AudioConformanceLevel::compute(96000, 125, 1) == AudioConformanceLevel::BX);
        CHECK(AudioConformanceLevel::compute(96000, 125, 8) == AudioConformanceLevel::BX);
}

TEST_CASE("AudioConformanceLevel::compute Level C: 48 kHz / 125µs / 9-64 ch") {
        // The 8↔9 channel boundary distinguishes Level B from Level
        // C; Level C also caps at 64 channels.
        CHECK(AudioConformanceLevel::compute(48000, 125, 9) == AudioConformanceLevel::C);
        CHECK(AudioConformanceLevel::compute(48000, 125, 16) == AudioConformanceLevel::C);
        CHECK(AudioConformanceLevel::compute(48000, 125, 64) == AudioConformanceLevel::C);
        CHECK(AudioConformanceLevel::compute(48000, 125, 65) == AudioConformanceLevel::None);
}

TEST_CASE("AudioConformanceLevel::compute Level CX: 96 kHz / 125µs / 9-32 ch") {
        // Level CX caps at 32 channels (not 64) because doubling
        // the sample rate halves the per-channel bandwidth at the
        // standard 1500-byte MTU.
        CHECK(AudioConformanceLevel::compute(96000, 125, 9) == AudioConformanceLevel::CX);
        CHECK(AudioConformanceLevel::compute(96000, 125, 32) == AudioConformanceLevel::CX);
        CHECK(AudioConformanceLevel::compute(96000, 125, 33) == AudioConformanceLevel::None);
}

// ============================================================================
// AudioConformanceLevel::compute — out-of-spec combinations
// ============================================================================

TEST_CASE("AudioConformanceLevel::compute: 44.1 kHz never matches a Table-2 row") {
        // ST 2110-30:2025 §6.1 says 44.1 kHz "should" be supported
        // but Table 2's six sender levels all pin to 48 kHz or
        // 96 kHz.  A 44.1 kHz / 1 ms / 2 ch AES67 stream is valid
        // AES67 but not ST 2110-30 conformant.
        CHECK(AudioConformanceLevel::compute(44100, 1000, 2) == AudioConformanceLevel::None);
        CHECK(AudioConformanceLevel::compute(44100, 125, 8) == AudioConformanceLevel::None);
}

TEST_CASE("AudioConformanceLevel::compute: AES67 250µs / 333µs / 4ms are None") {
        // AES67 §7.2.2 recommends 250 µs, 333 µs and 4 ms in
        // addition to the required 1 ms and 125 µs.  ST 2110-30
        // only recognises 1 ms and 125 µs (Tables 2 and 3) so the
        // other packet times are AES67-only and never qualify for
        // a ST 2110-30 sender conformance level.
        CHECK(AudioConformanceLevel::compute(48000, 250, 2) == AudioConformanceLevel::None);
        CHECK(AudioConformanceLevel::compute(48000, 333, 2) == AudioConformanceLevel::None);
        CHECK(AudioConformanceLevel::compute(48000, 4000, 2) == AudioConformanceLevel::None);
}

TEST_CASE("AudioConformanceLevel::compute: zero-channel guard") {
        // Zero channels is not a real stream shape; the compute
        // helper must never claim a conformance level for it.
        CHECK(AudioConformanceLevel::compute(48000, 1000, 0) == AudioConformanceLevel::None);
        CHECK(AudioConformanceLevel::compute(48000, 125, 0) == AudioConformanceLevel::None);
}

// ============================================================================
// AudioWireFormat enum smoke-test
// ============================================================================

TEST_CASE("AudioWireFormat: default is Auto") {
        AudioWireFormat w;
        CHECK(w == AudioWireFormat::Auto);
}

TEST_CASE("AudioWireFormat: value names round-trip") {
        CHECK(AudioWireFormat::Auto.valueName() == "Auto");
        CHECK(AudioWireFormat::L16.valueName() == "L16");
        CHECK(AudioWireFormat::L24.valueName() == "L24");
}
