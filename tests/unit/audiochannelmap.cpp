/**
 * @file      audiochannelmap.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <unordered_set>
#include <doctest/doctest.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiostreamdesc.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// ChannelRole basics
// ============================================================================

TEST_CASE("ChannelRole: default is Unused") {
        ChannelRole r;
        CHECK(r == ChannelRole::Unused);
        CHECK(r.value() == 0);
}

TEST_CASE("ChannelRole: lookup by name") {
        ChannelRole r("FrontLeft");
        CHECK(r == ChannelRole::FrontLeft);
        CHECK(r.value() == 2);
}

TEST_CASE("ChannelRole: roundtrips through Enum::valueOf") {
        for (auto name : {"FrontLeft", "FrontRight", "FrontCenter", "LFE", "BackLeft", "BackRight", "BackCenter",
                          "SideLeft", "SideRight", "FrontLeftOfCenter", "FrontRightOfCenter", "TopFrontLeft",
                          "TopBackLeft", "TopCenter", "AmbisonicW", "Aux0", "Aux7"}) {
                CAPTURE(name);
                ChannelRole r{String(name)};
                CHECK(r.hasListedValue());
                CHECK(r.valueName() == name);
        }
}

// ============================================================================
// AudioChannelMap construction and accessors
// ============================================================================

TEST_CASE("AudioChannelMap: default-constructed is empty/invalid") {
        AudioChannelMap m;
        CHECK_FALSE(m.isValid());
        CHECK(m.channels() == 0);
}

TEST_CASE("AudioChannelMap: from initializer list of roles") {
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        CHECK(m.isValid());
        CHECK(m.channels() == 2);
        CHECK(m.role(0) == ChannelRole::FrontLeft);
        CHECK(m.role(1) == ChannelRole::FrontRight);
        CHECK(m.role(99) == ChannelRole::Unused); // out-of-range soft-fail
}

TEST_CASE("AudioChannelMap: setRole grows the map with Unused fillers") {
        AudioChannelMap m;
        m.setRole(3, ChannelRole::FrontCenter);
        CHECK(m.channels() == 4);
        CHECK(m.role(0) == ChannelRole::Unused);
        CHECK(m.role(1) == ChannelRole::Unused);
        CHECK(m.role(2) == ChannelRole::Unused);
        CHECK(m.role(3) == ChannelRole::FrontCenter);
}

TEST_CASE("AudioChannelMap: indexOf and contains") {
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::LFE};
        CHECK(m.indexOf(ChannelRole::FrontLeft) == 0);
        CHECK(m.indexOf(ChannelRole::FrontRight) == 1);
        CHECK(m.indexOf(ChannelRole::LFE) == 2);
        CHECK(m.indexOf(ChannelRole::TopBackLeft) == -1);
        CHECK(m.contains(ChannelRole::FrontLeft));
        CHECK_FALSE(m.contains(ChannelRole::Mono));
}

// ============================================================================
// Well-known layouts
// ============================================================================

TEST_CASE("AudioChannelMap: well-known layouts are non-empty") {
        auto layouts = AudioChannelMap::wellKnownLayouts();
        CHECK_FALSE(layouts.isEmpty());
        // Expect at least these names to appear.
        bool sawStereo = false, sawFiveOne = false, sawSevenOneFour = false;
        for (const auto &l : layouts) {
                if (l.name == "Stereo") sawStereo = true;
                if (l.name == "5.1") sawFiveOne = true;
                if (l.name == "7.1.4") sawSevenOneFour = true;
        }
        CHECK(sawStereo);
        CHECK(sawFiveOne);
        CHECK(sawSevenOneFour);
}

TEST_CASE("AudioChannelMap: wellKnownName resolves an exact match") {
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        CHECK(m.wellKnownName() == "Stereo");
        CHECK(m.isWellKnown());
}

TEST_CASE("AudioChannelMap: wellKnownName is order-sensitive") {
        AudioChannelMap m{ChannelRole::FrontRight, ChannelRole::FrontLeft};
        CHECK(m.wellKnownName().isEmpty());
        CHECK_FALSE(m.isWellKnown());
}

TEST_CASE("AudioChannelMap: 5.1 layout matches SMPTE order") {
        // SMPTE order: L R C LFE Ls Rs.
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                          ChannelRole::LFE,       ChannelRole::BackLeft,   ChannelRole::BackRight};
        CHECK(m.wellKnownName() == "5.1");
}

TEST_CASE("AudioChannelMap: FOA layout matches AmbisonicW/X/Y/Z") {
        AudioChannelMap m{ChannelRole::AmbisonicW, ChannelRole::AmbisonicX, ChannelRole::AmbisonicY,
                          ChannelRole::AmbisonicZ};
        CHECK(m.wellKnownName() == "FOA");
}

// ============================================================================
// defaultForChannels
// ============================================================================

TEST_CASE("AudioChannelMap: defaultForChannels picks canonical layouts") {
        CHECK(AudioChannelMap::defaultForChannels(1).wellKnownName() == "Mono");
        CHECK(AudioChannelMap::defaultForChannels(2).wellKnownName() == "Stereo");
        CHECK(AudioChannelMap::defaultForChannels(6).wellKnownName() == "5.1");
        CHECK(AudioChannelMap::defaultForChannels(8).wellKnownName() == "7.1");
        CHECK(AudioChannelMap::defaultForChannels(12).wellKnownName() == "7.1.4");
}

TEST_CASE("AudioChannelMap: defaultForChannels for non-standard count gives all-Unused") {
        AudioChannelMap m = AudioChannelMap::defaultForChannels(13);
        CHECK(m.channels() == 13);
        CHECK(m.wellKnownName().isEmpty());
        for (size_t i = 0; i < 13; ++i) CHECK(m.role(i) == ChannelRole::Unused);
}

// ============================================================================
// fromString / toString round-trip
// ============================================================================

TEST_CASE("AudioChannelMap: fromString accepts well-known layout names") {
        for (auto name : {"Mono", "Stereo", "2.1", "5.1", "7.1", "7.1.4", "FOA"}) {
                CAPTURE(name);
                AudioChannelMap m = value(AudioChannelMap::fromString(name));
                CHECK(m.isValid());
                CHECK(m.wellKnownName() == name);
        }
}

TEST_CASE("AudioChannelMap: fromString accepts plain comma-separated role list") {
        AudioChannelMap m = value(AudioChannelMap::fromString("FrontLeft,FrontRight,LFE"));
        CHECK(m.channels() == 3);
        CHECK(m.role(0) == ChannelRole::FrontLeft);
        CHECK(m.role(2) == ChannelRole::LFE);
        // Every channel was Undefined-stream.
        CHECK(m.stream(0).isUndefined());
}

TEST_CASE("AudioChannelMap: fromString tolerates whitespace around commas") {
        AudioChannelMap m = value(AudioChannelMap::fromString("FrontLeft , FrontRight , LFE"));
        CHECK(m.channels() == 3);
        CHECK(m.role(0) == ChannelRole::FrontLeft);
        CHECK(m.role(2) == ChannelRole::LFE);
}

TEST_CASE("AudioChannelMap: fromString rejects garbage role names") {
        auto r = AudioChannelMap::fromString("FrontLeft,Xyzzy");
        CHECK(error(r) == Error::Invalid);
}

TEST_CASE("AudioChannelMap: toString round-trips well-known layouts") {
        for (auto name : {"Mono", "Stereo", "5.1", "7.1.4"}) {
                CAPTURE(name);
                AudioChannelMap m = value(AudioChannelMap::fromString(name));
                CHECK(m.toString() == name);
        }
}

TEST_CASE("AudioChannelMap: toString round-trips non-well-known via comma list") {
        // FrontLeft + Aux0 — not a well-known layout, so falls back to per-channel form.
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::Aux0};
        String          s = m.toString();
        CHECK(s == "FrontLeft,Aux0");
        AudioChannelMap back = value(AudioChannelMap::fromString(s));
        CHECK(back == m);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("AudioChannelMap: equality is order-sensitive") {
        AudioChannelMap a{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        AudioChannelMap b{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        AudioChannelMap c{ChannelRole::FrontRight, ChannelRole::FrontLeft};
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// DataStream serialization
// ============================================================================

TEST_CASE("AudioChannelMap: DataStream round-trip") {
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                          ChannelRole::LFE,       ChannelRole::BackLeft,   ChannelRole::BackRight};

        Buffer         buf(1024);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << m;
        }
        dev.seek(0);
        DataStream      rs = DataStream::createReader(&dev);
        AudioChannelMap back;
        rs >> back;
        CHECK(back == m);
        CHECK(back.wellKnownName() == "5.1");
}

TEST_CASE("AudioChannelMap: DataStream round-trip preserves stream assignments") {
        AudioStreamDesc main("Main");
        AudioStreamDesc commentary("Commentary");
        AudioChannelMap m{
                AudioChannelMap::Entry(main, ChannelRole::FrontLeft),
                AudioChannelMap::Entry(main, ChannelRole::FrontRight),
                AudioChannelMap::Entry(commentary, ChannelRole::Mono),
        };

        Buffer         buf(1024);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << m;
        }
        dev.seek(0);
        DataStream      rs = DataStream::createReader(&dev);
        AudioChannelMap back;
        rs >> back;
        CHECK(back == m);
        CHECK(back.stream(0) == main);
        CHECK(back.stream(2) == commentary);
}

// ============================================================================
// AudioStreamDesc
// ============================================================================

TEST_CASE("AudioStreamDesc: default-constructed is Undefined") {
        AudioStreamDesc s;
        CHECK(s.isUndefined());
        CHECK(s.id() == AudioStreamDesc::Undefined);
        CHECK(s.name() == "Undefined");
}

TEST_CASE("AudioStreamDesc: name lookup registers and round-trips") {
        AudioStreamDesc a("Main");
        CHECK_FALSE(a.isUndefined());
        CHECK(a.name() == "Main");
        // Same name, same ID.
        AudioStreamDesc b("Main");
        CHECK(a == b);
        CHECK(a.id() == b.id());
}

TEST_CASE("AudioStreamDesc: distinct names get distinct IDs") {
        AudioStreamDesc a("AudioStreamTest_X");
        AudioStreamDesc b("AudioStreamTest_Y");
        CHECK(a != b);
}

TEST_CASE("AudioStreamDesc: lookup returns Undefined for missing names") {
        AudioStreamDesc s = AudioStreamDesc::lookup("NeverRegisteredName_AudioStreamTest");
        CHECK(s.isUndefined());
}

TEST_CASE("AudioStreamDesc: empty name returns Undefined") {
        AudioStreamDesc s(String(""));
        CHECK(s.isUndefined());
}

TEST_CASE("AudioStreamDesc: rejects names containing ':' or ','") {
        AudioStreamDesc colon{String("Has:Colon")};
        CHECK(colon.isUndefined());
        AudioStreamDesc comma{String("Has,Comma")};
        CHECK(comma.isUndefined());
        // lookup() applies the same restriction.
        CHECK(AudioStreamDesc::lookup("With:Colon").isUndefined());
        CHECK(AudioStreamDesc::lookup("With,Comma").isUndefined());
}

// ============================================================================
// AudioChannelMap with explicit streams
// ============================================================================

TEST_CASE("AudioChannelMap: single-stream constructor pins every channel") {
        AudioStreamDesc main("Main");
        AudioChannelMap m(main, {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::LFE});
        CHECK(m.channels() == 3);
        CHECK(m.stream(0) == main);
        CHECK(m.stream(2) == main);
        CHECK(m.role(0) == ChannelRole::FrontLeft);
        CHECK(m.isSingleStream());
        CHECK(m.commonStream() == main);
}

TEST_CASE("AudioChannelMap: explicit per-channel entries support mixed streams") {
        AudioStreamDesc main("Main");
        AudioStreamDesc com("Commentary");
        AudioChannelMap m{
                AudioChannelMap::Entry(main, ChannelRole::FrontLeft),
                AudioChannelMap::Entry(main, ChannelRole::FrontRight),
                AudioChannelMap::Entry(com, ChannelRole::Mono),
        };
        CHECK(m.channels() == 3);
        CHECK_FALSE(m.isSingleStream());
        CHECK(m.commonStream().isUndefined());
        CHECK(m.indexOf(main, ChannelRole::FrontLeft) == 0);
        CHECK(m.indexOf(com, ChannelRole::Mono) == 2);
}

TEST_CASE("AudioChannelMap: setRole preserves existing stream assignment") {
        AudioStreamDesc main("Main");
        AudioChannelMap m(main, {ChannelRole::FrontLeft, ChannelRole::FrontRight});
        m.setRole(0, ChannelRole::FrontCenter);
        CHECK(m.stream(0) == main);
        CHECK(m.role(0) == ChannelRole::FrontCenter);
}

TEST_CASE("AudioChannelMap: setStream preserves existing role") {
        AudioChannelMap m{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        AudioStreamDesc main("Main");
        m.setStream(1, main);
        CHECK(m.stream(0).isUndefined());
        CHECK(m.stream(1) == main);
        CHECK(m.role(1) == ChannelRole::FrontRight);
}

TEST_CASE("AudioChannelMap: defaultForChannels with stream pins the layout to that stream") {
        AudioStreamDesc main("Main");
        AudioChannelMap m = AudioChannelMap::defaultForChannels(6, main);
        CHECK(m.channels() == 6);
        CHECK(m.commonStream() == main);
        CHECK(m.wellKnownName() == "Main:5.1");
}

TEST_CASE("AudioChannelMap: wellKnownName empty for mixed-stream maps") {
        AudioStreamDesc main("Main");
        AudioStreamDesc com("Commentary");
        AudioChannelMap m{
                AudioChannelMap::Entry(main, ChannelRole::FrontLeft),
                AudioChannelMap::Entry(com, ChannelRole::FrontRight),
        };
        CHECK(m.wellKnownName().isEmpty());
}

// ============================================================================
// fromString / toString with streams
// ============================================================================

TEST_CASE("AudioChannelMap: fromString accepts Stream:Layout shortcut") {
        AudioChannelMap m = value(AudioChannelMap::fromString("Main:5.1"));
        CHECK(m.channels() == 6);
        CHECK(m.commonStream() == AudioStreamDesc("Main"));
        CHECK(m.role(0) == ChannelRole::FrontLeft);
}

TEST_CASE("AudioChannelMap: toString emits Stream:Layout for single-stream named maps") {
        AudioStreamDesc main("Main");
        AudioChannelMap m = AudioChannelMap::defaultForChannels(2, main);
        CHECK(m.toString() == "Main:Stereo");
}

TEST_CASE("AudioChannelMap: per-channel form preserves stream prefix") {
        AudioChannelMap m = value(AudioChannelMap::fromString("Main:FrontLeft,Main:FrontRight,Commentary:Mono"));
        CHECK(m.channels() == 3);
        CHECK(m.stream(0) == AudioStreamDesc("Main"));
        CHECK(m.stream(2) == AudioStreamDesc("Commentary"));
        CHECK(m.toString() == "Main:FrontLeft,Main:FrontRight,Commentary:Mono");
}

TEST_CASE("AudioChannelMap: per-channel form mixes Undefined and named streams") {
        AudioChannelMap m = value(AudioChannelMap::fromString("FrontLeft,Commentary:Mono"));
        CHECK(m.channels() == 2);
        CHECK(m.stream(0).isUndefined());
        CHECK(m.stream(1) == AudioStreamDesc("Commentary"));
        CHECK(m.toString() == "FrontLeft,Commentary:Mono");
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("AudioStreamDesc: round-trips through Variant<->String") {
        Variant v(AudioStreamDesc("Main"));
        CHECK(v.type() == Variant::TypeAudioStreamDesc);
        CHECK(v.get<String>() == "Main");
        Variant         fromStr(v.get<String>());
        AudioStreamDesc back = fromStr.get<AudioStreamDesc>();
        CHECK(back == AudioStreamDesc("Main"));
}

TEST_CASE("AudioStreamDesc: Variant rejects names with reserved delimiters") {
        Variant         v(String("Bad:Name"));
        Error           err;
        AudioStreamDesc s = v.get<AudioStreamDesc>(&err);
        // A reserved delimiter is a parse failure for AudioStreamDesc — get<>
        // returns the default value and surfaces the error.
        CHECK(err.isError());
        CHECK(s.isUndefined());
}

TEST_CASE("AudioChannelMap: round-trips through Variant<->String") {
        Variant v(AudioChannelMap{ChannelRole::FrontLeft, ChannelRole::FrontRight});
        CHECK(v.type() == Variant::TypeAudioChannelMap);
        CHECK(v.get<String>() == "Stereo");
        Variant         fromStr(v.get<String>());
        AudioChannelMap back = fromStr.get<AudioChannelMap>();
        CHECK(back.wellKnownName() == "Stereo");
}

TEST_CASE("AudioChannelMap: Variant round-trip preserves stream prefixes") {
        AudioStreamDesc main("Main");
        AudioChannelMap orig(main, {ChannelRole::FrontLeft, ChannelRole::FrontRight});
        Variant         v(orig);
        String          s = v.get<String>();
        CHECK(s == "Main:Stereo");
        AudioChannelMap back = Variant(s).get<AudioChannelMap>();
        CHECK(back == orig);
}

// ============================================================================
// Hash support
// ============================================================================

TEST_CASE("AudioStreamDesc: hashable in std::unordered_set") {
        std::unordered_set<AudioStreamDesc> set;
        set.insert(AudioStreamDesc("Main"));
        set.insert(AudioStreamDesc("Music"));
        set.insert(AudioStreamDesc("Main")); // duplicate
        CHECK(set.size() == 2);
        CHECK(set.count(AudioStreamDesc("Main")) == 1);
        CHECK(set.count(AudioStreamDesc("NeverInserted")) == 0);
}

TEST_CASE("ChannelRole: hashable") {
        std::unordered_set<ChannelRole> set;
        set.insert(ChannelRole::FrontLeft);
        set.insert(ChannelRole::FrontRight);
        set.insert(ChannelRole::FrontLeft);
        CHECK(set.size() == 2);
}

TEST_CASE("AudioChannelMap: hashable, equal maps share a hash") {
        AudioChannelMap a{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        AudioChannelMap b{ChannelRole::FrontLeft, ChannelRole::FrontRight};
        AudioChannelMap c{ChannelRole::FrontRight, ChannelRole::FrontLeft};
        CHECK(std::hash<AudioChannelMap>()(a) == std::hash<AudioChannelMap>()(b));
        // Order-sensitive — distinct maps should hash differently almost
        // always; we don't assert that in case of an unlucky collision,
        // but we do check the inequality of the values themselves.
        CHECK(a != c);
        std::unordered_set<AudioChannelMap> set;
        set.insert(a);
        set.insert(b); // duplicate
        set.insert(c);
        CHECK(set.size() == 2);
}
