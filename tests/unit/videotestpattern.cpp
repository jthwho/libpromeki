/**
 * @file      tests/videotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <doctest/doctest.h>
#include <promeki/videotestpattern.h>
#include <promeki/enums.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/color.h>

using namespace promeki;

static ImageDesc testDesc(int w = 320, int h = 240, PixelFormat::ID fmt = PixelFormat::RGB8_sRGB) {
        return ImageDesc(w, h, fmt);
}

static size_t stride0(const UncompressedVideoPayload &p) {
        return p.desc().pixelFormat().memLayout().lineStride(0, p.desc().width());
}

// ============================================================================
// Construction and defaults
// ============================================================================

TEST_CASE("VideoTestPattern_Defaults") {
        VideoTestPattern gen;
        CHECK(gen.pattern() == VideoPattern::ColorBars);
        CHECK_FALSE(gen.solidColor().isValid());
}

// ============================================================================
// create() produces valid payloads for all patterns
// ============================================================================

TEST_CASE("VideoTestPattern_CreateAllPatterns") {
        VideoTestPattern gen;
        ImageDesc        desc = testDesc();

        VideoPattern patterns[] = {VideoPattern::ColorBars,  VideoPattern::ColorBars75,  VideoPattern::Ramp,
                                   VideoPattern::Grid,       VideoPattern::Crosshatch,   VideoPattern::Checkerboard,
                                   VideoPattern::SolidColor, VideoPattern::White,        VideoPattern::Black,
                                   VideoPattern::Noise,      VideoPattern::ZonePlate,    VideoPattern::ColorChecker,
                                   VideoPattern::SMPTE219,   VideoPattern::AvSync,       VideoPattern::MultiBurst,
                                   VideoPattern::LimitRange, VideoPattern::CircularZone, VideoPattern::Alignment,
                                   VideoPattern::SDIPathEQ,  VideoPattern::SDIPathPLL};

        for (auto pat : patterns) {
                gen.setPattern(pat);
                auto img = gen.createPayload(desc, 0.0);
                REQUIRE(img.isValid());
                CHECK(img->desc().width() == 320);
                CHECK(img->desc().height() == 240);
        }
}

// ============================================================================
// render() into existing image
// ============================================================================

TEST_CASE("VideoTestPattern_RenderIntoExisting") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::White);

        ImageDesc desc = testDesc(64, 64);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *data = img->plane(0).data();
        CHECK(data[0] == 255);
        CHECK(data[1] == 255);
        CHECK(data[2] == 255);
}

// ============================================================================
// Motion offset
// ============================================================================

TEST_CASE("VideoTestPattern_MotionOffset") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);

        ImageDesc desc = testDesc(64, 64);
        auto      img1 = gen.createPayload(desc, 0.0);
        auto      img2 = gen.createPayload(desc, 10.0);

        REQUIRE(img1.isValid());
        REQUIRE(img2.isValid());

        const uint8_t *d1 = img1->plane(0).data();
        const uint8_t *d2 = img2->plane(0).data();
        bool           differ = false;
        for (size_t i = 0; i < 64 * 3; i++) {
                if (d1[i] != d2[i]) {
                        differ = true;
                        break;
                }
        }
        CHECK(differ);
}

// ============================================================================
// SolidColor uses configured color
// ============================================================================

TEST_CASE("VideoTestPattern_SolidColor") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SolidColor);
        gen.setSolidColor(Color::Red);

        ImageDesc desc = testDesc(8, 8);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *data = img->plane(0).data();
        CHECK(data[0] == 255);
        CHECK(data[1] == 0);
        CHECK(data[2] == 0);
}

// ============================================================================
// RGBA8 pixel format
// ============================================================================

TEST_CASE("VideoTestPattern_RGBA8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::White);

        ImageDesc desc = testDesc(16, 16, PixelFormat::RGBA8_sRGB);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());
        CHECK(img->desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);
}

// ============================================================================
// VideoPattern name round-trip via the TypedEnum machinery
// ============================================================================

TEST_CASE("VideoTestPattern_StringRoundTrip") {
        VideoPattern patterns[] = {VideoPattern::ColorBars,  VideoPattern::ColorBars75,  VideoPattern::Ramp,
                                   VideoPattern::Grid,       VideoPattern::Crosshatch,   VideoPattern::Checkerboard,
                                   VideoPattern::SolidColor, VideoPattern::White,        VideoPattern::Black,
                                   VideoPattern::Noise,      VideoPattern::ZonePlate,    VideoPattern::ColorChecker,
                                   VideoPattern::SMPTE219,   VideoPattern::AvSync,       VideoPattern::MultiBurst,
                                   VideoPattern::LimitRange, VideoPattern::CircularZone, VideoPattern::Alignment,
                                   VideoPattern::SDIPathEQ,  VideoPattern::SDIPathPLL};

        for (auto pat : patterns) {
                String name = Enum::nameOf(VideoPattern::Type, pat.value());
                CHECK_FALSE(name.isEmpty());
                VideoPattern parsed(name);
                CHECK(parsed.hasListedValue());
                CHECK(parsed == pat);
        }
}

TEST_CASE("VideoTestPattern_FromStringInvalid") {
        VideoPattern bogus("bogus");
        CHECK_FALSE(bogus.hasListedValue());
}

// ============================================================================
// BurnPosition name round-trip
// ============================================================================

TEST_CASE("VideoTestPattern_BurnPositionRoundTrip") {
        struct Case {
                        BurnPosition pos;
                        const char  *name;
        };
        Case cases[] = {
                {BurnPosition::TopLeft, "TopLeft"},
                {BurnPosition::TopCenter, "TopCenter"},
                {BurnPosition::TopRight, "TopRight"},
                {BurnPosition::BottomLeft, "BottomLeft"},
                {BurnPosition::BottomCenter, "BottomCenter"},
                {BurnPosition::BottomRight, "BottomRight"},
                {BurnPosition::Center, "Center"},
        };

        for (auto &c : cases) {
                String name = Enum::nameOf(BurnPosition::Type, c.pos.value());
                CHECK(name == String(c.name));

                BurnPosition parsed(name);
                CHECK(parsed.hasListedValue());
                CHECK(parsed == c.pos);
        }
}

TEST_CASE("VideoTestPattern_BurnPositionFromStringInvalid") {
        BurnPosition bogus("bogus");
        CHECK_FALSE(bogus.hasListedValue());
}

// ============================================================================
// BurnCenter enum value is stable at 6
// ============================================================================

TEST_CASE("VideoTestPattern_BurnCenterValue") {
        CHECK(BurnPosition::Center.value() == 6);
}

#include <promeki/timecode.h>

static bool payloadPixelMatches(const UncompressedVideoPayload &img, uint8_t r, uint8_t g, uint8_t b) {
        const uint8_t *data = img.plane(0).data();
        return data[0] == r && data[1] == g && data[2] == b;
}

TEST_CASE("VideoTestPattern_AvSyncCachedWhiteAndBlack") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::AvSync);
        ImageDesc desc(64, 32, PixelFormat::RGB8_sRGB);

        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);
        REQUIRE(marker.isValid());
        REQUIRE(marker.frame() == 0);
        auto white = gen.createPayload(desc, 0.0, marker);
        REQUIRE(white.isValid());
        CHECK(payloadPixelMatches(*white, 255, 255, 255));

        Timecode nonMarker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 5);
        REQUIRE(nonMarker.isValid());
        REQUIRE(nonMarker.frame() == 5);
        auto black = gen.createPayload(desc, 0.0, nonMarker);
        REQUIRE(black.isValid());
        CHECK(payloadPixelMatches(*black, 0, 0, 0));

        // Repeated calls reuse the cached planes.
        auto white2 = gen.createPayload(desc, 0.0, marker);
        REQUIRE(white2.isValid());
        CHECK(white2->plane(0).data() == white->plane(0).data());

        auto black2 = gen.createPayload(desc, 0.0, nonMarker);
        REQUIRE(black2.isValid());
        CHECK(black2->plane(0).data() == black->plane(0).data());

        auto fallback = gen.createPayload(desc, 0.0, Timecode());
        REQUIRE(fallback.isValid());
        CHECK(payloadPixelMatches(*fallback, 0, 0, 0));
}

// ============================================================================
// SDI pathological patterns — exact byte verification
// ============================================================================

TEST_CASE("VideoTestPattern_SDIPathEQ_UYVY8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        CHECK(row0[0] == 0xC0);
        CHECK(row0[1] == 0x66);
        CHECK(row0[2] == 0xC0);
        CHECK(row0[3] == 0x66);
        CHECK(row0[124] == 0xC0);
        CHECK(row0[125] == 0x66);

        size_t         stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        CHECK(row1[0] == 0x66);
        CHECK(row1[1] == 0xC0);
        CHECK(row1[2] == 0x66);
        CHECK(row1[3] == 0xC0);
}

TEST_CASE("VideoTestPattern_SDIPathPLL_UYVY8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathPLL);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        CHECK(row0[0] == 0x80);
        CHECK(row0[1] == 0x44);
        CHECK(row0[2] == 0x80);
        CHECK(row0[3] == 0x44);

        size_t         stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        CHECK(row1[0] == 0x44);
        CHECK(row1[1] == 0x80);
        CHECK(row1[2] == 0x44);
        CHECK(row1[3] == 0x80);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_UYVY10LE") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV10_422_UYVY_LE_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint16_t *row0 = reinterpret_cast<const uint16_t *>(img->plane(0).data());
        CHECK(row0[0] == 0x0300);
        CHECK(row0[1] == 0x0198);
        CHECK(row0[2] == 0x0300);
        CHECK(row0[3] == 0x0198);

        size_t          stride = stride0(*img);
        const uint16_t *row1 = reinterpret_cast<const uint16_t *>(img->plane(0).data() + stride);
        CHECK(row1[0] == 0x0198);
        CHECK(row1[1] == 0x0300);
        CHECK(row1[2] == 0x0198);
        CHECK(row1[3] == 0x0300);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_YUYV8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        CHECK(row0[0] == 0x66);
        CHECK(row0[1] == 0xC0);
        CHECK(row0[2] == 0x66);
        CHECK(row0[3] == 0xC0);

        size_t         stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        CHECK(row1[0] == 0xC0);
        CHECK(row1[1] == 0x66);
        CHECK(row1[2] == 0xC0);
        CHECK(row1[3] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_v210") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV10_422_v210_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint32_t *row0 = reinterpret_cast<const uint32_t *>(img->plane(0).data());
        CHECK((row0[0] & 0x3FFFFFFFu) == 0x30066300u);
        CHECK((row0[1] & 0x3FFFFFFFu) == 0x198C0198u);
        CHECK((row0[2] & 0x3FFFFFFFu) == 0x30066300u);
        CHECK((row0[3] & 0x3FFFFFFFu) == 0x198C0198u);

        size_t          stride = stride0(*img);
        const uint32_t *row1 = reinterpret_cast<const uint32_t *>(img->plane(0).data() + stride);
        CHECK((row1[0] & 0x3FFFFFFFu) == 0x198C0198u);
        CHECK((row1[1] & 0x3FFFFFFFu) == 0x30066300u);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_Planar8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_Planar_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *yRow0 = img->plane(0).data();
        CHECK(yRow0[0] == 0x66);
        CHECK(yRow0[63] == 0x66);
        size_t yStride = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        CHECK(yRow0[yStride] == 0xC0);

        const uint8_t *cbRow0 = img->plane(1).data();
        CHECK(cbRow0[0] == 0xC0);
        CHECK(cbRow0[31] == 0xC0);
        size_t cbStride = img->desc().pixelFormat().memLayout().lineStride(1, img->desc().width());
        CHECK(cbRow0[cbStride] == 0x66);

        const uint8_t *crRow0 = img->plane(2).data();
        CHECK(crRow0[0] == 0xC0);
        CHECK(crRow0[cbStride] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_SemiPlanar8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_SemiPlanar_Rec709);
        auto      img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *yRow0 = img->plane(0).data();
        CHECK(yRow0[0] == 0x66);
        size_t yStride = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        CHECK(yRow0[yStride] == 0xC0);

        const uint8_t *cRow0 = img->plane(1).data();
        CHECK(cRow0[0] == 0xC0);
        CHECK(cRow0[1] == 0xC0);
        size_t cStride = img->desc().pixelFormat().memLayout().lineStride(1, img->desc().width());
        CHECK(cRow0[cStride] == 0x66);
        CHECK(cRow0[cStride + 1] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPath_CacheBehavior") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);

        auto a = gen.createPayload(desc, 0.0);
        auto b = gen.createPayload(desc, 0.0);
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        CHECK(a->plane(0).data() == b->plane(0).data());

        gen.setPattern(VideoPattern::SDIPathPLL);
        auto c = gen.createPayload(desc, 0.0);
        REQUIRE(c.isValid());
        CHECK(c->plane(0).data() != a->plane(0).data());

        const uint8_t *row0 = c->plane(0).data();
        CHECK(row0[0] == 0x80);
        CHECK(row0[1] == 0x44);
}

TEST_CASE("VideoTestPattern_SDIPath_RGBFallback") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::RGB8_sRGB);

        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        bool           linesDiffer = false;
        for (int i = 0; i < 3; i++) {
                if (row0[i] != row1[i]) {
                        linesDiffer = true;
                        break;
                }
        }
        CHECK(linesDiffer);
}

TEST_CASE("VideoTestPattern_AvSyncCacheRebuildsOnDescChange") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::AvSync);
        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);

        auto a = gen.createPayload(ImageDesc(32, 16, PixelFormat::RGB8_sRGB), 0.0, marker);
        REQUIRE(a.isValid());
        CHECK(a->desc().width() == 32);
        CHECK(a->desc().height() == 16);
        CHECK(payloadPixelMatches(*a, 255, 255, 255));

        auto b = gen.createPayload(ImageDesc(64, 32, PixelFormat::RGB8_sRGB), 0.0, marker);
        REQUIRE(b.isValid());
        CHECK(b->desc().width() == 64);
        CHECK(b->desc().height() == 32);
        CHECK(payloadPixelMatches(*b, 255, 255, 255));
        CHECK(b->plane(0).data() != a->plane(0).data());
}

// ============================================================================
// Allocator wiring (Phase D — VideoTestPattern routes through MediaIOAllocator)
// ============================================================================

#include <promeki/mediaioallocator.h>
#include <promeki/memspace.h>

namespace {
        struct VtpStampingAllocator : public MediaIOAllocator {
                        PROMEKI_SHARED_DERIVED(VtpStampingAllocator)
                        mutable std::atomic<int> planeCalls{0};
                        String name() const override { return String("VtpStampingAllocator"); }
                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                planeCalls.fetch_add(1);
                                return MediaIOAllocator::allocateVideoPlane(desc, planeIndex);
                        }
        };
}

TEST_CASE("VideoTestPattern_Allocator_RoutesCachedPayload") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars); // static => cached
        auto alloc = MediaIOAllocator::Ptr::takeOwnership(new VtpStampingAllocator());
        gen.setAllocator(alloc);

        ImageDesc desc(320, 240, PixelFormat::RGB8_sRGB);
        auto      a = gen.createPayload(desc, 0.0);
        REQUIRE(a.isValid());

        // Each call after the first should hit the cache (no further
        // plane allocations).  ColorBars is single-plane RGB8.
        const auto *typed = static_cast<const VtpStampingAllocator *>(alloc.ptr());
        const int   afterFirst = typed->planeCalls.load();
        CHECK(afterFirst >= 1);

        auto b = gen.createPayload(desc, 0.0);
        REQUIRE(b.isValid());
        CHECK(typed->planeCalls.load() == afterFirst); // no fresh allocation
        // Same impl pointer means we're sharing the cache slot.
        CHECK(a->plane(0).buffer().impl().ptr() == b->plane(0).buffer().impl().ptr());
}

TEST_CASE("VideoTestPattern_Allocator_ChangeInvalidatesCache") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);

        ImageDesc desc(320, 240, PixelFormat::RGB8_sRGB);
        auto      first = gen.createPayload(desc, 0.0);
        REQUIRE(first.isValid());
        const BufferImpl *firstImpl = first->plane(0).buffer().impl().ptr();

        // Install a new allocator — the cached payload should be
        // dropped so the next call re-allocates through the new
        // policy.
        auto alloc = MediaIOAllocator::Ptr::takeOwnership(new VtpStampingAllocator());
        gen.setAllocator(alloc);
        auto second = gen.createPayload(desc, 0.0);
        REQUIRE(second.isValid());
        const BufferImpl *secondImpl = second->plane(0).buffer().impl().ptr();
        CHECK(firstImpl != secondImpl);
}

#if PROMEKI_ENABLE_MEMFD

namespace {
        struct TpgLikeAlloc : public MediaIOAllocator {
                        PROMEKI_SHARED_DERIVED(TpgLikeAlloc)
                        String name() const override { return String("TpgLikeAlloc"); }
                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                const PixelFormat &pf = desc.pixelFormat();
                                if (planeIndex < 0 || planeIndex >= static_cast<int>(pf.planeCount())) return Buffer();
                                const size_t bytes = pf.planeSize(static_cast<size_t>(planeIndex), desc);
                                if (bytes == 0) return Buffer();
                                Buffer b(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
                                if (b.isValid()) b.setSize(bytes);
                                return b;
                        }
        };
}

TEST_CASE("VideoTestPattern_SystemCow_TpgFlowReplica") {
        // Mimic the EXACT TpgMediaIO::executeCmd(Read) flow:
        //   1. createPayload returns the cached Ptr
        //   2. payload.modify() (timecode metadata write) — refcount=2 → detach
        //   3. frame's slot holds the moved Ptr (refcount=1)
        //   4. slot->modify() — refcount=1, no detach
        //   5. uvp->ensureExclusive() — Buffer impls had refcount=2, detach each
        //   6. applyBurn writes to the fresh CoW clone
        // Verify burn pixels actually appear post-burn.
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);
        gen.setBurnEnabled(true);
        gen.setBurnFontSize(48);
        gen.setBurnTextColor(Color::White);
        gen.setBurnDrawBackground(true);
        gen.setBurnBackgroundColor(Color::Black);
        gen.setAllocator(MediaIOAllocator::Ptr::takeOwnership(new TpgLikeAlloc()));

        ImageDesc desc(640, 480, PixelFormat::RGB8_sRGB);

        // Drive frame 1
        auto payload = gen.createPayload(desc, 0.0);
        REQUIRE(payload.isValid());

        // Step 2: timecode write (forces payload.modify() to detach)
        payload.modify()->desc().metadata().set(Metadata::Timecode, Timecode());

        // Step 3: simulate frame.addPayload — wrap as MediaPayload Ptr
        MediaPayload::Ptr framePayload(payload);

        // Step 4-5: burn pass equivalent
        auto *uvp = static_cast<UncompressedVideoPayload *>(framePayload.modify());
        uvp->ensureExclusive();

        // Save buffer pointer before burn — should match plane(0).data()
        const uint8_t *bufBeforeBurn = static_cast<const uint8_t *>(uvp->plane(0).data());
        REQUIRE(bufBeforeBurn != nullptr);

        // Snapshot a small region — bars are in this region but for a
        // 640x480 image with 48-pixel-tall burn at BottomCenter, the
        // burn band is in the bottom rows; the top rows should still
        // be just bars.
        const size_t bytes = uvp->plane(0).size();

        Error burnErr = gen.applyBurn(*uvp, String("HELLO"));
        REQUIRE(burnErr == Error::Ok);

        // After burn, the buffer should still be the same pointer
        // (no further detach happened in applyBurn).
        const uint8_t *bufAfterBurn = static_cast<const uint8_t *>(uvp->plane(0).data());
        CHECK(bufAfterBurn == bufBeforeBurn);

        // Bottom 96 rows (where burn lives) should have at least one
        // pixel that's *not* a pure ColorBars value — the burn text
        // anti-aliasing produces grayscale pixels that aren't part of
        // the strict bar palette.
        const size_t stride = desc.pixelFormat().lineStride(0, desc);
        const size_t startRow = desc.size().height() - 96;
        bool         foundBurnBand = false;
        for (size_t y = startRow; y < desc.size().height() && !foundBurnBand; ++y) {
                const uint8_t *row = bufAfterBurn + y * stride;
                for (size_t x = 0; x < desc.size().width() && !foundBurnBand; ++x) {
                        const uint8_t r = row[x * 3 + 0];
                        const uint8_t g = row[x * 3 + 1];
                        const uint8_t b = row[x * 3 + 2];
                        // Bars: pure 0/255 values per channel.  Anything
                        // in between is from the burn (anti-aliased text).
                        if ((r != 0 && r != 255) || (g != 0 && g != 255) || (b != 0 && b != 255)) {
                                foundBurnBand = true;
                        }
                }
        }
        CHECK(foundBurnBand);
        (void)bytes;
}

TEST_CASE("VideoTestPattern_SystemCow_BurnTextActuallyAppears") {
        // Reproduces the full TpgMediaIO burn flow against a
        // SystemCow-backed cached payload to verify pixels actually
        // change in the burn band.  This is the property TPG users
        // see as "did the timecode appear on the picture?"
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SolidColor);
        gen.setSolidColor(Color::Black);
        gen.setBurnEnabled(true);
        gen.setBurnFontSize(24);
        gen.setBurnTextColor(Color::White);
        gen.setBurnBackgroundColor(Color::Black);
        gen.setBurnDrawBackground(false);
        gen.setAllocator(MediaIOAllocator::Ptr::takeOwnership(new TpgLikeAlloc()));

        ImageDesc desc(640, 480, PixelFormat::RGB8_sRGB);
        // First call seeds the cache (background pattern + seal)
        auto cached = gen.createPayload(desc, 0.0);
        REQUIRE(cached.isValid());
        REQUIRE(cached->plane(0).buffer().isCowBacked());

        // Per-frame: detach the cached UVP via the SharedPtr<UVP>::modify()
        // path that TpgMediaIO uses on the frame's payload list, then
        // ensureExclusive() the underlying buffers, then apply the burn.
        auto perFrame = cached;
        perFrame.detach(); // payload-level CoW
        perFrame.modify()->ensureExclusive();

        // Sanity: the per-frame UVP's plane 0 impl is now a CoW
        // clone, not the cached impl.
        CHECK(perFrame->plane(0).buffer().impl().ptr() != cached->plane(0).buffer().impl().ptr());

        // Snapshot a small region in the upper-left where burn text
        // should not yet have been drawn.  Black background, so all
        // bytes are 0 (RGB8: R,G,B).
        const uint8_t *pre = static_cast<const uint8_t *>(perFrame->plane(0).data());
        REQUIRE(pre != nullptr);
        bool preIsBlack = true;
        for (size_t i = 0; i < 256; ++i) {
                if (pre[i] != 0) { preIsBlack = false; break; }
        }
        CHECK(preIsBlack);

        // Apply burn — text should land somewhere in the image.
        Error burnErr = gen.applyBurn(*perFrame.modify(), String("HELLO"));
        REQUIRE(burnErr == Error::Ok);

        // After burn, the per-frame UVP's plane(0) should have at
        // least one non-zero byte somewhere in the image (the burn
        // text or its background).
        const uint8_t *post = static_cast<const uint8_t *>(perFrame->plane(0).data());
        REQUIRE(post != nullptr);
        const size_t bytes = perFrame->plane(0).size();
        bool         hasBurn = false;
        for (size_t i = 0; i < bytes; ++i) {
                if (post[i] != 0) { hasBurn = true; break; }
        }
        CHECK(hasBurn);

        // The cached payload must remain unchanged — burn went to the
        // CoW clone, not to the cached source.
        const uint8_t *cachedPx = static_cast<const uint8_t *>(cached->plane(0).data());
        REQUIRE(cachedPx != nullptr);
        bool cachedStillBlack = true;
        for (size_t i = 0; i < bytes; ++i) {
                if (cachedPx[i] != 0) { cachedStillBlack = false; break; }
        }
        CHECK(cachedStillBlack);
}

TEST_CASE("VideoTestPattern_SystemCow_PerFrameDetachIsCheap") {
        // Use the same TpgAllocator pattern TpgMediaIO installs:
        // SystemCow for video planes.  Verify that
        //   (a) the cached payload's planes report isCowBacked() true,
        //   (b) seal()'s effect persists across cache hits, and
        //   (c) ensureExclusive() on a sibling handle to the cached
        //       payload produces a fresh BufferImpl (the CoW clone) —
        //       which is the property TPG burn-in trades on.
        struct CowAlloc : public MediaIOAllocator {
                        PROMEKI_SHARED_DERIVED(CowAlloc)
                        String name() const override { return String("CowAlloc"); }
                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                const PixelFormat &pf = desc.pixelFormat();
                                if (planeIndex < 0 || planeIndex >= static_cast<int>(pf.planeCount())) return Buffer();
                                const size_t bytes = pf.planeSize(static_cast<size_t>(planeIndex), desc);
                                if (bytes == 0) return Buffer();
                                Buffer b(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
                                if (b.isValid()) b.setSize(bytes);
                                return b;
                        }
        };
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);
        gen.setAllocator(MediaIOAllocator::Ptr::takeOwnership(new CowAlloc()));

        ImageDesc desc(640, 480, PixelFormat::RGBA8_sRGB);

        auto first = gen.createPayload(desc, 0.0);
        REQUIRE(first.isValid());
        REQUIRE(first->plane(0).buffer().isCowBacked());
        // After populate, the cached payload is sealed and
        // residentBytes drops to the actually-resident set
        // (Private_Dirty is 0 for the source — its pages live in
        // the file-cache half of the memfd, not in private clones).
        CHECK(first->plane(0).buffer().residentBytes() == 0);

        // Sibling handle through a fresh cache hit — both Ptrs
        // reference the same UVP impl (cache hit, no fresh allocation).
        auto secondHandle = gen.createPayload(desc, 0.0);
        REQUIRE(secondHandle.isValid());
        CHECK(first->plane(0).buffer().impl().ptr() == secondHandle->plane(0).buffer().impl().ptr());

        // Per-frame applyBurn() prologue: detach the payload-level Ptr
        // so we can mutate without disturbing the cache, then
        // ensureExclusive() on the underlying plane Buffer to switch
        // to a private MAP_PRIVATE clone of the sealed memfd region.
        auto cloned = secondHandle;
        cloned.detach(); // payload-level CoW (UncompressedVideoPayload's _promeki_clone)
        // After payload detach, the UVP is fresh but its BufferView
        // still references the cached Buffers (refcount-bumped).
        // ensureExclusive on the plane Buffer triggers the cheap
        // MAP_PRIVATE clone of the sealed memfd region.
        Buffer plane0 = cloned->data()[0].buffer();
        plane0.ensureExclusive();
        CHECK(plane0.impl().ptr() != first->plane(0).buffer().impl().ptr());
        CHECK(plane0.isCowBacked());
}

#endif // PROMEKI_ENABLE_MEMFD
