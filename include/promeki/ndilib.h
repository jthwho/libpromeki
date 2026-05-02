/**
 * @file      ndilib.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/string.h>

// Forward-declare the SDK's function-pointer table so callers that only
// need NdiLib::api() can include this header without dragging in the
// full NDI SDK headers (which are large and pollute the global
// namespace with C macros).  The struct is defined in
// Processing.NDI.DynamicLoad.h; users that actually call NDI functions
// through the table will include that header alongside this one.
struct NDIlib_v6;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Process-wide NDI library bootstrap.
 * @ingroup proav
 *
 * Singleton that owns the dlopen'd NDI runtime library and the
 * `NDIlib_v6_load()` function-pointer table for the lifetime of the
 * process.  All other NDI code in libpromeki goes through this — the
 * table is exposed via @ref api().
 *
 * @par Library selection
 *
 * On first @ref instance() call, the loader probes for the NDI runtime
 * in this order, taking the first one that succeeds:
 *
 *  1. `libndi.so.6`           (standard NDI SDK, current major)
 *  2. `libndi_advanced.so.6`  (NDI Advanced SDK, current major)
 *  3. `libndi.so`             (unversioned fallback)
 *  4. `libndi_advanced.so`    (unversioned fallback)
 *
 * The Advanced SDK is API-compatible with the standard SDK at the
 * function-table level, so the same @c NDIlib_v6 pointer table works
 * with either runtime.  See `docs/ndi.md` for installation guidance.
 *
 * @par Lifetime
 *
 * The singleton constructs lazily on first @ref instance() call.
 * It calls `NDIlib_initialize()` once during construction and
 * `NDIlib_destroy()` from its destructor (i.e. on process exit).
 * Once constructed it never tears down — there is no `shutdown()`
 * surface.
 *
 * @par Failure handling
 *
 * If the runtime library cannot be located or `NDIlib_v6_load()`
 * fails, @ref instance() returns a non-null singleton whose
 * @ref isLoaded() reports @c false and whose @ref api() returns
 * @c nullptr.  Callers should check @c isLoaded() before
 * dereferencing the table.  This pattern lets the rest of the
 * library report a clean error at MediaIO open time rather than
 * crash at static-init.
 */
class NdiLib {
        public:
                /**
                 * @brief Returns the singleton, constructing it on first call.
                 *
                 * Thread-safe (Meyers singleton).  Lazy — no work happens
                 * until something actually asks for the bootstrap.
                 */
                static NdiLib &instance();

                /**
                 * @brief True when the runtime is loaded and `api()` is non-null.
                 *
                 * False when dlopen failed or `NDIlib_v6_load()` returned
                 * `NULL`.  When false, every other accessor returns a
                 * default-empty value.
                 */
                bool isLoaded() const { return _api != nullptr; }

                /**
                 * @brief Returns the NDI function-pointer table, or nullptr on load failure.
                 *
                 * The pointer is valid for the lifetime of the process once
                 * non-null.  Callers must include
                 * `<Processing.NDI.DynamicLoad.h>` to use the struct's
                 * function pointers.
                 */
                const NDIlib_v6 *api() const { return _api; }

                /**
                 * @brief Path of the runtime library that was loaded, or empty on failure.
                 */
                const String &libraryPath() const { return _libraryPath; }

                /**
                 * @brief NDI runtime version string from `NDIlib_version()`, or empty on failure.
                 */
                const String &version() const { return _version; }

                /**
                 * @brief True when the loaded runtime is the Advanced SDK.
                 *
                 * Inferred from the loaded library filename containing
                 * `_advanced`.  Cosmetic — the function table is the same
                 * between SDK flavours.
                 */
                bool isAdvanced() const { return _isAdvanced; }

                NdiLib(const NdiLib &) = delete;
                NdiLib &operator=(const NdiLib &) = delete;

        private:
                NdiLib();
                ~NdiLib();

                void *_handle = nullptr;       ///< dlopen handle, owned.
                const NDIlib_v6 *_api = nullptr; ///< function table, owned by the SDK.
                String _libraryPath;
                String _version;
                bool   _isAdvanced = false;
                bool   _initialized = false;   ///< NDIlib_initialize() succeeded.
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
