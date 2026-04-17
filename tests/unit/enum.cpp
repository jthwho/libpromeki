/**
 * @file      enum.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enum.h>
#include <promeki/enums.h>
#include <promeki/metadata.h>
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>
#include <promeki/json.h>

using namespace promeki;

namespace {

// A test enum type registered once at static-init time.
struct TestCodec {
        static inline const Enum::Type Type = Enum::registerType("TestCodec",
                {
                        { "H264", 1 },
                        { "H265", 2 },
                        { "VP9",  3 }
                },
                1);  // default = H264

        static inline const Enum H264{Type, 1};
        static inline const Enum H265{Type, 2};
        static inline const Enum VP9 {Type, 3};
};

// A second test enum type to exercise multiple registrations and to
// exercise negative integer values (to prove -1 is handled cleanly as a
// value rather than only as a failure sentinel).
struct TestSeverity {
        static inline const Enum::Type Type = Enum::registerType("TestSeverity",
                {
                        { "Debug",   -1 },
                        { "Info",     0 },
                        { "Warning",  1 },
                        { "Error",    2 }
                },
                0);  // default = Info
};

} // namespace

TEST_CASE("Enum: type registration returns a valid Type") {
        CHECK(TestCodec::Type.isValid());
        CHECK(TestCodec::Type.name() == String("TestCodec"));
}

TEST_CASE("Enum: default-constructed Enum is invalid") {
        Enum e;
        CHECK_FALSE(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == Enum::InvalidValue);
        CHECK(e.valueName().isEmpty());
        CHECK(e.typeName().isEmpty());
}

TEST_CASE("Enum: Enum(Type) picks up the registered default value") {
        Enum e(TestCodec::Type);
        CHECK(e.isValid());
        CHECK(e.hasListedValue());
        CHECK(e.value() == 1);
        CHECK(e.valueName() == String("H264"));
        CHECK(e == TestCodec::H264);
}

TEST_CASE("Enum: Enum(Type, int) stores the value and is valid when registered") {
        Enum e(TestCodec::Type, 2);
        CHECK(e.isValid());
        CHECK(e.hasListedValue());
        CHECK(e.value() == 2);
        CHECK(e.valueName() == String("H265"));
}

TEST_CASE("Enum: Enum(Type, int) with an out-of-list integer is still valid (no listed name)") {
        Enum e(TestCodec::Type, 999);
        // The type is known, so the Enum itself is valid...
        CHECK(e.isValid());
        // ...but 999 is not in TestCodec's value table.
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == 999);
        CHECK(e.valueName().isEmpty());
        CHECK(e.typeName() == String("TestCodec"));
}

TEST_CASE("Enum: Enum(Type, name) looks up the integer value") {
        Enum e(TestCodec::Type, "VP9");
        CHECK(e.isValid());
        CHECK(e.hasListedValue());
        CHECK(e.value() == 3);
        CHECK(e.valueName() == String("VP9"));
}

TEST_CASE("Enum: Enum(Type, name) with an unknown name yields InvalidValue") {
        Enum e(TestCodec::Type, "Bogus");
        // The type is still known, so the Enum is considered valid by isValid();
        // the value just isn't listed.
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == Enum::InvalidValue);
        CHECK(e.valueName().isEmpty());
}

TEST_CASE("Enum: valueOf sets err on miss, leaves it Ok on hit") {
        Error err;
        int v = Enum::valueOf(TestCodec::Type, "H265", &err);
        CHECK(err.isOk());
        CHECK(v == 2);

        err = Error::Ok;
        v = Enum::valueOf(TestCodec::Type, "DoesNotExist", &err);
        CHECK(err == Error::IdNotFound);
        CHECK(v == Enum::InvalidValue);
}

TEST_CASE("Enum: nameOf sets err on miss, leaves it Ok on hit") {
        Error err;
        String n = Enum::nameOf(TestCodec::Type, 3, &err);
        CHECK(err.isOk());
        CHECK(n == String("VP9"));

        err = Error::Ok;
        n = Enum::nameOf(TestCodec::Type, 999, &err);
        CHECK(err == Error::IdNotFound);
        CHECK(n.isEmpty());
}

TEST_CASE("Enum: -1 is a valid registered value, distinguishable via err") {
        // The InvalidValue sentinel is -1, but -1 must still work as a
        // legitimate registered value when the err pointer says "Ok".
        Enum debug(TestSeverity::Type, "Debug");
        CHECK(debug.isValid());
        CHECK(debug.hasListedValue());
        CHECK(debug.value() == -1);

        Error err;
        int v = Enum::valueOf(TestSeverity::Type, "Debug", &err);
        CHECK(err.isOk());
        CHECK(v == -1);
}

TEST_CASE("Enum: defaultValue returns the registered default") {
        CHECK(Enum::defaultValue(TestCodec::Type) == 1);
        CHECK(Enum::defaultValue(TestSeverity::Type) == 0);
}

TEST_CASE("Enum: values() returns entries in registration order") {
        Enum::ValueList list = Enum::values(TestCodec::Type);
        REQUIRE(list.size() == 3);
        CHECK(list[0].first() == String("H264"));
        CHECK(list[0].second() == 1);
        CHECK(list[1].first() == String("H265"));
        CHECK(list[1].second() == 2);
        CHECK(list[2].first() == String("VP9"));
        CHECK(list[2].second() == 3);
}

TEST_CASE("Enum: findType returns the registered Type") {
        Enum::Type t = Enum::findType("TestCodec");
        CHECK(t.isValid());
        CHECK(t == TestCodec::Type);
}

TEST_CASE("Enum: findType returns an invalid Type for unknown names") {
        Enum::Type t = Enum::findType("NoSuchEnum");
        CHECK_FALSE(t.isValid());
}

TEST_CASE("Enum: registeredTypes() contains the test types") {
        Enum::TypeList names = Enum::registeredTypes();
        CHECK(names.contains(String("TestCodec")));
        CHECK(names.contains(String("TestSeverity")));
}

TEST_CASE("Enum: toString returns \"TypeName::ValueName\"") {
        CHECK(TestCodec::H264.toString() == String("TestCodec::H264"));
        CHECK(TestCodec::H265.toString() == String("TestCodec::H265"));
        CHECK(TestCodec::VP9 .toString() == String("TestCodec::VP9"));
}

TEST_CASE("Enum: lookup parses \"TypeName::ValueName\" back into an Enum") {
        Error err;
        Enum e = Enum::lookup("TestCodec::H265", &err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK(e == TestCodec::H265);
}

TEST_CASE("Enum: lookup rejects malformed strings") {
        Error err;
        Enum e = Enum::lookup("no_colons_here", &err);
        CHECK(err == Error::InvalidArgument);
        CHECK_FALSE(e.isValid());
}

TEST_CASE("Enum: lookup returns invalid for unknown type or non-integer value") {
        Error err;
        Enum a = Enum::lookup("NoSuchType::H264", &err);
        CHECK(err == Error::IdNotFound);
        CHECK_FALSE(a.isValid());

        // "Bogus" is neither a registered name nor a parseable integer, so
        // the whole lookup fails.
        Enum b = Enum::lookup("TestCodec::Bogus", &err);
        CHECK(err == Error::IdNotFound);
        CHECK_FALSE(b.isValid());
}

TEST_CASE("Enum: equality compares type and value") {
        CHECK(TestCodec::H264 == Enum(TestCodec::Type, 1));
        CHECK(TestCodec::H264 != TestCodec::H265);
        // Different types with the same integer value are not equal.
        Enum codecOne(TestCodec::Type, 1);
        Enum severityOne(TestSeverity::Type, 1);  // "Warning"
        CHECK(codecOne != severityOne);
}

TEST_CASE("Variant: holds Enum and reports TypeEnum") {
        Variant v = TestCodec::H265;
        CHECK(v.isValid());
        CHECK(v.type() == Variant::TypeEnum);
}

TEST_CASE("Variant: get<String>() on Enum returns \"TypeName::ValueName\"") {
        Variant v = TestCodec::VP9;
        CHECK(v.get<String>() == String("TestCodec::VP9"));
}

TEST_CASE("Variant: get<int>() on Enum returns the integer value") {
        Variant v = TestCodec::H265;
        Error err;
        int n = v.get<int>(&err);
        CHECK(err.isOk());
        CHECK(n == 2);
}

TEST_CASE("Variant: get<Enum>() from String parses the TypeName::ValueName form") {
        Variant v = String("TestCodec::H264");
        Error err;
        Enum e = v.get<Enum>(&err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::H264);
}

TEST_CASE("Variant: get<Enum>() from int is unsupported") {
        Variant v = int32_t(1);
        Error err;
        Enum e = v.get<Enum>(&err);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(e.isValid());
}

TEST_CASE("Variant: toStandardType converts Enum to its String form") {
        Variant v = TestCodec::H265;
        Variant s = v.toStandardType();
        CHECK(s.type() == Variant::TypeString);
        CHECK(s.get<String>() == String("TestCodec::H265"));
}

// ---------------------------------------------------------------------------
// Variant cross-type equality
// ---------------------------------------------------------------------------

TEST_CASE("Variant: Enum compares equal to its String form (both directions)") {
        Variant ve = TestCodec::H264;
        Variant vs = String("TestCodec::H264");
        CHECK(ve == vs);
        CHECK(vs == ve);
}

TEST_CASE("Variant: Enum does not compare equal to a mismatched String") {
        Variant ve = TestCodec::H264;
        Variant vs = String("TestCodec::H265");
        CHECK(ve != vs);
        CHECK(vs != ve);
}

TEST_CASE("Variant: Enum compares equal to its integer value (both directions)") {
        Variant ve = TestCodec::H265;          // value == 2
        Variant vi = int32_t(2);
        CHECK(ve == vi);
        CHECK(vi == ve);
}

TEST_CASE("Variant: Enum does not compare equal to a mismatched integer") {
        Variant ve = TestCodec::H264;          // value == 1
        Variant vi = int32_t(999);
        CHECK(ve != vi);
        CHECK(vi != ve);
}

// ---------------------------------------------------------------------------
// DataStream round-trip
// ---------------------------------------------------------------------------

TEST_CASE("DataStream: round-trip Variant Enum") {
        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        Variant vIn = TestCodec::VP9;
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << vIn;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                Variant vOut;
                rs >> vOut;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(vOut.type() == Variant::TypeEnum);
                Enum e = vOut.get<Enum>();
                CHECK(e.isValid());
                CHECK(e == TestCodec::VP9);
                CHECK(e.value() == 3);
                CHECK(e.typeName() == String("TestCodec"));
                CHECK(e.valueName() == String("VP9"));
        }
}

TEST_CASE("DataStream: round-trip Variant Enum preserves negative value") {
        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        // TestSeverity::Debug has integer value -1, which collides with
        // the InvalidValue sentinel; this verifies the round-trip does not
        // accidentally lose the real value.
        Variant vIn = Enum(TestSeverity::Type, "Debug");
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << vIn;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                Variant vOut;
                rs >> vOut;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(vOut.type() == Variant::TypeEnum);
                Enum e = vOut.get<Enum>();
                CHECK(e.isValid());
                CHECK(e.value() == -1);
                CHECK(e.valueName() == String("Debug"));
        }
}

// ---------------------------------------------------------------------------
// VariantDatabase / JSON round-trip
// ---------------------------------------------------------------------------

TEST_CASE("VariantDatabase: Enum round-trips through JSON as its String form") {
        using TestDb = VariantDatabase<"EnumTestDb">;
        TestDb db;
        TestDb::ID codec("codec");
        db.set(codec, Variant(TestCodec::H265));

        JsonObject json = db.toJson();
        TestDb dbOut = TestDb::fromJson(json);

        // JSON has no way to preserve Enum's typed identity so the value
        // comes back as a String, but get<Enum>() can recover the typed form.
        Variant out = dbOut.get(codec);
        CHECK(out.type() == Variant::TypeString);
        CHECK(out.get<String>() == String("TestCodec::H265"));

        Error err;
        Enum e = out.get<Enum>(&err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::H265);
}

TEST_CASE("VariantDatabase: getAs<Enum> recovers Enum from a stored String") {
        using TestDb = VariantDatabase<"EnumTestDb">;
        TestDb db;
        TestDb::ID codec("codec2");
        // Mimic what happens after a JSON round-trip: value arrives as String.
        db.set(codec, Variant(String("TestCodec::VP9")));

        Error err;
        Enum e = db.getAs<Enum>(codec, Enum(), &err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::VP9);
}

// ---------------------------------------------------------------------------
// Out-of-list values
// ---------------------------------------------------------------------------

TEST_CASE("Enum: toString of an out-of-list value emits the integer form") {
        Enum e(TestCodec::Type, 100);
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.valueName().isEmpty());
        CHECK(e.toString() == String("TestCodec::100"));
}

TEST_CASE("Enum: toString of an out-of-list negative value emits the signed integer form") {
        // TestSeverity's table covers -1..2; -42 is outside the table.
        Enum e(TestSeverity::Type, -42);
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.toString() == String("TestSeverity::-42"));
}

TEST_CASE("Enum: toString of an invalid Enum is \"::\"") {
        Enum e;
        CHECK_FALSE(e.isValid());
        CHECK(e.toString() == String("::"));
}

TEST_CASE("Enum: lookup accepts the \"TypeName::<int>\" integer form for out-of-list values") {
        Error err;
        Enum e = Enum::lookup("TestCodec::100", &err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.type() == TestCodec::Type);
        CHECK(e.value() == 100);
        CHECK(e.toString() == String("TestCodec::100"));
}

TEST_CASE("Enum: lookup accepts a negative integer form") {
        Error err;
        Enum e = Enum::lookup("TestSeverity::-42", &err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == -42);
}

TEST_CASE("Enum: lookup still prefers name lookup when the name is a numeric-looking string") {
        // The "Debug" value in TestSeverity has integer -1; lookup by the
        // registered name should hit the name branch, not the integer branch.
        Error err;
        Enum e = Enum::lookup("TestSeverity::Debug", &err);
        CHECK(err.isOk());
        CHECK(e.hasListedValue());
        CHECK(e.value() == -1);
        CHECK(e.valueName() == String("Debug"));
}

TEST_CASE("Enum: toString -> lookup round-trip preserves out-of-list values") {
        Enum original(TestCodec::Type, 12345);
        String s = original.toString();
        CHECK(s == String("TestCodec::12345"));

        Error err;
        Enum round = Enum::lookup(s, &err);
        CHECK(err.isOk());
        CHECK(round == original);
        CHECK(round.value() == 12345);
}

TEST_CASE("DataStream: round-trip Variant Enum with an out-of-list value") {
        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        Variant vIn = Enum(TestCodec::Type, 777);
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << vIn;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                Variant vOut;
                rs >> vOut;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(vOut.type() == Variant::TypeEnum);
                Enum e = vOut.get<Enum>();
                CHECK(e.isValid());
                CHECK_FALSE(e.hasListedValue());
                CHECK(e.type() == TestCodec::Type);
                CHECK(e.value() == 777);
                CHECK(e.toString() == String("TestCodec::777"));
        }
}

TEST_CASE("Variant: get<String>() on an out-of-list Enum emits the integer form") {
        Variant v = Enum(TestCodec::Type, 100);
        CHECK(v.get<String>() == String("TestCodec::100"));
}

TEST_CASE("Variant: get<Enum>() from \"TypeName::<int>\" String parses the out-of-list form") {
        Variant v = String("TestCodec::100");
        Error err;
        Enum e = v.get<Enum>(&err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == 100);
}

// ---------------------------------------------------------------------------
// Variant::asEnum(type) — context-aware conversion
// ---------------------------------------------------------------------------

TEST_CASE("Variant::asEnum: passes through a matching-type Enum") {
        Variant v = TestCodec::H265;
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::H265);
}

TEST_CASE("Variant::asEnum: rejects a mismatched-type Enum") {
        Variant v = TestCodec::H265;
        Error err;
        Enum e = v.asEnum(TestSeverity::Type, &err);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(e.isValid());
}

TEST_CASE("Variant::asEnum: parses a qualified String \"TypeName::ValueName\"") {
        Variant v = String("TestCodec::VP9");
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::VP9);
}

TEST_CASE("Variant::asEnum: rejects a qualified String whose type doesn't match") {
        Variant v = String("TestCodec::H264");
        Error err;
        Enum e = v.asEnum(TestSeverity::Type, &err);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(e.isValid());
}

TEST_CASE("Variant::asEnum: resolves an unqualified value-name String against the target type") {
        Variant v = String("H265");
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::H265);
}

TEST_CASE("Variant::asEnum: rejects an unqualified name that isn't in the target type") {
        Variant v = String("Bogus");
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(e.isValid());
}

TEST_CASE("Variant::asEnum: parses a bare decimal-integer String as an out-of-list value") {
        Variant v = String("100");
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == 100);
        CHECK(e.type() == TestCodec::Type);
}

TEST_CASE("Variant::asEnum: parses a bare negative-integer String") {
        Variant v = String("-42");
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e.value() == -42);
}

TEST_CASE("Variant::asEnum: wraps an integer Variant") {
        Variant v = int32_t(2);
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK(e.hasListedValue());
        CHECK(e.value() == 2);
        CHECK(e == TestCodec::H265);
}

TEST_CASE("Variant::asEnum: wraps an unsigned integer Variant of a different width") {
        Variant v = uint8_t(3);
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e.value() == 3);
        CHECK(e == TestCodec::VP9);
}

TEST_CASE("Variant::asEnum: wraps a bool Variant") {
        Variant v = true;
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e.value() == 1);
        CHECK(e == TestCodec::H264);
}

TEST_CASE("Variant::asEnum: out-of-list integer Variant yields an out-of-list Enum") {
        Variant v = int32_t(777);
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e.isValid());
        CHECK_FALSE(e.hasListedValue());
        CHECK(e.value() == 777);
}

TEST_CASE("Variant::asEnum: rejects an invalid target type") {
        Variant v = String("H264");
        Error err;
        Enum e = v.asEnum(Enum::Type(), &err);
        CHECK(err == Error::InvalidArgument);
        CHECK_FALSE(e.isValid());
}

TEST_CASE("Variant::asEnum: a default-constructed Variant returns the type's registered default") {
        // This is the "missing config key" case: a caller that stored no
        // value in a Map<String, Variant> sees a TypeInvalid Variant when
        // they look it up.  asEnum should fall back to the registered
        // default so the registered default also serves as the config
        // default.
        Variant v;
        CHECK(v.type() == Variant::TypeInvalid);
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err.isOk());
        CHECK(e == TestCodec::H264);   // H264 is the registered default
}

TEST_CASE("Variant::asEnum: rejects an unsupported source type (e.g. Color)") {
        Variant v = Color::Red;
        Error err;
        Enum e = v.asEnum(TestCodec::Type, &err);
        CHECK(err == Error::Invalid);
        CHECK_FALSE(e.isValid());
}

// ---------------------------------------------------------------------------
// ImgSeqPathMode — well-known Enum from enums.h
// ---------------------------------------------------------------------------

TEST_CASE("ImgSeqPathMode: constants have expected values") {
        CHECK(ImgSeqPathMode::Relative.isValid());
        CHECK(ImgSeqPathMode::Absolute.isValid());
        CHECK(ImgSeqPathMode::Relative.value() == 0);
        CHECK(ImgSeqPathMode::Absolute.value() == 1);
        CHECK(ImgSeqPathMode::Relative != ImgSeqPathMode::Absolute);
}

TEST_CASE("ImgSeqPathMode: default Variant resolves to Relative") {
        // Simulates what happens when SaveImgSeqPathMode is absent from
        // the config: asEnum must return the registered default (Relative).
        Variant v;
        Error err;
        Enum e = v.asEnum(ImgSeqPathMode::Type, &err);
        CHECK(err.isOk());
        CHECK(e == ImgSeqPathMode::Relative);
}

TEST_CASE("ImgSeqPathMode: named String round-trip") {
        Variant v = String("ImgSeqPathMode::Absolute");
        Error err;
        Enum e = v.asEnum(ImgSeqPathMode::Type, &err);
        CHECK(err.isOk());
        CHECK(e == ImgSeqPathMode::Absolute);
}

TEST_CASE("ImgSeqPathMode: unqualified name resolves against type") {
        Variant v = String("Relative");
        Error err;
        Enum e = v.asEnum(ImgSeqPathMode::Type, &err);
        CHECK(err.isOk());
        CHECK(e == ImgSeqPathMode::Relative);
}

// ---------------------------------------------------------------------------
// Zero-copy String returns
// ---------------------------------------------------------------------------

TEST_CASE("Enum: typeName() is literal-backed (zero-copy)") {
        // Type names are cached in a StringLiteralData at registration
        // time — accessors return wrappers around that record rather
        // than copying bytes.
        String s = TestCodec::H265.typeName();
        CHECK(s == String("TestCodec"));
        CHECK(s.isLiteral());
}

TEST_CASE("Enum: valueName() is literal-backed for in-list values") {
        String s = TestCodec::H265.valueName();
        CHECK(s == String("H265"));
        CHECK(s.isLiteral());
}

TEST_CASE("Enum: toString() is literal-backed for in-list values") {
        String s = TestCodec::H265.toString();
        CHECK(s == String("TestCodec::H265"));
        CHECK(s.isLiteral());
}

TEST_CASE("Enum: toString() is NOT literal-backed for out-of-list values") {
        // Out-of-list values have no pre-built qualified form, so the
        // result falls through to a concatenation and lands in a
        // mutable Latin1 buffer.
        Enum e(TestCodec::Type, 999);
        String s = e.toString();
        CHECK(s == String("TestCodec::999"));
        CHECK_FALSE(s.isLiteral());
}

TEST_CASE("Enum: two Enums of the same in-list value share toString() backing") {
        // Same StringLiteralData record backs both returned Strings, so
        // the underlying byte pointers compare equal and any downstream
        // String == short-circuits on identity before byte compare.
        Enum a(TestCodec::Type, 2);  // H265
        Enum b(TestCodec::Type, 2);  // H265
        String sa = a.toString();
        String sb = b.toString();
        CHECK(sa == sb);
        CHECK(sa.cstr() == sb.cstr());
}

// ---------------------------------------------------------------------------
// constexpr compatibility
// ---------------------------------------------------------------------------

namespace {

// Exercises that a Metadata ID declared via PROMEKI_DECLARE_ID is a
// true constant expression — suitable for `switch` labels and
// `static_assert`.  Any regression in the constexpr-id path here would
// surface as a compile error in this helper, not a runtime failure.
constexpr int classifyMetadataId(uint64_t id) {
        switch(id) {
                case Metadata::Title.id():     return 1;
                case Metadata::Artist.id():    return 2;
                case Metadata::Copyright.id(): return 3;
                default:                       return 0;
        }
}

static_assert(classifyMetadataId(Metadata::Title.id())     == 1);
static_assert(classifyMetadataId(Metadata::Artist.id())    == 2);
static_assert(classifyMetadataId(Metadata::Copyright.id()) == 3);
static_assert(classifyMetadataId(0xdeadbeef)                == 0);

} // namespace

TEST_CASE("Metadata: constexpr ID usable in a switch statement") {
        CHECK(classifyMetadataId(Metadata::Title.id())     == 1);
        CHECK(classifyMetadataId(Metadata::Artist.id())    == 2);
        CHECK(classifyMetadataId(Metadata::Copyright.id()) == 3);
        CHECK(classifyMetadataId(0xdeadbeef)                == 0);
}
