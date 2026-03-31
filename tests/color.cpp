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

TEST_CASE("Color: fromString hex") {
        Color c = Color::fromString("#ff8040");
        CHECK(c.isValid());
        CHECK(c.r() == 255);
        CHECK(c.g() == 128);
        CHECK(c.b() == 64);
}

TEST_CASE("Color: fromString named colors") {
        CHECK(Color::fromString("red") == Color::Red);
        CHECK(Color::fromString("Red") == Color::Red);
        CHECK(Color::fromString("RED") == Color::Red);
        CHECK(Color::fromString("white") == Color::White);
        CHECK(Color::fromString("black") == Color::Black);
        CHECK(Color::fromString("blue") == Color::Blue);
        CHECK(Color::fromString("green") == Color::Green);
        CHECK(Color::fromString("yellow") == Color::Yellow);
        CHECK(Color::fromString("cyan") == Color::Cyan);
        CHECK(Color::fromString("magenta") == Color::Magenta);
        CHECK(Color::fromString("orange") == Color::Orange);
        CHECK(Color::fromString("darkgray") == Color::DarkGray);
        CHECK(Color::fromString("lightgray") == Color::LightGray);
        CHECK(Color::fromString("transparent") == Color::Transparent);
}

TEST_CASE("Color: fromString comma-separated RGB") {
        Color c = Color::fromString("128,64,32");
        CHECK(c.isValid());
        CHECK(c.r() == 128);
        CHECK(c.g() == 64);
        CHECK(c.b() == 32);
        CHECK(c.a() == 255);
}

TEST_CASE("Color: fromString comma-separated RGBA") {
        Color c = Color::fromString("128,64,32,200");
        CHECK(c.isValid());
        CHECK(c.r() == 128);
        CHECK(c.g() == 64);
        CHECK(c.b() == 32);
        CHECK(c.a() == 200);
}

TEST_CASE("Color: fromString hex with alpha") {
        Color c = Color::fromString("#ff804080");
        CHECK(c.isValid());
        CHECK(c.r() == 255);
        CHECK(c.g() == 128);
        CHECK(c.b() == 64);
        CHECK(c.a() == 128);
}

TEST_CASE("Color: fromString comma-separated with whitespace") {
        Color c = Color::fromString("128, 64, 32");
        CHECK(c.isValid());
        CHECK(c.r() == 128);
        CHECK(c.g() == 64);
        CHECK(c.b() == 32);
        CHECK(c.a() == 255);
}

TEST_CASE("Color: fromString comma-separated RGBA with whitespace") {
        Color c = Color::fromString(" 10 , 20 , 30 , 40 ");
        CHECK(c.isValid());
        CHECK(c.r() == 10);
        CHECK(c.g() == 20);
        CHECK(c.b() == 30);
        CHECK(c.a() == 40);
}

TEST_CASE("Color: fromString invalid") {
        CHECK_FALSE(Color::fromString("").isValid());
        CHECK_FALSE(Color::fromString("notacolor").isValid());
        CHECK_FALSE(Color::fromString("256,0,0").isValid());
        CHECK_FALSE(Color::fromString("-1,0,0").isValid());
}

TEST_CASE("Color: fromString invalid component counts") {
        // Two components - not valid RGB or RGBA
        CHECK_FALSE(Color::fromString("128,64").isValid());
        // Five components - not valid
        CHECK_FALSE(Color::fromString("128,64,32,200,100").isValid());
        // One number only
        CHECK_FALSE(Color::fromString("128").isValid());
}

TEST_CASE("Color: fromString out of range values") {
        CHECK_FALSE(Color::fromString("0,0,256").isValid());
        CHECK_FALSE(Color::fromString("0,0,0,256").isValid());
        CHECK_FALSE(Color::fromString("0,-1,0").isValid());
        CHECK_FALSE(Color::fromString("-1,0,0,0").isValid());
}

TEST_CASE("Color: fromString non-numeric comma values") {
        CHECK_FALSE(Color::fromString("abc,def,ghi").isValid());
        CHECK_FALSE(Color::fromString("10,abc,20").isValid());
        CHECK_FALSE(Color::fromString("10,20,30,abc").isValid());
}

TEST_CASE("Color: fromString boundary values") {
        Color c = Color::fromString("0,0,0,0");
        CHECK(c.isValid());
        CHECK(c.r() == 0);
        CHECK(c.g() == 0);
        CHECK(c.b() == 0);
        CHECK(c.a() == 0);

        Color c2 = Color::fromString("255,255,255,255");
        CHECK(c2.isValid());
        CHECK(c2.r() == 255);
        CHECK(c2.g() == 255);
        CHECK(c2.b() == 255);
        CHECK(c2.a() == 255);
}

TEST_CASE("Color: toString/fromString round-trip") {
        Color original(128, 64, 32);
        String str = original.toString();
        Color parsed = Color::fromString(str);
        CHECK(parsed.isValid());
        CHECK(parsed.r() == original.r());
        CHECK(parsed.g() == original.g());
        CHECK(parsed.b() == original.b());
}

TEST_CASE("Color: toString/fromString round-trip with alpha") {
        Color original(128, 64, 32, 200);
        String str = original.toString();
        Color parsed = Color::fromString(str);
        CHECK(parsed.isValid());
        CHECK(parsed.r() == original.r());
        CHECK(parsed.g() == original.g());
        CHECK(parsed.b() == original.b());
        CHECK(parsed.a() == original.a());
}

TEST_CASE("Color: toString default FloatFormat") {
        CHECK(Color::White.toString() == "rgb(1,1,1)");
        CHECK(Color::Black.toString() == "rgb(0,0,0)");
        CHECK(Color::Red.toString() == "rgb(1,0,0)");
        CHECK(Color(128, 64, 32).toString() == "rgb(0.501961,0.25098,0.12549)");
        CHECK(Color(128, 64, 32, 200).toString() == "rgba(0.501961,0.25098,0.12549,0.784314)");
        CHECK(Color(0, 0, 0, 0).toString() == "rgba(0,0,0,0)");
}

TEST_CASE("Color: toString HexFormat") {
        CHECK(Color::White.toString(Color::HexFormat) == "#ffffff");
        CHECK(Color::Red.toString(Color::HexFormat) == "#ff0000");
        CHECK(Color(128, 64, 32).toString(Color::HexFormat) == "#804020");
        CHECK(Color(128, 64, 32, 200).toString(Color::HexFormat) == "#804020c8");
        CHECK(Color(0, 0, 0, 0).toString(Color::HexFormat) == "#00000000");
}

TEST_CASE("Color: toString CSVFormat") {
        CHECK(Color::White.toString(Color::CSVFormat) == "255,255,255");
        CHECK(Color::Red.toString(Color::CSVFormat) == "255,0,0");
        CHECK(Color(128, 64, 32, 200).toString(Color::CSVFormat) == "128,64,32,200");
}

TEST_CASE("Color: toString AlphaAlways") {
        CHECK(Color::White.toString(Color::HexFormat, Color::AlphaAlways) == "#ffffffff");
        CHECK(Color::White.toString(Color::CSVFormat, Color::AlphaAlways) == "255,255,255,255");
        CHECK(Color::White.toString(Color::FloatFormat, Color::AlphaAlways) == "rgba(1,1,1,1)");
}

TEST_CASE("Color: toString AlphaNever") {
        Color c(128, 64, 32, 200);
        CHECK(c.toString(Color::HexFormat, Color::AlphaNever) == "#804020");
        CHECK(c.toString(Color::CSVFormat, Color::AlphaNever) == "128,64,32");
        CHECK(c.toString(Color::FloatFormat, Color::AlphaNever) == "rgb(0.501961,0.25098,0.12549)");
}

TEST_CASE("Color: fromString rgb() functional notation") {
        Color c = Color::fromString("rgb(0.501961,0.25098,0.12549)");
        CHECK(c.isValid());
        CHECK(c.r() == 128);
        CHECK(c.g() == 64);
        CHECK(c.b() == 32);
        CHECK(c.a() == 255);
}

TEST_CASE("Color: fromString rgba() functional notation") {
        Color c = Color::fromString("rgba(0.501961,0.25098,0.12549,0.784314)");
        CHECK(c.isValid());
        CHECK(c.r() == 128);
        CHECK(c.g() == 64);
        CHECK(c.b() == 32);
        CHECK(c.a() == 200);
}

TEST_CASE("Color: fromString rgb() edge values") {
        Color c = Color::fromString("rgb(0,0,0)");
        CHECK(c.isValid());
        CHECK(c == Color::Black);

        Color c2 = Color::fromString("rgb(1,1,1)");
        CHECK(c2.isValid());
        CHECK(c2 == Color::White);
}

TEST_CASE("Color: fromString rgba() zero alpha") {
        Color c = Color::fromString("rgba(0,0,0,0)");
        CHECK(c.isValid());
        CHECK(c == Color::Transparent);
}

TEST_CASE("Color: fromString rgb() case insensitive") {
        CHECK(Color::fromString("RGB(1,0,0)") == Color::Red);
        CHECK(Color::fromString("Rgb(0,1,0)") == Color::Green);
        CHECK(Color::fromString("RGBA(0,0,1,1)") == Color::Blue);
}

TEST_CASE("Color: fromString rgb() invalid") {
        CHECK_FALSE(Color::fromString("rgb()").isValid());
        CHECK_FALSE(Color::fromString("rgb(1,2)").isValid());
        CHECK_FALSE(Color::fromString("rgb(1,0,0,0)").isValid());
        CHECK_FALSE(Color::fromString("rgba(1,0,0)").isValid());
        CHECK_FALSE(Color::fromString("rgb(1.5,0,0)").isValid());
        CHECK_FALSE(Color::fromString("rgb(-0.1,0,0)").isValid());
        CHECK_FALSE(Color::fromString("rgb(0,0,0").isValid());
}

TEST_CASE("Color: fromString grey spelling variants") {
        CHECK(Color::fromString("darkgrey") == Color::DarkGray);
        CHECK(Color::fromString("DarkGrey") == Color::DarkGray);
        CHECK(Color::fromString("lightgrey") == Color::LightGray);
        CHECK(Color::fromString("LightGrey") == Color::LightGray);
}
