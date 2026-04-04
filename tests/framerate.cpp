/**
 * @file      framerate.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/framerate.h>

using namespace promeki;

TEST_CASE("FrameRate: default construction is invalid") {
        FrameRate fr;
        CHECK_FALSE(fr.isValid());
        CHECK_FALSE(fr.isWellKnownRate());
}

TEST_CASE("FrameRate: construction from well-known rate 24") {
        FrameRate fr(FrameRate::FPS_24);
        CHECK(fr.isValid());
        CHECK(fr.isWellKnownRate());
        CHECK(fr.wellKnownRate() == FrameRate::FPS_24);
        CHECK(fr.numerator() == 24);
        CHECK(fr.denominator() == 1);
        CHECK(fr.toDouble() == doctest::Approx(24.0));
}

TEST_CASE("FrameRate: construction from well-known rate 25") {
        FrameRate fr(FrameRate::FPS_25);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 25);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 29.97") {
        FrameRate fr(FrameRate::FPS_2997);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 30000);
        CHECK(fr.denominator() == 1001);
        CHECK(fr.toDouble() == doctest::Approx(29.97).epsilon(0.01));
}

TEST_CASE("FrameRate: construction from well-known rate 30") {
        FrameRate fr(FrameRate::FPS_30);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 30);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 59.94") {
        FrameRate fr(FrameRate::FPS_5994);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 60000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 60") {
        FrameRate fr(FrameRate::FPS_60);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 60);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 23.98") {
        FrameRate fr(FrameRate::FPS_2398);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 24000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 120") {
        FrameRate fr(FrameRate::FPS_120);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 120);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 119.88") {
        FrameRate fr(FrameRate::FPS_11988);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 120000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 100") {
        FrameRate fr(FrameRate::FPS_100);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 100);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 48") {
        FrameRate fr(FrameRate::FPS_48);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 48);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from well-known rate 47.95") {
        FrameRate fr(FrameRate::FPS_4795);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 48000);
        CHECK(fr.denominator() == 1001);
}

TEST_CASE("FrameRate: construction from well-known rate 50") {
        FrameRate fr(FrameRate::FPS_50);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 50);
        CHECK(fr.denominator() == 1);
}

TEST_CASE("FrameRate: construction from rational") {
        FrameRate::RationalType r(90, 1);
        FrameRate fr(r);
        CHECK(fr.isValid());
        CHECK(fr.numerator() == 90);
        CHECK(fr.denominator() == 1);
        CHECK_FALSE(fr.isWellKnownRate());
}

TEST_CASE("FrameRate: invalid well-known rate") {
        FrameRate fr(FrameRate::FPS_Invalid);
        CHECK_FALSE(fr.isValid());
}

TEST_CASE("FrameRate: toString") {
        FrameRate fr(FrameRate::FPS_24);
        String s = fr.toString();
        CHECK_FALSE(s.isEmpty());
}

TEST_CASE("FrameRate: rational() accessor") {
        FrameRate fr(FrameRate::FPS_2997);
        const FrameRate::RationalType &r = fr.rational();
        CHECK(r.numerator() == 30000);
        CHECK(r.denominator() == 1001);
}

TEST_CASE("FrameRate: operator== and operator!=") {
        FrameRate a(FrameRate::FPS_24);
        FrameRate b(FrameRate::FPS_24);
        FrameRate c(FrameRate::FPS_25);

        CHECK(a == b);
        CHECK_FALSE(a == c);
        CHECK(a != c);
        CHECK_FALSE(a != b);

        // Same rational from different construction paths
        FrameRate::RationalType r(24, 1);
        FrameRate d(r);
        CHECK(a == d);
}

TEST_CASE("FrameRate: fromString well-known names") {
        SUBCASE("24") {
                auto [fr, e] = FrameRate::fromString("24");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_24));
        }
        SUBCASE("25") {
                auto [fr, e] = FrameRate::fromString("25");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_25));
        }
        SUBCASE("29.97") {
                auto [fr, e] = FrameRate::fromString("29.97");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_2997));
        }
        SUBCASE("30") {
                auto [fr, e] = FrameRate::fromString("30");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_30));
        }
        SUBCASE("50") {
                auto [fr, e] = FrameRate::fromString("50");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_50));
        }
        SUBCASE("59.94") {
                auto [fr, e] = FrameRate::fromString("59.94");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_5994));
        }
        SUBCASE("60") {
                auto [fr, e] = FrameRate::fromString("60");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_60));
        }
        SUBCASE("23.98") {
                auto [fr, e] = FrameRate::fromString("23.98");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_2398));
        }
        SUBCASE("23.976 alias") {
                auto [fr, e] = FrameRate::fromString("23.976");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_2398));
        }
}

TEST_CASE("FrameRate: fromString fraction form") {
        SUBCASE("30000/1001") {
                auto [fr, e] = FrameRate::fromString("30000/1001");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_2997));
        }
        SUBCASE("48/1") {
                auto [fr, e] = FrameRate::fromString("48/1");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_48));
        }
        SUBCASE("24000/1001 (23.976)") {
                auto [fr, e] = FrameRate::fromString("24000/1001");
                CHECK_FALSE(e.isError());
                CHECK(fr == FrameRate(FrameRate::FPS_2398));
        }
}

TEST_CASE("FrameRate: wellKnownRate matches via reduced rational") {
        // 30000/1000 reduces to 30/1, which should match FPS_30
        FrameRate fr(FrameRate::RationalType(30000, 1000));
        CHECK(fr.isWellKnownRate());
        CHECK(fr.wellKnownRate() == FrameRate::FPS_30);

        // 60000/1000 reduces to 60/1, which should match FPS_60
        FrameRate fr2(FrameRate::RationalType(60000, 1000));
        CHECK(fr2.wellKnownRate() == FrameRate::FPS_60);

        // Non-well-known rate
        FrameRate fr3(FrameRate::RationalType(90, 1));
        CHECK_FALSE(fr3.isWellKnownRate());
        CHECK(fr3.wellKnownRate() == FrameRate::FPS_NotWellKnown);
}

TEST_CASE("FrameRate: fromString invalid") {
        SUBCASE("empty string") {
                auto [fr, e] = FrameRate::fromString("");
                CHECK(e.isError());
        }
        SUBCASE("garbage string") {
                auto [fr, e] = FrameRate::fromString("not_a_rate");
                CHECK(e.isError());
        }
        SUBCASE("zero denominator") {
                auto [fr, e] = FrameRate::fromString("30/0");
                CHECK(e.isError());
        }
}
