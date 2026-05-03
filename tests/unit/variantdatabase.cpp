/**
 * @file      variantdatabase.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/variantdatabase.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>
#include <promeki/size2d.h>

using namespace promeki;

using TestDB = VariantDatabase<"VDBTest">;

TEST_CASE("VariantDatabase: default is empty") {
        TestDB db;
        CHECK(db.isEmpty());
        CHECK(db.size() == 0);
}

TEST_CASE("VariantDatabase: set and get") {
        TestDB     db;
        TestDB::ID width("vdb.width");
        db.set(width, 1920);
        Variant v = db.get(width);
        CHECK(v.isValid());
        CHECK(v.get<int32_t>() == 1920);
}

TEST_CASE("VariantDatabase: get returns default for missing ID") {
        TestDB     db;
        TestDB::ID missing("vdb.missing");
        Variant    v = db.get(missing);
        CHECK_FALSE(v.isValid());
}

TEST_CASE("VariantDatabase: get with custom default") {
        TestDB     db;
        TestDB::ID missing("vdb.missing2");
        Variant    v = db.get(missing, Variant(42));
        CHECK(v.get<int32_t>() == 42);
}

TEST_CASE("VariantDatabase: contains") {
        TestDB     db;
        TestDB::ID key("vdb.contains");
        CHECK_FALSE(db.contains(key));
        db.set(key, String("hello"));
        CHECK(db.contains(key));
}

TEST_CASE("VariantDatabase: setIfMissing stores when absent") {
        TestDB     db;
        TestDB::ID key("vdb.setIfMissing.absent");
        bool       stored = db.setIfMissing(key, String("hello"));
        CHECK(stored);
        CHECK(db.contains(key));
        CHECK(db.get(key).get<String>() == "hello");
}

TEST_CASE("VariantDatabase: setIfMissing preserves existing value") {
        TestDB     db;
        TestDB::ID key("vdb.setIfMissing.present");
        db.set(key, String("first"));
        bool stored = db.setIfMissing(key, String("second"));
        CHECK_FALSE(stored);
        CHECK(db.get(key).get<String>() == "first");
}

TEST_CASE("VariantDatabase: set overwrites existing value") {
        TestDB     db;
        TestDB::ID key("vdb.overwrite");
        db.set(key, 100);
        db.set(key, 200);
        CHECK(db.get(key).get<int32_t>() == 200);
        CHECK(db.size() == 1);
}

TEST_CASE("VariantDatabase: remove") {
        TestDB     db;
        TestDB::ID key("vdb.remove");
        db.set(key, 42);
        CHECK(db.contains(key));
        CHECK(db.remove(key));
        CHECK_FALSE(db.contains(key));
        CHECK_FALSE(db.remove(key));
}

TEST_CASE("VariantDatabase: clear") {
        TestDB db;
        db.set(TestDB::ID("vdb.clear1"), 1);
        db.set(TestDB::ID("vdb.clear2"), 2);
        CHECK(db.size() == 2);
        db.clear();
        CHECK(db.isEmpty());
}

TEST_CASE("VariantDatabase: size tracks entries") {
        TestDB db;
        db.set(TestDB::ID("vdb.size1"), 1);
        CHECK(db.size() == 1);
        db.set(TestDB::ID("vdb.size2"), 2);
        CHECK(db.size() == 2);
        db.remove(TestDB::ID("vdb.size1"));
        CHECK(db.size() == 1);
}

TEST_CASE("VariantDatabase: stores different variant types") {
        TestDB     db;
        TestDB::ID intKey("vdb.type.int");
        TestDB::ID strKey("vdb.type.str");
        TestDB::ID dblKey("vdb.type.dbl");
        TestDB::ID boolKey("vdb.type.bool");

        db.set(intKey, 42);
        db.set(strKey, String("hello"));
        db.set(dblKey, 3.14);
        db.set(boolKey, true);

        CHECK(db.get(intKey).get<int32_t>() == 42);
        CHECK(db.get(strKey).get<String>() == "hello");
        CHECK(db.get(dblKey).get<double>() == doctest::Approx(3.14));
        CHECK(db.get(boolKey).get<bool>() == true);
}

TEST_CASE("VariantDatabase: ids returns stored IDs") {
        TestDB     db;
        TestDB::ID a("vdb.ids.a");
        TestDB::ID b("vdb.ids.b");
        db.set(a, 1);
        db.set(b, 2);
        auto idList = db.ids();
        CHECK(idList.size() == 2);
        bool foundA = false;
        bool foundB = false;
        for (size_t i = 0; i < idList.size(); ++i) {
                if (idList[i] == a) foundA = true;
                if (idList[i] == b) foundB = true;
        }
        CHECK(foundA);
        CHECK(foundB);
}

TEST_CASE("VariantDatabase: separate tag types have independent ID spaces") {
        using DBA = VariantDatabase<"IndepTagA">;
        using DBB = VariantDatabase<"IndepTagB">;

        DBA::ID idA("shared.name");
        DBB::ID idB("shared.name");

        CHECK(idA.isValid());
        CHECK(idB.isValid());
        CHECK(idA.name() == "shared.name");
        CHECK(idB.name() == "shared.name");
}

// ============================================================================
// getAs
// ============================================================================

TEST_CASE("VariantDatabase: getAs returns typed value") {
        TestDB     db;
        TestDB::ID key("vdb.getas.int");
        db.set(key, 42);
        CHECK(db.getAs<int32_t>(key) == 42);
}

TEST_CASE("VariantDatabase: getAs returns default for missing ID") {
        TestDB     db;
        TestDB::ID key("vdb.getas.missing");
        CHECK(db.getAs<int32_t>(key, 99) == 99);
}

TEST_CASE("VariantDatabase: getAs returns default on conversion failure") {
        TestDB     db;
        TestDB::ID key("vdb.getas.baconv");
        db.set(key, String("not-a-uuid"));
        UUID result = db.getAs<UUID>(key, UUID());
        CHECK_FALSE(result.isValid());
}

TEST_CASE("VariantDatabase: getAs with error output") {
        TestDB     db;
        TestDB::ID key("vdb.getas.err");
        db.set(key, 42);

        Error   err;
        int32_t val = db.getAs<int32_t>(key, 0, &err);
        CHECK(val == 42);
        CHECK_FALSE(err.isError());
}

TEST_CASE("VariantDatabase: getAs error on missing") {
        TestDB     db;
        TestDB::ID key("vdb.getas.errmiss");

        Error   err;
        int32_t val = db.getAs<int32_t>(key, -1, &err);
        CHECK(val == -1);
        CHECK(err == Error::IdNotFound);
}

TEST_CASE("VariantDatabase: getAs error on conversion failure") {
        TestDB     db;
        TestDB::ID key("vdb.getas.errconv");
        db.set(key, String("not-a-uuid"));

        Error err;
        UUID  val = db.getAs<UUID>(key, UUID(), &err);
        CHECK_FALSE(val.isValid());
        CHECK(err == Error::ConversionFailed);
}

TEST_CASE("VariantDatabase: getAs converts between types") {
        TestDB     db;
        TestDB::ID key("vdb.getas.conv");
        db.set(key, 42);
        String s = db.getAs<String>(key);
        CHECK(s == "42");
}

// ============================================================================
// forEach
// ============================================================================

TEST_CASE("VariantDatabase: forEach iterates all entries") {
        TestDB     db;
        TestDB::ID a("vdb.foreach.a");
        TestDB::ID b("vdb.foreach.b");
        TestDB::ID c("vdb.foreach.c");
        db.set(a, 1);
        db.set(b, 2);
        db.set(c, 3);

        int count = 0;
        int sum = 0;
        db.forEach([&](TestDB::ID id, const Variant &val) {
                count++;
                sum += val.get<int32_t>();
        });
        CHECK(count == 3);
        CHECK(sum == 6);
}

TEST_CASE("VariantDatabase: forEach on empty database") {
        TestDB db;
        int    count = 0;
        db.forEach([&](TestDB::ID, const Variant &) { count++; });
        CHECK(count == 0);
}

// ============================================================================
// merge
// ============================================================================

TEST_CASE("VariantDatabase: merge adds entries from other") {
        TestDB db1;
        TestDB db2;
        db1.set(TestDB::ID("vdb.merge.a"), 1);
        db2.set(TestDB::ID("vdb.merge.b"), 2);

        db1.merge(db2);
        CHECK(db1.size() == 2);
        CHECK(db1.getAs<int32_t>(TestDB::ID("vdb.merge.a")) == 1);
        CHECK(db1.getAs<int32_t>(TestDB::ID("vdb.merge.b")) == 2);
}

TEST_CASE("VariantDatabase: merge overwrites on conflict") {
        TestDB     db1;
        TestDB     db2;
        TestDB::ID key("vdb.merge.conflict");
        db1.set(key, 1);
        db2.set(key, 2);

        db1.merge(db2);
        CHECK(db1.size() == 1);
        CHECK(db1.getAs<int32_t>(key) == 2);
}

TEST_CASE("VariantDatabase: merge from empty is no-op") {
        TestDB db1;
        TestDB db2;
        db1.set(TestDB::ID("vdb.merge.noop"), 1);
        db1.merge(db2);
        CHECK(db1.size() == 1);
}

TEST_CASE("VariantDatabase: merge into empty copies all") {
        TestDB db1;
        TestDB db2;
        db2.set(TestDB::ID("vdb.merge.all.a"), 1);
        db2.set(TestDB::ID("vdb.merge.all.b"), 2);
        db1.merge(db2);
        CHECK(db1.size() == 2);
}

TEST_CASE("VariantDatabase: config layering with merge") {
        TestDB defaults;
        TestDB user;

        TestDB::ID width("vdb.layer.width");
        TestDB::ID height("vdb.layer.height");
        TestDB::ID quality("vdb.layer.quality");

        defaults.set(width, 1920);
        defaults.set(height, 1080);
        defaults.set(quality, String("high"));

        user.set(quality, String("medium"));

        TestDB effective = defaults;
        effective.merge(user);

        CHECK(effective.getAs<int32_t>(width) == 1920);
        CHECK(effective.getAs<int32_t>(height) == 1080);
        CHECK(effective.getAs<String>(quality) == "medium");
}

// ============================================================================
// extract
// ============================================================================

TEST_CASE("VariantDatabase: extract subset") {
        TestDB     db;
        TestDB::ID a("vdb.extract.a");
        TestDB::ID b("vdb.extract.b");
        TestDB::ID c("vdb.extract.c");
        db.set(a, 1);
        db.set(b, 2);
        db.set(c, 3);

        List<TestDB::ID> wanted;
        wanted.pushToBack(a);
        wanted.pushToBack(c);

        TestDB subset = db.extract(wanted);
        CHECK(subset.size() == 2);
        CHECK(subset.getAs<int32_t>(a) == 1);
        CHECK(subset.getAs<int32_t>(c) == 3);
        CHECK_FALSE(subset.contains(b));
}

TEST_CASE("VariantDatabase: extract skips missing IDs") {
        TestDB     db;
        TestDB::ID a("vdb.extract.skip.a");
        TestDB::ID b("vdb.extract.skip.b");
        db.set(a, 1);

        List<TestDB::ID> wanted;
        wanted.pushToBack(a);
        wanted.pushToBack(b);

        TestDB subset = db.extract(wanted);
        CHECK(subset.size() == 1);
        CHECK(subset.contains(a));
        CHECK_FALSE(subset.contains(b));
}

TEST_CASE("VariantDatabase: extract empty list") {
        TestDB db;
        db.set(TestDB::ID("vdb.extract.empty"), 1);

        List<TestDB::ID> wanted;
        TestDB           subset = db.extract(wanted);
        CHECK(subset.isEmpty());
}

TEST_CASE("VariantDatabase: extract from empty database") {
        TestDB           db;
        List<TestDB::ID> wanted;
        wanted.pushToBack(TestDB::ID("vdb.extract.fromempty"));

        TestDB subset = db.extract(wanted);
        CHECK(subset.isEmpty());
}

// ============================================================================
// JSON serialization
// ============================================================================

TEST_CASE("VariantDatabase: toJson roundtrip") {
        using DB = VariantDatabase<"JsonTag">;

        DB db;
        db.set(DB::ID("json.int"), 42);
        db.set(DB::ID("json.str"), String("hello"));
        db.set(DB::ID("json.dbl"), 3.14);
        db.set(DB::ID("json.bool"), true);

        JsonObject json = db.toJson();
        CHECK(json.contains("json.int"));
        CHECK(json.contains("json.str"));
        CHECK(json.contains("json.dbl"));
        CHECK(json.contains("json.bool"));

        DB db2 = DB::fromJson(json);
        CHECK(db2.size() == 4);
        CHECK(db2.get(DB::ID("json.int")).get<int32_t>() == 42);
        CHECK(db2.get(DB::ID("json.str")).get<String>() == "hello");
        CHECK(db2.get(DB::ID("json.dbl")).get<double>() == doctest::Approx(3.14));
        CHECK(db2.get(DB::ID("json.bool")).get<bool>() == true);
}

TEST_CASE("VariantDatabase: toJson empty database") {
        using DB = VariantDatabase<"JsonEmptyTag">;

        DB         db;
        JsonObject json = db.toJson();
        CHECK(json.size() == 0);
}

TEST_CASE("VariantDatabase: fromJson empty object") {
        using DB = VariantDatabase<"JsonEmptyTag2">;

        JsonObject json;
        DB         db = DB::fromJson(json);
        CHECK(db.isEmpty());
}

TEST_CASE("VariantDatabase: toJson string roundtrip") {
        using DB = VariantDatabase<"JsonStrTag">;

        DB db;
        db.set(DB::ID("jstr.key"), String("value"));

        String     jsonStr = db.toJson().toString();
        JsonObject parsed = JsonObject::parse(jsonStr);
        DB         db2 = DB::fromJson(parsed);
        CHECK(db2.get(DB::ID("jstr.key")).get<String>() == "value");
}

// ----------------------------------------------------------------------------
// setFromJson / fromJson spec-driven coercion
// ----------------------------------------------------------------------------

TEST_CASE("VariantDatabase: setFromJson coerces JSON strings via registered spec") {
        // Declare a typed key whose native form is Size2D.  toJson()
        // emits the Size2D as a String for wire purposes; fromJson()
        // must route the round-trip through the spec so the stored
        // Variant ends up as TypeSize2D, not TypeString.
        using DB = VariantDatabase<"SpecCoerceTag">;
        const DB::ID sizeId = DB::declareID(
                "size",
                VariantSpec().setType(Variant::TypeSize2D).setDefault(Size2Du32()).setDescription("Image size."));

        DB db;
        CHECK(db.set(sizeId, Size2Du32(1920, 1080)));

        JsonObject json = db.toJson();
        // Round-trip via an explicit serialization so we're testing the
        // realistic "write-then-read" path.
        String     text = json.toString();
        JsonObject reparsed = JsonObject::parse(text);

        DB db2 = DB::fromJson(reparsed);
        CHECK(db2.get(sizeId).type() == Variant::TypeSize2D);
        CHECK(db2.get(sizeId).get<Size2Du32>() == Size2Du32(1920, 1080));
}

TEST_CASE("VariantDatabase: setFromJson leaves legitimate strings alone") {
        // When the spec says TypeString, a string in JSON is a string
        // in memory — no reparsing should kick in.
        using DB = VariantDatabase<"SpecStringPassthruTag">;
        const DB::ID nameId = DB::declareID(
                "name", VariantSpec().setType(Variant::TypeString).setDefault(String()).setDescription("Any string."));

        DB db;
        db.set(nameId, String("hello world"));
        DB db2 = DB::fromJson(db.toJson());

        CHECK(db2.get(nameId).type() == Variant::TypeString);
        CHECK(db2.get(nameId).get<String>() == "hello world");
}

TEST_CASE("VariantDatabase: setFromJson falls back to raw value when no spec") {
        // Ad-hoc keys (no declareID) have no spec — the raw JSON-decoded
        // Variant should land in the database untouched.
        using DB = VariantDatabase<"SpecAdhocTag">;

        DB db;
        CHECK(db.setFromJson(DB::ID("ad.int"), Variant(int64_t(7))).isOk());
        CHECK(db.setFromJson(DB::ID("ad.str"), Variant(String("raw"))).isOk());

        CHECK(db.get(DB::ID("ad.int")).get<int64_t>() == 7);
        CHECK(db.get(DB::ID("ad.str")).get<String>() == "raw");
}

TEST_CASE("VariantDatabase: default validation mode is Strict") {
        // The default constructor must hand back a Strict-mode database
        // so HTTP / JSON / config loaders that lean on the default
        // refuse out-of-spec values automatically rather than silently
        // storing them with a warning.
        using DB = VariantDatabase<"DefaultStrictTag">;
        DB db;
        CHECK(db.validation() == SpecValidation::Strict);
}

TEST_CASE("VariantDatabase: set surfaces validator error in Strict mode") {
        // A spec with a numeric range should reject out-of-range
        // values and report Error::OutOfRange via the new err
        // out-param.
        using DB = VariantDatabase<"StrictRangeTag">;
        const DB::ID rangedId = DB::declareID("ranged", VariantSpec()
                                                                .setType(Variant::TypeS32)
                                                                .setDefault(int32_t(50))
                                                                .setRange(int32_t(1), int32_t(100))
                                                                .setDescription("Ranged int."));

        DB db;
        REQUIRE(db.validation() == SpecValidation::Strict);

        Error err;
        CHECK(db.set(rangedId, int32_t(50), &err));
        CHECK(err.isOk());

        Error rangeErr;
        CHECK_FALSE(db.set(rangedId, int32_t(200), &rangeErr));
        CHECK(rangeErr == Error::OutOfRange);
        // Original value must remain intact after the rejected write.
        CHECK(db.get(rangedId).get<int32_t>() == 50);
}

TEST_CASE("VariantDatabase: setFromJson surfaces parseString failure for unparseable strings") {
        // Spec wants Size2D, JSON delivers a non-parseable string —
        // the spec-driven coercion attempt must surface the
        // VariantSpec::parseString error (ConversionFailed) rather
        // than falling back to storing the raw string (which would
        // itself fail spec validation under Strict mode and leave
        // the failure invisible to callers).
        using DB = VariantDatabase<"ParseFailTag">;
        const DB::ID sizeId = DB::declareID(
                "size",
                VariantSpec().setType(Variant::TypeSize2D).setDefault(Size2Du32()).setDescription("Image size."));

        DB    db;
        Error err = db.setFromJson(sizeId, Variant(String("not-a-size")));
        CHECK(err.isError());
        CHECK(err == Error::ConversionFailed);
        CHECK_FALSE(db.contains(sizeId));
}

TEST_CASE("VariantDatabase: setFromJson returns Error::Invalid on Strict spec rejection") {
        // The wire value parses fine and matches the spec's type but
        // violates the declared range — Strict mode must surface the
        // validator's specific Error code (OutOfRange) all the way
        // back out through setFromJson.
        using DB = VariantDatabase<"StrictRejectTag">;
        const DB::ID id = DB::declareID("port", VariantSpec()
                                                        .setType(Variant::TypeS32)
                                                        .setDefault(int32_t(8080))
                                                        .setRange(int32_t(1), int32_t(65535))
                                                        .setDescription("TCP port."));

        DB    db;
        Error err = db.setFromJson(id, Variant(int32_t(99999)));
        CHECK(err == Error::OutOfRange);
        CHECK_FALSE(db.contains(id));
}

TEST_CASE("VariantDatabase: setFromJson returns Ok in Warn mode even when validator complains") {
        // In Warn mode the value is stored despite the validator's
        // protest, so the caller-facing return must be Ok — anything
        // else would be a contract violation against existing Warn
        // semantics.
        using DB = VariantDatabase<"WarnModeTag">;
        const DB::ID id = DB::declareID("warn.port", VariantSpec()
                                                             .setType(Variant::TypeS32)
                                                             .setDefault(int32_t(8080))
                                                             .setRange(int32_t(1), int32_t(65535))
                                                             .setDescription("TCP port."));

        DB db;
        db.setValidation(SpecValidation::Warn);
        Error err = db.setFromJson(id, Variant(int32_t(99999)));
        CHECK(err.isOk());
        CHECK(db.get(id).get<int32_t>() == 99999);
}

// ============================================================================
// DataStream serialization
// ============================================================================

TEST_CASE("VariantDatabase: DataStream roundtrip") {
        using DB = VariantDatabase<"DSTag">;

        DB db;
        db.set(DB::ID("ds.int"), 100);
        db.set(DB::ID("ds.str"), String("world"));
        db.set(DB::ID("ds.dbl"), 2.718);
        db.set(DB::ID("ds.bool"), false);

        // Write
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream writer = DataStream::createWriter(&dev);
        writer << db;
        CHECK(writer.status() == DataStream::Ok);

        // Read
        dev.seek(0);
        DataStream reader = DataStream::createReader(&dev);
        DB         db2;
        reader >> db2;
        CHECK(reader.status() == DataStream::Ok);

        CHECK(db2.size() == 4);
        CHECK(db2.get(DB::ID("ds.int")).get<int32_t>() == 100);
        CHECK(db2.get(DB::ID("ds.str")).get<String>() == "world");
        CHECK(db2.get(DB::ID("ds.dbl")).get<double>() == doctest::Approx(2.718));
        CHECK(db2.get(DB::ID("ds.bool")).get<bool>() == false);
}

TEST_CASE("VariantDatabase: DataStream empty roundtrip") {
        using DB = VariantDatabase<"DSEmptyTag">;

        DB db;

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream writer = DataStream::createWriter(&dev);
        writer << db;
        CHECK(writer.status() == DataStream::Ok);

        dev.seek(0);
        DataStream reader = DataStream::createReader(&dev);
        DB         db2;
        db2.set(DB::ID("ds.empty.pre"), 1); // should be cleared
        reader >> db2;
        CHECK(reader.status() == DataStream::Ok);
        CHECK(db2.isEmpty());
}

// ============================================================================
// TextStream serialization
// ============================================================================

TEST_CASE("VariantDatabase: TextStream output") {
        using DB = VariantDatabase<"TSTag">;

        DB db;
        db.set(DB::ID("ts.name"), String("test"));
        db.set(DB::ID("ts.value"), 42);

        String     output;
        TextStream stream(&output);
        stream << db;
        stream.flush();

        CHECK(output.contains("ts.name"));
        CHECK(output.contains("test"));
        CHECK(output.contains("ts.value"));
        CHECK(output.contains("42"));
}

TEST_CASE("VariantDatabase: TextStream empty output") {
        using DB = VariantDatabase<"TSEmptyTag">;

        DB         db;
        String     output;
        TextStream stream(&output);
        stream << db;
        stream.flush();

        CHECK(output.isEmpty());
}

// ============================================================
// Equality
// ============================================================

TEST_CASE("VariantDatabase: equality empty") {
        using DB = VariantDatabase<"EqEmptyTag">;

        DB a;
        DB b;
        CHECK(a == b);
        CHECK_FALSE(a != b);
}

TEST_CASE("VariantDatabase: equality matching") {
        using DB = VariantDatabase<"EqMatchTag">;
        DB::ID name("name");
        DB::ID count("count");

        DB a;
        a.set(name, String("hello"));
        a.set(count, int32_t(42));

        DB b;
        b.set(name, String("hello"));
        b.set(count, int32_t(42));

        CHECK(a == b);
}

TEST_CASE("VariantDatabase: inequality different values") {
        using DB = VariantDatabase<"EqDiffValTag">;
        DB::ID name("name");

        DB a;
        a.set(name, String("hello"));
        DB b;
        b.set(name, String("world"));

        CHECK(a != b);
}

TEST_CASE("VariantDatabase: inequality different keys") {
        using DB = VariantDatabase<"EqDiffKeyTag">;
        DB::ID k1("k1");
        DB::ID k2("k2");

        DB a;
        a.set(k1, int32_t(1));
        DB b;
        b.set(k2, int32_t(1));

        CHECK(a != b);
}

TEST_CASE("VariantDatabase: inequality different size") {
        using DB = VariantDatabase<"EqDiffSzTag">;
        DB::ID k1("k1");
        DB::ID k2("k2");

        DB a;
        a.set(k1, int32_t(1));
        DB b;
        b.set(k1, int32_t(1));
        b.set(k2, int32_t(2));

        CHECK(a != b);
}

// ============================================================================
// format() template substitution
// ============================================================================

TEST_CASE("VariantDatabase: format substitutes a single key with default spec") {
        using DB = VariantDatabase<"FmtTagBasic">;
        DB::ID name("Name");
        DB     db;
        db.set(name, String("Alice"));
        CHECK(db.format("Hello {Name}!") == String("Hello Alice!"));
}

TEST_CASE("VariantDatabase: format dispatches per-type spec to VideoFormat") {
        using DB = VariantDatabase<"FmtTagVF">;
        DB::ID vf("VideoFormat");
        DB     db;
        db.set(vf, VideoFormat(VideoFormat::Smpte1080p29_97));
        CHECK(db.format("The video format is {VideoFormat:smpte}") == String("The video format is 1080p29.97"));
        CHECK(db.format("Named: {VideoFormat:named}") == String("Named: HDp29.97"));
}

TEST_CASE("VariantDatabase: format dispatches per-type spec to Timecode") {
        using DB = VariantDatabase<"FmtTagTc">;
        DB::ID tc("Timecode");
        DB     db;
        db.set(tc, Timecode(Timecode::NDF24, 1, 0, 0, 0));
        CHECK(db.format("TC={Timecode:smpte}") == String("TC=01:00:00:00"));
}

TEST_CASE("VariantDatabase: format applies std::format spec to primitives") {
        using DB = VariantDatabase<"FmtTagNum">;
        DB::ID n("Count");
        DB     db;
        db.set(n, int32_t(42));
        CHECK(db.format("{Count:05d}") == String("00042"));
        CHECK(db.format("hex={Count:x}") == String("hex=2a"));
}

TEST_CASE("VariantDatabase: format replaces missing key with sentinel") {
        using DB = VariantDatabase<"FmtTagMiss">;
        DB db;
        CHECK(db.format("Value: {NotThere}") == String("Value: [UNKNOWN KEY: NotThere]"));
        CHECK(db.format("Value: {NotThere:smpte}") == String("Value: [UNKNOWN KEY: NotThere]"));
}

TEST_CASE("VariantDatabase: format reports IdNotFound when a key is missing") {
        using DB = VariantDatabase<"FmtTagErr">;
        DB::ID present("Present");
        DB     db;
        db.set(present, int32_t(1));

        Error  err;
        String s = db.format("{Present} and {Absent}", &err);
        CHECK(err == Error::IdNotFound);
        CHECK(s == String("1 and [UNKNOWN KEY: Absent]"));

        // All keys resolved -> Error::Ok.
        Error  ok;
        String s2 = db.format("{Present}", &ok);
        CHECK(ok.isOk());
        CHECK(s2 == String("1"));
}

TEST_CASE("VariantDatabase: format consults resolver for missing keys") {
        using DB = VariantDatabase<"FmtTagResolver">;
        DB::ID a("A");
        DB     db;
        db.set(a, int32_t(7));

        auto resolver = [](const String &key, const String &spec) -> std::optional<String> {
                if (key == String("B")) {
                        // Echo the spec back so we can verify it was forwarded.
                        return String("B-resolved(") + spec + String(")");
                }
                return std::nullopt;
        };

        Error  err;
        String s = db.format("{A}/{B:custom}/{C}", resolver, &err);
        // C is still unresolved, so err must report IdNotFound, but the
        // resolver-supplied B value is interpolated cleanly.
        CHECK(err == Error::IdNotFound);
        CHECK(s == String("7/B-resolved(custom)/[UNKNOWN KEY: C]"));
}

TEST_CASE("VariantDatabase: format resolver enables database nesting") {
        using DB = VariantDatabase<"FmtTagNest">;
        DB::ID local("Local");
        DB::ID shared("Shared");

        DB parent;
        parent.set(shared, VideoFormat(VideoFormat::Smpte1080p29_97));

        DB child;
        child.set(local, String("hello"));

        Error  err;
        String s = child.format(
                "{Local} on {Shared:smpte}",
                [&parent](const String &key, const String &spec) -> std::optional<String> {
                        DB::ID id = DB::ID::find(key);
                        if (!id.isValid() || !parent.contains(id)) return std::nullopt;
                        return parent.get(id).format(spec);
                },
                &err);
        CHECK(err.isOk());
        CHECK(s == String("hello on 1080p29.97"));
}

TEST_CASE("VariantDatabase: format resolver returning nullopt falls back to sentinel") {
        using DB = VariantDatabase<"FmtTagResolverMiss">;
        DB     db;
        Error  err;
        String s = db.format(
                "{X}", [](const String &, const String &) -> std::optional<String> { return std::nullopt; }, &err);
        CHECK(err == Error::IdNotFound);
        CHECK(s == String("[UNKNOWN KEY: X]"));
}

TEST_CASE("VariantDatabase: format honors {{ and }} escapes") {
        using DB = VariantDatabase<"FmtTagEsc">;
        DB::ID v("V");
        DB     db;
        db.set(v, int32_t(7));
        CHECK(db.format("literal {{ and }} braces, value={V}") == String("literal { and } braces, value=7"));
}

TEST_CASE("VariantDatabase: format renders multiple tokens in one pass") {
        using DB = VariantDatabase<"FmtTagMulti">;
        DB::ID vf("VideoFormat");
        DB::ID tc("Timecode");
        DB::ID who("Who");
        DB     db;
        db.set(vf, VideoFormat(VideoFormat::Smpte1080p29_97));
        db.set(tc, Timecode(Timecode::NDF24, 1, 2, 3, 4));
        db.set(who, String("rec1"));
        String out = db.format("[{Who}] {VideoFormat:smpte} @ {Timecode:smpte}");
        CHECK(out == String("[rec1] 1080p29.97 @ 01:02:03:04"));
}

TEST_CASE("VariantDatabase: format leaves text without tokens unchanged") {
        using DB = VariantDatabase<"FmtTagPass">;
        DB db;
        CHECK(db.format("no tokens here") == String("no tokens here"));
}

// ============================================================================
// Nested-key format substitution (VariantList / VariantMap descent)
// ============================================================================

TEST_CASE("VariantDatabase: format descends into VariantMap value") {
        using DB = VariantDatabase<"FmtTagNestedMap">;
        DB::ID who("Who");
        DB     db;

        VariantMap who_map;
        who_map.insert("first", Variant(String("Alice")));
        who_map.insert("last", Variant(String("Smith")));
        db.set(who, Variant(who_map));

        // {Who.first} resolves to the inner string via promekiResolveVariantPath.
        CHECK(db.format("hi {Who.first}!") == String("hi Alice!"));
        CHECK(db.format("{Who.last}, {Who.first}") == String("Smith, Alice"));
}

TEST_CASE("VariantDatabase: format descends into VariantList by index") {
        using DB = VariantDatabase<"FmtTagNestedList">;
        DB::ID tags("Tags");
        DB     db;

        VariantList list;
        list.pushToBack(Variant(String("alpha")));
        list.pushToBack(Variant(String("beta")));
        list.pushToBack(Variant(String("gamma")));
        db.set(tags, Variant(list));

        CHECK(db.format("{Tags[1]}") == String("beta"));
        CHECK(db.format("{Tags[0]}/{Tags[2]}") == String("alpha/gamma"));
}

TEST_CASE("VariantDatabase: format reports unknown nested key cleanly") {
        using DB = VariantDatabase<"FmtTagNestedMiss">;
        DB::ID who("Who");
        DB     db;
        VariantMap m;
        m.insert("name", Variant(String("Alice")));
        db.set(who, Variant(m));

        Error  err;
        String s = db.format("{Who.missing}", &err);
        CHECK(err == Error::IdNotFound);
        CHECK(s == String("[UNKNOWN KEY: Who.missing]"));
}

// ============================================================================
// Recursive setFromJson coercion (nested element / value spec)
// ============================================================================

namespace {
        // Tag wrapper exposes ID declarations as static inline members so
        // declareID's spec sticks for the whole TU run.
        struct CoerceDB : public VariantDatabase<"CoerceTagListOfInts"> {
                        // List of ints: declared with element-spec so JSON
                        // strings like ["1","2","3"] parse through to int32.
                        PROMEKI_DECLARE_ID(IntList,
                                           VariantSpec()
                                                   .setType(Variant::TypeVariantList)
                                                   .setElementSpec(VariantSpec().setType(Variant::TypeS32)));
        };

        struct CoerceMapDB : public VariantDatabase<"CoerceTagMapOfInts"> {
                        PROMEKI_DECLARE_ID(IntMap,
                                           VariantSpec()
                                                   .setType(Variant::TypeVariantMap)
                                                   .setValueSpec(VariantSpec().setType(Variant::TypeS32)));
        };
} // namespace

TEST_CASE("VariantDatabase: setFromJson coerces nested list elements") {
        // JSON values arrive as strings; the spec's elementSpec asks for
        // TypeS32, so each element should round-trip via parseString.
        VariantList incoming;
        incoming.pushToBack(Variant(String("1")));
        incoming.pushToBack(Variant(String("2")));
        incoming.pushToBack(Variant(String("3")));

        CoerceDB db;
        Error    err = db.setFromJson(CoerceDB::IntList, Variant(incoming));
        CHECK(err.isOk());

        Variant stored = db.get(CoerceDB::IntList);
        REQUIRE(stored.type() == Variant::TypeVariantList);
        VariantList out = stored.get<VariantList>();
        REQUIRE(out.size() == 3);
        CHECK(out[0].type() == Variant::TypeS32);
        CHECK(out[0].get<int32_t>() == 1);
        CHECK(out[1].get<int32_t>() == 2);
        CHECK(out[2].get<int32_t>() == 3);
}

TEST_CASE("VariantDatabase: setFromJson coerces nested map values") {
        VariantMap incoming;
        incoming.insert("a", Variant(String("10")));
        incoming.insert("b", Variant(String("20")));

        CoerceMapDB db;
        Error       err = db.setFromJson(CoerceMapDB::IntMap, Variant(incoming));
        CHECK(err.isOk());

        Variant stored = db.get(CoerceMapDB::IntMap);
        REQUIRE(stored.type() == Variant::TypeVariantMap);
        VariantMap out = stored.get<VariantMap>();
        CHECK(out.value("a").type() == Variant::TypeS32);
        CHECK(out.value("a").get<int32_t>() == 10);
        CHECK(out.value("b").get<int32_t>() == 20);
}

TEST_CASE("VariantDatabase: setFromJson reports nested coercion failure") {
        VariantList incoming;
        incoming.pushToBack(Variant(String("1")));
        incoming.pushToBack(Variant(String("not-a-number")));

        CoerceDB db;
        Error    err = db.setFromJson(CoerceDB::IntList, Variant(incoming));
        CHECK(err.isError());
        // Value should not have been stored on failure.
        CHECK_FALSE(db.contains(CoerceDB::IntList));
}

TEST_CASE("VariantDatabase: fromJson round-trips through nested coercion") {
        // End-to-end: parse a JSON object with a nested array of strings,
        // route it through the database's fromJson pipeline, and verify
        // that the spec's element-spec coerced each string to int.  The
        // CoerceDB subclass exists only to host the PROMEKI_DECLARE_ID
        // call; the Tag's spec registry is shared with the underlying
        // VariantDatabase template, so the base form picks up the spec.
        using DB = VariantDatabase<"CoerceTagListOfInts">;
        JsonObject  jo = JsonObject::parse(String(R"({"IntList":["7","8","9"]})"));
        DB          db = DB::fromJson(jo);
        VariantList out = db.get(CoerceDB::IntList).get<VariantList>();
        REQUIRE(out.size() == 3);
        CHECK(out[0].type() == Variant::TypeS32);
        CHECK(out[0].get<int32_t>() == 7);
        CHECK(out[2].get<int32_t>() == 9);
}
