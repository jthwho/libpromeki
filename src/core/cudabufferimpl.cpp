/**
 * @file      cudabufferimpl.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cudabufferimpl.h>

#if PROMEKI_ENABLE_CUDA

#include <cuda_runtime.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

Error mapCudaError(cudaError_t e) {
        switch (e) {
                case cudaSuccess: return Error::Ok;
                case cudaErrorMemoryAllocation: return Error::NoMem;
                case cudaErrorInvalidValue: return Error::Invalid;
                case cudaErrorInvalidDevice: return Error::Invalid;
                case cudaErrorNoDevice: return Error::NotExist;
                default: return Error::LibraryFailure;
        }
}

void logCudaError(const char *op, cudaError_t e) {
        promekiErr("CUDA: %s failed: %s (%d)", op, cudaGetErrorString(e), static_cast<int>(e));
}

BufferRequest resolvedMap(MemDomain target, MapFlags flags, void *hostPtr, Error result) {
        auto *cmd = new BufferMapCommand();
        cmd->target = target;
        cmd->flags = flags;
        cmd->hostPtr = hostPtr;
        cmd->result = result;
        return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
}

BufferRequest resolvedUnmap(MemDomain target, Error result) {
        auto *cmd = new BufferUnmapCommand();
        cmd->target = target;
        cmd->result = result;
        return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
}

} // namespace

// ---------------------------------------------------------------------------
// CudaHostBufferImpl
// ---------------------------------------------------------------------------

CudaHostBufferImpl::CudaHostBufferImpl(const MemSpace &ms, size_t bytes, size_t align)
    : HostMappedBufferImpl(ms, nullptr, 0, align) {
        void       *ptr = nullptr;
        cudaError_t e = cudaMallocHost(&ptr, bytes);
        if (e != cudaSuccess) {
                logCudaError("cudaMallocHost", e);
                ms.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        _hostPtr = ptr;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

CudaHostBufferImpl::~CudaHostBufferImpl() {
        if (_hostPtr == nullptr) return;
        const uint64_t bytes = static_cast<uint64_t>(_allocSize);
        cudaError_t    e = cudaFreeHost(_hostPtr);
        if (e != cudaSuccess) logCudaError("cudaFreeHost", e);
        _memSpace.stats().recordRelease(bytes);
}

CudaHostBufferImpl *CudaHostBufferImpl::_promeki_clone() const {
        auto *clone = new CudaHostBufferImpl(_memSpace, _allocSize, _align);
        if (clone->_hostPtr != nullptr && _hostPtr != nullptr && _allocSize > 0) {
                std::memcpy(clone->_hostPtr, _hostPtr, _allocSize);
        }
        clone->_logicalSize = _logicalSize;
        clone->_shift = _shift;
        return clone;
}

// ---------------------------------------------------------------------------
// CudaDeviceBufferImpl
// ---------------------------------------------------------------------------

CudaDeviceBufferImpl::CudaDeviceBufferImpl(const MemSpace &ms, size_t bytes, size_t align)
    : _allocSize(0), _align(align), _memSpace(ms) {
        void       *ptr = nullptr;
        cudaError_t e = cudaMalloc(&ptr, bytes);
        if (e != cudaSuccess) {
                logCudaError("cudaMalloc", e);
                ms.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        _devicePtr = ptr;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
        // CUDA device memory is permanently mapped to the CudaDevice
        // domain — its native domain.  Seeding the refcount means
        // isMapped(CudaDevice) reports true for the buffer's lifetime
        // without any explicit acquire.
        seedMapRefcount(MemDomain::CudaDevice, 1);
}

CudaDeviceBufferImpl::~CudaDeviceBufferImpl() {
        if (_devicePtr == nullptr) return;
        const uint64_t bytes = static_cast<uint64_t>(_allocSize);
        cudaError_t    e = cudaFree(_devicePtr);
        if (e != cudaSuccess) logCudaError("cudaFree", e);
        _memSpace.stats().recordRelease(bytes);
}

BufferRequest CudaDeviceBufferImpl::mapAcquire(MemDomain domain, MapFlags flags) {
        if (domain.id() == MemDomain::CudaDevice) {
                incrementMapRefcount(MemDomain::CudaDevice);
                return resolvedMap(domain, flags, nullptr, Error::Ok);
        }
        if (domain.id() == MemDomain::Host) {
                // Phase 5 wires this through the inter-MemSpace copy
                // registry by staging into pinned host memory.  Phase
                // 2 reports unsupported so callers know the path is
                // not yet wired.
                return resolvedMap(domain, flags, nullptr, Error::NotImplemented);
        }
        return resolvedMap(domain, flags, nullptr, Error::NotSupported);
}

BufferRequest CudaDeviceBufferImpl::mapRelease(MemDomain domain) {
        if (domain.id() != MemDomain::CudaDevice) return resolvedUnmap(domain, Error::Invalid);
        int newCount = decrementMapRefcount(MemDomain::CudaDevice);
        return resolvedUnmap(domain, newCount < 0 ? Error::Invalid : Error::Ok);
}

Error CudaDeviceBufferImpl::fill(char value, size_t offset, size_t bytes) {
        if (_devicePtr == nullptr) return Error::Invalid;
        cudaError_t e = cudaMemset(static_cast<uint8_t *>(_devicePtr) + offset,
                                   static_cast<unsigned char>(value), bytes);
        if (e != cudaSuccess) {
                logCudaError("cudaMemset", e);
                return mapCudaError(e);
        }
        return Error::Ok;
}

Error CudaDeviceBufferImpl::copyFromHost(const void *src, size_t bytes, size_t offset) {
        if (_devicePtr == nullptr) return Error::Invalid;
        cudaError_t e = cudaMemcpy(static_cast<uint8_t *>(_devicePtr) + offset, src, bytes, cudaMemcpyHostToDevice);
        if (e != cudaSuccess) {
                logCudaError("cudaMemcpy", e);
                return mapCudaError(e);
        }
        return Error::Ok;
}

CudaDeviceBufferImpl *CudaDeviceBufferImpl::_promeki_clone() const {
        auto *clone = new CudaDeviceBufferImpl(_memSpace, _allocSize, _align);
        if (clone->_devicePtr != nullptr && _devicePtr != nullptr && _allocSize > 0) {
                cudaError_t e = cudaMemcpy(clone->_devicePtr, _devicePtr, _allocSize, cudaMemcpyDeviceToDevice);
                if (e != cudaSuccess) {
                        // Returning a clone backed by uninitialized
                        // device memory would silently corrupt every
                        // downstream consumer.  Drop the partial clone
                        // (the destructor frees the cudaMalloc) and
                        // surface the failure as a null detach result;
                        // callers see an invalidated Buffer rather than
                        // garbage bytes.
                        logCudaError("cudaMemcpy(DeviceToDevice)", e);
                        delete clone;
                        return nullptr;
                }
        }
        clone->_logicalSize = _logicalSize;
        clone->_shift = _shift;
        return clone;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CUDA
