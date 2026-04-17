/**
 * @file      textstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <doctest/doctest.h>
#include <promeki/textstream.h>
#include <promeki/stringiodevice.h>
#include <promeki/fileiodevice.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>
#include <promeki/string.h>
#include <promeki/variant.h>

using namespace promeki;

static const size_t TestBufSize = 4096;

// ============================================================================
// String target: write and read back
// ============================================================================

TEST_CASE("TextStream: write to String target") {
        String str;
        {
                TextStream ts(&str);
                ts << "Hello" << ' ' << "World";
        }
        CHECK(str == "Hello World");
}

TEST_CASE("TextStream: read from String target") {
        String str("Hello World 42");
        TextStream ts(&str);
        String a, b;
        int c;
        ts >> a >> b >> c;
        CHECK(a == "Hello");
        CHECK(b == "World");
        CHECK(c == 42);
}

TEST_CASE("TextStream: readLine from String target") {
        String str("line1\nline2\nline3");
        TextStream ts(&str);
        CHECK(ts.readLine() == "line1");
        CHECK(ts.readLine() == "line2");
        CHECK(ts.readLine() == "line3");
}

TEST_CASE("TextStream: readLine handles CRLF") {
        String str("line1\r\nline2\r\n");
        TextStream ts(&str);
        CHECK(ts.readLine() == "line1");
        CHECK(ts.readLine() == "line2");
}

TEST_CASE("TextStream: readAll from String target") {
        String str("all the text");
        TextStream ts(&str);
        CHECK(ts.readAll() == "all the text");
}

TEST_CASE("TextStream: read with maxLength") {
        String str("abcdefghij");
        TextStream ts(&str);
        CHECK(ts.read(5) == "abcde");
        CHECK(ts.read(5) == "fghij");
}

TEST_CASE("TextStream: atEnd on String target") {
        String str("ab");
        TextStream ts(&str);
        CHECK_FALSE(ts.atEnd());
        ts.read(2);
        CHECK(ts.atEnd());
}

// ============================================================================
// Buffer target
// ============================================================================

TEST_CASE("TextStream: write to Buffer and read back") {
        Buffer buf(TestBufSize);
        {
                TextStream ts(&buf);
                ts << "Hello " << 42 << " " << 3.14;
        }
        // Read back by creating a new TextStream on the same buffer
        // Need to reset the buffer's position
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadOnly);
        TextStream ts(&dev);
        String word;
        int num;
        double dbl;
        ts >> word >> num >> dbl;
        CHECK(word == "Hello");
        CHECK(num == 42);
        CHECK(dbl == doctest::Approx(3.14));
}

// ============================================================================
// IODevice target
// ============================================================================

TEST_CASE("TextStream: write/read via BufferIODevice") {
        Buffer buf(TestBufSize);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                TextStream ts(&dev);
                ts << "test " << 123 << " " << true;
        }
        dev.seek(0);
        {
                TextStream ts(&dev);
                String a;
                int b;
                String c;
                ts >> a >> b >> c;
                CHECK(a == "test");
                CHECK(b == 123);
                CHECK(c == "true");
        }
}

// ============================================================================
// Write operators: primitive types
// ============================================================================

TEST_CASE("TextStream: write int") {
        String str;
        TextStream ts(&str);
        ts << 42;
        CHECK(str == "42");
}

TEST_CASE("TextStream: write negative int") {
        String str;
        TextStream ts(&str);
        ts << -99;
        CHECK(str == "-99");
}

TEST_CASE("TextStream: write unsigned int") {
        String str;
        TextStream ts(&str);
        ts << 4294967295u;
        CHECK(str == "4294967295");
}

TEST_CASE("TextStream: write int64_t") {
        String str;
        TextStream ts(&str);
        ts << static_cast<int64_t>(9876543210LL);
        CHECK(str == "9876543210");
}

TEST_CASE("TextStream: write uint64_t") {
        String str;
        TextStream ts(&str);
        ts << static_cast<uint64_t>(18446744073709551615ULL);
        CHECK(str == "18446744073709551615");
}

TEST_CASE("TextStream: write bool true/false") {
        String str;
        TextStream ts(&str);
        ts << true << " " << false;
        CHECK(str == "true false");
}

TEST_CASE("TextStream: write char") {
        String str;
        TextStream ts(&str);
        ts << 'A' << 'B' << 'C';
        CHECK(str == "ABC");
}

TEST_CASE("TextStream: write const char *") {
        String str;
        TextStream ts(&str);
        ts << "hello";
        CHECK(str == "hello");
}

TEST_CASE("TextStream: write nullptr const char *") {
        String str;
        TextStream ts(&str);
        ts << static_cast<const char *>(nullptr);
        CHECK(str.isEmpty());
}

// ============================================================================
// Formatting: field width and alignment
// ============================================================================

TEST_CASE("TextStream: field width right-aligned (default)") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(10);
        ts << "hi";
        CHECK(str == "        hi");
}

TEST_CASE("TextStream: field width left-aligned") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(10);
        ts.setFieldAlignment(TextStream::Left);
        ts << "hi";
        CHECK(str == "hi        ");
}

TEST_CASE("TextStream: field width center-aligned") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(10);
        ts.setFieldAlignment(TextStream::Center);
        ts << "hi";
        CHECK(str == "    hi    ");
}

TEST_CASE("TextStream: custom pad character") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(6);
        ts.setPadChar('0');
        ts << 42;
        CHECK(str == "000042");
}

TEST_CASE("TextStream: field width with value longer than width") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(3);
        ts << "hello";
        CHECK(str == "hello");
}

// ============================================================================
// Integer base formatting
// ============================================================================

TEST_CASE("TextStream: hex output") {
        String str;
        TextStream ts(&str);
        ts << hex << 255;
        CHECK(str == "FF");
}

TEST_CASE("TextStream: oct output") {
        String str;
        TextStream ts(&str);
        ts << oct << 255;
        CHECK(str == "377");
}

TEST_CASE("TextStream: bin output") {
        String str;
        TextStream ts(&str);
        ts << bin << 10;
        CHECK(str == "1010");
}

TEST_CASE("TextStream: dec output after hex") {
        String str;
        TextStream ts(&str);
        ts << hex << 255 << " " << dec << 255;
        CHECK(str == "FF 255");
}

// ============================================================================
// Float precision and notation
// ============================================================================

TEST_CASE("TextStream: default float precision") {
        String str;
        TextStream ts(&str);
        ts << 3.14159;
        CHECK(str == "3.14159");
}

TEST_CASE("TextStream: fixed notation with precision") {
        String str;
        TextStream ts(&str);
        ts << fixed;
        ts.setRealNumberPrecision(2);
        ts << 3.14159;
        CHECK(str == "3.14");
}

TEST_CASE("TextStream: scientific notation") {
        String str;
        TextStream ts(&str);
        ts << scientific;
        ts.setRealNumberPrecision(2);
        ts << 3.14159;
        // Should contain "e+" or "E+"
        CHECK(str.length() > 0);
        String result = str;
        // Check it looks like scientific notation
        CHECK(result == "3.14e+00");
}

// ============================================================================
// Manipulators
// ============================================================================

TEST_CASE("TextStream: endl manipulator") {
        String str;
        TextStream ts(&str);
        ts << "line1" << endl << "line2";
        CHECK(str == "line1\nline2");
}

TEST_CASE("TextStream: manipulator chaining") {
        String str;
        TextStream ts(&str);
        ts << hex << 255 << " " << dec << 10;
        CHECK(str == "FF 10");
}

TEST_CASE("TextStream: left/right/center manipulators") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(5);
        ts << left << "ab";
        CHECK(str == "ab   ");
}

// ============================================================================
// Variant output
// ============================================================================

TEST_CASE("TextStream: write Variant") {
        String str;
        TextStream ts(&str);
        Variant v(String("hello"));
        ts << v;
        CHECK(str == "hello");
}

TEST_CASE("TextStream: write Variant int") {
        String str;
        TextStream ts(&str);
        Variant v(static_cast<int32_t>(42));
        ts << v;
        CHECK(str == "42");
}

// ============================================================================
// Read operators
// ============================================================================

TEST_CASE("TextStream: read int from text") {
        String str("123 -456");
        TextStream ts(&str);
        int a, b;
        ts >> a >> b;
        CHECK(a == 123);
        CHECK(b == -456);
}

TEST_CASE("TextStream: read double from text") {
        String str("3.14 -2.5");
        TextStream ts(&str);
        double a, b;
        ts >> a >> b;
        CHECK(a == doctest::Approx(3.14));
        CHECK(b == doctest::Approx(-2.5));
}

TEST_CASE("TextStream: read char") {
        String str("ABC");
        TextStream ts(&str);
        char a, b, c;
        ts >> a >> b >> c;
        CHECK(a == 'A');
        CHECK(b == 'B');
        CHECK(c == 'C');
}

TEST_CASE("TextStream: read past end sets status") {
        String str("");
        TextStream ts(&str);
        String val;
        ts >> val;
        CHECK(val.isEmpty());
}

// ============================================================================
// Status
// ============================================================================

TEST_CASE("TextStream: default status is Ok") {
        String str;
        TextStream ts(&str);
        CHECK(ts.status() == TextStream::Ok);
}

TEST_CASE("TextStream: resetStatus") {
        String str("");
        TextStream ts(&str);
        char ch;
        ts >> ch; // Should fail
        CHECK(ts.status() == TextStream::ReadPastEnd);
        ts.resetStatus();
        CHECK(ts.status() == TextStream::Ok);
}

// ============================================================================
// Round-trip: write primitives, read back
// ============================================================================

TEST_CASE("TextStream: round-trip primitives via String") {
        String str;
        {
                TextStream ts(&str);
                ts << 42 << " " << 3.14 << " " << "hello" << " " << true;
        }
        {
                TextStream ts(&str);
                int a;
                double b;
                String c, d;
                ts >> a >> b >> c >> d;
                CHECK(a == 42);
                CHECK(b == doctest::Approx(3.14));
                CHECK(c == "hello");
                CHECK(d == "true");
        }
}

// ============================================================================
// Encoding (basic verification)
// ============================================================================

TEST_CASE("TextStream: default encoding is UTF-8") {
        String str;
        TextStream ts(&str);
        CHECK(ts.encoding() == "UTF-8");
}

TEST_CASE("TextStream: set encoding") {
        String str;
        TextStream ts(&str);
        ts.setEncoding("Latin-1");
        CHECK(ts.encoding() == "Latin-1");
}

// ============================================================================
// FILE* target
// ============================================================================

TEST_CASE("TextStream: FILE* constructor write and read back") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        {
                TextStream ts(f);
                ts << "file hello " << 99;
        }
        std::rewind(f);
        char buf[32] = {};
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        CHECK(std::string(buf) == "file hello 99");
        std::fclose(f);
}

// ============================================================================
// device() accessor
// ============================================================================

TEST_CASE("TextStream: device accessor returns IODevice") {
        String str;
        TextStream ts(&str);
        CHECK(ts.device() != nullptr);
}

// ============================================================================
// flush
// ============================================================================

TEST_CASE("TextStream: flush does not crash") {
        String str;
        TextStream ts(&str);
        ts << "hello";
        ts << flush;
        CHECK(str == "hello");
}

// ============================================================================
// Float (not double)
// ============================================================================

TEST_CASE("TextStream: write float") {
        String str;
        TextStream ts(&str);
        ts << 2.5f;
        CHECK(str == "2.5");
}

TEST_CASE("TextStream: write float fixed notation") {
        String str;
        TextStream ts(&str);
        ts << fixed;
        ts.setRealNumberPrecision(3);
        ts << 1.5f;
        CHECK(str == "1.500");
}

TEST_CASE("TextStream: write float scientific notation") {
        String str;
        TextStream ts(&str);
        ts << scientific;
        ts.setRealNumberPrecision(1);
        ts << 1.5f;
        CHECK(str == "1.5e+00");
}

// ============================================================================
// Integer base: unsigned int, int64_t, uint64_t
// ============================================================================

TEST_CASE("TextStream: hex unsigned int") {
        String str;
        TextStream ts(&str);
        ts << hex << 255u;
        CHECK(str == "FF");
}

TEST_CASE("TextStream: oct unsigned int") {
        String str;
        TextStream ts(&str);
        ts << oct << 255u;
        CHECK(str == "377");
}

TEST_CASE("TextStream: bin unsigned int") {
        String str;
        TextStream ts(&str);
        ts << bin << 10u;
        CHECK(str == "1010");
}

TEST_CASE("TextStream: hex int64_t") {
        String str;
        TextStream ts(&str);
        ts << hex << static_cast<int64_t>(255);
        CHECK(str == "FF");
}

TEST_CASE("TextStream: oct int64_t") {
        String str;
        TextStream ts(&str);
        ts << oct << static_cast<int64_t>(255);
        CHECK(str == "377");
}

TEST_CASE("TextStream: bin int64_t") {
        String str;
        TextStream ts(&str);
        ts << bin << static_cast<int64_t>(10);
        CHECK(str == "1010");
}

TEST_CASE("TextStream: hex uint64_t") {
        String str;
        TextStream ts(&str);
        ts << hex << static_cast<uint64_t>(255);
        CHECK(str == "FF");
}

TEST_CASE("TextStream: oct uint64_t") {
        String str;
        TextStream ts(&str);
        ts << oct << static_cast<uint64_t>(255);
        CHECK(str == "377");
}

TEST_CASE("TextStream: bin uint64_t") {
        String str;
        TextStream ts(&str);
        ts << bin << static_cast<uint64_t>(10);
        CHECK(str == "1010");
}

TEST_CASE("TextStream: bin zero") {
        String str;
        TextStream ts(&str);
        ts << bin << 0;
        CHECK(str == "0");
}

TEST_CASE("TextStream: bin zero unsigned int") {
        String str;
        TextStream ts(&str);
        ts << bin << 0u;
        CHECK(str == "0");
}

TEST_CASE("TextStream: bin zero int64_t") {
        String str;
        TextStream ts(&str);
        ts << bin << static_cast<int64_t>(0);
        CHECK(str == "0");
}

TEST_CASE("TextStream: bin zero uint64_t") {
        String str;
        TextStream ts(&str);
        ts << bin << static_cast<uint64_t>(0);
        CHECK(str == "0");
}

// ============================================================================
// Read int64_t
// ============================================================================

TEST_CASE("TextStream: read int64_t") {
        String str("123456789 -42");
        TextStream ts(&str);
        int64_t a, b;
        ts >> a >> b;
        CHECK(a == 123456789);
        CHECK(b == -42);
}

TEST_CASE("TextStream: read int64_t from empty returns 0") {
        String str("");
        TextStream ts(&str);
        int64_t val;
        ts >> val;
        CHECK(val == 0);
}

TEST_CASE("TextStream: read double from empty returns 0") {
        String str("");
        TextStream ts(&str);
        double val;
        ts >> val;
        CHECK(val == 0.0);
}

TEST_CASE("TextStream: read int from empty returns 0") {
        String str("");
        TextStream ts(&str);
        int val;
        ts >> val;
        CHECK(val == 0);
}

// ============================================================================
// readLine edge cases
// ============================================================================

TEST_CASE("TextStream: readLine with bare CR") {
        String str("line1\rline2");
        TextStream ts(&str);
        CHECK(ts.readLine() == "line1");
        CHECK(ts.readLine() == "line2");
}

TEST_CASE("TextStream: readLine at end of stream") {
        String str("");
        TextStream ts(&str);
        String line = ts.readLine();
        CHECK(line.isEmpty());
}

TEST_CASE("TextStream: readLine with CR at end of stream") {
        String str("hello\r");
        TextStream ts(&str);
        CHECK(ts.readLine() == "hello");
}

// ============================================================================
// Formatting getters
// ============================================================================

TEST_CASE("TextStream: formatting defaults") {
        String str;
        TextStream ts(&str);
        CHECK(ts.fieldWidth() == 0);
        CHECK(ts.fieldAlignment() == TextStream::Right);
        CHECK(ts.padChar() == ' ');
        CHECK(ts.integerBase() == 10);
        CHECK(ts.realNumberPrecision() == 6);
        CHECK(ts.realNumberNotation() == TextStream::SmartNotation);
}

// ============================================================================
// Center alignment with odd padding
// ============================================================================

TEST_CASE("TextStream: center alignment odd padding") {
        String str;
        TextStream ts(&str);
        ts.setFieldWidth(7);
        ts << center << "ab";
        // 5 pad total: left=2, right=3
        CHECK(str == "  ab   ");
}
