/**
 * @file      cudabufferimpl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_CUDA

#include <promeki/bufferimpl.h>
#include <promeki/hostbufferimpl.h>
#include <promeki/buffercommand.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief BufferImpl backed by CUDA pinned host memory.
 * @ingroup util
 *
 * Allocates with @c cudaMallocHost so the region is page-locked and
 * directly accessible from both the host and any CUDA device.
 * Behaves like an ordinary host buffer for @ref MemDomain::Host
 * mapping requests.  Future work: opt in to direct
 * @ref MemDomain::CudaDevice access without a staging copy.
 */
class CudaHostBufferImpl : public HostMappedBufferImpl {
        public:
                /**
                 * @brief Allocates a CUDA pinned host buffer.
                 * @param ms    The MemSpace this buffer belongs to.
                 * @param bytes Requested allocation size in bytes.
                 * @param align Alignment requested by the caller (CUDA-pinned
                 *              allocations satisfy any reasonable alignment
                 *              themselves; the value is recorded for reporting).
                 */
                CudaHostBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

                /** @brief Releases the pinned allocation. */
                ~CudaHostBufferImpl() override;

                /** @brief Deep-copy clone — allocates a fresh pinned region and copies. */
                CudaHostBufferImpl *_promeki_clone() const override;
};

/**
 * @brief BufferImpl backed by CUDA device memory.
 * @ingroup util
 *
 * Allocates with @c cudaMalloc; the resulting pointer is not
 * dereferenceable from the host.  Reports
 * @c isMapped(MemDomain::CudaDevice) true at all times so the
 * buffer's native domain is always available.  Mapping to
 * @ref MemDomain::Host requires staging; the current implementation
 * returns @ref Error::NotImplemented for that path — Phase 5 wires
 * the staging buffer through the inter-MemSpace copy registry.
 */
class CudaDeviceBufferImpl : public BufferImpl {
        public:
                /**
                 * @brief Allocates a CUDA device buffer.
                 * @param ms    The MemSpace this buffer belongs to (CudaDevice).
                 * @param bytes Requested allocation size in bytes.
                 * @param align Alignment requested by the caller (cudaMalloc
                 *              already returns aligned pointers; the value is
                 *              recorded for reporting).
                 */
                CudaDeviceBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

                /** @brief Releases the device allocation. */
                ~CudaDeviceBufferImpl() override;

                MemSpace memSpace() const override { return _memSpace; }
                size_t   allocSize() const override { return _allocSize; }
                size_t   align() const override { return _align; }

                /** @brief Device memory has no host pointer. */
                void *mappedHostData() const override { return nullptr; }

                /**
                 * @brief Returns the raw CUDA device pointer (or nullptr
                 *        when allocation failed).
                 *
                 * The pointer is the base of the allocation — callers
                 * that observe @ref shift must add it themselves.  Used
                 * by the inter-MemSpace copy registry to drive
                 * @c cudaMemcpy without going through the host-mapping
                 * pathway (which is unsupported for device memory).
                 */
                void *devicePtr() const { return _devicePtr; }

                BufferRequest mapAcquire(MemDomain domain, MapFlags flags) override;
                BufferRequest mapRelease(MemDomain domain) override;

                /** @brief Fills the device region via @c cudaMemset. */
                Error fill(char value, size_t offset, size_t bytes) override;

                /** @brief Copies host data to the device via @c cudaMemcpy. */
                Error copyFromHost(const void *src, size_t bytes, size_t offset) override;

                /** @brief Deep-copy clone — allocates fresh device memory and DtoD copies. */
                CudaDeviceBufferImpl *_promeki_clone() const override;

        private:
                void    *_devicePtr = nullptr;
                size_t   _allocSize = 0;
                size_t   _align = 0;
                MemSpace _memSpace;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CUDA
