/**
 * @file      tests/unit/bufferallocator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <thread>
#include <doctest/doctest.h>
#include <promeki/bufferallocator.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/pixelformat.h>

using namespace promeki;

TEST_CASE("BufferAllocator: defaultAllocator returns the same instance") {
        BufferAllocator::Ptr a = BufferAllocator::defaultAllocator();
        BufferAllocator::Ptr b = BufferAllocator::defaultAllocator();
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        CHECK(a == b);
        CHECK(a->name() == "DefaultBufferAllocator");
}

TEST_CASE("BufferAllocator: allocateBytes returns a default-MemSpace Buffer") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        Buffer               buf   = alloc->allocateBytes(4096);
        REQUIRE(buf.isValid());
        CHECK(buf.allocSize() == 4096);
        CHECK(buf.memSpace().id() == MemSpace::System);
}

TEST_CASE("BufferAllocator: allocateBytes(0) returns invalid Buffer") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        Buffer               buf   = alloc->allocateBytes(0);
        CHECK_FALSE(buf.isValid());
}

TEST_CASE("BufferAllocator: allocateBytes honours custom alignment") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        Buffer               buf   = alloc->allocateBytes(4096, 64);
        REQUIRE(buf.isValid());
        CHECK(buf.align() == 64);
        const auto addr = reinterpret_cast<uintptr_t>(buf.data());
        CHECK(addr % 64 == 0);
}

TEST_CASE("BufferAllocator: allocateVideoPlane sized to PixelFormat::planeSize") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        ImageDesc            desc(1920, 1080, PixelFormat(PixelFormat::RGBA8_sRGB));
        REQUIRE(desc.pixelFormat().isValid());
        const size_t expected = desc.pixelFormat().planeSize(0, desc);
        Buffer       buf      = alloc->allocateVideoPlane(desc, 0);
        REQUIRE(buf.isValid());
        CHECK(buf.allocSize() == expected);
        CHECK(buf.size() == expected);
}

TEST_CASE("BufferAllocator: allocateVideoPlane invalid plane index returns invalid Buffer") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        ImageDesc            desc(1920, 1080, PixelFormat(PixelFormat::RGBA8_sRGB));
        CHECK_FALSE(alloc->allocateVideoPlane(desc, -1).isValid());
        CHECK_FALSE(alloc->allocateVideoPlane(desc, 999).isValid());
}

TEST_CASE("BufferAllocator: allocateAudioChunk sized to AudioDesc::bufferSize") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        AudioDesc            desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000, 2);
        const size_t         samples  = 1024;
        const size_t         expected = desc.bufferSize(samples);
        Buffer               buf      = alloc->allocateAudioChunk(desc, samples);
        REQUIRE(buf.isValid());
        CHECK(buf.allocSize() == expected);
        CHECK(buf.size() == expected);
}

TEST_CASE("BufferAllocator: allocateAudioChunk(0 samples) returns invalid Buffer") {
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        AudioDesc            desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000, 2);
        CHECK_FALSE(alloc->allocateAudioChunk(desc, 0).isValid());
}

TEST_CASE("BufferAllocator: subclass override visible via name() and per-call counter") {
        struct CountingAllocator : public BufferAllocator {
                        PROMEKI_SHARED_DERIVED(CountingAllocator)
                        mutable std::atomic<int> bytesCalls{0};
                        mutable std::atomic<int> videoCalls{0};
                        mutable std::atomic<int> audioCalls{0};
                        String                   name() const override { return String("CountingAllocator"); }
                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                videoCalls.fetch_add(1);
                                return BufferAllocator::defaultAllocator()->allocateVideoPlane(desc, planeIndex);
                        }
                        Buffer allocateAudioChunk(const AudioDesc &desc, size_t samples) const override {
                                audioCalls.fetch_add(1);
                                return BufferAllocator::defaultAllocator()->allocateAudioChunk(desc, samples);
                        }
                        Buffer allocateBytes(size_t bytes, size_t align) const override {
                                bytesCalls.fetch_add(1);
                                return BufferAllocator::defaultAllocator()->allocateBytes(bytes, align);
                        }
        };
        BufferAllocator::Ptr alloc = BufferAllocator::Ptr::takeOwnership(new CountingAllocator());
        CHECK(alloc->name() == "CountingAllocator");
        Buffer    b = alloc->allocateBytes(2048);
        CHECK(b.isValid());
        ImageDesc id(640, 480, PixelFormat(PixelFormat::RGBA8_sRGB));
        Buffer    v = alloc->allocateVideoPlane(id, 0);
        CHECK(v.isValid());
        AudioDesc ad(AudioFormat(AudioFormat::PCMI_Float32LE), 48000, 2);
        Buffer    a = alloc->allocateAudioChunk(ad, 256);
        CHECK(a.isValid());
        // Confirm the override saw all three calls.  Use a downcast
        // through ptr() for the inspection — call sites at runtime
        // use sharedPointerCast or a typed Ptr alias.
        auto *typed = static_cast<const CountingAllocator *>(alloc.ptr());
        CHECK(typed->bytesCalls.load() == 1);
        CHECK(typed->videoCalls.load() == 1);
        CHECK(typed->audioCalls.load() == 1);
}

TEST_CASE("BufferAllocator: defaultAllocator concurrent allocateBytes is safe") {
        // The default allocator is stateless — confirm two threads
        // can each call allocateBytes without corrupting each other.
        BufferAllocator::Ptr alloc = BufferAllocator::defaultAllocator();
        std::atomic<int>     ok{0};
        auto                 worker = [&]() {
                for (int i = 0; i < 64; ++i) {
                        Buffer b = alloc->allocateBytes(4096);
                        if (b.isValid()) ok.fetch_add(1);
                }
        };
        std::thread t1(worker), t2(worker);
        t1.join();
        t2.join();
        CHECK(ok.load() == 128);
}
