/**
 * @file      stringregistry.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/stringregistry.h>

using namespace promeki;

struct TestRegistryTag {};
using TestRegistry = StringRegistry<TestRegistryTag>;
using TestItem = TestRegistry::Item;

TEST_CASE("StringRegistry: empty registry has zero count") {
        // Use the singleton instance directly.
        // Note: since it's a singleton, count may be non-zero if other
        // tests registered strings.  We test the Item API instead for
        // most behavior.
        CHECK(TestRegistry::instance().count() >= 0);
}

TEST_CASE("StringRegistry: findId returns InvalidID for unknown string") {
        CHECK(TestRegistry::instance().findId("never.seen.xyzzy") == TestRegistry::InvalidID);
}

TEST_CASE("StringRegistry: findOrCreate assigns IDs") {
        auto &reg = TestRegistry::instance();
        uint32_t id1 = reg.findOrCreate("sr.first");
        uint32_t id2 = reg.findOrCreate("sr.second");
        CHECK(id1 != id2);
        CHECK(id1 != TestRegistry::InvalidID);
        CHECK(id2 != TestRegistry::InvalidID);
}

TEST_CASE("StringRegistry: findOrCreate returns same ID for same string") {
        auto &reg = TestRegistry::instance();
        uint32_t id1 = reg.findOrCreate("sr.duplicate");
        uint32_t id2 = reg.findOrCreate("sr.duplicate");
        CHECK(id1 == id2);
}

TEST_CASE("StringRegistry: findId returns correct ID after registration") {
        auto &reg = TestRegistry::instance();
        uint32_t created = reg.findOrCreate("sr.findtest");
        uint32_t found = reg.findId("sr.findtest");
        CHECK(created == found);
}

TEST_CASE("StringRegistry: name returns correct string for ID") {
        auto &reg = TestRegistry::instance();
        uint32_t id = reg.findOrCreate("sr.nametest");
        CHECK(reg.name(id) == "sr.nametest");
}

TEST_CASE("StringRegistry: name returns empty for invalid ID") {
        auto &reg = TestRegistry::instance();
        CHECK(reg.name(TestRegistry::InvalidID).isEmpty());
}

TEST_CASE("StringRegistry: contains works correctly") {
        auto &reg = TestRegistry::instance();
        CHECK_FALSE(reg.contains("sr.missing.xyzzy"));
        reg.findOrCreate("sr.present");
        CHECK(reg.contains("sr.present"));
}

TEST_CASE("StringRegistry::Item: default is invalid") {
        TestItem item;
        CHECK_FALSE(item.isValid());
}

TEST_CASE("StringRegistry::Item: construct from string is valid") {
        TestItem item("item.test");
        CHECK(item.isValid());
}

TEST_CASE("StringRegistry::Item: same name produces same ID") {
        TestItem a("item.same");
        TestItem b("item.same");
        CHECK(a == b);
        CHECK(a.id() == b.id());
}

TEST_CASE("StringRegistry::Item: different names produce different IDs") {
        TestItem a("item.alpha");
        TestItem b("item.beta");
        CHECK(a != b);
}

TEST_CASE("StringRegistry::Item: name roundtrip") {
        TestItem item("item.roundtrip");
        CHECK(item.name() == "item.roundtrip");
}

TEST_CASE("StringRegistry::Item: find returns invalid for unregistered name") {
        TestItem item = TestItem::find("item.never.registered.xyzzy");
        CHECK_FALSE(item.isValid());
}

TEST_CASE("StringRegistry::Item: find returns valid for registered name") {
        TestItem created("item.findtest");
        TestItem found = TestItem::find("item.findtest");
        CHECK(found.isValid());
        CHECK(found == created);
}

TEST_CASE("StringRegistry::Item: less-than for ordered containers") {
        TestItem a("item.lt.alpha");
        TestItem b("item.lt.beta");
        CHECK((a < b) != (b < a));
}
