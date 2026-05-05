/**
 * @file      tests/unit/buffermap.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/buffercommand.h>
#include <promeki/bufferrequest.h>
#include <promeki/memdomain.h>
#include <promeki/memspace.h>

using namespace promeki;

TEST_CASE("Buffer::isMapped: host backends report Host mapped at construction") {
        Buffer buf(64);
        REQUIRE(buf.isValid());
        CHECK(buf.isHostAccessible());
        CHECK(buf.isMapped(MemDomain::Host));
        CHECK_FALSE(buf.isMapped(MemDomain::CudaDevice));
}

TEST_CASE("Buffer::mapAcquire(Host) on a host backend resolves inline") {
        Buffer        buf(64);
        BufferRequest req = buf.mapAcquire(MemDomain::Host, MapFlags::Read);
        CHECK(req.isReady());
        CHECK(req.wait() == Error::Ok);

        const auto *cmd = req.commandAs<BufferMapCommand>();
        REQUIRE(cmd != nullptr);
        CHECK(cmd->target == MemDomain::Host);
        CHECK(cmd->hostPtr != nullptr);
        CHECK(cmd->hostPtr == buf.data());
}

TEST_CASE("Buffer::mapAcquire(Host) bumps the per-domain refcount") {
        Buffer buf(64);
        // Construction seeds the Host refcount at 1; an explicit
        // acquire takes it to 2, and a release brings it back to 1.
        // The host pointer remains stable throughout.
        REQUIRE(buf.isMapped(MemDomain::Host));

        BufferRequest acq = buf.mapAcquire(MemDomain::Host);
        CHECK(acq.wait() == Error::Ok);
        CHECK(buf.isMapped(MemDomain::Host));

        BufferRequest rel = buf.mapRelease(MemDomain::Host);
        CHECK(rel.wait() == Error::Ok);
        CHECK(buf.isMapped(MemDomain::Host));
}

TEST_CASE("Buffer::mapAcquire(non-host) on a host backend reports NotSupported") {
        Buffer        buf(64);
        BufferRequest req = buf.mapAcquire(MemDomain::CudaDevice);
        CHECK(req.wait() == Error::NotSupported);
}

TEST_CASE("Buffer::mapRelease on an unmapped domain reports Invalid") {
        Buffer        buf(64);
        BufferRequest req = buf.mapRelease(MemDomain::CudaDevice);
        CHECK(req.wait() == Error::Invalid);
}

TEST_CASE("Buffer::mapAcquire on an invalid Buffer reports Invalid") {
        Buffer        buf;
        BufferRequest req = buf.mapAcquire();
        CHECK(req.wait() == Error::Invalid);
}
