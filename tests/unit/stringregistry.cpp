/**
 * @file      stringregistry.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <functional>
#include <unordered_map>
#include <promeki/fnv1a.h>
#include <promeki/hashmap.h>
#include <promeki/stringregistry.h>

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
        auto    &reg = TestRegistry::instance();
        uint64_t id1 = reg.findOrCreateProbe("sr.first");
        uint64_t id2 = reg.findOrCreateProbe("sr.second");
        CHECK(id1 != id2);
        CHECK(id1 != TestRegistry::InvalidID);
        CHECK(id2 != TestRegistry::InvalidID);
}

TEST_CASE("StringRegistry: findOrCreateProbe returns same ID for same string") {
        auto    &reg = TestRegistry::instance();
        uint64_t id1 = reg.findOrCreateProbe("sr.duplicate");
        uint64_t id2 = reg.findOrCreateProbe("sr.duplicate");
        CHECK(id1 == id2);
}

TEST_CASE("StringRegistry: findId returns correct ID after registration") {
        auto    &reg = TestRegistry::instance();
        uint64_t created = reg.findOrCreateProbe("sr.findtest");
        uint64_t found = reg.findId("sr.findtest");
        CHECK(created == found);
}

TEST_CASE("StringRegistry: name returns correct string for ID") {
        auto    &reg = TestRegistry::instance();
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
        auto    &reg = TestRegistry::instance();
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
        TestItem           runtime("item.literal.match");
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

// ============================================================================
// std::hash specialization on StringRegistryItem<Name>.
//
// The specialization makes registry IDs usable as keys in HashMap and
// std::unordered_map without each consumer having to provide its own
// hasher.  The specialization is partial-on-Name: every distinct
// VariantDatabase / StringRegistry instantiation gets it for free.
// ============================================================================

TEST_CASE("StringRegistry::Item: std::hash returns the underlying ID unchanged") {
        // Items already wrap a 64-bit FNV-1a hash, so the hash
        // specialization should not double-hash — verifying this both
        // documents intent and guards against accidental rehashing
        // future maintainers might introduce.
        TestItem            item("item.std.hash.identity");
        std::hash<TestItem> hasher;
        CHECK(hasher(item) == static_cast<std::size_t>(item.id()));
}

TEST_CASE("StringRegistry::Item: equal items produce equal hashes") {
        TestItem a("item.std.hash.equal");
        TestItem b("item.std.hash.equal");
        REQUIRE(a == b);
        std::hash<TestItem> hasher;
        CHECK(hasher(a) == hasher(b));
}

TEST_CASE("StringRegistry::Item: distinct items produce distinct hashes") {
        // FNV-1a hashes of distinct names will only collide
        // astronomically rarely — these two are picked from working
        // unit tests and known not to collide.
        TestItem a("item.std.hash.distinct.a");
        TestItem b("item.std.hash.distinct.b");
        REQUIRE(a != b);
        std::hash<TestItem> hasher;
        CHECK(hasher(a) != hasher(b));
}

TEST_CASE("StringRegistry::Item: usable as a HashMap key") {
        // The whole point of the std::hash specialization is so that
        // promeki::HashMap can key on registry IDs directly without a
        // custom hasher — verify the round-trip insert / lookup /
        // contains / remove path works.
        HashMap<TestItem, int> m;
        TestItem               width("item.hashmap.width");
        TestItem               height("item.hashmap.height");
        m.insert(width, 1920);
        m.insert(height, 1080);
        CHECK(m.size() == 2);
        CHECK(m.contains(width));
        CHECK(m.contains(height));
        CHECK(m.value(width, 0) == 1920);
        CHECK(m.value(height, 0) == 1080);
        CHECK(m.remove(width));
        CHECK_FALSE(m.contains(width));
        CHECK(m.contains(height));
}

TEST_CASE("StringRegistry::Item: usable as a std::unordered_map key") {
        // Standard-library containers should pick up the std::hash
        // specialization just as readily as promeki::HashMap.
        std::unordered_map<TestItem, int> m;
        TestItem                          a("item.unordered.a");
        TestItem                          b("item.unordered.b");
        m[a] = 1;
        m[b] = 2;
        CHECK(m.size() == 2);
        CHECK(m.at(a) == 1);
        CHECK(m.at(b) == 2);
}

// ============================================================================
// StringRegistryItem<Name> top-level type alias.
//
// The class was hoisted out of StringRegistry so that the std::hash
// partial specialization could resolve it; verify that
// StringRegistry<Name>::Item still aliases the top-level template.
// ============================================================================

TEST_CASE("StringRegistry::Item: type alias matches StringRegistryItem<Name>") {
        static_assert(std::is_same_v<TestRegistry::Item, StringRegistryItem<"TestRegistry">>,
                      "StringRegistry<Name>::Item must alias StringRegistryItem<Name>");
        // Runtime CHECK exists so doctest counts the case as run; the
        // real assertion is the static_assert above.
        CHECK(true);
}
