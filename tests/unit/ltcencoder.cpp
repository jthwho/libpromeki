/**
 * @file      ltcencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ltcencoder.h>
#include <promeki/ltcdecoder.h>

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
    List<int8_t> samples = enc.encode(tc);
    CHECK(!samples.isEmpty());
    // At 48kHz / 24fps, expect ~2000 samples per frame
    CHECK(samples.size() > 1500);
    CHECK(samples.size() < 2500);
}

TEST_CASE("LtcEncoder_Encode30fps") {
    LtcEncoder enc(48000, 0.5f);
    Timecode tc(Timecode::NDF30, 1, 0, 0, 0);
    List<int8_t> samples = enc.encode(tc);
    CHECK(!samples.isEmpty());
    // At 48kHz / 30fps, expect ~1600 samples per frame
    CHECK(samples.size() > 1200);
    CHECK(samples.size() < 2000);
}

// ============================================================================
// Different sample rates produce proportional sample counts
// ============================================================================

TEST_CASE("LtcEncoder_SampleRateProportional") {
    Timecode tc(Timecode::NDF24, 1, 0, 0, 0);

    LtcEncoder enc48(48000, 0.5f);
    List<int8_t> a48 = enc48.encode(tc);

    LtcEncoder enc96(96000, 0.5f);
    List<int8_t> a96 = enc96.encode(tc);

    // 96kHz should produce roughly 2x the samples of 48kHz
    double ratio = (double)a96.size() / (double)a48.size();
    CHECK(ratio > 1.8);
    CHECK(ratio < 2.2);
}

// ============================================================================
// Level affects output amplitude
// ============================================================================

TEST_CASE("LtcEncoder_LevelAffectsAmplitude") {
    Timecode tc(Timecode::NDF24, 1, 0, 0, 0);

    LtcEncoder encLow(48000, 0.25f);
    List<int8_t> aLow = encLow.encode(tc);

    LtcEncoder encHigh(48000, 0.75f);
    List<int8_t> aHigh = encHigh.encode(tc);

    // Find peak values
    int8_t peakLow = 0;
    int8_t peakHigh = 0;
    for(size_t i = 0; i < aLow.size(); i++) {
        int8_t v = aLow[i];
        if(v > peakLow) peakLow = v;
    }
    for(size_t i = 0; i < aHigh.size(); i++) {
        int8_t v = aHigh[i];
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
        List<int8_t> samples = enc.encode(tc);
        CHECK(!samples.isEmpty());
        ++tc;
    }
}

// ============================================================================
// Invalid timecode
// ============================================================================

TEST_CASE("LtcEncoder_InvalidTimecode") {
    LtcEncoder enc(48000, 0.5f);
    Timecode tc; // default/invalid
    List<int8_t> samples = enc.encode(tc);
    CHECK(samples.isEmpty());
}
