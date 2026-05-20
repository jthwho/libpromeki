/**
 * @file      anc_roundtrip_property.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

// Property-based round-trip coverage for the fundamental ANC stack:
// AncPacket / AncFormat / framing meta, St291Packet build / from, and
// RtpPayloadAnc pack / unpack.  Each SUBCASE drives a deterministic
// Random sequence so failures are reproducible; the per-iteration
// body asserts that wire bytes, framing fields, and parsed values
// survive the full layer trip byte-for-byte.

#include <doctest/doctest.h>

#include <cstring>

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/list.h>
#include <promeki/random.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/st291packet.h>

using namespace promeki;

namespace {

// Number of random iterations per SUBCASE.  Picked to keep total
// runtime well under a second while still exploring the input space
// across DC modulos, UDW values, sentinel lines, and packet counts.
constexpr int kIterationsPerCase = 200;

// Returns a Type-2 DID drawn uniformly from the non-reserved ranges
// per ST 291-1 §6.1 Figure 4b.  Permitted values: 0x04, 0x08, 0x0C,
// 0x10..0x7F.
uint8_t randomType2Did(Random &rng) {
        const int choice = rng.randomInt(0, 2);  // 0..2 selects the small valid prefix
        if (choice == 0) return 0x04;
        if (choice == 1) return 0x08;
        if (choice == 2) return 0x0C;
        return static_cast<uint8_t>(rng.randomInt(0x10, 0x7F));
}

// Returns a Type-1 DID drawn uniformly from the non-reserved ranges
// per ST 291-1 §6.1 Figure 4a.  Permitted values: 0x80,
// 0xA0..0xFF (0x81..0x9F is reserved).
uint8_t randomType1Did(Random &rng) {
        const int choice = rng.randomInt(0, 1);
        if (choice == 0) return 0x80;
        return static_cast<uint8_t>(rng.randomInt(0xA0, 0xFF));
}

// Returns an SDID in 0x01..0xFF (Type-2 packets reserve 0x00 per §6.2).
uint8_t randomSdid(Random &rng) {
        return static_cast<uint8_t>(rng.randomInt(0x01, 0xFF));
}

// Returns a UDW list of @p count entries, each a random 8-bit data
// byte the @c build / @c buildRaw path will parity-encode automatically.
List<uint16_t> randomUdw(Random &rng, size_t count) {
        List<uint16_t> udw;
        udw.reserve(count);
        for (size_t i = 0; i < count; ++i) {
                udw.pushToBack(static_cast<uint16_t>(rng.randomInt(0x00, 0xFF)));
        }
        return udw;
}

// Compares two @c Buffer instances byte-for-byte.
bool buffersEqual(const Buffer &a, const Buffer &b) {
        if (a.size() != b.size()) return false;
        if (a.size() == 0) return true;
        return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

// Builds one random Type-2 St291Packet with caller-supplied framing.
St291Packet buildRandomType2(Random &rng, uint16_t line, uint16_t hOffset,
                             bool fieldB, bool cBit, uint8_t streamNum,
                             size_t dc) {
        const uint8_t  did = randomType2Did(rng);
        const uint8_t  sdid = randomSdid(rng);
        List<uint16_t> udw = randomUdw(rng, dc);
        return St291Packet::buildRaw(did, sdid, udw, line, hOffset, fieldB, cBit, streamNum);
}

} // namespace

// ============================================================================
// Phase B — St291Packet build / from round-trip (byte-exact replay)
// ============================================================================

TEST_CASE("ANC Phase B property — Type-2 St291Packet wire byte-exactness") {
        // Drives a deterministic random walk through (DID, SDID, line,
        // hOffset, fieldB, cBit, streamNum, DC ∈ 1..255, UDW...).
        // Builds, promotes via from() under StrictValidate (proves the
        // checksum the builder emits matches what computedChecksum()
        // says), and asserts the round-trip preserves every observable
        // wire datum.
        Random rng(0xA17C2D58u);
        for (int iter = 0; iter < kIterationsPerCase; ++iter) {
                INFO("iter=", iter);
                const uint8_t  did = randomType2Did(rng);
                const uint8_t  sdid = randomSdid(rng);
                // DC sweeps the full 1..255 range; each DC value
                // exercises a different 10-bit packing phase
                // (4 words = 40 bits = 5 bytes, so modulos 0..3 of
                // (DC+3) recur).
                const size_t   dc = static_cast<size_t>(rng.randomInt(1, 255));
                List<uint16_t> udw = randomUdw(rng, dc);

                const uint16_t line = static_cast<uint16_t>(rng.randomInt(0, 0x7FF));
                const uint16_t hOffset = static_cast<uint16_t>(rng.randomInt(0, 0xFFF));
                const bool     fieldB = rng.randomBool();
                const bool     cBit = rng.randomBool();
                const uint8_t  streamNum = static_cast<uint8_t>(rng.randomInt(0, 0x7F));

                St291Packet src = St291Packet::buildRaw(did, sdid, udw, line, hOffset,
                                                        fieldB, cBit, streamNum);
                REQUIRE(src.isValid());
                CHECK(src.did() == did);
                CHECK(src.sdid() == sdid);
                CHECK(src.dataCount() == dc);
                CHECK(src.checksumValid());

                // StrictValidate proves the builder emitted a §6.4-correct
                // Checksum_Word — a property-test assertion that the
                // build path is bit-exact end to end.
                Result<St291Packet> r =
                        St291Packet::from(src.packet(), AncChecksumPolicy::StrictValidate);
                REQUIRE(isOk(r));
                const St291Packet &got = value(r);

                CHECK(got.did() == src.did());
                CHECK(got.sdid() == src.sdid());
                CHECK(got.dbn() == src.dbn());
                CHECK(got.dataCount() == src.dataCount());
                CHECK(got.checksum() == src.checksum());
                CHECK(got.checksumValid());
                CHECK(got.line() == src.line());
                CHECK(got.hOffset() == src.hOffset());
                CHECK(got.fieldB() == src.fieldB());
                CHECK(got.cBit() == src.cBit());
                CHECK(got.streamNum() == src.streamNum());

                // The contract: the same Buffer rides through promotion
                // (Result<St291Packet> stores the same AncPacket).
                CHECK(buffersEqual(got.packet().data(), src.packet().data()));

                // UDW bytes survive parity-strip; udwRaw preserves the
                // builder's canonical parity encoding.
                List<uint16_t> stripped = got.udw();
                REQUIRE(stripped.size() == dc);
                for (size_t i = 0; i < dc; ++i) {
                        CHECK(stripped.at(i) == (udw.at(i) & 0xFFu));
                }
        }
}

TEST_CASE("ANC Phase B property — Type-1 St291Packet wire byte-exactness") {
        // Type-1 packets carry a DBN in word 1; this exercises the
        // alternate buildRawType1 path and verifies that sdid() reports
        // the §3.1 sentinel 0x00 while dbn() returns the actual byte.
        Random rng(0x1B0C42A9u);
        for (int iter = 0; iter < kIterationsPerCase; ++iter) {
                INFO("iter=", iter);
                const uint8_t  did = randomType1Did(rng);
                const uint8_t  dbn = static_cast<uint8_t>(rng.randomInt(0x00, 0xFF));
                const size_t   dc = static_cast<size_t>(rng.randomInt(1, 64));
                List<uint16_t> udw = randomUdw(rng, dc);
                const uint16_t line = static_cast<uint16_t>(rng.randomInt(0, 0x7FF));

                St291Packet src = St291Packet::buildRawType1(did, dbn, udw, line);
                REQUIRE(src.isValid());
                CHECK(src.isType1());
                CHECK(src.did() == did);
                CHECK(src.sdid() == 0x00);
                CHECK(src.dbn() == dbn);
                CHECK(src.dataCount() == dc);
                CHECK(src.checksumValid());

                Result<St291Packet> r =
                        St291Packet::from(src.packet(), AncChecksumPolicy::StrictValidate);
                REQUIRE(isOk(r));
                const St291Packet &got = value(r);

                CHECK(got.isType1());
                CHECK(got.did() == did);
                CHECK(got.sdid() == 0x00);
                CHECK(got.dbn() == dbn);
                CHECK(got.dataCount() == dc);
                CHECK(got.checksum() == src.checksum());
                CHECK(got.line() == line);
                CHECK(buffersEqual(got.packet().data(), src.packet().data()));
        }
}

TEST_CASE("ANC Phase B property — Sentinel line/hOffset survive round-trip") {
        // RFC 8331 §2.2 sentinel values for Line_Number and
        // Horizontal_Offset must ride through the meta sidecar
        // unchanged.  The library treats UnspecifiedLine (0x7FE) and
        // UnspecifiedHOffset (0xFFF) as defaults; the other sentinels
        // (LineNoSpecific 0x7FF, LineLargerThan11Bits 0x7FD,
        // HOffsetInHanc 0xFFE, HOffsetInActiveVideo 0xFFD,
        // HOffsetLargerThan12Bits 0xFFC) must also survive.
        const uint16_t lineSentinels[] = {
                St291Packet::LineNoSpecific,
                St291Packet::LineSwitchingDefault,
                St291Packet::LineLargerThan11Bits,
        };
        const uint16_t hOffsetSentinels[] = {
                St291Packet::UnspecifiedHOffset,
                St291Packet::HOffsetInHanc,
                St291Packet::HOffsetInActiveVideo,
                St291Packet::HOffsetLargerThan12Bits,
        };
        Random rng(0x5E11A150u);
        for (uint16_t l : lineSentinels) {
                for (uint16_t h : hOffsetSentinels) {
                        INFO("line=0x", String::number(l, 16),
                             " hOffset=0x", String::number(h, 16));
                        List<uint16_t> udw = randomUdw(rng, 4);
                        St291Packet    src = St291Packet::buildRaw(0x41, 0x05, udw, l, h);
                        REQUIRE(src.isValid());
                        Result<St291Packet> r = St291Packet::from(src.packet());
                        REQUIRE(isOk(r));
                        CHECK(value(r).line() == l);
                        CHECK(value(r).hOffset() == h);
                }
        }
}

// ============================================================================
// Phase C — RtpPayloadAnc pack / unpack round-trip (RFC 8331)
// ============================================================================

TEST_CASE("ANC Phase C property — single-packet RTP round-trip is byte-exact") {
        // The simplest C-layer property: one ST 291 packet packs into
        // one RTP datagram and unpacks back to an identical AncPacket.
        // Byte-exact wire data and meta preserved.  Random DC, random
        // UDW values, random framing.
        Random        rng(0xF3CB7E81u);
        RtpPayloadAnc payload;
        for (int iter = 0; iter < kIterationsPerCase; ++iter) {
                INFO("iter=", iter);
                const size_t   dc = static_cast<size_t>(rng.randomInt(1, 64));
                const uint16_t line = static_cast<uint16_t>(rng.randomInt(9, 1000));
                const uint16_t hOffset = static_cast<uint16_t>(rng.randomInt(0, 0xFFB));
                const bool     fieldB = rng.randomBool();
                const bool     cBit = rng.randomBool();
                const uint8_t  streamNum = static_cast<uint8_t>(rng.randomInt(0, 0x7F));

                St291Packet src = buildRandomType2(rng, line, hOffset, fieldB, cBit, streamNum, dc);
                REQUIRE(src.isValid());

                AncPacket::List in;
                in.pushToBack(src.packet());

                RtpPacket::List rtp = payload.packAncFrame(in, 0xDEADBEEFu);
                REQUIRE(rtp.size() == 1u);
                CHECK(rtp[0].marker());
                CHECK(rtp[0].timestamp() == 0xDEADBEEFu);

                AncPacket::List out;
                Error           err = payload.unpackAncPackets(rtp, out);
                REQUIRE(!err.isError());
                REQUIRE(out.size() == 1u);

                // Byte-exact wire payload.
                CHECK(buffersEqual(out[0].data(), src.packet().data()));

                // Framing fields preserved.
                CHECK(out[0].st291Line() == line);
                CHECK(out[0].st291HOffset() == hOffset);
                CHECK(out[0].st291FieldB() == fieldB);
                CHECK(out[0].st291CBit() == cBit);
                CHECK(out[0].st291StreamNum() == streamNum);

                // Promote and verify the checksum still validates.
                Result<St291Packet> rp = St291Packet::from(out[0]);
                REQUIRE(isOk(rp));
                CHECK(value(rp).checksumValid());
        }
}

TEST_CASE("ANC Phase C property — multi-packet RTP round-trip preserves wire bytes") {
        // Multi-packet frames exercise the RFC 8331 §2.2 record
        // sequencing (header word + ANC bytes per record), the
        // ST 2110-40 §5.2.2 §C11 (line, hOffset)-stable sort the packer
        // imposes, and the C2 keep-alive bypass (a non-empty input
        // never emits a keep-alive).  We compare the output as a
        // multiset of (line, hOffset, wire bytes) tuples so that any
        // ordering chosen by the packer is accepted — the contract is
        // that no packet is lost and no wire bytes are corrupted.
        Random        rng(0x0C7E2A4Fu);
        RtpPayloadAnc payload;
        for (int iter = 0; iter < 80; ++iter) {
                INFO("iter=", iter);
                // 2..8 packets per frame, each fieldB the same so the
                // F-bit infers a single per-frame value and round-trips
                // unambiguously (C12 documents the per-record asymmetry
                // when fieldB is mixed across a datagram).
                const size_t    n = static_cast<size_t>(rng.randomInt(2, 8));
                const bool      fieldB = rng.randomBool();
                AncPacket::List in;
                in.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                        const size_t   dc = static_cast<size_t>(rng.randomInt(1, 16));
                        const uint16_t line = static_cast<uint16_t>(rng.randomInt(9, 800));
                        const uint16_t hOffset = static_cast<uint16_t>(rng.randomInt(0, 0xFFB));
                        const bool     cBit = rng.randomBool();
                        const uint8_t  streamNum = static_cast<uint8_t>(rng.randomInt(0, 0x7F));
                        St291Packet    p = buildRandomType2(rng, line, hOffset, fieldB, cBit, streamNum, dc);
                        REQUIRE(p.isValid());
                        in.pushToBack(p.packet());
                }

                RtpPacket::List rtp = payload.packAncFrame(in, 0x12345u + iter);
                REQUIRE(!rtp.isEmpty());
                // Only the last RTP packet has the marker bit set.
                for (size_t i = 0; i + 1 < rtp.size(); ++i) CHECK_FALSE(rtp[i].marker());
                CHECK(rtp[rtp.size() - 1].marker());

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == n);

                // For every input packet, find a matching output packet
                // with identical (line, hOffset, fieldB, cBit, streamNum,
                // wire bytes).  Mark used output slots so duplicates do
                // not double-match.
                // Use uint8_t (not bool) — List<bool> rides std::vector<bool>'s
                // proxy reference, which conflicts with the lvalue-reference
                // return of List::at().
                List<uint8_t> matched;
                matched.resize(n, 0u);
                for (size_t i = 0; i < n; ++i) {
                        const AncPacket &want = in[i];
                        bool             found = false;
                        for (size_t j = 0; j < n; ++j) {
                                if (matched.at(j)) continue;
                                const AncPacket &cand = out[j];
                                if (cand.st291Line() != want.st291Line()) continue;
                                if (cand.st291HOffset() != want.st291HOffset()) continue;
                                if (cand.st291FieldB() != want.st291FieldB()) continue;
                                if (cand.st291CBit() != want.st291CBit()) continue;
                                if (cand.st291StreamNum() != want.st291StreamNum()) continue;
                                if (!buffersEqual(cand.data(), want.data())) continue;
                                matched.at(j) = 1u;
                                found = true;
                                break;
                        }
                        CHECK(found);
                }
        }
}

TEST_CASE("ANC Phase C property — Type-1 packets ride RFC 8331 byte-exactly") {
        // Type-1 packets are uncommon on RFC 8331 but must round-trip
        // because the §2.2 record carries the wire bytes opaquely (the
        // DBN lives inside the AncPacket::data buffer just like an SDID
        // would).  Build batches mixing Type-1 and Type-2 to make sure
        // neither shape mishandles the other on pack / unpack.
        Random        rng(0x9D33F022u);
        RtpPayloadAnc payload;
        for (int iter = 0; iter < 60; ++iter) {
                INFO("iter=", iter);
                AncPacket::List in;
                const size_t    n = static_cast<size_t>(rng.randomInt(2, 6));
                const bool      fieldB = rng.randomBool();
                for (size_t i = 0; i < n; ++i) {
                        const size_t   dc = static_cast<size_t>(rng.randomInt(1, 16));
                        const uint16_t line = static_cast<uint16_t>(rng.randomInt(9, 600));
                        const uint16_t hOffset = static_cast<uint16_t>(rng.randomInt(0, 0xFFB));
                        const bool     cBit = rng.randomBool();
                        const uint8_t  streamNum = static_cast<uint8_t>(rng.randomInt(0, 0x7F));
                        List<uint16_t> udw = randomUdw(rng, dc);
                        St291Packet    p;
                        if (rng.randomBool()) {
                                // Type-1
                                const uint8_t did = randomType1Did(rng);
                                const uint8_t dbn = static_cast<uint8_t>(rng.randomInt(0, 0xFF));
                                p = St291Packet::buildRawType1(did, dbn, udw, line,
                                                               hOffset, fieldB, cBit, streamNum);
                        } else {
                                // Type-2
                                const uint8_t did = randomType2Did(rng);
                                const uint8_t sdid = randomSdid(rng);
                                p = St291Packet::buildRaw(did, sdid, udw, line,
                                                          hOffset, fieldB, cBit, streamNum);
                        }
                        REQUIRE(p.isValid());
                        in.pushToBack(p.packet());
                }

                RtpPacket::List rtp = payload.packAncFrame(in, 0x77000u + iter);
                REQUIRE(!rtp.isEmpty());

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == n);

                // Same multiset comparison as the multi-packet case.
                // Use uint8_t (not bool) — List<bool> rides std::vector<bool>'s
                // proxy reference, which conflicts with the lvalue-reference
                // return of List::at().
                List<uint8_t> matched;
                matched.resize(n, 0u);
                for (size_t i = 0; i < n; ++i) {
                        bool found = false;
                        for (size_t j = 0; j < n; ++j) {
                                if (matched.at(j)) continue;
                                if (out[j].st291Line() != in[i].st291Line()) continue;
                                if (out[j].st291HOffset() != in[i].st291HOffset()) continue;
                                if (!buffersEqual(out[j].data(), in[i].data())) continue;
                                matched.at(j) = 1u;
                                found = true;
                                break;
                        }
                        CHECK(found);
                }

                // Every output packet still promotes cleanly and reports
                // a valid checksum (B2 / RFC 8331 §7 happy path).
                for (size_t i = 0; i < out.size(); ++i) {
                        Result<St291Packet> rp = St291Packet::from(out[i]);
                        REQUIRE(isOk(rp));
                        CHECK(value(rp).checksumValid());
                }
        }
}

TEST_CASE("ANC Phase C property — MTU-split frames preserve every packet byte-exactly") {
        // Drives the packer with a payload limit small enough to force
        // multiple RTP packets per frame (per ST 2110-40 §6.2 the
        // packer fragments at the payload limit).  Verifies that
        // dropping the MTU does not corrupt or lose any packet's wire
        // bytes — RFC 8331 §2.2 record sequencing is byte-exact across
        // the split boundary.
        Random        rng(0x33A7B910u);
        RtpPayloadAnc payload;
        payload.setMaxPayloadSize(96);  // small MTU forces splits
        for (int iter = 0; iter < 40; ++iter) {
                INFO("iter=", iter);
                const size_t    n = static_cast<size_t>(rng.randomInt(4, 10));
                const bool      fieldB = rng.randomBool();
                AncPacket::List in;
                for (size_t i = 0; i < n; ++i) {
                        const size_t dc = static_cast<size_t>(rng.randomInt(4, 24));
                        // Use distinct lines so the multiset comparison
                        // can match each output unambiguously.
                        const uint16_t line = static_cast<uint16_t>(10 + i + iter);
                        St291Packet    p = buildRandomType2(rng, line,
                                                            St291Packet::UnspecifiedHOffset,
                                                            fieldB, /*cBit*/ false,
                                                            /*streamNum*/ 0, dc);
                        REQUIRE(p.isValid());
                        in.pushToBack(p.packet());
                }

                RtpPacket::List rtp = payload.packAncFrame(in, 0x90000u + iter);
                REQUIRE(!rtp.isEmpty());
                // Only the final RTP packet carries the marker bit.
                CHECK(rtp[rtp.size() - 1].marker());

                AncPacket::List out;
                CHECK(!payload.unpackAncPackets(rtp, out).isError());
                REQUIRE(out.size() == n);

                // Line-keyed match (each input has a unique line).
                for (size_t i = 0; i < n; ++i) {
                        bool found = false;
                        for (size_t j = 0; j < n; ++j) {
                                if (out[j].st291Line() != in[i].st291Line()) continue;
                                CHECK(buffersEqual(out[j].data(), in[i].data()));
                                CHECK(out[j].st291FieldB() == fieldB);
                                found = true;
                                break;
                        }
                        CHECK(found);
                }
        }
}

// ============================================================================
// Phase A — AncPacket / AncFormat fundamental contract (categorical, not
// numeric — the "no surprises across the layer boundary" property)
// ============================================================================

TEST_CASE("ANC Phase A property — wire-byte buffer is the AncPacket identity") {
        // Phase A's central contract (per docs/anc.md and ancdata.md):
        // the AncPacket's data() buffer holds the raw ST 291 packet
        // bytes starting at DID (ADF stripped), and that buffer is the
        // wire-stable identity.  Verifies the contract by mutating
        // AncPacket meta fields after construction and confirming the
        // wire buffer does not change (meta-only mutations preserve
        // the wire), and by mutating the UDW list and confirming the
        // wire buffer does change (DC / CS recomputed).
        Random rng(0x2F4C8E55u);
        for (int iter = 0; iter < kIterationsPerCase; ++iter) {
                INFO("iter=", iter);
                const size_t   dc = static_cast<size_t>(rng.randomInt(1, 32));
                List<uint16_t> udw = randomUdw(rng, dc);
                St291Packet    src = St291Packet::buildRaw(0x41, 0x05, udw, /*line*/ 9);
                REQUIRE(src.isValid());

                // Snapshot the original wire bytes.
                const Buffer originalWire = src.packet().data();

                // Meta-only mutations: setLine / setHOffset / setFieldB
                // / setCBit / setStreamNum must NOT alter wire bytes.
                src.setLine(static_cast<uint16_t>(rng.randomInt(0, 0x7FF)));
                src.setHOffset(static_cast<uint16_t>(rng.randomInt(0, 0xFFF)));
                src.setFieldB(rng.randomBool());
                src.setCBit(rng.randomBool());
                src.setStreamNum(static_cast<uint8_t>(rng.randomInt(0, 0x7F)));
                CHECK(buffersEqual(src.packet().data(), originalWire));

                // UDW mutation: wire bytes change, DC updates, checksum
                // recomputes.
                List<uint16_t> udw2 = randomUdw(rng, dc + 1);
                src.setUdw(udw2);
                CHECK(src.dataCount() == dc + 1);
                CHECK(src.checksumValid());
                CHECK_FALSE(buffersEqual(src.packet().data(), originalWire));
        }
}
