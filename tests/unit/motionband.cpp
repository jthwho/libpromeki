/**
 * @file      tests/motionband.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/motionband.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/color.h>

using namespace promeki;

namespace {

ImageDesc rgbDesc(int w = 320, int h = 240, PixelFormat::ID fmt = PixelFormat::RGBA8_sRGB) {
        return ImageDesc(w, h, fmt);
}

UncompressedVideoPayload::Ptr makeFilled(const ImageDesc &desc, uint8_t fillByte) {
        auto p = UncompressedVideoPayload::allocate(desc);
        REQUIRE(p.isValid());
        const PixelMemLayout &ml = desc.pixelFormat().memLayout();
        for (size_t pl = 0; pl < ml.planeCount(); pl++) {
                const size_t stride = ml.lineStride(pl, desc.width());
                const size_t vSub = ml.planeDesc(pl).vSubsampling;
                const size_t rows = desc.height() / vSub;
                std::memset(p.modify()->plane(pl).data(), fillByte, stride * rows);
        }
        return p;
}

bool rowAllEqual(uint8_t *row, size_t bytes, uint8_t v) {
        for (size_t i = 0; i < bytes; i++) {
                if (row[i] != v) return false;
        }
        return true;
}

bool planesEqual(const UncompressedVideoPayload &a, const UncompressedVideoPayload &b) {
        if (a.desc().width() != b.desc().width() || a.desc().height() != b.desc().height()) return false;
        const PixelMemLayout &ml = a.desc().pixelFormat().memLayout();
        for (size_t pl = 0; pl < ml.planeCount(); pl++) {
                const size_t stride = ml.lineStride(pl, a.desc().width());
                const size_t vSub = ml.planeDesc(pl).vSubsampling;
                const size_t rows = a.desc().height() / vSub;
                if (std::memcmp(a.plane(pl).data(), b.plane(pl).data(), stride * rows) != 0) return false;
        }
        return true;
}

} // namespace

// ============================================================================
// Defaults
// ============================================================================

TEST_CASE("MotionBand_Defaults") {
        MotionBand mb;
        CHECK_FALSE(mb.enabled());
        CHECK(mb.height() == MotionBand::DefaultHeight);
        CHECK(mb.sequenceLength() == 0);
        CHECK(mb.offset() == 0);
        CHECK(mb.position() == MotionBand::Position::Bottom);
        CHECK(mb.borderWidth() >= 1);
        CHECK(mb.reservedLines() == 0);
}

// ============================================================================
// Disabled apply is a no-op
// ============================================================================

TEST_CASE("MotionBand_DisabledApplyIsNoOp") {
        MotionBand mb;
        ImageDesc  d = rgbDesc();
        auto       payload = makeFilled(d, 0x42);
        Error      err = mb.apply(*payload.modify(), 0);
        CHECK(err.isOk());
        // Buffer untouched.
        const PixelMemLayout &ml = d.pixelFormat().memLayout();
        const size_t          stride = ml.lineStride(0, d.width());
        for (size_t y = 0; y < d.height(); y++) {
                CHECK(rowAllEqual(payload->plane(0).data() + y * stride, stride, 0x42));
        }
}

// ============================================================================
// Enabled but sequenceLength=0 is a no-op
// ============================================================================

TEST_CASE("MotionBand_EnabledZeroSequenceIsNoOp") {
        MotionBand mb;
        mb.setEnabled(true);
        // sequenceLength remains 0
        ImageDesc d = rgbDesc();
        auto      payload = makeFilled(d, 0x33);
        Error     err = mb.apply(*payload.modify(), 0);
        CHECK(err.isOk());
        const PixelMemLayout &ml = d.pixelFormat().memLayout();
        const size_t          stride = ml.lineStride(0, d.width());
        for (size_t y = 0; y < d.height(); y++) {
                CHECK(rowAllEqual(payload->plane(0).data() + y * stride, stride, 0x33));
        }
}

// ============================================================================
// Enable + cycle: rows above the band are untouched, band rows differ.
// ============================================================================

TEST_CASE("MotionBand_StampsBottomLeavesTopUntouched") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(30);
        mb.setHeight(16);
        // Default position is Bottom.

        ImageDesc d = rgbDesc(320, 240);
        auto      payload = makeFilled(d, 0x11);
        Error     err = mb.apply(*payload.modify(), 0);
        REQUIRE(err.isOk());
        CHECK(mb.reservedLines() == 16);

        const PixelMemLayout &ml = d.pixelFormat().memLayout();
        const size_t          stride = ml.lineStride(0, d.width());

        // Top (240 - 16) = 224 rows untouched.
        for (size_t y = 0; y + 16 < d.height(); y++) {
                CHECK(rowAllEqual(payload->plane(0).data() + y * stride, stride, 0x11));
        }
        // Last 16 rows should differ (we filled with 0x11; the BG fill
        // is dark gray, marker is yellow, ticks white).
        bool anyDifferent = false;
        for (size_t y = d.height() - 16; y < d.height(); y++) {
                if (!rowAllEqual(payload->plane(0).data() + y * stride, stride, 0x11)) {
                        anyDifferent = true;
                        break;
                }
        }
        CHECK(anyDifferent);
}

TEST_CASE("MotionBand_TopPositionLeavesBottomUntouched") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(30);
        mb.setHeight(16);
        mb.setPosition(MotionBand::Position::Top);

        ImageDesc d = rgbDesc(320, 240);
        auto      payload = makeFilled(d, 0x77);
        Error     err = mb.apply(*payload.modify(), 0);
        REQUIRE(err.isOk());

        const PixelMemLayout &ml = d.pixelFormat().memLayout();
        const size_t          stride = ml.lineStride(0, d.width());

        // Rows [16, 240) untouched.
        for (size_t y = 16; y < d.height(); y++) {
                CHECK(rowAllEqual(payload->plane(0).data() + y * stride, stride, 0x77));
        }
}

// ============================================================================
// Frame counter cycles — adjacent frames differ; cycle wraps.
// ============================================================================

TEST_CASE("MotionBand_AdjacentFramesDiffer") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(10);
        mb.setHeight(16);

        ImageDesc d = rgbDesc(640, 480);
        auto      pa = makeFilled(d, 0);
        auto      pb = makeFilled(d, 0);
        REQUIRE(mb.apply(*pa.modify(), 0).isOk());
        REQUIRE(mb.apply(*pb.modify(), 1).isOk());
        // Frame 0 and frame 1 must differ — the marker has moved one
        // tick to the right.
        CHECK_FALSE(planesEqual(*pa, *pb));
}

TEST_CASE("MotionBand_FrameCounterModuloWraps") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(7);
        mb.setHeight(16);

        ImageDesc d = rgbDesc(280, 120);
        auto      a = makeFilled(d, 0);
        auto      b = makeFilled(d, 0);
        REQUIRE(mb.apply(*a.modify(), 0).isOk());
        REQUIRE(mb.apply(*b.modify(), 7).isOk());
        CHECK(planesEqual(*a, *b));

        auto c = makeFilled(d, 0);
        auto d2 = makeFilled(d, 0);
        REQUIRE(mb.apply(*c.modify(), 3).isOk());
        REQUIRE(mb.apply(*d2.modify(), 7 + 3).isOk());
        CHECK(planesEqual(*c, *d2));
}

TEST_CASE("MotionBand_OffsetEquivalentToFrameShift") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(8);
        mb.setHeight(16);

        ImageDesc d = rgbDesc(320, 120);
        // Apply with offset=2 at frame 0.
        mb.setOffset(2);
        auto a = makeFilled(d, 0);
        REQUIRE(mb.apply(*a.modify(), 0).isOk());
        // Apply with offset=0 at frame 2.
        mb.setOffset(0);
        auto b = makeFilled(d, 0);
        REQUIRE(mb.apply(*b.modify(), 2).isOk());
        CHECK(planesEqual(*a, *b));
}

// ============================================================================
// Cache invalidation: changing visual config produces different pixels.
// ============================================================================

TEST_CASE("MotionBand_MarkerColorChangeInvalidatesCache") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(5);
        mb.setHeight(16);

        ImageDesc d = rgbDesc(160, 90);
        auto      a = makeFilled(d, 0);
        REQUIRE(mb.apply(*a.modify(), 1).isOk());

        // Change marker color and re-stamp; the band content must
        // differ because the cached frame was rebuilt.
        mb.setMarkerColor(Color::srgb(0.0f, 1.0f, 0.0f));
        auto b = makeFilled(d, 0);
        REQUIRE(mb.apply(*b.modify(), 1).isOk());
        CHECK_FALSE(planesEqual(*a, *b));
}

// ============================================================================
// Plane-aware stamping into a planar 4:2:0 target.
// ============================================================================

TEST_CASE("MotionBand_PlanarSubsampledStamp") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(10);
        mb.setHeight(16);

        // 8-bit 4:2:0 planar (semi-planar would also work but planar
        // is simpler to inspect).  Pick a small even size so chroma
        // halving is unambiguous.
        ImageDesc d(160, 120, PixelFormat::YUV8_420_Planar_Rec709);
        REQUIRE(d.pixelFormat().memLayout().planeCount() >= 2);

        auto  payload = makeFilled(d, 0x55);
        Error err = mb.apply(*payload.modify(), 3);
        REQUIRE(err.isOk());

        const PixelMemLayout &ml = d.pixelFormat().memLayout();
        // Top region of every plane untouched.
        for (size_t pl = 0; pl < ml.planeCount(); pl++) {
                const size_t stride = ml.lineStride(pl, d.width());
                const size_t vSub = ml.planeDesc(pl).vSubsampling;
                const size_t totalRows = d.height() / vSub;
                const size_t bandRows = 16 / vSub;
                const size_t topRows = totalRows - bandRows;
                for (size_t y = 0; y < topRows; y++) {
                        CHECK(rowAllEqual(payload->plane(pl).data() + y * stride, stride, 0x55));
                }
        }
}

// ============================================================================
// RGBA8 scratch fallback: stamping into a non-paintable target (BE
// 10-bit UYVY) routes through CSC and still modifies the bottom rows.
// ============================================================================

TEST_CASE("MotionBand_NonPaintableTargetUsesScratchFallback") {
        MotionBand mb;
        mb.setEnabled(true);
        mb.setSequenceLength(8);
        mb.setHeight(16);

        // 10-bit BE UYVY has no registered paint engine — exercises
        // the RGBA8 scratch + CSC fallback path.
        ImageDesc d(160, 120, PixelFormat::YUV10_422_UYVY_BE_Rec709);
        REQUIRE_FALSE(d.pixelFormat().hasPaintEngine());

        auto  payload = makeFilled(d, 0x80);
        Error err = mb.apply(*payload.modify(), 0);
        REQUIRE(err.isOk());

        // Bottom 16 rows of plane 0 should differ from the constant
        // fill (CSC will produce a varied pattern).
        const PixelMemLayout &ml = d.pixelFormat().memLayout();
        const size_t          stride = ml.lineStride(0, d.width());
        bool                  anyDifferent = false;
        for (size_t y = d.height() - 16; y < d.height(); y++) {
                if (!rowAllEqual(payload->plane(0).data() + y * stride, stride, 0x80)) {
                        anyDifferent = true;
                        break;
                }
        }
        CHECK(anyDifferent);
}

// ============================================================================
// reservedLines reflects enable state and configured height.
// ============================================================================

TEST_CASE("MotionBand_ReservedLinesTracksConfig") {
        MotionBand mb;
        CHECK(mb.reservedLines() == 0);
        mb.setHeight(24);
        CHECK(mb.reservedLines() == 0); // still disabled
        mb.setEnabled(true);
        CHECK(mb.reservedLines() == 24);
        mb.setEnabled(false);
        CHECK(mb.reservedLines() == 0);
}
