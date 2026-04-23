/**
 * @file      framebridge.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <cstring>
#include <thread>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/framebridge.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/imagedesc.h>
#include <promeki/framerate.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/uuid.h>
#include <promeki/dir.h>
#include <promeki/sharedmemory.h>
#include <promeki/localserver.h>
#include <cmath>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace promeki;

// ============================================================================
// Helpers
// ============================================================================

static String uniqueBridgeName(const char *tag) {
        return String("test-") + String(tag) + String("-") +
               UUID::generateV4().toString();
}

static FrameBridge::Config makeConfig() {
        FrameBridge::Config cfg;
        cfg.mediaDesc.setFrameRate(FrameRate(FrameRate::FPS_30));
        cfg.mediaDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(64, 48),
                          PixelFormat(PixelFormat::RGB8_sRGB)));
        cfg.audioDesc = AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        cfg.ringDepth = 2;
        return cfg;
}

static Frame::Ptr makeTestFrame(const FrameBridge::Config &cfg,
                                uint8_t fillByte) {
        Frame::Ptr f = Frame::Ptr::create();
        Image::Ptr img = Image::Ptr::create(cfg.mediaDesc.imageList()[0]);
        // Fill pixels with a pattern the reader can verify.
        for(int p = 0; p < img->desc().planeCount(); ++p) {
                const Buffer::Ptr &plane = img->plane(p);
                std::memset(plane->data(), fillByte, plane->size());
        }
        f.modify()->imageList().pushToBack(img);

        // One frame worth of audio.
        size_t samples = static_cast<size_t>(
                std::ceil(cfg.audioDesc.sampleRate() /
                          cfg.mediaDesc.frameRate().toDouble()));
        Audio::Ptr aud = Audio::Ptr::create(cfg.audioDesc, samples);
        std::memset(aud->buffer()->data(), fillByte,
                    aud->buffer()->size());
        f.modify()->audioList().pushToBack(aud);

        f.modify()->metadata().set(Metadata::Title, String("unit-test"));
        return f;
}

static void ensureIpcDir() {
        Dir::ipc().mkpath();
}

// Drive out.service() on a helper thread so openInput's handshake can
// progress on the main thread.  Call joinServiceThread() to stop.
struct ServiceThread {
        FrameBridge *bridge;
        std::atomic<bool> stop{false};
        std::thread t;
        explicit ServiceThread(FrameBridge *b) : bridge(b) {
                t = std::thread([this]() {
                        while(!stop.load(std::memory_order_relaxed)) {
                                bridge->service();
                                std::this_thread::sleep_for(
                                        std::chrono::milliseconds(2));
                        }
                });
        }
        ~ServiceThread() {
                stop.store(true, std::memory_order_relaxed);
                if(t.joinable()) t.join();
        }
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("FrameBridge: openOutput + openInput in one process") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("open");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        CHECK(out.isOpen());
        CHECK(out.isOutput());
        CHECK(out.ringDepth() == cfg.ringDepth);
        CHECK(out.name() == name);
        CHECK(out.uuid() != UUID());

        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());
        CHECK(in.isOpen());
        CHECK_FALSE(in.isOutput());
        CHECK(in.uuid() == out.uuid());
        CHECK(in.mediaDesc().imageList().size() == 1);
        CHECK(in.mediaDesc().imageList()[0].size() == Size2Du32(64, 48));
        CHECK(in.audioDesc().sampleRate() == 48000);
        CHECK(in.audioDesc().channels() == 2);
        CHECK(in.ringDepth() == cfg.ringDepth);
}

TEST_CASE("FrameBridge: connectionCount tracks connected inputs") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("count");
        FrameBridge out;
        REQUIRE(out.openOutput(name, makeConfig()).isOk());
        CHECK(out.connectionCount() == 0);

        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());

        // Give service() a moment to accept the input.
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(out.connectionCount() == 1);
}

TEST_CASE("FrameBridge: writeFrame no-op when zero inputs") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("noop");
        FrameBridge::Config cfg = makeConfig();
        // Override the default so writeFrame returns rather than
        // blocking while no consumer is connected.
        cfg.waitForConsumer = false;
        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());

        // With no readers, writeFrame should succeed without error.
        Frame::Ptr f = makeTestFrame(cfg, 0x55);
        for(int i = 0; i < 5; ++i) {
                CHECK(out.writeFrame(f).isOk());
        }
        CHECK(out.connectionCount() == 0);
}

TEST_CASE("FrameBridge: round-trip frame (image + audio + metadata)") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("rt");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());

        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());

        // Wait for service() to register the connection.
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);

        // Publish the frame.
        Frame::Ptr ping = makeTestFrame(cfg, 0xAA);
        REQUIRE(out.writeFrame(ping).isOk());

        Error rerr;
        Frame::Ptr rx;
        // Give the TICK a moment to arrive.
        for(int i = 0; i < 50 && !rx; ++i) {
                rx = in.readFrame(&rerr);
                if(!rx) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(rx);
        CHECK(rerr.isOk());
        REQUIRE(rx->imageList().size() == 1);
        const Image::Ptr &rxImg = rx->imageList()[0];
        REQUIRE(rxImg);
        const Buffer::Ptr &rxPlane = rxImg->plane(0);
        REQUIRE(rxPlane);
        CHECK(static_cast<const uint8_t *>(rxPlane->data())[0] == 0xAA);
        CHECK(static_cast<const uint8_t *>(rxPlane->data())[rxPlane->size() - 1] == 0xAA);

        REQUIRE(rx->audioList().size() == 1);
        const Audio::Ptr &rxAud = rx->audioList()[0];
        REQUIRE(rxAud);
        CHECK(static_cast<const uint8_t *>(rxAud->buffer()->data())[0] == 0xAA);

        CHECK(rx->metadata().getAs<String>(Metadata::Title) ==
              String("unit-test"));
}

TEST_CASE("FrameBridge: multiple frames round-trip distinct payloads") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("seq");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);

        std::vector<uint8_t> fills = { 0x11, 0x22, 0x33 };
        for(uint8_t fill : fills) {
                Frame::Ptr f = makeTestFrame(cfg, fill);
                REQUIRE(out.writeFrame(f).isOk());
                // Give input side a moment, then read.
                Frame::Ptr rx;
                for(int i = 0; i < 50 && !rx; ++i) {
                        rx = in.readFrame();
                        if(!rx) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                REQUIRE(rx);
                const uint8_t *px = static_cast<const uint8_t *>(
                        rx->imageList()[0]->plane(0)->data());
                CHECK(px[0] == fill);
        }
}

TEST_CASE("FrameBridge: openOutput twice with same name fails when first is live") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("dup");
        FrameBridge a;
        REQUIRE(a.openOutput(name, makeConfig()).isOk());
        FrameBridge b;
        Error err = b.openOutput(name, makeConfig());
        CHECK(err == Error::Exists);
        CHECK_FALSE(b.isOpen());
}

TEST_CASE("FrameBridge: openOutput recycles a stale name after crashed prior owner") {
        // Simulate a SIGKILL'd prior owner by leaving a shm region
        // under the bridge's name with no control-socket listener
        // bound — exactly the filesystem shape a killed process
        // leaves behind.  The probe inside openOutput must notice
        // there is no live listener and recycle both the socket path
        // and the orphaned shm region.
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("stale");

        // Stage a leftover shm region at the same name the bridge
        // would use.  Done via raw shm_open so there's no RAII owner
        // that might unlink it on scope exit and mask the test.
        String shmName = String("/promeki-fb-") + name;
        int leakedFd = ::shm_open(shmName.cstr(),
                                  O_CREAT | O_EXCL | O_RDWR, 0600);
        REQUIRE(leakedFd >= 0);
        REQUIRE(::ftruncate(leakedFd, 4096) == 0);
        ::close(leakedFd);

        FrameBridge b;
        Error err = b.openOutput(name, makeConfig());
        CHECK(err.isOk());
        CHECK(b.isOpen());
}

TEST_CASE("FrameBridge: openInput on missing name fails") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        FrameBridge in;
        Error err = in.openInput(uniqueBridgeName("none"));
        CHECK(err.isError());
        CHECK_FALSE(in.isOpen());
}

TEST_CASE("FrameBridge: readFrame returns fresh frames only, no duplicates") {
        // Regression: readFrame used to return the last slot on every
        // call, which let the MediaIO prefetch loop fill its queue with
        // duplicates and deliver real frames in bursts.  A readFrame
        // should only return a frame when a new TICK has been seen
        // since the last successful read.
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("fresh");
        FrameBridge::Config cfg = makeConfig();
        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);

        Frame::Ptr tx = makeTestFrame(cfg, 0x42);
        REQUIRE(out.writeFrame(tx).isOk());

        // First read after the TICK arrives: frame.
        Frame::Ptr first;
        for(int i = 0; i < 50 && !first; ++i) {
                first = in.readFrame();
                if(!first) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(first);

        // Subsequent reads with no new TICK: null.
        for(int i = 0; i < 10; ++i) {
                Frame::Ptr next = in.readFrame();
                CHECK_FALSE(next);
        }

        // New frame → new TICK → one more frame available.
        Frame::Ptr tx2 = makeTestFrame(cfg, 0x84);
        REQUIRE(out.writeFrame(tx2).isOk());
        Frame::Ptr second;
        for(int i = 0; i < 50 && !second; ++i) {
                second = in.readFrame();
                if(!second) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(second);
}

TEST_CASE("FrameBridge: input close stops the output from writing to a dead socket") {
        // Regression: when the input side went away, the output kept
        // trying to send TICKs and the underlying LocalSocket::write
        // repeatedly returned EPIPE without marking itself
        // disconnected — so writeFrame logged the same error on
        // every subsequent publication.  After the fix the output
        // drops the client on the first failing write and the
        // connection count reflects reality.
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("disc");
        FrameBridge::Config cfg = makeConfig();
        // This test specifically exercises the "input died while we
        // tried to write to it" path, so the publisher must not block
        // waiting for a replacement consumer after the input leaves.
        cfg.waitForConsumer = false;

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        {
                FrameBridge in;
                REQUIRE(in.openInput(name, /*sync=*/false).isOk());
                for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                REQUIRE(out.connectionCount() == 1);
        }
        // 'in' just went out of scope, which closes its socket.
        // The output won't notice until it tries to send — so drive
        // writes until the connection drops (should happen within a
        // couple of frames).
        Frame::Ptr tx = makeTestFrame(cfg, 0x00);
        for(int i = 0; i < 20 && out.connectionCount() > 0; ++i) {
                REQUIRE(out.writeFrame(tx).isOk());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(out.connectionCount() == 0);

        // Further writes must continue to succeed (writeFrame is a
        // no-op with zero clients) and must not blow up.
        for(int i = 0; i < 5; ++i) {
                CHECK(out.writeFrame(tx).isOk());
        }
}

TEST_CASE("FrameBridge: each openOutput mints a distinct UUID") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        FrameBridge a;
        REQUIRE(a.openOutput(uniqueBridgeName("u1"), makeConfig()).isOk());
        UUID u1 = a.uuid();
        a.close();

        FrameBridge b;
        REQUIRE(b.openOutput(uniqueBridgeName("u2"), makeConfig()).isOk());
        UUID u2 = b.uuid();

        CHECK(u1 != u2);
        CHECK(u1 != UUID());
        CHECK(u2 != UUID());
}

// ============================================================================
// Sync mode + timestamp
// ============================================================================

TEST_CASE("FrameBridge: no-sync input lets writeFrame return immediately") {
        // With sync=false the publisher does not wait on the input,
        // so writeFrame must return even when the input never reads.
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("nosync");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);
        CHECK_FALSE(in.isSyncInput());

        // Write several frames without the reader draining: writeFrame
        // must return quickly every time.
        Frame::Ptr f = makeTestFrame(cfg, 0x33);
        auto t0 = std::chrono::steady_clock::now();
        for(int i = 0; i < 5; ++i) {
                CHECK(out.writeFrame(f).isOk());
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
        // 5 non-blocking publishes should finish well under 500ms.
        CHECK(elapsed < 500);
}

TEST_CASE("FrameBridge: sync input blocks writeFrame until reader acks") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("sync");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/true).isOk());
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);
        CHECK(in.isSyncInput());

        // Launch the writer in a thread; it will block inside
        // writeFrame until the reader drains + acks.
        std::atomic<bool> writeReturned{false};
        std::thread writer([&]() {
                Frame::Ptr f = makeTestFrame(cfg, 0x5A);
                CHECK(out.writeFrame(f).isOk());
                writeReturned.store(true, std::memory_order_release);
        });

        // While the reader hasn't called readFrame, writeFrame must
        // still be blocked.  Wait a conservative amount of time —
        // not too long to make the test slow.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK_FALSE(writeReturned.load(std::memory_order_acquire));

        // Drain the TICK and ACK it.
        Frame::Ptr rx;
        for(int i = 0; i < 100 && !rx; ++i) {
                rx = in.readFrame();
                if(!rx) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(rx);

        // After the ACK, writeFrame must unblock.
        for(int i = 0; i < 200 && !writeReturned.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(writeReturned.load(std::memory_order_acquire));
        writer.join();
}

TEST_CASE("FrameBridge: lastFrameTimeStamp is populated + crosses the bridge") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("ts");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);

        // Before any publish, both sides report a default TimeStamp.
        CHECK(out.lastFrameTimeStamp() == TimeStamp());
        CHECK(in.lastFrameTimeStamp()  == TimeStamp());

        TimeStamp before = TimeStamp::now();
        Frame::Ptr f = makeTestFrame(cfg, 0x77);
        REQUIRE(out.writeFrame(f).isOk());
        TimeStamp after = TimeStamp::now();

        // Output side records a timestamp in [before, after].
        TimeStamp outTs = out.lastFrameTimeStamp();
        CHECK(outTs != TimeStamp());
        CHECK(outTs.nanoseconds() >= before.nanoseconds());
        CHECK(outTs.nanoseconds() <= after.nanoseconds());

        // Drain on the input side; the input's lastFrameTimeStamp
        // should equal the output's timestamp bit-for-bit.
        Frame::Ptr rx;
        for(int i = 0; i < 100 && !rx; ++i) {
                rx = in.readFrame();
                if(!rx) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(rx);
        CHECK(in.lastFrameTimeStamp() == outTs);
}

TEST_CASE("FrameBridge: waitForConsumer blocks writeFrame until a consumer connects") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("waitcons");
        FrameBridge::Config cfg = makeConfig();
        cfg.waitForConsumer = true;

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        REQUIRE(out.connectionCount() == 0);

        // No consumer yet → writeFrame must block.  Run it on a thread
        // so we can observe it stalling and then unblocking.
        std::atomic<bool> writeReturned{false};
        Error writeErr;
        std::thread writer([&]() {
                Frame::Ptr f = makeTestFrame(cfg, 0x2B);
                writeErr = out.writeFrame(f);
                writeReturned.store(true, std::memory_order_release);
        });

        // writeFrame must still be blocked after a short wait, since
        // nobody has connected yet.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK_FALSE(writeReturned.load(std::memory_order_acquire));

        // Connect a consumer — that should unblock the writer.
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());

        for(int i = 0; i < 500 && !writeReturned.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(writeReturned.load(std::memory_order_acquire));
        CHECK(writeErr.isOk());
        writer.join();
        CHECK(out.connectionCount() == 1);
}

TEST_CASE("FrameBridge: waitForConsumer=false does not block when no consumer") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("nowaitcons");
        FrameBridge::Config cfg = makeConfig();
        // Default is true; this test exercises the opt-out.
        cfg.waitForConsumer = false;

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        CHECK(out.connectionCount() == 0);

        // With the flag cleared, writeFrame must return immediately
        // as a no-op even when no consumer is attached.
        Frame::Ptr f = makeTestFrame(cfg, 0x2C);
        auto t0 = std::chrono::steady_clock::now();
        CHECK(out.writeFrame(f).isOk());
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
        CHECK(elapsed < 100);
}

TEST_CASE("FrameBridge: waitForConsumer is on by default") {
        FrameBridge::Config cfg;
        CHECK(cfg.waitForConsumer);
}

TEST_CASE("FrameBridge: abort() unblocks a writeFrame waiting for a consumer") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("abortwait");
        FrameBridge::Config cfg = makeConfig();
        cfg.waitForConsumer = true;

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());

        std::atomic<bool> writeReturned{false};
        Error writeErr;
        std::thread writer([&]() {
                Frame::Ptr f = makeTestFrame(cfg, 0x2E);
                writeErr = out.writeFrame(f);
                writeReturned.store(true, std::memory_order_release);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(writeReturned.load(std::memory_order_acquire));

        // Cross-thread abort must unblock writeFrame promptly.
        out.abort();
        for(int i = 0; i < 500 && !writeReturned.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(writeReturned.load(std::memory_order_acquire));
        CHECK(writeErr == Error::Cancelled);
        writer.join();

        // The abort latch persists: subsequent writeFrame calls while
        // the bridge is still open return Cancelled rather than
        // re-blocking.  Only reopening the bridge clears the latch.
        Frame::Ptr f = makeTestFrame(cfg, 0x2F);
        CHECK(out.writeFrame(f) == Error::Cancelled);
}

TEST_CASE("FrameBridge: waitForConsumer aborts on close") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("waitabort");
        FrameBridge::Config cfg = makeConfig();
        cfg.waitForConsumer = true;

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());

        std::atomic<bool> writeReturned{false};
        Error writeErr;
        std::thread writer([&]() {
                Frame::Ptr f = makeTestFrame(cfg, 0x2D);
                writeErr = out.writeFrame(f);
                writeReturned.store(true, std::memory_order_release);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(writeReturned.load(std::memory_order_acquire));

        // Closing must unblock writeFrame promptly.  close() trips the
        // abort flag on the way in, so the pending wait returns
        // Cancelled rather than stalling until role is flipped.
        out.close();
        for(int i = 0; i < 500 && !writeReturned.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(writeReturned.load(std::memory_order_acquire));
        CHECK(writeErr == Error::Cancelled);
        writer.join();
}

TEST_CASE("FrameBridge: successive publishes produce monotonically advancing timestamps") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        ensureIpcDir();
        String name = uniqueBridgeName("tsmono");
        FrameBridge::Config cfg = makeConfig();

        FrameBridge out;
        REQUIRE(out.openOutput(name, cfg).isOk());
        ServiceThread svc(&out);
        FrameBridge in;
        REQUIRE(in.openInput(name, /*sync=*/false).isOk());
        for(int i = 0; i < 100 && out.connectionCount() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(out.connectionCount() == 1);

        Frame::Ptr f = makeTestFrame(cfg, 0x01);
        REQUIRE(out.writeFrame(f).isOk());
        TimeStamp t1 = out.lastFrameTimeStamp();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(out.writeFrame(f).isOk());
        TimeStamp t2 = out.lastFrameTimeStamp();

        CHECK(t2.nanoseconds() > t1.nanoseconds());
}
