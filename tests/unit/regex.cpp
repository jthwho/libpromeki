/**
 * @file      regex.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/regex.h>

using namespace promeki;

TEST_CASE("RegEx: construction from String") {
        RegEx re("abc");
        CHECK(re.pattern() == "abc");
}

TEST_CASE("RegEx: construction from const char*") {
        RegEx re("test");
        CHECK(re.pattern() == "test");
}

TEST_CASE("RegEx: match full string") {
        RegEx re("^hello$");
        CHECK(re.match("hello"));
        CHECK_FALSE(re.match("hello world"));
}

TEST_CASE("RegEx: match with pattern") {
        RegEx re("^[0-9]+$");
        CHECK(re.match("12345"));
        CHECK_FALSE(re.match("abc"));
        CHECK_FALSE(re.match("123abc"));
}

TEST_CASE("RegEx: search finds substring") {
        RegEx re("world");
        CHECK(re.search("hello world"));
        CHECK_FALSE(re.search("hello earth"));
}

TEST_CASE("RegEx: search with regex pattern") {
        RegEx re("[0-9]+");
        CHECK(re.search("abc 123 def"));
        CHECK_FALSE(re.search("no digits here"));
}

TEST_CASE("RegEx: matches returns all matches") {
        RegEx re("[0-9]+");
        auto  results = re.matches("abc 123 def 456 ghi 789");
        CHECK(results.size() == 3);
        CHECK(results[0] == "123");
        CHECK(results[1] == "456");
        CHECK(results[2] == "789");
}

TEST_CASE("RegEx: matches with no results") {
        RegEx re("[0-9]+");
        auto  results = re.matches("no digits");
        CHECK(results.isEmpty());
}

TEST_CASE("RegEx: case insensitive flag") {
        RegEx re("hello", RegEx::IgnoreCase | RegEx::ECMAScript);
        CHECK(re.match("HELLO"));
        CHECK(re.match("Hello"));
        CHECK(re.match("hello"));
}

TEST_CASE("RegEx: assignment from String") {
        RegEx re("old");
        CHECK(re.match("old"));
        re = String("new");
        CHECK(re.pattern() == "new");
        CHECK(re.match("new"));
        CHECK_FALSE(re.match("old"));
}

TEST_CASE("RegEx: email-like pattern") {
        RegEx re("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
        CHECK(re.match("test@example.com"));
        CHECK_FALSE(re.match("not-an-email"));
}

TEST_CASE("RegEx: invalid pattern leaves regex invalid (no throw)") {
        // Unbalanced bracket: [ with no ].
        RegEx re("[abc");
        CHECK_FALSE(re.isValid());
        CHECK_FALSE(re.match("abc"));
        CHECK_FALSE(re.search("xabcy"));
        CHECK(re.matches("abcabc").isEmpty());
}

TEST_CASE("RegEx: valid pattern reports valid") {
        RegEx re("[abc]");
        CHECK(re.isValid());
        CHECK(re.match("a"));
}

TEST_CASE("RegEx: default-constructed regex is invalid") {
        RegEx re;
        CHECK_FALSE(re.isValid());
        CHECK_FALSE(re.match("anything"));
}

TEST_CASE("RegEx: compile() reports OK on valid pattern") {
        auto [re, err] = RegEx::compile("[0-9]+");
        CHECK(err.isOk());
        CHECK(re.isValid());
        CHECK(re.match("12345"));
}

TEST_CASE("RegEx: compile() reports Invalid on bad pattern") {
        auto [re, err] = RegEx::compile("[bad");
        CHECK(err == Error::Invalid);
        CHECK_FALSE(re.isValid());
}

TEST_CASE("RegEx: assignment to bad pattern leaves invalid (no throw)") {
        RegEx re("ok");
        CHECK(re.isValid());
        re = String("(unbalanced");
        CHECK_FALSE(re.isValid());
        CHECK_FALSE(re.match("ok"));
}
