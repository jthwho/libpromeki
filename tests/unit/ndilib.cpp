/**
 * @file      ndilib.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <doctest/doctest.h>
#include <promeki/ndilib.h>

using namespace promeki;

TEST_CASE("NdiLib: instance() is reachable and returns the same singleton each time") {
        NdiLib &a = NdiLib::instance();
        NdiLib &b = NdiLib::instance();
        CHECK(&a == &b);
}

TEST_CASE("NdiLib: when loaded, exposes a non-empty version string and api() table") {
        NdiLib &lib = NdiLib::instance();
        // The loader prints a clear error and leaves isLoaded() false on
        // boxes without an installed NDI runtime.  We don't fail the test
        // in that case — load failure is a deployment concern, not a
        // libpromeki bug — but everything else below assumes success.
        if (!lib.isLoaded()) {
                MESSAGE("NDI runtime not available on this host; skipping load-dependent checks");
                return;
        }
        // The function-pointer table is forward-declared in the public
        // header to keep the NDI SDK out of our public API; we can only
        // verify the loader wired things up to a non-null pointer here.
        // Deeper field-by-field checks live in the internal NDI sources.
        CHECK(lib.api() != nullptr);
        CHECK_FALSE(lib.libraryPath().isEmpty());
        CHECK_FALSE(lib.version().isEmpty());
}

TEST_CASE("NdiLib: load failure leaves api() null and version empty") {
        // We can't synthesise a load failure on a loaded box, so this
        // test just documents the contract — when isLoaded() is false,
        // api() is null and version() is empty.  Skips on hosts where
        // the runtime did load.
        NdiLib &lib = NdiLib::instance();
        if (lib.isLoaded()) {
                MESSAGE("NDI runtime loaded; load-failure invariants vacuously hold");
                return;
        }
        CHECK(lib.api() == nullptr);
        CHECK(lib.version().isEmpty());
}

#endif // PROMEKI_ENABLE_NDI
