/**
 * @file      httpserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpserver.h>
#include <promeki/httpfilehandler.h>
#include <promeki/tcpsocket.h>
#include <promeki/socketaddress.h>
#include <promeki/eventloop.h>
#include <promeki/thread.h>
#include <promeki/dir.h>
#include <promeki/file.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantspec.h>
#include <promeki/variantlookup.h>
#include <promeki/atomic.h>
#include <cstring>

using namespace promeki;

namespace {

        // Minimal blocking HTTP/1.0 client used by the tests.  We avoid
        // pulling in an HTTP client class (which the HttpClient task below
        // will implement) so these tests depend only on TcpSocket and the
        // server under test.  Non-chunked bodies only.
        struct HttpTestResponse {
                        int         status = 0;
                        HttpHeaders headers;
                        String      body;
        };

        static HttpTestResponse doRequest(uint16_t port, const String &method, const String &path,
                                          const String      &body = String(),
                                          const HttpHeaders &extraHeaders = HttpHeaders()) {
                TcpSocket sock;
                sock.open(IODevice::ReadWrite);
                Error err = sock.connectToHost(SocketAddress::localhost(port));
                REQUIRE(err.isOk());

                String req = method + " " + path + " HTTP/1.1\r\n";
                req += "Host: localhost\r\n";
                req += "Connection: close\r\n";
                if (!body.isEmpty()) {
                        req += String::sprintf("Content-Length: %zu\r\n", body.byteCount());
                }
                extraHeaders.forEach(
                        [&](const String &name, const String &value) { req += name + ": " + value + "\r\n"; });
                req += "\r\n";
                req += body;

                const int64_t n = sock.write(req.cstr(), req.byteCount());
                REQUIRE(n == static_cast<int64_t>(req.byteCount()));

                // Drain the response.
                String raw;
                char   buf[4096];
                for (;;) {
                        int64_t got = sock.read(buf, sizeof(buf));
                        if (got <= 0) break;
                        raw += String(buf, static_cast<size_t>(got));
                }
                sock.close();

                HttpTestResponse out;
                const size_t     sep = raw.find("\r\n\r\n");
                REQUIRE(sep != String::npos);
                const String head = raw.left(sep);
                out.body = raw.mid(sep + 4);

                // Status line: "HTTP/1.1 200 OK".
                const size_t eol = head.find("\r\n");
                const String statusLine = (eol == String::npos) ? head : head.left(eol);
                const size_t sp1 = statusLine.find(' ');
                const size_t sp2 = statusLine.find(' ', sp1 + 1);
                out.status = std::atoi(statusLine.mid(sp1 + 1, sp2 - sp1 - 1).cstr());

                // Headers.
                const String headerBlock = (eol == String::npos) ? String() : head.mid(eol + 2);
                StringList   lines = headerBlock.split("\r\n");
                for (size_t i = 0; i < lines.size(); ++i) {
                        const String &ln = lines[i];
                        if (ln.isEmpty()) continue;
                        const size_t colon = ln.find(':');
                        if (colon == String::npos) continue;
                        String name = ln.left(colon);
                        String value = ln.mid(colon + 1);
                        // Strip a single leading space.
                        if (!value.isEmpty() && value.cstr()[0] == ' ') value = value.mid(1);
                        out.headers.add(name, value);
                }
                return out;
        }

        // Spin up a server on an ephemeral port in a worker thread, wait
        // for it to start listening, and return the chosen port.
        struct ServerFixture {
                        Thread      thread;
                        HttpServer *server = nullptr;
                        uint16_t    port = 0;

                        explicit ServerFixture() {
                                thread.start();
                                Atomic<bool> ready(false);
                                thread.threadEventLoop()->postCallable([this, &ready]() {
                                        server = new HttpServer();
                                        ready.setValue(true);
                                });
                                for (int i = 0; i < 200 && !ready.value(); ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(ready.value());
                                REQUIRE(server != nullptr);
                        }

                        void configure(std::function<void(HttpServer &)> cfg) {
                                Atomic<bool> done(false);
                                thread.threadEventLoop()->postCallable([&]() {
                                        cfg(*server);
                                        done.setValue(true);
                                });
                                for (int i = 0; i < 500 && !done.value(); ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                                REQUIRE(done.value());
                        }

                        void listenOnAnyPort() {
                                Atomic<bool> done(false);
                                thread.threadEventLoop()->postCallable([&]() {
                                        Error err = server->listen(SocketAddress::localhost(0));
                                        REQUIRE(err.isOk());
                                        port = server->serverAddress().port();
                                        done.setValue(true);
                                });
                                for (int i = 0; i < 500 && !done.value(); ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                                REQUIRE(done.value());
                                REQUIRE(port != 0);
                        }

                        ~ServerFixture() {
                                thread.threadEventLoop()->postCallable([this]() {
                                        delete server;
                                        server = nullptr;
                                });
                                thread.quit();
                                thread.wait(2000);
                        }
        };

} // anonymous namespace

TEST_CASE("HttpServer - GET round trip") {
        ServerFixture f;
        f.configure([](HttpServer &s) {
                s.route("/hello", HttpMethod::Get,
                        [](const HttpRequest &, HttpResponse &res) { res.setText("world"); });
        });
        f.listenOnAnyPort();

        auto rsp = doRequest(f.port, "GET", "/hello");
        CHECK(rsp.status == 200);
        CHECK(rsp.body == "world");
}

TEST_CASE("HttpServer - 404 on unknown path") {
        ServerFixture f;
        f.listenOnAnyPort();
        auto rsp = doRequest(f.port, "GET", "/nope");
        CHECK(rsp.status == 404);
}

TEST_CASE("HttpServer - POST with JSON body") {
        ServerFixture f;
        f.configure([](HttpServer &s) {
                s.route("/echo", HttpMethod::Post, [](const HttpRequest &req, HttpResponse &res) {
                        Error      e;
                        JsonObject in = req.bodyAsJson(&e);
                        CHECK(e.isOk());
                        JsonObject out;
                        out.set("seen", in.getString("name"));
                        res.setJson(out);
                });
        });
        f.listenOnAnyPort();

        HttpHeaders extra;
        extra.set("Content-Type", "application/json");
        auto rsp = doRequest(f.port, "POST", "/echo", R"({"name":"jth"})", extra);
        CHECK(rsp.status == 200);
        CHECK(rsp.body.contains("jth"));
}

TEST_CASE("HttpServer - path parameter") {
        ServerFixture f;
        f.configure([](HttpServer &s) {
                s.route("/items/{id}", HttpMethod::Get,
                        [](const HttpRequest &req, HttpResponse &res) { res.setText("id=" + req.pathParam("id")); });
        });
        f.listenOnAnyPort();

        auto rsp = doRequest(f.port, "GET", "/items/42");
        CHECK(rsp.status == 200);
        CHECK(rsp.body == "id=42");
}

TEST_CASE("HttpServer - file serving via HttpFileHandler") {
        // Drop a scratch file under /mnt/data/tmp/promeki/ so the
        // handler has something real to serve.  (We must not use /tmp
        // per the repo's feedback memory.)
        const String scratchDir = "/mnt/data/tmp/promeki/httpserver-test";
        Dir::home(); // warm up static init
        std::filesystem::create_directories(scratchDir.cstr());
        const String path = scratchDir + "/hello.txt";
        File         f{path};
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        const char *payload = "Hello, libpromeki!";
        f.write(payload, std::strlen(payload));
        f.close();

        ServerFixture fix;
        fix.configure([&](HttpServer &s) {
                HttpHandler::Ptr handler = HttpHandler::Ptr::takeOwnership(new HttpFileHandler(scratchDir));
                s.route("/static/{path:*}", HttpMethod::Get, handler);
        });
        fix.listenOnAnyPort();

        auto rsp = doRequest(fix.port, "GET", "/static/hello.txt");
        CHECK(rsp.status == 200);
        CHECK(rsp.body == payload);
        CHECK(rsp.headers.contains("ETag"));
        CHECK(rsp.headers.contains("Last-Modified"));
}

// ----------------------------------------------------------------
// VariantDatabase reflection adapter
// ----------------------------------------------------------------

// A tiny database just for this test's scope.  Declaring the
// well-known IDs at namespace scope matches the convention used
// elsewhere — the specs register themselves at static-init.
using TestDB = VariantDatabase<"HttpServerTestDB">;

// Spec accepts both signed and unsigned 64-bit ints because
// JsonObject decodes JSON integer literals as TypeU64 when they
// are non-negative and TypeS64 when negative; specs that want to
// round-trip through JSON should accept the union.
static const TestDB::ID kDbWidth = TestDB::declareID("Width", VariantSpec()
                                                                      .setTypes({Variant::TypeS64, Variant::TypeU64})
                                                                      .setDefault(int64_t{1920})
                                                                      .setRange(int64_t{1}, int64_t{8192})
                                                                      .setDescription("Frame width in pixels"));

static const TestDB::ID kDbHeight = TestDB::declareID("Height", VariantSpec()
                                                                        .setTypes({Variant::TypeS64, Variant::TypeU64})
                                                                        .setDefault(int64_t{1080})
                                                                        .setDescription("Frame height in pixels"));

TEST_CASE("HttpServer - exposeDatabase") {
        TestDB db;
        db.set(kDbWidth, int64_t{1920});
        db.set(kDbHeight, int64_t{1080});

        ServerFixture fix;
        fix.configure([&](HttpServer &s) { s.exposeDatabase("/cfg", db); });
        fix.listenOnAnyPort();

        SUBCASE("GET full snapshot") {
                auto rsp = doRequest(fix.port, "GET", "/cfg");
                CHECK(rsp.status == 200);
                CHECK(rsp.body.contains("Width"));
                CHECK(rsp.body.contains("1920"));
        }

        SUBCASE("GET single key") {
                auto rsp = doRequest(fix.port, "GET", "/cfg/Width");
                CHECK(rsp.status == 200);
                CHECK(rsp.body.contains("Width"));
                CHECK(rsp.body.contains("1920"));
        }

        SUBCASE("GET unknown key -> 404") {
                auto rsp = doRequest(fix.port, "GET", "/cfg/DoesNotExist");
                CHECK(rsp.status == 404);
        }

        SUBCASE("GET schema") {
                auto rsp = doRequest(fix.port, "GET", "/cfg/_schema");
                CHECK(rsp.status == 200);
                CHECK(rsp.body.contains("Width"));
                CHECK(rsp.body.contains("description"));
        }

        SUBCASE("PUT updates value") {
                HttpHeaders extra;
                extra.set("Content-Type", "application/json");
                auto rsp = doRequest(fix.port, "PUT", "/cfg/Width", R"({"value":3840})", extra);
                CHECK(rsp.status == 200);
                // The JSON integer landed as TypeU64 (it's
                // non-negative); read back via uint64_t to avoid
                // a needless cross-type conversion.
                CHECK(db.getAs<uint64_t>(kDbWidth) == 3840);
        }

        SUBCASE("PUT out-of-range value -> 400 with validator detail") {
                HttpHeaders extra;
                extra.set("Content-Type", "application/json");
                // Width spec is 1..8192; 99999 violates the range and
                // must be rejected by the now-default Strict mode with
                // the validator's specific error description in the
                // body (rather than the old generic "Validation failed").
                auto rsp = doRequest(fix.port, "PUT", "/cfg/Width", R"({"value":99999})", extra);
                CHECK(rsp.status == 400);
                CHECK(rsp.body.contains("Width"));
                CHECK(rsp.body.contains("range"));
                // Original value untouched.
                CHECK(db.getAs<uint64_t>(kDbWidth) == 1920);
        }

        SUBCASE("DELETE clears entry") {
                auto rsp = doRequest(fix.port, "DELETE", "/cfg/Width");
                CHECK(rsp.status == 204);
                CHECK_FALSE(db.contains(kDbWidth));
        }
}
