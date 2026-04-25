/**
 * @file      httprouter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httprouter.h>

using namespace promeki;

namespace {

HttpRequest make(const HttpMethod &m, const String &path) {
        HttpRequest r;
        r.setMethod(m);
        Url u;
        u.setPath(path);
        r.setUrl(u);
        return r;
}

} // anonymous namespace

TEST_CASE("HttpRouter") {
        SUBCASE("default not-found handler") {
                HttpRouter r;
                HttpRequest req = make(HttpMethod::Get, "/missing");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(res.status() == HttpStatus::NotFound);
        }

        SUBCASE("exact match invokes the handler") {
                HttpRouter r;
                bool called = false;
                r.route("/ping", HttpMethod::Get,
                        [&](const HttpRequest &, HttpResponse &res) {
                                called = true;
                                res.setText("pong");
                        });
                HttpRequest req = make(HttpMethod::Get, "/ping");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(called);
                CHECK(res.status() == HttpStatus::Ok);
        }

        SUBCASE("method mismatch -> 405 with Allow") {
                HttpRouter r;
                r.route("/thing", HttpMethod::Get,
                        [](const auto &, auto &res) { res.setText("g"); });
                r.route("/thing", HttpMethod::Post,
                        [](const auto &, auto &res) { res.setText("p"); });

                HttpRequest req = make(HttpMethod::Delete, "/thing");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(res.status() == HttpStatus::MethodNotAllowed);
                const String allow = res.headers().value("Allow");
                CHECK(allow.contains("GET"));
                CHECK(allow.contains("POST"));
        }

        SUBCASE("single-segment path parameter") {
                HttpRouter r;
                String captured;
                r.route("/items/{id}", HttpMethod::Get,
                        [&](const HttpRequest &req, HttpResponse &res) {
                                captured = req.pathParam("id");
                                res.setText("ok");
                        });
                HttpRequest req = make(HttpMethod::Get, "/items/42");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(res.status() == HttpStatus::Ok);
                CHECK(captured == "42");
        }

        SUBCASE("greedy tail parameter") {
                HttpRouter r;
                String captured;
                r.route("/files/{path:*}", HttpMethod::Get,
                        [&](const HttpRequest &req, HttpResponse &res) {
                                captured = req.pathParam("path");
                                res.setText("ok");
                        });
                HttpRequest req = make(HttpMethod::Get, "/files/a/b/c.txt");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(res.status() == HttpStatus::Ok);
                CHECK(captured == "a/b/c.txt");
        }

        SUBCASE("literal match beats parameterized") {
                HttpRouter r;
                String picked;
                r.route("/api/{thing}", HttpMethod::Get,
                        [&](const auto &, auto &res) {
                                picked = "param";
                                res.setText("p");
                        });
                r.route("/api/status", HttpMethod::Get,
                        [&](const auto &, auto &res) {
                                picked = "literal";
                                res.setText("s");
                        });
                HttpRequest req = make(HttpMethod::Get, "/api/status");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(picked == "literal");
        }

        SUBCASE("middleware runs before handler in order") {
                HttpRouter r;
                String trace;
                r.use([&](const auto &, auto &, auto next) {
                        trace += "A";
                        next();
                        trace += "a";
                });
                r.use([&](const auto &, auto &, auto next) {
                        trace += "B";
                        next();
                        trace += "b";
                });
                r.route("/x", HttpMethod::Get,
                        [&](const auto &, auto &res) {
                                trace += "H";
                                res.setText("ok");
                        });

                HttpRequest req = make(HttpMethod::Get, "/x");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(trace == "ABHba");
        }

        SUBCASE("middleware can short-circuit") {
                HttpRouter r;
                bool handlerRan = false;
                r.use([](const auto &, auto &res, auto /*next*/) {
                        res.setStatus(HttpStatus::Unauthorized);
                        res.setText("nope");
                });
                r.route("/x", HttpMethod::Get,
                        [&](const auto &, auto &) { handlerRan = true; });

                HttpRequest req = make(HttpMethod::Get, "/x");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(res.status() == HttpStatus::Unauthorized);
                CHECK_FALSE(handlerRan);
        }

        SUBCASE("custom not-found handler is honored") {
                HttpRouter r;
                r.setNotFoundHandler([](const auto &req, auto &res) {
                        res.setStatus(HttpStatus::NotFound);
                        res.setText("gone: " + req.path());
                });
                HttpRequest req = make(HttpMethod::Get, "/foo");
                HttpResponse res;
                r.dispatch(req, res);
                CHECK(res.status() == HttpStatus::NotFound);
        }

        SUBCASE("any() matches every method") {
                HttpRouter r;
                int calls = 0;
                r.any("/all", [&](const auto &, auto &res) {
                        ++calls;
                        res.setText("hit");
                });
                for(const auto &m : { HttpMethod::Get, HttpMethod::Post,
                                      HttpMethod::Put, HttpMethod::Delete }) {
                        HttpRequest req = make(m, "/all");
                        HttpResponse res;
                        r.dispatch(req, res);
                        CHECK(res.status() == HttpStatus::Ok);
                }
                CHECK(calls == 4);
        }
}
