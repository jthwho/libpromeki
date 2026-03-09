/**
 * @file      variant.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// Default / invalid
// ============================================================================

PROMEKI_TEST_BEGIN(Variant_Default)
    Variant v;
    PROMEKI_TEST(!v.isValid());
    PROMEKI_TEST(v.type() == Variant::TypeInvalid);
PROMEKI_TEST_END()

// ============================================================================
// Basic types
// ============================================================================

PROMEKI_TEST_BEGIN(Variant_Bool)
    Variant v(true);
    PROMEKI_TEST(v.isValid());
    PROMEKI_TEST(v.type() == Variant::TypeBool);
    PROMEKI_TEST(v.get<bool>() == true);

    Variant v2(false);
    PROMEKI_TEST(v2.get<bool>() == false);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_Int)
    Variant v(int32_t(42));
    PROMEKI_TEST(v.type() == Variant::TypeS32);
    PROMEKI_TEST(v.get<int32_t>() == 42);

    Variant v2(int64_t(-99));
    PROMEKI_TEST(v2.type() == Variant::TypeS64);
    PROMEKI_TEST(v2.get<int64_t>() == -99);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_UInt)
    Variant v(uint32_t(100));
    PROMEKI_TEST(v.type() == Variant::TypeU32);
    PROMEKI_TEST(v.get<uint32_t>() == 100);

    Variant v2(uint64_t(12345));
    PROMEKI_TEST(v2.type() == Variant::TypeU64);
    PROMEKI_TEST(v2.get<uint64_t>() == 12345);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_Float)
    Variant v(3.14f);
    PROMEKI_TEST(v.type() == Variant::TypeFloat);
    PROMEKI_TEST(v.get<float>() > 3.13f);
    PROMEKI_TEST(v.get<float>() < 3.15f);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_Double)
    Variant v(2.718281828);
    PROMEKI_TEST(v.type() == Variant::TypeDouble);
    PROMEKI_TEST(v.get<double>() > 2.71);
    PROMEKI_TEST(v.get<double>() < 2.72);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_String)
    Variant v(String("hello"));
    PROMEKI_TEST(v.type() == Variant::TypeString);
    PROMEKI_TEST(v.get<String>() == "hello");
PROMEKI_TEST_END()

// ============================================================================
// Type conversions
// ============================================================================

PROMEKI_TEST_BEGIN(Variant_IntToString)
    Variant v(int32_t(42));
    String s = v.get<String>();
    PROMEKI_TEST(s == "42");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_StringToInt)
    Variant v(String("123"));
    bool ok = false;
    int32_t val = v.get<int32_t>(&ok);
    PROMEKI_TEST(ok);
    PROMEKI_TEST(val == 123);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_BoolToInt)
    Variant v(true);
    PROMEKI_TEST(v.get<int32_t>() == 1);

    Variant v2(false);
    PROMEKI_TEST(v2.get<int32_t>() == 0);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_IntToBool)
    Variant v(int32_t(42));
    PROMEKI_TEST(v.get<bool>() == true);

    Variant v2(int32_t(0));
    PROMEKI_TEST(v2.get<bool>() == false);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_IntToDouble)
    Variant v(int32_t(42));
    double d = v.get<double>();
    PROMEKI_TEST(d > 41.9);
    PROMEKI_TEST(d < 42.1);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_DoubleToString)
    Variant v(3.14);
    String s = v.get<String>();
    PROMEKI_TEST(s.startsWith("3.14"));
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Variant_BoolToString)
    Variant v(true);
    PROMEKI_TEST(v.get<String>() == "true");

    Variant v2(false);
    PROMEKI_TEST(v2.get<String>() == "false");
PROMEKI_TEST_END()

// ============================================================================
// Set
// ============================================================================

PROMEKI_TEST_BEGIN(Variant_Set)
    Variant v;
    PROMEKI_TEST(!v.isValid());

    v.set(int32_t(42));
    PROMEKI_TEST(v.isValid());
    PROMEKI_TEST(v.type() == Variant::TypeS32);
    PROMEKI_TEST(v.get<int32_t>() == 42);

    v.set(String("changed"));
    PROMEKI_TEST(v.type() == Variant::TypeString);
    PROMEKI_TEST(v.get<String>() == "changed");
PROMEKI_TEST_END()

// ============================================================================
// typeName
// ============================================================================

PROMEKI_TEST_BEGIN(Variant_TypeName)
    Variant v(int32_t(1));
    PROMEKI_TEST(String(v.typeName()) == "int32_t");

    Variant vs(String("hi"));
    PROMEKI_TEST(String(vs.typeName()) == "String");

    Variant vd(3.14);
    PROMEKI_TEST(String(vd.typeName()) == "double");
PROMEKI_TEST_END()

// ============================================================================
// Copy
// ============================================================================

PROMEKI_TEST_BEGIN(Variant_Copy)
    Variant v1(String("hello"));
    Variant v2 = v1;
    PROMEKI_TEST(v2.get<String>() == "hello");
    PROMEKI_TEST(v1.type() == v2.type());
PROMEKI_TEST_END()
