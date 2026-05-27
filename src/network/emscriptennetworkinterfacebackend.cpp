/**
 * @file      emscriptennetworkinterfacebackend.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#include <promeki/networkinterfacebackend.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // WASM has no notion of host interfaces in the libpromeki
        // sense — every API call goes through the JavaScript
        // runtime.  Register an empty backend so
        // @ref NetworkInterfaceBackend::registeredBackends still
        // returns a non-empty list when running under Emscripten,
        // letting consumers distinguish "WASM build" from "broken
        // registry" without a #ifdef of their own.
        class EmscriptenNetworkInterfaceBackend : public NetworkInterfaceBackend {
                public:
                        String   name() const override { return String("wasm"); }
                        int      priority() const override { return 100; }
                        ImplList enumerate() const override { return ImplList(); }
        };

        struct Registrar {
                Registrar() {
                        NetworkInterfaceBackend::registerBackend(new EmscriptenNetworkInterfaceBackend());
                }
        };
        Registrar gEmscriptenRegistrar;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_PLATFORM_EMSCRIPTEN
