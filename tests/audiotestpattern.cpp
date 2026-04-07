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
#include <promeki/timecode.h>

using namespace promeki;

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AudioTestPattern_Defaults") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        CHECK(gen.mode() == AudioTestPattern::Tone);
        CHECK(gen.toneFrequency() == doctest::Approx(1000.0));
        CHECK(gen.ltcChannel() == 0);
}

// ============================================================================
// Tone mode
// ============================================================================

TEST_CASE("AudioTestPattern_Tone") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        gen.setMode(AudioTestPattern::Tone);
        gen.setToneFrequency(1000.0);
        gen.setToneLevel(AudioLevel::fromDbfs(-10.0));
        gen.configure();

        Audio audio = gen.create(4800);
        CHECK(audio.isValid());
        CHECK(audio.samples() == 4800);

        // Verify audio contains non-zero data (it's a sine wave)
        float *data = audio.data<float>();
        bool hasNonZero = false;
        for(size_t i = 0; i < audio.samples() * desc.channels(); i++) {
                if(data[i] != 0.0f) { hasNonZero = true; break; }
        }
        CHECK(hasNonZero);
}

// ============================================================================
// Silence mode
// ============================================================================

TEST_CASE("AudioTestPattern_Silence") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setMode(AudioTestPattern::Silence);
        gen.configure();

        Audio audio = gen.create(1000);
        CHECK(audio.isValid());
        CHECK(audio.samples() == 1000);

        // All samples should be zero
        float *data = audio.data<float>();
        bool allZero = true;
        for(size_t i = 0; i < audio.samples(); i++) {
                if(data[i] != 0.0f) { allZero = false; break; }
        }
        CHECK(allZero);
}

// ============================================================================
// LTC mode
// ============================================================================

TEST_CASE("AudioTestPattern_LTC") {
        AudioDesc desc(48000, 2);
        AudioTestPattern gen(desc);
        gen.setMode(AudioTestPattern::LTC);
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        gen.setLtcChannel(0);
        gen.configure();

        Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
        Audio audio = gen.create(2000, tc);
        CHECK(audio.isValid());
}

// ============================================================================
// render() into existing buffer
// ============================================================================

TEST_CASE("AudioTestPattern_RenderIntoExisting") {
        AudioDesc desc(48000, 1);
        AudioTestPattern gen(desc);
        gen.setMode(AudioTestPattern::Tone);
        gen.setToneFrequency(440.0);
        gen.configure();

        Audio audio(desc, 480);
        audio.zero();
        gen.render(audio);

        float *data = audio.data<float>();
        bool hasNonZero = false;
        for(size_t i = 0; i < audio.samples(); i++) {
                if(data[i] != 0.0f) { hasNonZero = true; break; }
        }
        CHECK(hasNonZero);
}

// ============================================================================
// fromString / toString round-trip
// ============================================================================

TEST_CASE("AudioTestPattern_StringRoundTrip") {
        AudioTestPattern::Mode modes[] = {
                AudioTestPattern::Tone,
                AudioTestPattern::Silence,
                AudioTestPattern::LTC,
                AudioTestPattern::AvSync
        };

        for(auto mode : modes) {
                String name = AudioTestPattern::toString(mode);
                CHECK_FALSE(name.isEmpty());
                auto [parsed, err] = AudioTestPattern::fromString(name);
                CHECK(err.isOk());
                CHECK(parsed == mode);
        }
}

TEST_CASE("AudioTestPattern_FromStringInvalid") {
        auto [mode, err] = AudioTestPattern::fromString("bogus");
        CHECK(err.isError());
}

// ============================================================================
// AvSync mode — tone burst on tc.frame()==0, silence otherwise
// ============================================================================

static float audioPeak(const Audio &a) {
        if(!a.isValid()) return 0.0f;
        const float *d = a.data<float>();
        size_t total = a.samples() * a.desc().channels();
        float peak = 0.0f;
        for(size_t i = 0; i < total; i++) {
                float v = d[i] < 0.0f ? -d[i] : d[i];
                if(v > peak) peak = v;
        }
        return peak;
}

TEST_CASE("AudioTestPattern_AvSync") {
        AudioDesc desc(48000.0f, 2);
        AudioTestPattern gen(desc);
        gen.setMode(AudioTestPattern::AvSync);
        gen.setToneFrequency(1000.0);
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());

        const size_t samples = 1600; // ~one frame at 48k / 30 fps

        // tc.frame() == 0 -> tone burst
        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);
        REQUIRE(marker.isValid());
        Audio tone = gen.create(samples, marker);
        REQUIRE(tone.isValid());
        CHECK(tone.samples() == samples);
        CHECK(audioPeak(tone) > 0.05f);

        // tc.frame() != 0 -> silence
        Timecode nonMarker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 5);
        Audio silence = gen.create(samples, nonMarker);
        REQUIRE(silence.isValid());
        CHECK(silence.samples() == samples);
        CHECK(audioPeak(silence) == doctest::Approx(0.0f));

        // Repeated calls return the cached buffers — same backing data.
        Audio tone2 = gen.create(samples, marker);
        REQUIRE(tone2.isValid());
        CHECK(tone2.data<float>() == tone.data<float>());
        Audio silence2 = gen.create(samples, nonMarker);
        REQUIRE(silence2.isValid());
        CHECK(silence2.data<float>() == silence.data<float>());

        // Invalid timecode falls back to silence (graceful degradation).
        Audio fallback = gen.create(samples, Timecode());
        REQUIRE(fallback.isValid());
        CHECK(audioPeak(fallback) == doctest::Approx(0.0f));
}
