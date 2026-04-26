/**
 * @file      httpheaders.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpheaders.h>
#include <promeki/stringlist.h>

using namespace promeki;

TEST_CASE("HttpHeaders") {
        SUBCASE("empty on construction") {
                HttpHeaders h;
                CHECK(h.isEmpty());
                CHECK(h.count() == 0);
                CHECK_FALSE(h.contains("Content-Type"));
        }

        SUBCASE("set and retrieve is case-insensitive") {
                HttpHeaders h;
                h.set("Content-Type", "application/json");
                CHECK(h.value("content-type") == "application/json");
                CHECK(h.value("CONTENT-TYPE") == "application/json");
                CHECK(h.contains("content-type"));
                CHECK(h.count() == 1);
        }

        SUBCASE("set overwrites prior values regardless of case") {
                HttpHeaders h;
                h.add("X-Custom", "one");
                h.add("x-custom", "two");
                h.set("X-CUSTOM", "three");
                CHECK(h.count() == 1);
                CHECK(h.value("X-Custom") == "three");
        }

        SUBCASE("add preserves multi-value ordering") {
                HttpHeaders h;
                h.add("Set-Cookie", "a=1");
                h.add("Set-Cookie", "b=2");
                StringList all = h.values("Set-Cookie");
                REQUIRE(all.size() == 2);
                CHECK(all[0] == "a=1");
                CHECK(all[1] == "b=2");
                CHECK(h.count() == 2);
                CHECK(h.value("set-cookie") == "a=1");
        }

        SUBCASE("remove drops every value for the name") {
                HttpHeaders h;
                h.add("X-Trace", "one");
                h.add("X-Trace", "two");
                h.remove("x-trace");
                CHECK(h.count() == 0);
                CHECK_FALSE(h.contains("X-Trace"));
        }

        SUBCASE("forEach yields canonical casing in arrival order") {
                HttpHeaders h;
                h.add("Content-Type", "text/plain");
                h.add("X-Multi", "first");
                h.add("X-Multi", "second");
                h.add("Content-Length", "42");

                StringList order;
                h.forEach([&](const String &k, const String &v) { order.pushToBack(k + "=" + v); });
                REQUIRE(order.size() == 4);
                CHECK(order[0] == "Content-Type=text/plain");
                CHECK(order[1] == "X-Multi=first");
                CHECK(order[2] == "X-Multi=second");
                CHECK(order[3] == "Content-Length=42");
        }

        SUBCASE("foldName lowercases ASCII") {
                CHECK(HttpHeaders::foldName("Content-Type") == "content-type");
                CHECK(HttpHeaders::foldName("X-Foo-Bar") == "x-foo-bar");
                CHECK(HttpHeaders::foldName("already-lc") == "already-lc");
        }

        SUBCASE("equality depends on registered order and values") {
                HttpHeaders a, b;
                a.set("A", "1");
                a.set("B", "2");
                b.set("A", "1");
                b.set("B", "2");
                CHECK(a == b);

                b.set("B", "different");
                CHECK_FALSE(a == b);
        }
}
