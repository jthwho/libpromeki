/**
 * @file      string.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <iostream>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

using namespace promeki;

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
// Copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("String_CopyIsIndependent") {
        String s1 = "Original";

        // Copy is a value copy
        String s2 = s1;
        CHECK(s1 == s2);
        CHECK(s1 == "Original");

        // Mutating s2 does not affect s1
        s2[0] = 'X';
        CHECK(s1 == "Original");
        CHECK(s2 == "Xriginal");
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

        // Range-for mutation
        for(char &c : s2) c = 'x';
        CHECK(s2 == "xxxxxxxx");
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

TEST_CASE("String_IndexOperator") {
        String s = "Hello";
        CHECK(s[0] == 'H');
        CHECK(s[4] == 'o');

        // Non-const index (mutating)
        String s2 = s;
        s2[0] = 'J';
        CHECK(s == "Hello");
        CHECK(s2 == "Jello");
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

        const std::string &ref = s.stds();
        CHECK(ref == "Test");

        const char *cstr = s.cstr();
        CHECK(std::string(cstr) == "Test");

        const char *cstr2 = s;
        CHECK(std::string(cstr2) == "Test");
}

// ============================================================================
// Iterators
// ============================================================================

TEST_CASE("String_ConstIterators") {
        const String s = "Hello";

        std::string result;
        for(auto it = s.begin(); it != s.end(); ++it) {
                result += *it;
        }
        CHECK(result == "Hello");
}

TEST_CASE("String_NonConstIterators") {
        String s1 = "Hello";
        String s2 = s1;

        for(auto it = s2.begin(); it != s2.end(); ++it) {
                *it = 'x';
        }
        CHECK(s1 == "Hello");
        CHECK(s2 == "xxxxx");
}

// ============================================================================
// stds() mutation
// ============================================================================

TEST_CASE("String_StdsNonConst") {
        String s1 = "Hello";
        String s2 = s1;

        s2.stds() = "Changed";
        CHECK(s1 == "Hello");
        CHECK(s2 == "Changed");
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
// Thread safety (String copies are independent values)
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
// UTF-8 length
// ============================================================================

TEST_CASE("String_Utf8Length") {
        // Pure ASCII: utf8Length == length
        String ascii("hello");
        CHECK(ascii.length() == 5);
        CHECK(ascii.utf8Length() == 5);

        // Empty string
        String empty;
        CHECK(empty.utf8Length() == 0);

        // 2-byte codepoints: "café" (é = 0xC3 0xA9)
        String cafe("caf\xc3\xa9");
        CHECK(cafe.length() == 5);
        CHECK(cafe.utf8Length() == 4);

        // 3-byte codepoints: "日本" (each char is 3 bytes)
        String jp("\xe6\x97\xa5\xe6\x9c\xac");
        CHECK(jp.length() == 6);
        CHECK(jp.utf8Length() == 2);

        // 4-byte codepoint: emoji 😀 (U+1F600 = 0xF0 0x9F 0x98 0x80)
        String emoji("\xf0\x9f\x98\x80");
        CHECK(emoji.length() == 4);
        CHECK(emoji.utf8Length() == 1);
}
