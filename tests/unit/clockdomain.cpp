/**
 * @file      clockdomain.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/clockdomain.h>
#include <promeki/metadata.h>

using namespace promeki;

TEST_CASE("ClockDomain: default is invalid") {
        ClockDomain cd;
        CHECK_FALSE(cd.isValid());
        CHECK(cd.toString().isEmpty());
}

TEST_CASE("ClockDomain: well-known Synthetic") {
        ClockDomain cd(ClockDomain::Synthetic);
        CHECK(cd.isValid());
        CHECK(cd.name() == "Synthetic");
        CHECK(cd.toString() == "Synthetic");
        CHECK(cd.epoch() == ClockEpoch::PerStream);
        CHECK_FALSE(cd.isCrossStreamComparable());
        CHECK_FALSE(cd.isCrossMachineComparable());
}

TEST_CASE("ClockDomain: well-known SystemMonotonic") {
        ClockDomain cd(ClockDomain::SystemMonotonic);
        CHECK(cd.isValid());
        CHECK(cd.name() == "SystemMonotonic");
        CHECK(cd.toString() == "SystemMonotonic");
        CHECK(cd.epoch() == ClockEpoch::Correlated);
        CHECK(cd.isCrossStreamComparable());
        CHECK_FALSE(cd.isCrossMachineComparable());
}

TEST_CASE("ClockDomain: well-known domains are distinct") {
        ClockDomain syn(ClockDomain::Synthetic);
        ClockDomain mono(ClockDomain::SystemMonotonic);
        CHECK(syn != mono);
}

TEST_CASE("ClockDomain: same ID compares equal") {
        ClockDomain a(ClockDomain::Synthetic);
        ClockDomain b(ClockDomain::Synthetic);
        CHECK(a == b);
}

TEST_CASE("ClockDomain: registerDomain") {
        ClockDomain::ID id =
                ClockDomain::registerDomain("test.dynamic", "Dynamically registered test domain", ClockEpoch::Absolute);
        CHECK(id.isValid());

        ClockDomain cd(id);
        CHECK(cd.isValid());
        CHECK(cd.name() == "test.dynamic");
        CHECK(cd.description() == "Dynamically registered test domain");
        CHECK(cd.epoch() == ClockEpoch::Absolute);
        CHECK(cd.isCrossStreamComparable());
        CHECK(cd.isCrossMachineComparable());
}

TEST_CASE("ClockDomain: registerDomain returns existing for duplicate name") {
        ClockDomain::ID id1 = ClockDomain::registerDomain("test.dedup", "First", ClockEpoch::PerStream);
        ClockDomain::ID id2 = ClockDomain::registerDomain("test.dedup", "Second", ClockEpoch::Absolute);
        CHECK(id1 == id2);
        // First registration wins
        ClockDomain cd(id2);
        CHECK(cd.epoch() == ClockEpoch::PerStream);
}

TEST_CASE("ClockDomain: lookup by name") {
        ClockDomain found = ClockDomain::lookup("Synthetic");
        CHECK(found.isValid());
        CHECK(found == ClockDomain(ClockDomain::Synthetic));

        ClockDomain missing = ClockDomain::lookup("nonexistent");
        CHECK_FALSE(missing.isValid());
}

TEST_CASE("ClockDomain: registeredIDs") {
        ClockDomain::IDList ids = ClockDomain::registeredIDs();
        bool                hasSynthetic = false;
        bool                hasMono = false;
        for (size_t i = 0; i < ids.size(); ++i) {
                if (ids[i] == ClockDomain::Synthetic) hasSynthetic = true;
                if (ids[i] == ClockDomain::SystemMonotonic) hasMono = true;
        }
        CHECK(hasSynthetic);
        CHECK(hasMono);
}

TEST_CASE("ClockDomain: copy is pointer-identity") {
        ClockDomain original(ClockDomain::Synthetic);
        ClockDomain copy = original;
        CHECK(copy == original);
        CHECK(copy.data() == original.data());
}

TEST_CASE("ClockDomain: implicit construction from ID") {
        ClockDomain cd = ClockDomain::Synthetic;
        CHECK(cd.isValid());
        CHECK(cd.epoch() == ClockEpoch::PerStream);
}

TEST_CASE("ClockDomain: setDomainMetadata") {
        ClockDomain::ID id =
                ClockDomain::registerDomain("test.with.meta", "Domain with metadata", ClockEpoch::Absolute);
        Metadata meta;
        meta.set(Metadata::FrameRate, 30.0);
        ClockDomain::setDomainMetadata(id, meta);

        ClockDomain cd(id);
        CHECK(cd.metadata().contains(Metadata::FrameRate));
}
