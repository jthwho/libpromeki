/**
 * @file      tests/audiotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/proav/audiotestpattern.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/core/audiolevel.h>
#include <promeki/core/timecode.h>

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
                AudioTestPattern::LTC
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
