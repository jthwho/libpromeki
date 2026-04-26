/**
 * @file      websocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/websocket.h>
#include <promeki/httpserver.h>
#include <promeki/socketaddress.h>
#include <promeki/eventloop.h>
#include <promeki/thread.h>
#include <promeki/tcpsocket.h>
#include <promeki/buffer.h>
#include <promeki/string.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>

using namespace promeki;

namespace {

        // Spin up an HttpServer with a single WebSocket route.  Mirrors the
        // pattern used by the HttpServer / HttpClient suites — the server
        // runs on a worker Thread with its own EventLoop, the test thread
        // manipulates state via postCallable / atomic flags.
        struct WsServerFixture {
                        Thread      thread;
                        HttpServer *server = nullptr;
                        WebSocket  *acceptedSocket = nullptr;
                        uint16_t    port = 0;

                        // Per-test config knobs.
                        std::function<void(WebSocket *)> onAccepted;

                        WsServerFixture() {
                                thread.start();
                                thread.threadEventLoop()->postCallable([this]() { server = new HttpServer(); });
                                for (int i = 0; i < 200 && server == nullptr; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(server != nullptr);
                        }

                        ~WsServerFixture() {
                                thread.threadEventLoop()->postCallable([this]() {
                                        if (acceptedSocket != nullptr) {
                                                acceptedSocket->abort();
                                                delete acceptedSocket;
                                                acceptedSocket = nullptr;
                                        }
                                        delete server;
                                        server = nullptr;
                                });
                                thread.quit();
                                thread.wait(2000);
                        }

                        void listenWithRoute(const String &pattern, std::function<void(WebSocket *)> handler) {
                                bool done = false;
                                onAccepted = std::move(handler);
                                thread.threadEventLoop()->postCallable([this, pattern, &done]() {
                                        server->routeWebSocket(pattern, [this](WebSocket *ws, const HttpRequest &) {
                                                acceptedSocket = ws;
                                                if (onAccepted) onAccepted(ws);
                                        });
                                        Error err = server->listen(SocketAddress::localhost(0));
                                        REQUIRE(err.isOk());
                                        port = server->serverAddress().port();
                                        done = true;
                                });
                                for (int i = 0; i < 500 && !done; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                                REQUIRE(done);
                                REQUIRE(port != 0);
                        }
        };

        // Simple client driver that lives on its own EventLoop thread.
        // Mirrors the WsServerFixture pattern but for the client side.
        struct WsClientFixture {
                        Thread     thread;
                        WebSocket *ws = nullptr;

                        WsClientFixture() {
                                thread.start();
                                thread.threadEventLoop()->postCallable([this]() { ws = new WebSocket(); });
                                for (int i = 0; i < 200 && ws == nullptr; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(ws != nullptr);
                        }

                        ~WsClientFixture() {
                                thread.threadEventLoop()->postCallable([this]() {
                                        if (ws != nullptr) {
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
                                bool  done = false;
                                thread.threadEventLoop()->postCallable([&]() {
                                        result = ws->connectToUrl(url);
                                        done = true;
                                });
                                for (int i = 0; i < 500 && !done; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                                REQUIRE(done);
                                return result;
                        }

                        // Run a callable on the client's event loop.
                        void run(std::function<void()> fn) {
                                bool done = false;
                                thread.threadEventLoop()->postCallable([&]() {
                                        fn();
                                        done = true;
                                });
                                for (int i = 0; i < 500 && !done; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                                REQUIRE(done);
                        }
        };

        // Wait up to @p ms milliseconds for @p pred to become true.  Returns
        // true if @p pred fired within the budget.
        template <typename Pred> static bool waitFor(int ms, Pred pred) {
                for (int i = 0; i < ms; ++i) {
                        if (pred()) return true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return pred();
        }

} // anonymous namespace

TEST_CASE("WebSocket - computeAcceptValue matches RFC 6455 example") {
        // RFC 6455 §1.3: client key "dGhlIHNhbXBsZSBub25jZQ==" must
        // produce accept value "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
        const String accept = WebSocket::computeAcceptValue(String("dGhlIHNhbXBsZSBub25jZQ=="));
        CHECK(accept == String("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

TEST_CASE("WebSocket - text message echo round trip") {
        std::atomic<bool> serverConnected{false};
        std::atomic<bool> serverGotEcho{false};
        std::atomic<bool> clientConnected{false};
        std::atomic<bool> clientGotMessage{false};
        String            serverReceivedText;
        String            clientReceivedText;

        WsServerFixture sf;
        sf.listenWithRoute("/echo", [&](WebSocket *ws) {
                serverConnected = true;
                ws->textMessageReceivedSignal.connect([ws, &serverReceivedText, &serverGotEcho](String msg) {
                        serverReceivedText = msg;
                        ws->sendTextMessage(String("echo:") + msg);
                        serverGotEcho = true;
                });
        });

        WsClientFixture cf;
        cf.run([&]() {
                cf.ws->connectedSignal.connect([&]() {
                        clientConnected = true;
                        cf.ws->sendTextMessage("hello");
                });
                cf.ws->textMessageReceivedSignal.connect([&](String msg) {
                        clientReceivedText = msg;
                        clientGotMessage = true;
                });
        });

        const String url = String::sprintf("ws://127.0.0.1:%u/echo", sf.port);
        REQUIRE(cf.connect(url).isOk());

        // The server's accept callback runs after the upgrade
        // response drains; give both sides a generous window.
        REQUIRE(waitFor(2000, [&]() { return clientConnected.load() && serverConnected.load(); }));
        REQUIRE(waitFor(2000, [&]() { return clientGotMessage.load(); }));
        CHECK(serverReceivedText == String("hello"));
        CHECK(clientReceivedText == String("echo:hello"));
}

TEST_CASE("WebSocket - binary message echo round trip") {
        std::atomic<bool> clientReceivedBinary{false};
        Buffer            received;

        WsServerFixture sf;
        sf.listenWithRoute("/binecho", [&](WebSocket *ws) {
                ws->binaryMessageReceivedSignal.connect([ws](Buffer msg) { ws->sendBinaryMessage(msg); });
        });

        WsClientFixture cf;
        cf.run([&]() {
                cf.ws->connectedSignal.connect([&]() {
                        // Send a 200-byte payload — large enough to
                        // exercise the 2-byte length encoding path.
                        Buffer   payload(200);
                        uint8_t *p = static_cast<uint8_t *>(payload.data());
                        for (int i = 0; i < 200; ++i) {
                                p[i] = static_cast<uint8_t>(i & 0xFF);
                        }
                        payload.setSize(200);
                        cf.ws->sendBinaryMessage(payload);
                });
                cf.ws->binaryMessageReceivedSignal.connect([&](Buffer msg) {
                        received = msg;
                        clientReceivedBinary = true;
                });
        });

        const String url = String::sprintf("ws://127.0.0.1:%u/binecho", sf.port);
        REQUIRE(cf.connect(url).isOk());

        REQUIRE(waitFor(2000, [&]() { return clientReceivedBinary.load(); }));
        REQUIRE(received.size() == 200);
        for (int i = 0; i < 200; ++i) {
                CHECK(static_cast<const uint8_t *>(received.data())[i] == static_cast<uint8_t>(i & 0xFF));
        }
}

TEST_CASE("WebSocket - ping reply is a pong with same payload") {
        std::atomic<bool> clientGotPong{false};
        Buffer            pongPayload;

        WsServerFixture sf;
        sf.listenWithRoute("/ping", [&](WebSocket *ws) {
                (void)ws; // server handles ping internally
        });

        WsClientFixture cf;
        cf.run([&]() {
                cf.ws->connectedSignal.connect([&]() {
                        Buffer p(5);
                        std::memcpy(p.data(), "abcde", 5);
                        p.setSize(5);
                        cf.ws->ping(p);
                });
                cf.ws->pongReceivedSignal.connect([&](Buffer msg) {
                        pongPayload = msg;
                        clientGotPong = true;
                });
        });

        const String url = String::sprintf("ws://127.0.0.1:%u/ping", sf.port);
        REQUIRE(cf.connect(url).isOk());
        REQUIRE(waitFor(2000, [&]() { return clientGotPong.load(); }));
        REQUIRE(pongPayload.size() == 5);
        CHECK(std::memcmp(pongPayload.data(), "abcde", 5) == 0);
}

TEST_CASE("WebSocket - rejects non-WebSocket GET on the upgrade route") {
        // Clients that hit the route without proper upgrade headers
        // must receive a 400 — the route should not silently 200.
        WsServerFixture sf;
        sf.listenWithRoute("/ws", [](WebSocket *) {});

        // Use a raw TcpSocket to issue a plain GET so we observe the
        // server's HTTP response rather than driving the client
        // through the upgrade dance.
        TcpSocket sock;
        sock.open(IODevice::ReadWrite);
        REQUIRE(sock.connectToHost(SocketAddress::localhost(sf.port)).isOk());
        const char *req = "GET /ws HTTP/1.1\r\nHost: localhost\r\n"
                          "Connection: close\r\n\r\n";
        sock.write(req, std::strlen(req));
        char   buf[4096];
        String raw;
        for (;;) {
                int64_t n = sock.read(buf, sizeof(buf));
                if (n <= 0) break;
                raw += String(buf, static_cast<size_t>(n));
        }
        sock.close();
        CHECK(raw.find("400") != String::npos);
}
