/**
 * @file      rational.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/rational.h>

using namespace promeki;

TEST_CASE("Rational: default construction") {
        Rational r;
        CHECK(r.isValid());
        CHECK(r.numerator() == 0);
        CHECK(r.denominator() == 1);
}

TEST_CASE("Rational: construction with values") {
        Rational r(3, 4);
        CHECK(r.isValid());
        CHECK(r.numerator() == 3);
        CHECK(r.denominator() == 4);
}

TEST_CASE("Rational: automatic simplification") {
        Rational r(6, 8);
        CHECK(r.numerator() == 3);
        CHECK(r.denominator() == 4);
}

TEST_CASE("Rational: zero denominator is invalid") {
        Rational r(1, 0);
        CHECK_FALSE(r.isValid());
}

TEST_CASE("Rational: addition") {
        Rational a(1, 4);
        Rational b(1, 4);
        Rational c = a + b;
        CHECK(c.isValid());
        CHECK(c.numerator() == 1);
        CHECK(c.denominator() == 2);
}

TEST_CASE("Rational: addition with different denominators") {
        Rational a(1, 3);
        Rational b(1, 6);
        Rational c = a + b;
        CHECK(c.isValid());
        CHECK(c.numerator() == 1);
        CHECK(c.denominator() == 2);
}

TEST_CASE("Rational: addition with invalid operand") {
        Rational a(1, 2);
        Rational b(1, 0);
        Rational c = a + b;
        CHECK_FALSE(c.isValid());
}

TEST_CASE("Rational: subtraction") {
        Rational a(3, 4);
        Rational b(1, 4);
        Rational c = a - b;
        CHECK(c.isValid());
        CHECK(c.numerator() == 1);
        CHECK(c.denominator() == 2);
}

TEST_CASE("Rational: multiplication") {
        Rational a(2, 3);
        Rational b(3, 4);
        Rational c = a * b;
        CHECK(c.isValid());
        CHECK(c.numerator() == 1);
        CHECK(c.denominator() == 2);
}

TEST_CASE("Rational: division") {
        Rational a(1, 2);
        Rational b(3, 4);
        Rational c = a / b;
        CHECK(c.isValid());
        CHECK(c.numerator() == 2);
        CHECK(c.denominator() == 3);
}

TEST_CASE("Rational: division by zero returns invalid") {
        Rational a(1, 2);
        Rational b(0, 1);
        Rational c = a / b;
        CHECK_FALSE(c.isValid());
}

TEST_CASE("Rational: equality") {
        Rational a(1, 2);
        Rational b(2, 4);
        CHECK(a == b);
}

TEST_CASE("Rational: inequality") {
        Rational a(1, 2);
        Rational b(1, 3);
        CHECK(a != b);
}

TEST_CASE("Rational: toDouble") {
        Rational r(1, 4);
        CHECK(r.toDouble() == doctest::Approx(0.25));
}

TEST_CASE("Rational: toDouble on invalid returns 0") {
        Rational r(1, 0);
        CHECK(r.toDouble() == doctest::Approx(0.0));
}

TEST_CASE("Rational: toString") {
        Rational r(3, 4);
        CHECK(r.toString() == "3/4");
}

TEST_CASE("Rational: implicit String conversion") {
        Rational r(1, 2);
        String s = r;
        CHECK(s == "1/2");
}

TEST_CASE("Rational: whole number") {
        Rational r(6, 3);
        CHECK(r.numerator() == 2);
        CHECK(r.denominator() == 1);
}

TEST_CASE("Rational: negative numerator") {
        Rational r(-3, 6);
        CHECK(r.numerator() == -1);
        CHECK(r.denominator() == 2);
}
