/**
 * @file      colormodel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/colormodel.h>

using namespace promeki;

TEST_CASE("ColorModel: Invalid model") {
        CHECK_FALSE(ColorModel::Invalid.isValid());
        CHECK(ColorModel::Invalid.type() == ColorModel::TypeInvalid);
}

TEST_CASE("ColorModel: id() returns the correct ID") {
        CHECK(ColorModel::Invalid.id() == ColorModel::Invalid_ID);
        CHECK(ColorModel::sRGB.id() == ColorModel::sRGB_ID);
        CHECK(ColorModel::Rec709.id() == ColorModel::Rec709_ID);
        CHECK(ColorModel::HSV_sRGB.id() == ColorModel::HSV_sRGB_ID);
        CHECK(ColorModel::YCbCr_Rec2020.id() == ColorModel::YCbCr_Rec2020_ID);
}

TEST_CASE("ColorModel: sRGB model properties") {
        const ColorModel &m = ColorModel::sRGB;
        CHECK(m.isValid());
        CHECK(m.type() == ColorModel::TypeRGB);
        CHECK(m.name() == "sRGB");
        CHECK(m.compCount() == 3);
        CHECK_FALSE(m.isLinear());
        CHECK(m.compInfo(0).abbrev == "R");
        CHECK(m.compInfo(1).abbrev == "G");
        CHECK(m.compInfo(2).abbrev == "B");
}

TEST_CASE("ColorModel: LinearSRGB model properties") {
        const ColorModel &m = ColorModel::LinearSRGB;
        CHECK(m.isValid());
        CHECK(m.type() == ColorModel::TypeRGB);
        CHECK(m.isLinear());
}

TEST_CASE("ColorModel: sRGB and Rec709 share primaries") {
        auto &sp = ColorModel::sRGB.primaries();
        auto &rp = ColorModel::Rec709.primaries();
        for(int i = 0; i < 4; ++i) {
                CHECK(sp[i].data()[0] == doctest::Approx(rp[i].data()[0]));
                CHECK(sp[i].data()[1] == doctest::Approx(rp[i].data()[1]));
        }
}

TEST_CASE("ColorModel: sRGB transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::sRGB.applyTransfer(v);
                double decoded = ColorModel::sRGB.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: Rec709 transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::Rec709.applyTransfer(v);
                double decoded = ColorModel::Rec709.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: sRGB toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::sRGB.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: LinearSRGB toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.2f, 0.7f, 0.4f };
        float xyz[3], dst[3];
        ColorModel::LinearSRGB.toXYZ(src, xyz);
        ColorModel::LinearSRGB.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: sRGB white maps to D65 XYZ") {
        float white[3] = { 1.0f, 1.0f, 1.0f };
        float xyz[3];
        ColorModel::LinearSRGB.toXYZ(white, xyz);
        // D65 white point: X=0.95047, Y=1.0, Z=1.08883
        CHECK(xyz[0] == doctest::Approx(0.95047f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(1.0f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(1.08883f).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB red produces expected XYZ") {
        float red[3] = { 1.0f, 0.0f, 0.0f };
        float xyz[3];
        ColorModel::LinearSRGB.toXYZ(red, xyz);
        // sRGB red primary: X~0.4124, Y~0.2126, Z~0.0193
        CHECK(xyz[0] == doctest::Approx(0.4124f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(0.2126f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(0.0193f).epsilon(1e-3));
}

TEST_CASE("ColorModel: Cross-space conversion sRGB -> Rec601_PAL -> sRGB") {
        float src[3] = { 0.8f, 0.2f, 0.5f };
        float xyz[3], dst601[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::Rec601_PAL.fromXYZ(xyz, dst601);
        ColorModel::Rec601_PAL.toXYZ(dst601, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: HSV sRGB roundtrip through XYZ") {
        // Pure red in HSV: H=0, S=1, V=1 (normalized: 0, 1, 1)
        float hsv[3] = { 0.0f, 1.0f, 1.0f };
        float xyz[3], hsv2[3];
        ColorModel::HSV_sRGB.toXYZ(hsv, xyz);
        ColorModel::HSV_sRGB.fromXYZ(xyz, hsv2);
        CHECK(hsv2[0] == doctest::Approx(hsv[0]).epsilon(1e-3));
        CHECK(hsv2[1] == doctest::Approx(hsv[1]).epsilon(1e-3));
        CHECK(hsv2[2] == doctest::Approx(hsv[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: HSV pure green") {
        // Green in HSV: H=120/360 = 0.333..., S=1, V=1
        float hsv[3] = { 120.0f / 360.0f, 1.0f, 1.0f };
        float xyz[3];
        ColorModel::HSV_sRGB.toXYZ(hsv, xyz);
        // Then convert back via sRGB to check
        float rgb[3];
        ColorModel::sRGB.fromXYZ(xyz, rgb);
        CHECK(rgb[0] == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(rgb[1] == doctest::Approx(1.0f).epsilon(1e-2));
        CHECK(rgb[2] == doctest::Approx(0.0f).epsilon(1e-2));
}

TEST_CASE("ColorModel: HSL roundtrip through XYZ") {
        float hsl[3] = { 0.6f, 0.8f, 0.5f };
        float xyz[3], hsl2[3];
        ColorModel::HSL_sRGB.toXYZ(hsl, xyz);
        ColorModel::HSL_sRGB.fromXYZ(xyz, hsl2);
        CHECK(hsl2[0] == doctest::Approx(hsl[0]).epsilon(1e-3));
        CHECK(hsl2[1] == doctest::Approx(hsl[1]).epsilon(1e-3));
        CHECK(hsl2[2] == doctest::Approx(hsl[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: CIE Lab roundtrip through XYZ") {
        // Mid-gray in Lab: L=50 -> normalized 0.5, a=0 -> normalized ~0.502, b=0 -> normalized ~0.502
        float lab[3] = { 0.5f, 128.0f / 255.0f, 128.0f / 255.0f };
        float xyz[3], lab2[3];
        ColorModel::CIELab.toXYZ(lab, xyz);
        ColorModel::CIELab.fromXYZ(xyz, lab2);
        CHECK(lab2[0] == doctest::Approx(lab[0]).epsilon(1e-4));
        CHECK(lab2[1] == doctest::Approx(lab[1]).epsilon(1e-4));
        CHECK(lab2[2] == doctest::Approx(lab[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: YCbCr Rec709 roundtrip through XYZ") {
        // Y=0.5, Cb=0.5 (centered), Cr=0.5 (centered) = gray
        float ycbcr[3] = { 0.5f, 0.5f, 0.5f };
        float xyz[3], ycbcr2[3];
        ColorModel::YCbCr_Rec709.toXYZ(ycbcr, xyz);
        ColorModel::YCbCr_Rec709.fromXYZ(xyz, ycbcr2);
        CHECK(ycbcr2[0] == doctest::Approx(ycbcr[0]).epsilon(1e-3));
        CHECK(ycbcr2[1] == doctest::Approx(ycbcr[1]).epsilon(1e-3));
        CHECK(ycbcr2[2] == doctest::Approx(ycbcr[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: Linear counterpart") {
        CHECK(ColorModel::sRGB.linearCounterpart() == ColorModel::LinearSRGB);
        CHECK(ColorModel::LinearSRGB.linearCounterpart() == ColorModel::LinearSRGB);
        CHECK(ColorModel::Rec709.linearCounterpart() == ColorModel::LinearRec709);
}

TEST_CASE("ColorModel: Nonlinear counterpart") {
        CHECK(ColorModel::LinearSRGB.nonlinearCounterpart() == ColorModel::sRGB);
        CHECK(ColorModel::sRGB.nonlinearCounterpart() == ColorModel::sRGB);
}

TEST_CASE("ColorModel: Parent model") {
        CHECK(ColorModel::HSV_sRGB.parentModel() == ColorModel::sRGB);
        CHECK(ColorModel::HSL_sRGB.parentModel() == ColorModel::sRGB);
        CHECK(ColorModel::YCbCr_Rec709.parentModel() == ColorModel::Rec709);
        CHECK_FALSE(ColorModel::sRGB.parentModel().isValid());
}

TEST_CASE("ColorModel: Lookup by name") {
        CHECK(ColorModel::lookup("sRGB") == ColorModel::sRGB);
        CHECK(ColorModel::lookup("Rec709") == ColorModel::Rec709);
        CHECK(ColorModel::lookup("HSV_sRGB") == ColorModel::HSV_sRGB);
        CHECK(ColorModel::lookup("nonexistent") == ColorModel::Invalid);
}

TEST_CASE("ColorModel: toNative and fromNative") {
        // HSV hue: normalized 0.5 -> native 180 degrees
        CHECK(ColorModel::HSV_sRGB.toNative(0, 0.5f) == doctest::Approx(180.0f));
        CHECK(ColorModel::HSV_sRGB.fromNative(0, 180.0f) == doctest::Approx(0.5f));

        // Lab L: normalized 0.5 -> native 50
        CHECK(ColorModel::CIELab.toNative(0, 0.5f) == doctest::Approx(50.0f));
        CHECK(ColorModel::CIELab.fromNative(0, 50.0f) == doctest::Approx(0.5f));
}

TEST_CASE("ColorModel: Rec2020 properties") {
        CHECK(ColorModel::Rec2020.isValid());
        CHECK(ColorModel::Rec2020.type() == ColorModel::TypeRGB);
        CHECK_FALSE(ColorModel::Rec2020.isLinear());
        CHECK(ColorModel::LinearRec2020.isLinear());
}

TEST_CASE("ColorModel: DCI_P3 properties") {
        CHECK(ColorModel::DCI_P3.isValid());
        CHECK(ColorModel::DCI_P3.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::DCI_P3.name() == "DCI_P3");
        CHECK_FALSE(ColorModel::DCI_P3.isLinear());
        CHECK(ColorModel::LinearDCI_P3.isLinear());
        CHECK(ColorModel::DCI_P3.linearCounterpart() == ColorModel::LinearDCI_P3);
        CHECK(ColorModel::LinearDCI_P3.nonlinearCounterpart() == ColorModel::DCI_P3);
}

TEST_CASE("ColorModel: DCI_P3 transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::DCI_P3.applyTransfer(v);
                double decoded = ColorModel::DCI_P3.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: DCI_P3 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel::DCI_P3.toXYZ(src, xyz);
        ColorModel::DCI_P3.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: sRGB -> DCI_P3 -> sRGB roundtrip") {
        float src[3] = { 0.8f, 0.2f, 0.5f };
        float xyz[3], p3[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::DCI_P3.fromXYZ(xyz, p3);
        ColorModel::DCI_P3.toXYZ(p3, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: DCI_P3 uses D65 white point") {
        CIEPoint wp = ColorModel::DCI_P3.whitePoint();
        CHECK(wp.x() == doctest::Approx(0.3127).epsilon(1e-4));
        CHECK(wp.y() == doctest::Approx(0.3290).epsilon(1e-4));
}

TEST_CASE("ColorModel: DCI_P3 lookup") {
        CHECK(ColorModel::lookup("DCI_P3") == ColorModel::DCI_P3);
        CHECK(ColorModel::lookup("LinearDCI_P3") == ColorModel::LinearDCI_P3);
}

// ── Adobe RGB ─────────────────────────────────────────────────────

TEST_CASE("ColorModel: AdobeRGB properties") {
        CHECK(ColorModel::AdobeRGB.isValid());
        CHECK(ColorModel::AdobeRGB.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::AdobeRGB.name() == "AdobeRGB");
        CHECK_FALSE(ColorModel::AdobeRGB.isLinear());
        CHECK(ColorModel::LinearAdobeRGB.isLinear());
        CHECK(ColorModel::AdobeRGB.linearCounterpart() == ColorModel::LinearAdobeRGB);
        CHECK(ColorModel::LinearAdobeRGB.nonlinearCounterpart() == ColorModel::AdobeRGB);
}

TEST_CASE("ColorModel: AdobeRGB transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::AdobeRGB.applyTransfer(v);
                double decoded = ColorModel::AdobeRGB.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: AdobeRGB toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel::AdobeRGB.toXYZ(src, xyz);
        ColorModel::AdobeRGB.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: sRGB -> AdobeRGB -> sRGB roundtrip") {
        float src[3] = { 0.8f, 0.2f, 0.5f };
        float xyz[3], argb[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::AdobeRGB.fromXYZ(xyz, argb);
        ColorModel::AdobeRGB.toXYZ(argb, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: AdobeRGB lookup") {
        CHECK(ColorModel::lookup("AdobeRGB") == ColorModel::AdobeRGB);
        CHECK(ColorModel::lookup("LinearAdobeRGB") == ColorModel::LinearAdobeRGB);
}

// ── ACES ──────────────────────────────────────────────────────────

TEST_CASE("ColorModel: ACES AP0 properties") {
        CHECK(ColorModel::ACES_AP0.isValid());
        CHECK(ColorModel::ACES_AP0.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::ACES_AP0.name() == "ACES_AP0");
        CHECK(ColorModel::ACES_AP0.isLinear());
}

TEST_CASE("ColorModel: ACES AP1 properties") {
        CHECK(ColorModel::ACES_AP1.isValid());
        CHECK(ColorModel::ACES_AP1.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::ACES_AP1.name() == "ACES_AP1");
        CHECK(ColorModel::ACES_AP1.isLinear());
}

TEST_CASE("ColorModel: ACES uses D60 white point") {
        // ACES D60: (0.32168, 0.33767)
        CIEPoint wp = ColorModel::ACES_AP0.whitePoint();
        CHECK(wp.x() == doctest::Approx(0.32168).epsilon(1e-4));
        CHECK(wp.y() == doctest::Approx(0.33767).epsilon(1e-4));
        // AP1 uses the same white point
        CIEPoint wp1 = ColorModel::ACES_AP1.whitePoint();
        CHECK(wp1.x() == doctest::Approx(0.32168).epsilon(1e-4));
}

TEST_CASE("ColorModel: ACES AP0 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.2f };
        float xyz[3], dst[3];
        ColorModel::ACES_AP0.toXYZ(src, xyz);
        ColorModel::ACES_AP0.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: ACES AP1 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.2f };
        float xyz[3], dst[3];
        ColorModel::ACES_AP1.toXYZ(src, xyz);
        ColorModel::ACES_AP1.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> ACES AP0 -> sRGB roundtrip") {
        float src[3] = { 0.6f, 0.3f, 0.9f };
        float xyz[3], aces[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::ACES_AP0.fromXYZ(xyz, aces);
        ColorModel::ACES_AP0.toXYZ(aces, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: ACES lookup") {
        CHECK(ColorModel::lookup("ACES_AP0") == ColorModel::ACES_AP0);
        CHECK(ColorModel::lookup("ACES_AP1") == ColorModel::ACES_AP1);
}

// ── YCbCr Rec.2020 ───────────────────────────────────────────────

TEST_CASE("ColorModel: YCbCr_Rec2020 properties") {
        CHECK(ColorModel::YCbCr_Rec2020.isValid());
        CHECK(ColorModel::YCbCr_Rec2020.type() == ColorModel::TypeYCbCr);
        CHECK(ColorModel::YCbCr_Rec2020.name() == "YCbCr_Rec2020");
        CHECK(ColorModel::YCbCr_Rec2020.parentModel() == ColorModel::Rec2020);
}

TEST_CASE("ColorModel: YCbCr_Rec2020 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.55f, 0.45f };
        float xyz[3], dst[3];
        ColorModel::YCbCr_Rec2020.toXYZ(src, xyz);
        ColorModel::YCbCr_Rec2020.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> YCbCr_Rec2020 -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], ycbcr[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::YCbCr_Rec2020.fromXYZ(xyz, ycbcr);
        ColorModel::YCbCr_Rec2020.toXYZ(ycbcr, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: YCbCr_Rec2020 gray has centered chroma") {
        // Gray in Rec2020 -> YCbCr should give Cb=0.5, Cr=0.5
        float gray[3] = { 0.5f, 0.5f, 0.5f };
        float xyz[3], ycbcr[3];
        ColorModel::Rec2020.toXYZ(gray, xyz);
        ColorModel::YCbCr_Rec2020.fromXYZ(xyz, ycbcr);
        CHECK(ycbcr[1] == doctest::Approx(0.5f).epsilon(1e-2));
        CHECK(ycbcr[2] == doctest::Approx(0.5f).epsilon(1e-2));
}

TEST_CASE("ColorModel: YCbCr_Rec2020 lookup") {
        CHECK(ColorModel::lookup("YCbCr_Rec2020") == ColorModel::YCbCr_Rec2020);
}

TEST_CASE("ColorModel: Rec2020 transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::Rec2020.applyTransfer(v);
                double decoded = ColorModel::Rec2020.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

// ── Well-known instance properties ─────────────────────────────────

TEST_CASE("ColorModel: default constructor is Invalid") {
        ColorModel m;
        CHECK_FALSE(m.isValid());
        CHECK(m == ColorModel::Invalid);
}

TEST_CASE("ColorModel: equality and inequality") {
        CHECK(ColorModel::sRGB == ColorModel::sRGB);
        CHECK(ColorModel::sRGB != ColorModel::Rec709);
        CHECK(ColorModel() == ColorModel::Invalid);
}

TEST_CASE("ColorModel: desc accessor") {
        CHECK(ColorModel::sRGB.desc() == "IEC 61966-2-1 sRGB");
        CHECK(ColorModel::Invalid.desc() == "Invalid color model");
}

TEST_CASE("ColorModel: whitePoint") {
        CIEPoint wp = ColorModel::sRGB.whitePoint();
        CHECK(wp.x() == doctest::Approx(0.3127).epsilon(1e-4));
        CHECK(wp.y() == doctest::Approx(0.3290).epsilon(1e-4));
        // Rec2020 also uses D65
        CIEPoint wp2 = ColorModel::Rec2020.whitePoint();
        CHECK(wp2.x() == doctest::Approx(0.3127).epsilon(1e-4));
        CHECK(wp2.y() == doctest::Approx(0.3290).epsilon(1e-4));
}

TEST_CASE("ColorModel: all well-known instances are valid") {
        CHECK(ColorModel::sRGB.isValid());
        CHECK(ColorModel::LinearSRGB.isValid());
        CHECK(ColorModel::Rec709.isValid());
        CHECK(ColorModel::LinearRec709.isValid());
        CHECK(ColorModel::Rec601_PAL.isValid());
        CHECK(ColorModel::LinearRec601_PAL.isValid());
        CHECK(ColorModel::Rec601_NTSC.isValid());
        CHECK(ColorModel::LinearRec601_NTSC.isValid());
        CHECK(ColorModel::Rec2020.isValid());
        CHECK(ColorModel::LinearRec2020.isValid());
        CHECK(ColorModel::DCI_P3.isValid());
        CHECK(ColorModel::LinearDCI_P3.isValid());
        CHECK(ColorModel::CIEXYZ.isValid());
        CHECK(ColorModel::CIELab.isValid());
        CHECK(ColorModel::HSV_sRGB.isValid());
        CHECK(ColorModel::HSL_sRGB.isValid());
        CHECK(ColorModel::YCbCr_Rec709.isValid());
        CHECK(ColorModel::YCbCr_Rec601.isValid());
}

TEST_CASE("ColorModel: type for each model") {
        CHECK(ColorModel::Rec709.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::LinearRec709.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::Rec601_PAL.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::Rec601_NTSC.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::Rec2020.type() == ColorModel::TypeRGB);
        CHECK(ColorModel::CIEXYZ.type() == ColorModel::TypeXYZ);
        CHECK(ColorModel::CIELab.type() == ColorModel::TypeLab);
        CHECK(ColorModel::HSV_sRGB.type() == ColorModel::TypeHSV);
        CHECK(ColorModel::HSL_sRGB.type() == ColorModel::TypeHSL);
        CHECK(ColorModel::YCbCr_Rec709.type() == ColorModel::TypeYCbCr);
        CHECK(ColorModel::YCbCr_Rec601.type() == ColorModel::TypeYCbCr);
}

TEST_CASE("ColorModel: name for each model") {
        CHECK(ColorModel::Rec709.name() == "Rec709");
        CHECK(ColorModel::LinearRec709.name() == "LinearRec709");
        CHECK(ColorModel::Rec601_PAL.name() == "Rec601_PAL");
        CHECK(ColorModel::Rec601_NTSC.name() == "Rec601_NTSC");
        CHECK(ColorModel::Rec2020.name() == "Rec2020");
        CHECK(ColorModel::CIEXYZ.name() == "CIEXYZ");
        CHECK(ColorModel::CIELab.name() == "CIELab");
        CHECK(ColorModel::HSV_sRGB.name() == "HSV_sRGB");
        CHECK(ColorModel::HSL_sRGB.name() == "HSL_sRGB");
        CHECK(ColorModel::YCbCr_Rec709.name() == "YCbCr_Rec709");
        CHECK(ColorModel::YCbCr_Rec601.name() == "YCbCr_Rec601");
}

TEST_CASE("ColorModel: compCount is always 3") {
        CHECK(ColorModel::CIEXYZ.compCount() == 3);
        CHECK(ColorModel::CIELab.compCount() == 3);
        CHECK(ColorModel::HSV_sRGB.compCount() == 3);
        CHECK(ColorModel::YCbCr_Rec709.compCount() == 3);
}

// ── CompInfo ──────────────────────────────────────────────────────

TEST_CASE("ColorModel: sRGB CompInfo full details") {
        CHECK(ColorModel::sRGB.compInfo(0).name == "Red");
        CHECK(ColorModel::sRGB.compInfo(0).nativeMin == 0.0f);
        CHECK(ColorModel::sRGB.compInfo(0).nativeMax == 1.0f);
        CHECK(ColorModel::sRGB.compInfo(1).name == "Green");
        CHECK(ColorModel::sRGB.compInfo(2).name == "Blue");
}

TEST_CASE("ColorModel: HSV CompInfo") {
        CHECK(ColorModel::HSV_sRGB.compInfo(0).name == "Hue");
        CHECK(ColorModel::HSV_sRGB.compInfo(0).abbrev == "H");
        CHECK(ColorModel::HSV_sRGB.compInfo(0).nativeMin == 0.0f);
        CHECK(ColorModel::HSV_sRGB.compInfo(0).nativeMax == 360.0f);
        CHECK(ColorModel::HSV_sRGB.compInfo(1).name == "Saturation");
        CHECK(ColorModel::HSV_sRGB.compInfo(1).nativeMax == 1.0f);
        CHECK(ColorModel::HSV_sRGB.compInfo(2).name == "Value");
}

TEST_CASE("ColorModel: HSL CompInfo") {
        CHECK(ColorModel::HSL_sRGB.compInfo(0).abbrev == "H");
        CHECK(ColorModel::HSL_sRGB.compInfo(0).nativeMax == 360.0f);
        CHECK(ColorModel::HSL_sRGB.compInfo(2).name == "Lightness");
        CHECK(ColorModel::HSL_sRGB.compInfo(2).abbrev == "L");
}

TEST_CASE("ColorModel: Lab CompInfo") {
        CHECK(ColorModel::CIELab.compInfo(0).name == "Lightness");
        CHECK(ColorModel::CIELab.compInfo(0).nativeMin == 0.0f);
        CHECK(ColorModel::CIELab.compInfo(0).nativeMax == 100.0f);
        CHECK(ColorModel::CIELab.compInfo(1).name == "a");
        CHECK(ColorModel::CIELab.compInfo(1).nativeMin == -128.0f);
        CHECK(ColorModel::CIELab.compInfo(1).nativeMax == 127.0f);
        CHECK(ColorModel::CIELab.compInfo(2).name == "b");
        CHECK(ColorModel::CIELab.compInfo(2).nativeMin == -128.0f);
}

TEST_CASE("ColorModel: YCbCr CompInfo") {
        CHECK(ColorModel::YCbCr_Rec709.compInfo(0).name == "Luma");
        CHECK(ColorModel::YCbCr_Rec709.compInfo(0).abbrev == "Y");
        CHECK(ColorModel::YCbCr_Rec709.compInfo(1).abbrev == "Cb");
        CHECK(ColorModel::YCbCr_Rec709.compInfo(1).nativeMin == -0.5f);
        CHECK(ColorModel::YCbCr_Rec709.compInfo(1).nativeMax == 0.5f);
        CHECK(ColorModel::YCbCr_Rec709.compInfo(2).abbrev == "Cr");
}

TEST_CASE("ColorModel: XYZ CompInfo") {
        CHECK(ColorModel::CIEXYZ.compInfo(0).abbrev == "X");
        CHECK(ColorModel::CIEXYZ.compInfo(1).abbrev == "Y");
        CHECK(ColorModel::CIEXYZ.compInfo(2).abbrev == "Z");
}

TEST_CASE("ColorModel: compInfo out-of-range index falls back to index 0") {
        CHECK(ColorModel::sRGB.compInfo(3).abbrev == "R");
        CHECK(ColorModel::sRGB.compInfo(99).abbrev == "R");
}

// ── Transfer functions ────────────────────────────────────────────

TEST_CASE("ColorModel: linear models have identity transfer") {
        const ColorModel *linears[] = {
                &ColorModel::LinearSRGB, &ColorModel::LinearRec709,
                &ColorModel::LinearRec601_PAL, &ColorModel::LinearRec601_NTSC,
                &ColorModel::LinearRec2020
        };
        for(auto *m : linears) {
                CHECK(m->isLinear());
                CHECK(m->applyTransfer(0.5) == doctest::Approx(0.5));
                CHECK(m->removeTransfer(0.5) == doctest::Approx(0.5));
        }
}

TEST_CASE("ColorModel: Rec601_PAL transfer roundtrip") {
        double values[] = { 0.0, 0.01, 0.5, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::Rec601_PAL.applyTransfer(v);
                double decoded = ColorModel::Rec601_PAL.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: Rec601_NTSC transfer roundtrip") {
        double values[] = { 0.0, 0.01, 0.5, 1.0 };
        for(double v : values) {
                double encoded = ColorModel::Rec601_NTSC.applyTransfer(v);
                double decoded = ColorModel::Rec601_NTSC.removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

// ── toXYZ/fromXYZ roundtrips for all models ──────────────────────

TEST_CASE("ColorModel: Rec709 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel::Rec709.toXYZ(src, xyz);
        ColorModel::Rec709.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: Rec601_PAL toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.4f, 0.6f, 0.2f };
        float xyz[3], dst[3];
        ColorModel::Rec601_PAL.toXYZ(src, xyz);
        ColorModel::Rec601_PAL.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: Rec601_NTSC toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.7f, 0.1f, 0.9f };
        float xyz[3], dst[3];
        ColorModel::Rec601_NTSC.toXYZ(src, xyz);
        ColorModel::Rec601_NTSC.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: Rec2020 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.3f, 0.5f, 0.7f };
        float xyz[3], dst[3];
        ColorModel::Rec2020.toXYZ(src, xyz);
        ColorModel::Rec2020.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: LinearRec2020 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.3f, 0.5f, 0.7f };
        float xyz[3], dst[3];
        ColorModel::LinearRec2020.toXYZ(src, xyz);
        ColorModel::LinearRec2020.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: YCbCr_Rec601 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.4f, 0.55f, 0.45f };
        float xyz[3], dst[3];
        ColorModel::YCbCr_Rec601.toXYZ(src, xyz);
        ColorModel::YCbCr_Rec601.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: HSV non-primary roundtrip") {
        float src[3] = { 0.75f, 0.5f, 0.8f };
        float xyz[3], dst[3];
        ColorModel::HSV_sRGB.toXYZ(src, xyz);
        ColorModel::HSV_sRGB.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

// ── Cross-space conversions ──────────────────────────────────────

TEST_CASE("ColorModel: sRGB -> Rec2020 -> sRGB roundtrip") {
        float src[3] = { 0.6f, 0.3f, 0.9f };
        float xyz[3], r2020[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::Rec2020.fromXYZ(xyz, r2020);
        ColorModel::Rec2020.toXYZ(r2020, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> YCbCr_Rec709 -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], ycbcr[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::YCbCr_Rec709.fromXYZ(xyz, ycbcr);
        ColorModel::YCbCr_Rec709.toXYZ(ycbcr, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> HSV -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], hsv[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::HSV_sRGB.fromXYZ(xyz, hsv);
        ColorModel::HSV_sRGB.toXYZ(hsv, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> Lab -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], lab[3], xyz2[3], dst[3];
        ColorModel::sRGB.toXYZ(src, xyz);
        ColorModel::CIELab.fromXYZ(xyz, lab);
        ColorModel::CIELab.toXYZ(lab, xyz2);
        ColorModel::sRGB.fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

// ── Known XYZ reference values ───────────────────────────────────

TEST_CASE("ColorModel: linear sRGB green XYZ") {
        float green[3] = { 0.0f, 1.0f, 0.0f };
        float xyz[3];
        ColorModel::LinearSRGB.toXYZ(green, xyz);
        CHECK(xyz[0] == doctest::Approx(0.3576f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(0.7152f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(0.1192f).epsilon(1e-3));
}

TEST_CASE("ColorModel: linear sRGB blue XYZ") {
        float blue[3] = { 0.0f, 0.0f, 1.0f };
        float xyz[3];
        ColorModel::LinearSRGB.toXYZ(blue, xyz);
        CHECK(xyz[0] == doctest::Approx(0.1805f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(0.0722f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(0.9505f).epsilon(1e-3));
}

TEST_CASE("ColorModel: linear sRGB black XYZ is zero") {
        float black[3] = { 0.0f, 0.0f, 0.0f };
        float xyz[3];
        ColorModel::LinearSRGB.toXYZ(black, xyz);
        CHECK(xyz[0] == doctest::Approx(0.0f));
        CHECK(xyz[1] == doctest::Approx(0.0f));
        CHECK(xyz[2] == doctest::Approx(0.0f));
}

// ── linearCounterpart / nonlinearCounterpart completeness ────────

TEST_CASE("ColorModel: all linear/nonlinear counterpart pairs") {
        CHECK(ColorModel::Rec601_PAL.linearCounterpart() == ColorModel::LinearRec601_PAL);
        CHECK(ColorModel::LinearRec601_PAL.nonlinearCounterpart() == ColorModel::Rec601_PAL);
        CHECK(ColorModel::Rec601_NTSC.linearCounterpart() == ColorModel::LinearRec601_NTSC);
        CHECK(ColorModel::LinearRec601_NTSC.nonlinearCounterpart() == ColorModel::Rec601_NTSC);
        CHECK(ColorModel::Rec2020.linearCounterpart() == ColorModel::LinearRec2020);
        CHECK(ColorModel::LinearRec2020.nonlinearCounterpart() == ColorModel::Rec2020);
        CHECK(ColorModel::LinearRec709.nonlinearCounterpart() == ColorModel::Rec709);
}

TEST_CASE("ColorModel: non-RGB models return self for counterparts") {
        CHECK(ColorModel::CIEXYZ.linearCounterpart() == ColorModel::CIEXYZ);
        CHECK(ColorModel::CIEXYZ.nonlinearCounterpart() == ColorModel::CIEXYZ);
        CHECK(ColorModel::CIELab.linearCounterpart() == ColorModel::CIELab);
        CHECK(ColorModel::HSV_sRGB.linearCounterpart() == ColorModel::HSV_sRGB);
        CHECK(ColorModel::YCbCr_Rec709.linearCounterpart() == ColorModel::YCbCr_Rec709);
}

// ── parentModel completeness ─────────────────────────────────────

TEST_CASE("ColorModel: parentModel for all types") {
        CHECK(ColorModel::YCbCr_Rec601.parentModel() == ColorModel::Rec601_PAL);
        CHECK_FALSE(ColorModel::LinearSRGB.parentModel().isValid());
        CHECK_FALSE(ColorModel::Rec709.parentModel().isValid());
        CHECK_FALSE(ColorModel::Rec2020.parentModel().isValid());
        CHECK_FALSE(ColorModel::CIEXYZ.parentModel().isValid());
        CHECK_FALSE(ColorModel::CIELab.parentModel().isValid());
        CHECK_FALSE(ColorModel::Invalid.parentModel().isValid());
}

// ── lookup completeness ──────────────────────────────────────────

TEST_CASE("ColorModel: lookup all names") {
        CHECK(ColorModel::lookup("LinearSRGB") == ColorModel::LinearSRGB);
        CHECK(ColorModel::lookup("LinearRec709") == ColorModel::LinearRec709);
        CHECK(ColorModel::lookup("Rec601_PAL") == ColorModel::Rec601_PAL);
        CHECK(ColorModel::lookup("LinearRec601_PAL") == ColorModel::LinearRec601_PAL);
        CHECK(ColorModel::lookup("Rec601_NTSC") == ColorModel::Rec601_NTSC);
        CHECK(ColorModel::lookup("LinearRec601_NTSC") == ColorModel::LinearRec601_NTSC);
        CHECK(ColorModel::lookup("Rec2020") == ColorModel::Rec2020);
        CHECK(ColorModel::lookup("LinearRec2020") == ColorModel::LinearRec2020);
        CHECK(ColorModel::lookup("CIEXYZ") == ColorModel::CIEXYZ);
        CHECK(ColorModel::lookup("CIELab") == ColorModel::CIELab);
        CHECK(ColorModel::lookup("HSL_sRGB") == ColorModel::HSL_sRGB);
        CHECK(ColorModel::lookup("YCbCr_Rec709") == ColorModel::YCbCr_Rec709);
        CHECK(ColorModel::lookup("YCbCr_Rec601") == ColorModel::YCbCr_Rec601);
}

TEST_CASE("ColorModel: lookup empty string") {
        CHECK(ColorModel::lookup("") == ColorModel::Invalid);
}

TEST_CASE("ColorModel: lookup is case-sensitive") {
        CHECK(ColorModel::lookup("srgb") == ColorModel::Invalid);
        CHECK(ColorModel::lookup("SRGB") == ColorModel::Invalid);
}

// ── toNative / fromNative completeness ───────────────────────────

TEST_CASE("ColorModel: toNative edge values") {
        // 0.0 maps to nativeMin, 1.0 maps to nativeMax
        CHECK(ColorModel::HSV_sRGB.toNative(0, 0.0f) == doctest::Approx(0.0f));
        CHECK(ColorModel::HSV_sRGB.toNative(0, 1.0f) == doctest::Approx(360.0f));
        CHECK(ColorModel::CIELab.toNative(0, 0.0f) == doctest::Approx(0.0f));
        CHECK(ColorModel::CIELab.toNative(0, 1.0f) == doctest::Approx(100.0f));
        CHECK(ColorModel::CIELab.toNative(1, 0.0f) == doctest::Approx(-128.0f));
        CHECK(ColorModel::CIELab.toNative(1, 1.0f) == doctest::Approx(127.0f));
        CHECK(ColorModel::YCbCr_Rec709.toNative(1, 0.0f) == doctest::Approx(-0.5f));
        CHECK(ColorModel::YCbCr_Rec709.toNative(1, 1.0f) == doctest::Approx(0.5f));
}

TEST_CASE("ColorModel: toNative/fromNative out-of-range comp") {
        CHECK(ColorModel::sRGB.toNative(3, 0.5f) == 0.0f);
        CHECK(ColorModel::sRGB.fromNative(3, 0.5f) == 0.0f);
}

TEST_CASE("ColorModel: RGB toNative is identity") {
        CHECK(ColorModel::sRGB.toNative(0, 0.5f) == doctest::Approx(0.5f));
        CHECK(ColorModel::sRGB.fromNative(0, 0.5f) == doctest::Approx(0.5f));
}

// ── Edge-case colors through model spaces ────────────────────────

TEST_CASE("ColorModel: black through HSV") {
        float black[3] = { 0.0f, 0.0f, 0.0f };
        float xyz[3], hsv[3];
        ColorModel::sRGB.toXYZ(black, xyz);
        ColorModel::HSV_sRGB.fromXYZ(xyz, hsv);
        CHECK(hsv[2] == doctest::Approx(0.0f).epsilon(1e-3)); // V=0
}

TEST_CASE("ColorModel: white through HSV") {
        float white[3] = { 1.0f, 1.0f, 1.0f };
        float xyz[3], hsv[3];
        ColorModel::sRGB.toXYZ(white, xyz);
        ColorModel::HSV_sRGB.fromXYZ(xyz, hsv);
        CHECK(hsv[1] == doctest::Approx(0.0f).epsilon(1e-3)); // S=0
        CHECK(hsv[2] == doctest::Approx(1.0f).epsilon(1e-3)); // V=1
}

TEST_CASE("ColorModel: gray through YCbCr") {
        float gray[3] = { 0.5f, 0.5f, 0.5f };
        float xyz[3], ycbcr[3];
        ColorModel::sRGB.toXYZ(gray, xyz);
        ColorModel::YCbCr_Rec709.fromXYZ(xyz, ycbcr);
        // Gray: Cb and Cr should be at 0.5 (centered)
        CHECK(ycbcr[1] == doctest::Approx(0.5f).epsilon(1e-2));
        CHECK(ycbcr[2] == doctest::Approx(0.5f).epsilon(1e-2));
}

TEST_CASE("ColorModel: HSV pure blue") {
        float blue_hsv[3] = { 240.0f / 360.0f, 1.0f, 1.0f };
        float xyz[3], rgb[3];
        ColorModel::HSV_sRGB.toXYZ(blue_hsv, xyz);
        ColorModel::sRGB.fromXYZ(xyz, rgb);
        CHECK(rgb[0] == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(rgb[1] == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(rgb[2] == doctest::Approx(1.0f).epsilon(1e-2));
}

TEST_CASE("ColorModel: CIEXYZ identity") {
        float src[3] = { 0.4f, 0.3f, 0.2f };
        float dst[3];
        ColorModel::CIEXYZ.toXYZ(src, dst);
        CHECK(dst[0] == doctest::Approx(src[0]));
        CHECK(dst[1] == doctest::Approx(src[1]));
        CHECK(dst[2] == doctest::Approx(src[2]));
        ColorModel::CIEXYZ.fromXYZ(src, dst);
        CHECK(dst[0] == doctest::Approx(src[0]));
        CHECK(dst[1] == doctest::Approx(src[1]));
        CHECK(dst[2] == doctest::Approx(src[2]));
}
