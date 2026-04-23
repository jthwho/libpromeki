/**
 * @file      variantspec.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/variantspec.h>
#include <promeki/variantdatabase.h>
#include <promeki/buffer.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/size2d.h>
#include <promeki/textstream.h>
#include <promeki/timecode.h>
#include <promeki/videocodec.h>

using namespace promeki;

// ============================================================================
// VariantSpec basics
// ============================================================================

TEST_CASE("VariantSpec_Default") {
    VariantSpec s;
    CHECK(!s.isValid());
    CHECK(s.types().isEmpty());
    CHECK(!s.isPolymorphic());
    CHECK(!s.hasRange());
    CHECK(!s.hasEnumType());
    CHECK(s.description().isEmpty());
    Variant v = s;
    CHECK(!v.isValid());
}

TEST_CASE("VariantSpec_SingleType") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeS32)
        .setDefault(42)
        .setDescription("An integer value");
    CHECK(s.isValid());
    CHECK(s.types().size() == 1);
    CHECK(s.types()[0] == Variant::TypeS32);
    CHECK(!s.isPolymorphic());
    CHECK(s.acceptsType(Variant::TypeS32));
    CHECK(!s.acceptsType(Variant::TypeString));
    CHECK(s.description() == "An integer value");
    Variant v = s;
    CHECK(v.get<int32_t>() == 42);
}

TEST_CASE("VariantSpec_Polymorphic") {
    VariantSpec s = VariantSpec()
        .setTypes({Variant::TypeString, Variant::TypeS32})
        .setDefault(String("hello"));
    CHECK(s.isPolymorphic());
    CHECK(s.acceptsType(Variant::TypeString));
    CHECK(s.acceptsType(Variant::TypeS32));
    CHECK(!s.acceptsType(Variant::TypeBool));
}

TEST_CASE("VariantSpec_Range") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeS32)
        .setDefault(85)
        .setRange(1, 100);
    CHECK(s.hasRange());
    CHECK(s.hasMin());
    CHECK(s.hasMax());

    CHECK(s.validate(Variant(50)));
    CHECK(s.validate(Variant(1)));
    CHECK(s.validate(Variant(100)));
    CHECK(!s.validate(Variant(0)));
    CHECK(!s.validate(Variant(101)));

    Error err;
    s.validate(Variant(200), &err);
    CHECK(err == Error::OutOfRange);
}

TEST_CASE("VariantSpec_MinOnly") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeDouble)
        .setMin(0.0);
    CHECK(s.hasMin());
    CHECK(!s.hasMax());
    CHECK(s.validate(Variant(0.0)));
    CHECK(s.validate(Variant(999.0)));
    CHECK(!s.validate(Variant(-1.0)));
}

TEST_CASE("VariantSpec_MaxOnly") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeS32)
        .setMax(127);
    CHECK(!s.hasMin());
    CHECK(s.hasMax());
    CHECK(s.validate(Variant(int32_t(0))));
    CHECK(s.validate(Variant(int32_t(127))));
    CHECK(!s.validate(Variant(int32_t(128))));
}

TEST_CASE("VariantSpec_TypeValidation") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeBool)
        .setDefault(true);

    CHECK(s.validate(Variant(true)));
    CHECK(s.validate(Variant(false)));
    CHECK(!s.validate(Variant(42)));

    Error err;
    s.validate(Variant(42), &err);
    CHECK(err == Error::InvalidArgument);
}

TEST_CASE("VariantSpec_OperatorVariant") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeString)
        .setDefault(String("default_path"));

    Variant v = s;
    CHECK(v.type() == Variant::TypeString);
    CHECK(v.get<String>() == "default_path");
}

// ============================================================================
// VariantDatabase spec registry
// ============================================================================

TEST_CASE("VariantDatabase_DeclareID") {
    using DB = VariantDatabase<"TestSpec">;
    static const DB::ID TestWidth = DB::declareID("TestWidth",
        VariantSpec().setType(Variant::TypeS32).setDefault(1920)
                     .setDescription("Width in pixels"));

    const VariantSpec *s = DB::spec(TestWidth);
    REQUIRE(s != nullptr);
    CHECK(s->types().size() == 1);
    CHECK(s->types()[0] == Variant::TypeS32);
    CHECK(s->defaultValue().get<int32_t>() == 1920);
    CHECK(s->description() == "Width in pixels");
}

TEST_CASE("VariantDatabase_SpecReturnsNull") {
    using DB = VariantDatabase<"TestSpec">;
    DB::ID unspecced("NoSpec");
    CHECK(DB::spec(unspecced) == nullptr);
}

TEST_CASE("VariantDatabase_FromSpecs") {
    using DB = VariantDatabase<"TestSpec">;
    DB::SpecMap specs;
    DB::ID a = DB::declareID("SpecA", VariantSpec().setType(Variant::TypeS32).setDefault(10));
    DB::ID b = DB::declareID("SpecB", VariantSpec().setType(Variant::TypeBool).setDefault(true));
    specs.insert(a, *DB::spec(a));
    specs.insert(b, *DB::spec(b));

    DB db = DB::fromSpecs(specs);
    CHECK(db.getAs<int32_t>(a) == 10);
    CHECK(db.getAs<bool>(b) == true);
}

TEST_CASE("VariantDatabase_ValidationWarn") {
    using DB = VariantDatabase<"TestSpec">;
    DB::ID quality = DB::declareID("Quality",
        VariantSpec().setType(Variant::TypeS32).setDefault(85).setRange(1, 100));

    DB db;
    db.setValidation(SpecValidation::Warn);
    // Out-of-range value: stored with warning
    bool stored = db.set(quality, 200);
    CHECK(stored);
    CHECK(db.getAs<int32_t>(quality) == 200);
}

TEST_CASE("VariantDatabase_ValidationStrict") {
    using DB = VariantDatabase<"TestSpec">;
    DB::ID level = DB::declareID("Level",
        VariantSpec().setType(Variant::TypeS32).setDefault(5).setRange(0, 10));

    DB db;
    db.setValidation(SpecValidation::Strict);
    // In-range: accepted
    CHECK(db.set(level, 5));
    CHECK(db.getAs<int32_t>(level) == 5);
    // Out-of-range: rejected
    CHECK(!db.set(level, 20));
    CHECK(db.getAs<int32_t>(level) == 5); // unchanged
}

TEST_CASE("VariantDatabase_ValidationNone") {
    using DB = VariantDatabase<"TestSpec">;
    DB::ID x = DB::declareID("X",
        VariantSpec().setType(Variant::TypeS32).setDefault(0).setRange(0, 10));

    DB db;
    db.setValidation(SpecValidation::None);
    CHECK(db.set(x, 999));
    CHECK(db.getAs<int32_t>(x) == 999);
}

// ============================================================================
// Formatting — typeName
// ============================================================================

TEST_CASE("VariantSpec_TypeName_Single") {
    CHECK(VariantSpec().setType(Variant::TypeBool).typeName() == "bool");
    CHECK(VariantSpec().setType(Variant::TypeS32).typeName() == "int");
    CHECK(VariantSpec().setType(Variant::TypeDouble).typeName() == "double");
    CHECK(VariantSpec().setType(Variant::TypeString).typeName() == "String");
    CHECK(VariantSpec().setType(Variant::TypeFrameRate).typeName() == "FrameRate");
    CHECK(VariantSpec().setType(Variant::TypeSize2D).typeName() == "Size2D");
    CHECK(VariantSpec().setType(Variant::TypeTimecode).typeName() == "Timecode");
    CHECK(VariantSpec().setType(Variant::TypePixelFormat).typeName() == "PixelFormat");
}

TEST_CASE("VariantSpec_TypeName_Enum") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeEnum)
        .setEnumType(VideoPattern::Type);
    CHECK(s.typeName() == "Enum VideoPattern");
}

TEST_CASE("VariantSpec_TypeName_Polymorphic") {
    VariantSpec s = VariantSpec()
        .setTypes({Variant::TypeString, Variant::TypeS32});
    CHECK(s.typeName() == "String | int");
}

TEST_CASE("VariantSpec_TypeName_Empty") {
    CHECK(VariantSpec().typeName() == "(any)");
}

// ============================================================================
// Formatting — rangeString
// ============================================================================

TEST_CASE("VariantSpec_RangeString_Closed") {
    VariantSpec s = VariantSpec().setRange(1, 100);
    CHECK(s.rangeString() == "1 - 100");
}

TEST_CASE("VariantSpec_RangeString_MinOnly") {
    VariantSpec s = VariantSpec().setMin(0);
    CHECK(s.rangeString() == ">= 0");
}

TEST_CASE("VariantSpec_RangeString_MaxOnly") {
    VariantSpec s = VariantSpec().setMax(127);
    CHECK(s.rangeString() == "<= 127");
}

TEST_CASE("VariantSpec_RangeString_Float") {
    VariantSpec s = VariantSpec().setRange(0.0, 1.0);
    CHECK(s.rangeString() == "0 - 1");
}

TEST_CASE("VariantSpec_RangeString_None") {
    CHECK(VariantSpec().rangeString().isEmpty());
}

// ============================================================================
// Formatting — defaultString
// ============================================================================

TEST_CASE("VariantSpec_DefaultString_Int") {
    CHECK(VariantSpec().setDefault(85).defaultString() == "85");
}

TEST_CASE("VariantSpec_DefaultString_Float") {
    CHECK(VariantSpec().setDefault(48000.0f).defaultString() == "48000");
    CHECK(VariantSpec().setDefault(2.5).defaultString() == "2.5");
}

TEST_CASE("VariantSpec_DefaultString_Bool") {
    CHECK(VariantSpec().setDefault(true).defaultString() == "true");
    CHECK(VariantSpec().setDefault(false).defaultString() == "false");
}

TEST_CASE("VariantSpec_DefaultString_Enum") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeEnum)
        .setDefault(VideoPattern::ColorBars)
        .setEnumType(VideoPattern::Type);
    CHECK(s.defaultString() == "ColorBars");
}

TEST_CASE("VariantSpec_DefaultString_EmptyString") {
    CHECK(VariantSpec().setDefault(String()).defaultString() == "(empty)");
}

TEST_CASE("VariantSpec_DefaultString_String") {
    CHECK(VariantSpec().setDefault(String("hello")).defaultString() == "hello");
}

TEST_CASE("VariantSpec_DefaultString_None") {
    CHECK(VariantSpec().defaultString() == "(none)");
}

// ============================================================================
// parseString — scalars
// ============================================================================

TEST_CASE("VariantSpec_ParseString_Bool") {
    VariantSpec s = VariantSpec().setType(Variant::TypeBool);
    CHECK(s.parseString("true").get<bool>() == true);
    CHECK(s.parseString("false").get<bool>() == false);
    CHECK(s.parseString("yes").get<bool>() == true);
    CHECK(s.parseString("no").get<bool>() == false);
    CHECK(s.parseString("1").get<bool>() == true);
    CHECK(s.parseString("0").get<bool>() == false);
    CHECK(s.parseString("on").get<bool>() == true);
    CHECK(s.parseString("off").get<bool>() == false);
    Error err;
    s.parseString("maybe", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_Int") {
    VariantSpec s = VariantSpec().setType(Variant::TypeS32);
    CHECK(s.parseString("42").get<int32_t>() == 42);
    CHECK(s.parseString("-7").get<int32_t>() == -7);
    Error err;
    s.parseString("abc", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_Double") {
    VariantSpec s = VariantSpec().setType(Variant::TypeDouble);
    CHECK(s.parseString("3.14").get<double>() == doctest::Approx(3.14));
    CHECK(s.parseString("-20").get<double>() == doctest::Approx(-20.0));
}

TEST_CASE("VariantSpec_ParseString_String") {
    VariantSpec s = VariantSpec().setType(Variant::TypeString);
    CHECK(s.parseString("hello world").get<String>() == "hello world");
    CHECK(s.parseString("").get<String>().isEmpty());
}

// ============================================================================
// parseString — complex types
// ============================================================================

TEST_CASE("VariantSpec_ParseString_Size2D") {
    VariantSpec s = VariantSpec().setType(Variant::TypeSize2D);
    Variant v = s.parseString("1920x1080");
    REQUIRE(v.isValid());
    Size2Du32 sz = v.get<Size2Du32>();
    CHECK(sz.width() == 1920);
    CHECK(sz.height() == 1080);
    Error err;
    s.parseString("notasize", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_FrameRate") {
    VariantSpec s = VariantSpec().setType(Variant::TypeFrameRate);
    Variant v = s.parseString("29.97");
    REQUIRE(v.isValid());
    CHECK(v.get<FrameRate>().isValid());
}

TEST_CASE("VariantSpec_ParseString_Timecode") {
    VariantSpec s = VariantSpec().setType(Variant::TypeTimecode);
    Variant v = s.parseString("01:00:00:00");
    REQUIRE(v.isValid());
}

TEST_CASE("VariantSpec_ParseString_PixelFormat") {
    VariantSpec s = VariantSpec().setType(Variant::TypePixelFormat);
    Variant v = s.parseString("RGB8_sRGB");
    REQUIRE(v.isValid());
    CHECK(v.get<PixelFormat>() == PixelFormat(PixelFormat::RGB8_sRGB));
    Error err;
    s.parseString("NonexistentFormat", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_PixelMemLayout") {
    VariantSpec s = VariantSpec().setType(Variant::TypePixelMemLayout);
    Variant v = s.parseString("4x8");
    REQUIRE(v.isValid());
    CHECK(v.get<PixelMemLayout>() == PixelMemLayout(PixelMemLayout::I_4x8));
    Error err;
    s.parseString("NonexistentLayout", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_AudioFormat") {
    VariantSpec s = VariantSpec().setType(Variant::TypeAudioFormat);
    Variant v = s.parseString("PCMI_S16LE");
    REQUIRE(v.isValid());
    CHECK(v.get<AudioFormat>().id() == AudioFormat::PCMI_S16LE);
    Error err;
    s.parseString("NonexistentAudioFormat", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_VideoCodec_BareName") {
    // Bare "H264" (no backend pin) must still parse after the switch
    // from lookup() to fromString() — common config-file form.
    VariantSpec s = VariantSpec().setType(Variant::TypeVideoCodec);
    Variant v = s.parseString("H264");
    REQUIRE(v.isValid());
    CHECK(v.get<VideoCodec>().id() == VideoCodec::H264);
    Error err;
    s.parseString("NotACodec", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_AudioCodec_BareName") {
    VariantSpec s = VariantSpec().setType(Variant::TypeAudioCodec);
    Variant v = s.parseString("AAC");
    REQUIRE(v.isValid());
    CHECK(v.get<AudioCodec>().id() == AudioCodec::AAC);
    Error err;
    s.parseString("NotAnAudioCodec", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_Enum") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeEnum)
        .setEnumType(VideoPattern::Type);
    Variant v = s.parseString("Grid");
    REQUIRE(v.isValid());
    CHECK(v.get<Enum>().value() == VideoPattern::Grid.value());

    // Fully qualified form
    Variant v2 = s.parseString("VideoPattern::Ramp");
    REQUIRE(v2.isValid());
    CHECK(v2.get<Enum>().value() == VideoPattern::Ramp.value());

    Error err;
    s.parseString("NonexistentValue", &err);
    CHECK(err.isError());
}

TEST_CASE("VariantSpec_ParseString_StringList") {
    VariantSpec s = VariantSpec().setType(Variant::TypeStringList);
    Variant v = s.parseString("a,b,c");
    REQUIRE(v.isValid());
    StringList sl = v.get<StringList>();
    CHECK(sl.size() == 3);
    CHECK(sl[0] == "a");
    CHECK(sl[1] == "b");
    CHECK(sl[2] == "c");
}

// ============================================================================
// parseString — polymorphic
// ============================================================================

TEST_CASE("VariantSpec_ParseString_Polymorphic") {
    // String | int: string "42" should parse as String (first type tried)
    VariantSpec s = VariantSpec().setTypes({Variant::TypeString, Variant::TypeS32});
    Variant v = s.parseString("42");
    CHECK(v.type() == Variant::TypeString);

    // int | String: "42" should parse as int (first type tried)
    VariantSpec s2 = VariantSpec().setTypes({Variant::TypeS32, Variant::TypeString});
    Variant v2 = s2.parseString("42");
    CHECK(v2.type() == Variant::TypeS32);
    CHECK(v2.get<int32_t>() == 42);
}

TEST_CASE("VariantSpec_ParseString_NoType") {
    // No type constraint — returns as String
    VariantSpec s;
    Variant v = s.parseString("anything");
    CHECK(v.type() == Variant::TypeString);
    CHECK(v.get<String>() == "anything");
}

// ============================================================================
// detailsString / description
// ============================================================================

TEST_CASE("VariantSpec_DetailsString") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeS32)
        .setDefault(85)
        .setRange(1, 100)
        .setDescription("JPEG quality 1-100.");

    String details = s.detailsString();
    // Details column packs the type, range, and default into a
    // compact form the caller can line up in its own column.
    CHECK(details.contains("int"));
    CHECK(details.contains("1 - 100"));
    CHECK(details.contains("def: 85"));

    // Description stays on VariantSpec — callers render it as the
    // third help column.
    CHECK(s.description() == "JPEG quality 1-100.");
}

TEST_CASE("VariantSpec_DetailsString_Enum") {
    VariantSpec s = VariantSpec()
        .setType(Variant::TypeEnum)
        .setDefault(VideoPattern::ColorBars)
        .setEnumType(VideoPattern::Type)
        .setDescription("Selected test pattern.");

    String details = s.detailsString();
    CHECK(details.contains("Enum VideoPattern"));
    CHECK(details.contains("def: ColorBars"));
    // Enum values should not be listed inside the details column.
    CHECK(!details.contains("Values:"));

    CHECK(s.description() == "Selected test pattern.");
}
