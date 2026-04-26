/**
 * @file      httpmethod.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpmethod.h>

using namespace promeki;

TEST_CASE("HttpMethod") {
        SUBCASE("default constructs to Get") {
                HttpMethod m;
                CHECK(m == HttpMethod::Get);
                CHECK(m.value() == 0);
        }

        SUBCASE("named constants") {
                CHECK(HttpMethod::Get.wireName() == "GET");
                CHECK(HttpMethod::Head.wireName() == "HEAD");
                CHECK(HttpMethod::Post.wireName() == "POST");
                CHECK(HttpMethod::Put.wireName() == "PUT");
                CHECK(HttpMethod::Delete.wireName() == "DELETE");
                CHECK(HttpMethod::Patch.wireName() == "PATCH");
                CHECK(HttpMethod::Options.wireName() == "OPTIONS");
                CHECK(HttpMethod::Connect.wireName() == "CONNECT");
                CHECK(HttpMethod::Trace.wireName() == "TRACE");
        }

        SUBCASE("string round-trip") {
                HttpMethod m{String("POST")};
                CHECK(m == HttpMethod::Post);
        }

        SUBCASE("allowsBody returns true only for body-bearing methods") {
                CHECK_FALSE(HttpMethod::Get.allowsBody());
                CHECK_FALSE(HttpMethod::Head.allowsBody());
                CHECK_FALSE(HttpMethod::Delete.allowsBody());
                CHECK_FALSE(HttpMethod::Options.allowsBody());
                CHECK(HttpMethod::Post.allowsBody());
                CHECK(HttpMethod::Put.allowsBody());
                CHECK(HttpMethod::Patch.allowsBody());
        }
}
