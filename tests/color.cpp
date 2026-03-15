/**
 * @file      color.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/color.h>

using namespace promeki;

TEST_CASE("Color: default construction") {
        Color c;
        CHECK_FALSE(c.isValid());
        CHECK(c.r() == 0);
        CHECK(c.g() == 0);
        CHECK(c.b() == 0);
        CHECK(c.a() == 0);
}

TEST_CASE("Color: RGBA construction") {
        Color c(128, 64, 32, 200);
        CHECK(c.isValid());
        CHECK(c.r() == 128);
        CHECK(c.g() == 64);
        CHECK(c.b() == 32);
        CHECK(c.a() == 200);
}

TEST_CASE("Color: default alpha is 255") {
        Color c(10, 20, 30);
        CHECK(c.a() == 255);
}

TEST_CASE("Color: named constants") {
        CHECK(Color::Black == Color(0, 0, 0));
        CHECK(Color::White == Color(255, 255, 255));
        CHECK(Color::Red == Color(255, 0, 0));
        CHECK(Color::Green == Color(0, 255, 0));
        CHECK(Color::Blue == Color(0, 0, 255));
}

TEST_CASE("Color: fromHex") {
        Color c = Color::fromHex("#ff8040");
        CHECK(c.isValid());
        CHECK(c.r() == 255);
        CHECK(c.g() == 128);
        CHECK(c.b() == 64);
        CHECK(c.a() == 255);
}

TEST_CASE("Color: fromHex with alpha") {
        Color c = Color::fromHex("#ff804080");
        CHECK(c.isValid());
        CHECK(c.r() == 255);
        CHECK(c.g() == 128);
        CHECK(c.b() == 64);
        CHECK(c.a() == 128);
}

TEST_CASE("Color: fromHex invalid") {
        Color c = Color::fromHex("invalid");
        CHECK_FALSE(c.isValid());
}

TEST_CASE("Color: toHex") {
        Color c(255, 128, 64);
        CHECK(c.toHex() == "#ff8040");
        CHECK(c.toHex(true) == "#ff8040ff");
}

TEST_CASE("Color: lerp") {
        Color a(0, 0, 0);
        Color b(200, 100, 50);
        Color mid = a.lerp(b, 0.5);
        CHECK(mid.r() == 100);
        CHECK(mid.g() == 50);
        CHECK(mid.b() == 25);
}

TEST_CASE("Color: equality") {
        Color a(10, 20, 30, 40);
        Color b(10, 20, 30, 40);
        Color c(10, 20, 30, 41);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Color: setters") {
        Color c(0, 0, 0);
        c.setR(10);
        c.setG(20);
        c.setB(30);
        c.setA(40);
        CHECK(c.r() == 10);
        CHECK(c.g() == 20);
        CHECK(c.b() == 30);
        CHECK(c.a() == 40);
}

TEST_CASE("Color: inverted") {
        Color c(100, 200, 50, 128);
        Color inv = c.inverted();
        CHECK(inv.r() == 155);
        CHECK(inv.g() == 55);
        CHECK(inv.b() == 205);
        CHECK(inv.a() == 128); // alpha preserved
}

TEST_CASE("Color: inverted black to white") {
        Color inv = Color::Black.inverted();
        CHECK(inv.r() == 255);
        CHECK(inv.g() == 255);
        CHECK(inv.b() == 255);
}

TEST_CASE("Color: inverted white to black") {
        Color inv = Color::White.inverted();
        CHECK(inv.r() == 0);
        CHECK(inv.g() == 0);
        CHECK(inv.b() == 0);
}

TEST_CASE("Color: complementary red to cyan") {
        Color comp = Color::Red.complementary();
        CHECK(comp.r() == 0);
        CHECK(comp.g() == 255);
        CHECK(comp.b() == 255);
}

TEST_CASE("Color: complementary preserves alpha") {
        Color c(255, 0, 0, 128);
        Color comp = c.complementary();
        CHECK(comp.a() == 128);
}

TEST_CASE("Color: luminance black is zero") {
        CHECK(Color::Black.luminance() == doctest::Approx(0.0));
}

TEST_CASE("Color: luminance white is one") {
        CHECK(Color::White.luminance() == doctest::Approx(1.0));
}

TEST_CASE("Color: contrastingBW on dark color returns white") {
        Color dark(32, 160, 64); // ProgressFilled green
        Color contrast = dark.contrastingBW();
        CHECK(contrast == Color(255, 255, 255, 255));
}

TEST_CASE("Color: contrastingBW on light color returns black") {
        Color light(200, 220, 240);
        Color contrast = light.contrastingBW();
        CHECK(contrast == Color(0, 0, 0, 255));
}

TEST_CASE("Color: complementary gray is gray") {
        Color c(128, 128, 128);
        Color comp = c.complementary();
        CHECK(comp.r() == 128);
        CHECK(comp.g() == 128);
        CHECK(comp.b() == 128);
}
