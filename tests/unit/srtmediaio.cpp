/**
 * @file      tests/srtmediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end loopback test for @ref SrtMediaIO: stands up a listener
 * source in a worker thread, dials it with a caller sink, writes a
 * handful of fake compressed video access units, then verifies the
 * same access-unit bytes round-trip through SRT + MPEG-TS framing.
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <mutex>
#include <thread>
#include <vector>

#include <promeki/buffer.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums_mediaio.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/socketaddress.h>
#include <promeki/srtmediaio.h>
#include <promeki/srtserver.h>
#include <promeki/url.h>
#include <promeki/videopayload.h>

using namespace promeki;

namespace {

        // Reserve a free loopback port by transiently listening on 0
        // and recording the assigned port.  Same pattern as the SRT
        // socket-level tests in tests/unit/network/srtsocket.cpp.
        uint16_t reserveLoopbackPort() {
                SrtServer probe;
                if (probe.listen(SocketAddress::localhost(0)).isError()) return 0;
                const uint16_t port = probe.serverAddress().port();
                probe.close();
                return port;
        }

        Buffer makeBuffer(const std::vector<uint8_t> &v) {
                Buffer b(v.size() ? v.size() : 1);
                b.setSize(v.size());
                if (!v.empty()) std::memcpy(b.data(), v.data(), v.size());
                return b;
        }

        std::vector<uint8_t> fakeAu(size_t size, uint8_t seed) {
                std::vector<uint8_t> v(size);
                for (size_t i = 0; i < size; ++i) v[i] = static_cast<uint8_t>((seed + i) & 0xFF);
                return v;
        }

} // anonymous namespace

TEST_CASE("SrtMediaIO: factory registration") {
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("Srt"));
        REQUIRE(factory != nullptr);
        CHECK(factory->canBeSource());
        CHECK(factory->canBeSink());
        CHECK(factory->schemes().contains(String("srt")));
        CHECK(MediaIOFactory::findByScheme(String("srt")) == factory);
}

TEST_CASE("SrtMediaIO: srt:// URL parses into config (Caller)") {
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("Srt"));
        REQUIRE(factory != nullptr);

        const Result<Url> r = Url::fromString(
                String("srt://192.0.2.10:4200?mode=caller&latency=80&"
                       "passphrase=this-is-the-key-12&pbkeylen=16&"
                       "streamid=publish/cam1&payloadsize=1316"));
        REQUIRE(r.second().isOk());

        MediaIO::Config cfg;
        REQUIRE(factory->urlToConfig(r.first(), &cfg).isOk());

        CHECK(cfg.getAs<String>(MediaConfig::Type) == String("Srt"));
        CHECK(cfg.get(MediaConfig::SrtMode).asEnum(SrtMode::Type) == SrtMode::Caller);
        CHECK(cfg.getAs<String>(MediaConfig::SrtPeerHost) == String("192.0.2.10"));
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtPeerPort) == 4200);
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtLatencyMs) == 80);
        CHECK(cfg.getAs<String>(MediaConfig::SrtPassphrase) == String("this-is-the-key-12"));
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtEncryptionKeyLength) == 16);
        CHECK(cfg.getAs<String>(MediaConfig::SrtStreamId) == String("publish/cam1"));
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtPayloadSize) == 1316);
}

TEST_CASE("SrtMediaIO: srt:// MediaMTX-style publish URL") {
        // The exact URL form an OBS / ffmpeg publisher would push to a
        // MediaMTX server.  Confirms the pkt_size alias, a colon inside
        // a streamid value, and that the absence of mode= defaults
        // to Caller.
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("Srt"));
        REQUIRE(factory != nullptr);

        const Result<Url> r = Url::fromString(
                String("srt://localhost:8890?streamid=publish:mystream&pkt_size=1316"));
        REQUIRE(r.second().isOk());

        MediaIO::Config cfg;
        REQUIRE(factory->urlToConfig(r.first(), &cfg).isOk());

        CHECK(cfg.get(MediaConfig::SrtMode).asEnum(SrtMode::Type) == SrtMode::Caller);
        CHECK(cfg.getAs<String>(MediaConfig::SrtPeerHost) == String("localhost"));
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtPeerPort) == 8890);
        CHECK(cfg.getAs<String>(MediaConfig::SrtStreamId) == String("publish:mystream"));
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtPayloadSize) == 1316);
}

TEST_CASE("SrtMediaIO: srt:// URL parses into config (Listener)") {
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("Srt"));
        REQUIRE(factory != nullptr);

        const Result<Url> r = Url::fromString(
                String("srt://0.0.0.0:4200?mode=listener&timeout=5000"));
        REQUIRE(r.second().isOk());

        MediaIO::Config cfg;
        REQUIRE(factory->urlToConfig(r.first(), &cfg).isOk());

        CHECK(cfg.get(MediaConfig::SrtMode).asEnum(SrtMode::Type) == SrtMode::Listener);
        CHECK(cfg.getAs<String>(MediaConfig::SrtLocalHost) == String("0.0.0.0"));
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtLocalPort) == 4200);
        CHECK(cfg.getAs<int32_t>(MediaConfig::SrtAcceptTimeoutMs) == 5000);
        CHECK(!cfg.contains(MediaConfig::SrtPeerHost));
}

TEST_CASE("SrtMediaIO: caller→listener loopback round-trip") {
        const uint16_t port = reserveLoopbackPort();
        REQUIRE(port != 0);

        std::atomic<bool>                 readerOpened{false};
        std::atomic<bool>                 readerDone{false};
        std::atomic<int>                  framesRead{0};
        std::mutex                        readBytesMutex;
        std::vector<std::vector<uint8_t>> readBytes;

        std::thread readerThread([&] {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("Srt");
                cfg.set(MediaConfig::Type, "Srt");
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Read));
                cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
                cfg.set(MediaConfig::SrtMode, SrtMode(SrtMode::Listener));
                cfg.set(MediaConfig::SrtLocalHost, String("localhost"));
                cfg.set(MediaConfig::SrtLocalPort, int32_t(port));
                cfg.set(MediaConfig::SrtLatencyMs, int32_t(80));
                cfg.set(MediaConfig::SrtAcceptTimeoutMs, int32_t(5000));

                MediaIO *io = MediaIO::create(cfg);
                if (io == nullptr) return;
                if (io->open().wait().isError()) {
                        delete io;
                        return;
                }
                readerOpened.store(true);

                MediaIOSource *src = io->source(0);
                if (src == nullptr) {
                        (void)io->close().wait();
                        delete io;
                        return;
                }

                while (true) {
                        MediaIORequest req = src->readFrame();
                        Error          err = req.wait();
                        if (err == Error::EndOfFile) break;
                        if (err.isError()) break;
                        const auto *cmd = req.commandAs<MediaIOCommandRead>();
                        if (cmd == nullptr) break;
                        Frame      f = cmd->frame;
                        const auto vps = f.videoPayloads();
                        if (vps.isEmpty()) continue;
                        const auto *cvp = vps[0]->as<CompressedVideoPayload>();
                        if (cvp == nullptr) continue;
                        auto                 plane0 = cvp->plane(0);
                        std::vector<uint8_t> bytes(plane0.data(),
                                                   plane0.data() + plane0.size());
                        {
                                std::lock_guard<std::mutex> lock(readBytesMutex);
                                readBytes.push_back(std::move(bytes));
                        }
                        framesRead.fetch_add(1, std::memory_order_release);
                }
                (void)io->close().wait();
                delete io;
                readerDone.store(true, std::memory_order_release);
        });

        // Give the listener time to bind / start accepting.  Without
        // this the caller can race ahead and SRT will return
        // SRT_ECONNREJ before the server is ready.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Srt");
        cfg.set(MediaConfig::Type, "Srt");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        cfg.set(MediaConfig::SrtMode, SrtMode(SrtMode::Caller));
        cfg.set(MediaConfig::SrtPeerHost, String("localhost"));
        cfg.set(MediaConfig::SrtPeerPort, int32_t(port));
        cfg.set(MediaConfig::SrtLatencyMs, int32_t(80));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        const int                         kCount = 8;
        std::vector<std::vector<uint8_t>> writtenBytes;
        for (int i = 0; i < kCount; ++i) {
                Frame                      frame;
                const std::vector<uint8_t> au = fakeAu(400 + i * 91,
                                                       static_cast<uint8_t>(0x60 + i));
                Buffer                     buf = makeBuffer(au);
                ImageDesc                  imgDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264));
                auto                       p = CompressedVideoPayload::Ptr::create(imgDesc, std::move(buf));
                const bool                 isKey = (i == 0);
                if (isKey) p.modify()->addFlag(MediaPayload::Keyframe);
                frame.addPayload(p);
                MediaIOSink *sk = io->sink(0);
                REQUIRE(sk != nullptr);
                REQUIRE(sk->writeFrame(frame).wait().isOk());
                writtenBytes.push_back(au);
        }

        // Let SRT TSBPD drain the in-flight queue before tearing
        // down the connection — the configured latency is 80 ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        REQUIRE(io->close().wait().isOk());
        delete io;

        // Wait (bounded) for the reader to surface EndOfFile.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!readerDone.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        readerThread.join();

        CHECK(readerOpened.load());
        CHECK(readerDone.load());
        CHECK(framesRead.load() == kCount);
        {
                std::lock_guard<std::mutex> lock(readBytesMutex);
                REQUIRE(static_cast<int>(readBytes.size()) == kCount);
                for (int i = 0; i < kCount; ++i) {
                        CHECK(readBytes[i] == writtenBytes[i]);
                }
        }
}
