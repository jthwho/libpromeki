/**
 * @file      json.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/json.h>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// JsonObject basic
// ============================================================================

TEST_CASE("JsonObject_Basic") {
        JsonObject obj;
        CHECK(!obj.isValid());
        CHECK(obj.size() == 0);

        obj.set("name", "test");
        CHECK(obj.isValid());
        CHECK(obj.size() == 1);
        CHECK(obj.contains("name"));
        CHECK(obj.getString("name") == "test");
}

TEST_CASE("JsonObject_Types") {
        JsonObject obj;
        obj.set("bool", true);
        obj.set("int", 42);
        obj.set("double", 3.14);
        obj.set("string", "hello");
        obj.setNull("null");

        CHECK(obj.getBool("bool") == true);
        CHECK(obj.getInt("int") == 42);
        CHECK(obj.getDouble("double") > 3.13);
        CHECK(obj.getDouble("double") < 3.15);
        CHECK(obj.getString("string") == "hello");
        CHECK(obj.valueIsNull("null"));
        CHECK(!obj.valueIsNull("string"));
}

// ============================================================================
// JsonObject parse
// ============================================================================

TEST_CASE("JsonObject_Parse") {
        Error      err;
        JsonObject obj = JsonObject::parse("{\"key\": 42}", &err);
        CHECK(err.isOk());
        CHECK(obj.getInt("key") == 42);

        JsonObject bad = JsonObject::parse("not json", &err);
        CHECK(err.isError());
        CHECK(!bad.isValid());
}

// ============================================================================
// JsonObject nested
// ============================================================================

TEST_CASE("JsonObject_Nested") {
        JsonObject inner;
        inner.set("value", 99);

        JsonObject outer;
        outer.set("inner", inner);
        CHECK(outer.valueIsObject("inner"));

        Error      err;
        JsonObject got = outer.getObject("inner", &err);
        CHECK(err.isOk());
        CHECK(got.getInt("value") == 99);
}

// ============================================================================
// JsonObject copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("JsonObject_CopyIsIndependent") {
        JsonObject obj;
        obj.set("key", "original");

        JsonObject copy = obj;

        copy.set("key", "modified");
        CHECK(obj.getString("key") == "original");
        CHECK(copy.getString("key") == "modified");
}

TEST_CASE("JsonObject_CopyAddKeyIndependent") {
        JsonObject obj;
        obj.set("a", 1);

        JsonObject copy = obj;

        copy.set("b", 2);
        CHECK(obj.size() == 1);
        CHECK(copy.size() == 2);
}

// ============================================================================
// JsonObject equality
// ============================================================================

TEST_CASE("JsonObject_EqualityEmpty") {
        JsonObject a;
        JsonObject b;
        CHECK(a == b);
}

TEST_CASE("JsonObject_EqualityMatching") {
        JsonObject a;
        a.set("key", 42);
        a.set("name", "test");
        JsonObject b;
        b.set("key", 42);
        b.set("name", "test");
        CHECK(a == b);
}

TEST_CASE("JsonObject_EqualityDifferentValues") {
        JsonObject a;
        a.set("key", 42);
        JsonObject b;
        b.set("key", 99);
        CHECK_FALSE(a == b);
}

TEST_CASE("JsonObject_EqualityDifferentKeys") {
        JsonObject a;
        a.set("key", 42);
        JsonObject b;
        b.set("other", 42);
        CHECK_FALSE(a == b);
}

TEST_CASE("JsonObject_EqualityDifferentSize") {
        JsonObject a;
        a.set("key", 42);
        JsonObject b;
        a.set("key", 42);
        b.set("extra", 1);
        CHECK_FALSE(a == b);
}

// ============================================================================
// JsonObject toString
// ============================================================================

TEST_CASE("JsonObject_ToString") {
        JsonObject obj;
        obj.set("key", "value");
        String s = obj.toString();
        CHECK(s.size() > 0);
        CHECK(s.find('{') != String::npos);
}

// ============================================================================
// JsonObject clear
// ============================================================================

TEST_CASE("JsonObject_Clear") {
        JsonObject obj;
        obj.set("a", 1);
        obj.set("b", 2);
        CHECK(obj.size() == 2);

        obj.clear();
        CHECK(obj.size() == 0);
}

// ============================================================================
// JsonObject missing keys
// ============================================================================

TEST_CASE("JsonObject_MissingKeys") {
        JsonObject obj;
        Error      err;
        obj.getInt("missing", &err);
        CHECK(err.isError());

        obj.getString("missing", &err);
        CHECK(err.isError());

        CHECK(!obj.contains("missing"));
}

// ============================================================================
// JsonObject forEach
// ============================================================================

TEST_CASE("JsonObject_ForEach") {
        JsonObject obj;
        obj.set("a", 1);
        obj.set("b", 2);
        obj.set("c", 3);

        int count = 0;
        obj.forEach([&count](const String &key, const Variant &val) { count++; });
        CHECK(count == 3);
}

// ============================================================================
// JsonArray basic
// ============================================================================

TEST_CASE("JsonArray_Basic") {
        JsonArray arr;
        CHECK(!arr.isValid());
        CHECK(arr.size() == 0);

        arr.add(42);
        arr.add("hello");
        arr.add(true);
        CHECK(arr.isValid());
        CHECK(arr.size() == 3);
        CHECK(arr.getInt(0) == 42);
        CHECK(arr.getString(1) == "hello");
        CHECK(arr.getBool(2) == true);
}

TEST_CASE("JsonArray_Parse") {
        Error     err;
        JsonArray arr = JsonArray::parse("[1, 2, 3]", &err);
        CHECK(err.isOk());
        CHECK(arr.size() == 3);
        CHECK(arr.getInt(0) == 1);
        CHECK(arr.getInt(2) == 3);

        JsonArray bad = JsonArray::parse("not json", &err);
        CHECK(err.isError());
}

// ============================================================================
// JsonArray copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("JsonArray_CopyIsIndependent") {
        JsonArray arr;
        arr.add(1);
        arr.add(2);

        JsonArray copy = arr;

        copy.add(3);
        CHECK(arr.size() == 2);
        CHECK(copy.size() == 3);
}

// ============================================================================
// JsonArray nested
// ============================================================================

TEST_CASE("JsonArray_Nested") {
        JsonObject obj;
        obj.set("x", 10);

        JsonArray arr;
        arr.add(obj);
        CHECK(arr.valueIsObject(0));

        Error      err;
        JsonObject got = arr.getObject(0, &err);
        CHECK(err.isOk());
        CHECK(got.getInt("x") == 10);
}

TEST_CASE("JsonArray_NestedArray") {
        JsonArray inner;
        inner.add(1);
        inner.add(2);

        JsonArray outer;
        outer.add(inner);
        CHECK(outer.valueIsArray(0));

        Error     err;
        JsonArray got = outer.getArray(0, &err);
        CHECK(err.isOk());
        CHECK(got.size() == 2);
        CHECK(got.getInt(0) == 1);
}

// ============================================================================
// JsonArray forEach
// ============================================================================

TEST_CASE("JsonArray_ForEach") {
        JsonArray arr;
        arr.add(10);
        arr.add(20);
        arr.add(30);

        int sum = 0;
        arr.forEach([&sum](const Variant &val) { sum += val.get<int32_t>(); });
        CHECK(sum == 60);
}

// ============================================================================
// JsonObject with array
// ============================================================================

TEST_CASE("JsonObject_WithArray") {
        JsonArray arr;
        arr.add(1);
        arr.add(2);

        JsonObject obj;
        obj.set("list", arr);
        CHECK(obj.valueIsArray("list"));

        Error     err;
        JsonArray got = obj.getArray("list", &err);
        CHECK(err.isOk());
        CHECK(got.size() == 2);
}

// ============================================================================
// JsonArray bounds
// ============================================================================

TEST_CASE("JsonArray_OutOfBounds") {
        JsonArray arr;
        arr.add(1);

        Error err;
        arr.getInt(5, &err);
        CHECK(err.isError());

        arr.getInt(-1, &err);
        CHECK(err.isError());
}

// ============================================================================
// JsonArray clear
// ============================================================================

TEST_CASE("JsonArray_Clear") {
        JsonArray arr;
        arr.add(1);
        arr.add(2);
        CHECK(arr.size() == 2);

        arr.clear();
        CHECK(arr.size() == 0);
}

// ============================================================================
// JsonArray null
// ============================================================================

TEST_CASE("JsonArray_Null") {
        JsonArray arr;
        arr.addNull();
        arr.add(42);
        CHECK(arr.size() == 2);
        CHECK(arr.valueIsNull(0));
        CHECK(!arr.valueIsNull(1));
}
