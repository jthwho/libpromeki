/**
 * @file      debugserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <thread>
#include <chrono>
#include <atomic>
#include <doctest/doctest.h>
#include <promeki/application.h>
#include <promeki/debugserver.h>
#include <promeki/debugmodules.h>
#include <promeki/httpserver.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpmethod.h>
#include <promeki/httpstatus.h>
#include <promeki/logger.h>
#include <promeki/mutex.h>
#include <promeki/socketaddress.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/env.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/websocket.h>
#include <promeki/json.h>

using namespace promeki;

namespace {

// Reusing the same minimal HTTP/1.1 client style as
// tests/unit/network/httpserver.cpp — no llhttp, no chunking, just
// enough to drive the routes the debug server mounts.
struct DbgResponse {
        int     status = 0;
        String  location;
        String  contentType;
        String  body;
};

static DbgResponse hitWithBody(uint16_t port, const String &method,
                               const String &path, const String &body) {
        TcpSocket sock;
        sock.open(IODevice::ReadWrite);
        REQUIRE(sock.connectToHost(SocketAddress::localhost(port)).isOk());

        String req = method + " " + path + " HTTP/1.1\r\n";
        req += "Host: localhost\r\nConnection: close\r\n";
        if(!body.isEmpty()) {
                req += "Content-Type: application/json\r\n";
                req += String::sprintf("Content-Length: %zu\r\n", body.byteCount());
        }
        req += "\r\n";
        req += body;
        const int64_t n = sock.write(req.cstr(), req.byteCount());
        REQUIRE(n == static_cast<int64_t>(req.byteCount()));

        String raw;
        char buf[4096];
        for(;;) {
                int64_t got = sock.read(buf, sizeof(buf));
                if(got <= 0) break;
                raw += String(buf, static_cast<size_t>(got));
        }
        sock.close();

        DbgResponse out;
        const size_t sep = raw.find("\r\n\r\n");
        REQUIRE(sep != String::npos);
        const String head = raw.left(sep);
        out.body = raw.mid(sep + 4);

        // Status line.
        const size_t eol = head.find("\r\n");
        const String statusLine = (eol == String::npos) ? head : head.left(eol);
        const size_t sp1 = statusLine.find(' ');
        REQUIRE(sp1 != String::npos);
        const size_t sp2 = statusLine.find(' ', sp1 + 1);
        const String code = (sp2 == String::npos)
                ? statusLine.mid(sp1 + 1)
                : statusLine.mid(sp1 + 1, sp2 - sp1 - 1);
        Error e;
        out.status = code.toInt(&e);

        // Headers — only Location and Content-Type are interesting here.
        const String headers = (eol == String::npos) ? String() : head.mid(eol + 2);
        StringList lines = headers.split("\r\n");
        for(const auto &line : lines) {
                size_t colon = line.find(':');
                if(colon == String::npos) continue;
                String name = line.left(colon).trim();
                String val  = line.mid(colon + 1).trim();
                if(name.compareIgnoreCase("Location") == 0)     out.location = val;
                if(name.compareIgnoreCase("Content-Type") == 0) out.contentType = val;
        }
        return out;
}

static DbgResponse hit(uint16_t port, const String &method, const String &path) {
        return hitWithBody(port, method, path, String());
}

// Posts @p fn onto the worker @p thread's EventLoop and busy-waits up
// to a half-second for it to complete.  Mirrors the polling approach
// used in tests/unit/network/httpserver.cpp.
template <typename Fn>
static void runOnThread(Thread &thread, Fn fn) {
        std::atomic<bool> done{false};
        thread.threadEventLoop()->postCallable([&]() {
                fn();
                done = true;
        });
        for(int i = 0; i < 500 && !done.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(done.load());
}

// Worker-thread fixture for the pure DebugServer tests.  The
// HttpServer that DebugServer wraps captures EventLoop::current() at
// construction time, so the construction must happen on the worker
// thread that will subsequently drive its events.
struct DbgFixture {
        Thread          thread;
        DebugServer     *server = nullptr;
        uint16_t        port    = 0;

        DbgFixture() {
                thread.start();
                runOnThread(thread, [this]() {
                        server = new DebugServer();
                });
                REQUIRE(server != nullptr);
        }

        ~DbgFixture() {
                runOnThread(thread, [this]() {
                        delete server;
                        server = nullptr;
                });
                thread.quit();
                thread.wait(2000);
        }

        void installDefaults() {
                runOnThread(thread, [this]() {
                        server->installDefaultModules();
                });
        }

        template <typename Fn>
        void configure(Fn fn) {
                runOnThread(thread, [&]() { fn(*server); });
        }

        void listenOnAnyPort() {
                runOnThread(thread, [this]() {
                        REQUIRE(server->listen(0).isOk());
                        port = server->serverAddress().port();
                });
                REQUIRE(port != 0);
        }
};

}  // namespace

// ============================================================================
// parseSpec
// ============================================================================

TEST_CASE("DebugServer_parseSpec_BareColonPortFoldsToLoopback") {
        auto [addr, err] = DebugServer::parseSpec(":8085");
        REQUIRE(err.isOk());
        CHECK(addr.address().toString() == DebugServer::DefaultBindHost);
        CHECK(addr.port() == 8085);
}

TEST_CASE("DebugServer_parseSpec_ExplicitHostPort") {
        auto [addr, err] = DebugServer::parseSpec("0.0.0.0:8086");
        REQUIRE(err.isOk());
        CHECK(addr.address().toString() == "0.0.0.0");
        CHECK(addr.port() == 8086);
}

TEST_CASE("DebugServer_parseSpec_LoopbackHostPort") {
        auto [addr, err] = DebugServer::parseSpec("127.0.0.1:8087");
        REQUIRE(err.isOk());
        CHECK(addr.address().toString() == "127.0.0.1");
        CHECK(addr.port() == 8087);
}

TEST_CASE("DebugServer_parseSpec_RejectsEmpty") {
        auto [addr, err] = DebugServer::parseSpec("");
        CHECK(err.isError());
}

TEST_CASE("DebugServer_parseSpec_RejectsGarbage") {
        auto [addr, err] = DebugServer::parseSpec("not-a-spec");
        CHECK(err.isError());
}

// ============================================================================
// Lifecycle
// ============================================================================

TEST_CASE("DebugServer_ListenAndClose") {
        DbgFixture f;
        f.listenOnAnyPort();

        bool listening = false;
        runOnThread(f.thread, [&]() { listening = f.server->isListening(); });
        CHECK(listening);

        runOnThread(f.thread, [&]() { f.server->close(); });

        bool stillListening = true;
        runOnThread(f.thread, [&]() { stillListening = f.server->isListening(); });
        CHECK_FALSE(stillListening);
}

// ============================================================================
// Default modules
// ============================================================================

TEST_CASE("DebugServer_DefaultModulesMountUnderApiPrefix") {
        DbgFixture f;
        f.installDefaults();
        f.listenOnAnyPort();

        SUBCASE("build endpoint returns real build info") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/api/build");
                CHECK(r.status == 200);
                CHECK(r.body.contains("\"name\""));
                CHECK(r.body.contains("\"version\""));
                CHECK(r.body.contains("\"platform\""));
                CHECK(r.body.contains("\"features\""));
                CHECK(r.body.contains("\"lines\""));
        }

        SUBCASE("env endpoint returns environment variables") {
                // Set a marker var so we have something deterministic
                // to scan for.
                Env::set("PROMEKI_DEBUG_TEST_MARKER", "marker-value");
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/api/env");
                CHECK(r.status == 200);
                CHECK(r.body.contains("PROMEKI_DEBUG_TEST_MARKER"));
                CHECK(r.body.contains("marker-value"));
                Env::unset("PROMEKI_DEBUG_TEST_MARKER");
        }

        SUBCASE("options endpoint exposes LibraryOptions schema") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/api/options/_schema");
                CHECK(r.status == 200);
                // CrashHandler is a well-known LibraryOptions key.
                CHECK(r.body.contains("CrashHandler"));
        }

        SUBCASE("options endpoint is read-only") {
                // A PUT against the read-only mount should yield 405.
                DbgResponse r = hit(f.port, "PUT", "/promeki/debug/api/options/CrashHandler");
                CHECK(r.status == 405);
        }

        SUBCASE("memory endpoint reports registered MemSpaces") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/api/memory");
                CHECK(r.status == 200);
                CHECK(r.body.contains("\"spaces\""));
                // The System space is always registered.
                CHECK(r.body.contains("\"name\""));
                CHECK(r.body.contains("\"liveBytes\""));
        }

        SUBCASE("logger endpoint reports current state") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/api/logger");
                CHECK(r.status == 200);
                CHECK(r.body.contains("\"level\""));
                CHECK(r.body.contains("\"levelName\""));
                CHECK(r.body.contains("\"debugChannels\""));
        }

        SUBCASE("logger level can be changed via PUT") {
                Logger &logger = Logger::defaultLogger();
                int saved = logger.level();
                DbgResponse r = hitWithBody(f.port, "PUT",
                        "/promeki/debug/api/logger/level", "{\"level\":3}");
                CHECK(r.status == 200);
                logger.sync();
                CHECK(logger.level() == Logger::Warn);
                logger.setLogLevel(static_cast<Logger::LogLevel>(saved));
                logger.sync();
        }

        SUBCASE("logger level rejects out-of-range values") {
                DbgResponse r = hitWithBody(f.port, "PUT",
                        "/promeki/debug/api/logger/level", "{\"level\":99}");
                CHECK(r.status == 400);
        }

        SUBCASE("unknown debug channel returns 404") {
                DbgResponse r = hitWithBody(f.port, "PUT",
                        "/promeki/debug/api/logger/debug/no-such-channel-xyz",
                        "{\"enabled\":true}");
                CHECK(r.status == 404);
        }

        SUBCASE("frontend root (trailing slash) serves baked index.html") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/");
                CHECK(r.status == 200);
                CHECK(r.body.contains("</html>"));
        }

        SUBCASE("frontend without trailing slash redirects to canonical form") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug");
                CHECK(r.status == 302);
                CHECK(r.location == String(DebugServer::DefaultUiPrefix) + "/");
        }

        SUBCASE("frontend serves baked app.js") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/app.js");
                CHECK(r.status == 200);
                CHECK(r.body.contains("apiBase"));
        }

        SUBCASE("frontend serves baked style.css") {
                DbgResponse r = hit(f.port, "GET", "/promeki/debug/style.css");
                CHECK(r.status == 200);
                CHECK(r.contentType.contains("css"));
        }

        SUBCASE("root path redirects to UI prefix") {
                DbgResponse r = hit(f.port, "GET", "/");
                CHECK(r.status == 302);
                CHECK(r.location == DebugServer::DefaultUiPrefix);
        }
}

TEST_CASE("DebugServer_HttpServerAccessorAllowsCustomRoutes") {
        DbgFixture f;
        f.configure([](DebugServer &s) {
                s.httpServer().route("/custom", HttpMethod::Get,
                        [](const HttpRequest &, HttpResponse &res) {
                                res.setText("hello-from-custom");
                        });
        });
        f.listenOnAnyPort();

        DbgResponse r = hit(f.port, "GET", "/custom");
        CHECK(r.status == 200);
        CHECK(r.body == "hello-from-custom");
}

// ============================================================================
// Application integration
// ============================================================================

namespace {

}  // namespace

TEST_CASE("Application_DebugServerStartStopWiring") {
        // Verifies that Application::startDebugServer constructs the
        // DebugServer, mounts the default modules, and binds.  The
        // actual route round-trip is exercised by
        // DebugServer_DefaultModulesMountUnderApiPrefix on a worker-
        // thread fixture; here we only confirm the static lifecycle
        // through Application without driving the main loop.
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        REQUIRE(Application::debugServer() == nullptr);

        REQUIRE(Application::startDebugServer(0).isOk());
        DebugServer *srv = Application::debugServer();
        REQUIRE(srv != nullptr);
        CHECK(srv->isListening());
        CHECK(srv->serverAddress().port() != 0);
        CHECK(srv->serverAddress().address().toString() ==
              DebugServer::DefaultBindHost);

        Application::stopDebugServer();
        CHECK(Application::debugServer() == nullptr);
}

TEST_CASE("Application_DebugServerStartTwiceFails") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        REQUIRE(Application::startDebugServer(0).isOk());
        Error second = Application::startDebugServer(0);
        CHECK(second.isError());
        CHECK(second == Error::AlreadyOpen);

        Application::stopDebugServer();
}

TEST_CASE("Application_DebugServerEnvVarStartsServer") {
        const char *envName = Application::DebugServerEnv;
        Env::set(envName, ":0");

        {
                char arg0[] = "test";
                char *argv[] = { arg0 };
                Application app(1, argv);
                DebugServer *srv = Application::debugServer();
                REQUIRE(srv != nullptr);
                CHECK(srv->isListening());
                CHECK(srv->serverAddress().address().toString() ==
                      DebugServer::DefaultBindHost);
        }

        Env::unset(envName);
}

TEST_CASE("Application_DebugServerEnvVarUnsetSkipsServer") {
        const char *envName = Application::DebugServerEnv;
        Env::unset(envName);

        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);
        CHECK(Application::debugServer() == nullptr);
}

// ============================================================================
// WebSocket log streaming
// ============================================================================

namespace {

// Holds messages a WebSocket client receives, with a mutex for the
// cross-thread access pattern (signals fire on the WS thread; the
// test thread reads).
struct WsCapture {
        mutable Mutex   mu;
        StringList      messages;
        std::atomic<bool> connected{false};
        std::atomic<bool> closed{false};

        void onMessage(String m) {
                Mutex::Locker lock(mu);
                messages.pushToBack(m);
        }

        size_t size() const {
                Mutex::Locker lock(mu);
                return messages.size();
        }

        StringList snapshot() const {
                Mutex::Locker lock(mu);
                return messages;
        }
};

// Self-contained WebSocket client running on its own EventLoop thread
// (the standard pattern in tests/unit/network/websocket.cpp).
struct WsClient {
        Thread          thread;
        WebSocket       *ws = nullptr;

        WsClient() {
                thread.start();
                bool ready = false;
                thread.threadEventLoop()->postCallable([this, &ready]() {
                        ws = new WebSocket();
                        ready = true;
                });
                for(int i = 0; i < 200 && !ready; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                REQUIRE(ws != nullptr);
        }

        ~WsClient() {
                thread.threadEventLoop()->postCallable([this]() {
                        if(ws != nullptr) {
                                ws->abort();
                                delete ws;
                                ws = nullptr;
                        }
                });
                thread.quit();
                thread.wait(2000);
        }

        Error connect(const String &url) {
                Error result = Error::Invalid;
                bool done = false;
                thread.threadEventLoop()->postCallable([&]() {
                        result = ws->connectToUrl(url);
                        done = true;
                });
                for(int i = 0; i < 500 && !done; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                REQUIRE(done);
                return result;
        }

        template <typename Fn>
        void run(Fn fn) {
                bool done = false;
                thread.threadEventLoop()->postCallable([&]() {
                        fn();
                        done = true;
                });
                for(int i = 0; i < 500 && !done; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                REQUIRE(done);
        }
};

template <typename Pred>
static bool waitForFor(int ms, Pred pred) {
        for(int i = 0; i < ms; ++i) {
                if(pred()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
}

}  // namespace

TEST_CASE("DebugServer_WebSocket_LogStream_DeliversReplayAndLive") {
        DbgFixture f;
        f.installDefaults();
        f.listenOnAnyPort();

        Logger &logger = Logger::defaultLogger();
        size_t savedHistory = logger.historySize();
        logger.setHistorySize(64);

        // Drain prior history so the replay assertion below is
        // deterministic.  setHistorySize(0) drops the ring on the
        // next log; we then push a small known set of entries.
        logger.setHistorySize(0);
        logger.log(Logger::Info, "ws_drain.cpp", 1, "drain");
        logger.sync();
        logger.setHistorySize(64);

        for(int i = 0; i < 4; ++i) {
                logger.log(Logger::Info, "ws_replay.cpp", 100 + i,
                        String::sprintf("ws-replay-%d", i));
        }
        logger.sync();

        WsCapture cap;
        WsClient client;
        client.run([&]() {
                client.ws->connectedSignal.connect([&]() {
                        cap.connected.store(true);
                });
                client.ws->disconnectedSignal.connect([&]() {
                        cap.closed.store(true);
                });
                client.ws->textMessageReceivedSignal.connect([&](String msg) {
                        cap.onMessage(msg);
                });
        });

        const String url = String::sprintf(
                "ws://127.0.0.1:%u/promeki/debug/api/logger/stream?replay=4", f.port);
        REQUIRE(client.connect(url).isOk());
        REQUIRE(waitForFor(2000, [&]() { return cap.connected.load(); }));

        // Wait for the replayed entries to arrive.
        REQUIRE(waitForFor(2000, [&]() { return cap.size() >= 4; }));
        StringList replayed = cap.snapshot();
        // The first 4 messages should be our replay set.
        REQUIRE(replayed.size() >= 4);
        CHECK(replayed[replayed.size() - 4].contains("ws-replay-0"));
        CHECK(replayed[replayed.size() - 3].contains("ws-replay-1"));
        CHECK(replayed[replayed.size() - 2].contains("ws-replay-2"));
        CHECK(replayed[replayed.size() - 1].contains("ws-replay-3"));

        const size_t baseline = cap.size();

        // Log a new entry and verify the WS sees it as a live stream
        // delivery (i.e. arrives after replay).
        logger.log(Logger::Warn, "ws_live.cpp", 200, "ws-live-entry");
        REQUIRE(waitForFor(2000, [&]() { return cap.size() > baseline; }));

        StringList all = cap.snapshot();
        bool foundLive = false;
        for(const auto &m : all) {
                if(m.contains("ws-live-entry")) { foundLive = true; break; }
        }
        CHECK(foundLive);

        logger.setHistorySize(savedHistory);
}

TEST_CASE("DebugServer_WebSocket_LogStream_DefaultReplayWhenAbsent") {
        // No `?replay=` query — handler should default to 100, which
        // is fine even with an empty history (results in no replay).
        DbgFixture f;
        f.installDefaults();
        f.listenOnAnyPort();

        WsCapture cap;
        WsClient client;
        client.run([&]() {
                client.ws->connectedSignal.connect([&]() {
                        cap.connected.store(true);
                });
        });

        const String url = String::sprintf(
                "ws://127.0.0.1:%u/promeki/debug/api/logger/stream", f.port);
        REQUIRE(client.connect(url).isOk());
        REQUIRE(waitForFor(2000, [&]() { return cap.connected.load(); }));
        // Don't assert on message count — the test thread doesn't log
        // here, and replay may or may not include unrelated entries.
}

TEST_CASE("Application_DebugServerEnvVarBadSpecLogsAndSkips") {
        const char *envName = Application::DebugServerEnv;
        Env::set(envName, "this-is-not-a-valid-spec");

        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);
        // Bad spec → no server, no crash.  The Application logs a
        // warning, but it shouldn't abort startup.
        CHECK(Application::debugServer() == nullptr);

        Env::unset(envName);
}
