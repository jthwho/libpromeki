/**
 * @file      scc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/scc.h>
#include <promeki/string.h>
#include <promeki/timecode.h>

using namespace promeki;

namespace {

        /// @brief Builds a Scc::Line for tests with explicit timecode digits
        ///        (NDF unless @p drop) and a comma-separated list of byte
        ///        pairs in hex form.
        Scc::Line makeLine(int h, int m, int s, int f, bool drop,
                           std::initializer_list<uint16_t> pairs) {
                Scc::Line line;
                line.start = Timecode(drop ? Timecode::Mode(Timecode::DF30)
                                           : Timecode::Mode(Timecode::NDF30),
                                      static_cast<Timecode::DigitType>(h),
                                      static_cast<Timecode::DigitType>(m),
                                      static_cast<Timecode::DigitType>(s),
                                      static_cast<Timecode::DigitType>(f));
                for (uint16_t v : pairs) line.bytePairs.pushToBack(v);
                return line;
        }

} // namespace

// ============================================================================
// Construction / accessors
// ============================================================================

TEST_CASE("Scc: default constructed is empty") {
        Scc scc;
        CHECK(scc.isEmpty());
        CHECK(scc.size() == 0);
        CHECK(scc.lines().isEmpty());
}

TEST_CASE("Scc: append + size + iteration") {
        Scc scc;
        scc.append(makeLine(1, 0, 0, 0, false, {0x9420, 0x9420}));
        scc.append(makeLine(1, 0, 0, 2, false, {0x4142}));
        CHECK(scc.size() == 2);
        CHECK_FALSE(scc.isEmpty());
        CHECK(scc.lines()[0].bytePairs.size() == 2);
        CHECK(scc.lines()[1].bytePairs.size() == 1);
        CHECK(scc.lines()[1].bytePairs[0] == 0x4142);
}

// ============================================================================
// Parse
// ============================================================================

TEST_CASE("Scc::fromString: parses canonical 2-row file (NDF)") {
        String input = "Scenarist_SCC V1.0\r\n"
                       "\r\n"
                       "01:00:00:00\t9420 9420 947a 947a\r\n"
                       "01:00:02:00\t942c 942c\r\n";
        auto [scc, err] = Scc::fromString(input);
        REQUIRE(err.isOk());
        REQUIRE(scc.size() == 2);
        // Row 0.
        CHECK(scc.lines()[0].start.hour() == 1);
        CHECK(scc.lines()[0].start.min() == 0);
        CHECK(scc.lines()[0].start.sec() == 0);
        CHECK(scc.lines()[0].start.frame() == 0);
        CHECK_FALSE(scc.lines()[0].start.mode().isDropFrame());
        REQUIRE(scc.lines()[0].bytePairs.size() == 4);
        CHECK(scc.lines()[0].bytePairs[0] == 0x9420);
        CHECK(scc.lines()[0].bytePairs[1] == 0x9420);
        CHECK(scc.lines()[0].bytePairs[2] == 0x947a);
        CHECK(scc.lines()[0].bytePairs[3] == 0x947a);
        // Row 1.
        CHECK(scc.lines()[1].start.sec() == 2);
        REQUIRE(scc.lines()[1].bytePairs.size() == 2);
        CHECK(scc.lines()[1].bytePairs[0] == 0x942c);
}

TEST_CASE("Scc::fromString: parses drop-frame timecode (`;` before frame)") {
        String input = "Scenarist_SCC V1.0\r\n"
                       "\r\n"
                       "01:00:00;00\t9420 9420\r\n";
        auto [scc, err] = Scc::fromString(input);
        REQUIRE(err.isOk());
        REQUIRE(scc.size() == 1);
        CHECK(scc.lines()[0].start.mode().isDropFrame());
}

TEST_CASE("Scc::fromString: accepts LF-only line endings") {
        String input = "Scenarist_SCC V1.0\n\n01:00:00:00\t9420 9420\n";
        auto [scc, err] = Scc::fromString(input);
        REQUIRE(err.isOk());
        REQUIRE(scc.size() == 1);
        CHECK(scc.lines()[0].bytePairs.size() == 2);
}

TEST_CASE("Scc::fromString: tolerates trailing rows without newline") {
        String input = "Scenarist_SCC V1.0\n\n01:00:00:00\t9420 9420";
        auto [scc, err] = Scc::fromString(input);
        REQUIRE(err.isOk());
        REQUIRE(scc.size() == 1);
        CHECK(scc.lines()[0].bytePairs.size() == 2);
}

TEST_CASE("Scc::fromString: skips UTF-8 BOM") {
        // BOM bytes: 0xEF 0xBB 0xBF.
        uint8_t buf[] = {0xEF, 0xBB, 0xBF, 'S', 'c', 'e', 'n', 'a', 'r', 'i', 's', 't', '_', 'S',
                         'C', 'C', ' ', 'V', '1', '.', '0', '\r', '\n', '\r', '\n', '0', '1', ':',
                         '0', '0', ':', '0', '0', ':', '0', '0', '\t', '9', '4', '2', '0', '\r',
                         '\n'};
        auto [scc, err] = Scc::fromBuffer(buf, sizeof(buf));
        REQUIRE(err.isOk());
        REQUIRE(scc.size() == 1);
}

TEST_CASE("Scc::fromString: empty input returns empty Scc (no error)") {
        auto [scc, err] = Scc::fromString(String());
        REQUIRE(err.isOk());
        CHECK(scc.isEmpty());
}

TEST_CASE("Scc::fromString: missing header -> ParseFailed") {
        String input = "01:00:00:00\t9420\r\n";
        auto [scc, err] = Scc::fromString(input);
        CHECK(err.code() == Error::ParseFailed);
}

TEST_CASE("Scc::fromString: malformed timecode -> ParseFailed") {
        String input = "Scenarist_SCC V1.0\r\n\r\n"
                       "bogus\t9420\r\n";
        auto [scc, err] = Scc::fromString(input);
        CHECK(err.code() == Error::ParseFailed);
}

TEST_CASE("Scc::fromString: byte pair with non-hex digit -> ParseFailed") {
        String input = "Scenarist_SCC V1.0\r\n\r\n"
                       "01:00:00:00\t94zz\r\n";
        auto [scc, err] = Scc::fromString(input);
        CHECK(err.code() == Error::ParseFailed);
}

TEST_CASE("Scc::fromString: missing tab between TC and bytes -> ParseFailed") {
        String input = "Scenarist_SCC V1.0\r\n\r\n"
                       "01:00:00:009420\r\n";
        auto [scc, err] = Scc::fromString(input);
        CHECK(err.code() == Error::ParseFailed);
}

// ============================================================================
// Emit + round-trip
// ============================================================================

TEST_CASE("Scc::toString: emits canonical header + rows") {
        Scc scc;
        scc.append(makeLine(1, 0, 0, 0, false, {0x9420, 0x947a}));
        scc.append(makeLine(1, 0, 2, 0, true, {0x942c}));
        String out = scc.toString();
        CHECK(out.startsWith("Scenarist_SCC V1.0\r\n\r\n"));
        // NDF row uses ':' before frame.
        CHECK(out.contains("01:00:00:00\t9420 947a\r\n"));
        // DF row uses ';' before frame.
        CHECK(out.contains("01:00:02;00\t942c\r\n"));
}

TEST_CASE("Scc: round-trip through fromString / toString preserves byte pairs") {
        Scc orig;
        orig.append(makeLine(1, 0, 0, 0, false, {0x9420, 0x9420, 0x947a, 0x947a, 0x4142}));
        orig.append(makeLine(1, 0, 2, 15, true, {0x942c, 0x942c}));
        String txt = orig.toString();
        auto [parsed, err] = Scc::fromString(txt);
        REQUIRE(err.isOk());
        CHECK(parsed == orig);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("Scc: DataStream round-trip preserves rows") {
        Scc orig;
        orig.append(makeLine(1, 0, 0, 0, false, {0x9420, 0x947a, 0x4142}));
        orig.append(makeLine(1, 0, 1, 0, true, {0x942c}));

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << orig;
        }
        dev.seek(0);
        Scc got;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> got;
        }
        CHECK(got == orig);
}
