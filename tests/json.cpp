/**
 * @file      json.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/json.h>

using namespace promeki;

// ============================================================================
// JsonObject basic
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_Basic)
    JsonObject obj;
    PROMEKI_TEST(!obj.isValid());
    PROMEKI_TEST(obj.size() == 0);
    PROMEKI_TEST(obj.referenceCount() == 1);

    obj.set("name", "test");
    PROMEKI_TEST(obj.isValid());
    PROMEKI_TEST(obj.size() == 1);
    PROMEKI_TEST(obj.contains("name"));
    PROMEKI_TEST(obj.getString("name") == "test");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(JsonObject_Types)
    JsonObject obj;
    obj.set("bool", true);
    obj.set("int", 42);
    obj.set("double", 3.14);
    obj.set("string", "hello");
    obj.setNull("null");

    PROMEKI_TEST(obj.getBool("bool") == true);
    PROMEKI_TEST(obj.getInt("int") == 42);
    PROMEKI_TEST(obj.getDouble("double") > 3.13);
    PROMEKI_TEST(obj.getDouble("double") < 3.15);
    PROMEKI_TEST(obj.getString("string") == "hello");
    PROMEKI_TEST(obj.valueIsNull("null"));
    PROMEKI_TEST(!obj.valueIsNull("string"));
PROMEKI_TEST_END()

// ============================================================================
// JsonObject parse
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_Parse)
    bool ok = false;
    JsonObject obj = JsonObject::parse("{\"key\": 42}", &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(obj.getInt("key") == 42);

    JsonObject bad = JsonObject::parse("not json", &ok);
    PROMEKI_TEST(!ok);
    PROMEKI_TEST(!bad.isValid());
PROMEKI_TEST_END()

// ============================================================================
// JsonObject nested
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_Nested)
    JsonObject inner;
    inner.set("value", 99);

    JsonObject outer;
    outer.set("inner", inner);
    PROMEKI_TEST(outer.valueIsObject("inner"));

    bool ok = false;
    JsonObject got = outer.getObject("inner", &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(got.getInt("value") == 99);
PROMEKI_TEST_END()

// ============================================================================
// JsonObject COW
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_CopyOnWrite)
    JsonObject obj;
    obj.set("key", "original");

    JsonObject copy = obj;
    PROMEKI_TEST(obj.referenceCount() == 2);

    copy.set("key", "modified");
    PROMEKI_TEST(obj.referenceCount() == 1);
    PROMEKI_TEST(copy.referenceCount() == 1);
    PROMEKI_TEST(obj.getString("key") == "original");
    PROMEKI_TEST(copy.getString("key") == "modified");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(JsonObject_CopyOnWriteAddKey)
    JsonObject obj;
    obj.set("a", 1);

    JsonObject copy = obj;
    PROMEKI_TEST(obj.referenceCount() == 2);

    copy.set("b", 2);
    PROMEKI_TEST(obj.referenceCount() == 1);
    PROMEKI_TEST(obj.size() == 1);
    PROMEKI_TEST(copy.size() == 2);
PROMEKI_TEST_END()

// ============================================================================
// JsonObject toString
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_ToString)
    JsonObject obj;
    obj.set("key", "value");
    String s = obj.toString();
    PROMEKI_TEST(s.size() > 0);
    PROMEKI_TEST(s.find('{') != String::npos);
PROMEKI_TEST_END()

// ============================================================================
// JsonObject clear
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_Clear)
    JsonObject obj;
    obj.set("a", 1);
    obj.set("b", 2);
    PROMEKI_TEST(obj.size() == 2);

    obj.clear();
    PROMEKI_TEST(obj.size() == 0);
PROMEKI_TEST_END()

// ============================================================================
// JsonObject missing keys
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_MissingKeys)
    JsonObject obj;
    bool ok = true;
    obj.getInt("missing", &ok);
    PROMEKI_TEST(!ok);

    ok = true;
    obj.getString("missing", &ok);
    PROMEKI_TEST(!ok);

    PROMEKI_TEST(!obj.contains("missing"));
PROMEKI_TEST_END()

// ============================================================================
// JsonObject forEach
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_ForEach)
    JsonObject obj;
    obj.set("a", 1);
    obj.set("b", 2);
    obj.set("c", 3);

    int count = 0;
    obj.forEach([&count](const String &key, const Variant &val) {
        count++;
    });
    PROMEKI_TEST(count == 3);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray basic
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_Basic)
    JsonArray arr;
    PROMEKI_TEST(!arr.isValid());
    PROMEKI_TEST(arr.size() == 0);
    PROMEKI_TEST(arr.referenceCount() == 1);

    arr.add(42);
    arr.add("hello");
    arr.add(true);
    PROMEKI_TEST(arr.isValid());
    PROMEKI_TEST(arr.size() == 3);
    PROMEKI_TEST(arr.getInt(0) == 42);
    PROMEKI_TEST(arr.getString(1) == "hello");
    PROMEKI_TEST(arr.getBool(2) == true);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(JsonArray_Parse)
    bool ok = false;
    JsonArray arr = JsonArray::parse("[1, 2, 3]", &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(arr.size() == 3);
    PROMEKI_TEST(arr.getInt(0) == 1);
    PROMEKI_TEST(arr.getInt(2) == 3);

    JsonArray bad = JsonArray::parse("not json", &ok);
    PROMEKI_TEST(!ok);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray COW
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_CopyOnWrite)
    JsonArray arr;
    arr.add(1);
    arr.add(2);

    JsonArray copy = arr;
    PROMEKI_TEST(arr.referenceCount() == 2);

    copy.add(3);
    PROMEKI_TEST(arr.referenceCount() == 1);
    PROMEKI_TEST(arr.size() == 2);
    PROMEKI_TEST(copy.size() == 3);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray nested
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_Nested)
    JsonObject obj;
    obj.set("x", 10);

    JsonArray arr;
    arr.add(obj);
    PROMEKI_TEST(arr.valueIsObject(0));

    bool ok = false;
    JsonObject got = arr.getObject(0, &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(got.getInt("x") == 10);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(JsonArray_NestedArray)
    JsonArray inner;
    inner.add(1);
    inner.add(2);

    JsonArray outer;
    outer.add(inner);
    PROMEKI_TEST(outer.valueIsArray(0));

    bool ok = false;
    JsonArray got = outer.getArray(0, &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(got.size() == 2);
    PROMEKI_TEST(got.getInt(0) == 1);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray forEach
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_ForEach)
    JsonArray arr;
    arr.add(10);
    arr.add(20);
    arr.add(30);

    int sum = 0;
    arr.forEach([&sum](const Variant &val) {
        sum += val.get<int32_t>();
    });
    PROMEKI_TEST(sum == 60);
PROMEKI_TEST_END()

// ============================================================================
// JsonObject with array
// ============================================================================

PROMEKI_TEST_BEGIN(JsonObject_WithArray)
    JsonArray arr;
    arr.add(1);
    arr.add(2);

    JsonObject obj;
    obj.set("list", arr);
    PROMEKI_TEST(obj.valueIsArray("list"));

    bool ok = false;
    JsonArray got = obj.getArray("list", &ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(got.size() == 2);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray bounds
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_OutOfBounds)
    JsonArray arr;
    arr.add(1);

    bool ok = true;
    arr.getInt(5, &ok);
    PROMEKI_TEST(!ok);

    ok = true;
    arr.getInt(-1, &ok);
    PROMEKI_TEST(!ok);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray clear
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_Clear)
    JsonArray arr;
    arr.add(1);
    arr.add(2);
    PROMEKI_TEST(arr.size() == 2);

    arr.clear();
    PROMEKI_TEST(arr.size() == 0);
PROMEKI_TEST_END()

// ============================================================================
// JsonArray null
// ============================================================================

PROMEKI_TEST_BEGIN(JsonArray_Null)
    JsonArray arr;
    arr.addNull();
    arr.add(42);
    PROMEKI_TEST(arr.size() == 2);
    PROMEKI_TEST(arr.valueIsNull(0));
    PROMEKI_TEST(!arr.valueIsNull(1));
PROMEKI_TEST_END()
