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
        TestDB db;
        TestDB::ID width("vdb.width");
        db.set(width, 1920);
        Variant v = db.get(width);
        CHECK(v.isValid());
        CHECK(v.get<int32_t>() == 1920);
}

TEST_CASE("VariantDatabase: get returns default for missing ID") {
        TestDB db;
        TestDB::ID missing("vdb.missing");
        Variant v = db.get(missing);
        CHECK_FALSE(v.isValid());
}

TEST_CASE("VariantDatabase: get with custom default") {
        TestDB db;
        TestDB::ID missing("vdb.missing2");
        Variant v = db.get(missing, Variant(42));
        CHECK(v.get<int32_t>() == 42);
}

TEST_CASE("VariantDatabase: contains") {
        TestDB db;
        TestDB::ID key("vdb.contains");
        CHECK_FALSE(db.contains(key));
        db.set(key, String("hello"));
        CHECK(db.contains(key));
}

TEST_CASE("VariantDatabase: setIfMissing stores when absent") {
        TestDB db;
        TestDB::ID key("vdb.setIfMissing.absent");
        bool stored = db.setIfMissing(key, String("hello"));
        CHECK(stored);
        CHECK(db.contains(key));
        CHECK(db.get(key).get<String>() == "hello");
}

TEST_CASE("VariantDatabase: setIfMissing preserves existing value") {
        TestDB db;
        TestDB::ID key("vdb.setIfMissing.present");
        db.set(key, String("first"));
        bool stored = db.setIfMissing(key, String("second"));
        CHECK_FALSE(stored);
        CHECK(db.get(key).get<String>() == "first");
}

TEST_CASE("VariantDatabase: set overwrites existing value") {
        TestDB db;
        TestDB::ID key("vdb.overwrite");
        db.set(key, 100);
        db.set(key, 200);
        CHECK(db.get(key).get<int32_t>() == 200);
        CHECK(db.size() == 1);
}

TEST_CASE("VariantDatabase: remove") {
        TestDB db;
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
        TestDB db;
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
        TestDB db;
        TestDB::ID a("vdb.ids.a");
        TestDB::ID b("vdb.ids.b");
        db.set(a, 1);
        db.set(b, 2);
        auto idList = db.ids();
        CHECK(idList.size() == 2);
        bool foundA = false;
        bool foundB = false;
        for(size_t i = 0; i < idList.size(); ++i) {
                if(idList[i] == a) foundA = true;
                if(idList[i] == b) foundB = true;
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
        TestDB db;
        TestDB::ID key("vdb.getas.int");
        db.set(key, 42);
        CHECK(db.getAs<int32_t>(key) == 42);
}

TEST_CASE("VariantDatabase: getAs returns default for missing ID") {
        TestDB db;
        TestDB::ID key("vdb.getas.missing");
        CHECK(db.getAs<int32_t>(key, 99) == 99);
}

TEST_CASE("VariantDatabase: getAs returns default on conversion failure") {
        TestDB db;
        TestDB::ID key("vdb.getas.baconv");
        db.set(key, String("not-a-uuid"));
        UUID result = db.getAs<UUID>(key, UUID());
        CHECK_FALSE(result.isValid());
}

TEST_CASE("VariantDatabase: getAs with error output") {
        TestDB db;
        TestDB::ID key("vdb.getas.err");
        db.set(key, 42);

        Error err;
        int32_t val = db.getAs<int32_t>(key, 0, &err);
        CHECK(val == 42);
        CHECK_FALSE(err.isError());
}

TEST_CASE("VariantDatabase: getAs error on missing") {
        TestDB db;
        TestDB::ID key("vdb.getas.errmiss");

        Error err;
        int32_t val = db.getAs<int32_t>(key, -1, &err);
        CHECK(val == -1);
        CHECK(err == Error::IdNotFound);
}

TEST_CASE("VariantDatabase: getAs error on conversion failure") {
        TestDB db;
        TestDB::ID key("vdb.getas.errconv");
        db.set(key, String("not-a-uuid"));

        Error err;
        UUID val = db.getAs<UUID>(key, UUID(), &err);
        CHECK_FALSE(val.isValid());
        CHECK(err == Error::ConversionFailed);
}

TEST_CASE("VariantDatabase: getAs converts between types") {
        TestDB db;
        TestDB::ID key("vdb.getas.conv");
        db.set(key, 42);
        String s = db.getAs<String>(key);
        CHECK(s == "42");
}

// ============================================================================
// forEach
// ============================================================================

TEST_CASE("VariantDatabase: forEach iterates all entries") {
        TestDB db;
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
        int count = 0;
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
        TestDB db1;
        TestDB db2;
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
        TestDB db;
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
        TestDB db;
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
        TestDB subset = db.extract(wanted);
        CHECK(subset.isEmpty());
}

TEST_CASE("VariantDatabase: extract from empty database") {
        TestDB db;
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

        DB db;
        JsonObject json = db.toJson();
        CHECK(json.size() == 0);
}

TEST_CASE("VariantDatabase: fromJson empty object") {
        using DB = VariantDatabase<"JsonEmptyTag2">;

        JsonObject json;
        DB db = DB::fromJson(json);
        CHECK(db.isEmpty());
}

TEST_CASE("VariantDatabase: toJson string roundtrip") {
                using DB = VariantDatabase<"JsonStrTag">;

        DB db;
        db.set(DB::ID("jstr.key"), String("value"));

        String jsonStr = db.toJson().toString();
        JsonObject parsed = JsonObject::parse(jsonStr);
        DB db2 = DB::fromJson(parsed);
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
        const DB::ID sizeId = DB::declareID("size",
                VariantSpec().setType(Variant::TypeSize2D)
                        .setDefault(Size2Du32())
                        .setDescription("Image size."));

        DB db;
        CHECK(db.set(sizeId, Size2Du32(1920, 1080)));

        JsonObject json = db.toJson();
        // Round-trip via an explicit serialization so we're testing the
        // realistic "write-then-read" path.
        String text = json.toString();
        JsonObject reparsed = JsonObject::parse(text);

        DB db2 = DB::fromJson(reparsed);
        CHECK(db2.get(sizeId).type() == Variant::TypeSize2D);
        CHECK(db2.get(sizeId).get<Size2Du32>() == Size2Du32(1920, 1080));
}

TEST_CASE("VariantDatabase: setFromJson leaves legitimate strings alone") {
        // When the spec says TypeString, a string in JSON is a string
        // in memory — no reparsing should kick in.
        using DB = VariantDatabase<"SpecStringPassthruTag">;
        const DB::ID nameId = DB::declareID("name",
                VariantSpec().setType(Variant::TypeString)
                        .setDefault(String())
                        .setDescription("Any string."));

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
        db.setFromJson(DB::ID("ad.int"), Variant(int64_t(7)));
        db.setFromJson(DB::ID("ad.str"), Variant(String("raw")));

        CHECK(db.get(DB::ID("ad.int")).get<int64_t>() == 7);
        CHECK(db.get(DB::ID("ad.str")).get<String>() == "raw");
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
        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream writer = DataStream::createWriter(&dev);
        writer << db;
        CHECK(writer.status() == DataStream::Ok);

        // Read
        dev.seek(0);
        DataStream reader = DataStream::createReader(&dev);
        DB db2;
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

        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream writer = DataStream::createWriter(&dev);
        writer << db;
        CHECK(writer.status() == DataStream::Ok);

        dev.seek(0);
        DataStream reader = DataStream::createReader(&dev);
        DB db2;
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

        String output;
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

        DB db;
        String output;
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
