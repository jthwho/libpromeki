/**
 * @file      st436m.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/st436m.h>
#include <promeki/ancpacket.h>
#include <promeki/st291packet.h>
#include <promeki/ancformat.h>
#include <promeki/ancdesc.h>
#include <promeki/buffer.h>

using namespace promeki;

namespace {

        // Builds an ST 291 ANC packet with the given DID/SDID, user-data
        // bytes, line, and F-bit, returning the canonical AncPacket.
        AncPacket makeAnc(uint8_t did, uint8_t sdid, const List<uint16_t> &udw, uint16_t line, bool fieldB = false) {
                St291Packet sp = St291Packet::buildRaw(did, sdid, udw, line, St291Packet::UnspecifiedHOffset, fieldB);
                return sp.packet();
        }

} // namespace

TEST_CASE("St436m: empty packet list round-trips to an empty value") {
        AncPacket::List empty;
        Buffer          sample = St436m::encodeFrame(empty, AncDesc());
        // Just the 2-byte "number of ANC packets" = 0.
        REQUIRE(sample.size() == 2);
        const uint8_t *p = static_cast<const uint8_t *>(sample.data());
        CHECK(p[0] == 0);
        CHECK(p[1] == 0);

        Result<AncPacket::List> dec = St436m::decodeFrame(sample);
        CHECK(dec.second() == Error::Ok);
        CHECK(dec.first().isEmpty());
}

TEST_CASE("St436m: single ST 291 packet round-trips DID/SDID/UDW/line") {
        List<uint16_t> udw;
        for (uint16_t v = 1; v <= 8; ++v) udw.pushToBack(v);
        AncPacket::List packets;
        packets.pushToBack(makeAnc(0x61, 0x01, udw, 9)); // CEA-708 CDP DID/SDID

        Buffer                  sample = St436m::encodeFrame(packets, AncDesc());
        Result<AncPacket::List> dec = St436m::decodeFrame(sample);
        REQUIRE(dec.second() == Error::Ok);
        REQUIRE(dec.first().size() == 1);

        const AncPacket &out = dec.first()[0];
        CHECK(out.transport() == AncTransport::St291);
        CHECK(out.st291Line() == 9);

        Result<St291Packet> sp = St291Packet::from(out);
        REQUIRE(sp.second() == Error::Ok);
        CHECK(sp.first().did() == 0x61);
        CHECK(sp.first().sdid() == 0x01);
        List<uint16_t> outUdw = sp.first().udw();
        REQUIRE(outUdw.size() == udw.size());
        for (size_t i = 0; i < udw.size(); ++i) CHECK(outUdw[i] == udw[i]);
}

TEST_CASE("St436m: 10-bit pass-through preserves the wire bytes byte-exact") {
        List<uint16_t> udw;
        for (uint16_t v = 0; v < 12; ++v) udw.pushToBack(static_cast<uint16_t>(v * 17 + 1));
        AncPacket orig = makeAnc(0x41, 0x05, udw, 12); // VPID-ish DID

        AncPacket::List packets;
        packets.pushToBack(orig);

        Buffer                  sample = St436m::encodeFrame(packets, AncDesc());
        Result<AncPacket::List> dec = St436m::decodeFrame(sample);
        REQUIRE(dec.second() == Error::Ok);
        REQUIRE(dec.first().size() == 1);

        // The encoder uses 10-bit pass-through, so the canonical post-ADF
        // 10-bit word stream (incl. parity + checksum) must survive exactly.
        const Buffer &a = orig.data();
        const Buffer &b = dec.first()[0].data();
        REQUIRE(a.size() == b.size());
        const uint8_t *pa = static_cast<const uint8_t *>(a.data());
        const uint8_t *pb = static_cast<const uint8_t *>(b.data());
        for (size_t i = 0; i < a.size(); ++i) REQUIRE(pa[i] == pb[i]);
}

TEST_CASE("St436m: multiple packets and the F-bit round-trip") {
        List<uint16_t> udw1, udw2;
        for (uint16_t v = 0; v < 4; ++v) udw1.pushToBack(static_cast<uint16_t>(0x10 + v));
        for (uint16_t v = 0; v < 6; ++v) udw2.pushToBack(static_cast<uint16_t>(0x20 + v));

        AncPacket::List packets;
        packets.pushToBack(makeAnc(0x61, 0x01, udw1, 9, /*fieldB=*/false));
        packets.pushToBack(makeAnc(0x60, 0x60, udw2, 10, /*fieldB=*/true));

        Buffer                  sample = St436m::encodeFrame(packets, AncDesc());
        Result<AncPacket::List> dec = St436m::decodeFrame(sample);
        REQUIRE(dec.second() == Error::Ok);
        REQUIRE(dec.first().size() == 2);

        CHECK(dec.first()[0].st291FieldB() == false);
        CHECK(dec.first()[0].st291Line() == 9);
        CHECK(dec.first()[1].st291FieldB() == true);
        CHECK(dec.first()[1].st291Line() == 10);
}

TEST_CASE("St436m: truncated value reports an error") {
        // Claims one packet but provides no packet body.
        Buffer   bad(2);
        uint8_t *p = static_cast<uint8_t *>(bad.data());
        p[0] = 0;
        p[1] = 1; // count = 1
        bad.setSize(2);

        Result<AncPacket::List> dec = St436m::decodeFrame(bad);
        CHECK(dec.second().isError());
}
