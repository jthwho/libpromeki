/**
 * @file      ndilib.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/ndilib.h>
#include <promeki/list.h>
#include <promeki/logger.h>

#include <dlfcn.h>

// The dynamic-load shim defines the function table, the
// NDIlib_v6_load entry point, and pulls in the rest of the NDI SDK
// surface we need to call initialize/destroy/version through the
// table.  We deliberately confine this include to the .cpp so the
// header stays light.
#include <Processing.NDI.Lib.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Search order is most-specific first:
        //   1. Absolute path under PROMEKI_NDI_RUNTIME_DIR_STR — set by
        //      CMake when the SDK probe found a runtime tree under the
        //      SDK directory.  Lets dev builds against an extracted
        //      SDK work without LD_LIBRARY_PATH.
        //   2. Bare SONAMEs for the system loader to resolve — picks up
        //      packaged installs (e.g. /usr/lib/libndi.so.6) and
        //      anything on LD_LIBRARY_PATH.
        //   3. Unversioned fallbacks for odd installs that drop the
        //      .so.6 suffix.
        // Standard SDK before Advanced at every tier because users who
        // have only the Advanced runtime don't care which name matches,
        // but the reverse would silently shadow a standard install.
        struct LibCandidate {
                        String path;
                        bool   advanced;
        };

        List<LibCandidate> buildCandidateList() {
                List<LibCandidate> out;
#ifdef PROMEKI_NDI_RUNTIME_DIR_STR
                String runtimeDir = PROMEKI_NDI_RUNTIME_DIR_STR;
                if (!runtimeDir.isEmpty()) {
                        out.pushToBack({runtimeDir + "/libndi.so.6", false});
                        out.pushToBack({runtimeDir + "/libndi_advanced.so.6", true});
                }
#endif
                out.pushToBack({String("libndi.so.6"), false});
                out.pushToBack({String("libndi_advanced.so.6"), true});
                out.pushToBack({String("libndi.so"), false});
                out.pushToBack({String("libndi_advanced.so"), true});
                return out;
        }

} // namespace

NdiLib &NdiLib::instance() {
        static NdiLib s;
        return s;
}

NdiLib::NdiLib() {
        // Try each candidate in order.  RTLD_LOCAL keeps the NDI
        // library's symbols out of the global namespace so it can't
        // accidentally interpose on anything else (notably libc /
        // libstdc++ symbols the SDK happens to use).  RTLD_NOW
        // surfaces missing symbols at load time rather than the first
        // function call, which is the right trade since we're about
        // to immediately call NDIlib_v6_load through dlsym anyway.
        const List<LibCandidate> candidates = buildCandidateList();
        for (const auto &c : candidates) {
                _handle = dlopen(c.path.cstr(), RTLD_NOW | RTLD_LOCAL);
                if (_handle) {
                        _libraryPath = c.path;
                        _isAdvanced  = c.advanced;
                        break;
                }
        }
        if (!_handle) {
                promekiErr("NDI: failed to load runtime library — tried %d candidate(s) including "
                           "libndi.so.6 / libndi_advanced.so.6.  Last dlerror: %s",
                           (int)candidates.size(), dlerror());
                return;
        }

        using LoadFn = const NDIlib_v6 *(*)(void);
        auto loadFn = reinterpret_cast<LoadFn>(dlsym(_handle, "NDIlib_v6_load"));
        if (!loadFn) {
                promekiErr("NDI: dlsym(NDIlib_v6_load) failed in %s: %s", _libraryPath.cstr(), dlerror());
                dlclose(_handle);
                _handle = nullptr;
                _libraryPath.clear();
                return;
        }

        _api = loadFn();
        if (!_api) {
                promekiErr("NDI: NDIlib_v6_load() returned NULL in %s", _libraryPath.cstr());
                dlclose(_handle);
                _handle = nullptr;
                _libraryPath.clear();
                return;
        }

        // Per-process initialize.  Returns false on a CPU that lacks
        // SSE4.2 — extremely unlikely on any modern x86_64 box, but
        // the SDK promises this check exists so we honor it.
        if (_api->initialize) {
                if (!_api->initialize()) {
                        promekiErr("NDI: NDIlib_initialize() returned false (CPU not supported, "
                                   "or library probe failure)");
                        _api = nullptr;
                        dlclose(_handle);
                        _handle = nullptr;
                        _libraryPath.clear();
                        return;
                }
                _initialized = true;
        }

        if (_api->version) {
                const char *v = _api->version();
                if (v) _version = v;
        }

        promekiInfo("NDI: loaded %s%s (version %s)",
                    _libraryPath.cstr(),
                    _isAdvanced ? " [Advanced SDK]" : "",
                    _version.isEmpty() ? "?" : _version.cstr());
}

NdiLib::~NdiLib() {
        // Order: destroy NDI session first (drains internal threads),
        // then dlclose the library.  Doing it the other way around
        // unmaps code that's about to run.
        if (_initialized && _api && _api->destroy) {
                _api->destroy();
        }
        _api = nullptr;
        _initialized = false;
        if (_handle) {
                dlclose(_handle);
                _handle = nullptr;
        }
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
