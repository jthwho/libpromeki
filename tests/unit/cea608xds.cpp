/**
 * @file      cea608xds.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <ctime>
#include <doctest/doctest.h>
#include <promeki/cea608.h>
#include <promeki/cea608xds.h>
#include <promeki/datetime.h>
#include <promeki/list.h>

using namespace promeki;

namespace {

        /// @brief Returns a fully-formed XDS Program Name packet (class
        ///        Current, type 0x03) with the given title.
        List<uint8_t> programNamePacket(const char *title) {
                List<uint8_t> p;
                p.pushToBack(0x01); // Start, Current Class
                p.pushToBack(0x03); // Type: Program Name
                uint32_t      sum = 0x01 + 0x03;
                const size_t  len = std::char_traits<char>::length(title);
                // Pad to even length per §9.2 ("there always be an even
                // number of informational characters").
                for (size_t i = 0; i < len; ++i) {
                        const uint8_t b = static_cast<uint8_t>(title[i]);
                        p.pushToBack(b);
                        sum += b;
                }
                if (len % 2 != 0) {
                        p.pushToBack(0x00);
                        sum += 0x00;
                }
                p.pushToBack(0x0F); // End
                sum += 0x0F;
                p.pushToBack(static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F));
                return p;
        }

        /// @brief Feeds a list of XDS bytes through the extractor as
        ///        (b1, b2) pairs (no parity).
        void feedPairs(Cea608XdsExtractor &ext, const List<uint8_t> &bytes) {
                REQUIRE(bytes.size() % 2 == 0);
                for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
                        ext.processPair(bytes[i], bytes[i + 1]);
                }
        }

} // namespace

// ============================================================================
// Cea608XdsExtractor — packet framing + checksum
// ============================================================================

TEST_CASE("Cea608XdsExtractor: a complete Program Name packet round-trips") {
        Cea608XdsExtractor   ext;
        const List<uint8_t>  bytes = programNamePacket("HELLO");
        feedPairs(ext, bytes);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].class_ == Cea608XdsClass::Current);
        CHECK(packets[0].type == 0x03);
        CHECK(packets[0].programName() == "HELLO");
        CHECK(ext.checksumFailures() == 0);
}

TEST_CASE("Cea608XdsExtractor: corrupted checksum fails validation") {
        Cea608XdsExtractor  ext;
        List<uint8_t>        bytes = programNamePacket("ABC");
        // Flip the checksum byte by 1 — any bit change breaks the sum.
        bytes[bytes.size() - 1] = static_cast<uint8_t>((bytes[bytes.size() - 1] + 1) & 0x7F);
        feedPairs(ext, bytes);
        const auto packets = ext.drain();
        CHECK(packets.size() == 0);
        CHECK(ext.checksumFailures() == 1);
}

TEST_CASE("Cea608XdsExtractor: drain returns and clears the pending list") {
        Cea608XdsExtractor   ext;
        feedPairs(ext, programNamePacket("ONE"));
        feedPairs(ext, programNamePacket("TWO"));
        const auto packets = ext.drain();
        CHECK(packets.size() == 2);
        CHECK(packets[0].programName() == "ONE");
        CHECK(packets[1].programName() == "TWO");
        CHECK(ext.pending() == 0);
        // Second drain returns nothing.
        const auto second = ext.drain();
        CHECK(second.size() == 0);
}

TEST_CASE("Cea608XdsExtractor: §8.6.6 over-long packet is dropped + oversizedPackets counts it") {
        Cea608XdsExtractor ext;
        // Build a Current/Name packet whose payload exceeds the spec
        // cap of 32 informational bytes — push 34 'A's after the
        // Start/Type pair and observe the overrun guard fire.
        ext.processPair(0x01, 0x03);
        for (int i = 0; i + 1 < 34; i += 2) {
                ext.processPair('A', 'A');
        }
        // Past the cap now — the next info pair triggers the drop.
        ext.processPair('A', 'A');
        // No drained packet, but the over-long counter went up.
        CHECK(ext.drain().size() == 0);
        CHECK(ext.oversizedPackets() == 1);
}

TEST_CASE("Cea608XdsExtractor: §8.6.5 three-way interleave with three classes") {
        // Spec §8.6.5: at most one level of interleaving is
        // recommended.  The extractor tolerates more (MaxInFlight=4).
        // Build A=Current/Name, suspended by B=Channel/Name,
        // suspended by C=Misc/ToD — then End each in reverse order.
        Cea608XdsExtractor ext;
        // Start A.
        ext.processPair(0x01, 0x03);
        ext.processPair('A', 'A');
        // Suspend with Start B.
        ext.processPair(0x05, 0x01);
        ext.processPair('B', 'B');
        // Suspend with Start C — Time of Day, 6 binary bytes.
        ext.processPair(0x07, 0x01);
        ext.processPair(static_cast<uint8_t>(0x40 | 30),  // minute
                        static_cast<uint8_t>(0x40 | 14)); // hour
        ext.processPair(static_cast<uint8_t>(0x40 | 15),  // date
                        static_cast<uint8_t>(0x40 | 3));  // month
        ext.processPair(static_cast<uint8_t>(0x40 | 6),   // dow
                        static_cast<uint8_t>(0x40 | 34)); // year-1990
        // End C.
        {
                const uint32_t sum = 0x07 + 0x01 + (0x40 | 30) + (0x40 | 14)
                                    + (0x40 | 15) + (0x40 | 3) + (0x40 | 6)
                                    + (0x40 | 34) + 0x0F;
                ext.processPair(0x0F, static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F));
        }
        // Resume B with Continue.
        ext.processPair(0x06, 0x01);
        // End B.
        {
                const uint32_t sum = 0x05 + 0x01 + 'B' + 'B' + 0x0F;
                ext.processPair(0x0F, static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F));
        }
        // Resume A with Continue.
        ext.processPair(0x02, 0x03);
        // End A.
        {
                const uint32_t sum = 0x01 + 0x03 + 'A' + 'A' + 0x0F;
                ext.processPair(0x0F, static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F));
        }
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 3);
        // Order: C ended first, then B, then A.
        CHECK(packets[0].class_ == Cea608XdsClass::Misc);
        CHECK(packets[0].type == 0x01);
        CHECK(packets[1].class_ == Cea608XdsClass::Channel);
        CHECK(packets[1].programName().isEmpty()); // wrong type
        CHECK(packets[1].networkName() == "BB");
        CHECK(packets[2].class_ == Cea608XdsClass::Current);
        CHECK(packets[2].programName() == "AA");
}

TEST_CASE("Cea608XdsExtractor: §8.6.8 same-(class,type) Start aborts and restarts the packet") {
        Cea608XdsExtractor ext;
        // Start Current/Name with payload "XX".
        ext.processPair(0x01, 0x03);
        ext.processPair('X', 'X');
        // Another Start for the SAME (class, type) — restarts.  Per
        // §8.6.8 "A packet may be aborted or terminated by beginning
        // another packet of the same class and type."
        ext.processPair(0x01, 0x03);
        ext.processPair('O', 'K');
        // End — payload should be "OK" (the prior "XX" was discarded).
        const uint32_t sum = 0x01 + 0x03 + 'O' + 'K' + 0x0F;
        ext.processPair(0x0F, static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F));
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].programName() == "OK");
}

TEST_CASE("Cea608XdsExtractor: reset clears in-flight + drain state") {
        Cea608XdsExtractor ext;
        // Push a partial packet (Start but no End).
        ext.processPair(0x01, 0x03);
        ext.processPair(0x48, 0x49); // "HI"
        CHECK(ext.pending() == 0);
        ext.reset();
        // Pushing the End/Checksum from the prior packet should now do
        // nothing — the in-flight buffer was cleared.
        ext.processPair(0x0F, 0x00);
        const auto packets = ext.drain();
        CHECK(packets.size() == 0);
}

// ============================================================================
// Cea608XdsExtractor — Continue (resume) semantics
// ============================================================================

TEST_CASE("Cea608XdsExtractor: a packet split across Start + Continue assembles correctly") {
        // First sub-packet: Start "HI" — but no End.
        // Then a different packet's Start arrives (suspending us).
        // Then a Continue resumes the original.  End fires.
        // Per spec §8.6.3 Continue/Type bytes are NOT in the checksum.
        Cea608XdsExtractor ext;
        // Sub-packet A start: Start(0x01, Current) Type(0x03)
        ext.processPair(0x01, 0x03);
        // Two info bytes: "HI"
        ext.processPair(0x48, 0x49);
        // Suspend with sub-packet B's Start: Channel/Network Name.
        ext.processPair(0x05, 0x01);
        // One info byte pair for B: "NN" — followed by End.
        ext.processPair(0x4E, 0x4E);
        // End packet B.
        {
                // sum for B: 0x05 + 0x01 + 0x4E + 0x4E + 0x0F = 0xF5;
                // need (0x80 - (0xF5 & 0x7F)) & 0x7F = (0x80 - 0x75) = 0x0B.
                const uint8_t chk = static_cast<uint8_t>((0x80 - ((0x05 + 0x01 + 0x4E + 0x4E + 0x0F) & 0x7F)) & 0x7F);
                ext.processPair(0x0F, chk);
        }
        // Resume A with Continue: 0x02 (Continue Current) + 0x03 (Type).
        ext.processPair(0x02, 0x03);
        // More info bytes for A: ", X"  — three info chars need even
        // count, so pad with one info pair "X " then End.
        ext.processPair(0x20, 0x58); // " X" (space + X)
        // Sum for A: 0x01 + 0x03 + 0x48 + 0x49 + 0x20 + 0x58 + 0x0F = 0xFC.
        // Continue/Type pair (0x02, 0x03) are NOT in the sum per spec.
        const uint8_t chkA = static_cast<uint8_t>((0x80 - ((0x01 + 0x03 + 0x48 + 0x49 + 0x20 + 0x58 + 0x0F) & 0x7F)) & 0x7F);
        ext.processPair(0x0F, chkA);

        const auto packets = ext.drain();
        REQUIRE(packets.size() == 2);
        // Order: B finishes first (its End fired before A's resume + End).
        CHECK(packets[0].class_ == Cea608XdsClass::Channel);
        CHECK(packets[0].type == 0x01);
        CHECK(packets[0].networkName() == "NN");
        CHECK(packets[1].class_ == Cea608XdsClass::Current);
        CHECK(packets[1].type == 0x03);
        CHECK(packets[1].programName() == "HI X");
}

TEST_CASE("Cea608XdsExtractor: Continue without prior Start is ignored") {
        Cea608XdsExtractor ext;
        // Continue Current/Type 0x03 — no prior Start.
        ext.processPair(0x02, 0x03);
        ext.processPair(0x41, 0x42); // "AB" — should be ignored
        ext.processPair(0x0F, 0x00); // End with bogus checksum — ignored
        CHECK(ext.drain().size() == 0);
        // No checksum failure: we never had an in-flight buffer to validate.
        CHECK(ext.checksumFailures() == 0);
}

// ============================================================================
// Cea608XdsExtractor — pushFrame CDP integration
// ============================================================================

TEST_CASE("Cea608XdsExtractor::pushFrame filters to field-2 triples with valid parity") {
        Cea608XdsExtractor ext;
        const List<uint8_t> bytes = programNamePacket("X");
        // Build a CcDataList: field-2 (type=1) triples carrying the
        // parity-stamped XDS bytes, plus a field-1 (type=0) decoy that
        // should be filtered out.
        Cea708Cdp::CcDataList list;
        // Decoy field-1 triple — should NOT reach the extractor.
        {
                Cea708Cdp::CcData t;
                t.valid = true;
                t.type = 0; // field 1 (CC1/CC2 caption data)
                t.b1 = 0x14;
                t.b2 = 0x20;
                list.pushToBack(t);
        }
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
                Cea708Cdp::CcData t;
                t.valid = true;
                t.type = 1; // field 2
                t.b1 = Cea608::withOddParity(bytes[i]);
                t.b2 = Cea608::withOddParity(bytes[i + 1]);
                list.pushToBack(t);
        }
        ext.pushFrame(list);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].programName() == "X");
}

TEST_CASE("Cea608XdsExtractor::pushFrame skips field-2 caption / text bytes (0x10..0x1F)") {
        // Field 2 carries both XDS (b1 in 0x01..0x0F / 0x20..0x7F) and
        // CC3/CC4 caption / T3/T4 text (b1 in 0x10..0x1F).  pushFrame
        // must filter the caption / text range out.
        Cea608XdsExtractor    ext;
        Cea708Cdp::CcDataList list;
        // A field-2 caption byte pair (CC3 RCL: 0x14 0x20) — should be skipped.
        {
                Cea708Cdp::CcData t;
                t.valid = true;
                t.type = 1;
                t.b1 = Cea608::withOddParity(0x14);
                t.b2 = Cea608::withOddParity(0x20);
                list.pushToBack(t);
        }
        // Then a complete XDS Program Name packet "Z".
        const List<uint8_t> bytes = programNamePacket("Z");
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
                Cea708Cdp::CcData t;
                t.valid = true;
                t.type = 1;
                t.b1 = Cea608::withOddParity(bytes[i]);
                t.b2 = Cea608::withOddParity(bytes[i + 1]);
                list.pushToBack(t);
        }
        ext.pushFrame(list);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].programName() == "Z");
}

// ============================================================================
// Typed accessors — Time of Day, Call Letters, Content Advisory
// ============================================================================

TEST_CASE("Cea608XdsPacket: Misc/Time-of-Day decodes to a wall-clock DateTime") {
        // Build a Time-of-Day packet for 2024-03-15 14:30 UTC.  Per
        // §9.5.4.1 wire layout, each byte carries the field value with
        // bit 6 set (kept out of the XDS control-code range).
        //   minute = 30        → 0x40 | 30  = 0x5E
        //   hour   = 14        → 0x40 | 14  = 0x4E
        //   date   = 15        → 0x40 | 15  = 0x4F
        //   month  = 3         → 0x40 | 3   = 0x43
        //   dow    = 6 (Friday → enum 6)    = 0x46
        //   year   = 2024-1990 = 34         = 0x40 | 34 = 0x62
        Cea608XdsExtractor ext;
        const uint32_t sum =
                0x07 + 0x01 + 0x5E + 0x4E + 0x4F + 0x43 + 0x46 + 0x62 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x01); // Start Misc, Type ToD
        ext.processPair(0x5E, 0x4E); // minute, hour
        ext.processPair(0x4F, 0x43); // date, month
        ext.processPair(0x46, 0x62); // dow, year
        ext.processPair(0x0F, chk);  // End + checksum

        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto tod = packets[0].timeOfDay();
        REQUIRE(tod.hasValue());
        // Convert the DateTime back to a calendar time and inspect.
        const time_t epoch = tod.value().toTimeT();
        std::tm tm = {};
        gmtime_r(&epoch, &tm);
        CHECK(tm.tm_year == 2024 - 1900);
        CHECK(tm.tm_mon == 3 - 1);
        CHECK(tm.tm_mday == 15);
        CHECK(tm.tm_hour == 14);
        CHECK(tm.tm_min == 30);
        CHECK(tm.tm_sec == 0);
}

TEST_CASE("Cea608XdsPacket: Channel/Call Letters 6-byte form decodes native channel (§9.5.3.2)") {
        // 4 call-letter chars "WCBS" + 2 channel digits "02" → channel 2.
        Cea608XdsExtractor ext;
        const uint8_t b[6] = {'W', 'C', 'B', 'S', '0', '2'};
        const uint32_t sum = 0x05 + 0x02 + b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x05, 0x02);
        ext.processPair(b[0], b[1]);
        ext.processPair(b[2], b[3]);
        ext.processPair(b[4], b[5]);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].callLetters() == "WCBS");
        const auto ch = packets[0].nativeChannel();
        REQUIRE(ch.hasValue());
        CHECK(ch.value() == 2);
}

TEST_CASE("Cea608XdsPacket: Channel/Call Letters 6-byte form with NUL-prefixed single digit") {
        // 4 call-letter chars "KCBS" + NUL + "9" → channel 9 (single digit
        // preceded by NUL per §9.5.3.2).
        Cea608XdsExtractor ext;
        const uint8_t b[6] = {'K', 'C', 'B', 'S', 0x00, '9'};
        const uint32_t sum = 0x05 + 0x02 + b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x05, 0x02);
        ext.processPair(b[0], b[1]);
        ext.processPair(b[2], b[3]);
        ext.processPair(b[4], b[5]);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto ch = packets[0].nativeChannel();
        REQUIRE(ch.hasValue());
        CHECK(ch.value() == 9);
}

TEST_CASE("Cea608XdsPacket: nativeChannel is empty for 4-byte (no channel) form") {
        Cea608XdsExtractor ext;
        const uint32_t sum = 0x05 + 0x02 + 0x4B + 0x43 + 0x56 + 0x20 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x05, 0x02);
        ext.processPair(0x4B, 0x43);
        ext.processPair(0x56, 0x20);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK_FALSE(packets[0].nativeChannel().hasValue());
}

TEST_CASE("Cea608XdsPacket: Channel/Call Letters strips trailing space padding") {
        // 3-letter call sign "KCV" + space padding to the spec's 4-char
        // minimum.  Sum: 0x05 + 0x02 + 0x4B + 0x43 + 0x56 + 0x20 + 0x0F.
        Cea608XdsExtractor ext;
        const uint32_t sum = 0x05 + 0x02 + 0x4B + 0x43 + 0x56 + 0x20 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x05, 0x02); // Start Channel, Type Call Letters
        ext.processPair(0x4B, 0x43); // "KC"
        ext.processPair(0x56, 0x20); // "V "
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].callLetters() == "KCV");
}

TEST_CASE("Cea608XdsPacket: Content Advisory decodes US TV Parental Guideline TV-PG with V+L") {
        // US TV Parental Guidelines: (a1, a0) = (0, 1).  TV-PG is age
        // rating g2 g1 g0 = 1 0 0 = 4.  Set V (Violence, bit 5 of char2)
        // and L (Language, bit 3 of char2).  S and D are clear.
        //
        // Character 1: b6=1 b5=D b4=a1 b3=a0 b2..0=r2..r0
        //              D=0 a1=0 a0=1 r=000          → 0x48
        // Character 2: b6=1 b5=V b4=S b3=L b2..0=g2..g0
        //              V=1 S=0 L=1 g=100            → 0x6C
        Cea608XdsExtractor ext;
        const uint32_t sum = 0x01 + 0x05 + 0x48 + 0x6C + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05); // Start Current, Type Content Advisory
        ext.processPair(0x48, 0x6C);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().level == 4);    // TV-PG
        CHECK(adv.value().violence == true);
        CHECK(adv.value().sexual == false);
        CHECK(adv.value().language == true);
        CHECK(adv.value().dialog == false);
        CHECK(adv.value().fantasyViolence == false);
}

TEST_CASE("Cea608XdsPacket: Content Advisory decodes TV-Y7 with FV bit") {
        // TV-Y7: level 2 (g2 g1 g0 = 010).  V bit reinterpreted as FV
        // (Fantasy Violence) per §9.5.1.5.1 NOTE.
        Cea608XdsExtractor ext;
        // char1 = 0x48 (US TV, D=0, no MPAA picture rating)
        // char2 = 0x62 (V=1 (=FV), S=0, L=0, g=010)
        const uint32_t sum = 0x01 + 0x05 + 0x48 + 0x62 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(0x48, 0x62);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().level == 2);   // TV-Y7
        CHECK(adv.value().fantasyViolence == true);
        CHECK(adv.value().violence == false);
}

// ============================================================================
// Content Advisory — System selector (Table 19)
// ============================================================================

TEST_CASE("Cea608XdsPacket: Content Advisory decodes MPAA System 2 (backward compat)") {
        // (a1, a0) = (1, 0) → MPAA System 2 per Table 19 — same payload
        // as System 0.  Build an "R" rated packet (rating index 4).
        Cea608XdsExtractor ext;
        const uint8_t c1 = 0x40 | (1 << 4) | 0x04; // marker + a1=1, a0=0, rating=4
        const uint8_t c2 = 0x40;
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().system == Cea608XdsRatingSystem::Mpaa);
        CHECK(adv.value().mpaa == Cea608XdsMpaaRating::R);
}

TEST_CASE("Cea608XdsPacket: Content Advisory decodes Canadian English (a3=0, a2=0)") {
        // (a3, a2, a1, a0) = (0, 0, 1, 1) → CA English.  Level 4 = PG.
        Cea608XdsExtractor ext;
        // char1: a2 (b5)=0, a1 (b4)=1, a0 (b3)=1; r=000 → 0x58
        // char2: V/S=0, L=a3 (b3)=0, g=100 → 0x44
        const uint8_t c1 = 0x40 | (0 << 5) | (1 << 4) | (1 << 3);
        const uint8_t c2 = 0x40 | (0 << 3) | 0x04;
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().system == Cea608XdsRatingSystem::CanadianEnglish);
        CHECK(adv.value().level == 4);
}

TEST_CASE("Cea608XdsPacket: Content Advisory decodes Canadian French (a3=0, a2=1)") {
        // (a3, a2, a1, a0) = (0, 1, 1, 1) → CA French.  Level 5 = 18 ans +.
        Cea608XdsExtractor ext;
        const uint8_t c1 = 0x40 | (1 << 5) | (1 << 4) | (1 << 3); // a2=1, a1=1, a0=1
        const uint8_t c2 = 0x40 | (0 << 3) | 0x05;                 // a3=0, g=101
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().system == Cea608XdsRatingSystem::CanadianFrench);
        CHECK(adv.value().level == 5);
}

TEST_CASE("Cea608XdsPacket: Content Advisory rejects reserved (a3=1, a1=1, a0=1)") {
        // (a3, a2, a1, a0) = (1, 0, 1, 1) → Reserved per Table 19.
        Cea608XdsExtractor ext;
        const uint8_t c1 = 0x40 | (1 << 4) | (1 << 3); // a2=0, a1=1, a0=1
        const uint8_t c2 = 0x40 | (1 << 3);             // a3=1
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK_FALSE(packets[0].contentAdvisory().hasValue());
}

TEST_CASE("Cea608XdsPacket: Content Advisory rejects invalid Canadian-English level (1,1,1)") {
        // CA English with g=111 — invalid per Table 22.
        Cea608XdsExtractor ext;
        const uint8_t c1 = 0x40 | (0 << 5) | (1 << 4) | (1 << 3);
        const uint8_t c2 = 0x40 | (0 << 3) | 0x07;
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK_FALSE(packets[0].contentAdvisory().hasValue());
}

TEST_CASE("Cea608XdsContentAdvisory::ratingName returns symbolic names per Tables 20-23") {
        // MPAA PG-13 → "PG-13".
        Cea608XdsContentAdvisory mpaa;
        mpaa.system = Cea608XdsRatingSystem::Mpaa;
        mpaa.mpaa = Cea608XdsMpaaRating::Pg13;
        mpaa.level = 3;
        CHECK(mpaa.ratingName() == "PG-13");
        // US TV TV-PG (level 4).
        Cea608XdsContentAdvisory usTv;
        usTv.system = Cea608XdsRatingSystem::UsTvParental;
        usTv.level = 4;
        CHECK(usTv.ratingName() == "TV-PG");
        // CA English PG (level 4).
        Cea608XdsContentAdvisory canEn;
        canEn.system = Cea608XdsRatingSystem::CanadianEnglish;
        canEn.level = 4;
        CHECK(canEn.ratingName() == "PG");
        // CA French 18 ans + (level 5).
        Cea608XdsContentAdvisory canFr;
        canFr.system = Cea608XdsRatingSystem::CanadianFrench;
        canFr.level = 5;
        CHECK(canFr.ratingName() == "18 ans +");
}

TEST_CASE("Cea608XdsPacket: Content Advisory rejects invalid Canadian-French level (1,1,0)") {
        // CA French with g=110 — invalid per Table 23.
        Cea608XdsExtractor ext;
        const uint8_t c1 = 0x40 | (1 << 5) | (1 << 4) | (1 << 3);
        const uint8_t c2 = 0x40 | (0 << 3) | 0x06;
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK_FALSE(packets[0].contentAdvisory().hasValue());
}

// ============================================================================
// Content Advisory — MPAA
// ============================================================================

TEST_CASE("Cea608XdsPacket: Content Advisory decodes MPAA picture rating") {
        // MPAA selector: (a1, a0) = (0, 0).  char1 layout for MPAA per
        // §9.5.1.5 Table 19: bit 6 = 1 (marker), bits 0..2 = rating
        // index (Table 20).  Build a "PG-13" packet (rating 3).
        Cea608XdsExtractor ext;
        const uint8_t c1 = 0x40 | 0x03; // marker + rating=3 (PG-13), a1=a0=0
        const uint8_t c2 = 0x40;        // marker only
        const uint32_t sum = 0x01 + 0x05 + c1 + c2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x05);
        ext.processPair(c1, c2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().system == Cea608XdsRatingSystem::Mpaa);
        CHECK(adv.value().mpaa == Cea608XdsMpaaRating::Pg13);
        CHECK(adv.value().level == 3);
}

// ============================================================================
// Copy and Redistribution Control (CGMS-A)
// ============================================================================

TEST_CASE("Cea608XdsPacket: cgmsA decodes CGMS / APS / ASB / RCD from type 0x08") {
        // §9.5.1.8 Table 29: 2 bytes carry the CGMS-A packet.
        //   Byte 1: b6=1, b5=0, b4=CGMS hi, b3=CGMS lo,
        //           b2=APS hi, b1=APS lo, b0=ASB.
        //   Byte 2: b6=1, Re=0, b0=RCD.
        // Encode CGMS=NoMoreCopies (0,1), APS=Psp2LineSplitBurst (1,0),
        // ASB=1, RCD=1.  APS is meaningful only when CGMS restricts
        // copying (NoMoreCopies or CopyNever) per §9.5.1.8 — that's
        // satisfied here, so APS is surfaced.
        const uint8_t b1 = 0x40 | (0x01 << 3) | (0x02 << 1) | 0x01;
        const uint8_t b2 = 0x40 | 0x01;
        Cea608XdsExtractor ext;
        const uint32_t sum = 0x01 + 0x08 + b1 + b2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x08); // Current / Copy and Redistribution Control
        ext.processPair(b1, b2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto cgms = packets[0].cgmsA();
        REQUIRE(cgms.hasValue());
        CHECK(cgms.value().cgms == Cea608XdsCgmsControl::NoMoreCopies);
        REQUIRE(cgms.value().aps.hasValue());
        CHECK(cgms.value().aps.value() == Cea608XdsApsControl::Psp2LineSplitBurst);
        CHECK(cgms.value().analogSourceBit == true);
        CHECK(cgms.value().redistributionControl == true);
}

TEST_CASE("Cea608XdsPacket: cgmsA hides APS when CGMS == CopyFree (§9.5.1.8 note)") {
        // CGMS=CopyFree (0,0): APS bits are present on the wire but
        // carry no defined meaning per spec — accessor returns an
        // empty Optional for the aps field.
        const uint8_t b1 = 0x40 | (0x00 << 3) | (0x02 << 1) | 0x00;
        const uint8_t b2 = 0x40;
        Cea608XdsExtractor ext;
        const uint32_t sum = 0x01 + 0x08 + b1 + b2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x08);
        ext.processPair(b1, b2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto cgms = packets[0].cgmsA();
        REQUIRE(cgms.hasValue());
        CHECK(cgms.value().cgms == Cea608XdsCgmsControl::CopyFree);
        CHECK_FALSE(cgms.value().aps.hasValue());
}

TEST_CASE("Cea608XdsPacket: cgmsA rejects non-zero reserved bits per §9.1") {
        // Byte 1 bit 5 is reserved — set it; accessor must reject.
        {
                const uint8_t b1 = 0x40 | 0x20 | (0x01 << 3); // reserved bit set
                const uint8_t b2 = 0x40;
                Cea608XdsExtractor ext;
                const uint32_t sum = 0x01 + 0x08 + b1 + b2 + 0x0F;
                const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
                ext.processPair(0x01, 0x08);
                ext.processPair(b1, b2);
                ext.processPair(0x0F, chk);
                const auto packets = ext.drain();
                REQUIRE(packets.size() == 1);
                CHECK_FALSE(packets[0].cgmsA().hasValue());
        }
        // Byte 2 bits 5..1 all reserved — set bit 4 alone; reject.
        {
                const uint8_t b1 = 0x40 | (0x01 << 3);
                const uint8_t b2 = 0x40 | 0x10; // reserved bit set
                Cea608XdsExtractor ext;
                const uint32_t sum = 0x01 + 0x08 + b1 + b2 + 0x0F;
                const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
                ext.processPair(0x01, 0x08);
                ext.processPair(b1, b2);
                ext.processPair(0x0F, chk);
                const auto packets = ext.drain();
                REQUIRE(packets.size() == 1);
                CHECK_FALSE(packets[0].cgmsA().hasValue());
        }
}

TEST_CASE("Cea608XdsPacket: cgmsA returns empty for wrong type (e.g. Composite-1 at 0x0C)") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x0C; // Composite Packet-1, NOT CGMS-A
        p.payload.pushToBack(0x40);
        p.payload.pushToBack(0x40);
        CHECK_FALSE(p.cgmsA().hasValue());
}

// ============================================================================
// Program Identification Number
// ============================================================================

TEST_CASE("Cea608XdsPacket: isProgramIdEndOfProgramSentinel detects all-ones payload") {
        // §9.5.1.1 "When all characters of this packet contain all Ones,
        // it indicates the end of the current program."  Per §9 the
        // bit-6 marker is always 1, so all-ones means 0x7F on every
        // payload byte.
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x01;
        for (int i = 0; i < 4; ++i) p.payload.pushToBack(0x7F);
        CHECK(p.isProgramIdEndOfProgramSentinel());
        // programId() declines to surface an Optional for the
        // sentinel (date=31 is in-range but month=15 is out-of-range,
        // so the validation guard rejects it).
        CHECK_FALSE(p.programId().hasValue());
        // Non-sentinel returns false.
        Cea608XdsPacket q;
        q.class_ = Cea608XdsClass::Current;
        q.type = 0x01;
        q.payload.pushToBack(static_cast<uint8_t>(0x40 | 30));
        q.payload.pushToBack(static_cast<uint8_t>(0x40 | 14));
        q.payload.pushToBack(static_cast<uint8_t>(0x40 | 15));
        q.payload.pushToBack(static_cast<uint8_t>(0x40 | 3));
        CHECK_FALSE(q.isProgramIdEndOfProgramSentinel());
}

TEST_CASE("Cea608XdsPacket: timeOfDay tolerates the day-of-week byte (§9.5.4.1 Table 36)") {
        // §9.5.4.1: ToD payload[4] is the day-of-week (1=Sunday..7=Saturday).
        // The decoder reads year from payload[5] and lets std::tm /
        // timegm re-derive day-of-week from the date — so the value of
        // payload[4] doesn't affect the recovered DateTime.  Verify a
        // packet with day-of-week=1 (Sunday) and day-of-week=7 (Saturday)
        // both decode to the same wall-clock instant when other fields
        // match.
        auto build = [](uint8_t dow) {
                Cea608XdsExtractor ext;
                const uint8_t mb = 0x40 | 30;       // minute=30
                const uint8_t hb = 0x40 | 14;       // hour=14
                const uint8_t db = 0x40 | 15;       // date=15
                const uint8_t mob = 0x40 | 3;       // month=3
                const uint8_t dowb = 0x40 | dow;
                const uint8_t yb = 0x40 | 34;       // year 2024 (2024-1990 = 34)
                const uint32_t sum = 0x07 + 0x01 + mb + hb + db + mob + dowb + yb + 0x0F;
                const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
                ext.processPair(0x07, 0x01);
                ext.processPair(mb, hb);
                ext.processPair(db, mob);
                ext.processPair(dowb, yb);
                ext.processPair(0x0F, chk);
                return ext.drain();
        };
        const auto sundayPackets = build(1);
        const auto saturdayPackets = build(7);
        REQUIRE(sundayPackets.size() == 1);
        REQUIRE(saturdayPackets.size() == 1);
        const auto a = sundayPackets[0].timeOfDay();
        const auto b = saturdayPackets[0].timeOfDay();
        REQUIRE(a.hasValue());
        REQUIRE(b.hasValue());
        CHECK(a.value().toTimeT() == b.value().toTimeT());
}

TEST_CASE("Cea608XdsPacket: programId decodes minute/hour/date/month") {
        Cea608XdsExtractor ext;
        // 4-byte payload, each byte's bit 6 set:
        //  minute = 15 → 0x4F   hour = 21 → 0x55
        //  date   = 23 → 0x57   month = 11 + T-bit set → 0x5B
        const uint8_t b1 = 0x40 | 15;
        const uint8_t b2 = 0x40 | 21;
        const uint8_t b3 = 0x40 | 23;
        const uint8_t b4 = 0x40 | 0x10 | 11; // tape-delay flag + month=11
        const uint32_t sum = 0x01 + 0x01 + b1 + b2 + b3 + b4 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x01); // Current / ProgramId
        ext.processPair(b1, b2);
        ext.processPair(b3, b4);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto pid = packets[0].programId();
        REQUIRE(pid.hasValue());
        CHECK(pid.value().minute == 15);
        CHECK(pid.value().hour == 21);
        CHECK(pid.value().date == 23);
        CHECK(pid.value().month == 11);
        CHECK(pid.value().tapeDelay == true);
}

// ============================================================================
// Program Length
// ============================================================================

TEST_CASE("Cea608XdsPacket: programLength decodes length-only (2 bytes)") {
        Cea608XdsExtractor ext;
        // 2-byte payload: length minutes = 30, length hours = 1.
        const uint8_t b1 = 0x40 | 30; // minutes
        const uint8_t b2 = 0x40 | 1;  // hours
        const uint32_t sum = 0x01 + 0x02 + b1 + b2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x02); // Current / ProgramLength
        ext.processPair(b1, b2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto pl = packets[0].programLength();
        REQUIRE(pl.hasValue());
        CHECK(pl.value().lengthMinutes == 30);
        CHECK(pl.value().lengthHours == 1);
        CHECK_FALSE(pl.value().hasElapsedTime);
        CHECK_FALSE(pl.value().hasElapsedSeconds);
}

TEST_CASE("Cea608XdsPacket: programLength decodes length + elapsed (4 bytes)") {
        Cea608XdsExtractor ext;
        // 4-byte payload per §9.5.1.2 Table 16:
        //   [0] Length-(m) = 30   [1] Length-(h) = 1
        //   [2] Elapsed-(m) = 15  [3] Elapsed-(h) = 0
        const uint8_t bytes[4] = {
                static_cast<uint8_t>(0x40 | 30),
                static_cast<uint8_t>(0x40 | 1),
                static_cast<uint8_t>(0x40 | 15),
                static_cast<uint8_t>(0x40 | 0),
        };
        uint32_t sum = 0x01 + 0x02 + 0x0F;
        for (uint8_t b : bytes) sum += b;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x02);
        ext.processPair(bytes[0], bytes[1]);
        ext.processPair(bytes[2], bytes[3]);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto pl = packets[0].programLength();
        REQUIRE(pl.hasValue());
        CHECK(pl.value().lengthHours == 1);
        CHECK(pl.value().lengthMinutes == 30);
        CHECK(pl.value().hasElapsedTime);
        CHECK(pl.value().elapsedMinutes == 15);
        CHECK(pl.value().elapsedHours == 0);
        CHECK_FALSE(pl.value().hasElapsedSeconds);
}

TEST_CASE("Cea608XdsPacket: programLength decodes length + elapsed + seconds (6 bytes)") {
        Cea608XdsExtractor ext;
        // 6-byte payload per §9.5.1.2 Table 16:
        //   [0] Length-(m) = 30   [1] Length-(h) = 1
        //   [2] Elapsed-(m) = 15  [3] Elapsed-(h) = 0
        //   [4] Elapsed-(s) = 45  [5] Null
        const uint8_t bytes[6] = {
                static_cast<uint8_t>(0x40 | 30), // length minutes
                static_cast<uint8_t>(0x40 | 1),  // length hours
                static_cast<uint8_t>(0x40 | 15), // elapsed minutes
                static_cast<uint8_t>(0x40 | 0),  // elapsed hours
                static_cast<uint8_t>(0x40 | 45), // elapsed seconds
                static_cast<uint8_t>(0x00),      // null
        };
        uint32_t sum = 0x01 + 0x02 + 0x0F;
        for (uint8_t b : bytes) sum += b;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x02);
        ext.processPair(bytes[0], bytes[1]);
        ext.processPair(bytes[2], bytes[3]);
        ext.processPair(bytes[4], bytes[5]);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto pl = packets[0].programLength();
        REQUIRE(pl.hasValue());
        CHECK(pl.value().lengthHours == 1);
        CHECK(pl.value().lengthMinutes == 30);
        CHECK(pl.value().hasElapsedTime);
        CHECK(pl.value().elapsedMinutes == 15);
        CHECK(pl.value().elapsedHours == 0);
        CHECK(pl.value().hasElapsedSeconds);
        CHECK(pl.value().elapsedSeconds == 45);
}

// ============================================================================
// Program Type
// ============================================================================

TEST_CASE("Cea608XdsPacket: programTypeKeywords returns list of bytes; programTypeName decodes") {
        Cea608XdsExtractor ext;
        // 2-byte payload: keywords Movie (0x22) + Comedy (0x34).
        const uint32_t sum = 0x01 + 0x04 + 0x22 + 0x34 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x04); // Current / ProgramType
        ext.processPair(0x22, 0x34);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto kws = packets[0].programTypeKeywords();
        REQUIRE(kws.size() == 2);
        CHECK(kws[0] == 0x22);
        CHECK(kws[1] == 0x34);
        CHECK(Cea608XdsPacket::programTypeName(0x22) == "Movie");
        CHECK(Cea608XdsPacket::programTypeName(0x34) == "Comedy");
        CHECK(Cea608XdsPacket::programTypeName(0xFE).isEmpty());
}

// ============================================================================
// Aspect Ratio
// ============================================================================

TEST_CASE("Cea608XdsPacket: aspectRatio decodes start/end line + squeezed flag") {
        Cea608XdsExtractor ext;
        // 2-byte payload: startLine=5, squeezed=true, endLine=20.
        const uint8_t b1 = 0x40 | 0x20 | 5;  // marker + squeezed + start
        const uint8_t b2 = 0x40 | 20;
        const uint32_t sum = 0x01 + 0x09 + b1 + b2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x09); // Current / AspectRatio
        ext.processPair(b1, b2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto ar = packets[0].aspectRatio();
        REQUIRE(ar.hasValue());
        CHECK(ar.value().startLine == 5);
        CHECK(ar.value().endLine == 20);
        CHECK(ar.value().squeezed == true);
}

// ============================================================================
// Transmission Signal ID (Channel / 0x04)
// ============================================================================

TEST_CASE("Cea608XdsPacket: transmissionSignalId decodes 16-bit value from 4 binary nibbles") {
        Cea608XdsExtractor ext;
        // Per §9.5.3.4 Table 35: 4 bytes, each carries one 4-bit nibble
        // of the TSID in bits b3..b0, low-order nibble first.  Bit b6 is
        // the standard 1-marker; bits b5/b4 are Reserved (zero).
        // TSID = 0xABCD: t3..t0=0xD, t7..t4=0xC, t11..t8=0xB, t15..t12=0xA.
        const uint8_t b[4] = {
                static_cast<uint8_t>(0x40 | 0xD),
                static_cast<uint8_t>(0x40 | 0xC),
                static_cast<uint8_t>(0x40 | 0xB),
                static_cast<uint8_t>(0x40 | 0xA),
        };
        const uint32_t sum = 0x05 + 0x04 + b[0] + b[1] + b[2] + b[3] + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x05, 0x04); // Channel / TSID
        ext.processPair(b[0], b[1]);
        ext.processPair(b[2], b[3]);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto tsid = packets[0].transmissionSignalId();
        REQUIRE(tsid.hasValue());
        CHECK(tsid.value() == 0xABCD);
}

// ============================================================================
// Time Zone (Misc / 0x04)
// ============================================================================

TEST_CASE("Cea608XdsPacket: timeZone decodes UTC offset and DST flag") {
        Cea608XdsExtractor ext;
        // 2-byte payload (rounded to even per §9.2): UTC offset=5 hours west,
        // DST observed.  Wire byte = 0x40 | 0x20 | 5 = 0x65.  Pair it
        // with a 0x00 pad to keep the byte count even.
        const uint8_t b1 = 0x40 | 0x20 | 5;
        const uint32_t sum = 0x07 + 0x04 + b1 + 0x00 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x04); // Misc / TimeZone
        ext.processPair(b1, 0x00);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto tz = packets[0].timeZone();
        REQUIRE(tz.hasValue());
        CHECK(tz.value().utcOffsetHours == -5);
        CHECK(tz.value().observesDst == true);
}

// ============================================================================
// Encoder — Cea608XdsPacket::encode + helpers
// ============================================================================

TEST_CASE("xdsStartByte / xdsContinueByte: class control bytes match Table 14") {
        CHECK(xdsStartByte(Cea608XdsClass::Current) == 0x01);
        CHECK(xdsContinueByte(Cea608XdsClass::Current) == 0x02);
        CHECK(xdsStartByte(Cea608XdsClass::Future) == 0x03);
        CHECK(xdsContinueByte(Cea608XdsClass::Future) == 0x04);
        CHECK(xdsStartByte(Cea608XdsClass::Channel) == 0x05);
        CHECK(xdsStartByte(Cea608XdsClass::Misc) == 0x07);
        CHECK(xdsStartByte(Cea608XdsClass::PublicSvc) == 0x09);
        CHECK(xdsStartByte(Cea608XdsClass::Reserved) == 0x0B);
        CHECK(xdsStartByte(Cea608XdsClass::PrivateData) == 0x0D);
        CHECK(xdsStartByte(Cea608XdsClass::Unknown) == 0x00);
        CHECK(xdsContinueByte(Cea608XdsClass::Unknown) == 0x00);
}

TEST_CASE("xdsChecksum: makes the modular sum zero") {
        // Manual sanity check: class=0x01 type=0x03, info bytes 'A' 'B'.
        const uint8_t chk = xdsChecksum(0x01 + 0x03, static_cast<uint32_t>('A' + 'B'));
        const uint32_t sum = 0x01 + 0x03 + 'A' + 'B' + 0x0F + chk;
        CHECK((sum & 0x7F) == 0);
}

TEST_CASE("Cea608XdsPacket::encode: round-trips Program Name through extractor") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Current;
        src.type = 0x03;
        const char *title = "HELLO";
        for (size_t i = 0; i < std::char_traits<char>::length(title); ++i) {
                src.payload.pushToBack(static_cast<uint8_t>(title[i]));
        }
        const List<uint8_t> bytes = src.encode();
        REQUIRE(bytes.size() % 2 == 0);
        Cea608XdsExtractor ext;
        feedPairs(ext, bytes);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].class_ == Cea608XdsClass::Current);
        CHECK(packets[0].type == 0x03);
        CHECK(packets[0].programName() == "HELLO");
        CHECK(ext.checksumFailures() == 0);
}

TEST_CASE("Cea608XdsPacket::encode: pads odd-length payload with 0x00 and round-trips") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Channel;
        src.type = 0x01; // Network Name
        // 3-byte payload — odd; encoder must pad to 4.
        src.payload.pushToBack('A');
        src.payload.pushToBack('B');
        src.payload.pushToBack('C');
        const List<uint8_t> bytes = src.encode();
        REQUIRE(bytes.size() % 2 == 0);
        // Expected layout: 0x05 0x01 | 'A' 'B' | 'C' 0x00 | 0x0F chk.
        REQUIRE(bytes.size() == 8);
        CHECK(bytes[0] == 0x05);
        CHECK(bytes[1] == 0x01);
        CHECK(bytes[2] == 'A');
        CHECK(bytes[3] == 'B');
        CHECK(bytes[4] == 'C');
        CHECK(bytes[5] == 0x00);
        CHECK(bytes[6] == 0x0F);
        Cea608XdsExtractor ext;
        feedPairs(ext, bytes);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].class_ == Cea608XdsClass::Channel);
        CHECK(packets[0].networkName() == "ABC");
}

TEST_CASE("Cea608XdsPacket::encode: rejects Unknown class and types with bit 7 set") {
        Cea608XdsPacket badClass;
        badClass.class_ = Cea608XdsClass::Unknown;
        badClass.type = 0x03;
        CHECK(badClass.encode().isEmpty());

        Cea608XdsPacket badType;
        badType.class_ = Cea608XdsClass::Current;
        badType.type = 0x83; // bit 7 set — out of range
        CHECK(badType.encode().isEmpty());
}

TEST_CASE("Cea608XdsPacket::encode: round-trips MPAA Content Advisory") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Current;
        src.type = 0x05;
        src.payload.pushToBack(0x40 | 0x03); // MPAA selector, rating=3 (PG-13)
        src.payload.pushToBack(0x40);
        Cea608XdsExtractor ext;
        feedPairs(ext, src.encode());
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto adv = packets[0].contentAdvisory();
        REQUIRE(adv.hasValue());
        CHECK(adv.value().mpaa == Cea608XdsMpaaRating::Pg13);
}

// ============================================================================
// Audio Services (§9.5.1.6)
// ============================================================================

TEST_CASE("Cea608XdsPacket: audioServices decodes Main + SAP language + type") {
        // Main = English (L=001) + True Stereo (T=011) → 0x40 | (1<<3) | 3 = 0x4B.
        // SAP  = Spanish (L=010) + Video Descriptions (T=010) → 0x40 | (2<<3) | 2 = 0x52.
        Cea608XdsExtractor ext;
        const uint8_t mb = 0x4B;
        const uint8_t sb = 0x52;
        const uint32_t sum = 0x01 + 0x06 + mb + sb + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x06);
        ext.processPair(mb, sb);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto a = packets[0].audioServices();
        REQUIRE(a.hasValue());
        CHECK(a.value().mainLanguage == Cea608XdsLanguage::English);
        CHECK(a.value().mainType == Cea608XdsMainAudioType::TrueStereo);
        CHECK(a.value().sapLanguage == Cea608XdsLanguage::Spanish);
        CHECK(a.value().sapType == Cea608XdsSecondAudioType::VideoDescriptions);
}

// ============================================================================
// Caption Services (§9.5.1.7)
// ============================================================================

TEST_CASE("Cea608XdsPacket: captionServices decodes 2 entries with language + F/C/T bits") {
        // Entry 1: English (L=001), F1 C1 captioning (F=0 C=0 T=0) → 0x40 | (1<<3) = 0x48.
        // Entry 2: Spanish (L=010), F1 C2 captioning (F=0 C=1 T=0) → 0x40 | (2<<3) | 0x02 = 0x52.
        Cea608XdsExtractor ext;
        const uint8_t b0 = 0x48;
        const uint8_t b1 = 0x52;
        const uint32_t sum = 0x01 + 0x07 + b0 + b1 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x07);
        ext.processPair(b0, b1);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto cs = packets[0].captionServices();
        REQUIRE(cs.size() == 2);
        CHECK(cs[0].language == Cea608XdsLanguage::English);
        CHECK_FALSE(cs[0].fieldTwo);
        CHECK_FALSE(cs[0].channelTwo);
        CHECK_FALSE(cs[0].textMode);
        CHECK(cs[1].language == Cea608XdsLanguage::Spanish);
        CHECK(cs[1].channelTwo);
}

// ============================================================================
// Tape Delay (§9.5.3.3)
// ============================================================================

TEST_CASE("Cea608XdsPacket: tapeDelay decodes hours + minutes") {
        Cea608XdsExtractor ext;
        // Tape delay = 3h 0m.  payload[0] = 0x40 | 0 = 0x40, payload[1] = 0x40 | 3 = 0x43.
        const uint8_t mb = 0x40;
        const uint8_t hb = 0x43;
        const uint32_t sum = 0x05 + 0x03 + mb + hb + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x05, 0x03);
        ext.processPair(mb, hb);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto td = packets[0].tapeDelay();
        REQUIRE(td.hasValue());
        CHECK(td.value().hours == 3);
        CHECK(td.value().minutes == 0);
}

// ============================================================================
// Impulse Capture ID (§9.5.4.2)
// ============================================================================

TEST_CASE("Cea608XdsPacket: impulseCaptureId decodes PID + length prefix") {
        Cea608XdsExtractor ext;
        // PID: minute=20, hour=19, date=4, month=7 (no T/Z).
        // Length prefix: minutes=30, hours=1.
        const uint8_t b1 = 0x40 | 20;
        const uint8_t b2 = 0x40 | 19;
        const uint8_t b3 = 0x40 | 4;
        const uint8_t b4 = 0x40 | 7;
        const uint8_t b5 = 0x40 | 30;
        const uint8_t b6 = 0x40 | 1;
        const uint32_t sum = 0x07 + 0x02 + b1 + b2 + b3 + b4 + b5 + b6 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x02);
        ext.processPair(b1, b2);
        ext.processPair(b3, b4);
        ext.processPair(b5, b6);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto ic = packets[0].impulseCaptureId();
        REQUIRE(ic.hasValue());
        CHECK(ic.value().programId.minute == 20);
        CHECK(ic.value().programId.hour == 19);
        CHECK(ic.value().programId.date == 4);
        CHECK(ic.value().programId.month == 7);
        CHECK(ic.value().lengthMinutes == 30);
        CHECK(ic.value().lengthHours == 1);
}

// ============================================================================
// Supplemental Data Location (§9.5.4.3)
// ============================================================================

TEST_CASE("Cea608XdsPacket: supplementalDataLocations decodes F bit + line number") {
        Cea608XdsExtractor ext;
        // Two entries: F=0 line=21, F=1 line=13.
        // payload[0] = 0x40 | 21 = 0x55; payload[1] = 0x40 | 0x20 | 13 = 0x6D.
        const uint8_t b0 = 0x55;
        const uint8_t b1 = 0x6D;
        const uint32_t sum = 0x07 + 0x03 + b0 + b1 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x03);
        ext.processPair(b0, b1);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto locs = packets[0].supplementalDataLocations();
        REQUIRE(locs.size() == 2);
        CHECK_FALSE(locs[0].fieldTwo);
        CHECK(locs[0].lineNumber == 21);
        CHECK(locs[1].fieldTwo);
        CHECK(locs[1].lineNumber == 13);
}

// ============================================================================
// Out-of-Band Channel + Channel Map (§9.5.4.5)
// ============================================================================

TEST_CASE("Cea608XdsPacket: outOfBandChannel decodes 12-bit channel number") {
        // Channel = 0x123 (291).  c5..c0 = 0x23, c11..c6 = 0x04.
        Cea608XdsExtractor ext;
        const uint8_t lo = 0x40 | (0x123 & 0x3F);
        const uint8_t hi = 0x40 | ((0x123 >> 6) & 0x3F);
        const uint32_t sum = 0x07 + 0x40 + lo + hi + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x40);
        ext.processPair(lo, hi);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto c = packets[0].outOfBandChannel();
        REQUIRE(c.hasValue());
        CHECK(c.value() == 0x123);
}

TEST_CASE("Cea608XdsPacket: channelMapHeader decodes #channels + version") {
        Cea608XdsExtractor ext;
        // 100 channels, version 5.  100 = 0x64; c5..c0=0x24, c9..c6=0x01.
        const uint8_t lo = 0x40 | (100 & 0x3F);
        const uint8_t hi = 0x40 | ((100 >> 6) & 0x0F);
        const uint8_t ver = 0x40 | 5;
        const uint8_t nul = 0x00;
        const uint32_t sum = 0x07 + 0x42 + lo + hi + ver + nul + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x42);
        ext.processPair(lo, hi);
        ext.processPair(ver, nul);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto h = packets[0].channelMapHeader();
        REQUIRE(h.hasValue());
        CHECK(h.value().channelCount == 100);
        CHECK(h.value().version == 5);
}

TEST_CASE("Cea608XdsPacket: channelMapPacket decodes user+tune+ID for remapped channel") {
        Cea608XdsExtractor ext;
        // User chan = 7 (NOT remapped → no tune-channel bytes).  ID = "CBS".
        const uint8_t userLo = 0x40 | 7;
        const uint8_t userHi = 0x40 | 0; // rm = 0
        const uint8_t id1 = 'C';
        const uint8_t id2 = 'B';
        const uint8_t id3 = 'S';
        const uint8_t id4 = 0x00; // pad to even
        const uint32_t sum = 0x07 + 0x43 + userLo + userHi + id1 + id2 + id3 + id4 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x07, 0x43);
        ext.processPair(userLo, userHi);
        ext.processPair(id1, id2);
        ext.processPair(id3, id4);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto cm = packets[0].channelMapPacket();
        REQUIRE(cm.hasValue());
        CHECK(cm.value().userChannel == 7);
        CHECK_FALSE(cm.value().remapped);
        CHECK(cm.value().channelId == "CBS");
}

// ============================================================================
// WRSAME + NWS Message (§9.5.5)
// ============================================================================

TEST_CASE("Cea608XdsPacket: wrsame decodes event code + state + county + duration") {
        // Tornado warning, slice 1, state 06 (CA), county 037 (LA), duration 04 (1 hour).
        Cea608XdsExtractor ext;
        const uint8_t b[16] = {
                'T', 'O', 'R', '-',         // event code + '-'
                '1', '0', '6', '0',         // P, SS, CCC start
                '3', '7', '-', 0x00,        // CCC end, '-', NUL
                '+', '0', '4', '-',         // '+', nn, '-'
        };
        uint32_t sum = 0x09 + 0x01 + 0x0F;
        for (uint8_t bb : b) sum += bb;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x09, 0x01);
        for (size_t i = 0; i < sizeof(b); i += 2) {
                ext.processPair(b[i], b[i + 1]);
        }
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto w = packets[0].wrsame();
        REQUIRE(w.hasValue());
        CHECK(w.value().eventCode == "TOR");
        CHECK(w.value().countySlice == 1);
        CHECK(w.value().stateCode == 6);
        CHECK(w.value().countyCode == 37);
        CHECK(w.value().durationQuarters == 4);
}

TEST_CASE("Cea608XdsPacket: nwsMessage returns free-text NWS warning text") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::PublicSvc;
        p.type = 0x02;
        const char *msg = "TORNADO WARNING";
        for (size_t i = 0; i < std::char_traits<char>::length(msg); ++i) {
                p.payload.pushToBack(static_cast<uint8_t>(msg[i]));
        }
        CHECK(p.nwsMessage() == "TORNADO WARNING");
}

// ============================================================================
// Program Description rows (§9.5.1.12)
// ============================================================================

TEST_CASE("Cea608XdsPacket: programDescriptionRow returns the row text + index") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x12; // Row 3 (0x10 + 2)
        const char *text = "Episode pilot";
        for (size_t i = 0; i < std::char_traits<char>::length(text); ++i) {
                p.payload.pushToBack(static_cast<uint8_t>(text[i]));
        }
        CHECK(p.programDescriptionRow() == "Episode pilot");
        CHECK(p.programDescriptionRowIndex() == 3);
}

// ============================================================================
// Composite Packet-1 / Packet-2 (§9.5.1.10 / §9.5.1.11)
// ============================================================================

TEST_CASE("Cea608XdsPacket: compositePacket1 decodes 10-byte minimal form (no title)") {
        // Build a 10-byte Composite-1 payload:
        //   [0..4] Program Type keywords: Movie (0x22), Drama (0x3C), nulls.
        //   [5]    Content Advisory char 1 (MPAA system 0, rating PG-13 = 3) = 0x43.
        //   [6..7] Length: 30 min, 1 hour.
        //   [8..9] Time-in-show: 5 min, 0 hours.
        Cea608XdsExtractor ext;
        const uint8_t payload[10] = {
                0x22, 0x3C, 0x00, 0x00, 0x00,
                0x43,
                static_cast<uint8_t>(0x40 | 30), static_cast<uint8_t>(0x40 | 1),
                static_cast<uint8_t>(0x40 | 5), static_cast<uint8_t>(0x40 | 0),
        };
        uint32_t sum = 0x01 + 0x0C + 0x0F;
        for (uint8_t b : payload) sum += b;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x0C);
        for (size_t i = 0; i + 1 < 10; i += 2) {
                ext.processPair(payload[i], payload[i + 1]);
        }
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto c1 = packets[0].compositePacket1();
        REQUIRE(c1.hasValue());
        REQUIRE(c1.value().programTypeKeywords.size() == 2);
        CHECK(c1.value().programTypeKeywords[0] == 0x22);
        CHECK(c1.value().programTypeKeywords[1] == 0x3C);
        CHECK(c1.value().contentAdvisoryByte1 == 0x43);
        CHECK(c1.value().lengthMinutes == 30);
        CHECK(c1.value().lengthHours == 1);
        CHECK(c1.value().elapsedMinutes == 5);
        CHECK(c1.value().elapsedHours == 0);
        CHECK(c1.value().title.isEmpty());
}

TEST_CASE("Cea608XdsPacket: compositePacket1 decodes title bytes") {
        // 12-byte Composite-1 — 10-byte prefix + "HI" title.
        Cea608XdsExtractor ext;
        const uint8_t payload[12] = {
                0x23, 0x00, 0x00, 0x00, 0x00, // Program Type "News" only
                0x40,                          // Content Advisory N/A
                static_cast<uint8_t>(0x40 | 0), static_cast<uint8_t>(0x40 | 0),
                static_cast<uint8_t>(0x40 | 0), static_cast<uint8_t>(0x40 | 0),
                'H', 'I',
        };
        uint32_t sum = 0x01 + 0x0C + 0x0F;
        for (uint8_t b : payload) sum += b;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x0C);
        for (size_t i = 0; i + 1 < 12; i += 2) {
                ext.processPair(payload[i], payload[i + 1]);
        }
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto c1 = packets[0].compositePacket1();
        REQUIRE(c1.hasValue());
        CHECK(c1.value().title == "HI");
}

TEST_CASE("Cea608XdsPacket: compositePacket2 decodes 14-byte minimal form (no network name)") {
        // 14-byte Composite-2 payload:
        //   [0..3] PID: minute=15, hour=21, date=23, month=11.
        //   [4..5] Audio: Main English/TrueStereo (0x4B), SAP Spanish/VideoDescriptions (0x52).
        //   [6..7] Caption: 2 entries — English C1 captioning (0x48), Spanish C2 (0x52).
        //   [8..11] Call Letters "KCBS".
        //   [12..13] Native Channel "02" → 2.
        Cea608XdsExtractor ext;
        const uint8_t payload[14] = {
                static_cast<uint8_t>(0x40 | 15),
                static_cast<uint8_t>(0x40 | 21),
                static_cast<uint8_t>(0x40 | 23),
                static_cast<uint8_t>(0x40 | 11),
                0x4B, 0x52,
                0x48, 0x52,
                'K', 'C', 'B', 'S',
                '0', '2',
        };
        uint32_t sum = 0x01 + 0x0D + 0x0F;
        for (uint8_t b : payload) sum += b;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x0D);
        for (size_t i = 0; i + 1 < 14; i += 2) {
                ext.processPair(payload[i], payload[i + 1]);
        }
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto c2 = packets[0].compositePacket2();
        REQUIRE(c2.hasValue());
        CHECK(c2.value().programId.minute == 15);
        CHECK(c2.value().programId.hour == 21);
        CHECK(c2.value().programId.date == 23);
        CHECK(c2.value().programId.month == 11);
        CHECK(c2.value().audioServices.mainLanguage == Cea608XdsLanguage::English);
        CHECK(c2.value().audioServices.mainType == Cea608XdsMainAudioType::TrueStereo);
        CHECK(c2.value().audioServices.sapLanguage == Cea608XdsLanguage::Spanish);
        CHECK(c2.value().audioServices.sapType == Cea608XdsSecondAudioType::VideoDescriptions);
        REQUIRE(c2.value().captionServices.size() == 2);
        CHECK(c2.value().captionServices[0].language == Cea608XdsLanguage::English);
        CHECK(c2.value().captionServices[1].channelTwo);
        CHECK(c2.value().callLetters == "KCBS");
        REQUIRE(c2.value().nativeChannel.hasValue());
        CHECK(c2.value().nativeChannel.value() == 2);
        CHECK(c2.value().networkName.isEmpty());
}

// ============================================================================
// Out-of-Band flag
// ============================================================================

TEST_CASE("Cea608XdsPacket::isOutOfBand reflects bit 6 of the type byte") {
        Cea608XdsPacket inBand;
        inBand.type = 0x03;
        CHECK_FALSE(inBand.isOutOfBand());
        Cea608XdsPacket oob;
        oob.type = 0x43; // bit 6 set
        CHECK(oob.isOutOfBand());
}

// ============================================================================
// X4 — Aspect Ratio encoder refuses Reserved Current/Future type
// ============================================================================

TEST_CASE("Cea608XdsPacket::encode: refuses Current/Future type 0x09 (Reserved in 608-E)") {
        Cea608XdsPacket reserved;
        reserved.class_ = Cea608XdsClass::Current;
        reserved.type = 0x09;
        reserved.payload.pushToBack(0x40);
        reserved.payload.pushToBack(0x40);
        CHECK(reserved.encode().isEmpty());

        Cea608XdsPacket futReserved;
        futReserved.class_ = Cea608XdsClass::Future;
        futReserved.type = 0x09;
        futReserved.payload.pushToBack(0x40);
        futReserved.payload.pushToBack(0x40);
        CHECK(futReserved.encode().isEmpty());

        // Receive-side leniency is preserved — a pre-608-E broadcaster
        // can still send Aspect Ratio and we decode it.  Build the
        // packet by hand to bypass the encoder's gate.
        Cea608XdsExtractor ext;
        const uint8_t b1 = 0x40 | 0x20 | 5;
        const uint8_t b2 = 0x40 | 20;
        const uint32_t sum = 0x01 + 0x09 + b1 + b2 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x09);
        ext.processPair(b1, b2);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        REQUIRE(packets[0].aspectRatio().hasValue());
}

// ============================================================================
// X5 / X6 — Caption-pair interrupt and Continue-type-mismatch semantics
// ============================================================================

TEST_CASE("Cea608XdsExtractor: caption control pair suspends in-flight XDS (§8.6.7)") {
        // Start a Current/Name packet, accumulate "HI", inject a
        // caption control pair (0x14 0x20 — CC1 RCL), then push an
        // informational pair "JK" + End/Checksum.  Per §8.6.7 the
        // caption pair must suspend the in-flight XDS — the "JK"
        // pair should NOT append to the buffer, so the End validates
        // against just the original "HI" sum.
        Cea608XdsExtractor ext;
        ext.processPair(0x01, 0x03);   // Start Current / Name
        ext.processPair(0x48, 0x49);   // "HI"
        ext.processPair(0x14, 0x20);   // CC1 caption control — suspends
        ext.processPair(0x4A, 0x4B);   // "JK" — dropped (slot suspended)
        // No End for the suspended slot yet — resume with Continue.
        ext.processPair(0x02, 0x03);   // Continue Current / Type 0x03
        const uint32_t sum = 0x01 + 0x03 + 0x48 + 0x49 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].programName() == "HI");
}

TEST_CASE("Cea608XdsExtractor: Continue with type-mismatch is rejected (§9.2)") {
        // Start Current / Name (type 0x03).  Issue a Continue Current
        // with a DIFFERENT type (0x05) — that's a §9.2 Type-byte
        // mismatch.  The extractor must drop the in-flight Name slot
        // and treat the orphaned Continue as no-op.
        Cea608XdsExtractor ext;
        ext.processPair(0x01, 0x03);   // Start Current / 0x03 (Name)
        ext.processPair(0x48, 0x49);   // "HI"
        ext.processPair(0x02, 0x05);   // Continue Current / 0x05 — mismatch
        // End with the original (0x03) checksum — should produce
        // nothing (the slot was dropped).
        const uint32_t sum = 0x01 + 0x03 + 0x48 + 0x49 + 0x0F;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        CHECK(packets.size() == 0);
}

// ============================================================================
// X7 — Injector yields F2 to captions per §E.10
// ============================================================================
// (covered in tests/unit/cea608xdsinjector.cpp)

// ============================================================================
// X9 — WRSAME requires 16-byte payload with trailing '-'
// ============================================================================

TEST_CASE("Cea608XdsPacket: wrsame rejects 15-byte truncation (no trailing '-')") {
        // 15-byte payload — missing the trailing '-'.  Build the
        // packet by hand to bypass encoder validation.
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::PublicSvc;
        p.type = 0x01;
        const uint8_t bytes[15] = {
                'T', 'O', 'R', '-',
                '1', '0', '6', '0',
                '3', '7', '-', 0x00,
                '+', '0', '4',
        };
        for (uint8_t b : bytes) p.payload.pushToBack(b);
        CHECK_FALSE(p.wrsame().hasValue());

        // Now add the trailing '-' (16th byte) — accessor should
        // accept.
        p.payload.pushToBack('-');
        CHECK(p.wrsame().hasValue());
}

// ============================================================================
// X10 — Caption Services / Program Type entry bounds
// ============================================================================

TEST_CASE("Cea608XdsPacket: captionServices rejects single-entry payload (<2)") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x07;
        p.payload.pushToBack(0x48); // English C1 captioning
        p.payload.pushToBack(0x00); // padding
        CHECK(p.captionServices().isEmpty());
}

TEST_CASE("Cea608XdsPacket: programTypeKeywords rejects single-keyword payload (<2)") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x04;
        p.payload.pushToBack(0x22); // Movie (Basic group)
        p.payload.pushToBack(0x00); // padding
        CHECK(p.programTypeKeywords().isEmpty());
}

TEST_CASE("Cea608XdsPacket: programTypeKeywords rejects Detail-before-Basic ordering") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x04;
        p.payload.pushToBack(0x34); // Comedy (Detail group)
        p.payload.pushToBack(0x22); // Movie (Basic group) — out of order
        CHECK(p.programTypeKeywords().isEmpty());
}

// ============================================================================
// X12 — Time Zone hour cap at 12
// ============================================================================

TEST_CASE("Cea608XdsPacket: timeZone rejects hour > 12") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Misc;
        p.type = 0x04;
        p.payload.pushToBack(static_cast<uint8_t>(0x40 | 13)); // hour=13 — out of spec
        p.payload.pushToBack(0x00);
        CHECK_FALSE(p.timeZone().hasValue());
        // 12 itself is permitted (boundary).
        Cea608XdsPacket q;
        q.class_ = Cea608XdsClass::Misc;
        q.type = 0x04;
        q.payload.pushToBack(static_cast<uint8_t>(0x40 | 12));
        q.payload.pushToBack(0x00);
        REQUIRE(q.timeZone().hasValue());
        CHECK(q.timeZone().value().utcOffsetHours == -12);
}

// ============================================================================
// X18 — encode mask normalisation
// ============================================================================
// (existing test "rejects Unknown class and types with bit 7 set" covers
//  the reject side; nothing further to add.)

// ============================================================================
// X20 — round-trip tests for additional packet types
// ============================================================================

TEST_CASE("Cea608XdsPacket: Network Name round-trips through extract") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Channel;
        src.type = 0x01;
        const char *name = "ACME";
        for (size_t i = 0; i < std::char_traits<char>::length(name); ++i) {
                src.payload.pushToBack(static_cast<uint8_t>(name[i]));
        }
        const List<uint8_t> bytes = src.encode();
        Cea608XdsExtractor ext;
        feedPairs(ext, bytes);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].networkName() == "ACME");
}

TEST_CASE("Cea608XdsPacket: Impulse Capture ID encode round-trips") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Misc;
        src.type = 0x02;
        // PID: minute=20 / hour=19 / date=4 / month=7.
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | 20));
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | 19));
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | 4));
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | 7));
        // Length: minutes=30, hours=1.
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | 30));
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | 1));
        Cea608XdsExtractor ext;
        feedPairs(ext, src.encode());
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto ic = packets[0].impulseCaptureId();
        REQUIRE(ic.hasValue());
        CHECK(ic.value().programId.minute == 20);
        CHECK(ic.value().lengthHours == 1);
}

TEST_CASE("Cea608XdsPacket: Channel Map Pointer encode round-trips") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Misc;
        src.type = 0x41;
        // 10-bit channel = 0x123 (291).
        const uint16_t ch = 0x123;
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | (ch & 0x3F)));
        src.payload.pushToBack(static_cast<uint8_t>(0x40 | ((ch >> 6) & 0x0F)));
        Cea608XdsExtractor ext;
        feedPairs(ext, src.encode());
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto cmp = packets[0].channelMapPointer();
        REQUIRE(cmp.hasValue());
        CHECK(cmp.value() == 0x123);
}

TEST_CASE("Cea608XdsPacket: Channel Map Pointer rejects non-zero reserved bits") {
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Misc;
        p.type = 0x41;
        p.payload.pushToBack(static_cast<uint8_t>(0x40 | 0x23));
        p.payload.pushToBack(static_cast<uint8_t>(0x40 | 0x20 | 0x01)); // bit 5 set
        CHECK_FALSE(p.channelMapPointer().hasValue());
}

TEST_CASE("Cea608XdsPacket: Program Description Row encode round-trips at full 32-char length") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Current;
        src.type = 0x12; // Row 3
        // 32 chars — exactly the §9.5.1.12 cap.  Round-trips through
        // the §8.6.6 32-byte payload limit cleanly.
        const char *text = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
        for (size_t i = 0; i < std::char_traits<char>::length(text); ++i) {
                src.payload.pushToBack(static_cast<uint8_t>(text[i]));
        }
        Cea608XdsExtractor ext;
        feedPairs(ext, src.encode());
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].programDescriptionRow().length() == 32);
        CHECK(packets[0].programDescriptionRowIndex() == 3);
}

TEST_CASE("Cea608XdsPacket: Program Description Row truncates oversized payload to 32 chars") {
        // Build a hand-built packet that bypasses the extractor's
        // §8.6.6 32-byte cap to verify the row accessor itself
        // truncates to §9.5.1.12's 32-char ceiling.
        Cea608XdsPacket p;
        p.class_ = Cea608XdsClass::Current;
        p.type = 0x12;
        const char *text = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ----";
        for (size_t i = 0; i < std::char_traits<char>::length(text); ++i) {
                p.payload.pushToBack(static_cast<uint8_t>(text[i]));
        }
        CHECK(p.programDescriptionRow().length() == 32);
}

TEST_CASE("Cea608XdsPacket: NWS Message encode round-trips") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::PublicSvc;
        src.type = 0x02;
        const char *msg = "FLASH FLOOD";
        for (size_t i = 0; i < std::char_traits<char>::length(msg); ++i) {
                src.payload.pushToBack(static_cast<uint8_t>(msg[i]));
        }
        Cea608XdsExtractor ext;
        feedPairs(ext, src.encode());
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].nwsMessage() == "FLASH FLOOD");
}

TEST_CASE("Cea608XdsPacket: Future class Program Name round-trips") {
        Cea608XdsPacket src;
        src.class_ = Cea608XdsClass::Future;
        src.type = 0x03;
        const char *title = "NEXT";
        for (size_t i = 0; i < std::char_traits<char>::length(title); ++i) {
                src.payload.pushToBack(static_cast<uint8_t>(title[i]));
        }
        Cea608XdsExtractor ext;
        feedPairs(ext, src.encode());
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        CHECK(packets[0].class_ == Cea608XdsClass::Future);
        CHECK(packets[0].programName() == "NEXT");
}

TEST_CASE("Cea608XdsPacket: Composite-1 with non-zero Content Advisory byte 1 round-trips") {
        // 10-byte Composite-1 with Content Advisory byte 1 = 0x43
        // (MPAA selector, rating 3 = PG-13).
        Cea608XdsExtractor ext;
        const uint8_t payload[10] = {
                0x22, 0x3C, 0x00, 0x00, 0x00, // Movie + Drama
                0x43,                          // CA byte 1: MPAA PG-13
                static_cast<uint8_t>(0x40 | 30), static_cast<uint8_t>(0x40 | 1),
                static_cast<uint8_t>(0x40 | 5), static_cast<uint8_t>(0x40 | 0),
        };
        uint32_t sum = 0x01 + 0x0C + 0x0F;
        for (uint8_t b : payload) sum += b;
        const uint8_t chk = static_cast<uint8_t>((0x80 - (sum & 0x7F)) & 0x7F);
        ext.processPair(0x01, 0x0C);
        for (size_t i = 0; i + 1 < 10; i += 2) {
                ext.processPair(payload[i], payload[i + 1]);
        }
        ext.processPair(0x0F, chk);
        const auto packets = ext.drain();
        REQUIRE(packets.size() == 1);
        const auto c1 = packets[0].compositePacket1();
        REQUIRE(c1.hasValue());
        CHECK(c1.value().contentAdvisoryByte1 == 0x43);
}
