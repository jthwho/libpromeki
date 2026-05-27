/**
 * @file      sdistandards.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums_video.h>
#include <promeki/sdistandards.h>

using namespace promeki;

// ============================================================================
// sdiCableCount
// ============================================================================

TEST_CASE("sdiCableCount: maps every SdiLinkStandard to its physical cable count") {
        CHECK(sdiCableCount(SdiLinkStandard::Auto)      == 0);
        CHECK(sdiCableCount(SdiLinkStandard::SL_SD)     == 1);
        CHECK(sdiCableCount(SdiLinkStandard::SL_HD)     == 1);
        CHECK(sdiCableCount(SdiLinkStandard::DL_HD)     == 2);
        CHECK(sdiCableCount(SdiLinkStandard::SL_3GA)    == 1);
        CHECK(sdiCableCount(SdiLinkStandard::SL_3GB)    == 1);
        CHECK(sdiCableCount(SdiLinkStandard::DL_3GB)    == 2);
        CHECK(sdiCableCount(SdiLinkStandard::DL_3G)     == 2);
        CHECK(sdiCableCount(SdiLinkStandard::QL_3G_SQD) == 4);
        CHECK(sdiCableCount(SdiLinkStandard::QL_3G_2SI) == 4);
        CHECK(sdiCableCount(SdiLinkStandard::SL_6G)     == 1);
        CHECK(sdiCableCount(SdiLinkStandard::SL_12G)    == 1);
        CHECK(sdiCableCount(SdiLinkStandard::SL_24G)    == 1);
}

// ============================================================================
// sdiIsDualLink
// ============================================================================

TEST_CASE("sdiIsDualLink: identifies all dual-link variants") {
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::Auto));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::SL_SD));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::SL_HD));
        CHECK(sdiIsDualLink(SdiLinkStandard::DL_HD));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::SL_3GA));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::SL_3GB));
        CHECK(sdiIsDualLink(SdiLinkStandard::DL_3GB));
        CHECK(sdiIsDualLink(SdiLinkStandard::DL_3G));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::QL_3G_SQD));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::QL_3G_2SI));
        CHECK_FALSE(sdiIsDualLink(SdiLinkStandard::SL_12G));
}

// ============================================================================
// sdiIsQuadLink
// ============================================================================

TEST_CASE("sdiIsQuadLink: identifies both quad-link mappings") {
        CHECK_FALSE(sdiIsQuadLink(SdiLinkStandard::Auto));
        CHECK_FALSE(sdiIsQuadLink(SdiLinkStandard::SL_3GA));
        CHECK_FALSE(sdiIsQuadLink(SdiLinkStandard::DL_3G));
        CHECK(sdiIsQuadLink(SdiLinkStandard::QL_3G_SQD));
        CHECK(sdiIsQuadLink(SdiLinkStandard::QL_3G_2SI));
        CHECK_FALSE(sdiIsQuadLink(SdiLinkStandard::SL_12G));
}

// ============================================================================
// sdiNominalDataRateGbps
// ============================================================================

TEST_CASE("sdiNominalDataRateGbps: matches the published spec rates") {
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::Auto)      == doctest::Approx(0.0));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_SD)     == doctest::Approx(0.270));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_HD)     == doctest::Approx(1.485));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::DL_HD)     == doctest::Approx(2.970));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_3GA)    == doctest::Approx(2.970));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_3GB)    == doctest::Approx(2.970));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::DL_3GB)    == doctest::Approx(2.970));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::DL_3G)     == doctest::Approx(5.940));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::QL_3G_SQD) == doctest::Approx(11.880));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::QL_3G_2SI) == doctest::Approx(11.880));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_6G)     == doctest::Approx(5.940));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_12G)    == doctest::Approx(11.880));
        CHECK(sdiNominalDataRateGbps(SdiLinkStandard::SL_24G)    == doctest::Approx(23.760));
}

// ============================================================================
// sdiBitsPerPixel
// ============================================================================

TEST_CASE("sdiBitsPerPixel: returns intrinsic per-pixel bit counts for every wire format") {
        CHECK(sdiBitsPerPixel(SdiWireFormat::Auto)         == 0);
        CHECK(sdiBitsPerPixel(SdiWireFormat::YCbCr_422_10) == 20);
        CHECK(sdiBitsPerPixel(SdiWireFormat::YCbCr_422_12) == 24);
        CHECK(sdiBitsPerPixel(SdiWireFormat::YCbCr_444_10) == 30);
        CHECK(sdiBitsPerPixel(SdiWireFormat::YCbCr_444_12) == 36);
        CHECK(sdiBitsPerPixel(SdiWireFormat::RGB_444_10)   == 30);
        CHECK(sdiBitsPerPixel(SdiWireFormat::RGB_444_12)   == 36);
        CHECK(sdiBitsPerPixel(SdiWireFormat::RGBA_444_10)  == 40);
}
