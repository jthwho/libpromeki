/**
 * @file      tests/unit/buffercopy.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/buffercommand.h>
#include <promeki/bufferfactory.h>
#include <promeki/bufferrequest.h>
#include <promeki/memspace.h>

using namespace promeki;

TEST_CASE("Buffer::copyTo: host-to-host falls back to memcpy") {
        Buffer src(64);
        Buffer dst(64);
        REQUIRE(src.isValid());
        REQUIRE(dst.isValid());
        std::memset(src.data(), 0xA5, 64);
        std::memset(dst.data(), 0x00, 64);

        BufferRequest req = src.copyTo(dst, 64);
        CHECK(req.isReady());
        CHECK(req.wait() == Error::Ok);

        const auto *cmd = req.commandAs<BufferCopyCommand>();
        REQUIRE(cmd != nullptr);
        CHECK(cmd->bytes == 64);
        CHECK(cmd->srcOffset == 0);
        CHECK(cmd->dstOffset == 0);

        for (size_t i = 0; i < 64; ++i) {
                CHECK(static_cast<const uint8_t *>(dst.data())[i] == 0xA5);
        }
}

TEST_CASE("Buffer::copyTo: respects offsets and partial copies") {
        Buffer src(128);
        Buffer dst(128);
        REQUIRE(src.isValid());
        REQUIRE(dst.isValid());
        std::memset(src.data(), 0x00, 128);
        std::memset(dst.data(), 0x00, 128);
        for (size_t i = 0; i < 128; ++i) {
                static_cast<uint8_t *>(src.data())[i] = static_cast<uint8_t>(i);
        }

        BufferRequest req = src.copyTo(dst, /*bytes*/ 32, /*srcOffset*/ 16, /*dstOffset*/ 64);
        CHECK(req.wait() == Error::Ok);

        // Bytes 0..63 of dst untouched (zero), 64..95 copied from src 16..47, 96..127 untouched.
        const uint8_t *d = static_cast<const uint8_t *>(dst.data());
        for (size_t i = 0; i < 64; ++i) CHECK(d[i] == 0x00);
        for (size_t i = 0; i < 32; ++i) CHECK(d[64 + i] == static_cast<uint8_t>(16 + i));
        for (size_t i = 96; i < 128; ++i) CHECK(d[i] == 0x00);
}

TEST_CASE("Buffer::copyTo: rejects out-of-range") {
        Buffer src(64);
        Buffer dst(64);
        REQUIRE(src.isValid());
        REQUIRE(dst.isValid());

        // dstOffset + bytes > dst.availSize
        BufferRequest req1 = src.copyTo(dst, 64, 0, 8);
        CHECK(req1.wait() == Error::BufferTooSmall);

        // srcOffset + bytes > src.availSize
        BufferRequest req2 = src.copyTo(dst, 64, 8, 0);
        CHECK(req2.wait() == Error::BufferTooSmall);
}

TEST_CASE("Buffer::copyTo: zero-byte copy is a no-op success") {
        Buffer src(64);
        Buffer dst(64);
        BufferRequest req = src.copyTo(dst, 0);
        CHECK(req.wait() == Error::Ok);
}

TEST_CASE("Buffer::copyTo: invalid source returns Invalid") {
        Buffer src;
        Buffer dst(64);
        BufferRequest req = src.copyTo(dst, 32);
        CHECK(req.wait() == Error::Invalid);
}

namespace {

// Test-side state — tracks how many times the registered fn fired so
// the registry-dispatch path can be verified.  The fn does its own
// memcpy under the hood, but the framework should pick this up
// instead of falling back to the generic host memcpy.
int testCopyCallCount = 0;

Error testCopyFn(const Buffer &src, Buffer &dst, size_t bytes, size_t srcOffset, size_t dstOffset) {
        ++testCopyCallCount;
        if (!src.isValid() || !dst.isValid()) return Error::Invalid;
        std::memcpy(static_cast<uint8_t *>(dst.data()) + dstOffset,
                    static_cast<const uint8_t *>(src.data()) + srcOffset, bytes);
        return Error::Ok;
}

} // namespace

TEST_CASE("registerBufferCopy: registered fn wins over the host-mapped fallback") {
        // We register an entry for (System, System) and verify that
        // it's picked up.  Restoring the prior nullptr at the end
        // keeps the test isolated from the rest of the suite — the
        // generic host-mapped fallback is the natural state.
        BufferCopyFn prior = lookupBufferCopy(MemSpace::System, MemSpace::System);
        registerBufferCopy(MemSpace::System, MemSpace::System, testCopyFn);
        testCopyCallCount = 0;

        Buffer src(32);
        Buffer dst(32);
        std::memset(src.data(), 0x5A, 32);
        std::memset(dst.data(), 0x00, 32);
        BufferRequest req = src.copyTo(dst, 32);
        CHECK(req.wait() == Error::Ok);
        CHECK(testCopyCallCount == 1);
        for (size_t i = 0; i < 32; ++i) {
                CHECK(static_cast<const uint8_t *>(dst.data())[i] == 0x5A);
        }

        // Restore — registerBufferCopy(..., nullptr) removes the
        // entry by overwriting; future lookups return nullptr again
        // and the generic host-mapped fallback resumes.
        registerBufferCopy(MemSpace::System, MemSpace::System, prior);
}
