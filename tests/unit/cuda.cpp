/**
 * @file      tests/cuda.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the CUDA bootstrap and the CudaDevice / CudaHost
 * @ref MemSpace backends.  Deliberately defensive about whether the
 * process actually has a usable GPU: every device-backed assertion
 * is gated on @c CudaDevice::isAvailable(), so the suite passes on
 * CUDA-less CI runners (the build-flag-off case) and on CUDA-enabled
 * but device-less environments alike.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>
#include <promeki/cuda.h>
#include <promeki/memspace.h>
#include <promeki/buffer.h>
#include <cstring>

using namespace promeki;

TEST_CASE("CudaDevice: introspection is safe with or without CUDA") {
        // isAvailable / deviceCount must not throw, must not crash,
        // and must never report devices when CUDA is compiled out.
        const bool avail = CudaDevice::isAvailable();
        const int  count = CudaDevice::deviceCount();

#if PROMEKI_ENABLE_CUDA
        // With CUDA compiled in, availability tracks deviceCount > 0.
        CHECK(avail == (count > 0));
#else
        CHECK_FALSE(avail);
        CHECK(count == 0);
#endif

        // Out-of-range ordinals always yield an invalid device.
        CudaDevice bogus(99999);
        CHECK_FALSE(bogus.isValid());
}

TEST_CASE("CudaBootstrap: ensureRegistered behaves correctly regardless of build") {
        Error err = CudaBootstrap::ensureRegistered();
#if PROMEKI_ENABLE_CUDA
        CHECK(err == Error::Ok);
        CHECK(CudaBootstrap::isRegistered());
        // The CUDA MemSpace IDs must now be distinct from System's Ops
        // (i.e. lookup() is returning the real registered entries
        // rather than the System fallback).  We check via the human
        // readable name, which both backends populate.
        CHECK(MemSpace(MemSpace::CudaDevice).name() == String("CudaDevice"));
        CHECK(MemSpace(MemSpace::CudaHost).name()   == String("CudaHost"));
#else
        CHECK(err == Error::NotImplemented);
        CHECK_FALSE(CudaBootstrap::isRegistered());
        // Without CUDA the enum values exist, but lookup() falls back
        // to System so the name reports as "System".  The fallback is
        // deliberately silent — we just want to document it.
        CHECK(MemSpace(MemSpace::CudaDevice).name() == String("System"));
        CHECK(MemSpace(MemSpace::CudaHost).name()   == String("System"));
#endif
}

#if PROMEKI_ENABLE_CUDA

TEST_CASE("CudaDevice: current() reports a valid device when one is present") {
        if(!CudaDevice::isAvailable()) return;

        REQUIRE(CudaBootstrap::ensureRegistered() == Error::Ok);
        // cudaSetDevice(0) anchors subsequent allocations; without this
        // the current-device slot could be unset in a pristine process.
        REQUIRE(CudaDevice::setCurrent(0) == Error::Ok);

        CudaDevice cur = CudaDevice::current();
        REQUIRE(cur.isValid());
        CHECK(cur.ordinal() == 0);
        CHECK_FALSE(cur.name().isEmpty());
        CHECK(cur.totalMemory() > 0);
        // Any real NVIDIA GPU has a compute capability of at least 3.0
        // (Kepler), and libpromeki targets modern GPUs.  We just
        // confirm the major version was populated.
        CHECK(cur.computeMajor() >= 3);
}

TEST_CASE("CudaHost + CudaDevice: round-trip at the MemSpace level") {
        if(!CudaDevice::isAvailable()) return;

        REQUIRE(CudaBootstrap::ensureRegistered() == Error::Ok);
        REQUIRE(CudaDevice::setCurrent(0) == Error::Ok);

        constexpr size_t kBytes = 4096;
        constexpr size_t kAlign = 64;

        MemSpace hostSpace(MemSpace::CudaHost);
        MemSpace devSpace (MemSpace::CudaDevice);

        // Allocate pinned host staging and device memory directly
        // through MemSpace so we have MemAllocation records to pass
        // into MemSpace::copy().  Buffer would also work but keeps
        // internal state we don't want to reach into for the test.
        MemAllocation hostAlloc = hostSpace.alloc(kBytes, kAlign);
        MemAllocation devAlloc  = devSpace.alloc(kBytes, kAlign);
        REQUIRE(hostAlloc.isValid());
        REQUIRE(devAlloc.isValid());

        // Pinned host is CPU-dereferenceable; device memory is not.
        CHECK(hostSpace.isHostAccessible(hostAlloc));
        CHECK_FALSE(devSpace.isHostAccessible(devAlloc));

        // Fill host buffer with a recognisable pattern.
        REQUIRE(hostSpace.fill(hostAlloc.ptr, kBytes, 0x7F).isOk());

        // Upload host -> device, zero the host side, then download
        // back so the verified bytes had to travel through the GPU.
        CHECK(hostSpace.copy(hostAlloc, devAlloc, kBytes).isOk());
        REQUIRE(hostSpace.fill(hostAlloc.ptr, kBytes, 0x00).isOk());
        CHECK(devSpace.copy(devAlloc, hostAlloc, kBytes).isOk());

        const auto *bytes = static_cast<const uint8_t *>(hostAlloc.ptr);
        for(size_t i = 0; i < kBytes; ++i) {
                CHECK(bytes[i] == 0x7F);
        }

        hostSpace.release(hostAlloc);
        devSpace.release(devAlloc);
}

TEST_CASE("CudaDevice fill: cudaMemset honours the value byte") {
        if(!CudaDevice::isAvailable()) return;

        REQUIRE(CudaBootstrap::ensureRegistered() == Error::Ok);
        REQUIRE(CudaDevice::setCurrent(0) == Error::Ok);

        constexpr size_t kBytes = 1024;
        constexpr size_t kAlign = 64;

        MemSpace devSpace (MemSpace::CudaDevice);
        MemSpace hostSpace(MemSpace::CudaHost);

        MemAllocation devAlloc  = devSpace.alloc(kBytes, kAlign);
        MemAllocation hostAlloc = hostSpace.alloc(kBytes, kAlign);
        REQUIRE(devAlloc.isValid());
        REQUIRE(hostAlloc.isValid());

        REQUIRE(devSpace.fill(devAlloc.ptr, kBytes, 0x55).isOk());
        REQUIRE(hostSpace.fill(hostAlloc.ptr, kBytes, 0x00).isOk());
        CHECK(devSpace.copy(devAlloc, hostAlloc, kBytes).isOk());

        const auto *bytes = static_cast<const uint8_t *>(hostAlloc.ptr);
        for(size_t i = 0; i < kBytes; ++i) {
                CHECK(bytes[i] == 0x55);
        }

        devSpace.release(devAlloc);
        hostSpace.release(hostAlloc);
}

#endif  // PROMEKI_ENABLE_CUDA
