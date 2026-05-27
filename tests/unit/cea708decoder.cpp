/**
 * @file      cea708decoder.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708decoder.h>
#include <promeki/cea708service.h>
#include <promeki/cea708windowstate.h>
#include <promeki/framenumber.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

        TimeStamp tsFromMs(int64_t ms) {
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
        }

        /// @brief Bundles up the DefineWindow + text byte stream for
        ///        @p text into a Buffer.  Window 0, single row, @p text
        ///        columns wide, visible.
        Buffer makeServiceBytes(const std::vector<uint8_t> &cmds) {
                Buffer b(cmds.size());
                b.setSize(cmds.size());
                if (!cmds.empty()) b.copyFrom(cmds.data(), cmds.size(), 0);
                return b;
        }

        /// @brief Builds a DefineWindow(0) byte sequence with the given
        ///        column count and visibility.
        std::vector<uint8_t> dfw0(uint8_t cols, bool visible = true) {
                const uint8_t rows = 0; // 1 row
                const uint8_t colCountWire = static_cast<uint8_t>((cols > 0 ? cols - 1 : 0) & 0x3F);
                return {0x98,
                        static_cast<uint8_t>((visible ? 0x40 : 0x00) | 0x30 /* locks */),
                        0x00,
                        0x00,
                        static_cast<uint8_t>(0x10 /*anchor 1*/ | rows),
                        colCountWire,
                        0x00};
        }

        /// @brief Builds a single service block containing @p bytes for
        ///        service @p serviceNumber.
        Cea708Service makeService(uint8_t serviceNumber, const std::vector<uint8_t> &bytes) {
                return Cea708Service(serviceNumber, makeServiceBytes(bytes));
        }

        /// @brief Builds a one-service-block DTVCC packet (sequence
        ///        rotates 0..3 across calls per CEA-708-E §5.1 — the
        ///        decoder's discontinuity guard expects monotonic
        ///        wrap-around) and returns its cc_data triple list.
        Cea708Cdp::CcDataList packetTriples(uint8_t serviceNumber,
                                            const std::vector<uint8_t> &serviceBytes) {
                static uint8_t seq = 0;
                Cea708DtvccPacket pkt(seq, {});
                seq = static_cast<uint8_t>((seq + 1u) & 0x03u);
                pkt.serviceBlocks().pushToBack(makeService(serviceNumber, serviceBytes));
                return pkt.toCcData();
        }

} // namespace

// ============================================================================
// Construction / defaults
// ============================================================================

TEST_CASE("Cea708Decoder: default config -> service 1") {
        Cea708Decoder d;
        CHECK(d.config().serviceNumber == 1);
        CHECK(d.displayedText() == "");
        CHECK(d.finalize().isEmpty());
}

TEST_CASE("Cea708Decoder: no triples -> finalize returns empty") {
        Cea708Decoder         dec;
        Cea708Cdp::CcDataList list;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), list);
        CHECK(dec.finalize().isEmpty());
}

// ============================================================================
// CcData filtering
// ============================================================================

TEST_CASE("Cea708Decoder: cc_type=0/1 (CEA-608) triples are ignored") {
        Cea708Decoder         dec;
        Cea708Cdp::CcDataList list;
        list.pushToBack(Cea708Cdp::CcData{true, 0, 0x94, 0x20}); // 608 RCL
        list.pushToBack(Cea708Cdp::CcData{true, 1, 0xAB, 0xCD}); // 608 field 2
        dec.pushFrame(FrameNumber(0), tsFromMs(0), list);
        CHECK(dec.displayedText() == "");
}

TEST_CASE("Cea708Decoder: cc_valid=false triples are skipped") {
        Cea708Decoder         dec;
        // A real DTVCC packet header but cc_valid=0 — should be ignored.
        Cea708Cdp::CcDataList list;
        list.pushToBack(Cea708Cdp::CcData{false, 2, 0x21, 0x41}); // would have started a 1-byte payload packet
        dec.pushFrame(FrameNumber(0), tsFromMs(0), list);
        CHECK(dec.displayedText() == "");
}

// ============================================================================
// Single-packet decode
// ============================================================================

TEST_CASE("Cea708Decoder: multi-block packet concatenates same-service bytes before parsing") {
        // The wire @c block_size field is 5 bits — long command streams
        // (DefineWindow + many chars + DSW) MUST be split across
        // multiple service blocks for the same service.  The decoder
        // must concatenate them before parsing, otherwise the 7-byte
        // DefineWindow command split across the 31-byte boundary would
        // be misinterpreted.  This test forges a packet where DFW0
        // straddles two blocks and verifies the cue still decodes.
        std::vector<uint8_t> bytes = dfw0(40, true);
        // Pad with characters so the total stream crosses 31 bytes.
        for (char c = 'A'; c <= 'Z'; ++c) bytes.push_back(static_cast<uint8_t>(c));
        // Split into two blocks at offset 5 — splits DFW0 mid-command.
        std::vector<uint8_t> chunk1(bytes.begin(), bytes.begin() + 5);
        std::vector<uint8_t> chunk2(bytes.begin() + 5, bytes.end());

        Cea708DtvccPacket pkt;
        pkt.serviceBlocks().pushToBack(makeService(1, chunk1));
        pkt.serviceBlocks().pushToBack(makeService(1, chunk2));
        Cea708Cdp::CcDataList triples = pkt.toCcData();

        Cea708Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), triples);
        CHECK(dec.displayedText() == "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

TEST_CASE("Cea708Decoder: blocks for other services are not mixed into the configured service stream") {
        // Two service blocks in one packet: service 1 carries a full
        // DefineWindow + "AB", service 2 carries a stray byte.  The
        // decoder's same-service concatenation must skip service 2
        // entirely when configured for service 1.
        std::vector<uint8_t> svc1 = dfw0(4, true);
        svc1.push_back('A');
        svc1.push_back('B');
        Cea708DtvccPacket pkt;
        pkt.serviceBlocks().pushToBack(makeService(1, svc1));
        pkt.serviceBlocks().pushToBack(makeService(2, {'Z'}));
        Cea708Cdp::CcDataList triples = pkt.toCcData();

        Cea708Decoder dec; // default service 1
        dec.pushFrame(FrameNumber(0), tsFromMs(0), triples);
        CHECK(dec.displayedText() == "AB");
}

TEST_CASE("Cea708Decoder: DefineWindow(0) + 'HELLO' yields visible text") {
        std::vector<uint8_t> bytes = dfw0(8, true);
        bytes.push_back('H');
        bytes.push_back('E');
        bytes.push_back('L');
        bytes.push_back('L');
        bytes.push_back('O');

        Cea708Cdp::CcDataList triples = packetTriples(1, bytes);

        Cea708Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), triples);
        CHECK(dec.displayedText() == "HELLO");
}

TEST_CASE("Cea708Decoder: cue start/end timestamps captured on content transitions") {
        // Frame 0: define + write "AB" → cue starts.
        std::vector<uint8_t> bytesA = dfw0(4, true);
        bytesA.push_back('A');
        bytesA.push_back('B');
        Cea708Cdp::CcDataList tripA = packetTriples(1, bytesA);

        Cea708Decoder dec;
        const TimeStamp t0 = tsFromMs(0);
        dec.pushFrame(FrameNumber(0), t0, tripA);
        CHECK(dec.displayedText() == "AB");
        CHECK(dec.displayedCue().start() == t0);

        // Frame 30: clear window (FF) → cue ends.
        std::vector<uint8_t> bytesB{0x0C}; // FF = clear current window
        Cea708Cdp::CcDataList tripB = packetTriples(1, bytesB);
        const TimeStamp        t30 = tsFromMs(1000);
        dec.pushFrame(FrameNumber(30), t30, tripB);
        CHECK(dec.displayedText() == "");

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "AB");
        CHECK(out[0].start() == t0);
        CHECK(out[0].end() == t30);
}

TEST_CASE("Cea708Decoder: finalize closes a still-displayed cue at last-frame ts") {
        std::vector<uint8_t> bytes = dfw0(4, true);
        bytes.push_back('X');
        Cea708Cdp::CcDataList trip = packetTriples(1, bytes);

        Cea708Decoder dec;
        const TimeStamp t0 = tsFromMs(0);
        dec.pushFrame(FrameNumber(0), t0, trip);
        // Push some empty frames to advance lastFrameTs.
        const TimeStamp tEnd = tsFromMs(500);
        Cea708Cdp::CcDataList empty;
        dec.pushFrame(FrameNumber(1), tEnd, empty);

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "X");
        CHECK(out[0].start() == t0);
        CHECK(out[0].end() == tEnd);
}

TEST_CASE("Cea708Decoder: content change between frames emits a cue boundary") {
        std::vector<uint8_t> bytes1 = dfw0(4, true);
        bytes1.push_back('A');
        Cea708Cdp::CcDataList trip1 = packetTriples(1, bytes1);

        // Second packet: clear and write 'B'.
        std::vector<uint8_t> bytes2 = {0x0C /*FF clear*/, 'B'};
        Cea708Cdp::CcDataList trip2 = packetTriples(1, bytes2);

        Cea708Decoder dec;
        const TimeStamp t0 = tsFromMs(0);
        const TimeStamp t1 = tsFromMs(500);
        dec.pushFrame(FrameNumber(0), t0, trip1);
        dec.pushFrame(FrameNumber(15), t1, trip2);
        CHECK(dec.displayedText() == "B");

        // Finalize at t2.
        const TimeStamp t2 = tsFromMs(1000);
        dec.pushFrame(FrameNumber(30), t2, Cea708Cdp::CcDataList());

        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 2);
        CHECK(out[0].text() == "A");
        CHECK(out[0].start() == t0);
        CHECK(out[0].end() == t1);
        CHECK(out[1].text() == "B");
        CHECK(out[1].start() == t1);
        CHECK(out[1].end() == t2);
}

// ============================================================================
// Service filtering
// ============================================================================

TEST_CASE("Cea708Decoder: service number filter ignores non-matching service blocks") {
        std::vector<uint8_t> bytes = dfw0(4, true);
        bytes.push_back('A');

        Cea708DtvccPacket pkt(0, {});
        pkt.serviceBlocks().pushToBack(makeService(2, bytes)); // not service 1!
        Cea708Cdp::CcDataList triples = pkt.toCcData();

        Cea708Decoder         dec; // default service 1
        dec.pushFrame(FrameNumber(0), tsFromMs(0), triples);
        CHECK(dec.displayedText() == "");
}

TEST_CASE("Cea708Decoder: configured service number selects the right block") {
        std::vector<uint8_t> bytes = dfw0(4, true);
        bytes.push_back('Z');

        Cea708DtvccPacket pkt(0, {});
        pkt.serviceBlocks().pushToBack(makeService(2, bytes));
        Cea708Cdp::CcDataList triples = pkt.toCcData();

        Cea708Decoder::Config cfg;
        cfg.serviceNumber = 2;
        Cea708Decoder dec(cfg);
        dec.pushFrame(FrameNumber(0), tsFromMs(0), triples);
        CHECK(dec.displayedText() == "Z");
}

// ============================================================================
// reset()
// ============================================================================

TEST_CASE("Cea708Decoder::reset drops in-flight cue and clears state") {
        std::vector<uint8_t> bytes = dfw0(4, true);
        bytes.push_back('A');
        Cea708Cdp::CcDataList trip = packetTriples(1, bytes);

        Cea708Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), trip);
        CHECK(dec.displayedText() == "A");
        dec.reset();
        CHECK(dec.displayedText() == "");
        CHECK(dec.finalize().isEmpty());
}

// ============================================================================
// Multi-triple packet reassembly
// ============================================================================

TEST_CASE("Cea708Decoder: multi-triple packet correctly reassembled across triples") {
        // Build a service block large enough to span multiple cc_data
        // triples.  Each triple carries 2 wire bytes; a 10-byte service
        // block + 1-byte service-header + 1-byte packet-header = 12
        // wire bytes = 6 triples.
        std::vector<uint8_t> bytes = dfw0(12, true);
        for (char c : std::string("ABCDEFGH")) bytes.push_back(static_cast<uint8_t>(c));
        Cea708Cdp::CcDataList trip = packetTriples(1, bytes);
        REQUIRE(trip.size() >= 3); // confirm multi-triple

        Cea708Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), trip);
        CHECK(dec.displayedText() == "ABCDEFGH");
}

TEST_CASE("Cea708Decoder: packet triples split across frames still reassemble") {
        // Same content, but split the triple list across two pushFrame
        // calls.  Each triple has 2 wire bytes; the decoder buffers the
        // in-flight packet until completion.
        std::vector<uint8_t> bytes = dfw0(12, true);
        for (char c : std::string("ABCDEFGH")) bytes.push_back(static_cast<uint8_t>(c));
        Cea708Cdp::CcDataList trip = packetTriples(1, bytes);
        REQUIRE(trip.size() >= 3);

        Cea708Cdp::CcDataList half1, half2;
        size_t                split = trip.size() / 2;
        for (size_t i = 0; i < split; ++i) half1.pushToBack(trip[i]);
        for (size_t i = split; i < trip.size(); ++i) half2.pushToBack(trip[i]);

        Cea708Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), half1);
        // Not complete yet — no cue visible.
        CHECK(dec.displayedText() == "");
        dec.pushFrame(FrameNumber(1), tsFromMs(33), half2);
        CHECK(dec.displayedText() == "ABCDEFGH");
}

// ============================================================================
// Window state accessor
// ============================================================================

TEST_CASE("Cea708Decoder::windowState exposes the underlying state") {
        std::vector<uint8_t> bytes = dfw0(4, true);
        bytes.push_back('A');
        Cea708Cdp::CcDataList trip = packetTriples(1, bytes);

        Cea708Decoder dec;
        dec.pushFrame(FrameNumber(0), tsFromMs(0), trip);
        const Cea708WindowState &ws = dec.windowState();
        CHECK(ws.anyVisible());
        CHECK(ws.window(0).defined);
}
