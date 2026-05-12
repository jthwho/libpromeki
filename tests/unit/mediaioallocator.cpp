/**
 * @file      tests/unit/mediaioallocator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaioallocator.h>
#include <promeki/pixelformat.h>
#include "mediaio_test_helpers.h"

using namespace promeki;
using namespace promeki::tests;

// ============================================================================
// MediaIOAllocator default behaviour
// ============================================================================

TEST_CASE("MediaIOAllocator: defaultAllocator returns the same instance") {
        MediaIOAllocator::Ptr a = MediaIOAllocator::defaultAllocator();
        MediaIOAllocator::Ptr b = MediaIOAllocator::defaultAllocator();
        REQUIRE(a.isValid());
        CHECK(a == b);
        CHECK(a->name() == "DefaultMediaIOAllocator");
}

TEST_CASE("MediaIOAllocator: allocateVideoPayload assembles per-plane buffers") {
        MediaIOAllocator::Ptr alloc = MediaIOAllocator::defaultAllocator();
        ImageDesc             desc(640, 480, PixelFormat(PixelFormat::RGBA8_sRGB));
        UncompressedVideoPayload::Ptr payload = alloc->allocateVideoPayload(desc);
        REQUIRE(payload.isValid());
        CHECK(payload->desc().width() == 640);
        CHECK(payload->desc().height() == 480);
        CHECK(payload->data().count() == static_cast<size_t>(desc.planeCount()));
}

TEST_CASE("MediaIOAllocator: allocateAudioPayload sized to AudioDesc::bufferSize") {
        MediaIOAllocator::Ptr alloc   = MediaIOAllocator::defaultAllocator();
        AudioDesc             desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000, 2);
        const size_t          samples = 1024;
        PcmAudioPayload::Ptr  payload = alloc->allocateAudioPayload(desc, samples);
        REQUIRE(payload.isValid());
        CHECK(payload->sampleCount() == samples);
        CHECK(payload->data().count() == 1);
        CHECK(payload->data()[0].size() == desc.bufferSize(samples));
}

TEST_CASE("MediaIOAllocator: invalid descriptor returns null payload") {
        MediaIOAllocator::Ptr alloc = MediaIOAllocator::defaultAllocator();
        ImageDesc             bad;  // default-constructed → invalid
        CHECK_FALSE(alloc->allocateVideoPayload(bad).isValid());
}

// ============================================================================
// Per-plane override picks up via base-class default allocateVideoPayload
// ============================================================================

namespace {
        struct StampingAllocator : public MediaIOAllocator {
                        PROMEKI_SHARED_DERIVED(StampingAllocator)
                        mutable std::atomic<int> planeCalls{0};
                        String name() const override { return String("StampingAllocator"); }
                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                planeCalls.fetch_add(1);
                                Buffer b = MediaIOAllocator::allocateVideoPlane(desc, planeIndex);
                                if (b.isValid()) {
                                        // Stamp the first byte so we can verify the
                                        // override actually produced this plane.
                                        std::memset(b.data(), 0xAA, 1);
                                }
                                return b;
                        }
        };
}

TEST_CASE("MediaIOAllocator: per-plane override flows through default allocateVideoPayload") {
        MediaIOAllocator::Ptr alloc = MediaIOAllocator::Ptr::takeOwnership(new StampingAllocator());
        ImageDesc             desc(640, 480, PixelFormat(PixelFormat::RGBA8_sRGB));
        UncompressedVideoPayload::Ptr payload = alloc->allocateVideoPayload(desc);
        REQUIRE(payload.isValid());

        // Verify per-plane override was actually called for every plane.
        const int planeCount = desc.planeCount();
        const auto *typed = static_cast<const StampingAllocator *>(alloc.ptr());
        CHECK(typed->planeCalls.load() == planeCount);

        // First byte of plane 0 should carry the stamp.
        CHECK(payload->data().count() >= 1);
        const auto &slice = payload->data()[0];
        REQUIRE(slice.buffer().isValid());
        CHECK(static_cast<const unsigned char *>(slice.buffer().data())[0] == 0xAA);
}

// ============================================================================
// Full-payload override skips the per-plane path
// ============================================================================

namespace {
        struct WholePayloadAllocator : public MediaIOAllocator {
                        PROMEKI_SHARED_DERIVED(WholePayloadAllocator)
                        mutable std::atomic<int> planeCalls{0};
                        mutable std::atomic<int> payloadCalls{0};
                        String name() const override { return String("WholePayloadAllocator"); }
                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                planeCalls.fetch_add(1);
                                return MediaIOAllocator::allocateVideoPlane(desc, planeIndex);
                        }
                        UncompressedVideoPayload::Ptr allocateVideoPayload(const ImageDesc &desc) const override {
                                payloadCalls.fetch_add(1);
                                // Bypass the per-plane base path entirely.
                                Buffer single =
                                        BufferAllocator::defaultAllocator()->allocateBytes(desc.pixelFormat().planeSize(0, desc));
                                if (!single.isValid()) return UncompressedVideoPayload::Ptr();
                                BufferView view(single, 0, single.allocSize());
                                return UncompressedVideoPayload::Ptr::create(desc, view);
                        }
        };
}

TEST_CASE("MediaIOAllocator: full-payload override skips per-plane path") {
        MediaIOAllocator::Ptr alloc = MediaIOAllocator::Ptr::takeOwnership(new WholePayloadAllocator());
        ImageDesc             desc(64, 64, PixelFormat(PixelFormat::RGBA8_sRGB));
        UncompressedVideoPayload::Ptr payload = alloc->allocateVideoPayload(desc);
        REQUIRE(payload.isValid());

        const auto *typed = static_cast<const WholePayloadAllocator *>(alloc.ptr());
        CHECK(typed->payloadCalls.load() == 1);
        CHECK(typed->planeCalls.load() == 0); // base per-plane path NOT used
}

// ============================================================================
// MediaIO::allocator + setAllocator
// ============================================================================

TEST_CASE("MediaIO::allocator: never null and falls back to default") {
        InlineTestMediaIO io;
        MediaIOAllocator::Ptr a = io.allocator();
        REQUIRE(a.isValid());
        CHECK(a->name() == "DefaultMediaIOAllocator");
}

TEST_CASE("MediaIO::setAllocator: install + clear cycles back to default") {
        InlineTestMediaIO io;

        // Install a custom allocator.
        MediaIOAllocator::Ptr custom = MediaIOAllocator::Ptr::takeOwnership(new StampingAllocator());
        io.setAllocator(custom);
        CHECK(io.allocator() == custom);
        CHECK(io.allocator()->name() == "StampingAllocator");

        // Pass null to revert to default.
        io.setAllocator(MediaIOAllocator::Ptr());
        MediaIOAllocator::Ptr after = io.allocator();
        REQUIRE(after.isValid());
        CHECK(after->name() == "DefaultMediaIOAllocator");
}

TEST_CASE("UncompressedVideoPayload::allocate routes through MediaIOAllocator default") {
        // Existing static helper now goes through the allocator
        // framework — verify it still produces a valid payload.
        ImageDesc desc(320, 240, PixelFormat(PixelFormat::RGBA8_sRGB));
        UncompressedVideoPayload::Ptr p = UncompressedVideoPayload::allocate(desc);
        REQUIRE(p.isValid());
        CHECK(p->desc().width() == 320);
        CHECK(p->desc().height() == 240);
}
