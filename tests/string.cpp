/**
 * @file      string.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <doctest/doctest.h>
#include <promeki/char.h>
#include <promeki/enums.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/uuid.h>
#include <promeki/rational.h>
#include <promeki/timecode.h>
#include <promeki/size2d.h>
#include <promeki/point.h>
#include <promeki/duration.h>
#include <promeki/audiolevel.h>
#include <promeki/framerate.h>
#include <promeki/ipv4address.h>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// Char tests
// ============================================================================

TEST_CASE("Char_Construction") {
        Char null;
        CHECK(null.isNull());
        CHECK(null.codepoint() == 0);

        Char a('A');
        CHECK(a.codepoint() == 'A');
        CHECK(a.isAscii());
        CHECK(a.isAlpha());
        CHECK(a.isUpper());
        CHECK(!a.isLower());
        CHECK(!a.isDigit());

        Char d('5');
        CHECK(d.isDigit());
        CHECK(!d.isAlpha());
        CHECK(d.isAscii());

        Char space(' ');
        CHECK(space.isSpace());
        CHECK(space.isPrintable());

        // Unicode codepoint
        Char euro(static_cast<char32_t>(0x20AC));
        CHECK(!euro.isAscii());
        CHECK(euro.codepoint() == 0x20AC);
}

TEST_CASE("Char_CaseConversion") {
        CHECK(Char('a').toUpper() == Char('A'));
        CHECK(Char('A').toLower() == Char('a'));
        CHECK(Char('Z').toLower() == Char('z'));
        CHECK(Char('5').toUpper() == Char('5'));
}

TEST_CASE("Char_Utf8RoundTrip") {
        // ASCII
        Char a('H');
        char buf[4];
        size_t n = a.toUtf8(buf);
        CHECK(n == 1);
        CHECK(buf[0] == 'H');
        size_t br;
        Char decoded = Char::fromUtf8(buf, &br);
        CHECK(decoded == a);
        CHECK(br == 1);

        // 2-byte: é (U+00E9)
        Char e(static_cast<char32_t>(0xE9));
        n = e.toUtf8(buf);
        CHECK(n == 2);
        decoded = Char::fromUtf8(buf, &br);
        CHECK(decoded == e);
        CHECK(br == 2);

        // 3-byte: € (U+20AC)
        Char euro(static_cast<char32_t>(0x20AC));
        n = euro.toUtf8(buf);
        CHECK(n == 3);
        decoded = Char::fromUtf8(buf, &br);
        CHECK(decoded == euro);
        CHECK(br == 3);

        // 4-byte: 😀 (U+1F600)
        Char emoji(static_cast<char32_t>(0x1F600));
        n = emoji.toUtf8(buf);
        CHECK(n == 4);
        decoded = Char::fromUtf8(buf, &br);
        CHECK(decoded == emoji);
        CHECK(br == 4);
}

TEST_CASE("Char_IsHexDigit") {
        CHECK(Char('0').isHexDigit());
        CHECK(Char('9').isHexDigit());
        CHECK(Char('a').isHexDigit());
        CHECK(Char('f').isHexDigit());
        CHECK(Char('A').isHexDigit());
        CHECK(Char('F').isHexDigit());
        CHECK_FALSE(Char('g').isHexDigit());
        CHECK_FALSE(Char('G').isHexDigit());
        CHECK_FALSE(Char(' ').isHexDigit());
        CHECK_FALSE(Char('/').isHexDigit());
        CHECK_FALSE(Char(':').isHexDigit());
}

TEST_CASE("Char_HexDigitValue") {
        CHECK(Char('0').hexDigitValue() == 0);
        CHECK(Char('9').hexDigitValue() == 9);
        CHECK(Char('a').hexDigitValue() == 10);
        CHECK(Char('f').hexDigitValue() == 15);
        CHECK(Char('A').hexDigitValue() == 10);
        CHECK(Char('F').hexDigitValue() == 15);
        CHECK(Char('g').hexDigitValue() == -1);
        CHECK(Char(' ').hexDigitValue() == -1);
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("String_Construction") {
        // Default construction
        String nullstr;
        CHECK(nullstr.isEmpty());
        CHECK(nullstr.size() == 0);

        // From C string
        String s1 = "Hello";
        CHECK(s1 == "Hello");
        CHECK(s1.size() == 5);

        // From nullptr
        String s2(nullptr);
        CHECK(s2.isEmpty());

        // From C string with length
        String s3("Hello World", 5);
        CHECK(s3 == "Hello");

        // From repeated char
        String s4(5, 'x');
        CHECK(s4 == "xxxxx");

        // From std::string
        std::string stdstr = "StdString";
        String s5(stdstr);
        CHECK(s5 == "StdString");

        // From std::string rvalue
        String s6(std::string("Moved"));
        CHECK(s6 == "Moved");
}

// ============================================================================
// Copy semantics (COW)
// ============================================================================

TEST_CASE("String_CopyOnWrite") {
        String s1 = "Original";
        String s2 = s1;

        // Copy shares data
        CHECK(s1.referenceCount() == 2);
        CHECK(s1 == s2);

        // Mutation triggers COW
        s2.setCharAt(0, Char('X'));
        CHECK(s1 == "Original");
        CHECK(s2 == "Xriginal");
        CHECK(s1.referenceCount() == 1);
        CHECK(s2.referenceCount() == 1);
}

TEST_CASE("String_CopyAppendIndependent") {
        String s1 = "Hello";
        String s2 = s1;

        s2 += " World";
        CHECK(s1 == "Hello");
        CHECK(s2 == "Hello World");
}

TEST_CASE("String_CopyClearIndependent") {
        String s1 = "Hello";
        String s2 = s1;

        s2.clear();
        CHECK(s1 == "Hello");
        CHECK(s2.isEmpty());
}

TEST_CASE("String_CopyAssignIndependent") {
        String s1 = "Hello";
        String s2 = s1;

        s2 = "Goodbye";
        CHECK(s1 == "Hello");
        CHECK(s2 == "Goodbye");
}

// ============================================================================
// String operations
// ============================================================================

TEST_CASE("String_Operations") {
        String s1 = "String 1";
        String s2("String 2");
        String s3(s2);
        String s4(" \t \n \r WHITE \n\t\nSPACE  \t\n\t\t\n   ");
        String s5("item1,item2,item3");

        CHECK(s1.toUpper() == "STRING 1");
        CHECK(s1.toLower() == "string 1");
        CHECK(s1 == "String 1");
        CHECK(s2 == "String 2");
        CHECK(s3 == "String 2");
        CHECK(s2 == s3);
        CHECK(s4.trim() == "WHITE \n\t\nSPACE");
        auto s5split = s5.split(",");
        CHECK(s5split.size() == 3);
        CHECK(s5split.at(0) == "item1");
        CHECK(s5split.at(1) == "item2");
        CHECK(s5split.at(2) == "item3");
        CHECK(s1.startsWith("Stri"));
        CHECK(!s1.startsWith("StrI"));
        CHECK(s1.endsWith("g 1"));
        CHECK(!s1.endsWith("gg1"));
        CHECK(s5.count("item") == 3);
        CHECK(s1.reverse() == "1 gnirtS");
        CHECK(String("1234").isNumeric());
        CHECK(!s1.isNumeric());
        CHECK(String::dec(1234) == "1234");
        CHECK(String::dec(1234, 6) == "  1234");
        CHECK(String::dec(1234, 6, 'x') == "xx1234");
        CHECK(String::hex(0x42, 4) == "0x0042");
        CHECK(String::bin(0b11111, 8) == "0b00011111");
        CHECK(String::number(42435) == "42435");
        CHECK(String::number(0x3472, 16) == "3472");
        CHECK(String::number(12345, 10, 10) == "     12345");
        CHECK(String::number(12345, 10, -10) == "12345     ");
        CHECK(String::number(12345, 20, 10, ' ', true) == "  b20:1AH5");
        CHECK(String::number(12345, 4, 6, ' ', true) == "b4:3000321");
        // Negative integers get a leading minus sign.
        CHECK(String::number(static_cast<int32_t>(-42)) == "-42");
        CHECK(String::number(static_cast<int64_t>(-1)) == "-1");
        CHECK(String::number(static_cast<int16_t>(-1000)) == "-1000");
        CHECK(String::number(static_cast<int8_t>(-128)) == "-128");
        // INT_MIN edge case: naive "val = -val" is UB for INT_MIN since the
        // magnitude is unrepresentable in the signed type, so the absolute
        // value is computed in unsigned space.
        CHECK(String::number(std::numeric_limits<int8_t>::min()) == "-128");
        CHECK(String::number(std::numeric_limits<int16_t>::min()) == "-32768");
        CHECK(String::number(std::numeric_limits<int32_t>::min()) == "-2147483648");
        CHECK(String::number(std::numeric_limits<int64_t>::min()) == "-9223372036854775808");
        // Space-padded negatives right-align like printf("%5d", -42).
        CHECK(String::number(static_cast<int32_t>(-42), 10, 5) == "  -42");
        CHECK(String("%3 %2 %1").arg(3).arg(2).arg(1) == "1 2 3");
        CHECK(String("Two hundred and twenty-six billion, four hundred eighty-three million, One Hundred And Thirty-Four Thousand Two Hundred and Ninety-Six").parseNumberWords() == 226483134296);
}

// ============================================================================
// Operator tests
// ============================================================================

TEST_CASE("String_Concatenation") {
        String a = "Hello";
        String b = " World";

        String c = a + b;
        CHECK(c == "Hello World");
        CHECK(a == "Hello");
        CHECK(b == " World");

        String d = a + " There";
        CHECK(d == "Hello There");

        String e = a + '!';
        CHECK(e == "Hello!");

        String f = a + std::string(" Std");
        CHECK(f == "Hello Std");
}

TEST_CASE("String_AppendOperators") {
        String s = "Hello";

        s += " World";
        CHECK(s == "Hello World");

        s += std::string("!");
        CHECK(s == "Hello World!");

        s += '?';
        CHECK(s == "Hello World!?");

        String suffix = " End";
        s += suffix;
        CHECK(s == "Hello World!? End");
}

TEST_CASE("String_Comparison") {
        String a = "abc";
        String b = "abc";
        String c = "def";

        CHECK(a == b);
        CHECK(a != c);
        CHECK(a == "abc");
        CHECK(a != "def");

        // Single char comparison
        String single = "x";
        CHECK(single == 'x');
        CHECK(single != 'y');
        String multi = "xx";
        CHECK(multi != 'x');

        // operator<
        CHECK(a < c);
}

TEST_CASE("String_CharAt") {
        String s = "Hello";
        CHECK(s.charAt(0) == Char('H'));
        CHECK(s.charAt(4) == Char('o'));

        // setCharAt (COW)
        String s2 = s;
        s2.setCharAt(0, Char('J'));
        CHECK(s == "Hello");
        CHECK(s2 == "Jello");
}

TEST_CASE("String_ByteAt") {
        String s = "Hello";
        CHECK(s.byteAt(0) == 'H');
        CHECK(s.byteAt(4) == 'o');
        CHECK(s.byteCount() == 5);
}

// ============================================================================
// Assignment
// ============================================================================

TEST_CASE("String_Assignment") {
        String s;

        s = "Hello";
        CHECK(s == "Hello");

        s = std::string("World");
        CHECK(s == "World");

        std::string tmp = "Moved";
        s = std::move(tmp);
        CHECK(s == "Moved");

        String other = "Other";
        s = other;
        CHECK(s == "Other");
}

// ============================================================================
// Substring operations
// ============================================================================

TEST_CASE("String_Substrings") {
        String s = "Hello World";

        CHECK(s.substr(0, 5) == "Hello");
        CHECK(s.substr(6) == "World");
        CHECK(s.mid(6, 5) == "World");
        CHECK(s.left(5) == "Hello");
        CHECK(s.right(5) == "World");
        CHECK(s.find('W') == 6);
}

TEST_CASE("String_Truncated") {
        String s = "Hello World";

        SUBCASE("No truncation when string fits") {
                CHECK(s.truncated(11) == "Hello World");
                CHECK(s.truncated(20) == "Hello World");
        }

        SUBCASE("Truncation with ellipsis") {
                CHECK(s.truncated(8) == "Hello...");
                CHECK(s.truncated(4) == "H...");
                CHECK(s.truncated(3) == "...");
        }

        SUBCASE("Hard truncation when maxChars < 3") {
                CHECK(s.truncated(2) == "He");
                CHECK(s.truncated(1) == "H");
                CHECK(s.truncated(0) == "");
        }

        SUBCASE("Empty string") {
                String empty;
                CHECK(empty.truncated(5) == "");
                CHECK(empty.truncated(0) == "");
        }
}

// ============================================================================
// Find and Contains
// ============================================================================

TEST_CASE("String_FindSubstring") {
        String s = "Hello World";

        CHECK(s.find(String("World")) == 6);
        CHECK(s.find(String("Hello")) == 0);
        CHECK(s.find(String("xyz")) == String::npos);
        CHECK(s.find(String("")) == 0);
}

TEST_CASE("String_Contains") {
        String s = "Hello World";

        SUBCASE("Contains char") {
                CHECK(s.contains('H'));
                CHECK(s.contains('W'));
                CHECK(s.contains(' '));
                CHECK_FALSE(s.contains('z'));
        }

        SUBCASE("Contains String") {
                CHECK(s.contains(String("Hello")));
                CHECK(s.contains(String("World")));
                CHECK(s.contains(String("lo Wo")));
                CHECK_FALSE(s.contains(String("xyz")));
        }

        SUBCASE("Contains C string") {
                CHECK(s.contains("Hello"));
                CHECK(s.contains("World"));
                CHECK(s.contains("lo Wo"));
                CHECK_FALSE(s.contains("xyz"));
        }

        SUBCASE("Empty string") {
                String empty;
                CHECK_FALSE(empty.contains('a'));
                CHECK_FALSE(empty.contains("abc"));
                CHECK(s.contains(""));
        }
}

// ============================================================================
// Conversion operators
// ============================================================================

TEST_CASE("String_Conversions") {
        String s = "Test";

        const std::string &ref = s.str();
        CHECK(ref == "Test");

        const char *cstr = s.cstr();
        CHECK(std::string(cstr) == "Test");

        const char *cstr2 = s;
        CHECK(std::string(cstr2) == "Test");
}

// ============================================================================
// Character iterators
// ============================================================================

TEST_CASE("String_CharIterators") {
        const String s = "Hello";

        // Basic iteration
        std::string result;
        for(auto it = s.begin(); it != s.end(); ++it) {
                char buf[4];
                size_t n = (*it).toUtf8(buf);
                result.append(buf, n);
        }
        CHECK(result == "Hello");

        // Range-for
        size_t count = 0;
        for(Char c : s) {
                CHECK(c == s.charAt(count));
                ++count;
        }
        CHECK(count == 5);

        // Random access
        auto it = s.begin();
        CHECK(it[0] == Char('H'));
        CHECK(it[4] == Char('o'));
        CHECK((it + 2) - it == 2);
        CHECK(*(it + 1) == Char('e'));

        // Unicode iteration
        String unicode = String::fromUtf8("caf\xc3\xa9", 5);
        std::vector<char32_t> cps;
        for(Char c : unicode) {
                cps.push_back(c.codepoint());
        }
        CHECK(cps.size() == 4);
        CHECK(cps[0] == 'c');
        CHECK(cps[1] == 'a');
        CHECK(cps[2] == 'f');
        CHECK(cps[3] == 0xE9);
}

// ============================================================================
// Resize and clear
// ============================================================================

TEST_CASE("String_ResizeAndClear") {
        String s = "Hello";
        s.resize(3);
        CHECK(s == "Hel");
        CHECK(s.size() == 3);

        s.clear();
        CHECK(s.isEmpty());
        CHECK(s.size() == 0);
}

// ============================================================================
// startsWith / endsWith edge cases
// ============================================================================

TEST_CASE("String_StartsEndsEdgeCases") {
        String s = "Hello";

        CHECK(s.startsWith("Hello"));
        CHECK(s.startsWith(""));
        CHECK(s.startsWith('H'));
        CHECK(!s.startsWith('h'));

        CHECK(s.endsWith("Hello"));
        CHECK(s.endsWith("o"));
        CHECK(!s.endsWith("Hello World"));

        String empty;
        CHECK(!empty.startsWith('x'));
}

// ============================================================================
// toUpper / toLower don't mutate original
// ============================================================================

TEST_CASE("String_CaseConversion") {
        String s = "Hello World";
        String upper = s.toUpper();
        String lower = s.toLower();

        CHECK(s == "Hello World");
        CHECK(upper == "HELLO WORLD");
        CHECK(lower == "hello world");
}

TEST_CASE("String_CaseConversionLatin1HighBytes") {
        // Regression: the Latin1 toUpper/toLower fast path used to call
        // ::toupper / ::tolower from <cctype>, which is locale-dependent.
        // In modern UTF-8 locales those functions leave high bytes unchanged,
        // so 'é' (0xE9) stayed lowercase even though Char::toUpper would
        // correctly fold it to 'É' (0xC9).  The fix routes both Latin1 and
        // Unicode storage through Char::toUpper/Char::toLower for consistent,
        // locale-independent results.
        String latin1Lower(1, '\xe9');  // 'é' as a single Latin1 byte
        REQUIRE(latin1Lower.encoding() == String::Latin1);
        REQUIRE(latin1Lower.length() == 1);
        REQUIRE(latin1Lower.charAt(0).codepoint() == 0xE9);

        String latin1Upper = latin1Lower.toUpper();
        CHECK(latin1Upper.length() == 1);
        CHECK(latin1Upper.charAt(0).codepoint() == 0xC9);  // 'É'

        String latin1RoundTrip = latin1Upper.toLower();
        CHECK(latin1RoundTrip.charAt(0).codepoint() == 0xE9);

        // Unicode storage with the same logical content folds identically.
        String unicodeLower = String::fromUtf8("\xc3\xa9", 2);
        REQUIRE(unicodeLower.encoding() == String::Unicode);
        String unicodeUpper = unicodeLower.toUpper();
        CHECK(unicodeUpper.length() == 1);
        CHECK(unicodeUpper.charAt(0).codepoint() == 0xC9);

        // Cross-encoding equality after case folding.
        CHECK(latin1Upper == unicodeUpper);
}

// ============================================================================
// trim edge cases
// ============================================================================

TEST_CASE("String_TrimEdgeCases") {
        CHECK(String("  hello  ").trim() == "hello");
        CHECK(String("hello").trim() == "hello");
        CHECK(String("   ").trim() == "");
        CHECK(String("").trim() == "");
}

// ============================================================================
// sprintf
// ============================================================================

TEST_CASE("String_Sprintf") {
        String s = String::sprintf("Hello %s %d", "World", 42);
        CHECK(s == "Hello World 42");

        String s2 = String::sprintf("%05d", 42);
        CHECK(s2 == "00042");
}

// ============================================================================
// format / vformat (C++20 std::format wrapper)
// ============================================================================

TEST_CASE("String_Format_Basic") {
        // Basic positional substitution.
        CHECK(String::format("hello, {}!", "world") == "hello, world!");

        // Multiple arguments of different types.
        CHECK(String::format("{} + {} = {}", 2, 3, 5) == "2 + 3 = 5");

        // Numeric format specifiers (precision, width, fill).
        CHECK(String::format("{:.3f}", 3.14159) == "3.142");
        CHECK(String::format("{:>8}", 42) == "      42");
        CHECK(String::format("{:0>5}", 7) == "00007");
        CHECK(String::format("{:#x}", 255) == "0xff");

        // Empty format string.
        CHECK(String::format("") == "");

        // Format string with no arguments.
        CHECK(String::format("static text") == "static text");

        // Result for a pure-ASCII format takes the Latin1 fast path.
        String ascii = String::format("count = {}", 5);
        CHECK(ascii.encoding() == String::Latin1);
}

TEST_CASE("String_Format_PromekiTypes") {
        // promeki::String can be passed as a format argument via the
        // std::formatter<promeki::String> specialization.
        String name = "Alice";
        CHECK(String::format("hello, {}!", name) == "hello, Alice!");

        // Standard string-format specifiers (width / alignment / fill) work
        // because the formatter inherits from std::formatter<string_view>.
        CHECK(String::format("[{:>10}]", String("hi")) == "[        hi]");
        CHECK(String::format("[{:*<6}]", String("ab")) == "[ab****]");

        // Multiple promeki::String arguments.
        String greet = "hello";
        String who   = "world";
        CHECK(String::format("{}, {}!", greet, who) == "hello, world!");

        // promeki::Char formats as its UTF-8 byte sequence.
        Char a('A');
        CHECK(String::format("char = {}", a) == "char = A");

        // Multi-byte Char (U+00E9 'é') round-trips correctly.
        Char eAcute(static_cast<char32_t>(0xE9));
        String formatted = String::format("char = {}", eAcute);
        // The result contains the UTF-8 bytes of 'é'.
        CHECK(formatted == String::fromUtf8("char = \xc3\xa9", 9));
}

TEST_CASE("String_Format_UnicodeContent") {
        // A format result containing multi-byte UTF-8 sequences should land
        // in Unicode storage (via fromUtf8), and round-trip cleanly through
        // operator==.
        String name = String::fromUtf8("caf\xc3\xa9", 5);  // "café"
        String s = String::format("name = {}", name);
        CHECK(s.encoding() == String::Unicode);
        CHECK(s == String::fromUtf8("name = caf\xc3\xa9", 12));
}

TEST_CASE("String_Format_LibraryTypes") {
        // Library types with no-arg toString() returning a String can be
        // passed to format() directly via PROMEKI_FORMAT_VIA_TOSTRING.
        UUID id("00112233-4455-6677-8899-aabbccddeeff");
        String s = String::format("id = {}", id);
        CHECK(s == "id = 00112233-4455-6677-8899-aabbccddeeff");

        // Standard string format specs (width, fill, alignment) are
        // inherited from the std::formatter<string_view> base.
        CHECK(String::format("[{:>40}]", id)
              == "[    00112233-4455-6677-8899-aabbccddeeff]");
        CHECK(String::format("[{:.<40}]", id)
              == "[00112233-4455-6677-8899-aabbccddeeff....]");

        // Class templates work via partial specialization (rational.h).
        Rational<int> r(24000, 1001);
        CHECK(String::format("rate = {}", r) == "rate = 24000/1001");
}

TEST_CASE("String_Format_Timecode") {
        // Bespoke std::formatter<Timecode> with custom format hint syntax.
        Timecode tc(Timecode::NDF24, 1, 0, 0, 0);  // 01:00:00:00 @ 24fps

        // Default — equivalent to {:smpte}.
        CHECK(String::format("{}", tc) == "01:00:00:00");
        CHECK(String::format("{:smpte}", tc) == "01:00:00:00");

        // SMPTE with appended frame rate (separator '/').
        CHECK(String::format("{:smpte-fps}", tc) == "01:00:00:00/24");

        // SMPTE with space-separated frame rate.
        CHECK(String::format("{:smpte-space}", tc) == "01:00:00:00 24");

        // Field separator style — VTC_STR_FMT_FIELD only diverges from
        // SMPTE when the timecode has field-1 interlace flags set, which
        // a plain progressive 24fps timecode does not.  Just verify the
        // hint is recognised and produces a valid string.
        CHECK_FALSE(String::format("{:field}", tc).isEmpty());

        // Format hint combined with a standard string format spec.
        // The hint and the std spec are separated by a colon.
        CHECK(String::format("[{:smpte:>16}]", tc) == "[     01:00:00:00]");
        CHECK(String::format("[{:smpte:*<16}]", tc) == "[01:00:00:00*****]");

        // Std-spec only (no hint) still works — defaults to SMPTE.
        CHECK(String::format("[{:>16}]", tc) == "[     01:00:00:00]");
}

TEST_CASE("String_Format_TemplateTypes") {
        // Class templates (Size2D, Point) registered via partial
        // specialisation rather than the macro.

        Size2Du32 sz(1920, 1080);
        String s = String::format("size = {}", sz);
        // Don't pin the exact stringification — just verify it round-trips
        // through the same toString() the formatter calls.
        CHECK(s == String("size = ") + sz.toString());

        Point2Di32 p(10, 20);
        String s2 = String::format("at {}", p);
        CHECK(s2 == String("at ") + p.toString());

        // Width / fill / alignment specifiers still work via the inherited
        // string_view formatter.
        CHECK(String::format("[{:>20}]", sz)
              == String("[") + String(20 - sz.toString().length(), ' ')
                 + sz.toString() + String("]"));
}

TEST_CASE("String_Format_PrimitiveLibraryTypes") {
        // Sanity check that PROMEKI_FORMAT_VIA_TOSTRING reaches the simple
        // value types in their respective headers.
        Duration  d  = Duration::fromSeconds(2.5);
        AudioLevel al = AudioLevel::fromDbfs(-12.0);
        FrameRate  fr(FrameRate::RationalType(24000, 1001));
        Ipv4Address ip(192, 168, 1, 100);

        // Each test verifies the format result equals the type's own
        // toString() output (with optional surrounding text), so we don't
        // need to know the exact stringification syntax.
        CHECK(String::format("{}", d)  == d.toString());
        CHECK(String::format("{}", al) == al.toString());
        CHECK(String::format("{}", fr) == fr.toString());
        CHECK(String::format("{}", ip) == ip.toString());

        // Combined with surrounding text and standard specs.
        CHECK(String::format("rate = {:>12}", fr)
              == String("rate = ") + String(12 - fr.toString().length(), ' ')
                 + fr.toString());
}

TEST_CASE("String_Format_Variant") {
        // Variant routes through VariantImpl::get<String>(), which already
        // knows how to convert every alternative.  This means *any* held
        // type formats correctly without per-type plumbing.
        Variant v;

        // Integer.
        v.set(static_cast<int32_t>(42));
        CHECK(String::format("v = {}", v) == "v = 42");

        // Floating point.
        v.set(3.5);
        CHECK(String::format("v = {}", v) == "v = 3.500000000");

        // Boolean.
        v.set(true);
        CHECK(String::format("v = {}", v) == "v = true");

        // String.
        v.set(String("hello"));
        CHECK(String::format("v = {}", v) == "v = hello");

        // UUID.
        UUID id("00112233-4455-6677-8899-aabbccddeeff");
        v.set(id);
        CHECK(String::format("v = {}", v)
              == "v = 00112233-4455-6677-8899-aabbccddeeff");

        // Standard string format specifiers still apply on the rendered
        // string regardless of which alternative is held.
        v.set(static_cast<int32_t>(7));
        CHECK(String::format("[{:>5}]", v) == "[    7]");
}

TEST_CASE("String_VFormat") {
        // Runtime format string path: format string is not a constexpr,
        // so we use std::make_format_args at the call site.
        std::string fmt = "value = {} ({:#x})";
        int v = 255;
        String s = String::vformat(fmt, std::make_format_args(v, v));
        CHECK(s == "value = 255 (0xff)");

        // Empty runtime format string.
        std::string emptyFmt;
        CHECK(String::vformat(emptyFmt, std::make_format_args()) == "");

        // Malformed format strings throw std::format_error at runtime
        // (this is the documented behavior of std::vformat — we don't
        // catch or transform).
        std::string badFmt = "{:Q}";
        int dummy = 0;
        CHECK_THROWS_AS(
                String::vformat(badFmt, std::make_format_args(dummy)),
                std::format_error);
}

// ============================================================================
// Numeric conversions
// ============================================================================

TEST_CASE("String_NumericConversions") {
        CHECK(String("42").toInt() == 42);
        CHECK(String("42").toUInt() == 42);
        CHECK(String("3.14").toDouble() > 3.13);
        CHECK(String("3.14").toDouble() < 3.15);
        CHECK(String("true").toBool() == true);
        CHECK(String("false").toBool() == false);
        CHECK(String("1").toBool() == true);
        CHECK(String("0").toBool() == false);
        CHECK(String("TRUE").toBool() == true);
        CHECK(String("FALSE").toBool() == false);
}

TEST_CASE("String_NumericConversions_EdgeCases") {
        // Negative integers
        CHECK(String("-7").toInt() == -7);

        // Unsigned integers
        CHECK(String("4294967295").toUInt() == 4294967295u);

        // Float via to<float>
        Error err;
        float f = String("2.5").to<float>(&err);
        CHECK(err.isOk());
        CHECK(f == doctest::Approx(2.5f));

        // Bad input returns error
        Error err2;
        int v = String("abc").to<int>(&err2);
        CHECK(err2.isError());
        CHECK(v == 0);

        // Trailing garbage returns error
        Error err3;
        int v2 = String("42abc").to<int>(&err3);
        CHECK(err3.isError());
        CHECK(v2 == 0);

        // Empty string returns error
        Error err4;
        double d = String("").to<double>(&err4);
        CHECK(err4.isError());
        CHECK(d == 0.0);
}

TEST_CASE("String_NumericConversions_BasePrefixes") {
        // Hexadecimal
        CHECK(String("0xDEADBEEF").to<uint32_t>() == 0xDEADBEEF);
        CHECK(String("0xFF").to<int>() == 255);
        CHECK(String("0XFF").to<int>() == 255);
        CHECK(String("-0xFF").to<int>() == -255);
        CHECK(String("0x0").to<int>() == 0);

        // Binary
        CHECK(String("0b1010").to<int>() == 10);
        CHECK(String("0B11111111").to<uint8_t>() == 255);
        CHECK(String("-0b1010").to<int>() == -10);

        // Octal
        CHECK(String("0o777").to<int>() == 511);
        CHECK(String("0O10").to<int>() == 8);
        CHECK(String("-0o77").to<int>() == -63);

        // Plain decimal still works
        CHECK(String("42").to<int>() == 42);
        CHECK(String("0").to<int>() == 0);
        CHECK(String("-7").to<int>() == -7);

        // toInt/toUInt also handle prefixes
        CHECK(String("0xFF").toInt() == 255);
        CHECK(String("0b1010").toInt() == 10);
        CHECK(String("0o777").toInt() == 511);
        CHECK(String("0xDEADBEEF").toUInt() == 0xDEADBEEF);
}

TEST_CASE("String_NumericConversions_Separators") {
        // Underscores
        CHECK(String("1_000_000").to<int>() == 1000000);
        CHECK(String("0xDEAD_BEEF").to<uint32_t>() == 0xDEADBEEF);
        CHECK(String("0b1111_0000").to<int>() == 0xF0);

        // Commas
        CHECK(String("1,000,000").to<int>() == 1000000);

        // Apostrophes (C++ digit separator style)
        CHECK(String("1'000'000").to<int>() == 1000000);

        // Mixed separators
        CHECK(String("1_000,000'000").to<int64_t>() == 1000000000LL);

        // Float with separators
        Error err;
        double d = String("1_000.5").to<double>(&err);
        CHECK(err.isOk());
        CHECK(d == doctest::Approx(1000.5));

        CHECK(String("1,234,567.89").to<double>() == doctest::Approx(1234567.89));

        // toInt/toUInt/toDouble also strip separators
        CHECK(String("1_000").toInt() == 1000);
        CHECK(String("1_000").toUInt() == 1000);
        CHECK(String("1_000.5").toDouble() == doctest::Approx(1000.5));
}

TEST_CASE("String_NumericConversions_BasePrefixErrors") {
        // Invalid digits for the base
        Error err;
        int v = String("0b2").to<int>(&err);
        CHECK(err.isError());
        CHECK(v == 0);

        Error err2;
        int v2 = String("0o8").to<int>(&err2);
        CHECK(err2.isError());
        CHECK(v2 == 0);

        // Just a prefix with no digits
        Error err3;
        int v3 = String("0x").to<int>(&err3);
        CHECK(err3.isError());
        CHECK(v3 == 0);
}

TEST_CASE("String_NumericConversions_Overflow") {
        // Overflow for signed 64-bit
        Error err;
        int64_t v = String("99999999999999999999").to<int64_t>(&err);
        CHECK(err == Error::OutOfRange);
        CHECK(v == 0);

        // Overflow for unsigned 64-bit
        Error err2;
        uint64_t v2 = String("99999999999999999999").to<uint64_t>(&err2);
        CHECK(err2 == Error::OutOfRange);
        CHECK(v2 == 0);

        // toInt overflow (value exceeds int range but fits long long)
        Error err3;
        int v3 = String("9999999999").toInt(&err3);
        CHECK(err3 == Error::OutOfRange);
        CHECK(v3 == 0);

        // toUInt overflow
        Error err4;
        unsigned int v4 = String("9999999999").toUInt(&err4);
        CHECK(err4 == Error::OutOfRange);
        CHECK(v4 == 0);

        // Double overflow
        Error err5;
        double v5 = String("1e9999").to<double>(&err5);
        CHECK(err5 == Error::OutOfRange);
        CHECK(v5 == 0.0);
}

TEST_CASE("String_NumericConversions_Bool") {
        // to<bool>() should delegate to toBool() and handle
        // string representations, not just numeric ones.
        Error err;
        CHECK(String("true").to<bool>(&err) == true);
        CHECK(err.isOk());

        CHECK(String("false").to<bool>() == false);
        CHECK(String("TRUE").to<bool>() == true);
        CHECK(String("FALSE").to<bool>() == false);
        CHECK(String("1").to<bool>() == true);
        CHECK(String("0").to<bool>() == false);

        // Invalid bool string
        Error err2;
        bool b = String("maybe").to<bool>(&err2);
        CHECK(err2.isError());
        CHECK(b == false);
}

// ============================================================================
// Thread safety (String COW with atomic refcount)
// ============================================================================

static void stringThreadFunc(String str, int iterations) {
        for(int i = 0; i < iterations; i++) {
                String local = str;
                (void)local.size();
                (void)local.cstr();
        }
}

TEST_CASE("String_ThreadSafeCopy") {
        const int ThreadCount = 8;
        const int Iterations = 10000;
        String shared = "Thread safe string data";

        std::vector<std::thread> threads;
        for(int i = 0; i < ThreadCount; i++) {
                threads.emplace_back(stringThreadFunc, shared, Iterations);
        }
        for(auto &t : threads) {
                t.join();
        }

        CHECK(shared == "Thread safe string data");
}

// ============================================================================
// Arg placeholder replacement
// ============================================================================

TEST_CASE("String_ArgReplacement") {
        CHECK(String("%1").arg("hello") == "hello");
        CHECK(String("%1 %2").arg("a").arg("b") == "a b");
        CHECK(String("%2 %1").arg("a").arg("b") == "b a");
        CHECK(String("%3 %2 %1").arg(3).arg(2).arg(1) == "1 2 3");
        CHECK(String("no placeholders").arg("x") == "no placeholders");
}

// ============================================================================
// Arg with copy
// ============================================================================

TEST_CASE("String_CopyArgIndependent") {
        String s1 = "%1 world";
        String s2 = s1;

        s2.arg("hello");
        CHECK(s1 == "%1 world");
        CHECK(s2 == "hello world");
}

// ============================================================================
// Empty string
// ============================================================================

TEST_CASE("String_EmptyStrings") {
        String a;
        String b;
        CHECK(a.isEmpty());
        CHECK(b.isEmpty());
        CHECK(a == b);
}

// ============================================================================
// Bool number conversion
// ============================================================================

TEST_CASE("String_BoolNumber") {
        CHECK(String::number(true) == "true");
        CHECK(String::number(false) == "false");
}

// ============================================================================
// Encoding
// ============================================================================

TEST_CASE("String_Encoding") {
        // Default is Latin1
        String ascii = "Hello";
        CHECK(ascii.encoding() == String::Latin1);
        CHECK(ascii.length() == 5);
        CHECK(ascii.byteCount() == 5);

        // fromUtf8 with non-ASCII input creates Unicode string
        const char *utf8 = "caf\xc3\xa9";
        String unicode = String::fromUtf8(utf8, 5);
        CHECK(unicode.encoding() == String::Unicode);
        CHECK(unicode.length() == 4);  // 4 characters
        CHECK(unicode.byteCount() == 5);  // 5 bytes
        CHECK(unicode.charAt(3).codepoint() == 0xE9);

        // Comparison across encodings
        CHECK(unicode == "caf\xc3\xa9");

        // 3-byte codepoints
        const char *jp = "\xe6\x97\xa5\xe6\x9c\xac";
        String jpStr = String::fromUtf8(jp, 6);
        CHECK(jpStr.length() == 2);
        CHECK(jpStr.byteCount() == 6);

        // fromUtf8 with pure-ASCII input takes the cheap Latin1 fast path
        // (ASCII is a subset of valid UTF-8 and each byte is its own
        // codepoint, so the heavier Unicode storage is unnecessary).
        String asciiUtf8 = String::fromUtf8("Hello", 5);
        CHECK(asciiUtf8.encoding() == String::Latin1);
        CHECK(asciiUtf8.length() == 5);
        CHECK(asciiUtf8.byteCount() == 5);
        CHECK(asciiUtf8 == "Hello");

        // Empty input also takes the Latin1 path (no non-ASCII bytes).
        String emptyUtf8 = String::fromUtf8("", 0);
        CHECK(emptyUtf8.encoding() == String::Latin1);
        CHECK(emptyUtf8.isEmpty());
}

TEST_CASE("String_EncodingPromotion") {
        String latin1 = "Hello ";
        const char *utf8World = "\xe4\xb8\x96\xe7\x95\x8c";  // 世界
        String unicode = String::fromUtf8(utf8World, 6);

        // Latin1 + Unicode = Unicode
        String combined = latin1 + unicode;
        CHECK(combined.encoding() == String::Unicode);
        CHECK(combined.length() == 8);  // 6 + 2
}

TEST_CASE("String_toLatin1") {
        // Latin1 → toLatin1 is a shallow copy
        String latin1 = "Hello";
        String converted = latin1.toLatin1();
        CHECK(converted.encoding() == String::Latin1);
        CHECK(converted == "Hello");
        CHECK(converted.referenceCount() == 2);  // shared with latin1

        // Unicode → toLatin1 converts, clamping non-Latin1 codepoints to '?'
        String unicode = String::fromUtf8("caf\xc3\xa9", 5);  // café
        String l = unicode.toLatin1();
        CHECK(l.encoding() == String::Latin1);
        CHECK(l.length() == 4);
        CHECK(l.charAt(3).codepoint() == 0xE9);  // é fits in Latin1

        // Codepoints above 0xFF get replaced with '?'
        const char *jp = "\xe6\x97\xa5\xe6\x9c\xac";  // 日本
        String jpStr = String::fromUtf8(jp, 6);
        String jpLatin1 = jpStr.toLatin1();
        CHECK(jpLatin1.encoding() == String::Latin1);
        CHECK(jpLatin1.length() == 2);
        CHECK(jpLatin1.charAt(0) == Char('?'));
        CHECK(jpLatin1.charAt(1) == Char('?'));
}

TEST_CASE("String_toUnicode") {
        // Unicode → toUnicode is a shallow copy
        String unicode = String::fromUtf8("caf\xc3\xa9", 5);
        String converted = unicode.toUnicode();
        CHECK(converted.encoding() == String::Unicode);
        CHECK(converted == "caf\xc3\xa9");
        CHECK(converted.referenceCount() == 2);  // shared with unicode

        // Latin1 → toUnicode promotes
        String latin1 = "Hello";
        String u = latin1.toUnicode();
        CHECK(u.encoding() == String::Unicode);
        CHECK(u.length() == 5);
        CHECK(u == "Hello");
        CHECK(u.charAt(0).codepoint() == 'H');

        // Empty string
        String empty;
        String eu = empty.toUnicode();
        CHECK(eu.encoding() == String::Unicode);
        CHECK(eu.isEmpty());
}

// ============================================================================
// Insert and Erase
// ============================================================================

TEST_CASE("String_InsertErase") {
        String s = "Hello World";

        // Erase
        String s2 = s;
        s2.erase(5, 1);
        CHECK(s2 == "HelloWorld");
        CHECK(s == "Hello World");

        // Insert
        String s3 = "HelloWorld";
        s3.insert(5, String(" "));
        CHECK(s3 == "Hello World");
}

// ============================================================================
// COW reference counting
// ============================================================================

TEST_CASE("String_COWRefCount") {
        String s1 = "Shared";
        CHECK(s1.referenceCount() == 1);

        String s2 = s1;
        CHECK(s1.referenceCount() == 2);
        CHECK(s2.referenceCount() == 2);

        String s3 = s2;
        CHECK(s1.referenceCount() == 3);

        s3 += "!";
        CHECK(s3.referenceCount() == 1);
        CHECK(s1.referenceCount() == 2);
}

// ============================================================================
// Regular strings must NOT be literal
// ============================================================================

TEST_CASE("String_Regular_IsNotLiteral") {
        CHECK_FALSE(String().isLiteral());
        CHECK_FALSE(String("Hello").isLiteral());
        CHECK_FALSE(String(std::string("Hello")).isLiteral());
        CHECK_FALSE(String(5, 'x').isLiteral());
        CHECK_FALSE(String::fromUtf8("caf\xc3\xa9", 5).isLiteral());
        CHECK_FALSE(String::sprintf("Hello %d", 42).isLiteral());
        CHECK_FALSE(String::number(42).isLiteral());
}

// ============================================================================
// Compile-time detection (static_assert)
// ============================================================================

TEST_CASE("String_CompiledString_CompileTimeDetection") {
        static constexpr auto ascii = CompiledString<6>("Hello");
        static_assert(ascii.isAscii());
        static_assert(ascii.charCount() == 5);
        static_assert(ascii.byteCount() == 5);

        static constexpr auto utf8 = CompiledString<6>("caf\xc3\xa9");
        static_assert(!utf8.isAscii());
        static_assert(utf8.charCount() == 4);
        static_assert(utf8.byteCount() == 5);

        static constexpr auto empty = CompiledString<1>("");
        static_assert(empty.isAscii());
        static_assert(empty.charCount() == 0);
        static_assert(empty.byteCount() == 0);

        // 3-byte codepoints
        static constexpr auto jp = CompiledString<7>("\xe6\x97\xa5\xe6\x9c\xac");
        static_assert(!jp.isAscii());
        static_assert(jp.charCount() == 2);
        static_assert(jp.byteCount() == 6);

        // 4-byte codepoint (emoji)
        static constexpr auto emoji = CompiledString<5>("\xf0\x9f\x98\x80");
        static_assert(!emoji.isAscii());
        static_assert(emoji.charCount() == 1);
        static_assert(emoji.byteCount() == 4);
}

// ============================================================================
// PROMEKI_STRING Latin1 literal — backing type and zero-copy
// ============================================================================

TEST_CASE("String_Literal_Latin1_BackingType") {
        String s = PROMEKI_STRING("Hello");

        // Must be Latin1 encoding and literal-backed
        CHECK(s.encoding() == String::Latin1);
        CHECK(s.isLiteral());

        // Basic access
        CHECK(s == "Hello");
        CHECK(s.length() == 5);
        CHECK(s.byteCount() == 5);
        CHECK(s.charAt(0) == Char('H'));
        CHECK(s.charAt(4) == Char('o'));
        CHECK(s.byteAt(0) == 'H');

        // Empty literal
        String e = PROMEKI_STRING("");
        CHECK(e.isEmpty());
        CHECK(e.isLiteral());
        CHECK(e.encoding() == String::Latin1);
}

TEST_CASE("String_Literal_Latin1_ZeroCopy") {
        String s1 = PROMEKI_STRING("Shared");

        // Literal is immortal — refcount is at the immortal threshold
        CHECK(s1.isLiteral());
        int rc = s1.referenceCount();

        // Copy doesn't change the refcount (inc is no-op on immortal)
        String s2 = s1;
        CHECK(s2.isLiteral());
        CHECK(s2.referenceCount() == rc);
        CHECK(s1.referenceCount() == rc);

        // Both share the same underlying data (same cstr pointer)
        CHECK(s1.cstr() == s2.cstr());

        // Third copy — still same pointer, still immortal
        String s3 = s2;
        CHECK(s3.cstr() == s1.cstr());
        CHECK(s3.isLiteral());

        // Destroying copies doesn't affect the literal
        {
                String tmp = s1;
                CHECK(tmp.cstr() == s1.cstr());
        }
        CHECK(s1 == "Shared");
        CHECK(s1.isLiteral());
}

TEST_CASE("String_Literal_Latin1_COW") {
        // setCharAt promotes to normal Latin1
        String s1 = PROMEKI_STRING("Hello");
        CHECK(s1.isLiteral());
        s1.setCharAt(0, Char('J'));
        CHECK_FALSE(s1.isLiteral());
        CHECK(s1.encoding() == String::Latin1);
        CHECK(s1 == "Jello");
        CHECK(s1.referenceCount() == 1);

        // operator+= promotes to normal Latin1
        String s2 = PROMEKI_STRING("Hello");
        CHECK(s2.isLiteral());
        s2 += " World";
        CHECK_FALSE(s2.isLiteral());
        CHECK(s2.encoding() == String::Latin1);
        CHECK(s2 == "Hello World");
        CHECK(s2.referenceCount() == 1);

        // erase promotes to normal Latin1
        String s3 = PROMEKI_STRING("Hello");
        s3.erase(4, 1);
        CHECK_FALSE(s3.isLiteral());
        CHECK(s3.encoding() == String::Latin1);
        CHECK(s3 == "Hell");

        // insert promotes to normal Latin1
        String s4 = PROMEKI_STRING("Hllo");
        s4.insert(1, String("e"));
        CHECK_FALSE(s4.isLiteral());
        CHECK(s4.encoding() == String::Latin1);
        CHECK(s4 == "Hello");

        // clear promotes
        String s5 = PROMEKI_STRING("Hello");
        s5.clear();
        CHECK_FALSE(s5.isLiteral());
        CHECK(s5.isEmpty());

        // resize promotes
        String s6 = PROMEKI_STRING("Hello");
        s6.resize(3);
        CHECK_FALSE(s6.isLiteral());
        CHECK(s6 == "Hel");

        // Original literal is unaffected by all mutations above
        String original = PROMEKI_STRING("Hello");
        CHECK(original.isLiteral());
        CHECK(original == "Hello");
}

TEST_CASE("String_Literal_Latin1_Operations") {
        // All read operations work directly on literal data
        String s = PROMEKI_STRING("Hello World");
        CHECK(s.isLiteral());

        CHECK(s.find('W') == 6);
        CHECK(s.find(Char('o')) == 4);
        CHECK(s.find(String("World")) == 6);
        CHECK(s.find(String("xyz")) == String::npos);
        CHECK(s.contains("World"));
        CHECK_FALSE(s.contains("xyz"));
        CHECK(s.count("l") == 3);
        CHECK(s.count("o") == 2);

        CHECK(s.substr(0, 5) == "Hello");
        CHECK(s.substr(6) == "World");
        CHECK(s.left(5) == "Hello");
        CHECK(s.right(5) == "World");

        CHECK(s.startsWith("Hello"));
        CHECK(s.startsWith('H'));
        CHECK(s.endsWith("World"));
        CHECK_FALSE(s.startsWith("World"));

        // Iteration over literal
        size_t count = 0;
        for(Char c : s) { (void)c; ++count; }
        CHECK(count == 11);

        // Comparison: literal vs regular
        CHECK(s == String("Hello World"));
        CHECK(s == "Hello World");
        CHECK(String("Hello World") == s);

        // Comparison: literal vs literal
        CHECK(s == PROMEKI_STRING("Hello World"));

        // Non-mutating operations don't break literal status
        CHECK(s.isLiteral());

        // Concat produces a new (non-literal) string, source unchanged
        String combined = s + String("!");
        CHECK(combined == "Hello World!");
        CHECK_FALSE(combined.isLiteral());
        CHECK(s.isLiteral());
}

TEST_CASE("String_Literal_Latin1_StrCache") {
        // str() returns a valid std::string reference (lazy cache)
        String s = PROMEKI_STRING("Hello");
        const std::string &ref = s.str();
        CHECK(ref == "Hello");
        CHECK(ref.size() == 5);

        // Calling str() again returns the same reference
        const std::string &ref2 = s.str();
        CHECK(&ref == &ref2);

        // Implicit conversion to const std::string & also works
        const std::string &ref3 = s;
        CHECK(ref3 == "Hello");

        CHECK(s.isLiteral());
}

TEST_CASE("String_Literal_Latin1_COW_Copy") {
        // Copy a literal, mutate the copy — original stays literal
        String s1 = PROMEKI_STRING("Hello");
        String s2 = s1;
        CHECK(s1.isLiteral());
        CHECK(s2.isLiteral());
        CHECK(s1.cstr() == s2.cstr());

        s2 += " World";
        CHECK_FALSE(s2.isLiteral());
        CHECK(s2.encoding() == String::Latin1);
        CHECK(s2 == "Hello World");

        // Original is unmodified and still literal
        CHECK(s1.isLiteral());
        CHECK(s1 == "Hello");
}

TEST_CASE("String_Literal_Latin1_Assignment") {
        // Assigning over a literal replaces with a regular string
        String s = PROMEKI_STRING("Hello");
        CHECK(s.isLiteral());

        s = "Goodbye";
        CHECK_FALSE(s.isLiteral());
        CHECK(s == "Goodbye");
        CHECK(s.encoding() == String::Latin1);

        s = std::string("World");
        CHECK_FALSE(s.isLiteral());
        CHECK(s == "World");
}

TEST_CASE("String_Literal_Latin1_DerivedOps") {
        // reverse() on literal produces non-literal
        String s = PROMEKI_STRING("Hello");
        String rev = s.reverse();
        CHECK(rev == "olleH");
        CHECK_FALSE(rev.isLiteral());
        CHECK(rev.encoding() == String::Latin1);
        CHECK(s.isLiteral());

        // toUpper/toLower produce non-literal
        String upper = s.toUpper();
        CHECK(upper == "HELLO");
        CHECK_FALSE(upper.isLiteral());
        CHECK(upper.encoding() == String::Latin1);

        String lower = s.toLower();
        CHECK(lower == "hello");
        CHECK_FALSE(lower.isLiteral());

        CHECK(s.isLiteral());

        // trim on literal
        String padded = PROMEKI_STRING("  hi  ");
        String trimmed = padded.trim();
        CHECK(trimmed == "hi");
        CHECK_FALSE(trimmed.isLiteral());
        CHECK(padded.isLiteral());

        // isNumeric on literal
        CHECK(PROMEKI_STRING("1234").isNumeric());
        CHECK_FALSE(PROMEKI_STRING("12x4").isNumeric());
}

TEST_CASE("String_Literal_Latin1_Conversions") {
        CHECK(PROMEKI_STRING("42").toInt() == 42);
        CHECK(PROMEKI_STRING("42").toUInt() == 42);
        CHECK(PROMEKI_STRING("3.14").toDouble() > 3.13);
        CHECK(PROMEKI_STRING("3.14").toDouble() < 3.15);
        CHECK(PROMEKI_STRING("true").toBool() == true);
        CHECK(PROMEKI_STRING("false").toBool() == false);
        CHECK(PROMEKI_STRING("1").toBool() == true);
        CHECK(PROMEKI_STRING("0").toBool() == false);
}

TEST_CASE("String_Literal_Latin1_SplitArg") {
        // split on literal
        String s = PROMEKI_STRING("a,b,c");
        auto parts = s.split(",");
        CHECK(parts.size() == 3);
        CHECK(parts.at(0) == "a");
        CHECK(parts.at(1) == "b");
        CHECK(parts.at(2) == "c");
        CHECK(s.isLiteral());

        // arg on literal triggers COW
        String fmt = PROMEKI_STRING("%1 world");
        fmt.arg("hello");
        CHECK_FALSE(fmt.isLiteral());
        CHECK(fmt == "hello world");
}

// ============================================================================
// PROMEKI_STRING Unicode literal — backing type and zero-copy
// ============================================================================

TEST_CASE("String_Literal_Unicode_BackingType") {
        String s = PROMEKI_STRING("caf\xc3\xa9");

        // Must be Unicode encoding and literal-backed
        CHECK(s.encoding() == String::Unicode);
        CHECK(s.isLiteral());

        // Character access — from compile-time decoded codepoints
        CHECK(s.length() == 4);
        CHECK(s.charAt(0) == Char('c'));
        CHECK(s.charAt(1) == Char('a'));
        CHECK(s.charAt(2) == Char('f'));
        CHECK(s.charAt(3).codepoint() == 0xE9);

        // Byte access — from original UTF-8 bytes
        CHECK(s.byteCount() == 5);
        CHECK(s.byteAt(0) == 'c');
        CHECK(s.byteAt(3) == 0xC3);
        CHECK(s.byteAt(4) == 0xA9);

        // 3-byte codepoints
        String jp = PROMEKI_STRING("\xe6\x97\xa5\xe6\x9c\xac");
        CHECK(jp.encoding() == String::Unicode);
        CHECK(jp.isLiteral());
        CHECK(jp.length() == 2);
        CHECK(jp.byteCount() == 6);
        CHECK(jp.charAt(0).codepoint() == 0x65E5);
        CHECK(jp.charAt(1).codepoint() == 0x672C);

        // 4-byte codepoint (emoji)
        String emoji = PROMEKI_STRING("\xf0\x9f\x98\x80");
        CHECK(emoji.encoding() == String::Unicode);
        CHECK(emoji.isLiteral());
        CHECK(emoji.length() == 1);
        CHECK(emoji.byteCount() == 4);
        CHECK(emoji.charAt(0).codepoint() == 0x1F600);
}

TEST_CASE("String_Literal_Unicode_ZeroCopy") {
        String s1 = PROMEKI_STRING("caf\xc3\xa9");

        // Literal is immortal
        CHECK(s1.isLiteral());
        int rc = s1.referenceCount();

        // Copy doesn't change refcount
        String s2 = s1;
        CHECK(s2.isLiteral());
        CHECK(s2.referenceCount() == rc);
        CHECK(s1.referenceCount() == rc);

        // Both share the same underlying data
        CHECK(s1.cstr() == s2.cstr());

        // Multiple copies — all share
        String s3 = s2;
        CHECK(s3.cstr() == s1.cstr());
        CHECK(s3.isLiteral());

        // Destroying copies doesn't affect the literal
        {
                String tmp = s1;
                CHECK(tmp.cstr() == s1.cstr());
        }
        CHECK(s1 == "caf\xc3\xa9");
        CHECK(s1.isLiteral());
}

TEST_CASE("String_Literal_Unicode_COW") {
        // setCharAt promotes to normal Unicode
        String s1 = PROMEKI_STRING("caf\xc3\xa9");
        CHECK(s1.isLiteral());
        s1.setCharAt(0, Char('C'));
        CHECK_FALSE(s1.isLiteral());
        CHECK(s1.encoding() == String::Unicode);
        CHECK(s1.charAt(0) == Char('C'));
        CHECK(s1.charAt(3).codepoint() == 0xE9);
        CHECK(s1.length() == 4);
        CHECK(s1.referenceCount() == 1);

        // operator+= promotes to normal Unicode
        String s2 = PROMEKI_STRING("caf\xc3\xa9");
        CHECK(s2.isLiteral());
        s2 += String::fromUtf8("!", 1);
        CHECK_FALSE(s2.isLiteral());
        CHECK(s2.encoding() == String::Unicode);
        CHECK(s2.length() == 5);
        CHECK(s2.referenceCount() == 1);

        // erase promotes to normal Unicode
        String s3 = PROMEKI_STRING("caf\xc3\xa9");
        s3.erase(3, 1);
        CHECK_FALSE(s3.isLiteral());
        CHECK(s3.encoding() == String::Unicode);
        CHECK(s3 == "caf");
        CHECK(s3.length() == 3);

        // insert promotes to normal Unicode
        String s4 = PROMEKI_STRING("cf\xc3\xa9");
        s4.insert(1, String("a"));
        CHECK_FALSE(s4.isLiteral());
        CHECK(s4.encoding() == String::Unicode);
        CHECK(s4.length() == 4);

        // clear promotes
        String s5 = PROMEKI_STRING("caf\xc3\xa9");
        s5.clear();
        CHECK_FALSE(s5.isLiteral());
        CHECK(s5.isEmpty());

        // resize promotes
        String s6 = PROMEKI_STRING("caf\xc3\xa9");
        s6.resize(2);
        CHECK_FALSE(s6.isLiteral());
        CHECK(s6.length() == 2);

        // Original literal is unaffected
        String original = PROMEKI_STRING("caf\xc3\xa9");
        CHECK(original.isLiteral());
        CHECK(original.length() == 4);
        CHECK(original.charAt(3).codepoint() == 0xE9);
}

TEST_CASE("String_Literal_Unicode_Operations") {
        String s = PROMEKI_STRING("caf\xc3\xa9 lait");

        CHECK(s.isLiteral());
        CHECK(s.encoding() == String::Unicode);
        CHECK(s.length() == 9);  // c a f é   l a i t

        // find by Char
        CHECK(s.find(Char('c')) == 0);
        CHECK(s.find(Char(static_cast<char32_t>(0xE9))) == 3);
        CHECK(s.find(Char('l')) == 5);
        CHECK(s.find(Char('z')) == String::npos);

        // find by substring
        CHECK(s.find(String("caf")) == 0);
        CHECK(s.find(String("lait")) == 5);
        CHECK(s.find(String("xyz")) == String::npos);

        // contains
        CHECK(s.contains("caf"));
        CHECK(s.contains("lait"));
        CHECK_FALSE(s.contains("xyz"));

        // count
        CHECK(s.count("a") == 2);

        // substr
        CHECK(s.substr(0, 4).length() == 4);
        CHECK(s.substr(0, 4).charAt(3).codepoint() == 0xE9);
        CHECK(s.substr(5).length() == 4);

        // startsWith / endsWith
        CHECK(s.startsWith("caf"));
        CHECK(s.startsWith('c'));

        // Iteration
        size_t count = 0;
        for(Char c : s) { (void)c; ++count; }
        CHECK(count == 9);

        // Comparison: literal vs regular
        String regular = String::fromUtf8("caf\xc3\xa9 lait", 10);
        CHECK(s == regular);

        // Comparison: literal vs literal
        CHECK(s == PROMEKI_STRING("caf\xc3\xa9 lait"));

        // Non-mutating operations don't break literal status
        CHECK(s.isLiteral());

        // Concat produces non-literal, source unchanged
        String combined = s + String("!");
        CHECK_FALSE(combined.isLiteral());
        CHECK(s.isLiteral());

        // endsWith on Unicode literal
        CHECK(s.endsWith("lait"));
        CHECK(s.endsWith("t"));
        CHECK_FALSE(s.endsWith("xyz"));
}

TEST_CASE("String_Literal_Unicode_StrCache") {
        // str() returns valid UTF-8 string reference (lazy cache)
        String s = PROMEKI_STRING("caf\xc3\xa9");
        const std::string &ref = s.str();
        CHECK(ref == "caf\xc3\xa9");
        CHECK(ref.size() == 5);

        // Calling str() again returns the same reference
        const std::string &ref2 = s.str();
        CHECK(&ref == &ref2);

        CHECK(s.isLiteral());
}

TEST_CASE("String_Literal_Unicode_COW_Copy") {
        // Copy a Unicode literal, mutate the copy — original stays literal
        String s1 = PROMEKI_STRING("caf\xc3\xa9");
        String s2 = s1;
        CHECK(s1.isLiteral());
        CHECK(s2.isLiteral());
        CHECK(s1.cstr() == s2.cstr());

        s2.setCharAt(0, Char('C'));
        CHECK_FALSE(s2.isLiteral());
        CHECK(s2.encoding() == String::Unicode);
        CHECK(s2.charAt(0) == Char('C'));
        CHECK(s2.charAt(3).codepoint() == 0xE9);

        // Original is unmodified and still literal
        CHECK(s1.isLiteral());
        CHECK(s1.charAt(0) == Char('c'));
}

TEST_CASE("String_Literal_Unicode_Assignment") {
        // Assigning over a Unicode literal replaces with a regular string
        String s = PROMEKI_STRING("caf\xc3\xa9");
        CHECK(s.isLiteral());

        s = "plain";
        CHECK_FALSE(s.isLiteral());
        CHECK(s == "plain");
        CHECK(s.encoding() == String::Latin1);

        // Assigning UTF-8 via fromUtf8
        s = String::fromUtf8("caf\xc3\xa9", 5);
        CHECK_FALSE(s.isLiteral());
        CHECK(s.encoding() == String::Unicode);
}

TEST_CASE("String_Literal_Unicode_DerivedOps") {
        // reverse() on Unicode literal produces non-literal Unicode
        String s = PROMEKI_STRING("caf\xc3\xa9");
        String rev = s.reverse();
        CHECK_FALSE(rev.isLiteral());
        CHECK(rev.encoding() == String::Unicode);
        CHECK(rev.length() == 4);
        CHECK(rev.charAt(0).codepoint() == 0xE9);
        CHECK(rev.charAt(3) == Char('c'));
        CHECK(s.isLiteral());

        // toUpper/toLower produce non-literal Unicode
        String upper = s.toUpper();
        CHECK_FALSE(upper.isLiteral());
        CHECK(upper.encoding() == String::Unicode);
        CHECK(upper.charAt(0) == Char('C'));

        String lower = PROMEKI_STRING("CAF\xc3\x89");  // CAFÉ (É = U+00C9)
        String lowered = lower.toLower();
        CHECK_FALSE(lowered.isLiteral());
        CHECK(lowered.encoding() == String::Unicode);

        CHECK(s.isLiteral());
        CHECK(lower.isLiteral());

        // toLatin1/toUnicode on literals
        String l = s.toLatin1();
        CHECK_FALSE(l.isLiteral());
        CHECK(l.encoding() == String::Latin1);
        CHECK(l.length() == 4);
        CHECK(l.charAt(3).codepoint() == 0xE9);  // é fits in Latin1

        String latin1Lit = PROMEKI_STRING("Hello");
        String u = latin1Lit.toUnicode();
        CHECK_FALSE(u.isLiteral());
        CHECK(u.encoding() == String::Unicode);
        CHECK(u.length() == 5);
        CHECK(latin1Lit.isLiteral());
}

// ============================================================================
// Cross-encoding operations with literals
// ============================================================================

TEST_CASE("String_Literal_CrossEncoding") {
        String latin1Lit = PROMEKI_STRING("Hello ");
        String unicodeLit = PROMEKI_STRING("caf\xc3\xa9");

        // Latin1 literal + Unicode literal → Unicode (non-literal)
        String combined = latin1Lit + unicodeLit;
        CHECK(combined.encoding() == String::Unicode);
        CHECK_FALSE(combined.isLiteral());
        CHECK(combined.length() == 10);

        // Sources unchanged
        CHECK(latin1Lit.isLiteral());
        CHECK(latin1Lit.encoding() == String::Latin1);
        CHECK(unicodeLit.isLiteral());
        CHECK(unicodeLit.encoding() == String::Unicode);

        // Unicode literal + Latin1 literal → Unicode (non-literal)
        String combined2 = unicodeLit + latin1Lit;
        CHECK(combined2.encoding() == String::Unicode);
        CHECK_FALSE(combined2.isLiteral());

        // Latin1 literal += Unicode literal
        String s1 = PROMEKI_STRING("Hello ");
        s1 += unicodeLit;
        CHECK_FALSE(s1.isLiteral());
        CHECK(s1.encoding() == String::Unicode);
        CHECK(s1.length() == 10);

        // Unicode literal += Latin1 literal
        String s2 = PROMEKI_STRING("caf\xc3\xa9");
        s2 += latin1Lit;
        CHECK_FALSE(s2.isLiteral());
        CHECK(s2.encoding() == String::Unicode);

        // Latin1 literal + regular Unicode → Unicode (non-literal)
        String regularUnicode = String::fromUtf8("\xc3\xa9", 2);
        String mixed = latin1Lit + regularUnicode;
        CHECK(mixed.encoding() == String::Unicode);
        CHECK_FALSE(mixed.isLiteral());

        // Unicode literal + regular Latin1 → Unicode (non-literal)
        String regularLatin1 = "!";
        String mixed2 = unicodeLit + regularLatin1;
        CHECK(mixed2.encoding() == String::Unicode);
        CHECK_FALSE(mixed2.isLiteral());

        // setCharAt with Unicode char on Latin1 literal → Unicode (non-literal)
        String s3 = PROMEKI_STRING("Hello");
        s3.setCharAt(0, Char(static_cast<char32_t>(0x00C9)));  // É (fits Latin1)
        CHECK_FALSE(s3.isLiteral());
        // Codepoint > 0xFF forces Unicode promotion
        String s4 = PROMEKI_STRING("Hello");
        s4.setCharAt(0, Char(static_cast<char32_t>(0x20AC)));  // € (requires Unicode)
        CHECK_FALSE(s4.isLiteral());
        CHECK(s4.encoding() == String::Unicode);
        CHECK(s4.charAt(0).codepoint() == 0x20AC);
}

// ============================================================================
// User-defined literal (_ps)
// ============================================================================

TEST_CASE("String_UDL") {
        using namespace promeki::literals;

        // ASCII — Latin1, not literal (UDL is a convenience, not zero-copy)
        String s = "Hello"_ps;
        CHECK(s == "Hello");
        CHECK(s.encoding() == String::Latin1);
        CHECK_FALSE(s.isLiteral());
        CHECK(s.length() == 5);

        // UTF-8 — auto-detected as Unicode
        String u = "caf\xc3\xa9"_ps;
        CHECK(u.encoding() == String::Unicode);
        CHECK_FALSE(u.isLiteral());
        CHECK(u.length() == 4);
        CHECK(u.charAt(3).codepoint() == 0xE9);

        // Empty string
        String e = ""_ps;
        CHECK(e.isEmpty());

        // Operations work normally
        CHECK("Hello World"_ps.contains("World"));
        CHECK("Hello"_ps + " World"_ps == "Hello World");
}

// ============================================================================
// Unicode erase — multi-character and boundary cases
// ============================================================================

TEST_CASE("String_Unicode_Erase") {
        // café → erase "af" → "cé"
        String s = String::fromUtf8("caf\xc3\xa9", 5);
        CHECK(s.length() == 4);
        s.erase(1, 2);
        CHECK(s.length() == 2);
        CHECK(s.charAt(0) == Char('c'));
        CHECK(s.charAt(1).codepoint() == 0xE9);

        // Erase all
        String s2 = String::fromUtf8("caf\xc3\xa9", 5);
        s2.erase(0, 4);
        CHECK(s2.isEmpty());

        // Erase past end (clamped)
        String s3 = String::fromUtf8("caf\xc3\xa9", 5);
        s3.erase(2, 100);
        CHECK(s3.length() == 2);
        CHECK(s3.charAt(0) == Char('c'));
        CHECK(s3.charAt(1) == Char('a'));

        // Erase at end
        String s4 = String::fromUtf8("caf\xc3\xa9", 5);
        s4.erase(3, 1);
        CHECK(s4.length() == 3);
        CHECK(s4 == "caf");

        // Erase at beginning
        String s5 = String::fromUtf8("caf\xc3\xa9", 5);
        s5.erase(0, 1);
        CHECK(s5.length() == 3);
        CHECK(s5.charAt(0) == Char('a'));

        // Erase with pos beyond size (no-op)
        String s6 = String::fromUtf8("caf\xc3\xa9", 5);
        s6.erase(10, 1);
        CHECK(s6.length() == 4);

        // Erase from string with multi-byte codepoints (3-byte: 日本語)
        const char *jp = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
        String jpStr = String::fromUtf8(jp, 9);
        CHECK(jpStr.length() == 3);
        jpStr.erase(1, 1);  // remove 本
        CHECK(jpStr.length() == 2);
        CHECK(jpStr.charAt(0).codepoint() == 0x65E5);  // 日
        CHECK(jpStr.charAt(1).codepoint() == 0x8A9E);  // 語
}

// ============================================================================
// Unicode insert
// ============================================================================

TEST_CASE("String_Unicode_Insert") {
        // Insert ASCII into Unicode string
        String s = String::fromUtf8("caf\xc3\xa9", 5);
        s.insert(3, String(" au"));
        CHECK(s.length() == 7);
        CHECK(s.charAt(3) == Char(' '));
        CHECK(s.charAt(6).codepoint() == 0xE9);

        // Insert at beginning
        String s2 = String::fromUtf8("\xc3\xa9", 2);
        s2.insert(0, String("caf"));
        CHECK(s2.length() == 4);
        CHECK(s2.charAt(0) == Char('c'));
        CHECK(s2.charAt(3).codepoint() == 0xE9);

        // Insert at end
        String s3 = String::fromUtf8("caf", 3);
        String suffix = String::fromUtf8("\xc3\xa9", 2);
        s3.insert(3, suffix);
        CHECK(s3.length() == 4);
        CHECK(s3.charAt(3).codepoint() == 0xE9);

        // Insert Unicode into Unicode
        String s4 = String::fromUtf8("\xe6\x97\xa5\xe8\xaa\x9e", 6);  // 日語
        String mid = String::fromUtf8("\xe6\x9c\xac", 3);  // 本
        s4.insert(1, mid);
        CHECK(s4.length() == 3);
        CHECK(s4.charAt(0).codepoint() == 0x65E5);
        CHECK(s4.charAt(1).codepoint() == 0x672C);
        CHECK(s4.charAt(2).codepoint() == 0x8A9E);
}

// ============================================================================
// Unicode trim
// ============================================================================

TEST_CASE("String_Unicode_Trim") {
        // Trim Unicode string with ASCII whitespace
        String s = String::fromUtf8("  caf\xc3\xa9  ", 9);
        String trimmed = s.trim();
        CHECK(trimmed.length() == 4);
        CHECK(trimmed.charAt(0) == Char('c'));
        CHECK(trimmed.charAt(3).codepoint() == 0xE9);

        // (Encoding may narrow to Latin1 since 'é' fits — that's an
        //  optimization detail.  What matters is the logical content.)

        // Trim all-whitespace Unicode string
        String ws = String::fromUtf8("   ", 3);
        // Force to Unicode via promotion
        ws = ws.toUnicode();
        CHECK(ws.trim().isEmpty());

        // Trim with tabs and newlines
        String s2 = String::fromUtf8("\t\ncaf\xc3\xa9\r\n", 9);
        String trimmed2 = s2.trim();
        CHECK(trimmed2.length() == 4);
        CHECK(trimmed2.charAt(3).codepoint() == 0xE9);

        // Trim with no whitespace (no-op)
        String s3 = String::fromUtf8("caf\xc3\xa9", 5);
        String trimmed3 = s3.trim();
        CHECK(trimmed3.length() == 4);
        CHECK(trimmed3 == s3);
}

// ============================================================================
// Empty string edge cases
// ============================================================================

TEST_CASE("String_EmptyEdgeCases") {
        String empty;

        // Operations on empty string
        CHECK(empty.find('x') == String::npos);
        CHECK(empty.find(Char('x')) == String::npos);
        CHECK(empty.find("hello") == String::npos);
        CHECK(empty.rfind('x') == String::npos);
        CHECK(empty.rfind("hello") == String::npos);
        CHECK(!empty.contains('x'));
        CHECK(!empty.contains(Char('x')));
        CHECK(!empty.contains("hello"));
        CHECK(empty.count("x") == 0);
        CHECK(empty.trim().isEmpty());
        CHECK(empty.toUpper().isEmpty());
        CHECK(empty.toLower().isEmpty());
        CHECK(empty.reverse().isEmpty());
        CHECK(!empty.isNumeric());
        CHECK(!empty.startsWith('x'));
        CHECK(!empty.startsWith("x"));
        CHECK(!empty.endsWith("x"));
        CHECK(empty.substr(0) == "");
        CHECK(empty.left(0) == "");
        CHECK(empty.right(0) == "");
        CHECK(empty.replace("a", "b") == "");
        CHECK(empty.compareIgnoreCase("") == 0);

        // Hash of empty string is consistent
        CHECK(empty.hash() == String().hash());

        // Erase on empty (no crash)
        String e2;
        e2.erase(0, 1);
        CHECK(e2.isEmpty());
}

// ============================================================================
// right() with count >= length
// ============================================================================

TEST_CASE("String_RightOverflow") {
        String s = "Hi";
        CHECK(s.right(5) == "Hi");
        CHECK(s.right(2) == "Hi");
        CHECK(s.right(1) == "i");
        CHECK(s.right(0) == "");
}

// ============================================================================
// Comparison operators
// ============================================================================

TEST_CASE("String_ComparisonOperators") {
        String a = "apple";
        String b = "banana";
        String a2 = "apple";

        CHECK(a < b);
        CHECK(a <= b);
        CHECK(a <= a2);
        CHECK(b > a);
        CHECK(b >= a);
        CHECK(a >= a2);
        CHECK(!(a > b));
        CHECK(!(a > a2));
        CHECK(!(b < a));
        CHECK(!(b <= a));
}

// ============================================================================
// contains(Char)
// ============================================================================

TEST_CASE("String_ContainsChar") {
        String s = "Hello";
        CHECK(s.contains(Char('H')));
        CHECK(s.contains(Char('o')));
        CHECK(!s.contains(Char('z')));

        // Unicode
        String u = String::fromUtf8("caf\xc3\xa9", 5);
        CHECK(u.contains(Char(static_cast<char32_t>(0xE9))));
        CHECK(!u.contains(Char(static_cast<char32_t>(0x20AC))));
}

// ============================================================================
// find with from parameter
// ============================================================================

TEST_CASE("String_FindWithFrom") {
        String s = "abcabc";

        CHECK(s.find('a', 0) == 0);
        CHECK(s.find('a', 1) == 3);
        CHECK(s.find('a', 4) == String::npos);
        CHECK(s.find("bc", 0) == 1);
        CHECK(s.find("bc", 2) == 4);
        CHECK(s.find("bc", 5) == String::npos);

        // Unicode with from
        String u = String::fromUtf8("caf\xc3\xa9 caf\xc3\xa9", 11);
        CHECK(u.find(Char('c'), 0) == 0);
        CHECK(u.find(Char('c'), 1) == 5);
        CHECK(u.find(Char(static_cast<char32_t>(0xE9)), 0) == 3);
        CHECK(u.find(Char(static_cast<char32_t>(0xE9)), 4) == 8);
}

// ============================================================================
// rfind
// ============================================================================

TEST_CASE("String_Rfind") {
        String s = "abcabc";

        // rfind char
        CHECK(s.rfind('a') == 3);
        CHECK(s.rfind('c') == 5);
        CHECK(s.rfind('z') == String::npos);
        CHECK(s.rfind('a', 2) == 0);
        CHECK(s.rfind('c', 4) == 2);

        // rfind substring
        CHECK(s.rfind("abc") == 3);
        CHECK(s.rfind("abc", 2) == 0);
        CHECK(s.rfind("xyz") == String::npos);

        // rfind on single-char string
        String one = "x";
        CHECK(one.rfind('x') == 0);
        CHECK(one.rfind('y') == String::npos);

        // rfind on empty
        String empty;
        CHECK(empty.rfind('x') == String::npos);

        // rfind on Unicode
        String u = String::fromUtf8("caf\xc3\xa9 caf\xc3\xa9", 11);
        CHECK(u.rfind(Char(static_cast<char32_t>(0xE9))) == 8);
        CHECK(u.rfind(Char(static_cast<char32_t>(0xE9)), 5) == 3);
        CHECK(u.rfind(Char('c')) == 5);

        // rfind on literal
        String lit = PROMEKI_STRING("Hello World Hello");
        CHECK(lit.rfind("Hello") == 12);
        CHECK(lit.rfind("Hello", 5) == 0);
        CHECK(lit.rfind('o') == 16);

        // rfind on unicode literal
        String ulit = PROMEKI_STRING("caf\xc3\xa9 caf\xc3\xa9");
        CHECK(ulit.rfind(Char(static_cast<char32_t>(0xE9))) == 8);
        CHECK(ulit.rfind(Char('c')) == 5);
}

// ============================================================================
// Cross-encoding find / count / contains
// ============================================================================

TEST_CASE("String_CrossEncodingFind") {
        // A Latin1 haystack containing 'é' as the raw byte 0xE9.
        String latin1Haystack(reinterpret_cast<const char *>("caf\xe9 caf\xe9"), 9);
        REQUIRE(latin1Haystack.encoding() == String::Latin1);
        REQUIRE(latin1Haystack.length() == 9);
        REQUIRE(latin1Haystack.charAt(3).codepoint() == 0xE9);

        // A Unicode needle containing the same 'é' codepoint, decoded from UTF-8.
        String unicodeNeedle = String::fromUtf8("caf\xc3\xa9", 5);
        REQUIRE(unicodeNeedle.encoding() == String::Unicode);
        REQUIRE(unicodeNeedle.length() == 4);
        REQUIRE(unicodeNeedle.charAt(3).codepoint() == 0xE9);

        // Logical equality across encodings.
        CHECK(latin1Haystack.charAt(3) == unicodeNeedle.charAt(3));

        // Regression: Latin1 haystack with a Unicode needle used to do a
        // byte-level find of the UTF-8 bytes (0xC3 0xA9) inside the Latin1
        // storage (which only contains 0xE9), so this returned npos.
        CHECK(latin1Haystack.find(unicodeNeedle) == 0);
        CHECK(latin1Haystack.find(unicodeNeedle, 1) == 5);
        CHECK(latin1Haystack.rfind(unicodeNeedle) == 5);
        CHECK(latin1Haystack.contains(unicodeNeedle));
        CHECK(latin1Haystack.count(unicodeNeedle) == 2);

        // (StringLiteralData has the same fix applied as defense-in-depth,
        //  but it is unreachable through the public API: PROMEKI_STRING
        //  always interprets its literal as UTF-8, so a literal containing
        //  raw Latin1 high bytes ends up in StringUnicodeLiteralData rather
        //  than StringLiteralData.  The runtime String("...") path used here
        //  for latin1Haystack covers the practical case.)

        // The reverse direction (Unicode haystack, Latin1 needle) was already
        // correct because StringUnicodeData::find walks codepoint-by-codepoint;
        // pin it down so it stays that way.
        String unicodeHaystack = String::fromUtf8("caf\xc3\xa9 caf\xc3\xa9", 11);
        REQUIRE(unicodeHaystack.encoding() == String::Unicode);
        String latin1Needle(reinterpret_cast<const char *>("caf\xe9"), 4);
        REQUIRE(latin1Needle.encoding() == String::Latin1);
        CHECK(unicodeHaystack.find(latin1Needle) == 0);
        CHECK(unicodeHaystack.rfind(latin1Needle) == 5);
        CHECK(unicodeHaystack.count(latin1Needle) == 2);

        // replace() also relies on find() — verify it now works across encodings.
        String replaced = latin1Haystack.replace(unicodeNeedle, String("X"));
        CHECK(replaced == String("X X"));
}

TEST_CASE("String_FindEmbeddedNul") {
        // std::string::find(const char *) is null-terminated and would
        // truncate the needle at the first embedded NUL.  Our find() takes
        // an explicit length, so embedded NULs in the needle must work.
        std::string haystack;
        haystack.append("abc", 3);
        haystack.push_back('\0');
        haystack.append("def", 3);
        String h(haystack);
        REQUIRE(h.length() == 7);

        std::string needle;
        needle.push_back('\0');
        needle.append("def", 3);
        String n(needle);
        REQUIRE(n.length() == 4);

        CHECK(h.find(n) == 3);
        CHECK(h.rfind(n) == 3);
        CHECK(h.count(n) == 1);
        CHECK(h.contains(n));
}

// ============================================================================
// substr storage narrowing
// ============================================================================

TEST_CASE("String_SubstrShrinksToLatin1") {
        // Slicing the ASCII tail of a Unicode string should drop back to
        // Latin1 storage rather than re-cloning into a Unicode codepoint list.
        // "caf<é> lait" → 9 codepoints; substr(5) is "lait", pure ASCII.
        String mixed = String::fromUtf8("caf\xc3\xa9 lait", 10);
        REQUIRE(mixed.encoding() == String::Unicode);
        REQUIRE(mixed.length() == 9);

        String tail = mixed.substr(5);
        CHECK(tail == String("lait"));
        CHECK(tail.encoding() == String::Latin1);

        // The "café" prefix also narrows: 'é' (0xE9) fits in Latin1.
        String head = mixed.substr(0, 4);
        CHECK(head.encoding() == String::Latin1);
        CHECK(head.length() == 4);
        CHECK(head.charAt(3).codepoint() == 0xE9);

        // A slice containing a non-Latin1 codepoint must stay Unicode.
        String jp = String::fromUtf8("a\xe6\x97\xa5", 4);  // "a日"
        REQUIRE(jp.encoding() == String::Unicode);
        REQUIRE(jp.length() == 2);
        String jpSlice = jp.substr(0, 2);
        CHECK(jpSlice.encoding() == String::Unicode);
        CHECK(jpSlice.charAt(1).codepoint() == 0x65E5);

        // Same optimization for the Unicode literal backend.
        String litMixed = PROMEKI_STRING("caf\xc3\xa9 lait");
        REQUIRE(litMixed.encoding() == String::Unicode);
        REQUIRE(litMixed.isLiteral());
        String litTail = litMixed.substr(5);
        CHECK(litTail.encoding() == String::Latin1);
        CHECK(litTail == String("lait"));
}

// ============================================================================
// replace
// ============================================================================

TEST_CASE("String_Replace") {
        // Basic replacement
        CHECK(String("Hello World").replace("World", "Earth") == "Hello Earth");

        // Replace all occurrences
        CHECK(String("aaa").replace("a", "bb") == "bbbbbb");

        // Replace with empty (deletion)
        CHECK(String("Hello World").replace("World", "") == "Hello ");

        // Replace empty find (no-op)
        CHECK(String("Hello").replace("", "x") == "Hello");

        // No match (no-op)
        CHECK(String("Hello").replace("xyz", "abc") == "Hello");

        // Replace at boundaries
        CHECK(String("Hello").replace("Hello", "Goodbye") == "Goodbye");
        CHECK(String("aXbXc").replace("X", "---") == "a---b---c");

        // Replace on empty string
        CHECK(String("").replace("a", "b") == "");

        // Replace with longer replacement
        CHECK(String("ab").replace("a", "aaa") == "aaab");

        // Unicode replacement
        String u = String::fromUtf8("caf\xc3\xa9 lait", 10);
        String result = u.replace(String::fromUtf8("caf\xc3\xa9", 5), String("coffee"));
        CHECK(result == "coffee lait");
}

// ============================================================================
// compareIgnoreCase
// ============================================================================

TEST_CASE("String_CompareIgnoreCase") {
        CHECK(String("Hello").compareIgnoreCase("hello") == 0);
        CHECK(String("HELLO").compareIgnoreCase("hello") == 0);
        CHECK(String("Hello").compareIgnoreCase("HELLO") == 0);
        CHECK(String("abc").compareIgnoreCase("abd") < 0);
        CHECK(String("abd").compareIgnoreCase("abc") > 0);
        CHECK(String("abc").compareIgnoreCase("abcd") < 0);
        CHECK(String("abcd").compareIgnoreCase("abc") > 0);
        CHECK(String("").compareIgnoreCase("") == 0);
        CHECK(String("").compareIgnoreCase("a") < 0);
        CHECK(String("a").compareIgnoreCase("") > 0);
}

// ============================================================================
// hash
// ============================================================================

TEST_CASE("String_Hash") {
        // Compile-time literal hash matches runtime hash
        String lit = PROMEKI_STRING("Hello");
        String reg = "Hello";
        CHECK(lit.hash() == reg.hash());

        // Unicode compile-time hash matches runtime hash
        String ulit = PROMEKI_STRING("caf\xc3\xa9");
        String ureg = String::fromUtf8("caf\xc3\xa9", 5).toUnicode();
        CHECK(ulit.hash() == ureg.hash());

        // Same string, same hash
        String a = "Hello";
        String b = "Hello";
        CHECK(a.hash() == b.hash());

        // Different strings, different hash
        String c = "World";
        CHECK(a.hash() != c.hash());

        // Empty string has a hash
        String empty;
        CHECK(empty.hash() == String().hash());

        // Hash is deterministic
        CHECK(String("test").hash() == String("test").hash());

        // std::hash specialization works
        std::hash<String> hasher;
        CHECK(hasher(a) == hasher(b));
        CHECK(hasher(a) != hasher(c));

        // Can be used in unordered_map
        std::unordered_map<String, int> map;
        map["Hello"] = 1;
        map["World"] = 2;
        CHECK(map["Hello"] == 1);
        CHECK(map["World"] == 2);
        CHECK(map.count("Missing") == 0);

        // Unicode string hash
        String u1 = String::fromUtf8("caf\xc3\xa9", 5);
        String u2 = String::fromUtf8("caf\xc3\xa9", 5);
        CHECK(u1.hash() == u2.hash());
}

TEST_CASE("String_HashCrossEncoding") {
        // Regression: Latin1 and Unicode storage of the same logical
        // characters used to hash to different values, which silently broke
        // std::unordered_map<String, T> lookups whenever a lookup key arrived
        // in a different encoding from the stored key.  Equal strings must
        // hash equally regardless of backend storage.

        // Pure ASCII: Latin1 ctor vs Unicode (forced via toUnicode).
        String asciiLatin1 = "Hello";
        String asciiUnicode = String("Hello").toUnicode();
        REQUIRE(asciiLatin1.encoding() == String::Latin1);
        REQUIRE(asciiUnicode.encoding() == String::Unicode);
        CHECK(asciiLatin1 == asciiUnicode);
        CHECK(asciiLatin1.hash() == asciiUnicode.hash());

        // Latin1 with high bytes vs Unicode of the same logical chars.
        String latin1Cafe(reinterpret_cast<const char *>("caf\xe9"), 4);
        String unicodeCafe = String::fromUtf8("caf\xc3\xa9", 5);
        REQUIRE(latin1Cafe.encoding() == String::Latin1);
        REQUIRE(unicodeCafe.encoding() == String::Unicode);
        CHECK(latin1Cafe == unicodeCafe);
        CHECK(latin1Cafe.hash() == unicodeCafe.hash());

        // Compile-time literal hashes also stay consistent across the
        // ASCII and Unicode literal backends.
        String asciiLit = PROMEKI_STRING("Hello");
        String unicodeLit = PROMEKI_STRING("caf\xc3\xa9");
        CHECK(asciiLit.hash() == String("Hello").hash());
        CHECK(asciiLit.hash() == String("Hello").toUnicode().hash());
        CHECK(unicodeLit.hash() == String::fromUtf8("caf\xc3\xa9", 5).hash());

        // unordered_map lookups must work across encodings.
        std::unordered_map<String, int> map;
        map[latin1Cafe] = 7;
        CHECK(map.count(unicodeCafe) == 1);
        CHECK(map[unicodeCafe] == 7);
}

TEST_CASE("String_LessThanCrossEncoding") {
        // Regression: operator< used to do a raw byte compare on the encoded
        // representation, which mixed Latin1 bytes with UTF-8 bytes for
        // mixed-encoding pairs.  For "é" that meant Latin1(0xE9) compared
        // less-than-or-greater-than Unicode(0xC3 0xA9) — even though both are
        // operator==-equal — which broke std::map<String, T> and
        // std::set<String> invariants whenever a key arrived in a different
        // encoding from a stored key.
        String latin1Cafe(reinterpret_cast<const char *>("caf\xe9"), 4);
        String unicodeCafe = String::fromUtf8("caf\xc3\xa9", 5);
        REQUIRE(latin1Cafe.encoding() == String::Latin1);
        REQUIRE(unicodeCafe.encoding() == String::Unicode);
        REQUIRE(latin1Cafe == unicodeCafe);

        // Strict-weak-ordering: equal under == means neither is less.
        CHECK_FALSE(latin1Cafe < unicodeCafe);
        CHECK_FALSE(unicodeCafe < latin1Cafe);

        // operator< must agree with codepoint order across encodings.
        String latin1A(reinterpret_cast<const char *>("\xe0"), 1);  // U+00E0 'à'
        String unicodeB = String::fromUtf8("\xc3\xa9", 2);          // U+00E9 'é'
        REQUIRE(latin1A.charAt(0).codepoint() < unicodeB.charAt(0).codepoint());
        CHECK(latin1A < unicodeB);
        CHECK_FALSE(unicodeB < latin1A);

        // std::set sanity: inserting both encodings of the same logical
        // string must not produce two distinct entries.
        std::set<String> s;
        s.insert(latin1Cafe);
        s.insert(unicodeCafe);
        CHECK(s.size() == 1);

        // std::map sanity: lookup with a different encoding finds the entry.
        std::map<String, int> m;
        m[latin1Cafe] = 11;
        CHECK(m.find(unicodeCafe) != m.end());
        CHECK(m[unicodeCafe] == 11);
}

TEST_CASE("String_FindCStrIsUtf8") {
        // find/rfind/contains C-string overloads must interpret their argument
        // as UTF-8 (matching operator==(const char*) and the _ps literal),
        // not as a Latin1 byte sequence.  Previously the C-string overloads
        // built a Latin1 needle via String(const char*), so a multi-byte
        // UTF-8 literal was treated as several distinct Latin1 chars and
        // never matched the corresponding logical character in the haystack.

        // Latin1 haystack with high byte 'é' (0xE9), UTF-8 needle "é".
        String latin1Haystack(reinterpret_cast<const char *>("caf\xe9 caf\xe9"), 9);
        REQUIRE(latin1Haystack.encoding() == String::Latin1);
        CHECK(latin1Haystack.find("caf\xc3\xa9") == 0);
        CHECK(latin1Haystack.rfind("caf\xc3\xa9") == 5);
        CHECK(latin1Haystack.contains("caf\xc3\xa9"));

        // Unicode haystack, UTF-8 needle.
        String unicodeHaystack = String::fromUtf8("caf\xc3\xa9 caf\xc3\xa9", 11);
        REQUIRE(unicodeHaystack.encoding() == String::Unicode);
        CHECK(unicodeHaystack.find("caf\xc3\xa9") == 0);
        CHECK(unicodeHaystack.rfind("caf\xc3\xa9") == 5);
        CHECK(unicodeHaystack.contains("caf\xc3\xa9"));

        // Pure-ASCII C-string needles still work the same way they used to,
        // because the ASCII fast path produces a Latin1 needle either way.
        CHECK(latin1Haystack.find("caf") == 0);
        CHECK(unicodeHaystack.find("caf") == 0);
        CHECK_FALSE(latin1Haystack.contains("xyz"));
}

TEST_CASE("String_EqualsCStrIsUtf8") {
        // operator==(const char *) interprets the C-string argument as UTF-8,
        // matching String::fromUtf8 / the _ps literal convention.  This is
        // independent of how *this is stored.

        // Latin1 storage matching ASCII C-string.
        CHECK(String("Hello") == "Hello");

        // Unicode storage matching the UTF-8 bytes of its content.
        CHECK(String::fromUtf8("caf\xc3\xa9", 5) == "caf\xc3\xa9");

        // Latin1 storage compared to a UTF-8 literal of the same logical
        // characters (matches via codepoint comparison after UTF-8 decode).
        String latin1Cafe(reinterpret_cast<const char *>("caf\xe9"), 4);
        CHECK(latin1Cafe == "caf\xc3\xa9");

        // Length mismatches still fail correctly.
        CHECK_FALSE(String("Hello") == "Hell");
        CHECK_FALSE(String("Hello") == "Hello!");

        // Empty cases.
        CHECK(String() == "");
        CHECK(String() == static_cast<const char *>(nullptr));
        CHECK_FALSE(String("x") == "");
}

// ============================================================================
// find(const char *)
// ============================================================================

TEST_CASE("String_FindCstr") {
        String s = "Hello World";
        CHECK(s.find("World") == 6);
        CHECK(s.find("xyz") == String::npos);
        CHECK(s.find("Hello", 1) == String::npos);
        CHECK(s.find("llo", 0) == 2);
}

// ============================================================================
// Large string operations
// ============================================================================

TEST_CASE("String_LargeString") {
        // Build a large Latin1 string
        String large;
        for(int i = 0; i < 1000; ++i) {
                large += "abcdefghij";
        }
        CHECK(large.length() == 10000);
        CHECK(large.find("abcdefghij", 9990) == 9990);
        CHECK(large.rfind("abcdefghij") == 9990);
        CHECK(large.contains("efghij"));
        CHECK(large.count("abc") == 1000);

        // Erase from large string
        String copy = large;
        copy.erase(0, 5000);
        CHECK(copy.length() == 5000);

        // Replace in large string
        String replaced = large.replace("abc", "XYZ");
        CHECK(replaced.length() == 10000);
        CHECK(replaced.find("abc") == String::npos);
        CHECK(replaced.count("XYZ") == 1000);
}

TEST_CASE("ByteCountStyle: slices to Enum for Variant/runtime APIs") {
        // TypedEnum inherits publicly from Enum, so a const Enum &
        // reference on the base works for call sites that haven't
        // been migrated to the typed form.
        const Enum &asEnum = ByteCountStyle::Binary;
        CHECK(asEnum.typeName() == "ByteCountStyle");
        CHECK(asEnum.valueName() == "Binary");
        CHECK(asEnum.value() == 1);
}

TEST_CASE("ByteCountStyle: constructs from integer value") {
        ByteCountStyle s(1);
        CHECK(s.value() == 1);
        CHECK(s.valueName() == "Binary");
        CHECK(s == ByteCountStyle::Binary);
}

TEST_CASE("ByteCountStyle: constructs from registered name") {
        ByteCountStyle s(String("Metric"));
        CHECK(s.value() == 0);
        CHECK(s == ByteCountStyle::Metric);
}
