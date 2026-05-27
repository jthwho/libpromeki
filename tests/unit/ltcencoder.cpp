/**
 * @file      ltcencoder.cpp
 * @copyright Jason Howard. All rights reserved.
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
        LtcEncoder enc(48000, FrameRate(), 0.5f);
        CHECK(enc.sampleRate() == 48000);
        CHECK(enc.level() == doctest::Approx(0.5f));
}

// ============================================================================
// Encode produces reasonable sample count
// ============================================================================

TEST_CASE("LtcEncoder_EncodeSampleCount") {
        LtcEncoder   enc(48000, FrameRate(), 0.5f);
        Timecode     tc(Timecode::NDF24, 1, 0, 0, 0);
        List<int8_t> samples = enc.encode(tc);
        CHECK(!samples.isEmpty());
        // At 48kHz / 24fps, expect ~2000 samples per frame
        CHECK(samples.size() > 1500);
        CHECK(samples.size() < 2500);
}

TEST_CASE("LtcEncoder_Encode30fps") {
        LtcEncoder   enc(48000, FrameRate(), 0.5f);
        Timecode     tc(Timecode::NDF30, 1, 0, 0, 0);
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

        LtcEncoder   enc48(48000, FrameRate(), 0.5f);
        List<int8_t> a48 = enc48.encode(tc);

        LtcEncoder   enc96(96000, FrameRate(), 0.5f);
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

        LtcEncoder   encLow(48000, FrameRate(), 0.25f);
        List<int8_t> aLow = encLow.encode(tc);

        LtcEncoder   encHigh(48000, FrameRate(), 0.75f);
        List<int8_t> aHigh = encHigh.encode(tc);

        // Find peak values
        int8_t peakLow = 0;
        int8_t peakHigh = 0;
        for (size_t i = 0; i < aLow.size(); i++) {
                int8_t v = aLow[i];
                if (v > peakLow) peakLow = v;
        }
        for (size_t i = 0; i < aHigh.size(); i++) {
                int8_t v = aHigh[i];
                if (v > peakHigh) peakHigh = v;
        }
        CHECK(peakHigh > peakLow);
}

// ============================================================================
// setLevel
// ============================================================================

TEST_CASE("LtcEncoder_SetLevel") {
        LtcEncoder enc(48000, FrameRate(), 0.5f);
        enc.setLevel(0.75f);
        CHECK(enc.level() == doctest::Approx(0.75f));
}

// ============================================================================
// frameSizeApprox
// ============================================================================

TEST_CASE("LtcEncoder_FrameSizeApprox") {
        LtcEncoder enc(48000, FrameRate(), 0.5f);
        size_t     approx = enc.frameSizeApprox(&VTC_FORMAT_24);
        CHECK(approx > 0);
        CHECK(approx > 1500);
        CHECK(approx < 2500);
}

// ============================================================================
// Encode sequential timecodes
// ============================================================================

TEST_CASE("LtcEncoder_Sequential") {
        LtcEncoder enc(48000, FrameRate(), 0.5f);
        Timecode   tc(Timecode::NDF24, 1, 0, 0, 0);

        for (int i = 0; i < 10; i++) {
                List<int8_t> samples = enc.encode(tc);
                CHECK(!samples.isEmpty());
                ++tc;
        }
}

// ============================================================================
// Invalid timecode
// ============================================================================

TEST_CASE("LtcEncoder_InvalidTimecode") {
        LtcEncoder   enc(48000, FrameRate(), 0.5f);
        Timecode     tc; // default/invalid
        List<int8_t> samples = enc.encode(tc);
        CHECK(samples.isEmpty());
}

// ============================================================================
// Phase 3: HFR encoding produces proportional sample counts; explicit
// FrameRate overrides the Timecode's NTSC/integer choice.
// ============================================================================

TEST_CASE("LtcEncoder_Encode60p_per_video_frame_slice") {
        // At 60p the LTC codeword (at the 30 fps super-frame rate) spans two
        // video frames, so each encode() call emits half of a codeword's
        // audio = 800 samples at 48 kHz.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_60), 0.5f);
        Timecode   tc(Timecode::NDF60, 1, 0, 0, 0);
        List<int8_t> s0 = enc.encode(tc);
        ++tc;
        List<int8_t> s1 = enc.encode(tc);
        CHECK(s0.size() == 800);
        CHECK(s1.size() == 800);
        // Two consecutive calls together reconstruct one full codeword.
        CHECK(s0.size() + s1.size() == 1600);
}

TEST_CASE("LtcEncoder_Encode120p_per_video_frame_slice") {
        // At 120p (30×4) one codeword spans 4 video frames; each call emits
        // 400 samples at 48 kHz.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_120), 0.5f);
        Timecode   tc(Timecode::NDF120, 0, 0, 0, 0);
        size_t total = 0;
        for (int i = 0; i < 4; ++i) {
                List<int8_t> s = enc.encode(tc);
                CHECK(s.size() == 400);
                total += s.size();
                ++tc;
        }
        CHECK(total == 1600);
}

TEST_CASE("LtcEncoder_HFR_long_run_keeps_exact_sample_total") {
        // Walk one wall-clock second at 60p (60 video frames) and confirm the
        // accumulated sample count is exactly one second of audio (48000) —
        // the per-call slicing accounting must not drift.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_60), 0.5f);
        Timecode   tc(Timecode::NDF60, 0, 0, 0, 0);
        size_t total = 0;
        for (int i = 0; i < 60; ++i) {
                total += enc.encode(tc).size();
                ++tc;
        }
        CHECK(total == 48000);
}

TEST_CASE("LtcEncoder_NTSC_HFR_sample_total_within_one_sample") {
        // 59.94 fps at 48 kHz is fractional (800.8 samples per video frame on
        // average).  Over 60 video frames we expect 60 × 48000 × 1001 / 60000
        // = 48048 samples; the chunked accounting truncates per call so the
        // long-term total stays exact.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_59_94), 0.5f);
        Timecode   tc(Timecode::NDF60, 0, 0, 0, 0);
        size_t total = 0;
        for (int i = 0; i < 60; ++i) {
                total += enc.encode(tc).size();
                ++tc;
        }
        // 60 video frames * (48000 * 1001 / 60000) = 48048 samples exactly.
        CHECK(total == 48048u);
}

TEST_CASE("LtcEncoder_emits_silence_when_starting_mid_super_frame") {
        // Caller starts the stream at physical frame 1 of a 60p super-frame
        // (sub-frame 1, not a boundary).  The encoder must emit one video
        // frame of silence and wait for the next super-frame boundary at
        // frame 2 before latching a codeword.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_60), 0.5f);
        Timecode   tc(Timecode::NDF60, 0, 0, 0, 1);

        List<int8_t> silent = enc.encode(tc);
        REQUIRE(silent.size() == 800);
        for (size_t i = 0; i < silent.size(); ++i) {
                CHECK(silent[i] == 0);
        }

        ++tc; // physical frame 2 — super-frame boundary
        REQUIRE(tc.isSuperFrameBoundary());
        List<int8_t> a = enc.encode(tc);
        REQUIRE(a.size() == 800);
        bool anyNonZero = false;
        for (size_t i = 0; i < a.size(); ++i) {
                if (a[i] != 0) {
                        anyNonZero = true;
                        break;
                }
        }
        CHECK(anyNonZero);
}

TEST_CASE("LtcEncoder_emits_silence_when_skip_lands_mid_super_frame") {
        // Start at frame 0 (boundary), consume one codeword (800+800 samples
        // = 1 full codeword), then skip to a non-boundary frame.  Next call
        // must emit silence.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_60), 0.5f);
        Timecode   tc0(Timecode::NDF60, 0, 0, 0, 0);
        enc.encode(tc0);
        Timecode tc1(Timecode::NDF60, 0, 0, 0, 1);
        enc.encode(tc1);
        // Cursor exhausted.  Skip to frame 5 (sub-frame 1 of super-frame 2).
        Timecode tc5(Timecode::NDF60, 0, 0, 0, 5);
        REQUIRE_FALSE(tc5.isSuperFrameBoundary());
        List<int8_t> s = enc.encode(tc5);
        REQUIRE(s.size() == 800);
        for (size_t i = 0; i < s.size(); ++i) {
                CHECK(s[i] == 0);
        }
        // Next call lands on frame 6 (a boundary) — codeword regenerates.
        Timecode tc6(Timecode::NDF60, 0, 0, 0, 6);
        REQUIRE(tc6.isSuperFrameBoundary());
        List<int8_t> live = enc.encode(tc6);
        CHECK(live.size() == 800);
        bool anyNonZero = false;
        for (size_t i = 0; i < live.size(); ++i) {
                if (live[i] != 0) {
                        anyNonZero = true;
                        break;
                }
        }
        CHECK(anyNonZero);
}

TEST_CASE("LtcEncoder_non_HFR_always_emits_full_codeword") {
        // At 30p every video frame is a super-frame boundary, so the silence
        // behavior never kicks in.  Verify that an arbitrary Timecode digit
        // value still emits a full codeword.
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_30), 0.5f);
        Timecode   tc(Timecode::NDF30, 0, 0, 0, 7);
        REQUIRE(tc.isSuperFrameBoundary()); // non-HFR ⇒ always boundary
        List<int8_t> a = enc.encode(tc);
        CHECK(a.size() == 1600);
        bool anyNonZero = false;
        for (size_t i = 0; i < a.size(); ++i) {
                if (a[i] != 0) {
                        anyNonZero = true;
                        break;
                }
        }
        CHECK(anyNonZero);
}

TEST_CASE("LtcEncoder_resetSlicing_restarts_codeword") {
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_60), 0.5f);
        Timecode   tc(Timecode::NDF60, 0, 0, 0, 0);
        // Consume one slice (800 samples), leaving 800 in the buffer.
        List<int8_t> a = enc.encode(tc);
        REQUIRE(a.size() == 800);
        // Reset and re-encode at the same Timecode — the next call must emit
        // 800 samples (a fresh codeword head), not the leftover 800.
        enc.resetSlicing();
        List<int8_t> b = enc.encode(tc);
        CHECK(b.size() == 800);
        // The first 800 of a fresh codeword should equal a's first 800 (we
        // just rebuilt the same codeword).
        bool same = (a.size() == b.size());
        for (size_t i = 0; same && i < a.size(); ++i) {
                if (a[i] != b[i]) same = false;
        }
        CHECK(same);
}

TEST_CASE("LtcEncoder_FrameRate_overrides_NTSC_choice") {
        // The Timecode mode is at integer 30 fps.  Configuring the encoder
        // with the NTSC FrameRate must produce more samples per frame
        // (1001/1000 ratio) than the integer-rate variant.
        Timecode tc(Timecode::NDF30, 1, 0, 0, 0);

        LtcEncoder   encInt(48000, FrameRate(FrameRate::FPS_30), 0.5f);
        List<int8_t> aInt = encInt.encode(tc);
        REQUIRE(!aInt.isEmpty());

        LtcEncoder   encNtsc(48000, FrameRate(FrameRate::FPS_29_97), 0.5f);
        List<int8_t> aNtsc = encNtsc.encode(tc);
        REQUIRE(!aNtsc.isEmpty());

        CHECK(aNtsc.size() > aInt.size());
        // The size ratio should sit near 1001/1000 — allow ±2% slack
        // around the ideal 1.001 fraction.
        const double ratio = static_cast<double>(aNtsc.size()) / static_cast<double>(aInt.size());
        CHECK(ratio > 0.99);
        CHECK(ratio < 1.02);
}

TEST_CASE("LtcEncoder_frameRate_accessor") {
        LtcEncoder enc(48000, FrameRate(FrameRate::FPS_30), 0.5f);
        CHECK(enc.frameRate().numerator() == 30);
        CHECK(enc.frameRate().denominator() == 1);

        LtcEncoder encNone(48000, FrameRate(), 0.5f);
        CHECK_FALSE(encNone.frameRate().isValid());
}
