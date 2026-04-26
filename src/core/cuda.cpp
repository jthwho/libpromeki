/**
 * @file      cuda.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/cuda.h>
#include <promeki/memspace.h>
#include <promeki/logger.h>
#include <atomic>
#include <cstring>

#if PROMEKI_ENABLE_CUDA
#include <cuda_runtime.h>
#endif

PROMEKI_NAMESPACE_BEGIN

#if PROMEKI_ENABLE_CUDA

// ---------------------------------------------------------------------------
// CUDA error helpers.
// ---------------------------------------------------------------------------

// Maps a cudaError_t to the closest Error::Code we have.  Anything
// that isn't in the short list collapses to LibraryFailure, which is
// the convention used elsewhere in libpromeki for "third-party call
// failed and we have a string describing why".
static Error mapCudaError(cudaError_t e) {
        switch(e) {
                case cudaSuccess:                 return Error::Ok;
                case cudaErrorMemoryAllocation:   return Error::NoMem;
                case cudaErrorInvalidValue:       return Error::Invalid;
                case cudaErrorInvalidDevice:      return Error::Invalid;
                case cudaErrorNoDevice:           return Error::NotExist;
                default:                          return Error::LibraryFailure;
        }
}

static void logCudaError(const char *op, cudaError_t e) {
        promekiErr("CUDA: %s failed: %s (%d)", op, cudaGetErrorString(e), (int)e);
}

// ---------------------------------------------------------------------------
// MemSpace Ops for CudaDevice.
// ---------------------------------------------------------------------------

static bool cudaDeviceIsHostAccessible(const MemAllocation &) {
        // Device memory is not mappable into the host address space
        // through the runtime API.  Callers must stage through a
        // CudaHost (pinned) buffer or a System buffer with cudaMemcpy.
        return false;
}

static void cudaDeviceAlloc(MemAllocation &a) {
        // cudaMalloc guarantees at least 256-byte alignment (far larger
        // than any request we are likely to see here), so we do not
        // need to honour a.align explicitly.
        void *ptr = nullptr;
        cudaError_t e = cudaMalloc(&ptr, a.size);
        if(e != cudaSuccess) {
                logCudaError("cudaMalloc", e);
                a.ptr = nullptr;
                return;
        }
        a.ptr = ptr;
}

static void cudaDeviceRelease(MemAllocation &a) {
        if(a.ptr == nullptr) return;
        cudaError_t e = cudaFree(a.ptr);
        if(e != cudaSuccess) logCudaError("cudaFree", e);
}

// Unified copy that handles any combination of
// System/SystemSecure/CudaHost (host-side) and CudaDevice (device-side)
// on either end.  We inspect src/dst MemSpace IDs to pick the right
// cudaMemcpyKind.
static Error cudaKindCopy(const MemAllocation &src, const MemAllocation &dst, size_t bytes) {
        const MemSpace::ID sid = src.ms.id();
        const MemSpace::ID did = dst.ms.id();
        const bool srcDev = (sid == MemSpace::CudaDevice);
        const bool dstDev = (did == MemSpace::CudaDevice);
        cudaMemcpyKind kind;
        if(srcDev && dstDev)      kind = cudaMemcpyDeviceToDevice;
        else if(srcDev)           kind = cudaMemcpyDeviceToHost;
        else if(dstDev)           kind = cudaMemcpyHostToDevice;
        else                      kind = cudaMemcpyHostToHost;
        cudaError_t e = cudaMemcpy(dst.ptr, src.ptr, bytes, kind);
        if(e != cudaSuccess) {
                logCudaError("cudaMemcpy", e);
                return mapCudaError(e);
        }
        return Error::Ok;
}

static Error cudaDeviceFill(void *ptr, size_t bytes, char value) {
        // cudaMemset writes a byte value across device memory, matching
        // std::memset's semantics on the host side.  Unlike the System
        // fill we cannot fall through to memset because the pointer is
        // not host-dereferenceable.
        cudaError_t e = cudaMemset(ptr, static_cast<unsigned char>(value), bytes);
        if(e != cudaSuccess) {
                logCudaError("cudaMemset", e);
                return mapCudaError(e);
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// MemSpace Ops for CudaHost (pinned host memory).
// ---------------------------------------------------------------------------

static bool cudaHostIsHostAccessible(const MemAllocation &) {
        // Pinned host memory is CPU-dereferenceable; it just happens
        // to also be page-locked so the driver can DMA it directly.
        return true;
}

static void cudaHostAlloc(MemAllocation &a) {
        void *ptr = nullptr;
        cudaError_t e = cudaMallocHost(&ptr, a.size);
        if(e != cudaSuccess) {
                logCudaError("cudaMallocHost", e);
                a.ptr = nullptr;
                return;
        }
        a.ptr = ptr;
}

static void cudaHostRelease(MemAllocation &a) {
        if(a.ptr == nullptr) return;
        cudaError_t e = cudaFreeHost(a.ptr);
        if(e != cudaSuccess) logCudaError("cudaFreeHost", e);
}

static Error cudaHostFill(void *ptr, size_t bytes, char value) {
        // Pinned host memory is plain old bytes to the CPU.
        std::memset(ptr, value, bytes);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// CudaDevice introspection.
// ---------------------------------------------------------------------------

static bool tryPopulate(CudaDevice &out, int ordinal,
                        String &outName, size_t &outTotalMem,
                        int &outCcMajor, int &outCcMinor) {
        int count = 0;
        if(cudaGetDeviceCount(&count) != cudaSuccess) return false;
        if(ordinal < 0 || ordinal >= count) return false;
        cudaDeviceProp prop{};
        if(cudaGetDeviceProperties(&prop, ordinal) != cudaSuccess) return false;
        outName      = String(prop.name);
        outTotalMem  = prop.totalGlobalMem;
        outCcMajor   = prop.major;
        outCcMinor   = prop.minor;
        (void)out;
        return true;
}

#endif  // PROMEKI_ENABLE_CUDA

CudaDevice::CudaDevice(int ordinal) {
#if PROMEKI_ENABLE_CUDA
        if(tryPopulate(*this, ordinal, _name, _totalMem, _ccMajor, _ccMinor)) {
                _ordinal = ordinal;
        }
#else
        (void)ordinal;
#endif
}

bool CudaDevice::isAvailable() {
#if PROMEKI_ENABLE_CUDA
        int count = 0;
        if(cudaGetDeviceCount(&count) != cudaSuccess) return false;
        return count > 0;
#else
        return false;
#endif
}

int CudaDevice::deviceCount() {
#if PROMEKI_ENABLE_CUDA
        int count = 0;
        if(cudaGetDeviceCount(&count) != cudaSuccess) return 0;
        return count;
#else
        return 0;
#endif
}

CudaDevice CudaDevice::current() {
#if PROMEKI_ENABLE_CUDA
        int ordinal = 0;
        if(cudaGetDevice(&ordinal) != cudaSuccess) return CudaDevice();
        return CudaDevice(ordinal);
#else
        return CudaDevice();
#endif
}

Error CudaDevice::setCurrent(int ordinal) {
#if PROMEKI_ENABLE_CUDA
        cudaError_t e = cudaSetDevice(ordinal);
        if(e != cudaSuccess) {
                logCudaError("cudaSetDevice", e);
                return mapCudaError(e);
        }
        return Error::Ok;
#else
        (void)ordinal;
        return Error::NotImplemented;
#endif
}

// ---------------------------------------------------------------------------
// CudaBootstrap.
// ---------------------------------------------------------------------------

namespace {

// Tracks the result of the registration attempt so repeat callers get
// the same answer without retrying the whole sequence.  Written once
// under the once_flag, read afterward with relaxed semantics.
std::atomic<bool> _cudaRegistered{false};

} // namespace

bool CudaBootstrap::isRegistered() {
        return _cudaRegistered.load(std::memory_order_acquire);
}

Error CudaBootstrap::ensureRegistered() {
#if PROMEKI_ENABLE_CUDA
        if(_cudaRegistered.load(std::memory_order_acquire)) return Error::Ok;

        // registerData() is thread-safe internally, but we still want
        // to avoid racing two concurrent registrations so the atomic
        // flag accurately reflects whether the Ops are in the registry.
        static std::atomic_flag inFlight = ATOMIC_FLAG_INIT;
        while(inFlight.test_and_set(std::memory_order_acquire)) {
                if(_cudaRegistered.load(std::memory_order_acquire)) return Error::Ok;
        }

        if(_cudaRegistered.load(std::memory_order_acquire)) {
                inFlight.clear(std::memory_order_release);
                return Error::Ok;
        }

        {
                MemSpace::Ops ops{};
                ops.id               = MemSpace::CudaDevice;
                ops.name             = "CudaDevice";
                ops.isHostAccessible = cudaDeviceIsHostAccessible;
                ops.alloc            = cudaDeviceAlloc;
                ops.release          = cudaDeviceRelease;
                ops.copy             = cudaKindCopy;
                ops.fill             = cudaDeviceFill;
                MemSpace::registerData(std::move(ops));
        }
        {
                MemSpace::Ops ops{};
                ops.id               = MemSpace::CudaHost;
                ops.name             = "CudaHost";
                ops.isHostAccessible = cudaHostIsHostAccessible;
                ops.alloc            = cudaHostAlloc;
                ops.release          = cudaHostRelease;
                ops.copy             = cudaKindCopy;
                ops.fill             = cudaHostFill;
                MemSpace::registerData(std::move(ops));
        }

        _cudaRegistered.store(true, std::memory_order_release);
        inFlight.clear(std::memory_order_release);
        return Error::Ok;
#else
        return Error::NotImplemented;
#endif
}

PROMEKI_NAMESPACE_END
