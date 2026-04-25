/**
 * @file      httpstatus.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpstatus.h>

using namespace promeki;

TEST_CASE("HttpStatus") {
        SUBCASE("default is Ok (200)") {
                HttpStatus s;
                CHECK(s == HttpStatus::Ok);
                CHECK(s.value() == 200);
        }

        SUBCASE("integer constructor accepts non-well-known codes") {
                HttpStatus teapot{418};
                CHECK(teapot.value() == 418);
                CHECK(teapot.reasonPhrase() == "I'm a Teapot");
        }

        SUBCASE("reasonPhrase covers canonical codes") {
                CHECK(HttpStatus::Ok.reasonPhrase()                  == "OK");
                CHECK(HttpStatus::Created.reasonPhrase()             == "Created");
                CHECK(HttpStatus::NoContent.reasonPhrase()           == "No Content");
                CHECK(HttpStatus::BadRequest.reasonPhrase()          == "Bad Request");
                CHECK(HttpStatus::NotFound.reasonPhrase()            == "Not Found");
                CHECK(HttpStatus::MethodNotAllowed.reasonPhrase()    == "Method Not Allowed");
                CHECK(HttpStatus::InternalServerError.reasonPhrase() == "Internal Server Error");
        }

        SUBCASE("reasonPhrase falls back for unknown codes") {
                HttpStatus weird{599};
                CHECK(weird.reasonPhrase() == "Status 599");
        }

        SUBCASE("class-of-status helpers") {
                CHECK(HttpStatus{100}.isInformational());
                CHECK(HttpStatus::Ok.isSuccess());
                CHECK(HttpStatus::Created.isSuccess());
                CHECK(HttpStatus::MovedPermanently.isRedirect());
                CHECK(HttpStatus::NotFound.isClientError());
                CHECK(HttpStatus::NotFound.isError());
                CHECK(HttpStatus::InternalServerError.isServerError());
                CHECK(HttpStatus::InternalServerError.isError());
                CHECK_FALSE(HttpStatus::Ok.isError());
        }
}
