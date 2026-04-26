/**
 * @file      tests/audiolevel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/audiolevel.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("AudioLevel_DefaultIsSilence") {
        AudioLevel level;
        CHECK(level.isSilence());
        CHECK(level.toLinear() == 0.0);
}

TEST_CASE("AudioLevel_FullScale") {
        AudioLevel level = AudioLevel::fromDbfs(0.0);
        CHECK(level.dbfs() == 0.0);
        CHECK(level.toLinear() == doctest::Approx(1.0));
        CHECK_FALSE(level.isSilence());
        CHECK_FALSE(level.isClipping());
}

TEST_CASE("AudioLevel_Minus6") {
        AudioLevel level = AudioLevel::fromDbfs(-6.0);
        CHECK(level.toLinear() == doctest::Approx(0.501187).epsilon(0.001));
}

TEST_CASE("AudioLevel_Minus20") {
        AudioLevel level = AudioLevel::fromDbfs(-20.0);
        CHECK(level.toLinear() == doctest::Approx(0.1).epsilon(0.001));
}

TEST_CASE("AudioLevel_FromLinear") {
        AudioLevel level = AudioLevel::fromLinear(0.5);
        CHECK(level.dbfs() == doctest::Approx(-6.0206).epsilon(0.01));
        CHECK(level.toLinear() == doctest::Approx(0.5).epsilon(0.001));
}

TEST_CASE("AudioLevel_FromLinearZero") {
        AudioLevel level = AudioLevel::fromLinear(0.0);
        CHECK(level.isSilence());
        CHECK(level.toLinear() == 0.0);
}

TEST_CASE("AudioLevel_FromLinearNegative") {
        AudioLevel level = AudioLevel::fromLinear(-1.0);
        CHECK(level.isSilence());
}

TEST_CASE("AudioLevel_FromLinearFullScale") {
        AudioLevel level = AudioLevel::fromLinear(1.0);
        CHECK(level.dbfs() == doctest::Approx(0.0));
}

TEST_CASE("AudioLevel_Clipping") {
        AudioLevel level = AudioLevel::fromDbfs(3.0);
        CHECK(level.isClipping());
        CHECK(level.toLinear() == doctest::Approx(1.4125).epsilon(0.01));
}

TEST_CASE("AudioLevel_RoundTrip") {
        double values[] = {0.001, 0.01, 0.1, 0.25, 0.5, 0.75, 1.0};
        for (double v : values) {
                AudioLevel level = AudioLevel::fromLinear(v);
                CHECK(level.toLinear() == doctest::Approx(v).epsilon(0.0001));
        }
}

TEST_CASE("AudioLevel_ToLinearFloat") {
        AudioLevel level = AudioLevel::fromDbfs(-20.0);
        CHECK(level.toLinearFloat() == doctest::Approx(0.1f).epsilon(0.001f));
}

TEST_CASE("AudioLevel_Comparison") {
        AudioLevel a = AudioLevel::fromDbfs(-20.0);
        AudioLevel b = AudioLevel::fromDbfs(-6.0);
        AudioLevel c = AudioLevel::fromDbfs(-20.0);
        CHECK(a < b);
        CHECK(b > a);
        CHECK(a == c);
        CHECK(a != b);
        CHECK(a <= c);
        CHECK(a >= c);
}

TEST_CASE("AudioLevel_ToString") {
        CHECK(AudioLevel::fromDbfs(-20.0).toString() == "-20.0 dBFS");
        CHECK(AudioLevel::fromDbfs(0.0).toString() == "0.0 dBFS");
        CHECK(AudioLevel().toString() == "-inf dBFS");
        // Positive (clipping) value
        CHECK(AudioLevel::fromDbfs(3.0).toString() == "3.0 dBFS");
        // Small fractional value
        CHECK(AudioLevel::fromDbfs(-0.5).toString() == "-0.5 dBFS");
}

TEST_CASE("AudioLevel_CopyConstruction") {
        AudioLevel original = AudioLevel::fromDbfs(-12.0);
        AudioLevel copy(original);
        CHECK(copy.dbfs() == original.dbfs());
        CHECK(copy == original);
}

TEST_CASE("AudioLevel_CopyAssignment") {
        AudioLevel a = AudioLevel::fromDbfs(-12.0);
        AudioLevel b;
        CHECK(b.isSilence());
        b = a;
        CHECK(b.dbfs() == doctest::Approx(-12.0));
        CHECK(b == a);
}

TEST_CASE("AudioLevel_ExplicitConstructor") {
        AudioLevel level(AudioLevel(-48.0));
        CHECK(level.dbfs() == doctest::Approx(-48.0));
        CHECK_FALSE(level.isSilence());
        CHECK_FALSE(level.isClipping());
}

TEST_CASE("AudioLevel_SilenceToLinearFloat") {
        AudioLevel silence;
        CHECK(silence.toLinearFloat() == 0.0f);
}

TEST_CASE("AudioLevel_ClippingNotSilence") {
        AudioLevel level = AudioLevel::fromDbfs(6.0);
        CHECK(level.isClipping());
        CHECK_FALSE(level.isSilence());
        CHECK(level.toLinear() > 1.0);
}

TEST_CASE("AudioLevel_ZeroDbfsNotClipping") {
        AudioLevel level = AudioLevel::fromDbfs(0.0);
        CHECK_FALSE(level.isClipping());
        CHECK_FALSE(level.isSilence());
}

TEST_CASE("AudioLevel_ComparisonWithSilence") {
        AudioLevel silence;
        AudioLevel quiet = AudioLevel::fromDbfs(-96.0);
        CHECK(silence < quiet);
        CHECK(quiet > silence);
        CHECK(silence != quiet);
}

TEST_CASE("AudioLevel_FromLinearAboveFullScale") {
        AudioLevel level = AudioLevel::fromLinear(2.0);
        CHECK(level.isClipping());
        CHECK(level.dbfs() == doctest::Approx(6.0206).epsilon(0.01));
}

TEST_CASE("AudioLevel_DbfsRoundTrip") {
        double dbValues[] = {-96.0, -48.0, -20.0, -12.0, -6.0, -3.0, 0.0, 3.0, 6.0};
        for (double db : dbValues) {
                AudioLevel level = AudioLevel::fromDbfs(db);
                double     linear = level.toLinear();
                AudioLevel reconstructed = AudioLevel::fromLinear(linear);
                CHECK(reconstructed.dbfs() == doctest::Approx(db).epsilon(0.001));
        }
}
