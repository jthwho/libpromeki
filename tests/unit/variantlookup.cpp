/**
 * @file      variantlookup.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/variantlookup.h>
#include <promeki/variantdatabase.h>
#include <promeki/list.h>

using namespace promeki;

namespace {

        // ----- Synthetic types exercised by the tests ---------------------------
        // Kept local to this TU so the template is tested against shapes that
        // deliberately do not match Frame / Image / Audio semantics.

        using TestDB = VariantDatabase<"VariantLookupTestDB">;

        struct LookupChild {
                        uint32_t value = 0;
        };

        struct LookupRoot {
                        uint32_t          width = 1920;
                        uint32_t          height = 1080;
                        List<LookupChild> kids;
                        LookupChild       singleton;
                        TestDB            db;

                        LookupRoot() {
                                kids.pushToBack(LookupChild{10});
                                kids.pushToBack(LookupChild{20});
                                kids.pushToBack(LookupChild{30});
                                singleton.value = 99;
                        }
        };

} // namespace

// ----- Child registrations ----------------------------------------------

PROMEKI_LOOKUP_REGISTER(LookupChild)
        .scalar(
                "Value", [](const LookupChild &c) -> std::optional<Variant> { return Variant(c.value); },
                [](LookupChild &c, const Variant &v) -> Error {
                        Error    e;
                        uint32_t x = v.get<uint32_t>(&e);
                        if (e.isError()) return Error::ConversionFailed;
                        c.value = x;
                        return Error::Ok;
                });

// ----- Root registrations -----------------------------------------------

PROMEKI_LOOKUP_REGISTER(LookupRoot)
        .scalar(
                "Width", [](const LookupRoot &o) -> std::optional<Variant> { return Variant(o.width); },
                [](LookupRoot &o, const Variant &v) -> Error {
                        Error    e;
                        uint32_t x = v.get<uint32_t>(&e);
                        if (e.isError()) return Error::ConversionFailed;
                        o.width = x;
                        return Error::Ok;
                })
        .scalar("KidCount",
                [](const LookupRoot &o) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(o.kids.size()));
                })
        .scalar(
                "RejectsStrings", [](const LookupRoot &o) -> std::optional<Variant> { return Variant(o.height); },
                [](LookupRoot &o, const Variant &v) -> Error {
                        Error    e;
                        uint32_t x = v.get<uint32_t>(&e);
                        if (e.isError()) return Error::ConversionFailed;
                        if (v.type() == Variant::TypeString) return Error::ConversionFailed;
                        o.height = x;
                        return Error::Ok;
                })
        .indexedScalar("KidValue",
                       [](const LookupRoot &o, size_t i) -> std::optional<Variant> {
                               if (i >= o.kids.size()) return std::nullopt;
                               return Variant(o.kids[i].value);
                       })
        .child<LookupChild>(
                "Single", [](const LookupRoot &o) -> const LookupChild * { return &o.singleton; },
                [](LookupRoot &o) -> LookupChild * { return &o.singleton; })
        .child<LookupChild>("SingleReadOnly", [](const LookupRoot &o) -> const LookupChild * { return &o.singleton; })
        .indexedChild<LookupChild>(
                "Kid",
                [](const LookupRoot &o, size_t i) -> const LookupChild * {
                        if (i >= o.kids.size()) return nullptr;
                        return &o.kids[i];
                },
                [](LookupRoot &o, size_t i) -> LookupChild * {
                        if (i >= o.kids.size()) return nullptr;
                        return &o.kids[i];
                })
        .database<"VariantLookupTestDB">(
                "Meta", [](const LookupRoot &o) -> const TestDB * { return &o.db; },
                [](LookupRoot &o) -> TestDB * { return &o.db; });

// ========================================================================
// Segment parser
// ========================================================================

TEST_CASE("VariantLookup: parser accepts bare name") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK(detail::parseLeadingSegment("Foo", seg, &err));
        CHECK(err.isOk());
        CHECK(seg.name == "Foo");
        CHECK_FALSE(seg.hasIndex);
        CHECK_FALSE(seg.hasRest);
}

TEST_CASE("VariantLookup: parser accepts indexed name") {
        detail::VariantLookupSegment seg;
        CHECK(detail::parseLeadingSegment("Foo[42]", seg));
        CHECK(seg.name == "Foo");
        CHECK(seg.hasIndex);
        CHECK(seg.index == 42u);
        CHECK_FALSE(seg.hasRest);
}

TEST_CASE("VariantLookup: parser splits bare name and rest") {
        detail::VariantLookupSegment seg;
        CHECK(detail::parseLeadingSegment("Foo.Bar.Baz", seg));
        CHECK(seg.name == "Foo");
        CHECK_FALSE(seg.hasIndex);
        CHECK(seg.hasRest);
        CHECK(seg.rest == "Bar.Baz");
}

TEST_CASE("VariantLookup: parser splits indexed name and rest") {
        detail::VariantLookupSegment seg;
        CHECK(detail::parseLeadingSegment("Foo[3].Bar", seg));
        CHECK(seg.name == "Foo");
        CHECK(seg.hasIndex);
        CHECK(seg.index == 3u);
        CHECK(seg.hasRest);
        CHECK(seg.rest == "Bar");
}

TEST_CASE("VariantLookup: parser rejects empty key") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment("", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects empty name") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment(".Foo", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects empty index") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment("Foo[]", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects unterminated bracket") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment("Foo[3", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects non-digit inside bracket") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment("Foo[abc]", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects trailing dot") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment("Foo.", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects empty segment between dots") {
        detail::VariantLookupSegment seg;
        Error                        err;
        // Leading segment "Foo" parses; rest is "..Bar" which then fails
        // when the recursion tries the next segment.  We can still check
        // the raw ".." case by parsing the inner directly.
        CHECK_FALSE(detail::parseLeadingSegment(".", seg, &err));
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: parser rejects junk after bracket") {
        detail::VariantLookupSegment seg;
        Error                        err;
        CHECK_FALSE(detail::parseLeadingSegment("Foo[1]garbage", seg, &err));
        CHECK(err == Error::ParseFailed);
}

// ========================================================================
// resolve() - scalar handlers
// ========================================================================

TEST_CASE("VariantLookup: resolve scalar") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Width", &err);
        REQUIRE(v.has_value());
        CHECK(err.isOk());
        CHECK(v->get<uint32_t>() == 1920u);
}

TEST_CASE("VariantLookup: resolve read-only scalar") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "KidCount", &err);
        REQUIRE(v.has_value());
        CHECK(err.isOk());
        CHECK(v->get<uint64_t>() == 3u);
}

TEST_CASE("VariantLookup: resolve unknown scalar returns IdNotFound") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Unknown", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::IdNotFound);
}

TEST_CASE("VariantLookup: resolve indexed scalar in range") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "KidValue[1]", &err);
        REQUIRE(v.has_value());
        CHECK(err.isOk());
        CHECK(v->get<uint32_t>() == 20u);
}

TEST_CASE("VariantLookup: resolve indexed scalar out of range") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "KidValue[99]", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::OutOfRange);
}

TEST_CASE("VariantLookup: resolve unknown indexed scalar returns IdNotFound") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Nope[0]", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::IdNotFound);
}

// ========================================================================
// resolve() - child composition
// ========================================================================

TEST_CASE("VariantLookup: resolve into child") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Single.Value", &err);
        REQUIRE(v.has_value());
        CHECK(err.isOk());
        CHECK(v->get<uint32_t>() == 99u);
}

TEST_CASE("VariantLookup: resolve into indexed child") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Kid[2].Value", &err);
        REQUIRE(v.has_value());
        CHECK(err.isOk());
        CHECK(v->get<uint32_t>() == 30u);
}

TEST_CASE("VariantLookup: resolve indexed child out of range") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Kid[99].Value", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::OutOfRange);
}

TEST_CASE("VariantLookup: bare child lookup returns nullopt") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Single", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::IdNotFound);
}

TEST_CASE("VariantLookup: child with unknown sub-key") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Single.Nope", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::IdNotFound);
}

// ========================================================================
// resolve() - database binding
// ========================================================================

TEST_CASE("VariantLookup: resolve through database binding") {
        LookupRoot root;
        TestDB::ID key("Title");
        root.db.set(key, String("Hello"));
        Error err;
        auto  v = VariantLookup<LookupRoot>::resolve(root, "Meta.Title", &err);
        REQUIRE(v.has_value());
        CHECK(err.isOk());
        CHECK(v->get<String>() == "Hello");
}

TEST_CASE("VariantLookup: resolve unknown database key returns IdNotFound") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Meta.Missing", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::IdNotFound);
}

// ========================================================================
// resolve() - same name scalar + indexedScalar coexist
// ========================================================================

namespace {

        struct ShapeCollision {
                        uint32_t scalarValue = 11;
                        uint32_t indexedValues[3] = {100, 200, 300};
        };

} // namespace

PROMEKI_LOOKUP_REGISTER(ShapeCollision)
        .scalar("Value", [](const ShapeCollision &o) -> std::optional<Variant> { return Variant(o.scalarValue); })
        .indexedScalar("Value", [](const ShapeCollision &o, size_t i) -> std::optional<Variant> {
                if (i >= 3) return std::nullopt;
                return Variant(o.indexedValues[i]);
        });

TEST_CASE("VariantLookup: scalar and indexedScalar share a name") {
        ShapeCollision o;
        auto           bare = VariantLookup<ShapeCollision>::resolve(o, "Value");
        REQUIRE(bare.has_value());
        CHECK(bare->get<uint32_t>() == 11u);

        auto indexed = VariantLookup<ShapeCollision>::resolve(o, "Value[1]");
        REQUIRE(indexed.has_value());
        CHECK(indexed->get<uint32_t>() == 200u);
}

// ========================================================================
// assign() - scalar setters
// ========================================================================

TEST_CASE("VariantLookup: assign to scalar") {
        LookupRoot root;
        Error      err;
        CHECK(VariantLookup<LookupRoot>::assign(root, "Width", Variant(uint32_t(3840)), &err));
        CHECK(err.isOk());
        CHECK(root.width == 3840u);
}

TEST_CASE("VariantLookup: assign to read-only scalar fails") {
        LookupRoot root;
        Error      err;
        CHECK_FALSE(VariantLookup<LookupRoot>::assign(root, "KidCount", Variant(uint64_t(5)), &err));
        CHECK(err == Error::ReadOnly);
}

TEST_CASE("VariantLookup: assign to unknown scalar fails") {
        LookupRoot root;
        Error      err;
        CHECK_FALSE(VariantLookup<LookupRoot>::assign(root, "Nope", Variant(uint32_t(0)), &err));
        CHECK(err == Error::IdNotFound);
}

TEST_CASE("VariantLookup: assign rejected by setter fails with ConversionFailed") {
        LookupRoot root;
        Error      err;
        CHECK_FALSE(VariantLookup<LookupRoot>::assign(root, "RejectsStrings", Variant(String("not a number")), &err));
        CHECK(err == Error::ConversionFailed);
}

// ========================================================================
// assign() - child composition
// ========================================================================

TEST_CASE("VariantLookup: assign through child") {
        LookupRoot root;
        Error      err;
        CHECK(VariantLookup<LookupRoot>::assign(root, "Single.Value", Variant(uint32_t(777)), &err));
        CHECK(err.isOk());
        CHECK(root.singleton.value == 777u);
}

TEST_CASE("VariantLookup: assign through indexed child") {
        LookupRoot root;
        Error      err;
        CHECK(VariantLookup<LookupRoot>::assign(root, "Kid[0].Value", Variant(uint32_t(555)), &err));
        CHECK(err.isOk());
        CHECK(root.kids[0].value == 555u);
}

TEST_CASE("VariantLookup: assign through read-only child fails") {
        LookupRoot root;
        Error      err;
        CHECK_FALSE(VariantLookup<LookupRoot>::assign(root, "SingleReadOnly.Value", Variant(uint32_t(1)), &err));
        CHECK(err == Error::ReadOnly);
}

TEST_CASE("VariantLookup: assign through indexed child out of range") {
        LookupRoot root;
        Error      err;
        CHECK_FALSE(VariantLookup<LookupRoot>::assign(root, "Kid[99].Value", Variant(uint32_t(1)), &err));
        CHECK(err == Error::OutOfRange);
}

// ========================================================================
// assign() - database binding
// ========================================================================

TEST_CASE("VariantLookup: assign to database creates new key") {
        LookupRoot root;
        Error      err;
        CHECK(VariantLookup<LookupRoot>::assign(root, "Meta.NewKey", Variant(String("value")), &err));
        CHECK(err.isOk());
        TestDB::ID key("NewKey");
        CHECK(root.db.contains(key));
        CHECK(root.db.get(key).get<String>() == "value");
}

// ========================================================================
// Error passthrough via resolve() with malformed key
// ========================================================================

TEST_CASE("VariantLookup: malformed key on resolve") {
        LookupRoot root;
        Error      err;
        auto       v = VariantLookup<LookupRoot>::resolve(root, "Kid[abc].Value", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup: malformed key on assign") {
        LookupRoot root;
        Error      err;
        CHECK_FALSE(VariantLookup<LookupRoot>::assign(root, "Kid[.Value", Variant(uint32_t(0)), &err));
        CHECK(err == Error::ParseFailed);
}

// ========================================================================
// Introspection
// ========================================================================

TEST_CASE("VariantLookup: introspection lists registered names") {
        StringList scalars = VariantLookup<LookupRoot>::registeredScalars();
        CHECK(scalars.contains("Width"));
        CHECK(scalars.contains("KidCount"));
        CHECK(scalars.contains("RejectsStrings"));

        StringList ixScalars = VariantLookup<LookupRoot>::registeredIndexedScalars();
        CHECK(ixScalars.contains("KidValue"));

        StringList kids = VariantLookup<LookupRoot>::registeredChildren();
        CHECK(kids.contains("Single"));
        CHECK(kids.contains("SingleReadOnly"));

        StringList ixKids = VariantLookup<LookupRoot>::registeredIndexedChildren();
        CHECK(ixKids.contains("Kid"));

        StringList dbs = VariantLookup<LookupRoot>::registeredDatabases();
        CHECK(dbs.contains("Meta"));
}

TEST_CASE("VariantLookup: forEachScalar iterates every scalar") {
        StringList seen;
        VariantLookup<LookupRoot>::forEachScalar([&seen](const String &name) { seen.pushToBack(name); });
        CHECK(seen.contains("Width"));
        CHECK(seen.contains("KidCount"));
        CHECK(seen.contains("RejectsStrings"));
}

// ========================================================================
// Inheritance cascade (inheritsFrom<Base>)
// ========================================================================
//
// Synthetic three-tier hierarchy exercising the Registrar::inheritsFrom
// cascade:
//   CascadeBase   — Base scalar/setter + read-only scalar + indexed scalar
//   CascadeMid    — inherits CascadeBase, adds a Mid scalar that shadows
//                   nothing, plus a child composition
//   CascadeDerived— inherits CascadeMid, adds a Derived-only scalar
//
// The tests below pin: (a) derived classes resolve keys registered on
// any ancestor, (b) writes go to the correct tier, (c) a derived
// class can shadow a base key, (d) introspection merges names across
// the chain, (e) the segment parse error (IdNotFound vs ParseFailed)
// is preserved through the cascade.

namespace {

        struct CascadeBase {
                        uint32_t baseValue = 1;
                        uint32_t baseRo = 99;
                        uint32_t indexedVals[3] = {10, 20, 30};
                        virtual ~CascadeBase() = default;
        };

        struct CascadeMid : public CascadeBase {
                        uint32_t    midValue = 2;
                        LookupChild child;
        };

        struct CascadeDerived : public CascadeMid {
                        uint32_t derivedValue = 3;
                        uint32_t baseValue = 4; // Shadow on the derived.
        };

} // namespace

PROMEKI_LOOKUP_REGISTER(CascadeBase)
        .scalar(
                "BaseValue", [](const CascadeBase &o) -> std::optional<Variant> { return Variant(o.baseValue); },
                [](CascadeBase &o, const Variant &v) -> Error {
                        Error    e;
                        uint32_t x = v.get<uint32_t>(&e);
                        if (e.isError()) return Error::ConversionFailed;
                        o.baseValue = x;
                        return Error::Ok;
                })
        .scalar("BaseRo", [](const CascadeBase &o) -> std::optional<Variant> { return Variant(o.baseRo); })
        .indexedScalar("IndexedBase", [](const CascadeBase &o, size_t i) -> std::optional<Variant> {
                if (i >= 3) return std::nullopt;
                return Variant(o.indexedVals[i]);
        });

PROMEKI_LOOKUP_REGISTER(CascadeMid)
        .inheritsFrom<CascadeBase>()
        .scalar("MidValue", [](const CascadeMid &o) -> std::optional<Variant> { return Variant(o.midValue); })
        .child<LookupChild>(
                "Kid", [](const CascadeMid &o) -> const LookupChild * { return &o.child; },
                [](CascadeMid &o) -> LookupChild * { return &o.child; });

PROMEKI_LOOKUP_REGISTER(CascadeDerived)
        .inheritsFrom<CascadeMid>()
        .scalar("DerivedValue",
                [](const CascadeDerived &o) -> std::optional<Variant> { return Variant(o.derivedValue); })
        // Shadow BaseValue — the derived value wins at this tier
        // because the own-registry check runs before the cascade.
        .scalar("BaseValue", [](const CascadeDerived &o) -> std::optional<Variant> { return Variant(o.baseValue); });

TEST_CASE("VariantLookup cascade: derived resolves base scalars") {
        CascadeDerived d;
        d.midValue = 5;
        d.baseRo = 77;
        Error err;
        auto  mid = VariantLookup<CascadeDerived>::resolve(d, "MidValue", &err);
        REQUIRE(mid.has_value());
        CHECK(err.isOk());
        CHECK(mid->get<uint32_t>() == 5u);

        auto baseRo = VariantLookup<CascadeDerived>::resolve(d, "BaseRo", &err);
        REQUIRE(baseRo.has_value());
        CHECK(err.isOk());
        CHECK(baseRo->get<uint32_t>() == 77u);
}

TEST_CASE("VariantLookup cascade: derived shadows base scalar") {
        CascadeDerived d;
        d.CascadeBase::baseValue = 11; // base storage
        d.baseValue = 22;              // derived storage
        auto v = VariantLookup<CascadeDerived>::resolve(d, "BaseValue");
        REQUIRE(v.has_value());
        CHECK(v->get<uint32_t>() == 22u); // derived wins
}

TEST_CASE("VariantLookup cascade: base's indexed scalar reachable from derived") {
        CascadeDerived d;
        d.indexedVals[1] = 444;
        auto v = VariantLookup<CascadeDerived>::resolve(d, "IndexedBase[1]");
        REQUIRE(v.has_value());
        CHECK(v->get<uint32_t>() == 444u);
}

TEST_CASE("VariantLookup cascade: shadow handler takes precedence over base setter") {
        // A shadowed scalar that's read-only on the derived should
        // fail with ReadOnly rather than silently routing to the
        // base's setter — the derived class owns the key once it
        // registers it.
        CascadeDerived d;
        Error          err;
        CHECK_FALSE(VariantLookup<CascadeDerived>::assign(d, "BaseValue", Variant(uint32_t(999)), &err));
        CHECK(err == Error::ReadOnly);
}

TEST_CASE("VariantLookup cascade: non-shadowed base key assigns through cascade") {
        // A key registered only on the base must cascade all the way
        // up when assigned through the derived's lookup.
        // CascadeBase registers BaseValue with a setter; CascadeMid
        // shadows nothing, so assigning through CascadeMid goes
        // directly to the base's setter.
        CascadeMid m;
        Error      err;
        CHECK(VariantLookup<CascadeMid>::assign(m, "BaseValue", Variant(uint32_t(42)), &err));
        CHECK(err.isOk());
        CHECK(m.baseValue == 42u);
}

TEST_CASE("VariantLookup cascade: unknown key still reports IdNotFound") {
        CascadeDerived d;
        Error          err;
        auto           v = VariantLookup<CascadeDerived>::resolve(d, "NoSuchKey", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::IdNotFound);
}

TEST_CASE("VariantLookup cascade: parse-failure short-circuits before cascade") {
        CascadeDerived d;
        Error          err;
        auto           v = VariantLookup<CascadeDerived>::resolve(d, "Kid[bad].Value", &err);
        CHECK_FALSE(v.has_value());
        CHECK(err == Error::ParseFailed);
}

TEST_CASE("VariantLookup cascade: introspection merges names") {
        StringList s = VariantLookup<CascadeDerived>::registeredScalars();
        CHECK(s.contains("DerivedValue"));
        CHECK(s.contains("MidValue"));
        CHECK(s.contains("BaseValue"));
        CHECK(s.contains("BaseRo"));

        StringList ix = VariantLookup<CascadeDerived>::registeredIndexedScalars();
        CHECK(ix.contains("IndexedBase"));

        StringList kids = VariantLookup<CascadeDerived>::registeredChildren();
        CHECK(kids.contains("Kid"));
}

TEST_CASE("VariantLookup cascade: forEachScalar walks the chain deduped") {
        StringList seen;
        VariantLookup<CascadeDerived>::forEachScalar([&seen](const String &n) { seen.pushToBack(n); });
        CHECK(seen.contains("DerivedValue"));
        CHECK(seen.contains("MidValue"));
        CHECK(seen.contains("BaseValue"));
        CHECK(seen.contains("BaseRo"));
        // Shadow should appear once, not twice.
        int baseValueCount = 0;
        for (const String &n : seen)
                if (n == "BaseValue") ++baseValueCount;
        CHECK(baseValueCount == 1);
}

// ========================================================================
// VariantTree handler — descends through nested VariantList / VariantMap
// ========================================================================

namespace {

        struct LookupTreeOwner {
                        VariantMap config;

                        LookupTreeOwner() {
                                VariantMap video;
                                video.insert("width", Variant(int32_t(1920)));
                                video.insert("height", Variant(int32_t(1080)));

                                VariantList tags;
                                tags.pushToBack(Variant(String("alpha")));
                                tags.pushToBack(Variant(String("beta")));

                                config.insert("video", Variant(video));
                                config.insert("tags", Variant(tags));
                        }
        };

        // Spec describing the tree shape so specFor() can walk it.
        // The outer is TypeVariantMap; values can be either a TypeVariantMap
        // (the "video" sub-object) or a TypeVariantList (the "tags" array),
        // so the value-spec is left open here — the test exercises the
        // specFor walker on the outer type primarily.
        const VariantSpec *configTreeSpec() {
                static const VariantSpec spec = []() {
                        VariantSpec s;
                        s.setType(Variant::TypeVariantMap);
                        return s;
                }();
                return &spec;
        }

} // namespace

PROMEKI_LOOKUP_REGISTER(LookupTreeOwner)
        .variantTree(
                "Config",
                [](const LookupTreeOwner &o) -> Variant { return Variant(o.config); },
                configTreeSpec());

TEST_CASE("VariantLookup: variantTree resolves terminal Name") {
        LookupTreeOwner o;
        Error           err;
        auto            v = VariantLookup<LookupTreeOwner>::resolve(o, "Config", &err);
        CHECK(err.isOk());
        REQUIRE(v.has_value());
        CHECK(v->type() == Variant::TypeVariantMap);
}

TEST_CASE("VariantLookup: variantTree resolves nested map key") {
        LookupTreeOwner o;
        Error           err;
        auto            v = VariantLookup<LookupTreeOwner>::resolve(o, "Config.video.width", &err);
        CHECK(err.isOk());
        REQUIRE(v.has_value());
        CHECK(v->get<int32_t>() == 1920);

        v = VariantLookup<LookupTreeOwner>::resolve(o, "Config.video.height", &err);
        CHECK(err.isOk());
        CHECK(v->get<int32_t>() == 1080);
}

TEST_CASE("VariantLookup: variantTree resolves indexed list element") {
        LookupTreeOwner o;
        Error           err;
        auto            v = VariantLookup<LookupTreeOwner>::resolve(o, "Config.tags[0]", &err);
        CHECK(err.isOk());
        REQUIRE(v.has_value());
        CHECK(v->get<String>() == "alpha");

        v = VariantLookup<LookupTreeOwner>::resolve(o, "Config.tags[1]", &err);
        CHECK(err.isOk());
        CHECK(v->get<String>() == "beta");
}

TEST_CASE("VariantLookup: variantTree missing nested key reports IdNotFound") {
        LookupTreeOwner o;
        Error           err;
        auto            v = VariantLookup<LookupTreeOwner>::resolve(o, "Config.video.depth", &err);
        CHECK(err.code() == Error::IdNotFound);
        CHECK_FALSE(v.has_value());
}

TEST_CASE("VariantLookup: variantTree out-of-range index reports OutOfRange") {
        LookupTreeOwner o;
        Error           err;
        auto            v = VariantLookup<LookupTreeOwner>::resolve(o, "Config.tags[42]", &err);
        CHECK(err.code() == Error::OutOfRange);
        CHECK_FALSE(v.has_value());
}

TEST_CASE("VariantLookup: variantTree assign reports ReadOnly") {
        LookupTreeOwner o;
        Error           err;
        bool            ok = VariantLookup<LookupTreeOwner>::assign(o, "Config.video.width", Variant(int32_t(640)), &err);
        CHECK_FALSE(ok);
        CHECK(err.code() == Error::ReadOnly);
}

TEST_CASE("VariantLookup: variantTree specFor surfaces the registered spec") {
        Error              err;
        const VariantSpec *sp = VariantLookup<LookupTreeOwner>::specFor("Config", &err);
        CHECK(err.isOk());
        REQUIRE(sp != nullptr);
        CHECK(sp->types().size() == 1);
        CHECK(sp->types()[0] == Variant::TypeVariantMap);
}

// Spec-walker test fixture: nested Map → List → S32 so we can assert
// specFor walks each layer (named, bracketed, and the mixed "name[N]"
// segment that exercises both descents in one segment).
namespace {
        struct LookupTreeNested {};

        const VariantSpec *nestedTreeSpec() {
                static const VariantSpec spec = []() {
                        VariantSpec leaf = VariantSpec().setType(Variant::TypeS32);
                        VariantSpec listSpec =
                                VariantSpec().setType(Variant::TypeVariantList).setElementSpec(leaf);
                        return VariantSpec().setType(Variant::TypeVariantMap).setValueSpec(listSpec);
                }();
                return &spec;
        }
} // namespace

PROMEKI_LOOKUP_REGISTER(LookupTreeNested)
        .variantTree(
                "Tree", [](const LookupTreeNested &) -> Variant { return Variant(VariantMap()); },
                nestedTreeSpec());

TEST_CASE("VariantLookup: variantTree specFor walks element/value sub-specs") {
        // The walker must descend into valueSpec for named keys and
        // elementSpec for bracketed indices, including the mixed
        // "name[N]" segment shape (descend valueSpec → elementSpec).
        Error              err;
        const VariantSpec *spTop = VariantLookup<LookupTreeNested>::specFor("Tree", &err);
        REQUIRE(spTop != nullptr);
        CHECK(spTop->types()[0] == Variant::TypeVariantMap);

        const VariantSpec *spInnerList = VariantLookup<LookupTreeNested>::specFor("Tree.someKey", &err);
        CHECK(err.isOk());
        REQUIRE(spInnerList != nullptr);
        CHECK(spInnerList->types()[0] == Variant::TypeVariantList);

        // "Tree.someKey[0]" must descend valueSpec then elementSpec —
        // the mixed-segment case the original lambda got wrong.
        const VariantSpec *spLeaf = VariantLookup<LookupTreeNested>::specFor("Tree.someKey[0]", &err);
        CHECK(err.isOk());
        REQUIRE(spLeaf != nullptr);
        CHECK(spLeaf->types()[0] == Variant::TypeS32);
}

TEST_CASE("VariantLookup: variantTree appears in dump output") {
        LookupTreeOwner o;
        StringList      lines = VariantLookup<LookupTreeOwner>::dump(o);
        bool            sawConfig = false;
        for (const String &l : lines) {
                if (l.contains("Config")) {
                        sawConfig = true;
                        break;
                }
        }
        CHECK(sawConfig);
}

// ========================================================================
// VariantSpec::coerce — recursive String → native coercion under nested specs
// ========================================================================

TEST_CASE("VariantSpec::coerce passes value through when no coercion needed") {
        VariantSpec spec = VariantSpec().setType(Variant::TypeS32);
        Error       err;
        Variant     out = spec.coerce(Variant(int32_t(42)), &err);
        CHECK(err.isOk());
        CHECK(out.get<int32_t>() == 42);
}

TEST_CASE("VariantSpec::coerce parses leaf String into native type") {
        VariantSpec spec = VariantSpec().setType(Variant::TypeS32);
        Error       err;
        Variant     out = spec.coerce(Variant(String("99")), &err);
        CHECK(err.isOk());
        CHECK(out.type() == Variant::TypeS32);
        CHECK(out.get<int32_t>() == 99);
}

TEST_CASE("VariantSpec::coerce walks VariantList element-spec") {
        VariantSpec elem = VariantSpec().setType(Variant::TypeS32);
        VariantSpec spec = VariantSpec().setType(Variant::TypeVariantList).setElementSpec(elem);

        VariantList src;
        src.pushToBack(Variant(String("1")));
        src.pushToBack(Variant(String("2")));
        src.pushToBack(Variant(String("3")));

        Error   err;
        Variant out = spec.coerce(Variant(src), &err);
        CHECK(err.isOk());
        REQUIRE(out.type() == Variant::TypeVariantList);
        VariantList vl = out.get<VariantList>();
        REQUIRE(vl.size() == 3);
        CHECK(vl[0].type() == Variant::TypeS32);
        CHECK(vl[0].get<int32_t>() == 1);
        CHECK(vl[2].get<int32_t>() == 3);
}

TEST_CASE("VariantSpec::coerce walks VariantMap value-spec") {
        VariantSpec val  = VariantSpec().setType(Variant::TypeS32);
        VariantSpec spec = VariantSpec().setType(Variant::TypeVariantMap).setValueSpec(val);

        VariantMap src;
        src.insert("a", Variant(String("10")));
        src.insert("b", Variant(String("20")));

        Error   err;
        Variant out = spec.coerce(Variant(src), &err);
        CHECK(err.isOk());
        VariantMap vm = out.get<VariantMap>();
        CHECK(vm.value("a").type() == Variant::TypeS32);
        CHECK(vm.value("a").get<int32_t>() == 10);
        CHECK(vm.value("b").get<int32_t>() == 20);
}

TEST_CASE("VariantSpec::coerce reports parse failure inside nested list") {
        VariantSpec elem = VariantSpec().setType(Variant::TypeS32);
        VariantSpec spec = VariantSpec().setType(Variant::TypeVariantList).setElementSpec(elem);

        VariantList src;
        src.pushToBack(Variant(String("1")));
        src.pushToBack(Variant(String("not-a-number")));

        Error   err;
        Variant out = spec.coerce(Variant(src), &err);
        CHECK(err.isError());
        CHECK_FALSE(out.isValid());
}
