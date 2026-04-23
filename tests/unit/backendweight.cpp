/**
 * @file      tests/backendweight.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tiny coverage for the @ref BackendWeight namespace constants.  The
 * codec encoder/decoder registries lean on these tiers being widely
 * spaced and strictly ordered (Vendored < System < User) so registry
 * sort/selection tests remain stable as new backends register at
 * intermediate weights — pin the contract here.
 */

#include <doctest/doctest.h>
#include <promeki/backendweight.h>

using namespace promeki;

TEST_CASE("BackendWeight: standard tiers are strictly ordered Vendored < System < User") {
        CHECK(BackendWeight::Vendored < BackendWeight::System);
        CHECK(BackendWeight::System   < BackendWeight::User);
}

TEST_CASE("BackendWeight: tiers leave room for inter-tier intermediate values") {
        // The spacing rationale: a backend can land between two
        // standard tiers (Vendored + 50, etc.) without crossing into
        // the next.  Bands therefore must be wide enough to fit at
        // least a +50 bump.
        CHECK(BackendWeight::Vendored + 50 < BackendWeight::System);
        CHECK(BackendWeight::System   + 50 < BackendWeight::User);
}

TEST_CASE("BackendWeight: well-known tier values are positive") {
        // Negative weights would never be selected against the default
        // sort comparator (descending) — pin that they're > 0.
        CHECK(BackendWeight::Vendored > 0);
        CHECK(BackendWeight::System   > 0);
        CHECK(BackendWeight::User     > 0);
}
