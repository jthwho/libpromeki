/**
 * @file      ltcencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/ltcencoder.h>
#include <promeki/proav/ltcdecoder.h>

using namespace promeki;

// ============================================================================
// Basic construction
// ============================================================================

TEST_CASE("LtcEncoder_Construct") {
    LtcEncoder enc(48000, 0.5f);
    CHECK(enc.sampleRate() == 48000);
    CHECK(enc.level() == doctest::Approx(0.5f));
}

// ============================================================================
// Encode produces reasonable sample count
// ============================================================================

TEST_CASE("LtcEncoder_EncodeSampleCount") {
    LtcEncoder enc(48000, 0.5f);
    Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
    Audio audio = enc.encode(tc);
    CHECK(audio.isValid());
    CHECK(audio.samples() > 0);
    // At 48kHz / 24fps, expect ~2000 samples per frame
    CHECK(audio.samples() > 1500);
    CHECK(audio.samples() < 2500);
}

TEST_CASE("LtcEncoder_Encode30fps") {
    LtcEncoder enc(48000, 0.5f);
    Timecode tc(Timecode::NDF30, 1, 0, 0, 0);
    Audio audio = enc.encode(tc);
    CHECK(audio.isValid());
    // At 48kHz / 30fps, expect ~1600 samples per frame
    CHECK(audio.samples() > 1200);
    CHECK(audio.samples() < 2000);
}

// ============================================================================
// Different sample rates produce proportional sample counts
// ============================================================================

TEST_CASE("LtcEncoder_SampleRateProportional") {
    Timecode tc(Timecode::NDF24, 1, 0, 0, 0);

    LtcEncoder enc48(48000, 0.5f);
    Audio a48 = enc48.encode(tc);

    LtcEncoder enc96(96000, 0.5f);
    Audio a96 = enc96.encode(tc);

    // 96kHz should produce roughly 2x the samples of 48kHz
    double ratio = (double)a96.samples() / (double)a48.samples();
    CHECK(ratio > 1.8);
    CHECK(ratio < 2.2);
}

// ============================================================================
// Level affects output amplitude
// ============================================================================

TEST_CASE("LtcEncoder_LevelAffectsAmplitude") {
    Timecode tc(Timecode::NDF24, 1, 0, 0, 0);

    LtcEncoder encLow(48000, 0.25f);
    Audio aLow = encLow.encode(tc);

    LtcEncoder encHigh(48000, 0.75f);
    Audio aHigh = encHigh.encode(tc);

    // Find peak values
    int8_t peakLow = 0;
    int8_t peakHigh = 0;
    for(size_t i = 0; i < aLow.samples(); i++) {
        int8_t v = aLow.data<int8_t>()[i];
        if(v > peakLow) peakLow = v;
    }
    for(size_t i = 0; i < aHigh.samples(); i++) {
        int8_t v = aHigh.data<int8_t>()[i];
        if(v > peakHigh) peakHigh = v;
    }
    CHECK(peakHigh > peakLow);
}

// ============================================================================
// setLevel
// ============================================================================

TEST_CASE("LtcEncoder_SetLevel") {
    LtcEncoder enc(48000, 0.5f);
    enc.setLevel(0.75f);
    CHECK(enc.level() == doctest::Approx(0.75f));
}

// ============================================================================
// frameSizeApprox
// ============================================================================

TEST_CASE("LtcEncoder_FrameSizeApprox") {
    LtcEncoder enc(48000, 0.5f);
    size_t approx = enc.frameSizeApprox(&VTC_FORMAT_24);
    CHECK(approx > 0);
    CHECK(approx > 1500);
    CHECK(approx < 2500);
}

// ============================================================================
// Encode sequential timecodes
// ============================================================================

TEST_CASE("LtcEncoder_Sequential") {
    LtcEncoder enc(48000, 0.5f);
    Timecode tc(Timecode::NDF24, 1, 0, 0, 0);

    for(int i = 0; i < 10; i++) {
        Audio audio = enc.encode(tc);
        CHECK(audio.isValid());
        CHECK(audio.samples() > 0);
        ++tc;
    }
}

// ============================================================================
// Invalid timecode
// ============================================================================

TEST_CASE("LtcEncoder_InvalidTimecode") {
    LtcEncoder enc(48000, 0.5f);
    Timecode tc; // default/invalid
    Audio audio = enc.encode(tc);
    CHECK(!audio.isValid());
}
