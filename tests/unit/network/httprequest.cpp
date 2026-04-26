/**
 * @file      httprequest.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httprequest.h>

using namespace promeki;

TEST_CASE("HttpRequest") {
        SUBCASE("default state") {
                HttpRequest r;
                CHECK(r.method() == HttpMethod::Get);
                CHECK(r.headers().isEmpty());
                CHECK(r.body().size() == 0);
                CHECK(r.httpVersion() == "HTTP/1.1");
                CHECK(r.pathParams().isEmpty());
        }

        SUBCASE("setBody from String stores the bytes") {
                HttpRequest r;
                r.setBody(String("hello"));
                CHECK(r.body().isValid());
                CHECK(r.body().size() == 5);
                CHECK(r.bodyAsString() == "hello");
        }

        SUBCASE("setBody from JSON sets Content-Type") {
                JsonObject obj;
                obj.set("a", 1);
                obj.set("b", "two");

                HttpRequest r;
                r.setBody(obj);

                CHECK(r.header("Content-Type") == "application/json");
                Error      err;
                JsonObject parsed = r.bodyAsJson(&err);
                CHECK(err.isOk());
                CHECK(parsed.getInt("a") == 1);
                CHECK(parsed.getString("b") == "two");
        }

        SUBCASE("path / queryValue forward to the URL") {
                HttpRequest r;
                Url         u = Url::fromString("http://example/foo/bar?id=42&q=cat").first();
                r.setUrl(u);
                CHECK(r.path() == "/foo/bar");
                CHECK(r.queryValue("id") == "42");
                CHECK(r.queryValue("missing", "fallback") == "fallback");
        }

        SUBCASE("pathParam returns the stored value") {
                HttpRequest             r;
                HashMap<String, String> p;
                p.insert("id", "99");
                r.setPathParams(p);
                CHECK(r.pathParam("id") == "99");
                CHECK(r.pathParam("missing", "default") == "default");
        }

        SUBCASE("equality reflects every relevant field") {
                HttpRequest a, b;
                a.setMethod(HttpMethod::Post);
                a.setUrl(Url::fromString("http://h/x").first());
                a.headers().set("Content-Type", "text/plain");
                a.setBody(String("hi"));

                b = a;
                CHECK(a == b);

                b.setBody(String("ho"));
                CHECK(a != b);
        }
}
