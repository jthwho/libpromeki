/**
 * @file      result.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/result.h>
#include <promeki/core/string.h>

using namespace promeki;

TEST_CASE("Result: makeResult") {
        auto r = makeResult<int>(42);
        CHECK(r.first() == 42);
        CHECK(r.second().isOk());
        CHECK(isOk(r));
        CHECK_FALSE(isError(r));
}

TEST_CASE("Result: makeError") {
        auto r = makeError<int>(Error::Invalid);
        CHECK(r.first() == 0);
        CHECK(r.second() == Error::Invalid);
        CHECK(isError(r));
        CHECK_FALSE(isOk(r));
}

TEST_CASE("Result: value and error accessors") {
        auto r = makeResult<String>("hello");
        CHECK(value(r) == "hello");
        CHECK(error(r).isOk());
}

TEST_CASE("Result: structured bindings") {
        auto r = makeResult<int>(99);
        auto [val, err] = r;
        CHECK(val == 99);
        CHECK(err.isOk());
}

TEST_CASE("Result: structured bindings with error") {
        auto r = makeError<String>(Error::NotExist);
        auto [val, err] = r;
        CHECK(val.isEmpty());
        CHECK(err == Error::NotExist);
}

TEST_CASE("Result: factory pattern usage") {
        // Simulates a from*() factory returning Result<T>
        auto parse = [](const String &input) -> Result<int> {
                if(input == "42") return makeResult<int>(42);
                return makeError<int>(Error::Invalid);
        };

        auto [v1, e1] = parse("42");
        CHECK(v1 == 42);
        CHECK(e1.isOk());

        auto [v2, e2] = parse("bad");
        CHECK(e2 == Error::Invalid);
}
