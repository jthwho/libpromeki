/**
 * @file      cea608packet.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/cea608.h>
#include <promeki/cea608packet.h>
#include <promeki/cea708cdp.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// Construction + accessors
// ============================================================================

TEST_CASE("Cea608Packet: default-constructed is empty CC1") {
        Cea608Packet pkt;
        CHECK(pkt.channel == Cea608Packet::Channel::CC1);
        CHECK(pkt.ccData.size() == 0);
}

TEST_CASE("Cea608Packet: explicit-construct stamps channel + ccData") {
        Cea708Cdp::CcDataList list;
        list.pushToBack(Cea708Cdp::CcData{true, 0, 0xC1, 0xC2});
        Cea608Packet pkt(Cea608Packet::Channel::CC2, list);
        CHECK(pkt.channel == Cea608Packet::Channel::CC2);
        REQUIRE(pkt.ccData.size() == 1);
        CHECK(pkt.ccData[0].b1 == 0xC1);
}

TEST_CASE("Cea608Packet::channelName returns the wire name") {
        CHECK(Cea608Packet::channelName(Cea608Packet::Channel::CC1) == String("CC1"));
        CHECK(Cea608Packet::channelName(Cea608Packet::Channel::CC2) == String("CC2"));
        CHECK(Cea608Packet::channelName(Cea608Packet::Channel::CC3) == String("CC3"));
        CHECK(Cea608Packet::channelName(Cea608Packet::Channel::CC4) == String("CC4"));
}

// ============================================================================
// fromCdp — channel filter (cc_type + intra-field channel bit)
// ============================================================================

TEST_CASE("Cea608Packet::fromCdp filters by cc_type and channel bit") {
        // Build a CDP with one triple per channel: CC1, CC2, CC3, CC4.
        // Use control bytes (b1 in 0x10..0x17) so the channel bit
        // discriminator activates.
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        cdp.ccDataPresent = true;
        cdp.captionServiceActive = true;
        // CC1: cc_type=0, b1=0x14 (RCL on CC1)
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0,
                Cea608::withOddParity(0x14), Cea608::withOddParity(0x20)});
        // CC2: cc_type=0, b1=0x1C (RCL on CC2 — bit 3 set)
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0,
                Cea608::withOddParity(0x1C), Cea608::withOddParity(0x20)});
        // CC3: cc_type=1, b1=0x14
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 1,
                Cea608::withOddParity(0x14), Cea608::withOddParity(0x20)});
        // CC4: cc_type=1, b1=0x1C
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 1,
                Cea608::withOddParity(0x1C), Cea608::withOddParity(0x20)});

        Cea608Packet cc1 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC1);
        REQUIRE(cc1.ccData.size() == 1);
        CHECK(Cea608::stripParity(cc1.ccData[0].b1) == 0x14);
        CHECK(cc1.ccData[0].type == 0);

        Cea608Packet cc2 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC2);
        REQUIRE(cc2.ccData.size() == 1);
        CHECK(Cea608::stripParity(cc2.ccData[0].b1) == 0x1C);
        CHECK(cc2.ccData[0].type == 0);

        Cea608Packet cc3 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC3);
        REQUIRE(cc3.ccData.size() == 1);
        CHECK(Cea608::stripParity(cc3.ccData[0].b1) == 0x14);
        CHECK(cc3.ccData[0].type == 1);

        Cea608Packet cc4 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC4);
        REQUIRE(cc4.ccData.size() == 1);
        CHECK(Cea608::stripParity(cc4.ccData[0].b1) == 0x1C);
        CHECK(cc4.ccData[0].type == 1);
}

TEST_CASE("Cea608Packet::fromCdp passes character pairs through unfiltered") {
        // Character pairs (b1 in 0x20..0x7F) carry no channel info on
        // the wire — fromCdp passes them through to every requested
        // channel.  Real channel separation requires per-channel
        // context tracking via Cea608Decoder.
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        cdp.ccDataPresent = true;
        cdp.captionServiceActive = true;
        // Single character pair (cc_type=0).
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0,
                Cea608::withOddParity('A'), Cea608::withOddParity('B')});

        Cea608Packet cc1 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC1);
        Cea608Packet cc2 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC2);
        REQUIRE(cc1.ccData.size() == 1);
        REQUIRE(cc2.ccData.size() == 1);
        CHECK(Cea608::stripParity(cc1.ccData[0].b1) == 'A');
        CHECK(Cea608::stripParity(cc2.ccData[0].b1) == 'A');
}

TEST_CASE("Cea608Packet::fromCdp returns an empty packet when no triples match the channel") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        // CC1 RCL only.
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0,
                Cea608::withOddParity(0x14), Cea608::withOddParity(0x20)});

        Cea608Packet cc4 = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC4);
        CHECK(cc4.channel == Cea608Packet::Channel::CC4);
        CHECK(cc4.ccData.size() == 0);
}

// ============================================================================
// toCdp — wraps ccData into a fresh Cea708Cdp
// ============================================================================

TEST_CASE("Cea608Packet::toCdp wraps the channel's triples into a CDP") {
        Cea608Packet pkt(Cea608Packet::Channel::CC1, [] {
                Cea708Cdp::CcDataList list;
                list.pushToBack(Cea708Cdp::CcData{true, 0,
                        Cea608::withOddParity('X'), Cea608::withOddParity('Y')});
                return list;
        }());

        Cea708Cdp cdp = pkt.toCdp(/*frameRateCode*/ 5, /*sequenceCounter*/ 42);
        CHECK(cdp.frameRateCode == 5);
        CHECK(cdp.sequenceCounter == 42);
        CHECK(cdp.ccDataPresent);
        REQUIRE(cdp.ccData.size() == 1);
        CHECK(Cea608::stripParity(cdp.ccData[0].b1) == 'X');
}

TEST_CASE("Cea608Packet round-trips through fromCdp/toCdp via the same triples") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        cdp.ccDataPresent = true;
        cdp.captionServiceActive = true;
        cdp.ccData.pushToBack(Cea708Cdp::CcData{true, 0,
                Cea608::withOddParity(0x14), Cea608::withOddParity(0x20)});

        Cea608Packet pkt = Cea608Packet::fromCdp(cdp, Cea608Packet::Channel::CC1);
        Cea708Cdp    rebuilt = pkt.toCdp(cdp.frameRateCode, cdp.sequenceCounter);
        CHECK(rebuilt.ccData == cdp.ccData);
        CHECK(rebuilt.frameRateCode == cdp.frameRateCode);
}

// ============================================================================
// Variant + DataStream + JSON integration
// ============================================================================

TEST_CASE("Cea608Packet: Variant payload type round-trips") {
        Cea608Packet pkt(Cea608Packet::Channel::CC2, [] {
                Cea708Cdp::CcDataList list;
                list.pushToBack(Cea708Cdp::CcData{true, 0, 0x9C, 0xA0});
                return list;
        }());

        Variant v(pkt);
        CHECK(v.type() == DataTypeCea608);
        const Cea608Packet &got = v.get<Cea608Packet>();
        CHECK(got == pkt);
}

TEST_CASE("Cea608Packet: DataStream operators round-trip the value") {
        Cea608Packet pkt(Cea608Packet::Channel::CC3, [] {
                Cea708Cdp::CcDataList list;
                list.pushToBack(Cea708Cdp::CcData{true, 1, 0x80, 0x80});
                list.pushToBack(Cea708Cdp::CcData{false, 1, 0xC1, 0xC2});
                return list;
        }());

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream ws = DataStream::createWriter(&dev);
        ws << pkt;
        REQUIRE(ws.status() == DataStream::Ok);

        dev.seek(0);
        DataStream   rs = DataStream::createReader(&dev);
        Cea608Packet got;
        rs >> got;
        REQUIRE(rs.status() == DataStream::Ok);
        CHECK(got == pkt);
}

TEST_CASE("Cea608Packet::toJson surfaces channel + cc_data triples") {
        Cea608Packet pkt(Cea608Packet::Channel::CC2, [] {
                Cea708Cdp::CcDataList list;
                list.pushToBack(Cea708Cdp::CcData{true, 0, 0x9C, 0xA0});
                return list;
        }());
        JsonObject obj = pkt.toJson();
        CHECK(obj.getString("channel") == "CC2");
        const JsonArray arr = obj.getArray("ccData");
        REQUIRE(arr.size() == 1);
        const JsonObject row = arr.getObject(0);
        CHECK(row.getBool("valid") == true);
        CHECK(row.getInt("type") == 0);
        CHECK(row.getInt("b1") == 0x9C);
        CHECK(row.getInt("b2") == 0xA0);
}
