/**
 * @file      variant.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/variant.h>

using namespace promeki;

// ============================================================================
// Default / invalid
// ============================================================================

TEST_CASE("Variant_Default") {
    Variant v;
    CHECK(!v.isValid());
    CHECK(v.type() == Variant::TypeInvalid);
}

// ============================================================================
// Basic types
// ============================================================================

TEST_CASE("Variant_Bool") {
    Variant v(true);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeBool);
    CHECK(v.get<bool>() == true);

    Variant v2(false);
    CHECK(v2.get<bool>() == false);
}

TEST_CASE("Variant_Int") {
    Variant v(int32_t(42));
    CHECK(v.type() == Variant::TypeS32);
    CHECK(v.get<int32_t>() == 42);

    Variant v2(int64_t(-99));
    CHECK(v2.type() == Variant::TypeS64);
    CHECK(v2.get<int64_t>() == -99);
}

TEST_CASE("Variant_UInt") {
    Variant v(uint32_t(100));
    CHECK(v.type() == Variant::TypeU32);
    CHECK(v.get<uint32_t>() == 100);

    Variant v2(uint64_t(12345));
    CHECK(v2.type() == Variant::TypeU64);
    CHECK(v2.get<uint64_t>() == 12345);
}

TEST_CASE("Variant_Float") {
    Variant v(3.14f);
    CHECK(v.type() == Variant::TypeFloat);
    CHECK(v.get<float>() > 3.13f);
    CHECK(v.get<float>() < 3.15f);
}

TEST_CASE("Variant_Double") {
    Variant v(2.718281828);
    CHECK(v.type() == Variant::TypeDouble);
    CHECK(v.get<double>() > 2.71);
    CHECK(v.get<double>() < 2.72);
}

TEST_CASE("Variant_String") {
    Variant v(String("hello"));
    CHECK(v.type() == Variant::TypeString);
    CHECK(v.get<String>() == "hello");
}

// ============================================================================
// Type conversions
// ============================================================================

TEST_CASE("Variant_IntToString") {
    Variant v(int32_t(42));
    String s = v.get<String>();
    CHECK(s == "42");
}

TEST_CASE("Variant_StringToInt") {
    Variant v(String("123"));
    Error err;
    int32_t val = v.get<int32_t>(&err);
    CHECK(err.isOk());
    CHECK(val == 123);
}

TEST_CASE("Variant_BoolToInt") {
    Variant v(true);
    CHECK(v.get<int32_t>() == 1);

    Variant v2(false);
    CHECK(v2.get<int32_t>() == 0);
}

TEST_CASE("Variant_IntToBool") {
    Variant v(int32_t(42));
    CHECK(v.get<bool>() == true);

    Variant v2(int32_t(0));
    CHECK(v2.get<bool>() == false);
}

TEST_CASE("Variant_IntToDouble") {
    Variant v(int32_t(42));
    double d = v.get<double>();
    CHECK(d > 41.9);
    CHECK(d < 42.1);
}

TEST_CASE("Variant_DoubleToString") {
    Variant v(3.14);
    String s = v.get<String>();
    CHECK(s.startsWith("3.14"));
}

TEST_CASE("Variant_BoolToString") {
    Variant v(true);
    CHECK(v.get<String>() == "true");

    Variant v2(false);
    CHECK(v2.get<String>() == "false");
}

// ============================================================================
// Set
// ============================================================================

TEST_CASE("Variant_Set") {
    Variant v;
    CHECK(!v.isValid());

    v.set(int32_t(42));
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeS32);
    CHECK(v.get<int32_t>() == 42);

    v.set(String("changed"));
    CHECK(v.type() == Variant::TypeString);
    CHECK(v.get<String>() == "changed");
}

// ============================================================================
// typeName
// ============================================================================

TEST_CASE("Variant_TypeName") {
    Variant v(int32_t(1));
    CHECK(String(v.typeName()) == "int32_t");

    Variant vs(String("hi"));
    CHECK(String(vs.typeName()) == "String");

    Variant vd(3.14);
    CHECK(String(vd.typeName()) == "double");
}

// ============================================================================
// Copy
// ============================================================================

TEST_CASE("Variant_Copy") {
    Variant v1(String("hello"));
    Variant v2 = v1;
    CHECK(v2.get<String>() == "hello");
    CHECK(v1.type() == v2.type());
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("Variant_EqualitySameType") {
    CHECK(Variant(int32_t(42)) == Variant(int32_t(42)));
    CHECK(Variant(String("hello")) == Variant(String("hello")));
    CHECK(Variant(true) == Variant(true));
    CHECK(Variant(3.14) == Variant(3.14));
}

TEST_CASE("Variant_InequalitySameType") {
    CHECK(Variant(int32_t(1)) != Variant(int32_t(2)));
    CHECK(Variant(String("a")) != Variant(String("b")));
    CHECK(Variant(true) != Variant(false));
}

TEST_CASE("Variant_EqualityCrossNumeric") {
    // Same value, different integer widths/signs
    CHECK(Variant(int32_t(42)) == Variant(uint32_t(42)));
    CHECK(Variant(int32_t(42)) == Variant(int64_t(42)));
    CHECK(Variant(uint8_t(7)) == Variant(int64_t(7)));
    CHECK(Variant(int32_t(1)) == Variant(true));
    CHECK(Variant(int32_t(0)) == Variant(false));

    // Integer vs floating-point
    CHECK(Variant(int32_t(3)) == Variant(3.0));
    CHECK(Variant(uint64_t(100)) == Variant(100.0f));

    // Negative signed vs unsigned is never equal
    CHECK(Variant(int32_t(-1)) != Variant(uint32_t(0)));

    // Numeric vs non-numeric: conversion is attempted
    CHECK(Variant(int32_t(42)) == Variant(String("42")));
    CHECK(Variant(int32_t(42)) != Variant(String("99")));
}

TEST_CASE("Variant_EqualityCrossConvertible") {
    // Timecode <-> String
    Timecode tc = Timecode::fromFrameNumber(Timecode::NDF24, 86400);
    auto [tcStr, strErr] = tc.toString();
    CHECK(Variant(tc) == Variant(tcStr));

    // UUID <-> String
    UUID u = UUID::generate();
    String uStr = u.toString();
    CHECK(Variant(u) == Variant(uStr));

    // Mismatched values still compare unequal
    CHECK(Variant(tc) != Variant(String("not a timecode")));

    // Completely unrelated types are unequal
    CHECK(Variant(tc) != Variant(u));
}

TEST_CASE("Variant_EqualityInvalid") {
    Variant a;
    Variant b;
    CHECK(a == b);
    CHECK(a != Variant(int32_t(0)));
}
