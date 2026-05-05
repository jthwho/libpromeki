/**
 * @file      tests/unit/memdomain.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/memdomain.h>

using namespace promeki;

TEST_CASE("MemDomain: built-in IDs map to the expected names") {
        CHECK(MemDomain(MemDomain::Host).name() == "Host");
        CHECK(MemDomain(MemDomain::CudaDevice).name() == "CudaDevice");
        CHECK(MemDomain(MemDomain::FpgaDevice).name() == "FpgaDevice");
}

TEST_CASE("MemDomain: default constructor is Host") {
        MemDomain d;
        CHECK(d.id() == MemDomain::Host);
        CHECK(d.name() == "Host");
}

TEST_CASE("MemDomain: equality is identity-based") {
        MemDomain a(MemDomain::Host);
        MemDomain b(MemDomain::Host);
        MemDomain c(MemDomain::CudaDevice);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("MemDomain: registerType issues unique IDs at or above UserDefined") {
        MemDomain::ID first = MemDomain::registerType();
        MemDomain::ID second = MemDomain::registerType();
        CHECK(first >= MemDomain::UserDefined);
        CHECK(second > first);
}

TEST_CASE("MemDomain: registerData makes a custom domain visible to lookup") {
        MemDomain::ID custom = MemDomain::registerType();
        MemDomain::registerData(MemDomain::Data{custom, String("MyCustomDomain")});

        MemDomain d(custom);
        CHECK(d.id() == custom);
        CHECK(d.name() == "MyCustomDomain");

        // The ID appears in the registered list.
        auto ids = MemDomain::registeredIDs();
        bool found = false;
        for (size_t i = 0; i < ids.size(); ++i) {
                if (ids[i] == custom) {
                        found = true;
                        break;
                }
        }
        CHECK(found);
}
