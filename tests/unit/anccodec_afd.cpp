/**
 * @file      anccodec_afd.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancafd.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // Legacy uint8_t packing: bit 7 = AR flag, bits 3..6 = AFD code.
        constexpr uint8_t packLegacy(uint8_t afdCode, bool ar) {
                uint8_t v = static_cast<uint8_t>((afdCode & 0x0F) << 3);
                if (ar) v = static_cast<uint8_t>(v | 0x80);
                return v;
        }

} // namespace

// ============================================================================
// Canonical AncAfd round-trip — every AFD code, both AR settings.
// ============================================================================

TEST_CASE("AFD<->St291: AncAfd round-trip every AFD code with AR=1") {
        AncTranslator t;
        for (uint8_t code = 0; code < 16; ++code) {
                AncAfd input(code, true);
                Result<List<AncPacket>> built = t.build(Variant(input), AncFormat(AncFormat::Afd), AncTransport::St291);
                REQUIRE(built.second().isOk());
                CHECK(built.first().front().format().id() == AncFormat::Afd);
                CHECK(built.first().front().transport() == AncTransport::St291);

                Result<St291Packet> rp = St291Packet::from(built.first().front());
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().did() == 0x41);
                CHECK(rp.first().sdid() == 0x05);
                CHECK(rp.first().dataCount() == 8);
                CHECK(rp.first().checksumValid());

                Result<Variant> parsed = t.parse(built.first().front());
                REQUIRE(parsed.second().isOk());
                AncAfd back = parsed.first().get<AncAfd>();
                CHECK(back == input);
        }
}

TEST_CASE("AFD<->St291: AncAfd round-trip AR=0 path") {
        AncTranslator t;
        AncAfd input(0x09, false);
        Result<List<AncPacket>> built = t.build(Variant(input), AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        CHECK(parsed.first().get<AncAfd>() == input);
}

TEST_CASE("AFD<->St291: AncAfd round-trip preserves letterbox bar data") {
        AncTranslator t;
        AncAfd input(0x0A, true);
        input.setBarFlag(AncAfd::TopBar, true);
        input.setBarFlag(AncAfd::BottomBar, true);
        input.setBarValue1(0x003C);   // top-bar end line  (60)
        input.setBarValue2(0x01A4);   // bottom-bar start line  (420)

        Result<List<AncPacket>> built = t.build(Variant(input), AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 8);
        // UDW 4 (index 3): Top|Bot|Left|Right flags in bits 7-4 (=0xC0
        // here: top + bottom only).
        CHECK((udw[3] & 0xFF) == 0xC0);
        // UDW 5-6 (indices 4-5): bar value 1 MSB-first.
        CHECK((udw[4] & 0xFF) == 0x00);
        CHECK((udw[5] & 0xFF) == 0x3C);
        // UDW 7-8 (indices 6-7): bar value 2 MSB-first.
        CHECK((udw[6] & 0xFF) == 0x01);
        CHECK((udw[7] & 0xFF) == 0xA4);

        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAfd back = parsed.first().get<AncAfd>();
        CHECK(back == input);
        CHECK(back.topBar());
        CHECK(back.bottomBar());
        CHECK_FALSE(back.leftBar());
        CHECK_FALSE(back.rightBar());
        CHECK(back.barValue1() == 0x003C);
        CHECK(back.barValue2() == 0x01A4);
}

TEST_CASE("AFD<->St291: AncAfd round-trip preserves pillarbox bar data") {
        AncTranslator t;
        AncAfd input(0x08, false);
        input.setBarFlag(AncAfd::LeftBar, true);
        input.setBarFlag(AncAfd::RightBar, true);
        input.setBarValue1(0x0078);    // left-bar last pixel (120)
        input.setBarValue2(0x0A00);    // right-bar first pixel (2560)

        Result<List<AncPacket>> built = t.build(Variant(input), AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());

        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        List<uint16_t> udw = rp.first().udw();
        REQUIRE(udw.size() == 8);
        CHECK((udw[3] & 0xFF) == 0x30);    // Left + Right in bits 5-4

        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAfd back = parsed.first().get<AncAfd>();
        CHECK(back == input);
        CHECK(back.leftBar());
        CHECK(back.rightBar());
        CHECK(back.barValue1() == 0x0078);
        CHECK(back.barValue2() == 0x0A00);
}

TEST_CASE("AFD<->St291: built packet has 8 UDWs, reserved slots zeroed") {
        AncTranslator t;
        Result<List<AncPacket>> built = t.build(Variant(AncAfd(0x0A, true)),
                                                AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().dataCount() == 8);
        List<uint16_t> udw = rp.first().udw();
        // UDWs 2..8 (indices 1..7) are all zero in the no-bar-data case.
        for (size_t i = 1; i < udw.size(); ++i) {
                CHECK((udw[i] & 0xFF) == 0);
        }
}

// ============================================================================
// Legacy uint8_t Variant compatibility shim.
// ============================================================================

TEST_CASE("AFD<->St291: legacy uint8_t Variant input still builds a valid packet") {
        AncTranslator t;
        // The pre-F4 API used a packed uint8_t (bit 7 = AR, bits 6..3 = code).
        Result<List<AncPacket>> built = t.build(Variant(packLegacy(0x0A, true)),
                                                AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());

        // Parsed result is the new AncAfd shape with zero bar data
        // (the legacy form had no slot to carry it).
        Result<Variant> parsed = t.parse(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAfd back = parsed.first().get<AncAfd>();
        CHECK(back.afdCode() == 0x0A);
        CHECK(back.arFlag() == true);
        CHECK_FALSE(back.hasBarData());
        CHECK(back.barValue1() == 0);
        CHECK(back.barValue2() == 0);
}

// ============================================================================
// Capability / cfg threading.
// ============================================================================

TEST_CASE("AFD<->St291: capability queries report parser+builder registered") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Afd), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::Afd), AncTransport::St291));
}

TEST_CASE("AFD<->St291: line / fieldB threaded from AncTranslateConfig") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(13));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        AncTranslator     t(cfg);
        Result<List<AncPacket>> built = t.build(Variant(AncAfd(0x0A, true)),
                                                AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        Result<St291Packet> rp = St291Packet::from(built.first().front());
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 13);
        CHECK(rp.first().fieldB() == true);
}

// ===========================================================================
// Frame-sync policy: AFD is sticky / idempotent — copy through on Repeat,
// drop on Drop, no per-frame state to advance.
// ===========================================================================

TEST_CASE("AFD sync policy: Play returns the packet unchanged") {
        AncTranslator           t;
        Result<List<AncPacket>> built = t.build(Variant(AncAfd(0x0A, true)),
                                                  AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        // Bytes preserved exactly.
        CHECK(res.first().front().data().size() == pkt.data().size());
}

TEST_CASE("AFD sync policy: Drop returns an empty list") {
        AncTranslator           t;
        Result<List<AncPacket>> built = t.build(Variant(AncAfd(0x0A, true)),
                                                  AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("AFD sync policy: Repeat copies the packet through at every index") {
        AncTranslator           t;
        AncAfd                  src(0x0A, true);
        Result<List<AncPacket>> built = t.build(Variant(src),
                                                  AncFormat(AncFormat::Afd), AncTransport::St291);
        REQUIRE(built.second().isOk());
        const AncPacket &pkt = built.first().front();

        for (uint8_t i = 0; i < 4; ++i) {
                Result<List<AncPacket>> res = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                Result<Variant> parsed = t.parse(res.first().front());
                REQUIRE(parsed.second().isOk());
                CHECK(parsed.first().get<AncAfd>() == src);
        }
}

TEST_CASE("AFD sync policy: hasSyncPolicy reflects registration") {
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::Afd)));
}

// ===========================================================================
// AncAfd value-type sanity (mirrors AncAtc unit tests in shape).
// ===========================================================================

TEST_CASE("AncAfd: default-constructed value is zero / no bar data") {
        AncAfd a;
        CHECK(a.afdCode() == 0);
        CHECK_FALSE(a.arFlag());
        CHECK(a.barFlags() == 0);
        CHECK_FALSE(a.hasBarData());
        CHECK(a.barValue1() == 0);
        CHECK(a.barValue2() == 0);
}

TEST_CASE("AncAfd: setBarFlags masks off the reserved low nibble") {
        AncAfd a;
        a.setBarFlags(0xFF);
        // Low nibble per ST 2016-3 Table 1 is reserved-zero; the
        // setter masks it.
        CHECK(a.barFlags() == 0xF0);
        CHECK(a.topBar());
        CHECK(a.bottomBar());
        CHECK(a.leftBar());
        CHECK(a.rightBar());
}

TEST_CASE("AncAfd: per-flag accessors round-trip independently") {
        AncAfd a;
        a.setTopBar(true);
        CHECK(a.barFlags() == AncAfd::TopBar);
        a.setBottomBar(true);
        CHECK(a.barFlags() == (AncAfd::TopBar | AncAfd::BottomBar));
        a.setTopBar(false);
        CHECK(a.barFlags() == AncAfd::BottomBar);
        a.setLeftBar(true);
        a.setRightBar(true);
        CHECK(a.barFlags() == (AncAfd::BottomBar | AncAfd::LeftBar | AncAfd::RightBar));
}

TEST_CASE("AncAfd: equality / inequality") {
        AncAfd a(0x0A, true);
        AncAfd b(0x0A, true);
        CHECK(a == b);
        b.setBarValue1(42);
        CHECK(a != b);
        a.setBarValue1(42);
        CHECK(a == b);
}

TEST_CASE("AncAfd: Variant round-trip preserves identity") {
        AncAfd a(0x0A, true);
        a.setTopBar(true);
        a.setBarValue1(0x0123);
        Variant v(a);
        CHECK(v.type() == DataTypeAncAfd);
        AncAfd back = v.get<AncAfd>();
        CHECK(back == a);
}

TEST_CASE("AncAfd: DataStream round-trip") {
        AncAfd a(0x0B, true);
        a.setBarFlag(AncAfd::LeftBar, true);
        a.setBarFlag(AncAfd::RightBar, true);
        a.setBarValue1(0xBEEF);
        a.setBarValue2(0xCAFE);

        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << a;
        }
        dev.seek(0);
        AncAfd back;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> back;
        }
        CHECK(back == a);
}

// ===========================================================================
// PanScan registry verification (F4 rename of BarData).
// ===========================================================================

TEST_CASE("PanScan: registered at DID 0x41 / SDID 0x06 with ST 2016-4 description") {
        AncFormat f(AncFormat::PanScan);
        CHECK(f.isValid());
        CHECK(f.name() == "PanScan");
        CHECK(f.st291Did() == 0x41);
        CHECK(f.st291Sdid() == 0x06);
        CHECK(f.category() == AncCategory::Aspect);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        // Description must reference ST 2016-4 rather than the
        // pre-F4 wrong "ST 2016-3 Bar Data" text.
        CHECK(f.desc().contains("2016-4"));
        CHECK(f.desc().contains("Pan-Scan"));
}

TEST_CASE("PanScan: fromName returns the format") {
        Result<AncFormat> r = AncFormat::fromName("PanScan");
        REQUIRE(isOk(r));
        CHECK(value(r).id() == AncFormat::PanScan);
}

TEST_CASE("PanScan: legacy 'BarData' name no longer resolves") {
        Result<AncFormat> r = AncFormat::fromName("BarData");
        CHECK(isError(r));
        CHECK(error(r) == Error::IdNotFound);
}
