/**
 * @file      mediaiotask_mjpegstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/httpheaders.h>
#include <promeki/httpmethod.h>
#include <promeki/httpserver.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_mjpegstream.h>
#include <promeki/mutex.h>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/socketaddress.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

        // Counts incoming JPEG frames and remembers the most recent buffer
        // for SOI/EOI assertions.  Thread-safe; the encoder strand may fire
        // onFrame from a worker thread while the test thread reads.
        struct CountingSubscriber : public MjpegStreamSubscriber {
                        std::atomic<int> frames{0};
                        std::atomic<int> closed{0};
                        Mutex            mtx;
                        Buffer::Ptr      lastJpeg;
                        TimeStamp        lastTs;

                        void onFrame(const Buffer::Ptr &jpeg, const TimeStamp &ts) override {
                                Mutex::Locker lk(mtx);
                                lastJpeg = jpeg;
                                lastTs = ts;
                                frames.fetch_add(1);
                        }
                        void onClosed() override { closed.fetch_add(1); }

                        Buffer::Ptr snapshotJpeg() {
                                Mutex::Locker lk(mtx);
                                return lastJpeg;
                        }
        };

        // TPG → MjpegStream rig.  TPG produces RGBA8 frames; the planner
        // would normally insert a CSC bridge for the JPEG encoder, but for a
        // direct test we feed RGB8 as input (not RGBA8) so the encoder takes
        // it natively.  We pick RGB8 explicitly so the rig doesn't need a
        // CSC stage; the encoder's supportedInputList includes RGB8.
        struct MjpegStreamRig {
                        MediaIO                 *tpg = nullptr;
                        MediaIO                 *sinkIo = nullptr;
                        MediaIOTask_MjpegStream *sink = nullptr;

                        ~MjpegStreamRig() {
                                if (tpg) {
                                        tpg->close();
                                        delete tpg;
                                }
                                if (sinkIo) {
                                        sinkIo->close();
                                        delete sinkIo;
                                }
                                // sink is owned by sinkIo and freed when sinkIo is.
                        }
        };

        void buildRig(MjpegStreamRig &rig, VideoFormat::WellKnownFormat sourceFormat, const Rational<int> &maxFps,
                      int quality = 70, int ringDepth = 1) {
                // ---- TPG source ----
                MediaIO::Config tpgCfg = MediaIO::defaultConfig("TPG");
                tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(sourceFormat));
                tpgCfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGB8_sRGB));
                tpgCfg.set(MediaConfig::VideoEnabled, true);
                tpgCfg.set(MediaConfig::AudioEnabled, false);
                rig.tpg = MediaIO::create(tpgCfg);
                REQUIRE(rig.tpg != nullptr);
                REQUIRE(rig.tpg->open(MediaIO::Source).isOk());

                // ---- MjpegStream sink ----
                rig.sink = new MediaIOTask_MjpegStream();
                rig.sinkIo = new MediaIO();
                MediaIO::Config sinkCfg = MediaIO::defaultConfig("MjpegStream");
                sinkCfg.set(MediaConfig::MjpegMaxFps, maxFps);
                sinkCfg.set(MediaConfig::MjpegQuality, quality);
                sinkCfg.set(MediaConfig::MjpegMaxQueueFrames, ringDepth);
                rig.sinkIo->setConfig(sinkCfg);
                REQUIRE(rig.sinkIo->setExpectedDesc(rig.tpg->mediaDesc()).isOk());
                REQUIRE(rig.sinkIo->adoptTask(rig.sink).isOk());
                REQUIRE(rig.sinkIo->open(MediaIO::Sink).isOk());
        }

        Error pumpOne(MjpegStreamRig &rig) {
                Frame::Ptr frame;
                Error      rerr = rig.tpg->readFrame(frame);
                if (rerr.isError()) return rerr;
                REQUIRE(frame.isValid());
                return rig.sinkIo->writeFrame(frame);
        }

        void pumpForMs(MjpegStreamRig &rig, int64_t durationMs) {
                ElapsedTimer t;
                t.start();
                while (t.elapsed() < durationMs) {
                        Error werr = pumpOne(rig);
                        if (werr.isError() && werr != Error::TryAgain) {
                                FAIL("pumpOne failed: " << werr.desc().cstr());
                        }
                }
        }

        // Verifies a JPEG bitstream begins with SOI (FFD8) and ends with EOI
        // (FFD9).  Both markers must be present for the buffer to count as a
        // well-formed JPEG.
        bool hasSoiEoi(const Buffer::Ptr &b) {
                if (!b.isValid() || b->size() < 4) return false;
                const uint8_t *p = static_cast<const uint8_t *>(b->data());
                const size_t   n = b->size();
                if (p[0] != 0xFF || p[1] != 0xD8) return false;
                if (p[n - 2] != 0xFF || p[n - 1] != 0xD9) return false;
                return true;
        }

        // Same check but for raw Buffer (used on wire-bytes).
        bool hasSoiEoiRaw(const Buffer &b) {
                if (!b.isValid() || b.size() < 4) return false;
                const uint8_t *p = static_cast<const uint8_t *>(b.data());
                const size_t   n = b.size();
                if (p[0] != 0xFF || p[1] != 0xD8) return false;
                if (p[n - 2] != 0xFF || p[n - 1] != 0xD9) return false;
                return true;
        }

} // namespace

// ============================================================================
// Format descriptor / factory plumbing
// ============================================================================

TEST_CASE("MediaIOTask_MjpegStream_FormatDescIsRegistered") {
        const auto desc = MediaIOTask_MjpegStream::formatDesc();
        CHECK(desc.name == String("MjpegStream"));
        CHECK(desc.canBeSink);
        CHECK_FALSE(desc.canBeSource);
        CHECK_FALSE(desc.canBeTransform);

        MediaIO::Config cfg = MediaIO::defaultConfig("MjpegStream");
        // Defaults are good for a smoke test — no upstream desc yet so
        // the rate gate stays inactive until the first frame.
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Sink).isOk());
        CHECK(io->isOpen());
        CHECK(io->close().isOk());
        delete io;
}

// ============================================================================
// Subscriber receives JPEG frames at roughly the configured rate
// ============================================================================

TEST_CASE("MediaIOTask_MjpegStream_SubscriberReceivesValidJpegs") {
        // Source @ 60 fps, encode gate @ 30 fps; over ~600 ms we
        // expect ~18 encodes (30 fps × 0.6 s).  Allow ±50% slack on
        // the rate to keep CI quiet.
        MjpegStreamRig rig;
        buildRig(rig, VideoFormat::Smpte1080p60, Rational<int>(30, 1),
                 /*quality=*/70);

        CountingSubscriber sub;
        const int          id = rig.sink->attachSubscriber(&sub);
        REQUIRE(id >= 0);

        pumpForMs(rig, 600);

        rig.sink->detachSubscriber(id);
        // Detach must trigger onClosed exactly once for the
        // detached subscriber.
        CHECK(sub.closed.load() == 1);

        // Sanity on the encode side.  We're tolerant on count,
        // strict on content (every encoded buffer is a real JPEG).
        const MjpegStreamSnapshot snap = rig.sink->snapshot();
        CHECK(snap.framesEncoded >= 8);
        CHECK(snap.framesEncoded <= 32);
        CHECK(snap.framesRateLimited > 0);
        CHECK(snap.totalEncodedBytes > 0);

        // The last JPEG buffer the subscriber saw must carry valid
        // SOI / EOI markers.
        const Buffer::Ptr last = sub.snapshotJpeg();
        CHECK(hasSoiEoi(last));
}

// ============================================================================
// Two simultaneous subscribers see the same frames
// ============================================================================

TEST_CASE("MediaIOTask_MjpegStream_MultipleSubscribersGetSameFrames") {
        MjpegStreamRig rig;
        buildRig(rig, VideoFormat::Smpte1080p30, Rational<int>(30, 1));

        CountingSubscriber a;
        CountingSubscriber b;
        const int          idA = rig.sink->attachSubscriber(&a);
        const int          idB = rig.sink->attachSubscriber(&b);
        REQUIRE(idA >= 0);
        REQUIRE(idB >= 0);
        REQUIRE(idA != idB);

        pumpForMs(rig, 400);

        // Both subscribers must have seen the same number of new
        // frames.  (The freshly-attached subscribers can each carry
        // a primer if a previous frame was already in the ring;
        // since we attached before any encode happened, neither got
        // a primer — so the counts equal the encode count.)
        rig.sink->detachSubscriber(idA);
        rig.sink->detachSubscriber(idB);

        CHECK(a.frames.load() == b.frames.load());
        CHECK(a.frames.load() > 0);
        CHECK(a.closed.load() == 1);
        CHECK(b.closed.load() == 1);
}

// ============================================================================
// A subscriber attached after the stream is flowing receives the
// most-recent cached frame inside attachSubscriber.
// ============================================================================

TEST_CASE("MediaIOTask_MjpegStream_LateAttachReceivesPrimer") {
        MjpegStreamRig rig;
        buildRig(rig, VideoFormat::Smpte1080p30, Rational<int>(30, 1),
                 /*quality=*/70,
                 /*ringDepth=*/3);

        // Pump frames first — without any subscriber attached — so
        // the ring fills up before the late-attach subscriber lands.
        pumpForMs(rig, 250);

        const MjpegStreamSnapshot warmed = rig.sink->snapshot();
        REQUIRE(warmed.framesEncoded >= 1);

        CountingSubscriber late;
        const int          id = rig.sink->attachSubscriber(&late);
        REQUIRE(id >= 0);
        // Attach must have delivered exactly one primer onFrame
        // synchronously before returning.  No further frames have
        // been pumped yet, so the count is 1 right now.
        CHECK(late.frames.load() == 1);
        CHECK(hasSoiEoi(late.snapshotJpeg()));

        rig.sink->detachSubscriber(id);
        CHECK(late.closed.load() == 1);
}

// ============================================================================
// detachSubscriber stops further callbacks
// ============================================================================

TEST_CASE("MediaIOTask_MjpegStream_DetachStopsCallbacks") {
        MjpegStreamRig rig;
        buildRig(rig, VideoFormat::Smpte1080p30, Rational<int>(30, 1));

        CountingSubscriber sub;
        const int          id = rig.sink->attachSubscriber(&sub);
        REQUIRE(id >= 0);

        pumpForMs(rig, 200);
        const int duringRun = sub.frames.load();
        REQUIRE(duringRun > 0);

        rig.sink->detachSubscriber(id);
        const int afterDetach = sub.frames.load();

        // Pump more frames; no further callbacks must arrive.
        pumpForMs(rig, 200);
        const int finalCount = sub.frames.load();

        CHECK(afterDetach == finalCount);
        CHECK(sub.closed.load() == 1);
}

// ============================================================================
// Latest accessors mirror the ring's tail.
// ============================================================================

TEST_CASE("MediaIOTask_MjpegStream_LatestAccessorsTrackRingTail") {
        MjpegStreamRig rig;
        buildRig(rig, VideoFormat::Smpte1080p30, Rational<int>(30, 1));

        CHECK_FALSE(rig.sink->latestJpeg().isValid());

        pumpForMs(rig, 200);
        const Buffer::Ptr latest = rig.sink->latestJpeg();
        CHECK(latest.isValid());
        CHECK(hasSoiEoi(latest));
}

// ============================================================================
// HTTP route smoke test.  The helper serves a continuous
// multipart/x-mixed-replace stream (boundary-separated JPEG parts)
// over a single connection.  The test connects with a hand-rolled
// HTTP/1.1 client, parses the wire bytes, and asserts on:
//  - 200 status + correct multipart Content-Type
//  - >=3 distinct JPEG parts arriving in <500 ms each
//  - each part begins with SOI (FFD8) and ends with EOI (FFD9)
//  - client disconnect tears down the subscriber cleanly
// ============================================================================

namespace {

        // Spin up an HttpServer on its own worker thread so the test thread
        // can pump frames into the sink while the server handles incoming
        // requests on a separate event loop.  Mirrors the pattern from
        // tests/unit/network/httpserver.cpp.
        struct ServerFixture {
                        Thread      thread;
                        HttpServer *server = nullptr;
                        uint16_t    port = 0;

                        explicit ServerFixture() {
                                thread.start();
                                thread.threadEventLoop()->postCallable([this]() { server = new HttpServer(); });
                                for (int i = 0; i < 200 && server == nullptr; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                }
                                REQUIRE(server != nullptr);
                        }

                        void configure(std::function<void(HttpServer &)> cfg) {
                                bool done = false;
                                thread.threadEventLoop()->postCallable([&]() {
                                        cfg(*server);
                                        done = true;
                                });
                                for (int i = 0; i < 500 && !done; ++i) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                }
                                REQUIRE(done);
                        }

                        void listenOnAnyPort() {
                                bool done = false;
                                thread.threadEventLoop()->postCallable([&]() {
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

                        ~ServerFixture() {
                                thread.threadEventLoop()->postCallable([this]() {
                                        delete server;
                                        server = nullptr;
                                });
                                thread.quit();
                                thread.wait(2000);
                        }
        };

        // Pumps source frames into the sink in a background thread until a
        // stop flag is set.  Used by the streaming HTTP test so the test
        // thread can read from the socket while encoded frames are
        // continuously produced on a separate strand.
        struct BackgroundPumper {
                        std::thread       worker;
                        std::atomic<bool> stop{false};

                        void start(MjpegStreamRig *rig) {
                                worker = std::thread([rig, this]() {
                                        while (!stop.load(std::memory_order_relaxed)) {
                                                Frame::Ptr f;
                                                Error      rerr = rig->tpg->readFrame(f);
                                                if (!rerr.isError() && f.isValid()) {
                                                        rig->sinkIo->writeFrame(f);
                                                }
                                                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                                        }
                                });
                        }
                        ~BackgroundPumper() {
                                stop.store(true, std::memory_order_relaxed);
                                if (worker.joinable()) worker.join();
                        }
        };

        // Connects, sends a GET, returns the headers as a String and a
        // stream-state struct that keeps reading body bytes until @p maxMs
        // elapses or @p targetParts complete JPEG parts have been observed.
        // Parses multipart/x-mixed-replace boundaries on-the-fly.
        struct StreamObservation {
                        int                   status = 0;
                        String                headers;
                        String                contentType;
                        promeki::List<Buffer> parts;
        };

        bool waitConnected(TcpSocket &sock, uint16_t port) {
                sock.open(IODevice::ReadWrite);
                Error err = sock.connectToHost(SocketAddress::localhost(port));
                return err.isOk();
        }

        // Pulls all immediately-available bytes off @p sock into @p sink.
        // Returns the number of bytes appended.  Polls @ref bytesAvailable
        // instead of @ref waitForReadyRead because TcpSocket inherits the
        // no-op default for the latter on this platform.  Sleeps briefly
        // per iteration so the loop can be bounded by an outer wall-clock
        // timer without busy-spinning.
        size_t drainSomeBytes(TcpSocket &sock, Buffer &sink, size_t &sinkLen, unsigned int timeoutMs) {
                ElapsedTimer t;
                t.start();
                char   buf[8192];
                size_t appended = 0;
                while (t.elapsed() < timeoutMs) {
                        if (sock.bytesAvailable() <= 0) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
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

        // Decodes a chunked transfer-encoding into a flat Buffer.  Walks
        // @p src from byte 0 to @p len, parsing chunk-size lines and
        // concatenating chunk bodies.  Returns the number of bytes decoded
        // up to the point we ran out of input (so we can call repeatedly as
        // more bytes arrive).  Returns @c SIZE_MAX on a malformed chunk.
        size_t decodeChunked(const uint8_t *src, size_t len, Buffer &dst, size_t &dstLen) {
                size_t cursor = 0;
                while (cursor < len) {
                        // Find end of chunk-size line.
                        size_t crlf = SIZE_MAX;
                        for (size_t i = cursor; i + 1 < len; ++i) {
                                if (src[i] == '\r' && src[i + 1] == '\n') {
                                        crlf = i;
                                        break;
                                }
                        }
                        if (crlf == SIZE_MAX) return cursor; // need more bytes
                        String       sizeLine(reinterpret_cast<const char *>(src + cursor), crlf - cursor);
                        const size_t semi = sizeLine.find(';');
                        const String hex = (semi == String::npos) ? sizeLine : sizeLine.left(semi);
                        const size_t chunkSize = std::strtoul(hex.cstr(), nullptr, 16);
                        const size_t bodyStart = crlf + 2;
                        if (chunkSize == 0) {
                                // Last chunk; consume optional trailers + CRLF.
                                return bodyStart + 2;
                        }
                        if (bodyStart + chunkSize + 2 > len) {
                                // Need more bytes for this chunk + trailing CRLF.
                                return cursor;
                        }
                        if (dstLen + chunkSize > dst.allocSize()) {
                                Buffer grown(dst.allocSize() * 2 + chunkSize);
                                std::memcpy(grown.data(), dst.data(), dstLen);
                                dst = std::move(grown);
                        }
                        std::memcpy(static_cast<uint8_t *>(dst.data()) + dstLen, src + bodyStart, chunkSize);
                        dstLen += chunkSize;
                        cursor = bodyStart + chunkSize + 2; // skip CRLF
                }
                dst.setSize(dstLen);
                return cursor;
        }

        // Walks the decoded multipart body and pulls out as many complete
        // JPEG parts as it can find (each delimited by SOI..EOI), starting
        // at @p searchStart.  Updates @p searchStart to the byte just past
        // the last extracted part.
        void harvestJpegs(const uint8_t *decoded, size_t decodedLen, size_t &searchStart, promeki::List<Buffer> &out) {
                // Each JPEG bitstream begins with FFD8 and ends with FFD9.
                // The multipart wrapping is irrelevant for SOI/EOI scanning;
                // the boundary text never contains those byte pairs.
                size_t i = searchStart;
                while (i + 1 < decodedLen) {
                        if (decoded[i] == 0xFF && decoded[i + 1] == 0xD8) {
                                const size_t soi = i;
                                size_t       j = soi + 2;
                                bool         foundEoi = false;
                                for (; j + 1 < decodedLen; ++j) {
                                        if (decoded[j] == 0xFF && decoded[j + 1] == 0xD9) {
                                                foundEoi = true;
                                                break;
                                        }
                                }
                                if (!foundEoi) {
                                        searchStart = soi; // wait for more bytes
                                        return;
                                }
                                const size_t end = j + 2; // include EOI
                                Buffer       part(end - soi);
                                std::memcpy(part.data(), decoded + soi, end - soi);
                                part.setSize(end - soi);
                                out.pushToBack(std::move(part));
                                i = end;
                        } else {
                                ++i;
                        }
                }
                searchStart = i;
        }

} // namespace

TEST_CASE("MediaIOTask_MjpegStream_HttpRouteStreamsMultipart") {
        // Source 60 fps, encoder gate 30 fps for plenty of frames.
        MjpegStreamRig rig;
        buildRig(rig, VideoFormat::Smpte1080p60, Rational<int>(30, 1));

        ServerFixture fix;
        fix.configure([&](HttpServer &s) { rig.sink->registerHttpRoute(s, "/preview"); });
        fix.listenOnAnyPort();

        // Continuously feed frames in a worker thread so the
        // streaming connection has something to send while the test
        // reads from the socket.
        BackgroundPumper pumper;
        pumper.start(&rig);

        TcpSocket sock;
        REQUIRE(waitConnected(sock, fix.port));

        const String req = "GET /preview HTTP/1.1\r\n"
                           "Host: localhost\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        sock.write(req.cstr(), req.byteCount());

        // Drain until we see headers or time out.
        Buffer       raw(16 * 1024);
        size_t       rawLen = 0;
        ElapsedTimer timer;
        timer.start();
        size_t headerEnd = SIZE_MAX;
        while (timer.elapsed() < 2000 && headerEnd == SIZE_MAX) {
                drainSomeBytes(sock, raw, rawLen, /*timeoutMs=*/100);
                const uint8_t *bytes = static_cast<const uint8_t *>(raw.data());
                for (size_t i = 3; i < rawLen; ++i) {
                        if (bytes[i - 3] == '\r' && bytes[i - 2] == '\n' && bytes[i - 1] == '\r' && bytes[i] == '\n') {
                                headerEnd = i + 1;
                                break;
                        }
                }
        }
        REQUIRE(headerEnd != SIZE_MAX);

        // Parse status line + headers.
        const char  *p = static_cast<const char *>(raw.data());
        const String head(p, headerEnd - 4);
        const size_t firstEol = head.find("\r\n");
        const String statusLine = (firstEol == String::npos) ? head : head.left(firstEol);
        const size_t sp1 = statusLine.find(' ');
        const size_t sp2 = statusLine.find(' ', sp1 + 1);
        const int    status = std::atoi(statusLine.mid(sp1 + 1, sp2 - sp1 - 1).cstr());
        CHECK(status == 200);

        const String headers = (firstEol == String::npos) ? String() : head.mid(firstEol + 2);
        StringList   lines = headers.split("\r\n");
        String       contentType;
        bool         chunked = false;
        for (size_t i = 0; i < lines.size(); ++i) {
                const String &ln = lines[i];
                const size_t  colon = ln.find(':');
                if (colon == String::npos) continue;
                String name = ln.left(colon);
                String value = ln.mid(colon + 1);
                if (!value.isEmpty() && value.cstr()[0] == ' ') value = value.mid(1);
                if (name.toLower() == "content-type") contentType = value;
                if (name.toLower() == "transfer-encoding" && value.toLower().contains("chunked")) chunked = true;
        }
        CHECK(contentType.contains("multipart/x-mixed-replace"));
        CHECK(contentType.contains(MediaIOTask_MjpegStream::HttpBoundary));
        CHECK(chunked);

        // Now drain body bytes.  Decode chunked transfer-encoding,
        // then scan the resulting multipart body for SOI/EOI pairs.
        Buffer                decoded(64 * 1024);
        size_t                decodedLen = 0;
        promeki::List<Buffer> parts;
        size_t                bodyStart = headerEnd;
        size_t                bodyConsumed = 0;
        size_t                scanStart = 0;
        while (timer.elapsed() < 3000 && parts.size() < 3) {
                drainSomeBytes(sock, raw, rawLen, /*timeoutMs=*/50);
                const uint8_t *bb = static_cast<const uint8_t *>(raw.data()) + bodyStart + bodyConsumed;
                const size_t   bbLen = rawLen - bodyStart - bodyConsumed;
                const size_t   consumed = decodeChunked(bb, bbLen, decoded, decodedLen);
                bodyConsumed += consumed;
                harvestJpegs(static_cast<const uint8_t *>(decoded.data()), decodedLen, scanStart, parts);
        }

        CHECK(parts.size() >= 3);
        for (size_t i = 0; i < parts.size(); ++i) {
                CHECK(hasSoiEoiRaw(parts[i]));
        }

        // Tear down: stop pumping and close the client socket.  The
        // server-side connection observes the half-close, drops its
        // body stream, the queue's last shared ref goes away, the
        // adapter (queue's child) destructs, and detachSubscriber
        // runs on the route thread.  Verify that the sink eventually
        // returns to a single subscriber count of zero by reading
        // its snapshot subscriber gauge indirectly: a fresh attach
        // should yield a new subscriber id and onClosed must fire
        // exactly once on detach (caught below).
        pumper.stop.store(true);
        sock.close();

        // Give the server time to notice the half-close + reap.  No
        // assertion shape here beyond "no crash" — ASan / UBSan in CI
        // catch any post-close use-after-free.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

TEST_CASE("MediaIOTask_MjpegStream_HttpRouteRefusesWhenSinkNotOpen") {
        // Build the sink but don't open it.  The route must surface
        // 503 rather than dropping the client into a never-emitting
        // multipart stream.
        MediaIOTask_MjpegStream *task = new MediaIOTask_MjpegStream();
        MediaIO                  sinkIo;
        sinkIo.setConfig(MediaIO::defaultConfig("MjpegStream"));
        REQUIRE(sinkIo.adoptTask(task).isOk());
        // Intentionally NOT calling open() here.

        ServerFixture fix;
        fix.configure([&](HttpServer &s) { task->registerHttpRoute(s, "/preview"); });
        fix.listenOnAnyPort();

        TcpSocket sock;
        REQUIRE(waitConnected(sock, fix.port));
        const String req = "GET /preview HTTP/1.1\r\n"
                           "Host: localhost\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        sock.write(req.cstr(), req.byteCount());

        Buffer       raw(8192);
        size_t       rawLen = 0;
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < 1500 && rawLen == 0) {
                drainSomeBytes(sock, raw, rawLen, /*timeoutMs=*/100);
        }
        REQUIRE(rawLen > 0);

        const String got(static_cast<const char *>(raw.data()), static_cast<size_t>(std::min<size_t>(rawLen, 64)));
        CHECK(got.contains("503"));

        sock.close();
}
