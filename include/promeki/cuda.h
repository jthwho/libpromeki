/**
 * @file      cuda.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @defgroup cuda CUDA support
 * @ingroup  util
 *
 * The CUDA bindings in libpromeki are intentionally thin: they cover
 * exactly what a GPU-accelerated codec (NVENC, NVDEC, and anything
 * that follows) or a GPU CSC kernel needs from the library — device
 * enumeration, device-memory / pinned-host @ref MemSpace backends,
 * and a one-shot bootstrap that wires those backends into the
 * MemSpace registry.  Higher-level constructs (streams, events,
 * graphs) are the responsibility of the backend that uses them;
 * libpromeki does not try to be a general-purpose CUDA wrapper.
 *
 * The entire CUDA API surface is compiled in only when
 * @c PROMEKI_ENABLE_CUDA is defined.  When it is not, the public
 * declarations below still exist (so conditional callers do not need
 * to guard every call site), but they report no available devices
 * and the bootstrap returns @c Error::NotImplemented.  This keeps
 * downstream code portable between builds with and without the CUDA
 * toolkit.
 *
 * @{
 */

/**
 * @brief Lightweight wrapper around a CUDA device ordinal.
 *
 * @ref CudaDevice identifies a single GPU in a CUDA-enabled process
 * and gives callers the small bits of introspection they need
 * without pulling in the CUDA runtime header.  Construction with an
 * invalid ordinal yields an invalid device whose @c name() is empty
 * and whose @c totalMemory() is zero.
 */
class CudaDevice {
        public:
                /** @brief Constructs an invalid device handle. */
                CudaDevice() = default;

                /**
                 * @brief Constructs a handle referring to the device at the given ordinal.
                 * @param ordinal Zero-based device index as returned by
                 *                @c cudaGetDeviceCount.  Values outside
                 *                @c [0, deviceCount()) yield an invalid
                 *                device.
                 */
                explicit CudaDevice(int ordinal);

                /** @brief True when the CUDA runtime is linked in and at least one device is visible. */
                static bool isAvailable();

                /**
                 * @brief Returns the number of CUDA-capable devices visible to the process.
                 * @return Device count, or 0 when CUDA is unavailable.
                 */
                static int deviceCount();

                /**
                 * @brief Returns a handle to the currently-selected device.
                 * @return A valid @ref CudaDevice on success, invalid when
                 *         CUDA is unavailable or the query failed.
                 */
                static CudaDevice current();

                /**
                 * @brief Sets the currently-selected device for the calling thread.
                 *
                 * Subsequent allocations in the @ref MemSpace::CudaDevice
                 * space and transfers driven by the CUDA runtime use the
                 * selected device.  Each thread has its own current-device
                 * slot — callers that fan work across threads must set
                 * the device again on every worker.
                 *
                 * @param ordinal The device ordinal to select.
                 * @return @c Error::Ok on success, an error describing why
                 *         the device could not be selected otherwise.
                 */
                static Error setCurrent(int ordinal);

                /** @brief True when this handle refers to a valid device. */
                bool isValid() const { return _ordinal >= 0; }

                /** @brief Returns the zero-based device ordinal. */
                int ordinal() const { return _ordinal; }

                /** @brief Returns the device name (e.g. @c "NVIDIA RTX 5070 Laptop GPU"). */
                const String &name() const { return _name; }

                /** @brief Returns the total amount of device memory in bytes. */
                size_t totalMemory() const { return _totalMem; }

                /** @brief Returns the compute capability major version, or 0 when unknown. */
                int computeMajor() const { return _ccMajor; }

                /** @brief Returns the compute capability minor version, or 0 when unknown. */
                int computeMinor() const { return _ccMinor; }

        private:
                int     _ordinal   = -1;
                String  _name;
                size_t  _totalMem  = 0;
                int     _ccMajor   = 0;
                int     _ccMinor   = 0;
};

/**
 * @brief Process-wide CUDA bootstrap.
 *
 * CudaBootstrap is the single entry point that wires CUDA-backed
 * @ref MemSpace::CudaDevice and @ref MemSpace::CudaHost into the
 * MemSpace registry.  It is intentionally idempotent: call
 * @ref ensureRegistered from anywhere before you start allocating
 * device or pinned-host memory and the first call does the real
 * work; subsequent calls are no-ops.
 *
 * The bootstrap does @em not select a device — callers pick one with
 * @ref CudaDevice::setCurrent if they care which GPU is used.  When
 * no device is currently selected, CUDA defaults to device 0.
 */
class CudaBootstrap {
        public:
                /**
                 * @brief Registers the CUDA-backed MemSpaces on first call.
                 *
                 * Safe to call from any thread; registration runs under
                 * a static once-flag.  Returns @c Error::NotImplemented
                 * when the library was built without CUDA support.
                 *
                 * @return @c Error::Ok when registration completed (or
                 *         had already completed on a prior call).
                 */
                static Error ensureRegistered();

                /**
                 * @brief Returns true when the MemSpaces have been registered.
                 *
                 * Useful in tests that want to skip device-memory
                 * assertions when the bootstrap has not been run
                 * (e.g. because CUDA isn't available in the build
                 * environment).
                 */
                static bool isRegistered();
};

/** @} */

PROMEKI_NAMESPACE_END
