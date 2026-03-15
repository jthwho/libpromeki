/**
 * @file      error.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/error.h>
#include <promeki/core/string.h>

using namespace promeki;

TEST_CASE("Error: default construction is Ok") {
        Error e;
        CHECK(e.isOk());
        CHECK_FALSE(e.isError());
        CHECK(e.code() == Error::Ok);
}

TEST_CASE("Error: construction with error code") {
        Error e(Error::Invalid);
        CHECK(e.isError());
        CHECK_FALSE(e.isOk());
        CHECK(e.code() == Error::Invalid);
}

TEST_CASE("Error: equality comparison") {
        Error a(Error::Ok);
        Error b(Error::Ok);
        Error c(Error::IOError);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("Error: ordering comparison") {
        Error a(Error::Ok);
        Error b(Error::Invalid);
        CHECK(a < b);
        CHECK(a <= b);
        CHECK(b > a);
        CHECK(b >= a);
        CHECK(a <= a);
        CHECK(a >= a);
}

TEST_CASE("Error: name returns non-empty string") {
        Error e(Error::Invalid);
        CHECK_FALSE(e.name().isEmpty());
}

TEST_CASE("Error: desc returns non-empty string") {
        Error e(Error::Invalid);
        CHECK_FALSE(e.desc().isEmpty());
}

TEST_CASE("Error: Ok name") {
        Error e(Error::Ok);
        CHECK_FALSE(e.name().isEmpty());
}

TEST_CASE("Error: all codes are distinct") {
        Error ok(Error::Ok);
        Error inv(Error::Invalid);
        Error io(Error::IOError);
        Error oom(Error::NoMem);
        CHECK(ok != inv);
        CHECK(inv != io);
        CHECK(io != oom);
}

TEST_CASE("Error: copy construction") {
        Error a(Error::Timeout);
        Error b(a);
        CHECK(a == b);
        CHECK(b.code() == Error::Timeout);
}

TEST_CASE("Error: assignment") {
        Error a(Error::Timeout);
        Error b;
        b = a;
        CHECK(b.code() == Error::Timeout);
}

TEST_CASE("Error: domain-specific codes have names") {
        Error a(Error::InvalidArgument);
        CHECK_FALSE(a.name().isEmpty());
        CHECK(a.isError());
        Error b(Error::InvalidDimension);
        CHECK_FALSE(b.name().isEmpty());
        CHECK(b.isError());
}
