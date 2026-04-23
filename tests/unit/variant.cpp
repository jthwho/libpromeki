/**
 * @file      variant.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/variant.h>
#include <promeki/sdpsession.h>
#include <promeki/url.h>
#include <promeki/variantspec.h>

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

TEST_CASE("Variant_StringToInt_HexBinOctSeparators") {
    // Hex
    Error e1;
    CHECK(Variant(String("0xDEADBEEF")).get<uint32_t>(&e1) == 0xDEADBEEF);
    CHECK(e1.isOk());

    // Hex with separators
    Error e2;
    CHECK(Variant(String("0xDEAD_BEEF")).get<uint32_t>(&e2) == 0xDEADBEEF);
    CHECK(e2.isOk());

    // Binary
    Error e3;
    CHECK(Variant(String("0b1010")).get<int32_t>(&e3) == 10);
    CHECK(e3.isOk());

    // Octal
    Error e4;
    CHECK(Variant(String("0o777")).get<int32_t>(&e4) == 511);
    CHECK(e4.isOk());

    // Comma/apostrophe separators
    Error e5;
    CHECK(Variant(String("1,000,000")).get<int32_t>(&e5) == 1000000);
    CHECK(e5.isOk());

    Error e6;
    CHECK(Variant(String("1'000'000")).get<int32_t>(&e6) == 1000000);
    CHECK(e6.isOk());
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

// ============================================================================
// StringList
// ============================================================================

TEST_CASE("Variant_StringList") {
    StringList sl;
    sl.pushToBack("alpha");
    sl.pushToBack("beta");
    sl.pushToBack("gamma");

    Variant v(sl);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeStringList);

    StringList out = v.get<StringList>();
    CHECK(out.size() == 3);
    CHECK(out[0] == "alpha");
    CHECK(out[1] == "beta");
    CHECK(out[2] == "gamma");
}

TEST_CASE("Variant_StringListToString") {
    StringList sl;
    sl.pushToBack("a");
    sl.pushToBack("b");
    sl.pushToBack("c");

    Variant v(sl);
    String s = v.get<String>();
    CHECK(s == "a,b,c");
}

TEST_CASE("Variant_StringToStringList") {
    Variant v(String("x,y,z"));
    StringList sl = v.get<StringList>();
    CHECK(sl.size() == 3);
    CHECK(sl[0] == "x");
    CHECK(sl[1] == "y");
    CHECK(sl[2] == "z");
}

TEST_CASE("Variant_StringListEquality") {
    StringList sl1;
    sl1.pushToBack("a");
    sl1.pushToBack("b");

    StringList sl2;
    sl2.pushToBack("a");
    sl2.pushToBack("b");

    StringList sl3;
    sl3.pushToBack("a");
    sl3.pushToBack("c");

    CHECK(Variant(sl1) == Variant(sl2));
    CHECK(Variant(sl1) != Variant(sl3));
}

TEST_CASE("Variant_StringListFromJson") {
    nlohmann::json j = nlohmann::json::array({"foo", "bar", "baz"});
    Variant v = Variant::fromJson(j);
    CHECK(v.type() == Variant::TypeStringList);

    StringList sl = v.get<StringList>();
    CHECK(sl.size() == 3);
    CHECK(sl[0] == "foo");
    CHECK(sl[1] == "bar");
    CHECK(sl[2] == "baz");
}

TEST_CASE("Variant_StringListToStandardType") {
    StringList sl;
    sl.pushToBack("one");
    sl.pushToBack("two");

    Variant v(sl);
    Variant standard = v.toStandardType();
    CHECK(standard.type() == Variant::TypeString);
    CHECK(standard.get<String>() == "one,two");
}

TEST_CASE("Variant_EmptyStringList") {
    StringList sl;
    Variant v(sl);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeStringList);
    CHECK(v.get<StringList>().isEmpty());
    CHECK(v.get<String>() == "");
}

// ============================================================================
// Color
// ============================================================================

TEST_CASE("Variant_Color") {
    Variant v(Color::Red);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeColor);
    CHECK(v.get<Color>() == Color::Red);
}

TEST_CASE("Variant_ColorToString") {
    Variant v(Color(255, 128, 64));
    CHECK(v.get<String>() == "sRGB(1,0.501961,0.25098,1)");
}

TEST_CASE("Variant_StringToColor") {
    Variant v(String("#ff8040"));
    Color c = v.get<Color>();
    CHECK(c.isValid());
    CHECK(c.r8() == 255);
    CHECK(c.g8() == 128);
    CHECK(c.b8() == 64);
}

TEST_CASE("Variant_StringNameToColor") {
    Variant v(String("red"));
    Color c = v.get<Color>();
    CHECK(c == Color::Red);
}

TEST_CASE("Variant_ColorEquality") {
    CHECK(Variant(Color::White) == Variant(Color::White));
    CHECK(Variant(Color::White) != Variant(Color::Black));
}

TEST_CASE("Variant_ColorToStandardType") {
    Variant v(Color(255, 128, 64));
    Variant standard = v.toStandardType();
    CHECK(standard.type() == Variant::TypeString);
    CHECK(standard.get<String>() == "sRGB(1,0.501961,0.25098,1)");
}

TEST_CASE("Variant_ColorCrossTypeEquality") {
    // Color vs String comparison via conversion
    CHECK(Variant(Color::Red) == Variant(String("rgb(1,0,0)")));
    CHECK(Variant(Color::White) == Variant(String("rgb(1,1,1)")));
    CHECK(Variant(Color::Red) != Variant(String("rgb(0,1,0)")));
}

TEST_CASE("Variant_ColorCopy") {
    Variant v1(Color(10, 20, 30, 40));
    Variant v2 = v1;
    CHECK(v2.type() == Variant::TypeColor);
    CHECK(v2.get<Color>().r8() == 10);
    CHECK(v2.get<Color>().g8() == 20);
    CHECK(v2.get<Color>().b8() == 30);
    CHECK(v2.get<Color>().a8() == 40);
}

TEST_CASE("Variant_ColorSet") {
    Variant v;
    v.set(Color::Blue);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeColor);
    CHECK(v.get<Color>() == Color::Blue);
}

TEST_CASE("Variant_ColorTypeName") {
    Variant v(Color::Red);
    CHECK(String(v.typeName()) == "Color");
}

TEST_CASE("Variant_InvalidStringToColor") {
    Variant v(String("notacolor"));
    Error err;
    Color c = v.get<Color>(&err);
    // fromString returns an invalid Color for unknown strings
    CHECK_FALSE(c.isValid());
}

TEST_CASE("Variant_ColorNamedStringConversion") {
    // Named color strings should convert via fromString
    Variant v(String("white"));
    Color c = v.get<Color>();
    CHECK(c == Color::White);
}

TEST_CASE("Variant_ColorCommaStringConversion") {
    Variant v(String("128,64,32"));
    Color c = v.get<Color>();
    CHECK(c.isValid());
    CHECK(c.r8() == 128);
    CHECK(c.g8() == 64);
    CHECK(c.b8() == 32);
}

// ============================================================================
// FrameRate
// ============================================================================

TEST_CASE("Variant_FrameRate_Store") {
    Variant v(FrameRate(FrameRate::FPS_29_97));
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeFrameRate);
    CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_29_97));
}

TEST_CASE("Variant_FrameRate_AllWellKnownRates") {
    SUBCASE("FPS_24") {
        Variant v(FrameRate(FrameRate::FPS_24));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_24));
    }
    SUBCASE("FPS_25") {
        Variant v(FrameRate(FrameRate::FPS_25));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_25));
    }
    SUBCASE("FPS_30") {
        Variant v(FrameRate(FrameRate::FPS_30));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_30));
    }
    SUBCASE("FPS_50") {
        Variant v(FrameRate(FrameRate::FPS_50));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_50));
    }
    SUBCASE("FPS_59_94") {
        Variant v(FrameRate(FrameRate::FPS_59_94));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_59_94));
    }
    SUBCASE("FPS_60") {
        Variant v(FrameRate(FrameRate::FPS_60));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_60));
    }
    SUBCASE("FPS_23_98") {
        Variant v(FrameRate(FrameRate::FPS_23_98));
        CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_23_98));
    }
}

TEST_CASE("Variant_FrameRate_ToDouble") {
    Variant v(FrameRate(FrameRate::FPS_24));
    double d = v.get<double>();
    CHECK(d == doctest::Approx(24.0));
}

TEST_CASE("Variant_FrameRate_ToFloat") {
    Variant v(FrameRate(FrameRate::FPS_25));
    float f = v.get<float>();
    CHECK(f == doctest::Approx(25.0f));
}

TEST_CASE("Variant_FrameRate_ToString") {
    Variant v(FrameRate(FrameRate::FPS_29_97));
    String s = v.get<String>();
    CHECK_FALSE(s.isEmpty());
}

TEST_CASE("Variant_FrameRate_FromString") {
    Variant v(String("29.97"));
    FrameRate fr = v.get<FrameRate>();
    CHECK(fr == FrameRate(FrameRate::FPS_29_97));
}

TEST_CASE("Variant_FrameRate_FromString_Fraction") {
    Variant v(String("30000/1001"));
    FrameRate fr = v.get<FrameRate>();
    CHECK(fr == FrameRate(FrameRate::FPS_29_97));
}

TEST_CASE("Variant_FrameRate_FromInvalidString") {
    Variant v(String("not_a_framerate"));
    Error err;
    FrameRate fr = v.get<FrameRate>(&err);
    CHECK(err.isError());
    CHECK_FALSE(fr.isValid());
}

TEST_CASE("Variant_FrameRate_FromRational") {
    Variant v(Rational<int>(30000, 1001));
    FrameRate fr = v.get<FrameRate>();
    CHECK(fr == FrameRate(FrameRate::FPS_29_97));
}

TEST_CASE("Variant_FrameRate_ToStandardType") {
    Variant v(FrameRate(FrameRate::FPS_24));
    Variant standard = v.toStandardType();
    CHECK(standard.type() == Variant::TypeString);
    CHECK_FALSE(standard.get<String>().isEmpty());
}

TEST_CASE("Variant_FrameRate_TypeName") {
    Variant v(FrameRate(FrameRate::FPS_24));
    CHECK(String(v.typeName()) == "FrameRate");
}

TEST_CASE("Variant_FrameRate_Copy") {
    Variant v1(FrameRate(FrameRate::FPS_60));
    Variant v2 = v1;
    CHECK(v2.type() == Variant::TypeFrameRate);
    CHECK(v2.get<FrameRate>() == FrameRate(FrameRate::FPS_60));
}

TEST_CASE("Variant_FrameRate_Set") {
    Variant v;
    v.set(FrameRate(FrameRate::FPS_50));
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeFrameRate);
    CHECK(v.get<FrameRate>() == FrameRate(FrameRate::FPS_50));
}

// ============================================================================
// PixelMemLayout
// ============================================================================

TEST_CASE("Variant_PixelMemLayout_RoundTrip") {
    PixelMemLayout pf(PixelMemLayout::I_4x8);
    Variant v(pf);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypePixelMemLayout);
    CHECK(v.get<PixelMemLayout>() == pf);
}

TEST_CASE("Variant_PixelMemLayout_ToString") {
    Variant v(PixelMemLayout(PixelMemLayout::I_3x8));
    String s = v.get<String>();
    CHECK(s == "3x8");
}

TEST_CASE("Variant_PixelMemLayout_FromString") {
    Variant v(String("4x8"));
    PixelMemLayout pf = v.get<PixelMemLayout>();
    CHECK(pf.id() == PixelMemLayout::I_4x8);
}

TEST_CASE("Variant_PixelMemLayout_FromInt") {
    Variant v(int32_t(PixelMemLayout::I_3x8));
    PixelMemLayout pf = v.get<PixelMemLayout>();
    CHECK(pf.id() == PixelMemLayout::I_3x8);
}

TEST_CASE("Variant_PixelMemLayout_ToInt") {
    Variant v(PixelMemLayout(PixelMemLayout::I_4x8));
    int32_t id = v.get<int32_t>();
    CHECK(id == PixelMemLayout::I_4x8);
}

// ============================================================================
// PixelFormat
// ============================================================================

TEST_CASE("Variant_PixelFormat_RoundTrip") {
    PixelFormat pd(PixelFormat::RGBA8_sRGB);
    Variant v(pd);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypePixelFormat);
    CHECK(v.get<PixelFormat>() == pd);
}

TEST_CASE("Variant_PixelFormat_ToString") {
    Variant v(PixelFormat(PixelFormat::RGB8_sRGB));
    String s = v.get<String>();
    CHECK(s == "RGB8_sRGB");
}

TEST_CASE("Variant_PixelFormat_FromString") {
    Variant v(String("RGBA8_sRGB"));
    PixelFormat pd = v.get<PixelFormat>();
    CHECK(pd.id() == PixelFormat::RGBA8_sRGB);
}

TEST_CASE("Variant_PixelFormat_FromInt") {
    Variant v(int32_t(PixelFormat::YUV8_422_Rec709));
    PixelFormat pd = v.get<PixelFormat>();
    CHECK(pd.id() == PixelFormat::YUV8_422_Rec709);
}

TEST_CASE("Variant_PixelFormat_ToInt") {
    Variant v(PixelFormat(PixelFormat::RGBA8_sRGB));
    int32_t id = v.get<int32_t>();
    CHECK(id == PixelFormat::RGBA8_sRGB);
}

// ============================================================================
// ColorModel
// ============================================================================

TEST_CASE("Variant_ColorModel_RoundTrip") {
    ColorModel cm(ColorModel::sRGB);
    Variant v(cm);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeColorModel);
    CHECK(v.get<ColorModel>() == cm);
}

TEST_CASE("Variant_ColorModel_ToString") {
    Variant v(ColorModel(ColorModel::Rec709));
    CHECK(v.get<String>() == "Rec709");
}

TEST_CASE("Variant_ColorModel_FromString") {
    Variant v(String("sRGB"));
    CHECK(v.get<ColorModel>().id() == ColorModel::sRGB);
}

TEST_CASE("Variant_ColorModel_FromInt") {
    Variant v(int32_t(ColorModel::sRGB));
    CHECK(v.get<ColorModel>().id() == ColorModel::sRGB);
}

TEST_CASE("Variant_ColorModel_ToInt") {
    Variant v(ColorModel(ColorModel::sRGB));
    CHECK(v.get<int32_t>() == ColorModel::sRGB);
}

// ============================================================================
// MemSpace
// ============================================================================

TEST_CASE("Variant_MemSpace_RoundTrip") {
    MemSpace ms(MemSpace::System);
    Variant v(ms);
    CHECK(v.isValid());
    CHECK(v.type() == Variant::TypeMemSpace);
    CHECK(v.get<MemSpace>() == ms);
}

TEST_CASE("Variant_MemSpace_FromInt") {
    Variant v(int32_t(MemSpace::System));
    CHECK(v.get<MemSpace>().id() == MemSpace::System);
}

TEST_CASE("Variant_MemSpace_ToInt") {
    Variant v(MemSpace(MemSpace::SystemSecure));
    CHECK(v.get<int32_t>() == MemSpace::SystemSecure);
}

// ============================================================================
// TypeRegistry integer conversion across int types
// ============================================================================

TEST_CASE("Variant_PixelFormat_FromUint32") {
    Variant v(uint32_t(PixelFormat::RGBA8_sRGB));
    CHECK(v.get<PixelFormat>().id() == PixelFormat::RGBA8_sRGB);
}

TEST_CASE("Variant_PixelMemLayout_ToUint32") {
    Variant v(PixelMemLayout(PixelMemLayout::I_4x8));
    CHECK(v.get<uint32_t>() == PixelMemLayout::I_4x8);
}

TEST_CASE("Variant_ColorModel_FromInt64") {
    Variant v(int64_t(ColorModel::Rec2020));
    CHECK(v.get<ColorModel>().id() == ColorModel::Rec2020);
}

#if PROMEKI_ENABLE_NETWORK

TEST_CASE("Variant_SocketAddress_TypedRoundtrip") {
    SocketAddress addr(Ipv4Address(239, 1, 2, 3), 5004);
    Variant v(addr);
    CHECK(v.type() == Variant::TypeSocketAddress);
    SocketAddress back = v.get<SocketAddress>();
    CHECK(back == addr);
}

TEST_CASE("Variant_SocketAddress_ToString") {
    SocketAddress addr(Ipv4Address(239, 1, 2, 3), 5004);
    Variant v(addr);
    CHECK(v.get<String>() == "239.1.2.3:5004");
}

TEST_CASE("Variant_SocketAddress_FromString") {
    Variant v(String("192.168.10.20:5006"));
    SocketAddress addr = v.get<SocketAddress>();
    CHECK(addr.address().toIpv4() == Ipv4Address(192, 168, 10, 20));
    CHECK(addr.port() == 5006);
}

TEST_CASE("Variant_SocketAddress_FromStringInvalid") {
    Variant v(String("not-a-socket-address"));
    Error err;
    SocketAddress addr = v.get<SocketAddress>(&err);
    CHECK(err.isError());
    CHECK(addr.isNull());
}

TEST_CASE("Variant_SocketAddress_TypeName") {
    Variant v(SocketAddress(Ipv4Address::loopback(), 80));
    // The type name is whatever the X-macro stringified.
    String name = String(v.typeName());
    CHECK(name == "SocketAddress");
}

// ============================================================================
// SdpSession Variant type
// ============================================================================

TEST_CASE("Variant_SdpSession_TypedRoundtrip") {
        SdpSession sdp;
        sdp.setSessionName("test-session");
        sdp.setOrigin("user", 42, 1, "IN", "IP4", "10.0.0.1");
        Variant v(sdp);
        CHECK(v.type() == Variant::TypeSdpSession);
        CHECK(v.isValid());
        SdpSession back = v.get<SdpSession>();
        CHECK(back == sdp);
        CHECK(back.sessionName() == "test-session");
        CHECK(back.originUsername() == "user");
        CHECK(back.sessionId() == 42);
}

TEST_CASE("Variant_SdpSession_ToString") {
        SdpSession sdp;
        sdp.setSessionName("variant-to-string-test");
        Variant v(sdp);
        String s = v.get<String>();
        CHECK(s.contains("v=0"));
        CHECK(s.contains("variant-to-string-test"));
}

TEST_CASE("Variant_SdpSession_FromString") {
        // Build a valid SDP text and store it as a String variant,
        // then convert to SdpSession via get<SdpSession>().
        String sdpText =
                "v=0\r\n"
                "o=test 100 1 IN IP4 127.0.0.1\r\n"
                "s=from-string-test\r\n"
                "c=IN IP4 239.0.0.1\r\n"
                "t=0 0\r\n"
                "m=video 5004 RTP/AVP 96\r\n"
                "a=rtpmap:96 raw/90000\r\n";
        Variant v(sdpText);
        Error err;
        SdpSession sdp = v.get<SdpSession>(&err);
        CHECK(err.isOk());
        CHECK(sdp.sessionName() == "from-string-test");
        CHECK(sdp.connectionAddress() == "239.0.0.1");
        REQUIRE(sdp.mediaDescriptions().size() == 1);
        CHECK(sdp.mediaDescriptions()[0].mediaType() == "video");
}

TEST_CASE("Variant_SdpSession_FromStringDegenerateInput") {
        // SdpSession::fromString is lenient — it skips lines it
        // cannot parse and returns Error::Ok with whatever fields
        // it could extract.  So "garbage" input yields an empty
        // session rather than an error.  Verify we get a valid
        // Variant round-trip even for this degenerate case.
        Variant v(String("this is not valid SDP"));
        Error err;
        SdpSession sdp = v.get<SdpSession>(&err);
        // The parser returns Ok with an empty session.
        CHECK(err.isOk());
        CHECK(sdp.sessionName().isEmpty());
}

TEST_CASE("Variant_SdpSession_TypeName") {
        Variant v(SdpSession{});
        String name = String(v.typeName());
        CHECK(name == "SdpSession");
}

TEST_CASE("Variant_SdpSession_ToStringViaGetString") {
        // Test that Variant::get<String>() works for SdpSession type
        // (this exercises the toString conversion branch in the
        // variant visitor).
        SdpSession sdp;
        sdp.setSessionName("to-string-overall");
        Variant v(sdp);
        String s = v.get<String>();
        CHECK(s.contains("v=0"));
        CHECK(s.contains("to-string-overall"));
}

#endif // PROMEKI_ENABLE_NETWORK

// ============================================================================
// VideoCodec / AudioCodec — TypeRegistry types added in task 38.
// ============================================================================

TEST_CASE("Variant_VideoCodec_RoundTrip") {
        Variant v(VideoCodec(VideoCodec::H264));
        CHECK(v.type() == Variant::TypeVideoCodec);
        CHECK(v.get<VideoCodec>() == VideoCodec(VideoCodec::H264));
        // Round-trip via String — name() in, lookup() out.
        CHECK(v.get<String>() == "H264");
        Variant w(String("HEVC"));
        CHECK(w.get<VideoCodec>() == VideoCodec(VideoCodec::HEVC));
}

TEST_CASE("Variant_AudioCodec_RoundTrip") {
        Variant v(AudioCodec(AudioCodec::Opus));
        CHECK(v.type() == Variant::TypeAudioCodec);
        CHECK(v.get<AudioCodec>() == AudioCodec(AudioCodec::Opus));
        CHECK(v.get<String>() == "Opus");
        Variant w(String("AAC"));
        CHECK(w.get<AudioCodec>() == AudioCodec(AudioCodec::AAC));
}

// ============================================================================
// format(spec)
// ============================================================================

TEST_CASE("Variant_Format_EmptySpec_UsesDefault") {
        Variant v(VideoFormat(VideoFormat::Smpte1080p29_97));
        CHECK(v.format(String()) == String("1080p29.97"));
        CHECK(v.format(String("")) == String("1080p29.97"));
}

TEST_CASE("Variant_Format_VideoFormat_Styles") {
        Variant v(VideoFormat(VideoFormat::Smpte1080p29_97));
        CHECK(v.format("smpte") == String("1080p29.97"));
        CHECK(v.format("named") == String("HDp29.97"));
}

TEST_CASE("Variant_Format_Timecode_Styles") {
        Variant v(Timecode(Timecode::NDF24, 1, 0, 0, 0));
        CHECK(v.format("smpte") == String("01:00:00:00"));
        // smpte-fps appends the rate; just check the prefix matches.
        String withFps = v.format("smpte-fps");
        CHECK(withFps.find(String("01:00:00:00")) == 0);
}

TEST_CASE("Variant_Format_Primitive_StdSpec") {
        Variant v(int32_t(255));
        CHECK(v.format("x") == String("ff"));
        CHECK(v.format("05d") == String("00255"));
        Variant d(3.14159);
        CHECK(d.format(".2f") == String("3.14"));
}

TEST_CASE("Variant_Format_String_Width") {
        Variant v(String("hi"));
        CHECK(v.format(">5") == String("   hi"));
        CHECK(v.format("*<5") == String("hi***"));
}

TEST_CASE("Variant_Format_NoFormatter_FallsBack") {
        // UUID has no std::formatter — spec is applied to its String form.
        Variant v(UUID(String("12345678-1234-1234-1234-1234567890ab")));
        String full = v.format(String());
        // Width spec on a non-formattable type reuses the String form.
        String widened = v.format(">40");
        CHECK(widened.byteCount() == 40);
        CHECK(widened.find(full) != String::npos);
}

TEST_CASE("Variant_Format_InvalidSpec_ReturnsDefault") {
        Variant v(int32_t(7));
        Error err;
        String s = v.format("not_a_real_spec", &err);
        CHECK(err.isError());
        CHECK(s == String("7"));
}

TEST_CASE("Variant_Format_Invalid_Variant_Empty") {
        Variant v;
        CHECK(v.format("smpte") == String());
}

// ============================================================================
// Url
// ============================================================================

TEST_CASE("Variant_Url_RoundTrip") {
        Url u = Url::fromString("pmfb://studio-a?FrameBridgeRingDepth=4");
        Variant v(u);
        CHECK(v.isValid());
        CHECK(v.type() == Variant::TypeUrl);
        Url out = v.get<Url>();
        CHECK(out == u);
}

TEST_CASE("Variant_Url_ToString") {
        Url u = Url::fromString("pmfb://studio-a");
        Variant v(u);
        CHECK(v.get<String>() == "pmfb://studio-a");
}

TEST_CASE("VariantSpec_Url_ParseString") {
        VariantSpec spec = VariantSpec().setType(Variant::TypeUrl);
        Error err;
        Variant parsed = spec.parseString("pmfb://bridge", &err);
        CHECK_FALSE(err.isError());
        CHECK(parsed.type() == Variant::TypeUrl);
        CHECK(parsed.get<Url>().scheme() == "pmfb");
        CHECK(parsed.get<Url>().host() == "bridge");
}

TEST_CASE("VariantSpec_Url_ParseString_Rejects_Malformed") {
        VariantSpec spec = VariantSpec().setType(Variant::TypeUrl);
        Error err = Error::Ok;
        Variant parsed = spec.parseString("no-scheme-here", &err);
        CHECK(err.isError());
        CHECK_FALSE(parsed.isValid());
}

TEST_CASE("Variant_Url_FromStringVariant") {
        // Exercises the From==String branch in variant.tpp: converting a
        // TypeString Variant to Url via get<Url>().
        Variant sv(String("pmfb://relay"));
        REQUIRE(sv.type() == Variant::TypeString);
        Url u = sv.get<Url>();
        CHECK(u.isValid());
        CHECK(u.scheme() == "pmfb");
        CHECK(u.host() == "relay");
}

TEST_CASE("Variant_Url_FromStringVariant_Malformed") {
        // From==String branch must return invalid Url and set Error::Invalid
        // when the string is not a parseable URL.
        Variant sv(String("not-a-url"));
        Error err = Error::Ok;
        Url u = sv.get<Url>(&err);
        CHECK_FALSE(u.isValid());
        CHECK(err.isError());
}
