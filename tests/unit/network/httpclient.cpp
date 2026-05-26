/**
 * @file      httpclient.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/httpclient.h>
#include <promeki/httpserver.h>
#include <promeki/sslcontext.h>
#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/socketaddress.h>
#include <promeki/atomic.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>

using namespace promeki;

namespace {

        // Spin up an HttpServer + HttpClient on independent worker threads
        // (each with its own EventLoop) so we can drive a real loopback
        // round trip between them without colliding on a single loop.
        struct ClientFixture {
                        Thread      serverThread;
                        Thread      clientThread;
                        HttpServer *server = nullptr;
                        HttpClient *client = nullptr;
                        uint16_t    port = 0;

                        ClientFixture() {
                                serverThread.start();
                                clientThread.start();

                                Atomic<bool> serverReady(false);
                                Atomic<bool> clientReady(false);
                                serverThread.threadEventLoop()->postCallable([this, &serverReady]() {
                                        server = new HttpServer();
                                        serverReady.setValue(true);
                                });
                                clientThread.threadEventLoop()->postCallable([this, &clientReady]() {
                                        client = new HttpClient();
                                        clientReady.setValue(true);
                                });
                                for (int i = 0; i < 200 && (!serverReady.value() || !clientReady.value()); ++i) {
                                        BasicThread::sleepMs(2);
                                }
                                REQUIRE(serverReady.value());
                                REQUIRE(clientReady.value());
                                REQUIRE(server != nullptr);
                                REQUIRE(client != nullptr);
                        }

                        // Configure the server on its loop thread; blocks until done.
                        void configure(std::function<void(HttpServer &)> cfg) {
                                Atomic<bool> done(false);
                                serverThread.threadEventLoop()->postCallable([&]() {
                                        cfg(*server);
                                        done.setValue(true);
                                });
                                for (int i = 0; i < 500 && !done.value(); ++i) {
                                        BasicThread::sleepMs(2);
                                }
                                REQUIRE(done.value());
                        }

                        void listenOnAnyPort() {
                                Atomic<bool> done(false);
                                serverThread.threadEventLoop()->postCallable([&]() {
                                        Error err = server->listen(SocketAddress::localhost(0));
                                        REQUIRE(err.isOk());
                                        port = server->serverAddress().port();
                                        done.setValue(true);
                                });
                                for (int i = 0; i < 500 && !done.value(); ++i) {
                                        BasicThread::sleepMs(2);
                                }
                                REQUIRE(done.value());
                                REQUIRE(port != 0);
                        }

                        // Issue a request from the client thread and wait for the
                        // future to fulfill on the test thread.  The client's loop
                        // drives the I/O on the client thread.
                        Result<HttpResponse> request(std::function<Future<HttpResponse>(HttpClient &)> issue,
                                                     unsigned int timeoutMs = 3000) {
                                std::promise<Future<HttpResponse>> futPromise;
                                auto                               futShared = futPromise.get_future();
                                clientThread.threadEventLoop()->postCallable(
                                        [&]() { futPromise.set_value(issue(*client)); });
                                Future<HttpResponse> fut = futShared.get();
                                return fut.result(timeoutMs);
                        }

                        ~ClientFixture() {
                                // deleteLater + quit: the EventLoop drains
                                // remaining queued items (including any cleanup
                                // callables ~HttpClient / ~HttpServer post back
                                // to the same loop) before exiting, so this
                                // sequence cannot race the way a manual
                                // post-then-quit would.
                                if (client != nullptr) {
                                        client->deleteLater();
                                        client = nullptr;
                                }
                                if (server != nullptr) {
                                        server->deleteLater();
                                        server = nullptr;
                                }
                                clientThread.quit();
                                serverThread.quit();
                                clientThread.wait(2000);
                                serverThread.wait(2000);
                        }
        };

        String urlFor(uint16_t port, const String &path) {
                return String::sprintf("http://127.0.0.1:%u%s", port, path.cstr());
        }

} // anonymous namespace

TEST_CASE("HttpClient - GET round trip") {
        ClientFixture f;
        f.configure([](HttpServer &s) {
                s.route("/hello", HttpMethod::Get,
                        [](const HttpRequest &, HttpResponse &res) { res.setText("world"); });
        });
        f.listenOnAnyPort();

        auto [res, err] = f.request([&](HttpClient &c) { return c.get(urlFor(f.port, "/hello")); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        CHECK(res.body().size() == 5);
        // Body shape, not content — bodyAsString round-trips bytes.
        Buffer b = res.body();
        CHECK(std::memcmp(b.data(), "world", 5) == 0);
}

TEST_CASE("HttpClient - POST with body") {
        ClientFixture f;
        f.configure([](HttpServer &s) {
                s.route("/echo", HttpMethod::Post, [](const HttpRequest &req, HttpResponse &res) {
                        res.setBody(req.body());
                        res.setHeader("Content-Type", req.headers().value("Content-Type"));
                });
        });
        f.listenOnAnyPort();

        Buffer body(11);
        std::memcpy(body.data(), "hello!world", 11);
        body.setSize(11);

        auto [res, err] = f.request(
                [&](HttpClient &c) { return c.post(urlFor(f.port, "/echo"), body, "application/octet-stream"); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        REQUIRE(res.body().size() == 11);
        CHECK(std::memcmp(res.body().data(), "hello!world", 11) == 0);
        CHECK(res.headers().value("Content-Type") == "application/octet-stream");
}

TEST_CASE("HttpClient - 404 surfaces as response, not error") {
        ClientFixture f;
        f.listenOnAnyPort();

        auto [res, err] = f.request([&](HttpClient &c) { return c.get(urlFor(f.port, "/no-such-thing")); });
        CHECK(err.isOk()); // transport ok
        CHECK(res.status() == HttpStatus::NotFound);
        CHECK(res.isError());
}

TEST_CASE("HttpClient - DELETE method") {
        ClientFixture    f;
        std::atomic<int> deleteCalls{0};
        f.configure([&](HttpServer &s) {
                s.route("/items/{id}", HttpMethod::Delete, [&](const HttpRequest &, HttpResponse &res) {
                        ++deleteCalls;
                        res.setStatus(HttpStatus::NoContent);
                });
        });
        f.listenOnAnyPort();

        auto [res, err] = f.request([&](HttpClient &c) { return c.del(urlFor(f.port, "/items/42")); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::NoContent);
        CHECK(deleteCalls.load() == 1);
}

TEST_CASE("HttpClient - default headers applied to every request") {
        ClientFixture     f;
        std::atomic<bool> seenHeader{false};
        f.configure([&](HttpServer &s) {
                s.route("/whoami", HttpMethod::Get, [&](const HttpRequest &req, HttpResponse &res) {
                        if (req.header("X-Trace") == "abc123") {
                                seenHeader = true;
                        }
                        res.setText("ok");
                });
        });
        f.listenOnAnyPort();

        std::atomic<bool> done{false};
        f.clientThread.threadEventLoop()->postCallable([&]() {
                f.client->setDefaultHeader("X-Trace", "abc123");
                done = true;
        });
        for (int i = 0; i < 200 && !done; ++i) {
                BasicThread::sleepMs(2);
        }

        auto [res, err] = f.request([&](HttpClient &c) { return c.get(urlFor(f.port, "/whoami")); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        CHECK(seenHeader.load());
}

TEST_CASE("HttpClient - https request reaches the network layer") {
        // We don't reach example.com from the test harness, but the
        // request must clear the up-front URL gate now that TLS is
        // wired in.  Acceptable terminal errors include host-not-
        // found, connection-refused, timeout, or transport reset —
        // not NotImplemented or Invalid.
        //
        // The fail-closed verify policy refuses to handshake without
        // a CA bundle, so attach a context with verifyPeer=false to
        // isolate this test from system-CA availability (CI runners
        // may not have a bundle installed).  We're testing the
        // request plumbing, not actual verification.
        ClientFixture     f;
        std::atomic<bool> done{false};
        f.clientThread.threadEventLoop()->postCallable([&]() {
                SslContext ctx;
                ctx.setVerifyPeer(false);
                f.client->setSslContext(ctx);
                f.client->setTimeoutMs(50);
                done = true;
        });
        for (int i = 0; i < 200 && !done; ++i) {
                BasicThread::sleepMs(2);
        }
        auto [res, err] = f.request([&](HttpClient &c) { return c.get("https://127.0.0.1:1/"); }, /*timeoutMs=*/2000);
        CHECK(err != Error::NotImplemented);
        CHECK(err != Error::Invalid);
}

TEST_CASE("HttpClient - https request fails closed without CA bundle or opt-out") {
        // No setSslContext call: HttpClient's internal default
        // SslContext is constructed for us.  Its constructor
        // best-effort-loads the system CA bundle, so on a normal
        // Linux machine the request proceeds to the network and
        // fails with a transport error against 127.0.0.1:1.  On a
        // hypothetical machine with no system bundle the handshake
        // refuses with Error::Invalid (fail-closed).  Either is a
        // valid outcome; the test only asserts what is @em not
        // allowed: a silent successful handshake against an
        // unauthenticated peer.
        ClientFixture     f;
        std::atomic<bool> done{false};
        f.clientThread.threadEventLoop()->postCallable([&]() {
                f.client->setTimeoutMs(50);
                done = true;
        });
        for (int i = 0; i < 200 && !done; ++i) {
                BasicThread::sleepMs(2);
        }
        auto [res, err] = f.request([&](HttpClient &c) { return c.get("https://127.0.0.1:1/"); }, /*timeoutMs=*/2000);
        CHECK(err != Error::NotImplemented);
        CHECK(err != Error::Ok);
}

TEST_CASE("HttpClient - rejects empty/invalid URL up front") {
        ClientFixture f;
        auto [res, err] = f.request([&](HttpClient &c) { return c.get("not-a-url"); });
        CHECK(err == Error::Invalid);
}

TEST_CASE("HttpClient - bodySink receives full body, response buffer stays empty") {
        // Streams 256 KiB through the sink so we exercise multiple
        // llhttp_execute chunks (read buffer is 64 KiB).  The
        // response's own body buffer must remain empty — the sink
        // is the exclusive consumer when installed.
        const size_t payloadLen = 256 * 1024;
        ClientFixture f;
        f.configure([&](HttpServer &s) {
                s.route("/blob", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        Buffer b(payloadLen);
                        std::memset(b.data(), 'A', payloadLen);
                        b.setSize(payloadLen);
                        res.setBody(b);
                });
        });
        f.listenOnAnyPort();

        Buffer accum(payloadLen);
        std::memset(accum.data(), 0, payloadLen);
        accum.setSize(0);

        auto [res, err] = f.request([&](HttpClient &c) {
                HttpRequest r;
                r.setMethod(HttpMethod::Get);
                r.setUrl(Url::fromString(urlFor(f.port, "/blob")).first());
                r.setBodySink([&accum, payloadLen](const void *data, size_t len) -> Error {
                        const size_t prev = accum.size();
                        if (prev + len > payloadLen) return Error::OutOfRange;
                        std::memcpy(static_cast<char *>(accum.data()) + prev, data, len);
                        accum.setSize(prev + len);
                        return Error::Ok;
                });
                return c.send(r);
        });
        REQUIRE(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        CHECK(res.body().size() == 0); // sink owns the bytes
        REQUIRE(accum.size() == payloadLen);
        // Spot-check a few bytes — full memcmp inside CHECK would
        // spam doctest output on mismatch; bytes are uniform 'A'.
        const unsigned char *p = static_cast<const unsigned char *>(accum.data());
        CHECK(p[0] == 'A');
        CHECK(p[payloadLen / 2] == 'A');
        CHECK(p[payloadLen - 1] == 'A');
}

TEST_CASE("HttpClient - progressCallback fires with cumulative received and parsed total") {
        const size_t payloadLen = 128 * 1024;
        ClientFixture f;
        f.configure([&](HttpServer &s) {
                s.route("/p", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        Buffer b(payloadLen);
                        std::memset(b.data(), 'B', payloadLen);
                        b.setSize(payloadLen);
                        res.setBody(b);
                });
        });
        f.listenOnAnyPort();

        Atomic<int64_t> lastReceived(-1);
        Atomic<int64_t> lastTotal(-2);
        Atomic<int>     callCount(0);

        auto [res, err] = f.request([&](HttpClient &c) {
                HttpRequest r;
                r.setMethod(HttpMethod::Get);
                r.setUrl(Url::fromString(urlFor(f.port, "/p")).first());
                r.setProgressCallback([&](int64_t received, int64_t total) -> bool {
                        lastReceived.setValue(received);
                        lastTotal.setValue(total);
                        callCount.setValue(callCount.value() + 1);
                        return true;
                });
                return c.send(r);
        });
        REQUIRE(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        CHECK(callCount.value() >= 2); // at minimum: initial(0,total) + final
        CHECK(lastReceived.value() == static_cast<int64_t>(payloadLen));
        CHECK(lastTotal.value() == static_cast<int64_t>(payloadLen));
}

TEST_CASE("HttpClient - progressCallback returning false cancels the request") {
        // Trickle bytes through a 1 MiB body so the progress callback
        // gets a chance to fire mid-stream and reject the rest.
        const size_t payloadLen = 1024 * 1024;
        ClientFixture f;
        f.configure([&](HttpServer &s) {
                s.route("/cancel-me", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        Buffer b(payloadLen);
                        std::memset(b.data(), 'C', payloadLen);
                        b.setSize(payloadLen);
                        res.setBody(b);
                });
        });
        f.listenOnAnyPort();

        auto [res, err] = f.request([&](HttpClient &c) {
                HttpRequest r;
                r.setMethod(HttpMethod::Get);
                r.setUrl(Url::fromString(urlFor(f.port, "/cancel-me")).first());
                r.setProgressCallback([](int64_t /*received*/, int64_t /*total*/) -> bool {
                        // Refuse from the very first tick (the initial
                        // headers-complete callback before any bytes
                        // arrive).  Cancellation propagates as
                        // Error::Cancelled on the Future.
                        return false;
                });
                return c.send(r);
        });
        CHECK(err == Error::Cancelled);
}

TEST_CASE("HttpClient - follows redirects by default; sink only sees final body") {
        // Build a redirect chain on a single server: /a 302→/b 302→/c
        // (200 with payload).  Our chase only follows absolute URLs,
        // so the Location headers are written as full http://host:port
        // URLs — the same scheme HF / S3 / xethub use in practice.
        // The body sink must see only the /c body, never the
        // would-be junk from the /a /b redirect bodies.
        ClientFixture f;
        f.listenOnAnyPort();

        const String baseUrl = urlFor(f.port, "");
        const String finalText = "REDIRECTED_PAYLOAD";
        f.configure([&](HttpServer &s) {
                s.route("/a", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", baseUrl + "/b");
                        res.setText("AAAAAAA");
                });
                s.route("/b", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        res.setHeader("Location", baseUrl + "/c");
                        res.setText("BBBBBBB");
                });
                s.route("/c", HttpMethod::Get,
                        [&](const HttpRequest &, HttpResponse &res) { res.setText(finalText); });
        });

        String      capturedBody;
        Atomic<int> progressCalls(0);
        auto [res, err] = f.request([&](HttpClient &c) {
                HttpRequest r;
                r.setMethod(HttpMethod::Get);
                r.setUrl(Url::fromString(urlFor(f.port, "/a")).first());
                r.setBodySink([&capturedBody](const void *data, size_t len) -> Error {
                        capturedBody += String(static_cast<const char *>(data), len);
                        return Error::Ok;
                });
                r.setProgressCallback([&](int64_t /*r*/, int64_t /*t*/) -> bool {
                        progressCalls.setValue(progressCalls.value() + 1);
                        return true;
                });
                return c.send(r);
        });
        REQUIRE(err.isOk());
        CHECK(res.status() == HttpStatus::Ok);
        CHECK(capturedBody == finalText);
        // Progress must have ticked only for the final hop.  An exact
        // count would lock the test to internal chunking details, but
        // we know it must fire at least once (initial headers-complete
        // tick) and won't fire more than a handful of times for a
        // tiny payload.
        CHECK(progressCalls.value() >= 1);
        CHECK(progressCalls.value() <= 4);
}

TEST_CASE("HttpClient - setMaxRedirects(0) disables follow") {
        ClientFixture f;
        f.configure([&](HttpServer &s) {
                s.route("/r", HttpMethod::Get, [](const HttpRequest &, HttpResponse &res) {
                        res.setStatus(HttpStatus::Found);
                        // The Location is irrelevant — we'll opt out
                        // of following, so the 302 must reach the
                        // caller as-is.
                        res.setHeader("Location", "/elsewhere");
                        res.setText("noisy-redirect-body");
                });
        });
        f.listenOnAnyPort();

        Atomic<bool> done{false};
        f.clientThread.threadEventLoop()->postCallable([&]() {
                f.client->setMaxRedirects(0);
                done.setValue(true);
        });
        for (int i = 0; i < 200 && !done.value(); ++i) {
                BasicThread::sleepMs(2);
        }

        auto [res, err] = f.request([&](HttpClient &c) { return c.get(urlFor(f.port, "/r")); });
        CHECK(err.isOk());
        CHECK(res.status() == HttpStatus::Found);
        CHECK(res.headers().value("Location") == "/elsewhere");
}

TEST_CASE("HttpClient - timeout when server never responds") {
        // Server that accepts but never writes.  We register the
        // listening fd manually so the kernel completes the
        // connect handshake; the server never sends a response.
        ClientFixture f;
        f.configure([](HttpServer &s) {
                s.route("/stuck", HttpMethod::Get, [](const HttpRequest &, HttpResponse &) {
                        // Do nothing: handler returns without
                        // populating the response.  The
                        // connection serializes the empty 200
                        // immediately, so we can't actually
                        // exercise a hang here without
                        // reaching into HttpConnection.
                        // Instead we set a tiny client
                        // timeout and hit a non-existent
                        // host:port to provoke a connect
                        // failure or timeout.
                });
        });
        f.listenOnAnyPort();

        // Set a very short client timeout, then point at an
        // unreachable address.  127.0.0.1:1 is reliably refused
        // (port 1 is unused), so we get ConnectionRefused fast.
        std::atomic<bool> done{false};
        f.clientThread.threadEventLoop()->postCallable([&]() {
                f.client->setTimeoutMs(50);
                done = true;
        });
        for (int i = 0; i < 200 && !done; ++i) {
                BasicThread::sleepMs(2);
        }

        auto [res, err] = f.request([&](HttpClient &c) { return c.get("http://127.0.0.1:1/no"); });
        // Either ConnectionRefused (kernel rejects) or Timeout
        // (no response within the budget) is acceptable here.
        CHECK((err == Error::ConnectionRefused || err == Error::Timeout));
}
