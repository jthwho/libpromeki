/**
 * @file      string.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <iostream>
#include <thread>
#include <vector>
#include <promeki/unittest.h>
#include <promeki/string.h>

using namespace promeki;

// ============================================================================
// Construction
// ============================================================================

PROMEKI_TEST_BEGIN(String_Construction)
        // Default construction
        String nullstr;
        PROMEKI_TEST(nullstr.isEmpty());
        PROMEKI_TEST(nullstr.size() == 0);
        PROMEKI_TEST(nullstr.referenceCount() == 1);

        // From C string
        String s1 = "Hello";
        PROMEKI_TEST(s1 == "Hello");
        PROMEKI_TEST(s1.size() == 5);

        // From nullptr
        String s2(nullptr);
        PROMEKI_TEST(s2.isEmpty());

        // From C string with length
        String s3("Hello World", 5);
        PROMEKI_TEST(s3 == "Hello");

        // From repeated char
        String s4(5, 'x');
        PROMEKI_TEST(s4 == "xxxxx");

        // From std::string
        std::string stdstr = "StdString";
        String s5(stdstr);
        PROMEKI_TEST(s5 == "StdString");

        // From std::string rvalue
        String s6(std::string("Moved"));
        PROMEKI_TEST(s6 == "Moved");
PROMEKI_TEST_END()

// ============================================================================
// Copy-on-write semantics
// ============================================================================

PROMEKI_TEST_BEGIN(String_CopyOnWrite)
        String s1 = "Original";
        PROMEKI_TEST(s1.referenceCount() == 1);

        // Copy should share data
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);
        PROMEKI_TEST(s2.referenceCount() == 2);
        PROMEKI_TEST(s1 == s2);
        PROMEKI_TEST(s1 == "Original");

        // Mutating s2 should detach (COW)
        s2[0] = 'X';
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s2.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Original");
        PROMEKI_TEST(s2 == "Xriginal");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_CopyOnWriteAppend)
        String s1 = "Hello";
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);

        // operator+= should detach s2
        s2 += " World";
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s2.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Hello");
        PROMEKI_TEST(s2 == "Hello World");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_CopyOnWriteClear)
        String s1 = "Hello";
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);

        s2.clear();
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s2.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Hello");
        PROMEKI_TEST(s2.isEmpty());
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_CopyOnWriteChain)
        String s1 = "Original";
        String s2 = s1;
        String s3 = s1;
        String s4 = s1;
        PROMEKI_TEST(s1.referenceCount() == 4);

        // Mutate s3 — only s3 should detach
        s3 += "!";
        PROMEKI_TEST(s1.referenceCount() == 3);
        PROMEKI_TEST(s3.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Original");
        PROMEKI_TEST(s3 == "Original!");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_CopyOnWriteAssign)
        String s1 = "Hello";
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);

        // Assigning a new value should release old shared data
        s2 = "Goodbye";
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s2.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Hello");
        PROMEKI_TEST(s2 == "Goodbye");
PROMEKI_TEST_END()

// ============================================================================
// String operations (original tests)
// ============================================================================

PROMEKI_TEST_BEGIN(String_Operations)
        String s1 = "String 1";
        String s2("String 2");
        String s3(s2);
        String s4(" \t \n \r WHITE \n\t\nSPACE  \t\n\t\t\n   ");
        String s5("item1,item2,item3");

        PROMEKI_TEST(s1.toUpper() == "STRING 1");
        PROMEKI_TEST(s1.toLower() == "string 1");
        PROMEKI_TEST(s1 == "String 1");
        PROMEKI_TEST(s2 == "String 2");
        PROMEKI_TEST(s3 == "String 2");
        PROMEKI_TEST(s2 == s3);
        PROMEKI_TEST(s4.trim() == "WHITE \n\t\nSPACE");
        auto s5split = s5.split(",");
        PROMEKI_TEST(s5split.size() == 3);
        PROMEKI_TEST(s5split.at(0) == "item1");
        PROMEKI_TEST(s5split.at(1) == "item2");
        PROMEKI_TEST(s5split.at(2) == "item3");
        PROMEKI_TEST(s1.startsWith("Stri"));
        PROMEKI_TEST(!s1.startsWith("StrI"));
        PROMEKI_TEST(s1.endsWith("g 1"));
        PROMEKI_TEST(!s1.endsWith("gg1"));
        PROMEKI_TEST(s5.count("item") == 3);
        PROMEKI_TEST(s1.reverse() == "1 gnirtS");
        PROMEKI_TEST(String("1234").isNumeric());
        PROMEKI_TEST(!s1.isNumeric());
        PROMEKI_TEST(String::dec(1234) == "1234");
        PROMEKI_TEST(String::dec(1234, 6) == "  1234");
        PROMEKI_TEST(String::dec(1234, 6, 'x') == "xx1234");
        PROMEKI_TEST(String::hex(0x42, 4) == "0x0042");
        PROMEKI_TEST(String::bin(0b11111, 8) == "0b00011111");
        PROMEKI_TEST(String::number(42435) == "42435");
        PROMEKI_TEST(String::number(0x3472, 16) == "3472");
        PROMEKI_TEST(String::number(12345, 10, 10) == "     12345");
        PROMEKI_TEST(String::number(12345, 10, -10) == "12345     ");
        PROMEKI_TEST(String::number(12345, 20, 10, ' ', true) == "  b20:1AH5");
        PROMEKI_TEST(String::number(12345, 4, 6, ' ', true) == "b4:3000321");
        PROMEKI_TEST(String("%3 %2 %1").arg(3).arg(2).arg(1) == "1 2 3");
        PROMEKI_TEST(String("Two hundred and twenty-six billion, four hundred eighty-three million, One Hundred And Thirty-Four Thousand Two Hundred and Ninety-Six").parseNumberWords() == 226483134296);

        // Range-for mutation (tests non-const begin/end with COW)
        for(char &c : s2) c = 'x';
        PROMEKI_TEST(s2 == "xxxxxxxx");
PROMEKI_TEST_END()

// ============================================================================
// Operator tests
// ============================================================================

PROMEKI_TEST_BEGIN(String_Concatenation)
        String a = "Hello";
        String b = " World";

        // operator+ (String)
        String c = a + b;
        PROMEKI_TEST(c == "Hello World");
        PROMEKI_TEST(a == "Hello");
        PROMEKI_TEST(b == " World");

        // operator+ (const char *)
        String d = a + " There";
        PROMEKI_TEST(d == "Hello There");

        // operator+ (char)
        String e = a + '!';
        PROMEKI_TEST(e == "Hello!");

        // operator+ (std::string)
        String f = a + std::string(" Std");
        PROMEKI_TEST(f == "Hello Std");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_AppendOperators)
        String s = "Hello";

        s += " World";
        PROMEKI_TEST(s == "Hello World");

        s += std::string("!");
        PROMEKI_TEST(s == "Hello World!");

        s += '?';
        PROMEKI_TEST(s == "Hello World!?");

        String suffix = " End";
        s += suffix;
        PROMEKI_TEST(s == "Hello World!? End");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_Comparison)
        String a = "abc";
        String b = "abc";
        String c = "def";

        PROMEKI_TEST(a == b);
        PROMEKI_TEST(a != c);
        PROMEKI_TEST(a == "abc");
        PROMEKI_TEST(a != "def");

        // Single char comparison
        String single = "x";
        PROMEKI_TEST(single == 'x');
        PROMEKI_TEST(single != 'y');
        String multi = "xx";
        PROMEKI_TEST(multi != 'x');

        // operator<
        PROMEKI_TEST(a < c);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_IndexOperator)
        String s = "Hello";
        PROMEKI_TEST(s[0] == 'H');
        PROMEKI_TEST(s[4] == 'o');

        // Non-const index (mutating)
        String s2 = s;
        PROMEKI_TEST(s.referenceCount() == 2);
        s2[0] = 'J';
        PROMEKI_TEST(s.referenceCount() == 1);
        PROMEKI_TEST(s == "Hello");
        PROMEKI_TEST(s2 == "Jello");
PROMEKI_TEST_END()

// ============================================================================
// Assignment
// ============================================================================

PROMEKI_TEST_BEGIN(String_Assignment)
        String s;

        s = "Hello";
        PROMEKI_TEST(s == "Hello");

        s = std::string("World");
        PROMEKI_TEST(s == "World");

        std::string tmp = "Moved";
        s = std::move(tmp);
        PROMEKI_TEST(s == "Moved");

        String other = "Other";
        s = other;
        PROMEKI_TEST(s == "Other");
        PROMEKI_TEST(s.referenceCount() == 2);
PROMEKI_TEST_END()

// ============================================================================
// Substring operations
// ============================================================================

PROMEKI_TEST_BEGIN(String_Substrings)
        String s = "Hello World";

        PROMEKI_TEST(s.substr(0, 5) == "Hello");
        PROMEKI_TEST(s.substr(6) == "World");
        PROMEKI_TEST(s.mid(6, 5) == "World");
        PROMEKI_TEST(s.left(5) == "Hello");
        PROMEKI_TEST(s.right(5) == "World");
        PROMEKI_TEST(s.find('W') == 6);
PROMEKI_TEST_END()

// ============================================================================
// Conversion operators
// ============================================================================

PROMEKI_TEST_BEGIN(String_Conversions)
        String s = "Test";

        // const std::string &
        const std::string &ref = s.stds();
        PROMEKI_TEST(ref == "Test");

        // const char *
        const char *cstr = s.cstr();
        PROMEKI_TEST(std::string(cstr) == "Test");

        // operator const char *
        const char *cstr2 = s;
        PROMEKI_TEST(std::string(cstr2) == "Test");
PROMEKI_TEST_END()

// ============================================================================
// Iterators
// ============================================================================

PROMEKI_TEST_BEGIN(String_ConstIterators)
        const String s = "Hello";

        // const iteration should not trigger COW
        std::string result;
        for(auto it = s.begin(); it != s.end(); ++it) {
                result += *it;
        }
        PROMEKI_TEST(result == "Hello");
        PROMEKI_TEST(s.referenceCount() == 1);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(String_NonConstIterators)
        String s1 = "Hello";
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);

        // Non-const begin/end should trigger COW detach
        for(auto it = s2.begin(); it != s2.end(); ++it) {
                *it = 'x';
        }
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s2.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Hello");
        PROMEKI_TEST(s2 == "xxxxx");
PROMEKI_TEST_END()

// ============================================================================
// stds() and modify semantics
// ============================================================================

PROMEKI_TEST_BEGIN(String_StdsNonConst)
        String s1 = "Hello";
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);

        // Non-const stds() should trigger detach
        s2.stds() = "Changed";
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s1 == "Hello");
        PROMEKI_TEST(s2 == "Changed");
PROMEKI_TEST_END()

// ============================================================================
// Resize and clear
// ============================================================================

PROMEKI_TEST_BEGIN(String_ResizeAndClear)
        String s = "Hello";
        s.resize(3);
        PROMEKI_TEST(s == "Hel");
        PROMEKI_TEST(s.size() == 3);

        s.clear();
        PROMEKI_TEST(s.isEmpty());
        PROMEKI_TEST(s.size() == 0);
PROMEKI_TEST_END()

// ============================================================================
// startsWith / endsWith edge cases
// ============================================================================

PROMEKI_TEST_BEGIN(String_StartsEndsEdgeCases)
        String s = "Hello";

        PROMEKI_TEST(s.startsWith("Hello"));
        PROMEKI_TEST(s.startsWith(""));
        PROMEKI_TEST(s.startsWith('H'));
        PROMEKI_TEST(!s.startsWith('h'));

        PROMEKI_TEST(s.endsWith("Hello"));
        PROMEKI_TEST(s.endsWith("o"));
        PROMEKI_TEST(!s.endsWith("Hello World"));

        String empty;
        PROMEKI_TEST(!empty.startsWith('x'));
PROMEKI_TEST_END()

// ============================================================================
// toUpper / toLower don't mutate original
// ============================================================================

PROMEKI_TEST_BEGIN(String_CaseConversion)
        String s = "Hello World";
        String upper = s.toUpper();
        String lower = s.toLower();

        PROMEKI_TEST(s == "Hello World");
        PROMEKI_TEST(upper == "HELLO WORLD");
        PROMEKI_TEST(lower == "hello world");
PROMEKI_TEST_END()

// ============================================================================
// trim edge cases
// ============================================================================

PROMEKI_TEST_BEGIN(String_TrimEdgeCases)
        PROMEKI_TEST(String("  hello  ").trim() == "hello");
        PROMEKI_TEST(String("hello").trim() == "hello");
        PROMEKI_TEST(String("   ").trim() == "");
        PROMEKI_TEST(String("").trim() == "");
PROMEKI_TEST_END()

// ============================================================================
// sprintf
// ============================================================================

PROMEKI_TEST_BEGIN(String_Sprintf)
        String s = String::sprintf("Hello %s %d", "World", 42);
        PROMEKI_TEST(s == "Hello World 42");

        String s2 = String::sprintf("%05d", 42);
        PROMEKI_TEST(s2 == "00042");
PROMEKI_TEST_END()

// ============================================================================
// Numeric conversions
// ============================================================================

PROMEKI_TEST_BEGIN(String_NumericConversions)
        PROMEKI_TEST(String("42").toInt() == 42);
        PROMEKI_TEST(String("42").toUInt() == 42);
        PROMEKI_TEST(String("3.14").toDouble() > 3.13);
        PROMEKI_TEST(String("3.14").toDouble() < 3.15);
        PROMEKI_TEST(String("true").toBool() == true);
        PROMEKI_TEST(String("false").toBool() == false);
        PROMEKI_TEST(String("1").toBool() == true);
        PROMEKI_TEST(String("0").toBool() == false);
        PROMEKI_TEST(String("TRUE").toBool() == true);
        PROMEKI_TEST(String("FALSE").toBool() == false);
PROMEKI_TEST_END()

// ============================================================================
// Thread safety of COW
// ============================================================================

static void stringThreadFunc(String str, int iterations) {
        for(int i = 0; i < iterations; i++) {
                String local = str;
                (void)local.size();
                (void)local.cstr();
        }
}

PROMEKI_TEST_BEGIN(String_ThreadSafeCOW)
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

        PROMEKI_TEST(shared.referenceCount() == 1);
        PROMEKI_TEST(shared == "Thread safe string data");
PROMEKI_TEST_END()

// ============================================================================
// Arg placeholder replacement
// ============================================================================

PROMEKI_TEST_BEGIN(String_ArgReplacement)
        PROMEKI_TEST(String("%1").arg("hello") == "hello");
        PROMEKI_TEST(String("%1 %2").arg("a").arg("b") == "a b");
        PROMEKI_TEST(String("%2 %1").arg("a").arg("b") == "b a");
        PROMEKI_TEST(String("%3 %2 %1").arg(3).arg(2).arg(1) == "1 2 3");
        PROMEKI_TEST(String("no placeholders").arg("x") == "no placeholders");
PROMEKI_TEST_END()

// ============================================================================
// COW with arg (mutating in-place)
// ============================================================================

PROMEKI_TEST_BEGIN(String_CopyOnWriteArg)
        String s1 = "%1 world";
        String s2 = s1;
        PROMEKI_TEST(s1.referenceCount() == 2);

        s2.arg("hello");
        PROMEKI_TEST(s1.referenceCount() == 1);
        PROMEKI_TEST(s1 == "%1 world");
        PROMEKI_TEST(s2 == "hello world");
PROMEKI_TEST_END()

// ============================================================================
// Empty string sharing
// ============================================================================

PROMEKI_TEST_BEGIN(String_EmptyStrings)
        String a;
        String b;
        // Each empty string has its own Data (they're constructed independently)
        PROMEKI_TEST(a.isEmpty());
        PROMEKI_TEST(b.isEmpty());
        PROMEKI_TEST(a == b);

        // Copy should share
        String c = a;
        PROMEKI_TEST(a.referenceCount() == 2);
        PROMEKI_TEST(c.referenceCount() == 2);
PROMEKI_TEST_END()

// ============================================================================
// Bool number conversion
// ============================================================================

PROMEKI_TEST_BEGIN(String_BoolNumber)
        PROMEKI_TEST(String::number(true) == "true");
        PROMEKI_TEST(String::number(false) == "false");
PROMEKI_TEST_END()
