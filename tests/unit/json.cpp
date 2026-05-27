/**
 * @file      json.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
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
// JsonObject copy semantics (internal copy-on-write)
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

TEST_CASE("JsonObject_MutatingOriginalAfterAliasIsIndependent") {
        // CoW must detach when *either* side mutates, not just the copy.
        JsonObject obj;
        obj.set("key", "original");

        JsonObject alias = obj;
        obj.set("key", "modified");

        CHECK(obj.getString("key") == "modified");
        CHECK(alias.getString("key") == "original");
}

TEST_CASE("JsonObject_ClearOnAliasLeavesOriginalIntact") {
        JsonObject obj;
        obj.set("a", 1);
        obj.set("b", 2);

        JsonObject alias = obj;
        alias.clear();

        CHECK(alias.size() == 0);
        CHECK(obj.size() == 2);
        CHECK(obj.getInt("a") == 1);
}

TEST_CASE("JsonObject_GetObjectReturnsIndependentSubtree") {
        // A subtree pulled out via getObject must not share storage with
        // the parent — mutating either side leaves the other unchanged.
        JsonObject inner;
        inner.set("value", 1);

        JsonObject outer;
        outer.set("inner", inner);

        JsonObject pulled = outer.getObject("inner");
        pulled.set("value", 99);

        CHECK(pulled.getInt("value") == 99);
        // Parent's nested copy is unchanged.
        JsonObject reread = outer.getObject("inner");
        CHECK(reread.getInt("value") == 1);
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
// JsonArray copy semantics (internal copy-on-write)
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

TEST_CASE("JsonArray_MutatingOriginalAfterAliasIsIndependent") {
        // CoW must detach when *either* side mutates.
        JsonArray arr;
        arr.add(1);
        arr.add(2);

        JsonArray alias = arr;
        arr.add(3);

        CHECK(arr.size() == 3);
        CHECK(alias.size() == 2);
}

TEST_CASE("JsonArray_GetArrayReturnsIndependentSubtree") {
        JsonArray inner;
        inner.add(10);

        JsonArray outer;
        outer.add(inner);

        JsonArray pulled = outer.getArray(0);
        pulled.add(20);

        CHECK(pulled.size() == 2);
        // Parent's nested array is unchanged.
        JsonArray reread = outer.getArray(0);
        CHECK(reread.size() == 1);
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

// ============================================================================
// JsonValue type queries
// ============================================================================

TEST_CASE("JsonValue_DefaultIsNull") {
        JsonValue v;
        CHECK(v.type() == JsonValue::Null);
        CHECK(v.isNull());
        CHECK(!v.isUndefined());
        CHECK(!v.isBool());
        CHECK(!v.isDouble());
        CHECK(!v.isString());
}

TEST_CASE("JsonValue_Undefined") {
        JsonValue v = JsonValue::undefined();
        CHECK(v.type() == JsonValue::Undefined);
        CHECK(v.isUndefined());
        CHECK(!v.isNull());
}

TEST_CASE("JsonValue_FromPrimitives") {
        CHECK(JsonValue(true).type() == JsonValue::Bool);
        CHECK(JsonValue(true).toBool() == true);
        CHECK(JsonValue(42).type() == JsonValue::Double);
        CHECK(JsonValue(42).toInt() == 42);
        CHECK(JsonValue(int64_t{-7}).toInt() == -7);
        CHECK(JsonValue(uint64_t{99}).toUInt() == 99);
        CHECK(JsonValue(3.5).toDouble() > 3.49);
        CHECK(JsonValue("hi").type() == JsonValue::String);
        CHECK(JsonValue("hi").toString() == "hi");
        CHECK(JsonValue(String("there")).toString() == "there");
}

TEST_CASE("JsonValue_DefaultsOnTypeMismatch") {
        JsonValue v(42);
        // toBool on a number returns the default
        CHECK(v.toBool(true) == true);
        CHECK(v.toBool(false) == false);
        CHECK(v.toString("fallback") == "fallback");
}

TEST_CASE("JsonValue_FromObjectSharesStorage") {
        // Constructing a JsonValue from a JsonObject must not deep-copy
        // the tree; refcount on the source data should bump.
        JsonObject obj;
        obj.set("a", 1);
        obj.set("b", 2);
        JsonValue v = obj;
        CHECK(v.isObject());
        // Round-trip back to JsonObject — should still see the same data.
        JsonObject got = v.toObject();
        CHECK(got.size() == 2);
        CHECK(got.getInt("b") == 2);
}

TEST_CASE("JsonValue_FromArraySharesStorage") {
        JsonArray arr;
        arr.add(10);
        arr.add(20);
        JsonValue v = arr;
        CHECK(v.isArray());
        JsonArray got = v.toArray();
        CHECK(got.size() == 2);
        CHECK(got.getInt(1) == 20);
}

TEST_CASE("JsonValue_ToVariantPrimitives") {
        Variant b = JsonValue(true).toVariant();
        CHECK(b.get<bool>() == true);
        Variant n = JsonValue(int64_t{42}).toVariant();
        CHECK(n.get<int64_t>() == 42);
}

TEST_CASE("JsonValue_FromVariant") {
        Variant   v(int64_t{99});
        JsonValue jv = JsonValue::fromVariant(v);
        CHECK(jv.isDouble());
        CHECK(jv.toInt() == 99);
}

TEST_CASE("JsonValue_Equality") {
        CHECK(JsonValue(42) == JsonValue(42));
        CHECK(JsonValue(42) != JsonValue(43));
        CHECK(JsonValue::undefined() == JsonValue::undefined());
        CHECK(JsonValue::undefined() != JsonValue());  // undefined != null
}

// ============================================================================
// JsonObject operator[] / value() / keys
// ============================================================================

TEST_CASE("JsonObject_OperatorBracketReturnsValue") {
        JsonObject obj;
        obj.set("x", 10);
        obj.set("name", "promeki");

        CHECK(obj["x"].toInt() == 10);
        CHECK(obj["name"].toString() == "promeki");
        CHECK(obj["missing"].isUndefined());
}

TEST_CASE("JsonObject_ValueOnMissingKeyReturnsUndefined") {
        JsonObject obj;
        obj.setNull("present_null");

        CHECK(obj.value("present_null").isNull());
        CHECK(!obj.value("present_null").isUndefined());
        CHECK(obj.value("absent").isUndefined());
        CHECK(!obj.value("absent").isNull());
}

TEST_CASE("JsonObject_Keys") {
        JsonObject obj;
        obj.set("alpha", 1);
        obj.set("beta", 2);
        obj.set("gamma", 3);

        auto k = obj.keys();
        CHECK(k.size() == 3);
        CHECK(k.contains("alpha"));
        CHECK(k.contains("beta"));
        CHECK(k.contains("gamma"));
}

TEST_CASE("JsonObject_IsEmptyCount") {
        JsonObject obj;
        CHECK(obj.isEmpty());
        CHECK(obj.count() == 0);
        obj.set("k", 1);
        CHECK(!obj.isEmpty());
        CHECK(obj.count() == 1);
}

TEST_CASE("JsonObject_Remove") {
        JsonObject obj;
        obj.set("a", 1);
        obj.set("b", 2);

        CHECK(obj.remove("a"));
        CHECK(!obj.contains("a"));
        CHECK(obj.size() == 1);
        CHECK(!obj.remove("missing"));
}

TEST_CASE("JsonObject_Take") {
        JsonObject obj;
        obj.set("a", 42);

        JsonValue taken = obj.take("a");
        CHECK(taken.toInt() == 42);
        CHECK(!obj.contains("a"));

        JsonValue absent = obj.take("missing");
        CHECK(absent.isUndefined());
}

TEST_CASE("JsonObject_RemoveDoesNotMutateAlias") {
        JsonObject obj;
        obj.set("a", 1);
        obj.set("b", 2);
        JsonObject alias = obj;

        obj.remove("a");
        CHECK(obj.size() == 1);
        CHECK(alias.size() == 2);
}

// ============================================================================
// JsonObject move-set
// ============================================================================

TEST_CASE("JsonObject_MoveSetEquivalentResult") {
        JsonObject child;
        child.set("inner", 99);

        JsonObject parent;
        parent.set("child", std::move(child));

        // Move-overload must produce the same observable result
        // as the lvalue overload.
        JsonObject got = parent.getObject("child");
        CHECK(got.getInt("inner") == 99);
}

TEST_CASE("JsonObject_MoveSetWithSharedRvalue") {
        // When the rvalue is shared (an lvalue passed via std::move with
        // an alias still alive), the move-overload must still produce
        // correct semantics — the alias must not see the move-out.
        JsonObject child;
        child.set("v", 7);
        JsonObject alias = child;

        JsonObject parent;
        parent.set("c", std::move(child));

        // Alias keeps its data unchanged.
        CHECK(alias.getInt("v") == 7);
        CHECK(parent.getObject("c").getInt("v") == 7);
}

// ============================================================================
// JsonObject Variant bulk
// ============================================================================

TEST_CASE("JsonObject_VariantMapRoundTrip") {
        VariantMap in;
        in.insert("name", Variant(String("clip")));
        in.insert("width", Variant(int64_t{1920}));
        in.insert("hdr", Variant(true));

        JsonObject obj = JsonObject::fromVariantMap(in);
        CHECK(obj.size() == 3);
        CHECK(obj.getString("name") == "clip");
        CHECK(obj.getInt("width") == 1920);
        CHECK(obj.getBool("hdr") == true);

        VariantMap out = obj.toVariantMap();
        CHECK(out.size() == 3);
        CHECK(out.value("name").get<String>() == "clip");
}

// ============================================================================
// JsonArray Qt-style accessors
// ============================================================================

TEST_CASE("JsonArray_AtOperatorBracket") {
        JsonArray arr;
        arr.add(10);
        arr.add("hi");

        CHECK(arr.at(0).toInt() == 10);
        CHECK(arr[1].toString() == "hi");
        CHECK(arr.at(99).isUndefined());
        CHECK(arr.at(-1).isUndefined());
}

TEST_CASE("JsonArray_FirstLast") {
        JsonArray empty;
        CHECK(empty.first().isUndefined());
        CHECK(empty.last().isUndefined());

        JsonArray arr;
        arr.add(1);
        arr.add(2);
        arr.add(3);
        CHECK(arr.first().toInt() == 1);
        CHECK(arr.last().toInt() == 3);
}

TEST_CASE("JsonArray_RemoveAtTakeAt") {
        JsonArray arr;
        arr.add(10);
        arr.add(20);
        arr.add(30);

        JsonValue taken = arr.takeAt(1);
        CHECK(taken.toInt() == 20);
        CHECK(arr.size() == 2);
        CHECK(arr.getInt(1) == 30);

        CHECK(arr.removeAt(0));
        CHECK(arr.size() == 1);
        CHECK(!arr.removeAt(5));
}

TEST_CASE("JsonArray_InsertPrepend") {
        JsonArray arr;
        arr.add(2);
        arr.add(3);
        arr.insert(0, JsonValue(1));
        CHECK(arr.size() == 3);
        CHECK(arr.getInt(0) == 1);
        CHECK(arr.getInt(2) == 3);

        arr.prepend(JsonValue(0));
        CHECK(arr.getInt(0) == 0);

        // Out-of-range clamps to size().
        arr.insert(99, JsonValue(99));
        CHECK(arr.last().toInt() == 99);
}

TEST_CASE("JsonArray_AppendAlias") {
        JsonArray arr;
        arr.append(1);
        arr.append("hello");
        CHECK(arr.size() == 2);
        CHECK(arr.getInt(0) == 1);
        CHECK(arr.getString(1) == "hello");
}

TEST_CASE("JsonArray_RangeFor") {
        JsonArray arr;
        arr.add(10);
        arr.add(20);
        arr.add(30);

        int sum = 0;
        for (const JsonValue &v : arr) {
                sum += static_cast<int>(v.toInt());
        }
        CHECK(sum == 60);
}

TEST_CASE("JsonArray_IsEmptyCount") {
        JsonArray arr;
        CHECK(arr.isEmpty());
        CHECK(arr.count() == 0);
        arr.add(1);
        CHECK(!arr.isEmpty());
        CHECK(arr.count() == 1);
}

// ============================================================================
// JsonArray move-add
// ============================================================================

TEST_CASE("JsonArray_MoveAddEquivalentResult") {
        JsonObject child;
        child.set("v", 7);

        JsonArray arr;
        arr.add(std::move(child));

        JsonObject got = arr.getObject(0);
        CHECK(got.getInt("v") == 7);
}

TEST_CASE("JsonArray_VariantListRoundTrip") {
        VariantList in;
        in.pushToBack(Variant(int64_t{1}));
        in.pushToBack(Variant(String("two")));
        in.pushToBack(Variant(true));

        JsonArray arr = JsonArray::fromVariantList(in);
        CHECK(arr.size() == 3);
        CHECK(arr.getInt(0) == 1);
        CHECK(arr.getString(1) == "two");
        CHECK(arr.getBool(2) == true);

        VariantList out = arr.toVariantList();
        CHECK(out.size() == 3);
        CHECK(out[0].get<int64_t>() == 1);
}

// ============================================================================
// JsonObject set(JsonValue)
// ============================================================================

TEST_CASE("JsonObject_SetJsonValue") {
        JsonObject obj;
        obj.set("a", JsonValue(42));
        obj.set("b", JsonValue("hi"));
        obj.set("c", JsonValue::undefined()); // should store null

        CHECK(obj.getInt("a") == 42);
        CHECK(obj.getString("b") == "hi");
        CHECK(obj.valueIsNull("c"));
}

TEST_CASE("JsonArray_AddJsonValue") {
        JsonArray arr;
        arr.add(JsonValue(1));
        arr.add(JsonValue("hi"));
        arr.add(JsonValue::undefined()); // should store null
        CHECK(arr.getInt(0) == 1);
        CHECK(arr.getString(1) == "hi");
        CHECK(arr.valueIsNull(2));
}
