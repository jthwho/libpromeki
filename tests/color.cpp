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
        CHECK(c.comp(0) == 0.0f);
        CHECK(c.comp(1) == 0.0f);
        CHECK(c.comp(2) == 0.0f);
        CHECK(c.comp(3) == 0.0f);
}

TEST_CASE("Color: RGBA construction") {
        Color c(128, 64, 32, 200);
        CHECK(c.isValid());
        CHECK(c.r8() == 128);
        CHECK(c.g8() == 64);
        CHECK(c.b8() == 32);
        CHECK(c.a8() == 200);
}

TEST_CASE("Color: default alpha is 255") {
        Color c(10, 20, 30);
        CHECK(c.a8() == 255);
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
        CHECK(c.r8() == 255);
        CHECK(c.g8() == 128);
        CHECK(c.b8() == 64);
        CHECK(c.a8() == 255);
}

TEST_CASE("Color: fromHex with alpha") {
        Color c = Color::fromHex("#ff804080");
        CHECK(c.isValid());
        CHECK(c.r8() == 255);
        CHECK(c.g8() == 128);
        CHECK(c.b8() == 64);
        CHECK(c.a8() == 128);
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
        CHECK(mid.r8() == 100);
        CHECK(mid.g8() == 50);
        CHECK(mid.b8() == 25);
}

TEST_CASE("Color: equality") {
        Color a(10, 20, 30, 40);
        Color b(10, 20, 30, 40);
        Color c(10, 20, 30, 41);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Color: setters") {
        Color c = Color::srgb(0.0f, 0.0f, 0.0f);
        c.setR(10.0f / 255.0f);
        c.setG(20.0f / 255.0f);
        c.setB(30.0f / 255.0f);
        c.setA(40.0f / 255.0f);
        CHECK(c.r8() == 10);
        CHECK(c.g8() == 20);
        CHECK(c.b8() == 30);
        CHECK(c.a8() == 40);
}

TEST_CASE("Color: inverted") {
        Color c(100, 200, 50, 128);
        Color inv = c.inverted();
        CHECK(inv.r8() == 155);
        CHECK(inv.g8() == 55);
        CHECK(inv.b8() == 205);
        CHECK(inv.a8() == 128); // alpha preserved
}

TEST_CASE("Color: inverted black to white") {
        Color inv = Color::Black.inverted();
        CHECK(inv.r8() == 255);
        CHECK(inv.g8() == 255);
        CHECK(inv.b8() == 255);
}

TEST_CASE("Color: inverted white to black") {
        Color inv = Color::White.inverted();
        CHECK(inv.r8() == 0);
        CHECK(inv.g8() == 0);
        CHECK(inv.b8() == 0);
}

TEST_CASE("Color: complementary red to cyan") {
        Color comp = Color::Red.complementary();
        CHECK(comp.r8() == 0);
        CHECK(comp.g8() == 255);
        CHECK(comp.b8() == 255);
}

TEST_CASE("Color: complementary preserves alpha") {
        Color c(255, 0, 0, 128);
        Color comp = c.complementary();
        CHECK(comp.a8() == 128);
}

TEST_CASE("Color: luminance black is zero") {
        CHECK(Color::Black.luminance() == doctest::Approx(0.0));
}

TEST_CASE("Color: luminance white is one") {
        CHECK(Color::White.luminance() == doctest::Approx(1.0));
}

TEST_CASE("Color: contrastingBW on dark color returns white") {
        Color dark(32, 160, 64);
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
        CHECK(comp.r8() == 128);
        CHECK(comp.g8() == 128);
        CHECK(comp.b8() == 128);
}

TEST_CASE("Color: fromString hex") {
        Color c = Color::fromString("#ff8040");
        CHECK(c.isValid());
        CHECK(c.r8() == 255);
        CHECK(c.g8() == 128);
        CHECK(c.b8() == 64);
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
        CHECK(c.r8() == 128);
        CHECK(c.g8() == 64);
        CHECK(c.b8() == 32);
        CHECK(c.a8() == 255);
}

TEST_CASE("Color: fromString comma-separated RGBA") {
        Color c = Color::fromString("128,64,32,200");
        CHECK(c.isValid());
        CHECK(c.r8() == 128);
        CHECK(c.g8() == 64);
        CHECK(c.b8() == 32);
        CHECK(c.a8() == 200);
}

TEST_CASE("Color: fromString hex with alpha") {
        Color c = Color::fromString("#ff804080");
        CHECK(c.isValid());
        CHECK(c.r8() == 255);
        CHECK(c.g8() == 128);
        CHECK(c.b8() == 64);
        CHECK(c.a8() == 128);
}

TEST_CASE("Color: fromString comma-separated with whitespace") {
        Color c = Color::fromString("128, 64, 32");
        CHECK(c.isValid());
        CHECK(c.r8() == 128);
        CHECK(c.g8() == 64);
        CHECK(c.b8() == 32);
        CHECK(c.a8() == 255);
}

TEST_CASE("Color: fromString comma-separated RGBA with whitespace") {
        Color c = Color::fromString(" 10 , 20 , 30 , 40 ");
        CHECK(c.isValid());
        CHECK(c.r8() == 10);
        CHECK(c.g8() == 20);
        CHECK(c.b8() == 30);
        CHECK(c.a8() == 40);
}

TEST_CASE("Color: fromString invalid") {
        CHECK_FALSE(Color::fromString("").isValid());
        CHECK_FALSE(Color::fromString("notacolor").isValid());
        CHECK_FALSE(Color::fromString("256,0,0").isValid());
        CHECK_FALSE(Color::fromString("-1,0,0").isValid());
}

TEST_CASE("Color: fromString invalid component counts") {
        CHECK_FALSE(Color::fromString("128,64").isValid());
        CHECK_FALSE(Color::fromString("128,64,32,200,100").isValid());
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
        CHECK(c.r8() == 0);
        CHECK(c.g8() == 0);
        CHECK(c.b8() == 0);
        CHECK(c.a8() == 0);

        Color c2 = Color::fromString("255,255,255,255");
        CHECK(c2.isValid());
        CHECK(c2.r8() == 255);
        CHECK(c2.g8() == 255);
        CHECK(c2.b8() == 255);
        CHECK(c2.a8() == 255);
}

TEST_CASE("Color: toString/fromString round-trip") {
        Color original(128, 64, 32);
        String str = original.toString();
        Color parsed = Color::fromString(str);
        CHECK(parsed.isValid());
        CHECK(parsed.r8() == original.r8());
        CHECK(parsed.g8() == original.g8());
        CHECK(parsed.b8() == original.b8());
}

TEST_CASE("Color: toString/fromString round-trip with alpha") {
        Color original(128, 64, 32, 200);
        String str = original.toString();
        Color parsed = Color::fromString(str);
        CHECK(parsed.isValid());
        CHECK(parsed.r8() == original.r8());
        CHECK(parsed.g8() == original.g8());
        CHECK(parsed.b8() == original.b8());
        CHECK(parsed.a8() == original.a8());
}

TEST_CASE("Color: toString default ModelFormat") {
        CHECK(Color::White.toString() == "sRGB(1,1,1,1)");
        CHECK(Color::Black.toString() == "sRGB(0,0,0,1)");
        CHECK(Color::Red.toString() == "sRGB(1,0,0,1)");
        CHECK(Color(128, 64, 32).toString() == "sRGB(0.501961,0.25098,0.12549,1)");
        CHECK(Color(128, 64, 32, 200).toString() == "sRGB(0.501961,0.25098,0.12549,0.784314)");
        CHECK(Color((uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0).toString() == "sRGB(0,0,0,0)");
}

TEST_CASE("Color: toString ModelFormat for non-sRGB") {
        Color c = Color::hsv(0.5f, 1.0f, 0.8f, 0.9f);
        CHECK(c.toString() == "HSV_sRGB(0.5,1,0.8,0.9)");
}

TEST_CASE("Color: toString FloatFormat") {
        CHECK(Color::White.toString(Color::FloatFormat) == "rgb(1,1,1)");
        CHECK(Color::Black.toString(Color::FloatFormat) == "rgb(0,0,0)");
        CHECK(Color::Red.toString(Color::FloatFormat) == "rgb(1,0,0)");
        CHECK(Color(128, 64, 32).toString(Color::FloatFormat) == "rgb(0.501961,0.25098,0.12549)");
        CHECK(Color(128, 64, 32, 200).toString(Color::FloatFormat) == "rgba(0.501961,0.25098,0.12549,0.784314)");
        CHECK(Color((uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0).toString(Color::FloatFormat) == "rgba(0,0,0,0)");
}

TEST_CASE("Color: toString HexFormat") {
        CHECK(Color::White.toString(Color::HexFormat) == "#ffffff");
        CHECK(Color::Red.toString(Color::HexFormat) == "#ff0000");
        CHECK(Color(128, 64, 32).toString(Color::HexFormat) == "#804020");
        CHECK(Color(128, 64, 32, 200).toString(Color::HexFormat) == "#804020c8");
        CHECK(Color((uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0).toString(Color::HexFormat) == "#00000000");
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
        CHECK(c.r8() == 128);
        CHECK(c.g8() == 64);
        CHECK(c.b8() == 32);
        CHECK(c.a8() == 255);
}

TEST_CASE("Color: fromString rgba() functional notation") {
        Color c = Color::fromString("rgba(0.501961,0.25098,0.12549,0.784314)");
        CHECK(c.isValid());
        CHECK(c.r8() == 128);
        CHECK(c.g8() == 64);
        CHECK(c.b8() == 32);
        CHECK(c.a8() == 200);
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

// --- New tests for ColorModel-aware features ---

TEST_CASE("Color: model is sRGB for uint8_t constructor") {
        Color c(255, 0, 0);
        CHECK(c.model() == ColorModel::sRGB);
}

TEST_CASE("Color: float component access") {
        Color c(128, 64, 32);
        CHECK(c.r() == doctest::Approx(128.0f / 255.0f));
        CHECK(c.g() == doctest::Approx(64.0f / 255.0f));
        CHECK(c.b() == doctest::Approx(32.0f / 255.0f));
        CHECK(c.a() == doctest::Approx(1.0f));
}

TEST_CASE("Color: srgb factory") {
        Color c = Color::srgb(1.0f, 0.0f, 0.0f);
        CHECK(c.isValid());
        CHECK(c.model() == ColorModel::sRGB);
        CHECK(c.r() == 1.0f);
        CHECK(c.g() == 0.0f);
        CHECK(c.b() == 0.0f);
        CHECK(c == Color::Red);
}

TEST_CASE("Color: hsv factory") {
        Color c = Color::hsv(0.0f, 1.0f, 1.0f); // Red in HSV
        CHECK(c.isValid());
        CHECK(c.model() == ColorModel::HSV_sRGB);
}

TEST_CASE("Color: convert sRGB to HSV and back") {
        Color red = Color::Red;
        Color hsv = red.toHSV();
        CHECK(hsv.model() == ColorModel::HSV_sRGB);
        // Red in HSV: H~0, S~1, V~1
        CHECK(hsv.s() == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(hsv.v() == doctest::Approx(1.0f).epsilon(0.01));
        Color back = hsv.toRGB();
        CHECK(back.r8() == 255);
        CHECK(back.g8() == 0);
        CHECK(back.b8() == 0);
}

TEST_CASE("Color: convert to Lab and back") {
        Color c(128, 64, 32);
        Color lab = c.toLab();
        CHECK(lab.model() == ColorModel::CIELab);
        Color back = lab.toRGB();
        CHECK(back.r8() == 128);
        CHECK(back.g8() == 64);
        CHECK(back.b8() == 32);
}

TEST_CASE("Color: isClose") {
        Color a = Color::srgb(0.5f, 0.3f, 0.1f);
        Color b = Color::srgb(0.50001f, 0.30001f, 0.10001f);
        CHECK(a.isClose(b, 1e-4f));
        Color c = Color::srgb(0.6f, 0.3f, 0.1f);
        CHECK_FALSE(a.isClose(c));
}

TEST_CASE("Color: Ignored is invalid") {
        CHECK_FALSE(Color::Ignored.isValid());
        CHECK(Color::Ignored.model() == ColorModel::Invalid);
}

TEST_CASE("Color: toNative for HSV hue") {
        Color c = Color::hsv(0.5f, 1.0f, 1.0f);
        CHECK(c.toNative(0) == doctest::Approx(180.0f)); // 0.5 * 360
}

TEST_CASE("Color: fromNative for HSV") {
        Color c = Color::fromNative(ColorModel::HSV_sRGB, 180.0f, 1.0f, 1.0f);
        CHECK(c.h() == doctest::Approx(0.5f));
        CHECK(c.s() == doctest::Approx(1.0f));
        CHECK(c.v() == doctest::Approx(1.0f));
}

// ── Static factory completeness ──────────────────────────────────

TEST_CASE("Color: linearSrgb factory") {
        Color c = Color::linearSrgb(0.5f, 0.3f, 0.1f);
        CHECK(c.model() == ColorModel::LinearSRGB);
        CHECK(c.r() == 0.5f);
        CHECK(c.alpha() == 1.0f);
}

TEST_CASE("Color: rec709 factory") {
        Color c = Color::rec709(0.5f, 0.3f, 0.1f, 0.8f);
        CHECK(c.model() == ColorModel::Rec709);
        CHECK(c.alpha() == 0.8f);
}

TEST_CASE("Color: hsl factory") {
        Color c = Color::hsl(0.5f, 0.8f, 0.6f);
        CHECK(c.model() == ColorModel::HSL_sRGB);
        CHECK(c.comp(0) == 0.5f);
}

TEST_CASE("Color: ycbcr709 factory") {
        Color c = Color::ycbcr709(0.5f, 0.5f, 0.5f);
        CHECK(c.model() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("Color: ycbcr601 factory") {
        Color c = Color::ycbcr601(0.5f, 0.5f, 0.5f);
        CHECK(c.model() == ColorModel::YCbCr_Rec601);
}

TEST_CASE("Color: xyz factory") {
        Color c = Color::xyz(0.4f, 0.3f, 0.2f);
        CHECK(c.model() == ColorModel::CIEXYZ);
}

TEST_CASE("Color: lab factory") {
        Color c = Color::lab(0.5f, 0.5f, 0.5f);
        CHECK(c.model() == ColorModel::CIELab);
}

// ── ColorModel& constructor ──────────────────────────────────────

TEST_CASE("Color: explicit model constructor") {
        Color c(ColorModel::Rec709, 0.5f, 0.3f, 0.1f);
        CHECK(c.model() == ColorModel::Rec709);
        CHECK(c.comp(0) == 0.5f);
        CHECK(c.comp(1) == 0.3f);
        CHECK(c.comp(2) == 0.1f);
        CHECK(c.alpha() == 1.0f); // default
}

TEST_CASE("Color: explicit model constructor with alpha") {
        Color c(ColorModel::sRGB, 1.0f, 0.0f, 0.0f, 0.5f);
        CHECK(c.alpha() == 0.5f);
}

// ── comp/setComp ─────────────────────────────────────────────────

TEST_CASE("Color: comp out-of-range returns 0") {
        Color c = Color::srgb(0.5f, 0.3f, 0.1f, 0.9f);
        CHECK(c.comp(4) == 0.0f);
        CHECK(c.comp(100) == 0.0f);
}

TEST_CASE("Color: setComp") {
        Color c = Color::srgb(0.0f, 0.0f, 0.0f);
        c.setComp(0, 0.5f);
        c.setComp(1, 0.3f);
        c.setComp(2, 0.1f);
        c.setComp(3, 0.8f);
        CHECK(c.r() == 0.5f);
        CHECK(c.g() == 0.3f);
        CHECK(c.b() == 0.1f);
        CHECK(c.alpha() == 0.8f);
}

TEST_CASE("Color: setComp out-of-range is no-op") {
        Color c = Color::srgb(0.5f, 0.3f, 0.1f);
        c.setComp(4, 99.0f);
        CHECK(c.r() == 0.5f); // unchanged
}

// ── alpha / a aliases ────────────────────────────────────────────

TEST_CASE("Color: alpha and a are identical") {
        Color c(128, 64, 32, 200);
        CHECK(c.alpha() == c.a());
}

TEST_CASE("Color: setAlpha") {
        Color c = Color::srgb(1.0f, 0.0f, 0.0f);
        c.setAlpha(0.7f);
        CHECK(c.alpha() == 0.7f);
}

// ── Model-specific accessors ─────────────────────────────────────

TEST_CASE("Color: HSV accessors") {
        Color c = Color::hsv(0.25f, 0.8f, 0.6f);
        CHECK(c.h() == 0.25f);
        CHECK(c.s() == 0.8f);
        CHECK(c.v() == 0.6f);
}

TEST_CASE("Color: YCbCr accessors") {
        Color c = Color::ycbcr709(0.5f, 0.4f, 0.6f);
        CHECK(c.y() == 0.5f);
        CHECK(c.cb() == 0.4f);
        CHECK(c.cr() == 0.6f);
}

// ── r8/g8/b8 on non-sRGB colors ─────────────────────────────────

TEST_CASE("Color: r8/g8/b8 on HSV auto-converts to sRGB") {
        Color c = Color::hsv(0.0f, 1.0f, 1.0f); // red in HSV
        CHECK(c.r8() == 255);
        CHECK(c.g8() == 0);
        CHECK(c.b8() == 0);
}

TEST_CASE("Color: r8/g8/b8 on linear sRGB") {
        Color c = Color::linearSrgb(1.0f, 0.0f, 0.0f);
        CHECK(c.r8() == 255); // sRGB gamma of 1.0 linear = 1.0 encoded = 255
        CHECK(c.g8() == 0);
        CHECK(c.b8() == 0);
}

TEST_CASE("Color: a8 does not convert model") {
        Color c = Color::hsv(0.5f, 1.0f, 1.0f, 0.5f);
        CHECK(c.a8() == 128); // 0.5 * 255 = 127.5, rounds to 128
}

// ── convert comprehensive ────────────────────────────────────────

TEST_CASE("Color: identity conversion") {
        Color c = Color::Red;
        Color same = c.convert(ColorModel::sRGB);
        CHECK(same == c);
}

TEST_CASE("Color: convert invalid color returns invalid") {
        Color c;
        Color result = c.convert(ColorModel::sRGB);
        CHECK_FALSE(result.isValid());
}

TEST_CASE("Color: convert to Invalid model returns invalid") {
        Color c = Color::Red;
        Color result = c.convert(ColorModel::Invalid);
        CHECK_FALSE(result.isValid());
}

TEST_CASE("Color: sRGB -> LinearSRGB -> sRGB roundtrip") {
        Color c(128, 64, 32);
        Color lin = c.toLinearRGB();
        Color back = lin.toRGB();
        CHECK(back.r8() == 128);
        CHECK(back.g8() == 64);
        CHECK(back.b8() == 32);
}

TEST_CASE("Color: sRGB -> Rec709 -> sRGB roundtrip") {
        Color c(200, 100, 50);
        Color r709 = c.convert(ColorModel::Rec709);
        Color back = r709.convert(ColorModel::sRGB);
        CHECK(back.r8() == 200);
        CHECK(back.g8() == 100);
        CHECK(back.b8() == 50);
}

TEST_CASE("Color: sRGB -> Rec2020 -> sRGB roundtrip") {
        Color c(200, 100, 50);
        Color r2020 = c.convert(ColorModel::Rec2020);
        Color back = r2020.convert(ColorModel::sRGB);
        CHECK(back.r8() == 200);
        CHECK(back.g8() == 100);
        CHECK(back.b8() == 50);
}

TEST_CASE("Color: sRGB -> HSL -> sRGB roundtrip") {
        Color c(200, 100, 50);
        Color hsl = c.toHSL();
        Color back = hsl.toRGB();
        CHECK(back.r8() == 200);
        CHECK(back.g8() == 100);
        CHECK(back.b8() == 50);
}

TEST_CASE("Color: sRGB -> YCbCr_Rec709 -> sRGB roundtrip") {
        Color c(200, 100, 50);
        Color ycbcr = c.toYCbCr709();
        Color back = ycbcr.toRGB();
        CHECK(back.r8() == 200);
        CHECK(back.g8() == 100);
        CHECK(back.b8() == 50);
}

TEST_CASE("Color: sRGB -> XYZ -> sRGB roundtrip") {
        Color c(200, 100, 50);
        Color xyz = c.toXYZ();
        CHECK(xyz.model() == ColorModel::CIEXYZ);
        Color back = xyz.toRGB();
        CHECK(back.r8() == 200);
        CHECK(back.g8() == 100);
        CHECK(back.b8() == 50);
}

TEST_CASE("Color: alpha preservation through conversion") {
        Color c(200, 100, 50, 128);
        Color hsv = c.toHSV();
        CHECK(hsv.a8() == 128);
        Color lab = c.toLab();
        CHECK(lab.a8() == 128);
        Color back = lab.toRGB();
        CHECK(back.a8() == 128);
}

// ── Known conversion reference values ────────────────────────────

TEST_CASE("Color: red in HSV") {
        Color hsv = Color::Red.toHSV();
        CHECK(hsv.h() == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(hsv.s() == doctest::Approx(1.0f).epsilon(1e-2));
        CHECK(hsv.v() == doctest::Approx(1.0f).epsilon(1e-2));
}

TEST_CASE("Color: green in HSV") {
        Color hsv = Color::Green.toHSV();
        CHECK(hsv.h() == doctest::Approx(120.0f / 360.0f).epsilon(1e-2));
        CHECK(hsv.s() == doctest::Approx(1.0f).epsilon(1e-2));
        CHECK(hsv.v() == doctest::Approx(1.0f).epsilon(1e-2));
}

TEST_CASE("Color: blue in HSV") {
        Color hsv = Color::Blue.toHSV();
        CHECK(hsv.h() == doctest::Approx(240.0f / 360.0f).epsilon(1e-2));
        CHECK(hsv.s() == doctest::Approx(1.0f).epsilon(1e-2));
        CHECK(hsv.v() == doctest::Approx(1.0f).epsilon(1e-2));
}

TEST_CASE("Color: red in HSL") {
        Color hsl = Color::Red.toHSL();
        CHECK(hsl.h() == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(hsl.s() == doctest::Approx(1.0f).epsilon(1e-2));
        CHECK(hsl.comp(2) == doctest::Approx(0.5f).epsilon(1e-2)); // L=0.5
}

// ── Convenience conversion methods ───────────────────────────────

TEST_CASE("Color: toLinearRGB") {
        Color c = Color::Red;
        Color lin = c.toLinearRGB();
        CHECK(lin.model() == ColorModel::LinearSRGB);
        // sRGB 1.0 encoded -> 1.0 linear
        CHECK(lin.r() == doctest::Approx(1.0f).epsilon(1e-3));
        CHECK(lin.g() == doctest::Approx(0.0f).epsilon(1e-3));
}

TEST_CASE("Color: toHSL") {
        Color hsl = Color::Red.toHSL();
        CHECK(hsl.model() == ColorModel::HSL_sRGB);
}

TEST_CASE("Color: toYCbCr709") {
        Color ycbcr = Color::Red.toYCbCr709();
        CHECK(ycbcr.model() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("Color: toXYZ") {
        Color xyz = Color::Red.toXYZ();
        CHECK(xyz.model() == ColorModel::CIEXYZ);
}

// ── lerp ─────────────────────────────────────────────────────────

TEST_CASE("Color: lerp t=0 returns this") {
        Color a(100, 50, 25);
        Color b(200, 150, 75);
        Color result = a.lerp(b, 0.0);
        CHECK(result.r8() == 100);
        CHECK(result.g8() == 50);
        CHECK(result.b8() == 25);
}

TEST_CASE("Color: lerp t=1 returns other") {
        Color a(100, 50, 25);
        Color b(200, 150, 75);
        Color result = a.lerp(b, 1.0);
        CHECK(result.r8() == 200);
        CHECK(result.g8() == 150);
        CHECK(result.b8() == 75);
}

TEST_CASE("Color: lerp interpolates alpha") {
        Color a((uint8_t)255, (uint8_t)0, (uint8_t)0, (uint8_t)0);
        Color b((uint8_t)255, (uint8_t)0, (uint8_t)0, (uint8_t)255);
        Color mid = a.lerp(b, 0.5);
        CHECK(mid.a8() == 128);
}

TEST_CASE("Color: lerp cross-model converts other") {
        Color a = Color::srgb(1.0f, 0.0f, 0.0f);
        Color b = Color::hsv(120.0f / 360.0f, 1.0f, 1.0f); // green in HSV
        Color mid = a.lerp(b, 0.5);
        CHECK(mid.model() == ColorModel::sRGB); // result in a's model
}

TEST_CASE("Color: lerp in HSV space") {
        Color a = Color::hsv(0.0f, 1.0f, 1.0f);   // red
        Color b = Color::hsv(0.5f, 1.0f, 1.0f);   // cyan
        Color mid = a.lerp(b, 0.5);
        CHECK(mid.h() == doctest::Approx(0.25f));
        CHECK(mid.s() == doctest::Approx(1.0f));
}

// ── inverted ─────────────────────────────────────────────────────

TEST_CASE("Color: inverted preserves model") {
        Color c = Color::Red;
        Color inv = c.inverted();
        CHECK(inv.model() == ColorModel::sRGB);
}

// ── luminance ────────────────────────────────────────────────────

TEST_CASE("Color: luminance of primaries") {
        // Rec709 coefficients: R=0.2126, G=0.7152, B=0.0722
        // But Color::Red is sRGB encoded, so luminance() converts to linear first.
        // sRGB(1,0,0) -> linear(1,0,0) -> lum = 0.2126
        CHECK(Color::Red.luminance() == doctest::Approx(0.2126).epsilon(1e-3));
        CHECK(Color::Green.luminance() == doctest::Approx(0.7152).epsilon(1e-3));
        CHECK(Color::Blue.luminance() == doctest::Approx(0.0722).epsilon(1e-3));
}

TEST_CASE("Color: luminance on non-sRGB color") {
        Color c = Color::hsv(0.0f, 1.0f, 1.0f); // red in HSV
        CHECK(c.luminance() == doctest::Approx(Color::Red.luminance()).epsilon(1e-3));
}

// ── contrastingBW ────────────────────────────────────────────────

TEST_CASE("Color: contrastingBW preserves alpha") {
        Color c(32, 160, 64, 100);
        CHECK(c.contrastingBW().a8() == 100);
}

// ── complementary ────────────────────────────────────────────────

TEST_CASE("Color: complementary blue to yellow") {
        Color comp = Color::Blue.complementary();
        CHECK(comp.r8() == 255);
        CHECK(comp.g8() == 255);
        CHECK(comp.b8() == 0);
}

TEST_CASE("Color: complementary green to magenta") {
        Color comp = Color::Green.complementary();
        CHECK(comp.r8() == 255);
        CHECK(comp.g8() == 0);
        CHECK(comp.b8() == 255);
}

TEST_CASE("Color: double complementary roundtrips") {
        Color c(200, 100, 50);
        Color back = c.complementary().complementary();
        CHECK(back.r8() == 200);
        CHECK(back.g8() == 100);
        CHECK(back.b8() == 50);
}

// ── toString / toHex on non-sRGB ─────────────────────────────────

TEST_CASE("Color: toString on HSV color converts to sRGB") {
        Color c = Color::hsv(0.0f, 1.0f, 1.0f); // red
        CHECK(c.toString(Color::HexFormat) == "#ff0000");
}

TEST_CASE("Color: toHex on non-sRGB") {
        Color c = Color::hsv(120.0f / 360.0f, 1.0f, 1.0f); // green
        CHECK(c.toHex() == "#00ff00");
}

// ── operator== / operator!= ──────────────────────────────────────

TEST_CASE("Color: different model same components are not equal") {
        Color a(ColorModel::sRGB, 0.5f, 0.3f, 0.1f);
        Color b(ColorModel::Rec709, 0.5f, 0.3f, 0.1f);
        CHECK(a != b);
}

TEST_CASE("Color: self-equality") {
        CHECK(Color::Red == Color::Red);
}

TEST_CASE("Color: two invalid colors are equal") {
        CHECK(Color() == Color());
}

// ── isClose ──────────────────────────────────────────────────────

TEST_CASE("Color: isClose different models returns false") {
        Color a = Color::srgb(0.5f, 0.3f, 0.1f);
        Color b(ColorModel::Rec709, 0.5f, 0.3f, 0.1f);
        CHECK_FALSE(a.isClose(b));
}

TEST_CASE("Color: isClose exact same color") {
        CHECK(Color::Red.isClose(Color::Red));
}

TEST_CASE("Color: isClose with large epsilon") {
        Color a = Color::srgb(0.5f, 0.3f, 0.1f);
        Color b = Color::srgb(0.6f, 0.4f, 0.2f);
        CHECK_FALSE(a.isClose(b));        // default epsilon too small
        CHECK(a.isClose(b, 0.2f));         // large epsilon
}

TEST_CASE("Color: isClose checks alpha") {
        Color a = Color::srgb(0.5f, 0.3f, 0.1f, 1.0f);
        Color b = Color::srgb(0.5f, 0.3f, 0.1f, 0.5f);
        CHECK_FALSE(a.isClose(b));
}

// ── Named constants completeness ─────────────────────────────────

TEST_CASE("Color: all named constants values") {
        CHECK(Color::Yellow.r8() == 255);
        CHECK(Color::Yellow.g8() == 255);
        CHECK(Color::Yellow.b8() == 0);
        CHECK(Color::Yellow.a8() == 255);

        CHECK(Color::Cyan.r8() == 0);
        CHECK(Color::Cyan.g8() == 255);
        CHECK(Color::Cyan.b8() == 255);

        CHECK(Color::Magenta.r8() == 255);
        CHECK(Color::Magenta.g8() == 0);
        CHECK(Color::Magenta.b8() == 255);

        CHECK(Color::DarkGray.r8() == 64);
        CHECK(Color::DarkGray.g8() == 64);
        CHECK(Color::DarkGray.b8() == 64);

        CHECK(Color::LightGray.r8() == 192);
        CHECK(Color::LightGray.g8() == 192);
        CHECK(Color::LightGray.b8() == 192);

        CHECK(Color::Orange.r8() == 255);
        CHECK(Color::Orange.g8() == 165);
        CHECK(Color::Orange.b8() == 0);

        CHECK(Color::Transparent.r8() == 0);
        CHECK(Color::Transparent.g8() == 0);
        CHECK(Color::Transparent.b8() == 0);
        CHECK(Color::Transparent.a8() == 0);
}

TEST_CASE("Color: all named constants are sRGB") {
        CHECK(Color::Black.model() == ColorModel::sRGB);
        CHECK(Color::White.model() == ColorModel::sRGB);
        CHECK(Color::Red.model() == ColorModel::sRGB);
        CHECK(Color::Green.model() == ColorModel::sRGB);
        CHECK(Color::Blue.model() == ColorModel::sRGB);
        CHECK(Color::Yellow.model() == ColorModel::sRGB);
        CHECK(Color::Cyan.model() == ColorModel::sRGB);
        CHECK(Color::Magenta.model() == ColorModel::sRGB);
        CHECK(Color::DarkGray.model() == ColorModel::sRGB);
        CHECK(Color::LightGray.model() == ColorModel::sRGB);
        CHECK(Color::Orange.model() == ColorModel::sRGB);
        CHECK(Color::Transparent.model() == ColorModel::sRGB);
}

// ── fromString edge cases ────────────────────────────────────────

TEST_CASE("Color: fromString short hex is invalid") {
        CHECK_FALSE(Color::fromString("#fff").isValid());
}

TEST_CASE("Color: fromString # only is invalid") {
        CHECK_FALSE(Color::fromString("#").isValid());
}

TEST_CASE("Color: fromString invalid hex digits") {
        CHECK_FALSE(Color::fromString("#gggggg").isValid());
}

TEST_CASE("Color: fromString rgba out-of-range alpha") {
        CHECK_FALSE(Color::fromString("rgba(0.5,0.5,0.5,1.5)").isValid());
        CHECK_FALSE(Color::fromString("rgba(0.5,0.5,0.5,-0.1)").isValid());
}

// ── toNative / fromNative ────────────────────────────────────────

TEST_CASE("Color: toNative alpha returns raw value") {
        Color c = Color::srgb(0.5f, 0.3f, 0.1f, 0.7f);
        CHECK(c.toNative(3) == doctest::Approx(0.7f));
}

TEST_CASE("Color: fromNative passes alpha through") {
        Color c = Color::fromNative(ColorModel::HSV_sRGB, 90.0f, 1.0f, 1.0f, 0.3f);
        CHECK(c.alpha() == 0.3f);
}
