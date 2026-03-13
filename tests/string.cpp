/**
 * @file      string.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <doctest/doctest.h>
#include <promeki/char.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

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

        // fromUtf8 creates Unicode string
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

        // Trim preserves encoding
        CHECK(trimmed.encoding() == String::Unicode);

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
