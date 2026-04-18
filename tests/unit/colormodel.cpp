/**
 * @file      colormodel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/colormodel.h>

using namespace promeki;

TEST_CASE("ColorModel: Invalid model") {
        CHECK_FALSE(ColorModel(ColorModel::Invalid).isValid());
        CHECK(ColorModel(ColorModel::Invalid).type() == ColorModel::TypeInvalid);
}

TEST_CASE("ColorModel: id() returns the correct ID") {
        CHECK(ColorModel(ColorModel::Invalid).id() == ColorModel::Invalid);
        CHECK(ColorModel(ColorModel::sRGB).id() == ColorModel::sRGB);
        CHECK(ColorModel(ColorModel::Rec709).id() == ColorModel::Rec709);
        CHECK(ColorModel(ColorModel::HSV_sRGB).id() == ColorModel::HSV_sRGB);
        CHECK(ColorModel(ColorModel::YCbCr_Rec2020).id() == ColorModel::YCbCr_Rec2020);
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

TEST_CASE("ColorModel: toH273() maps well-known models") {
        // SDR RGB family.
        CHECK(ColorModel::toH273(ColorModel::sRGB).primaries       == 1);
        CHECK(ColorModel::toH273(ColorModel::sRGB).transfer        == 13);
        CHECK(ColorModel::toH273(ColorModel::sRGB).matrix          == 0);

        CHECK(ColorModel::toH273(ColorModel::Rec709).primaries     == 1);
        CHECK(ColorModel::toH273(ColorModel::Rec709).transfer      == 1);

        CHECK(ColorModel::toH273(ColorModel::Rec2020).primaries    == 9);
        CHECK(ColorModel::toH273(ColorModel::Rec2020).transfer     == 14);

        CHECK(ColorModel::toH273(ColorModel::Rec601_PAL).primaries == 5);
        CHECK(ColorModel::toH273(ColorModel::Rec601_NTSC).primaries == 6);
        CHECK(ColorModel::toH273(ColorModel::DCI_P3).primaries      == 12);

        // Linear variants substitute transfer=8.
        CHECK(ColorModel::toH273(ColorModel::LinearRec709).transfer == 8);
        CHECK(ColorModel::toH273(ColorModel::LinearRec2020).transfer == 8);

        // YCbCr derivations stamp the matching matrix coefficients.
        CHECK(ColorModel::toH273(ColorModel::YCbCr_Rec709).matrix   == 1);
        CHECK(ColorModel::toH273(ColorModel::YCbCr_Rec2020).matrix  == 9);
        CHECK(ColorModel::toH273(ColorModel::YCbCr_Rec601).matrix   == 6);

        // Non-addressable models fall back to all-zero.
        CHECK(ColorModel::toH273(ColorModel::CIEXYZ).primaries      == 0);
        CHECK(ColorModel::toH273(ColorModel::Invalid).primaries     == 0);
}

TEST_CASE("ColorModel: sRGB and Rec709 share primaries") {
        auto &sp = ColorModel(ColorModel::sRGB).primaries();
        auto &rp = ColorModel(ColorModel::Rec709).primaries();
        for(int i = 0; i < 4; ++i) {
                CHECK(sp[i].data()[0] == doctest::Approx(rp[i].data()[0]));
                CHECK(sp[i].data()[1] == doctest::Approx(rp[i].data()[1]));
        }
}

TEST_CASE("ColorModel: sRGB transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::sRGB).applyTransfer(v);
                double decoded = ColorModel(ColorModel::sRGB).removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: Rec709 transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::Rec709).applyTransfer(v);
                double decoded = ColorModel(ColorModel::Rec709).removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: sRGB toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: LinearSRGB toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.2f, 0.7f, 0.4f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::LinearSRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::LinearSRGB).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: sRGB white maps to D65 XYZ") {
        float white[3] = { 1.0f, 1.0f, 1.0f };
        float xyz[3];
        ColorModel(ColorModel::LinearSRGB).toXYZ(white, xyz);
        // D65 white point: X=0.95047, Y=1.0, Z=1.08883
        CHECK(xyz[0] == doctest::Approx(0.95047f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(1.0f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(1.08883f).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB red produces expected XYZ") {
        float red[3] = { 1.0f, 0.0f, 0.0f };
        float xyz[3];
        ColorModel(ColorModel::LinearSRGB).toXYZ(red, xyz);
        // sRGB red primary: X~0.4124, Y~0.2126, Z~0.0193
        CHECK(xyz[0] == doctest::Approx(0.4124f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(0.2126f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(0.0193f).epsilon(1e-3));
}

TEST_CASE("ColorModel: Cross-space conversion sRGB -> Rec601_PAL -> sRGB") {
        float src[3] = { 0.8f, 0.2f, 0.5f };
        float xyz[3], dst601[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::Rec601_PAL).fromXYZ(xyz, dst601);
        ColorModel(ColorModel::Rec601_PAL).toXYZ(dst601, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: HSV sRGB roundtrip through XYZ") {
        // Pure red in HSV: H=0, S=1, V=1 (normalized: 0, 1, 1)
        float hsv[3] = { 0.0f, 1.0f, 1.0f };
        float xyz[3], hsv2[3];
        ColorModel(ColorModel::HSV_sRGB).toXYZ(hsv, xyz);
        ColorModel(ColorModel::HSV_sRGB).fromXYZ(xyz, hsv2);
        CHECK(hsv2[0] == doctest::Approx(hsv[0]).epsilon(1e-3));
        CHECK(hsv2[1] == doctest::Approx(hsv[1]).epsilon(1e-3));
        CHECK(hsv2[2] == doctest::Approx(hsv[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: HSV pure green") {
        // Green in HSV: H=120/360 = 0.333..., S=1, V=1
        float hsv[3] = { 120.0f / 360.0f, 1.0f, 1.0f };
        float xyz[3];
        ColorModel(ColorModel::HSV_sRGB).toXYZ(hsv, xyz);
        // Then convert back via sRGB to check
        float rgb[3];
        ColorModel(ColorModel::sRGB).fromXYZ(xyz, rgb);
        CHECK(rgb[0] == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(rgb[1] == doctest::Approx(1.0f).epsilon(1e-2));
        CHECK(rgb[2] == doctest::Approx(0.0f).epsilon(1e-2));
}

TEST_CASE("ColorModel: HSL roundtrip through XYZ") {
        float hsl[3] = { 0.6f, 0.8f, 0.5f };
        float xyz[3], hsl2[3];
        ColorModel(ColorModel::HSL_sRGB).toXYZ(hsl, xyz);
        ColorModel(ColorModel::HSL_sRGB).fromXYZ(xyz, hsl2);
        CHECK(hsl2[0] == doctest::Approx(hsl[0]).epsilon(1e-3));
        CHECK(hsl2[1] == doctest::Approx(hsl[1]).epsilon(1e-3));
        CHECK(hsl2[2] == doctest::Approx(hsl[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: CIE Lab roundtrip through XYZ") {
        // Mid-gray in Lab: L=50 -> normalized 0.5, a=0 -> normalized ~0.502, b=0 -> normalized ~0.502
        float lab[3] = { 0.5f, 128.0f / 255.0f, 128.0f / 255.0f };
        float xyz[3], lab2[3];
        ColorModel(ColorModel::CIELab).toXYZ(lab, xyz);
        ColorModel(ColorModel::CIELab).fromXYZ(xyz, lab2);
        CHECK(lab2[0] == doctest::Approx(lab[0]).epsilon(1e-4));
        CHECK(lab2[1] == doctest::Approx(lab[1]).epsilon(1e-4));
        CHECK(lab2[2] == doctest::Approx(lab[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: YCbCr Rec709 roundtrip through XYZ") {
        // Y=0.5, Cb=0.5 (centered), Cr=0.5 (centered) = gray
        float ycbcr[3] = { 0.5f, 0.5f, 0.5f };
        float xyz[3], ycbcr2[3];
        ColorModel(ColorModel::YCbCr_Rec709).toXYZ(ycbcr, xyz);
        ColorModel(ColorModel::YCbCr_Rec709).fromXYZ(xyz, ycbcr2);
        CHECK(ycbcr2[0] == doctest::Approx(ycbcr[0]).epsilon(1e-3));
        CHECK(ycbcr2[1] == doctest::Approx(ycbcr[1]).epsilon(1e-3));
        CHECK(ycbcr2[2] == doctest::Approx(ycbcr[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: Linear counterpart") {
        CHECK(ColorModel(ColorModel::sRGB).linearCounterpart() == ColorModel::LinearSRGB);
        CHECK(ColorModel(ColorModel::LinearSRGB).linearCounterpart() == ColorModel::LinearSRGB);
        CHECK(ColorModel(ColorModel::Rec709).linearCounterpart() == ColorModel::LinearRec709);
}

TEST_CASE("ColorModel: Nonlinear counterpart") {
        CHECK(ColorModel(ColorModel::LinearSRGB).nonlinearCounterpart() == ColorModel::sRGB);
        CHECK(ColorModel(ColorModel::sRGB).nonlinearCounterpart() == ColorModel::sRGB);
}

TEST_CASE("ColorModel: Parent model") {
        CHECK(ColorModel(ColorModel::HSV_sRGB).parentModel() == ColorModel::sRGB);
        CHECK(ColorModel(ColorModel::HSL_sRGB).parentModel() == ColorModel::sRGB);
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).parentModel() == ColorModel::Rec709);
        CHECK_FALSE(ColorModel(ColorModel::sRGB).parentModel().isValid());
}

TEST_CASE("ColorModel: Lookup by name") {
        CHECK(ColorModel::lookup("sRGB") == ColorModel::sRGB);
        CHECK(ColorModel::lookup("Rec709") == ColorModel::Rec709);
        CHECK(ColorModel::lookup("HSV_sRGB") == ColorModel::HSV_sRGB);
        CHECK(ColorModel::lookup("nonexistent") == ColorModel::Invalid);
}

TEST_CASE("ColorModel: toNative and fromNative") {
        // HSV hue: normalized 0.5 -> native 180 degrees
        CHECK(ColorModel(ColorModel::HSV_sRGB).toNative(0, 0.5f) == doctest::Approx(180.0f));
        CHECK(ColorModel(ColorModel::HSV_sRGB).fromNative(0, 180.0f) == doctest::Approx(0.5f));

        // Lab L: normalized 0.5 -> native 50
        CHECK(ColorModel(ColorModel::CIELab).toNative(0, 0.5f) == doctest::Approx(50.0f));
        CHECK(ColorModel(ColorModel::CIELab).fromNative(0, 50.0f) == doctest::Approx(0.5f));
}

TEST_CASE("ColorModel: Rec2020 properties") {
        CHECK(ColorModel(ColorModel::Rec2020).isValid());
        CHECK(ColorModel(ColorModel::Rec2020).type() == ColorModel::TypeRGB);
        CHECK_FALSE(ColorModel(ColorModel::Rec2020).isLinear());
        CHECK(ColorModel(ColorModel::LinearRec2020).isLinear());
}

TEST_CASE("ColorModel: DCI_P3 properties") {
        CHECK(ColorModel(ColorModel::DCI_P3).isValid());
        CHECK(ColorModel(ColorModel::DCI_P3).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::DCI_P3).name() == "DCI_P3");
        CHECK_FALSE(ColorModel(ColorModel::DCI_P3).isLinear());
        CHECK(ColorModel(ColorModel::LinearDCI_P3).isLinear());
        CHECK(ColorModel(ColorModel::DCI_P3).linearCounterpart() == ColorModel::LinearDCI_P3);
        CHECK(ColorModel(ColorModel::LinearDCI_P3).nonlinearCounterpart() == ColorModel::DCI_P3);
}

TEST_CASE("ColorModel: DCI_P3 transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::DCI_P3).applyTransfer(v);
                double decoded = ColorModel(ColorModel::DCI_P3).removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: DCI_P3 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::DCI_P3).toXYZ(src, xyz);
        ColorModel(ColorModel::DCI_P3).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: sRGB -> DCI_P3 -> sRGB roundtrip") {
        float src[3] = { 0.8f, 0.2f, 0.5f };
        float xyz[3], p3[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::DCI_P3).fromXYZ(xyz, p3);
        ColorModel(ColorModel::DCI_P3).toXYZ(p3, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: DCI_P3 uses D65 white point") {
        CIEPoint wp = ColorModel(ColorModel::DCI_P3).whitePoint();
        CHECK(wp.x() == doctest::Approx(0.3127).epsilon(1e-4));
        CHECK(wp.y() == doctest::Approx(0.3290).epsilon(1e-4));
}

TEST_CASE("ColorModel: DCI_P3 lookup") {
        CHECK(ColorModel::lookup("DCI_P3") == ColorModel::DCI_P3);
        CHECK(ColorModel::lookup("LinearDCI_P3") == ColorModel::LinearDCI_P3);
}

// ── Adobe RGB ─────────────────────────────────────────────────────

TEST_CASE("ColorModel: AdobeRGB properties") {
        CHECK(ColorModel(ColorModel::AdobeRGB).isValid());
        CHECK(ColorModel(ColorModel::AdobeRGB).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::AdobeRGB).name() == "AdobeRGB");
        CHECK_FALSE(ColorModel(ColorModel::AdobeRGB).isLinear());
        CHECK(ColorModel(ColorModel::LinearAdobeRGB).isLinear());
        CHECK(ColorModel(ColorModel::AdobeRGB).linearCounterpart() == ColorModel::LinearAdobeRGB);
        CHECK(ColorModel(ColorModel::LinearAdobeRGB).nonlinearCounterpart() == ColorModel::AdobeRGB);
}

TEST_CASE("ColorModel: AdobeRGB transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::AdobeRGB).applyTransfer(v);
                double decoded = ColorModel(ColorModel::AdobeRGB).removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: AdobeRGB toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::AdobeRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::AdobeRGB).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: sRGB -> AdobeRGB -> sRGB roundtrip") {
        float src[3] = { 0.8f, 0.2f, 0.5f };
        float xyz[3], argb[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::AdobeRGB).fromXYZ(xyz, argb);
        ColorModel(ColorModel::AdobeRGB).toXYZ(argb, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
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
        CHECK(ColorModel(ColorModel::ACES_AP0).isValid());
        CHECK(ColorModel(ColorModel::ACES_AP0).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::ACES_AP0).name() == "ACES_AP0");
        CHECK(ColorModel(ColorModel::ACES_AP0).isLinear());
}

TEST_CASE("ColorModel: ACES AP1 properties") {
        CHECK(ColorModel(ColorModel::ACES_AP1).isValid());
        CHECK(ColorModel(ColorModel::ACES_AP1).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::ACES_AP1).name() == "ACES_AP1");
        CHECK(ColorModel(ColorModel::ACES_AP1).isLinear());
}

TEST_CASE("ColorModel: ACES uses D60 white point") {
        // ACES D60: (0.32168, 0.33767)
        CIEPoint wp = ColorModel(ColorModel::ACES_AP0).whitePoint();
        CHECK(wp.x() == doctest::Approx(0.32168).epsilon(1e-4));
        CHECK(wp.y() == doctest::Approx(0.33767).epsilon(1e-4));
        // AP1 uses the same white point
        CIEPoint wp1 = ColorModel(ColorModel::ACES_AP1).whitePoint();
        CHECK(wp1.x() == doctest::Approx(0.32168).epsilon(1e-4));
}

TEST_CASE("ColorModel: ACES AP0 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.2f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::ACES_AP0).toXYZ(src, xyz);
        ColorModel(ColorModel::ACES_AP0).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: ACES AP1 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.2f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::ACES_AP1).toXYZ(src, xyz);
        ColorModel(ColorModel::ACES_AP1).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> ACES AP0 -> sRGB roundtrip") {
        float src[3] = { 0.6f, 0.3f, 0.9f };
        float xyz[3], aces[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::ACES_AP0).fromXYZ(xyz, aces);
        ColorModel(ColorModel::ACES_AP0).toXYZ(aces, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
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
        CHECK(ColorModel(ColorModel::YCbCr_Rec2020).isValid());
        CHECK(ColorModel(ColorModel::YCbCr_Rec2020).type() == ColorModel::TypeYCbCr);
        CHECK(ColorModel(ColorModel::YCbCr_Rec2020).name() == "YCbCr_Rec2020");
        CHECK(ColorModel(ColorModel::YCbCr_Rec2020).parentModel() == ColorModel::Rec2020);
}

TEST_CASE("ColorModel: YCbCr_Rec2020 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.55f, 0.45f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::YCbCr_Rec2020).toXYZ(src, xyz);
        ColorModel(ColorModel::YCbCr_Rec2020).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> YCbCr_Rec2020 -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], ycbcr[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::YCbCr_Rec2020).fromXYZ(xyz, ycbcr);
        ColorModel(ColorModel::YCbCr_Rec2020).toXYZ(ycbcr, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: YCbCr_Rec2020 gray has centered chroma") {
        // Gray in Rec2020 -> YCbCr should give Cb=0.5, Cr=0.5
        float gray[3] = { 0.5f, 0.5f, 0.5f };
        float xyz[3], ycbcr[3];
        ColorModel(ColorModel::Rec2020).toXYZ(gray, xyz);
        ColorModel(ColorModel::YCbCr_Rec2020).fromXYZ(xyz, ycbcr);
        CHECK(ycbcr[1] == doctest::Approx(0.5f).epsilon(1e-2));
        CHECK(ycbcr[2] == doctest::Approx(0.5f).epsilon(1e-2));
}

TEST_CASE("ColorModel: YCbCr_Rec2020 lookup") {
        CHECK(ColorModel::lookup("YCbCr_Rec2020") == ColorModel::YCbCr_Rec2020);
}

TEST_CASE("ColorModel: Rec2020 transfer function roundtrip") {
        double values[] = { 0.0, 0.01, 0.1, 0.5, 0.9, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::Rec2020).applyTransfer(v);
                double decoded = ColorModel(ColorModel::Rec2020).removeTransfer(encoded);
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
        CHECK(ColorModel(ColorModel::sRGB).desc() == "IEC 61966-2-1 sRGB");
        CHECK(ColorModel(ColorModel::Invalid).desc() == "Invalid color model");
}

TEST_CASE("ColorModel: whitePoint") {
        CIEPoint wp = ColorModel(ColorModel::sRGB).whitePoint();
        CHECK(wp.x() == doctest::Approx(0.3127).epsilon(1e-4));
        CHECK(wp.y() == doctest::Approx(0.3290).epsilon(1e-4));
        // Rec2020 also uses D65
        CIEPoint wp2 = ColorModel(ColorModel::Rec2020).whitePoint();
        CHECK(wp2.x() == doctest::Approx(0.3127).epsilon(1e-4));
        CHECK(wp2.y() == doctest::Approx(0.3290).epsilon(1e-4));
}

TEST_CASE("ColorModel: all well-known instances are valid") {
        CHECK(ColorModel(ColorModel::sRGB).isValid());
        CHECK(ColorModel(ColorModel::LinearSRGB).isValid());
        CHECK(ColorModel(ColorModel::Rec709).isValid());
        CHECK(ColorModel(ColorModel::LinearRec709).isValid());
        CHECK(ColorModel(ColorModel::Rec601_PAL).isValid());
        CHECK(ColorModel(ColorModel::LinearRec601_PAL).isValid());
        CHECK(ColorModel(ColorModel::Rec601_NTSC).isValid());
        CHECK(ColorModel(ColorModel::LinearRec601_NTSC).isValid());
        CHECK(ColorModel(ColorModel::Rec2020).isValid());
        CHECK(ColorModel(ColorModel::LinearRec2020).isValid());
        CHECK(ColorModel(ColorModel::DCI_P3).isValid());
        CHECK(ColorModel(ColorModel::LinearDCI_P3).isValid());
        CHECK(ColorModel(ColorModel::CIEXYZ).isValid());
        CHECK(ColorModel(ColorModel::CIELab).isValid());
        CHECK(ColorModel(ColorModel::HSV_sRGB).isValid());
        CHECK(ColorModel(ColorModel::HSL_sRGB).isValid());
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).isValid());
        CHECK(ColorModel(ColorModel::YCbCr_Rec601).isValid());
}

TEST_CASE("ColorModel: type for each model") {
        CHECK(ColorModel(ColorModel::Rec709).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::LinearRec709).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::Rec601_PAL).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::Rec601_NTSC).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::Rec2020).type() == ColorModel::TypeRGB);
        CHECK(ColorModel(ColorModel::CIEXYZ).type() == ColorModel::TypeXYZ);
        CHECK(ColorModel(ColorModel::CIELab).type() == ColorModel::TypeLab);
        CHECK(ColorModel(ColorModel::HSV_sRGB).type() == ColorModel::TypeHSV);
        CHECK(ColorModel(ColorModel::HSL_sRGB).type() == ColorModel::TypeHSL);
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).type() == ColorModel::TypeYCbCr);
        CHECK(ColorModel(ColorModel::YCbCr_Rec601).type() == ColorModel::TypeYCbCr);
}

TEST_CASE("ColorModel: name for each model") {
        CHECK(ColorModel(ColorModel::Rec709).name() == "Rec709");
        CHECK(ColorModel(ColorModel::LinearRec709).name() == "LinearRec709");
        CHECK(ColorModel(ColorModel::Rec601_PAL).name() == "Rec601_PAL");
        CHECK(ColorModel(ColorModel::Rec601_NTSC).name() == "Rec601_NTSC");
        CHECK(ColorModel(ColorModel::Rec2020).name() == "Rec2020");
        CHECK(ColorModel(ColorModel::CIEXYZ).name() == "CIEXYZ");
        CHECK(ColorModel(ColorModel::CIELab).name() == "CIELab");
        CHECK(ColorModel(ColorModel::HSV_sRGB).name() == "HSV_sRGB");
        CHECK(ColorModel(ColorModel::HSL_sRGB).name() == "HSL_sRGB");
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).name() == "YCbCr_Rec709");
        CHECK(ColorModel(ColorModel::YCbCr_Rec601).name() == "YCbCr_Rec601");
}

TEST_CASE("ColorModel: compCount is always 3") {
        CHECK(ColorModel(ColorModel::CIEXYZ).compCount() == 3);
        CHECK(ColorModel(ColorModel::CIELab).compCount() == 3);
        CHECK(ColorModel(ColorModel::HSV_sRGB).compCount() == 3);
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compCount() == 3);
}

// ── CompInfo ──────────────────────────────────────────────────────

TEST_CASE("ColorModel: sRGB CompInfo full details") {
        CHECK(ColorModel(ColorModel::sRGB).compInfo(0).name == "Red");
        CHECK(ColorModel(ColorModel::sRGB).compInfo(0).nativeMin == 0.0f);
        CHECK(ColorModel(ColorModel::sRGB).compInfo(0).nativeMax == 1.0f);
        CHECK(ColorModel(ColorModel::sRGB).compInfo(1).name == "Green");
        CHECK(ColorModel(ColorModel::sRGB).compInfo(2).name == "Blue");
}

TEST_CASE("ColorModel: HSV CompInfo") {
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(0).name == "Hue");
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(0).abbrev == "H");
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(0).nativeMin == 0.0f);
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(0).nativeMax == 360.0f);
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(1).name == "Saturation");
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(1).nativeMax == 1.0f);
        CHECK(ColorModel(ColorModel::HSV_sRGB).compInfo(2).name == "Value");
}

TEST_CASE("ColorModel: HSL CompInfo") {
        CHECK(ColorModel(ColorModel::HSL_sRGB).compInfo(0).abbrev == "H");
        CHECK(ColorModel(ColorModel::HSL_sRGB).compInfo(0).nativeMax == 360.0f);
        CHECK(ColorModel(ColorModel::HSL_sRGB).compInfo(2).name == "Lightness");
        CHECK(ColorModel(ColorModel::HSL_sRGB).compInfo(2).abbrev == "L");
}

TEST_CASE("ColorModel: Lab CompInfo") {
        CHECK(ColorModel(ColorModel::CIELab).compInfo(0).name == "Lightness");
        CHECK(ColorModel(ColorModel::CIELab).compInfo(0).nativeMin == 0.0f);
        CHECK(ColorModel(ColorModel::CIELab).compInfo(0).nativeMax == 100.0f);
        CHECK(ColorModel(ColorModel::CIELab).compInfo(1).name == "a");
        CHECK(ColorModel(ColorModel::CIELab).compInfo(1).nativeMin == -128.0f);
        CHECK(ColorModel(ColorModel::CIELab).compInfo(1).nativeMax == 127.0f);
        CHECK(ColorModel(ColorModel::CIELab).compInfo(2).name == "b");
        CHECK(ColorModel(ColorModel::CIELab).compInfo(2).nativeMin == -128.0f);
}

TEST_CASE("ColorModel: YCbCr CompInfo") {
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compInfo(0).name == "Luma");
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compInfo(0).abbrev == "Y");
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compInfo(1).abbrev == "Cb");
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compInfo(1).nativeMin == -0.5f);
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compInfo(1).nativeMax == 0.5f);
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).compInfo(2).abbrev == "Cr");
}

TEST_CASE("ColorModel: XYZ CompInfo") {
        CHECK(ColorModel(ColorModel::CIEXYZ).compInfo(0).abbrev == "X");
        CHECK(ColorModel(ColorModel::CIEXYZ).compInfo(1).abbrev == "Y");
        CHECK(ColorModel(ColorModel::CIEXYZ).compInfo(2).abbrev == "Z");
}

TEST_CASE("ColorModel: compInfo out-of-range index falls back to index 0") {
        CHECK(ColorModel(ColorModel::sRGB).compInfo(3).abbrev == "R");
        CHECK(ColorModel(ColorModel::sRGB).compInfo(99).abbrev == "R");
}

// ── Transfer functions ────────────────────────────────────────────

TEST_CASE("ColorModel: linear models have identity transfer") {
        ColorModel linears[] = {
                ColorModel::LinearSRGB, ColorModel::LinearRec709,
                ColorModel::LinearRec601_PAL, ColorModel::LinearRec601_NTSC,
                ColorModel::LinearRec2020
        };
        for(auto &m : linears) {
                CHECK(m.isLinear());
                CHECK(m.applyTransfer(0.5) == doctest::Approx(0.5));
                CHECK(m.removeTransfer(0.5) == doctest::Approx(0.5));
        }
}

TEST_CASE("ColorModel: Rec601_PAL transfer roundtrip") {
        double values[] = { 0.0, 0.01, 0.5, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::Rec601_PAL).applyTransfer(v);
                double decoded = ColorModel(ColorModel::Rec601_PAL).removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

TEST_CASE("ColorModel: Rec601_NTSC transfer roundtrip") {
        double values[] = { 0.0, 0.01, 0.5, 1.0 };
        for(double v : values) {
                double encoded = ColorModel(ColorModel::Rec601_NTSC).applyTransfer(v);
                double decoded = ColorModel(ColorModel::Rec601_NTSC).removeTransfer(encoded);
                CHECK(decoded == doctest::Approx(v).epsilon(1e-6));
        }
}

// ── toXYZ/fromXYZ roundtrips for all models ──────────────────────

TEST_CASE("ColorModel: Rec709 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::Rec709).toXYZ(src, xyz);
        ColorModel(ColorModel::Rec709).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: Rec601_PAL toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.4f, 0.6f, 0.2f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::Rec601_PAL).toXYZ(src, xyz);
        ColorModel(ColorModel::Rec601_PAL).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: Rec601_NTSC toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.7f, 0.1f, 0.9f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::Rec601_NTSC).toXYZ(src, xyz);
        ColorModel(ColorModel::Rec601_NTSC).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: Rec2020 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.3f, 0.5f, 0.7f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::Rec2020).toXYZ(src, xyz);
        ColorModel(ColorModel::Rec2020).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: LinearRec2020 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.3f, 0.5f, 0.7f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::LinearRec2020).toXYZ(src, xyz);
        ColorModel(ColorModel::LinearRec2020).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-4));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-4));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-4));
}

TEST_CASE("ColorModel: YCbCr_Rec601 toXYZ/fromXYZ roundtrip") {
        float src[3] = { 0.4f, 0.55f, 0.45f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::YCbCr_Rec601).toXYZ(src, xyz);
        ColorModel(ColorModel::YCbCr_Rec601).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: HSV non-primary roundtrip") {
        float src[3] = { 0.75f, 0.5f, 0.8f };
        float xyz[3], dst[3];
        ColorModel(ColorModel::HSV_sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::HSV_sRGB).fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

// ── Cross-space conversions ──────────────────────────────────────

TEST_CASE("ColorModel: sRGB -> Rec2020 -> sRGB roundtrip") {
        float src[3] = { 0.6f, 0.3f, 0.9f };
        float xyz[3], r2020[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::Rec2020).fromXYZ(xyz, r2020);
        ColorModel(ColorModel::Rec2020).toXYZ(r2020, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> YCbCr_Rec709 -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], ycbcr[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::YCbCr_Rec709).fromXYZ(xyz, ycbcr);
        ColorModel(ColorModel::YCbCr_Rec709).toXYZ(ycbcr, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> HSV -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], hsv[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::HSV_sRGB).fromXYZ(xyz, hsv);
        ColorModel(ColorModel::HSV_sRGB).toXYZ(hsv, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

TEST_CASE("ColorModel: sRGB -> Lab -> sRGB roundtrip") {
        float src[3] = { 0.5f, 0.3f, 0.8f };
        float xyz[3], lab[3], xyz2[3], dst[3];
        ColorModel(ColorModel::sRGB).toXYZ(src, xyz);
        ColorModel(ColorModel::CIELab).fromXYZ(xyz, lab);
        ColorModel(ColorModel::CIELab).toXYZ(lab, xyz2);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz2, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-3));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-3));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-3));
}

// ── Known XYZ reference values ───────────────────────────────────

TEST_CASE("ColorModel: linear sRGB green XYZ") {
        float green[3] = { 0.0f, 1.0f, 0.0f };
        float xyz[3];
        ColorModel(ColorModel::LinearSRGB).toXYZ(green, xyz);
        CHECK(xyz[0] == doctest::Approx(0.3576f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(0.7152f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(0.1192f).epsilon(1e-3));
}

TEST_CASE("ColorModel: linear sRGB blue XYZ") {
        float blue[3] = { 0.0f, 0.0f, 1.0f };
        float xyz[3];
        ColorModel(ColorModel::LinearSRGB).toXYZ(blue, xyz);
        CHECK(xyz[0] == doctest::Approx(0.1805f).epsilon(1e-3));
        CHECK(xyz[1] == doctest::Approx(0.0722f).epsilon(1e-3));
        CHECK(xyz[2] == doctest::Approx(0.9505f).epsilon(1e-3));
}

TEST_CASE("ColorModel: linear sRGB black XYZ is zero") {
        float black[3] = { 0.0f, 0.0f, 0.0f };
        float xyz[3];
        ColorModel(ColorModel::LinearSRGB).toXYZ(black, xyz);
        CHECK(xyz[0] == doctest::Approx(0.0f));
        CHECK(xyz[1] == doctest::Approx(0.0f));
        CHECK(xyz[2] == doctest::Approx(0.0f));
}

// ── linearCounterpart / nonlinearCounterpart completeness ────────

TEST_CASE("ColorModel: all linear/nonlinear counterpart pairs") {
        CHECK(ColorModel(ColorModel::Rec601_PAL).linearCounterpart() == ColorModel::LinearRec601_PAL);
        CHECK(ColorModel(ColorModel::LinearRec601_PAL).nonlinearCounterpart() == ColorModel::Rec601_PAL);
        CHECK(ColorModel(ColorModel::Rec601_NTSC).linearCounterpart() == ColorModel::LinearRec601_NTSC);
        CHECK(ColorModel(ColorModel::LinearRec601_NTSC).nonlinearCounterpart() == ColorModel::Rec601_NTSC);
        CHECK(ColorModel(ColorModel::Rec2020).linearCounterpart() == ColorModel::LinearRec2020);
        CHECK(ColorModel(ColorModel::LinearRec2020).nonlinearCounterpart() == ColorModel::Rec2020);
        CHECK(ColorModel(ColorModel::LinearRec709).nonlinearCounterpart() == ColorModel::Rec709);
}

TEST_CASE("ColorModel: non-RGB models return self for counterparts") {
        CHECK(ColorModel(ColorModel::CIEXYZ).linearCounterpart() == ColorModel::CIEXYZ);
        CHECK(ColorModel(ColorModel::CIEXYZ).nonlinearCounterpart() == ColorModel::CIEXYZ);
        CHECK(ColorModel(ColorModel::CIELab).linearCounterpart() == ColorModel::CIELab);
        CHECK(ColorModel(ColorModel::HSV_sRGB).linearCounterpart() == ColorModel::HSV_sRGB);
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).linearCounterpart() == ColorModel::YCbCr_Rec709);
}

// ── parentModel completeness ─────────────────────────────────────

TEST_CASE("ColorModel: parentModel for all types") {
        CHECK(ColorModel(ColorModel::YCbCr_Rec601).parentModel() == ColorModel::Rec601_PAL);
        CHECK_FALSE(ColorModel(ColorModel::LinearSRGB).parentModel().isValid());
        CHECK_FALSE(ColorModel(ColorModel::Rec709).parentModel().isValid());
        CHECK_FALSE(ColorModel(ColorModel::Rec2020).parentModel().isValid());
        CHECK_FALSE(ColorModel(ColorModel::CIEXYZ).parentModel().isValid());
        CHECK_FALSE(ColorModel(ColorModel::CIELab).parentModel().isValid());
        CHECK_FALSE(ColorModel(ColorModel::Invalid).parentModel().isValid());
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
        CHECK(ColorModel(ColorModel::HSV_sRGB).toNative(0, 0.0f) == doctest::Approx(0.0f));
        CHECK(ColorModel(ColorModel::HSV_sRGB).toNative(0, 1.0f) == doctest::Approx(360.0f));
        CHECK(ColorModel(ColorModel::CIELab).toNative(0, 0.0f) == doctest::Approx(0.0f));
        CHECK(ColorModel(ColorModel::CIELab).toNative(0, 1.0f) == doctest::Approx(100.0f));
        CHECK(ColorModel(ColorModel::CIELab).toNative(1, 0.0f) == doctest::Approx(-128.0f));
        CHECK(ColorModel(ColorModel::CIELab).toNative(1, 1.0f) == doctest::Approx(127.0f));
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).toNative(1, 0.0f) == doctest::Approx(-0.5f));
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).toNative(1, 1.0f) == doctest::Approx(0.5f));
}

TEST_CASE("ColorModel: toNative/fromNative out-of-range comp") {
        CHECK(ColorModel(ColorModel::sRGB).toNative(3, 0.5f) == 0.0f);
        CHECK(ColorModel(ColorModel::sRGB).fromNative(3, 0.5f) == 0.0f);
}

TEST_CASE("ColorModel: RGB toNative is identity") {
        CHECK(ColorModel(ColorModel::sRGB).toNative(0, 0.5f) == doctest::Approx(0.5f));
        CHECK(ColorModel(ColorModel::sRGB).fromNative(0, 0.5f) == doctest::Approx(0.5f));
}

// ── Edge-case colors through model spaces ────────────────────────

TEST_CASE("ColorModel: black through HSV") {
        float black[3] = { 0.0f, 0.0f, 0.0f };
        float xyz[3], hsv[3];
        ColorModel(ColorModel::sRGB).toXYZ(black, xyz);
        ColorModel(ColorModel::HSV_sRGB).fromXYZ(xyz, hsv);
        CHECK(hsv[2] == doctest::Approx(0.0f).epsilon(1e-3)); // V=0
}

TEST_CASE("ColorModel: white through HSV") {
        float white[3] = { 1.0f, 1.0f, 1.0f };
        float xyz[3], hsv[3];
        ColorModel(ColorModel::sRGB).toXYZ(white, xyz);
        ColorModel(ColorModel::HSV_sRGB).fromXYZ(xyz, hsv);
        CHECK(hsv[1] == doctest::Approx(0.0f).epsilon(1e-3)); // S=0
        CHECK(hsv[2] == doctest::Approx(1.0f).epsilon(1e-3)); // V=1
}

TEST_CASE("ColorModel: gray through YCbCr") {
        float gray[3] = { 0.5f, 0.5f, 0.5f };
        float xyz[3], ycbcr[3];
        ColorModel(ColorModel::sRGB).toXYZ(gray, xyz);
        ColorModel(ColorModel::YCbCr_Rec709).fromXYZ(xyz, ycbcr);
        // Gray: Cb and Cr should be at 0.5 (centered)
        CHECK(ycbcr[1] == doctest::Approx(0.5f).epsilon(1e-2));
        CHECK(ycbcr[2] == doctest::Approx(0.5f).epsilon(1e-2));
}

TEST_CASE("ColorModel: HSV pure blue") {
        float blue_hsv[3] = { 240.0f / 360.0f, 1.0f, 1.0f };
        float xyz[3], rgb[3];
        ColorModel(ColorModel::HSV_sRGB).toXYZ(blue_hsv, xyz);
        ColorModel(ColorModel::sRGB).fromXYZ(xyz, rgb);
        CHECK(rgb[0] == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(rgb[1] == doctest::Approx(0.0f).epsilon(1e-2));
        CHECK(rgb[2] == doctest::Approx(1.0f).epsilon(1e-2));
}

TEST_CASE("ColorModel: CIEXYZ identity") {
        float src[3] = { 0.4f, 0.3f, 0.2f };
        float dst[3];
        ColorModel(ColorModel::CIEXYZ).toXYZ(src, dst);
        CHECK(dst[0] == doctest::Approx(src[0]));
        CHECK(dst[1] == doctest::Approx(src[1]));
        CHECK(dst[2] == doctest::Approx(src[2]));
        ColorModel(ColorModel::CIEXYZ).fromXYZ(src, dst);
        CHECK(dst[0] == doctest::Approx(src[0]));
        CHECK(dst[1] == doctest::Approx(src[1]));
        CHECK(dst[2] == doctest::Approx(src[2]));
}

// ── TypeRegistry: registerType() / registerData() ─────────────────

TEST_CASE("ColorModel: registerType returns unique IDs above UserDefined") {
        ColorModel::ID id1 = ColorModel::registerType();
        ColorModel::ID id2 = ColorModel::registerType();
        CHECK(id1 >= ColorModel::UserDefined);
        CHECK(id2 >= ColorModel::UserDefined);
        CHECK(id1 != id2);
}

TEST_CASE("ColorModel: registerData and construction from custom ID") {
        ColorModel::ID id = ColorModel::registerType();

        ColorModel::Data d;
        d.id   = id;
        d.type = ColorModel::TypeRGB;
        d.name = "TestModel";
        d.desc = "A synthetic color model for unit testing.";
        d.comps[0] = { "Red",   "R", 0.0f, 1.0f };
        d.comps[1] = { "Green", "G", 0.0f, 1.0f };
        d.comps[2] = { "Blue",  "B", 0.0f, 1.0f };
        d.linear   = true;
        d.linearCounterpart    = id;
        d.nonlinearCounterpart = id;
        d.parentModel          = ColorModel::Invalid;
        // Identity transfer functions
        d.oetf = [](double v) { return v; };
        d.eotf = [](double v) { return v; };
        // Identity XYZ conversion (treat our model as CIE XYZ for simplicity)
        d.toXYZFunc   = [](const ColorModel::Data *, const float *src, float *dst) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        };
        d.fromXYZFunc = [](const ColorModel::Data *, const float *src, float *dst) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        };

        ColorModel::registerData(std::move(d));

        ColorModel cm(id);
        CHECK(cm.isValid());
        CHECK(cm.id() == id);
        CHECK(cm.type() == ColorModel::TypeRGB);
        CHECK(cm.name() == "TestModel");
        CHECK(cm.isLinear());
        CHECK(cm.compInfo(0).abbrev == "R");
        CHECK(cm.compInfo(1).abbrev == "G");
        CHECK(cm.compInfo(2).abbrev == "B");
}

TEST_CASE("ColorModel: custom model toXYZ/fromXYZ roundtrip") {
        ColorModel::ID id = ColorModel::registerType();

        ColorModel::Data d;
        d.id   = id;
        d.type = ColorModel::TypeRGB;
        d.name = "TestModelRT";
        d.comps[0] = { "X", "X", 0.0f, 1.0f };
        d.comps[1] = { "Y", "Y", 0.0f, 1.0f };
        d.comps[2] = { "Z", "Z", 0.0f, 1.0f };
        d.linear   = true;
        d.linearCounterpart    = id;
        d.nonlinearCounterpart = id;
        d.parentModel          = ColorModel::Invalid;
        d.oetf = [](double v) { return v; };
        d.eotf = [](double v) { return v; };
        d.toXYZFunc   = [](const ColorModel::Data *, const float *src, float *dst) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        };
        d.fromXYZFunc = [](const ColorModel::Data *, const float *src, float *dst) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        };

        ColorModel::registerData(std::move(d));

        ColorModel cm(id);
        float src[3] = { 0.3f, 0.6f, 0.9f };
        float xyz[3], dst[3];
        cm.toXYZ(src, xyz);
        cm.fromXYZ(xyz, dst);
        CHECK(dst[0] == doctest::Approx(src[0]).epsilon(1e-6f));
        CHECK(dst[1] == doctest::Approx(src[1]).epsilon(1e-6f));
        CHECK(dst[2] == doctest::Approx(src[2]).epsilon(1e-6f));
}
