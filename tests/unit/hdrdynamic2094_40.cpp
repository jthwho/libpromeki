/**
 * @file      hdrdynamic2094_40.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/json.h>
#include <promeki/result.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // A non-trivial single-window descriptor exercising every
        // structural branch: distribution percentiles, tone-mapping
        // Bezier curve, colour-saturation weight, and the targeted-display
        // actual-peak grid.
        HdrDynamic2094_40 fullSingleWindow() {
                HdrDynamic2094_40 md;
                md.setApplicationVersion(1);
                md.setNumWindows(1);
                md.setTargetedSystemDisplayMaximumLuminance(4'000'0000u); // 4000 cd/m^2 * 10000
                // Targeted-display actual peak grid (2x3 = 6 cells)
                auto &tgrid = md.targetedSystemDisplayActualPeakLuminance();
                tgrid.numRows = 2;
                tgrid.numCols = 3;
                tgrid.values = {1, 2, 3, 4, 5, 15};

                auto &wp = md.windowProcessing()[0];
                wp.maxScl[0] = 100000;
                wp.maxScl[1] = 95000;
                wp.maxScl[2] = 90000;
                wp.averageMaxRgb = 50000;
                wp.distribution = {
                        HdrDynamic2094_40::DistributionMaxRgb{ 1u, 12000u },
                        HdrDynamic2094_40::DistributionMaxRgb{ 5u, 25000u },
                        HdrDynamic2094_40::DistributionMaxRgb{50u, 45000u },
                        HdrDynamic2094_40::DistributionMaxRgb{99u, 95000u },
                };
                wp.fractionBrightPixels = 256;

                wp.hasToneMapping = true;
                wp.toneMapping.kneePointX = 1024;
                wp.toneMapping.kneePointY = 2048;
                wp.toneMapping.bezierCurveAnchors = {64, 128, 192, 256, 320, 384};

                wp.hasColorSaturationMapping = true;
                wp.colorSaturationWeight = 42;
                return md;
        }

        // A three-window descriptor with extra-window geometry on
        // windows 1 and 2, no optional sub-structures.
        HdrDynamic2094_40 multiWindowMinimal() {
                HdrDynamic2094_40 md;
                md.setApplicationVersion(0);
                md.setNumWindows(3);
                md.setTargetedSystemDisplayMaximumLuminance(10000u);
                REQUIRE(md.extraWindows().size() == 2);

                md.extraWindows()[0].upperLeftCornerX = 100;
                md.extraWindows()[0].upperLeftCornerY = 200;
                md.extraWindows()[0].lowerRightCornerX = 500;
                md.extraWindows()[0].lowerRightCornerY = 400;
                md.extraWindows()[0].centerOfEllipseX = 300;
                md.extraWindows()[0].centerOfEllipseY = 300;
                md.extraWindows()[0].rotationAngle = 45;
                md.extraWindows()[0].semimajorAxisInternalEllipse = 100;
                md.extraWindows()[0].semimajorAxisExternalEllipse = 200;
                md.extraWindows()[0].semiminorAxisExternalEllipse = 150;
                md.extraWindows()[0].overlapProcessOption = true;

                md.extraWindows()[1].upperLeftCornerX = 600;
                md.extraWindows()[1].lowerRightCornerX = 1000;
                md.extraWindows()[1].overlapProcessOption = false;

                // Distinct per-window MaxSCL/average so the wire walker is
                // actually exercising the loop, not aliasing window 0.
                for (uint8_t w = 0; w < 3; ++w) {
                        md.windowProcessing()[w].maxScl[0] = 1000u * (w + 1);
                        md.windowProcessing()[w].averageMaxRgb = 500u * (w + 1);
                        md.windowProcessing()[w].fractionBrightPixels = static_cast<uint16_t>(10u * (w + 1));
                }
                return md;
        }

} // namespace

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("HdrDynamic2094_40: default-constructs to one empty window") {
        HdrDynamic2094_40 md;
        CHECK(md.applicationVersion() == 0u);
        CHECK(md.numWindows() == 1u);
        CHECK(md.extraWindows().isEmpty());
        REQUIRE(md.windowProcessing().size() == 1);
        CHECK_FALSE(md.targetedSystemDisplayActualPeakLuminance().isPresent());
        CHECK_FALSE(md.masteringDisplayActualPeakLuminance().isPresent());
        CHECK_FALSE(md.windowProcessing()[0].hasToneMapping);
        CHECK_FALSE(md.windowProcessing()[0].hasColorSaturationMapping);
}

TEST_CASE("HdrDynamic2094_40: setNumWindows reshapes both lists") {
        HdrDynamic2094_40 md;
        md.setNumWindows(3);
        CHECK(md.numWindows() == 3u);
        CHECK(md.extraWindows().size() == 2u);
        CHECK(md.windowProcessing().size() == 3u);
        md.setNumWindows(1);
        CHECK(md.numWindows() == 1u);
        CHECK(md.extraWindows().isEmpty());
        CHECK(md.windowProcessing().size() == 1u);
}

TEST_CASE("HdrDynamic2094_40: setNumWindows clamps to [1, MaxWindows]") {
        HdrDynamic2094_40 md;
        md.setNumWindows(0);
        CHECK(md.numWindows() == 1u);
        md.setNumWindows(99);
        CHECK(md.numWindows() == HdrDynamic2094_40::MaxWindows);
}

// ============================================================================
// Wire round-trip
// ============================================================================

TEST_CASE("HdrDynamic2094_40: default descriptor round-trips through toBuffer/fromBuffer") {
        HdrDynamic2094_40 md;
        Buffer            wire = md.toBuffer();
        CHECK(wire.size() > 0u);

        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == md);
}

TEST_CASE("HdrDynamic2094_40: full single-window descriptor round-trips") {
        HdrDynamic2094_40 md = fullSingleWindow();
        Buffer            wire = md.toBuffer();

        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == md);

        // Bit-exact wire identity on a second emit.
        CHECK(r.first().toBuffer() == wire);
}

TEST_CASE("HdrDynamic2094_40: multi-window descriptor round-trips") {
        HdrDynamic2094_40 md = multiWindowMinimal();
        Buffer            wire = md.toBuffer();

        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == md);
}

TEST_CASE("HdrDynamic2094_40: targeted_system_display_maximum_luminance respects 27-bit mask") {
        HdrDynamic2094_40 md;
        md.setTargetedSystemDisplayMaximumLuminance(0xFFFFFFFFu); // over 27 bits
        CHECK(md.targetedSystemDisplayMaximumLuminance() == 0x07FFFFFFu);
}

TEST_CASE("HdrDynamic2094_40: mastering-display actual-peak grid round-trips") {
        HdrDynamic2094_40 md;
        auto &grid = md.masteringDisplayActualPeakLuminance();
        grid.numRows = 3;
        grid.numCols = 4;
        grid.values = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        REQUIRE(grid.isPresent());

        Buffer                    wire = md.toBuffer();
        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const auto &outGrid = r.first().masteringDisplayActualPeakLuminance();
        CHECK(outGrid.numRows == 3u);
        CHECK(outGrid.numCols == 4u);
        REQUIRE(outGrid.values.size() == 12u);
        for (size_t i = 0; i < 12; ++i) CHECK(outGrid.values[i] == grid.values[i]);
}

// ============================================================================
// Failure modes
// ============================================================================

TEST_CASE("HdrDynamic2094_40: fromBuffer rejects empty input") {
        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(nullptr, 0);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("HdrDynamic2094_40: fromBuffer rejects truncated input") {
        // 4 bytes is well short of even a default-constructed descriptor.
        uint8_t                   bytes[4] = {0};
        Result<HdrDynamic2094_40> r = HdrDynamic2094_40::fromBuffer(bytes, sizeof(bytes));
        CHECK(r.second().code() == Error::CorruptData);
}

// ============================================================================
// JSON / toString
// ============================================================================

TEST_CASE("HdrDynamic2094_40: toJson produces structured output") {
        HdrDynamic2094_40 md = fullSingleWindow();
        JsonObject        obj = md.toJson();

        Error err;
        CHECK(obj.getInt("applicationVersion", &err) == 1);
        CHECK(obj.getInt("numWindows", &err) == 1);

        JsonObject tgrid = obj.getObject("targetedSystemDisplayActualPeakLuminance", &err);
        CHECK(err.isOk());
        CHECK(tgrid.getBool("present", &err));
        CHECK(tgrid.getInt("numRows", &err) == 2);
        CHECK(tgrid.getInt("numCols", &err) == 3);

        JsonArray wps = obj.getArray("windowProcessing", &err);
        CHECK(err.isOk());
        REQUIRE(wps.size() == 1u);
}

TEST_CASE("HdrDynamic2094_40: toString summarises top-level fields") {
        HdrDynamic2094_40 md = fullSingleWindow();
        String            s = md.toString();
        CHECK(s.contains("appVer=1"));
        CHECK(s.contains("numWindows=1"));
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("HdrDynamic2094_40: round-trips through Variant") {
        HdrDynamic2094_40 original = fullSingleWindow();
        Variant           v;
        v.set(original);
        CHECK(v.type() == Variant::TypeHdrDynamic2094_40);
        HdrDynamic2094_40 out = v.get<HdrDynamic2094_40>();
        CHECK(out == original);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("HdrDynamic2094_40: DataStream operators round-trip") {
        HdrDynamic2094_40 original = fullSingleWindow();
        Buffer            buf(4096);
        BufferIODevice    dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        HdrDynamic2094_40 restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        CHECK(restored == original);
}

// ============================================================================
// Equality / inequality
// ============================================================================

TEST_CASE("HdrDynamic2094_40: operator== / operator!= field-wise") {
        HdrDynamic2094_40 a = fullSingleWindow();
        HdrDynamic2094_40 b = a;
        CHECK(a == b);

        b.windowProcessing()[0].colorSaturationWeight = 7;
        CHECK(a != b);

        b = a;
        b.windowProcessing()[0].toneMapping.bezierCurveAnchors.pushToBack(999);
        CHECK(a != b);

        b = a;
        b.setNumWindows(2);
        CHECK(a != b);
}
