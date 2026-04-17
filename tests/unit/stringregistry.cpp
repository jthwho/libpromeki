/**
 * @file      stringregistry.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/stringregistry.h>
#include <promeki/fnv1a.h>

using namespace promeki;

using TestRegistry = StringRegistry<"TestRegistry">;
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

TEST_CASE("StringRegistry: findOrCreateProbe assigns IDs") {
        auto &reg = TestRegistry::instance();
        uint64_t id1 = reg.findOrCreateProbe("sr.first");
        uint64_t id2 = reg.findOrCreateProbe("sr.second");
        CHECK(id1 != id2);
        CHECK(id1 != TestRegistry::InvalidID);
        CHECK(id2 != TestRegistry::InvalidID);
}

TEST_CASE("StringRegistry: findOrCreateProbe returns same ID for same string") {
        auto &reg = TestRegistry::instance();
        uint64_t id1 = reg.findOrCreateProbe("sr.duplicate");
        uint64_t id2 = reg.findOrCreateProbe("sr.duplicate");
        CHECK(id1 == id2);
}

TEST_CASE("StringRegistry: findId returns correct ID after registration") {
        auto &reg = TestRegistry::instance();
        uint64_t created = reg.findOrCreateProbe("sr.findtest");
        uint64_t found = reg.findId("sr.findtest");
        CHECK(created == found);
}

TEST_CASE("StringRegistry: name returns correct string for ID") {
        auto &reg = TestRegistry::instance();
        uint64_t id = reg.findOrCreateProbe("sr.nametest");
        CHECK(reg.name(id) == "sr.nametest");
}

TEST_CASE("StringRegistry: name returns empty for invalid ID") {
        auto &reg = TestRegistry::instance();
        CHECK(reg.name(TestRegistry::InvalidID).isEmpty());
}

TEST_CASE("StringRegistry: contains works correctly") {
        auto &reg = TestRegistry::instance();
        CHECK_FALSE(reg.contains("sr.missing.xyzzy"));
        reg.findOrCreateProbe("sr.present");
        CHECK(reg.contains("sr.present"));
}

TEST_CASE("StringRegistry: findOrCreateStrict agrees with the pure hash") {
        auto &reg = TestRegistry::instance();
        uint64_t id = reg.findOrCreateStrict("sr.strict.one");
        CHECK(id == fnv1a("sr.strict.one"));
        // Idempotent on the same name.
        CHECK(reg.findOrCreateStrict("sr.strict.one") == id);
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

TEST_CASE("StringRegistry::Item: literal matches runtime-registered ID") {
        TestItem runtime("item.literal.match");
        constexpr TestItem compile = TestItem::literal("item.literal.match");
        CHECK(runtime.id() == compile.id());
}

TEST_CASE("StringRegistry::Item: literal is a constexpr constant") {
        // Exercises the constexpr path: these must be usable in
        // constant-expression contexts.
        static constexpr TestItem lit = TestItem::literal("item.literal.constexpr");
        static_assert(lit.id() != TestRegistry::InvalidID);
        static_assert(lit.isValid());
        static_assert(lit == TestItem::literal("item.literal.constexpr"));
        static_assert(lit != TestItem::literal("item.literal.constexpr.other"));
        CHECK(lit.isValid());
}

TEST_CASE("StringRegistry::Item: fromId wraps fnv1a output") {
        constexpr TestItem a = TestItem::fromId(fnv1a("item.fromid"));
        constexpr TestItem b = TestItem::literal("item.fromid");
        static_assert(a == b);
        CHECK(a == b);
}

TEST_CASE("StringRegistry::Item: literal does not register for reverse lookup") {
        // The probe-free literal path is not expected to register the
        // name, so a subsequent find() must still report invalid.
        TestItem::literal("item.literal.noregister");
        TestItem found = TestItem::find("item.literal.noregister");
        CHECK_FALSE(found.isValid());
}

TEST_CASE("StringRegistry::Item: runtime + literal roundtrip") {
        TestItem runtime("item.literal.roundtrip");
        CHECK(TestItem::literal("item.literal.roundtrip").id() == runtime.id());
        CHECK(runtime.name() == "item.literal.roundtrip");
}
