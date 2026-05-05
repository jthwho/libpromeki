/**
 * @file      httpconnection_async.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <promeki/asyncbufferqueue.h>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <promeki/httpheaders.h>
#include <promeki/httpmethod.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>
#include <promeki/httpserver.h>
#include <promeki/httpstatus.h>
#include <promeki/iodevice.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>

using namespace promeki;

namespace {

        // Boilerplate: HttpServer hosted on its own worker thread.  Mirrors
        // the pattern used in tests/unit/network/httpserver.cpp and
        // mediaiotask_mjpegstream.cpp.
        struct ServerFixture {
                        Thread      thread;
                        HttpServer *server = nullptr;
                        uint16_t    port = 0;

                        ServerFixture() {
                                thread.start();
                                Atomic<bool> ready(false);
                                thread.threadEventLoop()->postCallable([this, &ready]() {
                                        server = new HttpServer();
                                        ready.setValue(true);
                                });
                                for (int i = 0; i < 200 && !ready.value(); ++i) {
                                        Thread::sleepMs(2);
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
                                        Thread::sleepMs(1);
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
                                        Thread::sleepMs(1);
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

        // Polls bytesAvailable + read in a loop.  TcpSocket on this build
        // inherits the no-op default for waitForReadyRead, so we hand-roll a
        // short-sleep poll that's bounded by the caller's wall-clock budget.
        size_t drainSomeBytes(TcpSocket &sock, Buffer &sink, size_t &sinkLen, unsigned int timeoutMs) {
                ElapsedTimer t;
                t.start();
                char   buf[4096];
                size_t appended = 0;
                while (t.elapsed() < timeoutMs) {
                        if (sock.bytesAvailable() <= 0) {
                                Thread::sleepMs(5);
                                continue;
                        }
                        int64_t got = sock.read(buf, sizeof(buf));
                        if (got <= 0) break;
                        if (sinkLen + static_cast<size_t>(got) > sink.allocSize()) {
                                Buffer grown(sink.allocSize() * 2 + static_cast<size_t>(got));
                                std::memcpy(grown.data(), sink.data(), sinkLen);
                                sink = std::move(grown);
                        }
                        std::memcpy(static_cast<uint8_t *>(sink.data()) + sinkLen, buf, static_cast<size_t>(got));
                        sinkLen += static_cast<size_t>(got);
                        appended += static_cast<size_t>(got);
                }
                sink.setSize(sinkLen);
                return appended;
        }

        // Builds a Buffer containing the given byte sequence.
        Buffer makeSegment(const char *bytes, size_t n) {
                Buffer ptr = Buffer(n);
                if (n > 0) std::memcpy(ptr.data(), bytes, n);
                ptr.setSize(n);
                return ptr;
        }

} // namespace

// ============================================================================
// Async-read pump-loop: a body device that returns read()==0 with
// atEnd()==false must NOT terminate the response.  The connection
// must park, then resume when bytes become available, then finish
// when the writer side closes.
// ============================================================================

TEST_CASE("HttpConnection_AsyncBodyStreamParksAndResumes") {
        ServerFixture fix;

        // Per-connection async queues + producer threads are managed
        // by the route handler closure.  Each handler invocation
        // allocates its own queue + spawns a one-shot pusher.
        struct PushPlan {
                        std::thread       pusher;
                        std::atomic<bool> finished{false};
        };
        // We test only one connection at a time so a single shared
        // PushPlan suffices for assertions back in the test thread.
        PushPlan plan;

        fix.configure([&](HttpServer &s) {
                s.route("/async", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        AsyncBufferQueue *q = new AsyncBufferQueue();
                        Error             oerr = q->open(IODevice::ReadOnly);
                        REQUIRE(oerr.isOk());
                        IODevice::Shared dev = IODevice::Shared::takeOwnership(q);

                        // The header phase will end with chunked
                        // encoding because we pass length=-1.
                        res.setStatus(HttpStatus::Ok);
                        res.setBodyStream(dev, /*length=*/-1, "text/plain");
                        res.setHeader("Connection", "close");

                        // Producer: enqueue three bursts at
                        // 100 ms intervals, then closeWriting.
                        // Note: the queue is held alive by
                        // (a) the response stream we just
                        // attached and (b) this thread's
                        // raw pointer.  When the connection
                        // drops the stream, the queue stays
                        // valid because the producer thread
                        // is still using it; the queue only
                        // dies after closeWriting + the
                        // connection sees atEnd via the
                        // already-drained queue.
                        plan.pusher = std::thread([q, &plan]() {
                                Thread::sleepMs(80);
                                q->enqueue(makeSegment("first ", 6));
                                Thread::sleepMs(80);
                                q->enqueue(makeSegment("second ", 7));
                                Thread::sleepMs(80);
                                q->enqueue(makeSegment("third", 5));
                                Thread::sleepMs(40);
                                q->closeWriting();
                                plan.finished.store(true);
                        });
                });
        });
        fix.listenOnAnyPort();

        TcpSocket sock;
        sock.open(IODevice::ReadWrite);
        REQUIRE(sock.connectToHost(SocketAddress::localhost(fix.port)).isOk());

        const String req = "GET /async HTTP/1.1\r\n"
                           "Host: localhost\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        sock.write(req.cstr(), req.byteCount());

        // Drain until either the connection closes (read==0) or we
        // exceed 2 s.  We expect ~250 ms of total wall time.
        Buffer       raw(8192);
        size_t       rawLen = 0;
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < 2000) {
                drainSomeBytes(sock, raw, rawLen, /*timeoutMs=*/100);
                // Tear-down: chunked encoding ends with "0\r\n\r\n".
                const char *p = static_cast<const char *>(raw.data());
                if (rawLen >= 5 && p[rawLen - 5] == '0' && p[rawLen - 4] == '\r' && p[rawLen - 3] == '\n' &&
                    p[rawLen - 2] == '\r' && p[rawLen - 1] == '\n') {
                        break;
                }
        }
        sock.close();

        if (plan.pusher.joinable()) plan.pusher.join();
        CHECK(plan.finished.load());

        // Decode the chunked body and verify all three pieces are
        // present in order.
        const char  *p = static_cast<const char *>(raw.data());
        const String wire(p, rawLen);
        const size_t sep = wire.find("\r\n\r\n");
        REQUIRE(sep != String::npos);
        const String headers = wire.left(sep);
        CHECK(headers.toLower().contains("transfer-encoding: chunked"));

        const String body = wire.mid(sep + 4);
        // De-chunk by hand: chunk-size CRLF body CRLF until 0 CRLF CRLF.
        String decoded;
        size_t cursor = 0;
        while (cursor < body.byteCount()) {
                const size_t crlf = body.find("\r\n", cursor);
                REQUIRE(crlf != String::npos);
                const String hex = body.mid(cursor, crlf - cursor);
                const size_t sz = std::strtoul(hex.cstr(), nullptr, 16);
                cursor = crlf + 2;
                if (sz == 0) break;
                decoded += body.mid(cursor, sz);
                cursor += sz + 2; // skip body + trailing CRLF
        }
        CHECK(decoded == String("first second third"));
}

// ============================================================================
// File-backed body devices (which return read()==0 with atEnd()==true)
// must continue to behave as before — no parking, no infinite wait.
// This is a regression guard for the new branch in pumpWrite.
// ============================================================================

TEST_CASE("HttpConnection_FixedLengthBodyStreamStillCompletes") {
        ServerFixture fix;

        // A simple seekable in-memory body — BufferIODevice.  When
        // exhausted it reports read()==0 with atEnd()==true, the
        // existing path that signals "stream done".
        Buffer payload(32);
        std::memcpy(payload.data(), "hello-from-the-async-pump-test", 30);
        payload.setSize(30);

        fix.configure([&](HttpServer &s) {
                s.route("/fixed", HttpMethod::Get, [&](const HttpRequest &, HttpResponse &res) {
                        BufferIODevice *bd = new BufferIODevice(&payload);
                        bd->open(IODevice::ReadOnly);
                        IODevice::Shared dev = IODevice::Shared::takeOwnership(bd);
                        res.setStatus(HttpStatus::Ok);
                        res.setBodyStream(dev, /*length=*/30, "text/plain");
                        res.setHeader("Connection", "close");
                });
        });
        fix.listenOnAnyPort();

        TcpSocket sock;
        sock.open(IODevice::ReadWrite);
        REQUIRE(sock.connectToHost(SocketAddress::localhost(fix.port)).isOk());
        const String req = "GET /fixed HTTP/1.1\r\n"
                           "Host: localhost\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        sock.write(req.cstr(), req.byteCount());

        Buffer       raw(8192);
        size_t       rawLen = 0;
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < 2000) {
                drainSomeBytes(sock, raw, rawLen, /*timeoutMs=*/50);
                if (rawLen > 0 && sock.bytesAvailable() == 0) {
                        // Try a final read to detect server close.
                        char    tmp;
                        int64_t got = sock.read(&tmp, 1);
                        if (got == 0) break;
                        if (got > 0) {
                                if (rawLen + 1 > raw.allocSize()) {
                                        Buffer grown(raw.allocSize() * 2);
                                        std::memcpy(grown.data(), raw.data(), rawLen);
                                        raw = std::move(grown);
                                }
                                static_cast<char *>(raw.data())[rawLen++] = tmp;
                        }
                }
        }
        sock.close();

        const char  *p = static_cast<const char *>(raw.data());
        const String wire(p, rawLen);
        const size_t sep = wire.find("\r\n\r\n");
        REQUIRE(sep != String::npos);
        const String body = wire.mid(sep + 4);
        CHECK(body == String("hello-from-the-async-pump-test"));
}
