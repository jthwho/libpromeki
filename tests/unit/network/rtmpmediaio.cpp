/**
 * @file      rtmpmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <promeki/amf0.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/clock.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/error.h>
#include <promeki/flvtag.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/h264bitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiostats.h>
#include <promeki/pixelformat.h>
#include <promeki/result.h>
#include <promeki/rtmpchunkstream.h>
#include <promeki/rtmpmediaio.h>
#include <promeki/rtmpmessage.h>
#include <promeki/rtmphandshake.h>
#include <promeki/rtmpsession.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/tcpserver.h>
#include <promeki/tcpsocket.h>
#include <promeki/url.h>

using namespace promeki;

namespace {

/**
 * @brief Minimal in-process RTMP server that drives the handshake +
 *        publish flow against a single accepted TcpSocket.
 *
 * Lifts the publish-side behaviors out of the @c FakeRtmpServer in
 * @c tests/unit/network/rtmpclient.cpp so the @c RtmpMediaIO can
 * exercise the same wire interactions.  Counts received video / audio
 * messages and watches for the sequence-header tag emission ordering
 * the writer is supposed to enforce.
 */
class FakeRtmpServer {
        public:
                explicit FakeRtmpServer(TcpSocket *sock) : _sock(sock) {}

                bool                          publishSeen = false;
                std::atomic<int>              videoReceived{0};
                std::atomic<int>              audioReceived{0};
                std::atomic<int>              videoSequenceHeaders{0};
                std::atomic<int>              audioSequenceHeaders{0};
                std::atomic<int>              videoKeyframes{0};
                // Number of received non-SequenceHeader video tags
                // whose AVCC payload carries at least one H.264 SPS
                // NAL (nal_unit_type == 7).  Used by the
                // RtmpRepeatParameterSets test to assert that every
                // IDR after the first one ships with in-band
                // parameter sets re-injected by the packetizer.
                std::atomic<int>              videoKeyframesWithSps{0};
                std::atomic<bool>             stop{false};

                void run() {
                        RtmpHandshake hs(RtmpRole::Server);
                        while (hs.state() != RtmpHandshake::Done
                               && hs.state() != RtmpHandshake::Failed
                               && !stop.load()) {
                                Buffer pending = hs.pendingOutput();
                                if (pending.size() > 0) {
                                        _sock->write(pending.data(), pending.size());
                                        continue;
                                }
                                if (hs.state() == RtmpHandshake::Done) break;
                                uint8_t buf[4096];
                                if (_sock->bytesAvailable() == 0) {
                                        if (!_sock->waitForReadyRead(500)) {
                                                if (stop.load()) return;
                                                continue;
                                        }
                                }
                                int64_t n = _sock->read(buf, sizeof(buf));
                                if (n <= 0) return;
                                Buffer wrap = Buffer::wrapHost(buf, static_cast<size_t>(n));
                                wrap.setSize(static_cast<size_t>(n));
                                hs.feed(BufferView(wrap, 0, static_cast<size_t>(n)));
                        }
                        if (hs.state() != RtmpHandshake::Done) return;

                        RtmpChunkStream chunk(_sock);
                        while (!stop.load()) {
                                Result<RtmpMessage> got = chunk.readMessage(100);
                                if (got.second().isError()) {
                                        if (got.second() == Error::Timeout) continue;
                                        return;
                                }
                                const RtmpMessage &msg = got.first();
                                if (msg.type == RtmpMessage::CommandMessageAmf0) {
                                        handleCommand(chunk, msg);
                                } else if (msg.type == RtmpMessage::VideoMessage) {
                                        ++videoReceived;
                                        inspectVideo(msg);
                                } else if (msg.type == RtmpMessage::AudioMessage) {
                                        ++audioReceived;
                                        inspectAudio(msg);
                                }
                        }
                }

        private:
                void handleCommand(RtmpChunkStream &chunk, const RtmpMessage &msg) {
                        auto parsed = Amf0Reader::read(
                                static_cast<const uint8_t *>(msg.payload.data()),
                                msg.payload.size());
                        if (parsed.second().isError()) return;
                        const auto &vals = parsed.first();
                        if (vals.size() < 2) return;
                        String cmd = vals[0].asString();
                        double txn = vals[1].asNumber();

                        if (cmd == "connect") {
                                {
                                        RtmpMessage m;
                                        m.type = RtmpMessage::WindowAckSize;
                                        m.streamId = 0;
                                        m.chunkStreamId = 2;
                                        m.payload = Buffer(4);
                                        m.payload.setSize(4);
                                        auto *q = static_cast<uint8_t *>(m.payload.data());
                                        q[0] = 0; q[1] = 0x4C; q[2] = 0x4B; q[3] = 0x40;
                                        chunk.writeMessage(m);
                                }
                                {
                                        RtmpMessage m;
                                        m.type = RtmpMessage::SetPeerBandwidth;
                                        m.streamId = 0;
                                        m.chunkStreamId = 2;
                                        m.payload = Buffer(5);
                                        m.payload.setSize(5);
                                        auto *q = static_cast<uint8_t *>(m.payload.data());
                                        q[0] = 0; q[1] = 0x4C; q[2] = 0x4B; q[3] = 0x40;
                                        q[4] = 0x02;
                                        chunk.writeMessage(m);
                                }
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("_result");
                                w.writeNumber(txn);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code", Amf0Value(String("NetConnection.Connect.Success")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.chunkStreamId = 3;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                return;
                        }
                        if (cmd == "createStream") {
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("_result");
                                w.writeNumber(txn);
                                w.writeNull();
                                w.writeNumber(1.0);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.chunkStreamId = 3;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                return;
                        }
                        if (cmd == "releaseStream" || cmd == "FCPublish"
                            || cmd == "FCUnpublish" || cmd == "FCSubscribe") {
                                return;
                        }
                        if (cmd == "publish") {
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("onStatus");
                                w.writeNumber(txn);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code", Amf0Value(String("NetStream.Publish.Start")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = msg.streamId;
                                reply.chunkStreamId = 5;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                publishSeen = true;
                                return;
                        }
                        if (cmd == "play") {
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("onStatus");
                                w.writeNumber(txn);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code", Amf0Value(String("NetStream.Play.Start")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = msg.streamId;
                                reply.chunkStreamId = 5;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                return;
                        }
                }

                void inspectVideo(const RtmpMessage &msg) {
                        Buffer payload = msg.payload;
                        BufferView view(payload, 0, payload.size());
                        FlvVideoTag tag;
                        if (FlvVideoTag::unpack(view, tag).isOk()) {
                                if (tag.packetType == FlvVideoTag::SequenceHeader) {
                                        ++videoSequenceHeaders;
                                }
                                if (tag.frameType == FlvVideoTag::Keyframe ||
                                    tag.frameType == FlvVideoTag::GeneratedKeyframe) {
                                        ++videoKeyframes;
                                        if (tag.packetType == FlvVideoTag::Nalu) {
                                                BufferView avccView(tag.data, 0, tag.data.size());
                                                bool       hasSps = false;
                                                H264Bitstream::forEachAvccNal(
                                                        avccView, /*lenSize=*/4,
                                                        [&hasSps](const H264Bitstream::NalUnit &nu)
                                                                        -> Error {
                                                                if ((nu.header0 & 0x1F) == 7) hasSps = true;
                                                                return Error::Ok;
                                                        });
                                                if (hasSps) ++videoKeyframesWithSps;
                                        }
                                }
                        }
                }

                void inspectAudio(const RtmpMessage &msg) {
                        Buffer payload = msg.payload;
                        BufferView view(payload, 0, payload.size());
                        FlvAudioTag tag;
                        if (FlvAudioTag::unpack(view, tag).isOk()) {
                                if (tag.aacPacketType == FlvAudioTag::AudioSpecificConfig) {
                                        ++audioSequenceHeaders;
                                }
                        }
                }

                TcpSocket *_sock;
};

Url localhostUrl(uint16_t port, const String &appAndKey) {
        String s = "rtmp://127.0.0.1:";
        s += String::number(port);
        s += "/";
        s += appAndKey;
        return Url(s);
}

/// @brief Builds a minimal Annex-B H.264 IDR access unit (just SPS + PPS + IDR slice NALs).
Buffer makeFakeAnnexBKeyframe() {
        // The H.264 Annex-B byte stream needs valid SPS/PPS for our
        // packetizer's AvcDecoderConfig::fromAnnexB to succeed.  We
        // hand-craft a minimal baseline SPS + PPS lifted from a known
        // pattern, then append a stub IDR slice NAL.
        //
        // SPS bytes encode: profile=66 (baseline), level=10, 16x16 mb,
        // 1 frame.
        static const uint8_t kSps[] = {
                0x67, 0x42, 0xC0, 0x0A,
                0xDB, 0x06, 0x07, 0xE8,
                0x40, 0x00, 0x00, 0x03,
                0x00, 0x40, 0x00, 0x00,
                0x0F, 0x03, 0xC5, 0x8B,
                0x80
        };
        static const uint8_t kPps[] = { 0x68, 0xCE, 0x38, 0x80 };
        static const uint8_t kIdr[] = { 0x65, 0x88, 0x84, 0x21,
                                        0xFF, 0xEF, 0xFF };
        static const uint8_t kStartCode[] = { 0x00, 0x00, 0x00, 0x01 };

        size_t total = (sizeof(kStartCode) + sizeof(kSps)) +
                       (sizeof(kStartCode) + sizeof(kPps)) +
                       (sizeof(kStartCode) + sizeof(kIdr));
        Buffer out(total);
        out.setSize(total);
        uint8_t *p = static_cast<uint8_t *>(out.data());
        std::memcpy(p, kStartCode, sizeof(kStartCode));     p += sizeof(kStartCode);
        std::memcpy(p, kSps, sizeof(kSps));                 p += sizeof(kSps);
        std::memcpy(p, kStartCode, sizeof(kStartCode));     p += sizeof(kStartCode);
        std::memcpy(p, kPps, sizeof(kPps));                 p += sizeof(kPps);
        std::memcpy(p, kStartCode, sizeof(kStartCode));     p += sizeof(kStartCode);
        std::memcpy(p, kIdr, sizeof(kIdr));
        return out;
}

/// @brief Builds an Annex-B IDR access unit with @e only the slice NAL — no SPS/PPS.
///
/// Represents the case where the encoder emits a keyframe whose
/// parameter sets are not re-included inline.  The packetizer should
/// detect the missing parameter sets and prepend the cached ones from
/// the first IDR (RtmpRepeatParameterSets policy).
Buffer makeIdrOnlyAnnexB() {
        static const uint8_t kIdr[] = { 0x65, 0x88, 0x84, 0x21,
                                        0xFF, 0xEF, 0xFF };
        static const uint8_t kStartCode[] = { 0x00, 0x00, 0x00, 0x01 };
        size_t total = sizeof(kStartCode) + sizeof(kIdr);
        Buffer out(total);
        out.setSize(total);
        uint8_t *p = static_cast<uint8_t *>(out.data());
        std::memcpy(p, kStartCode, sizeof(kStartCode));
        p += sizeof(kStartCode);
        std::memcpy(p, kIdr, sizeof(kIdr));
        return out;
}

/// @brief Builds a minimal raw AAC frame (just stub bytes — the encoder side isn't decoded).
Buffer makeFakeAacFrame(uint8_t seed) {
        Buffer out(16);
        out.setSize(16);
        std::memset(out.data(), seed, 16);
        return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Factory registration
// ---------------------------------------------------------------------------

TEST_CASE("RtmpFactory is registered under the Rtmp name and rtmp(s) schemes") {
        const MediaIOFactory *byName = MediaIOFactory::findByName(String("Rtmp"));
        REQUIRE(byName != nullptr);
        CHECK(byName->canBeSource());
        CHECK(byName->canBeSink());
        const MediaIOFactory *byRtmp = MediaIOFactory::findByScheme(String("rtmp"));
        const MediaIOFactory *byRtmps = MediaIOFactory::findByScheme(String("rtmps"));
        CHECK(byRtmp == byName);
        CHECK(byRtmps == byName);
}

TEST_CASE("RtmpFactory::urlToConfig populates RtmpUrl and Type") {
        const MediaIOFactory *fac = MediaIOFactory::findByName(String("Rtmp"));
        REQUIRE(fac != nullptr);
        Url u("rtmp://example.com/live/streamKey");
        MediaConfig cfg;
        REQUIRE(fac->urlToConfig(u, &cfg) == Error::Ok);
        CHECK(cfg.getAs<String>(MediaConfig::Type, String()) == String("Rtmp"));
        Variant urlVar = cfg.get(MediaConfig::RtmpUrl);
        REQUIRE(urlVar.type() == Variant::TypeUrl);
        CHECK(urlVar.get<Url>().toString() == u.toString());
}

TEST_CASE("RtmpFactory::urlToConfig rejects unsupported URL schemes") {
        const MediaIOFactory *fac = MediaIOFactory::findByName(String("Rtmp"));
        REQUIRE(fac != nullptr);
        Url         http("http://example.com/x");
        MediaConfig cfg;
        CHECK(fac->urlToConfig(http, &cfg) == Error::InvalidArgument);
}

TEST_CASE("RtmpMediaIO::objectId is unique per instance and monotonic") {
        RtmpMediaIO a;
        RtmpMediaIO b;
        CHECK(a.objectId() != 0);
        CHECK(b.objectId() != 0);
        CHECK(b.objectId() > a.objectId());
}

// ---------------------------------------------------------------------------
// Sink-mode end-to-end
// ---------------------------------------------------------------------------

TEST_CASE("RtmpMediaIO sink: publishes H.264 + AAC frames end-to-end") {
        TcpServer server;
        REQUIRE(server.listen(SocketAddress::localhost(0)) == Error::Ok);
        const uint16_t port = server.serverAddress().port();

        std::atomic<bool>         running{true};
        std::atomic<bool>         publishObserved{false};
        std::atomic<int>          videoSeqHeaders{0};
        std::atomic<int>          audioSeqHeaders{0};
        std::atomic<int>          videoKeyframes{0};

        std::thread serverThread([&]() {
                while (running.load()) {
                        Error err = server.waitForNewConnection(200);
                        if (err == Error::Timeout) continue;
                        if (err.isError()) return;
                        TcpSocket *client = server.nextPendingConnection();
                        if (client == nullptr) continue;
                        FakeRtmpServer fake(client);
                        std::thread fakeThread([&]() { fake.run(); });

                        auto deadline = std::chrono::steady_clock::now()
                                        + std::chrono::seconds(5);
                        while (running.load()
                               && (!fake.publishSeen
                                   || fake.videoSequenceHeaders.load() == 0
                                   || fake.audioSequenceHeaders.load() == 0
                                   || fake.videoReceived.load() < 2
                                   || fake.audioReceived.load() < 2)) {
                                if (std::chrono::steady_clock::now() >= deadline) break;
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        publishObserved.store(fake.publishSeen);
                        videoSeqHeaders.store(fake.videoSequenceHeaders.load());
                        audioSeqHeaders.store(fake.audioSequenceHeaders.load());
                        videoKeyframes.store(fake.videoKeyframes.load());
                        fake.stop = true;
                        fakeThread.join();
                        delete client;
                        running = false;
                }
        });

        // Build the MediaIO directly so we control the config exactly.
        MediaConfig cfg;
        cfg.set(MediaConfig::Type, String("Rtmp"));
        cfg.set(MediaConfig::RtmpUrl, localhostUrl(port, "live/myStream"));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::RtmpConnectTimeoutMs, int32_t(5000));
        cfg.set(MediaConfig::RtmpCommandTimeoutMs, int32_t(5000));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        REQUIRE(io->open().wait() == Error::Ok);
        REQUIRE(io->isOpen());
        MediaIOSink *sink = io->sink(0);
        REQUIRE(sink != nullptr);

        // Build two frames: an IDR + an inter-frame.  Both carry a
        // CompressedVideoPayload and a CompressedAudioPayload so the
        // packetizer fires both kinds.
        const ImageDesc videoDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::H264));
        const AudioDesc audioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2);
        Buffer          keyframe = makeFakeAnnexBKeyframe();

        // Frame 1 — keyframe (triggers sequence-header emission).
        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, keyframe);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);

                CompressedAudioPayload::Ptr ca =
                        CompressedAudioPayload::Ptr::create(audioDesc, makeFakeAacFrame(0x10));
                f.addPayload(ca);

                CHECK(sink->writeFrame(f).wait() == Error::Ok);
        }

        // Frame 2 — inter-frame, no keyframe flag.  Reuse the same
        // Annex-B blob since the packetizer's Annex-B parser doesn't
        // care about frame-type for the conversion itself.
        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, keyframe);
                f.addPayload(cv);

                CompressedAudioPayload::Ptr ca =
                        CompressedAudioPayload::Ptr::create(audioDesc, makeFakeAacFrame(0x11));
                f.addPayload(ca);

                CHECK(sink->writeFrame(f).wait() == Error::Ok);
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (publishObserved.load() == false ||
               videoSeqHeaders.load() == 0 ||
               audioSeqHeaders.load() == 0) {
                if (std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        running = false;
        CHECK(io->close().wait() == Error::Ok);
        delete io;
        serverThread.join();

        CHECK(publishObserved.load());
        CHECK(videoSeqHeaders.load() >= 1);
        CHECK(audioSeqHeaders.load() >= 1);
        CHECK(videoKeyframes.load() >= 1);
}

// ---------------------------------------------------------------------------
// Error-path coverage
// ---------------------------------------------------------------------------

TEST_CASE("RtmpMediaIO: open fails cleanly when no URL is configured") {
        MediaConfig cfg;
        cfg.set(MediaConfig::Type, String("Rtmp"));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open().wait() != Error::Ok);
        CHECK(!io->isOpen());
        delete io;
}

TEST_CASE("RtmpMediaIO: open fails when the target refuses the connection") {
        uint16_t port = 0;
        {
                TcpServer probe;
                REQUIRE(probe.listen(SocketAddress::localhost(0)) == Error::Ok);
                port = probe.serverAddress().port();
                probe.close();
        }

        MediaConfig cfg;
        cfg.set(MediaConfig::Type, String("Rtmp"));
        cfg.set(MediaConfig::RtmpUrl, localhostUrl(port, "live/x"));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::RtmpConnectTimeoutMs, int32_t(500));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open().wait() != Error::Ok);
        CHECK(!io->isOpen());
        delete io;
}

TEST_CASE("RtmpMediaIO sink: peer-disconnect mid-stream surfaces as writeFrame error") {
        // Reproduces the user-reported scenario where mediamtx goes
        // away after publish is established: the strand-side write
        // path should latch the disconnect and surface it to the
        // pipeline rather than silently dropping frames into a dead
        // packetizer that spews per-frame send failures.
        TcpServer server;
        REQUIRE(server.listen(SocketAddress::localhost(0)) == Error::Ok);
        const uint16_t port = server.serverAddress().port();

        std::atomic<bool> running{true};
        std::atomic<bool> publishObserved{false};

        std::thread serverThread([&]() {
                while (running.load()) {
                        Error err = server.waitForNewConnection(200);
                        if (err == Error::Timeout) continue;
                        if (err.isError()) return;
                        TcpSocket *client = server.nextPendingConnection();
                        if (client == nullptr) continue;
                        FakeRtmpServer fake(client);
                        std::thread fakeThread([&]() { fake.run(); });
                        // Wait until publish has succeeded, then yank
                        // the socket out from under the client to
                        // simulate the peer going away mid-stream.
                        auto deadline = std::chrono::steady_clock::now()
                                        + std::chrono::seconds(5);
                        while (running.load() && !fake.publishSeen) {
                                if (std::chrono::steady_clock::now() >= deadline) break;
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        publishObserved.store(fake.publishSeen);
                        fake.stop = true;
                        client->close();
                        fakeThread.join();
                        delete client;
                        running = false;
                }
        });

        MediaConfig cfg;
        cfg.set(MediaConfig::Type, String("Rtmp"));
        cfg.set(MediaConfig::RtmpUrl, localhostUrl(port, "live/byebye"));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::RtmpConnectTimeoutMs, int32_t(5000));
        cfg.set(MediaConfig::RtmpCommandTimeoutMs, int32_t(5000));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait() == Error::Ok);
        MediaIOSink *sink = io->sink(0);
        REQUIRE(sink != nullptr);

        const ImageDesc videoDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::H264));
        const AudioDesc audioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2);
        Buffer          keyframe = makeFakeAnnexBKeyframe();

        // Send one keyframe to push through publish + sequence-header
        // and trigger the fake to drop the socket.
        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, keyframe);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                CompressedAudioPayload::Ptr ca =
                        CompressedAudioPayload::Ptr::create(audioDesc, makeFakeAacFrame(0x20));
                f.addPayload(ca);
                CHECK(sink->writeFrame(f).wait() == Error::Ok);
        }

        // Once the peer drops the socket, subsequent writeFrame calls
        // must surface an error.  Poll briefly so we don't depend on
        // the exact moment the WriterThread notices EPIPE.
        Error lastWriteErr = Error::Ok;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, keyframe);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                lastWriteErr = sink->writeFrame(f).wait();
                if (lastWriteErr.isError() && lastWriteErr != Error::TryAgain) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        CHECK(publishObserved.load());
        CHECK(lastWriteErr.isError());
        CHECK(lastWriteErr != Error::TryAgain);

        running = false;
        CHECK(io->close().wait() == Error::Ok);
        delete io;
        serverThread.join();
}

// ---------------------------------------------------------------------------
// In-band parameter-set re-injection (RtmpRepeatParameterSets)
// ---------------------------------------------------------------------------

namespace {

/// @brief Spins up a FakeRtmpServer and a sink built from @p extra-applied config.
///
/// Returns ownership of every piece so the caller can drive a sequence
/// of writes and inspect the server-side observations before unwinding.
struct InjectionScenario {
                std::unique_ptr<TcpServer>     server;
                std::thread                    serverThread;
                std::atomic<bool>              running{true};
                std::atomic<int>               videoSeqHeadersSeen{0};
                std::atomic<int>               keyframesSeen{0};
                std::atomic<int>               keyframesWithSpsSeen{0};
                std::atomic<bool>              publishSeen{false};
                MediaIO                       *io = nullptr;
                MediaIOSink                   *sink = nullptr;

                ~InjectionScenario() {
                        running = false;
                        if (io != nullptr) {
                                io->close().wait();
                                delete io;
                        }
                        if (serverThread.joinable()) serverThread.join();
                }
};

std::unique_ptr<InjectionScenario> openInjectionScenario(
        std::function<void(MediaConfig &)> extra) {
        auto scn = std::make_unique<InjectionScenario>();
        scn->server = std::make_unique<TcpServer>();
        REQUIRE(scn->server->listen(SocketAddress::localhost(0)) == Error::Ok);
        const uint16_t port = scn->server->serverAddress().port();

        scn->serverThread = std::thread([scn = scn.get()]() {
                while (scn->running.load()) {
                        Error err = scn->server->waitForNewConnection(200);
                        if (err == Error::Timeout) continue;
                        if (err.isError()) return;
                        TcpSocket *client = scn->server->nextPendingConnection();
                        if (client == nullptr) continue;
                        FakeRtmpServer fake(client);
                        std::thread fakeThread([&]() { fake.run(); });
                        while (scn->running.load()) {
                                scn->publishSeen.store(fake.publishSeen);
                                scn->videoSeqHeadersSeen.store(fake.videoSequenceHeaders.load());
                                scn->keyframesSeen.store(fake.videoKeyframes.load());
                                scn->keyframesWithSpsSeen.store(fake.videoKeyframesWithSps.load());
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        fake.stop = true;
                        scn->publishSeen.store(fake.publishSeen);
                        scn->videoSeqHeadersSeen.store(fake.videoSequenceHeaders.load());
                        scn->keyframesSeen.store(fake.videoKeyframes.load());
                        scn->keyframesWithSpsSeen.store(fake.videoKeyframesWithSps.load());
                        fakeThread.join();
                        delete client;
                        return;
                }
        });

        MediaConfig cfg;
        cfg.set(MediaConfig::Type, String("Rtmp"));
        cfg.set(MediaConfig::RtmpUrl, localhostUrl(port, "live/myStream"));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::RtmpConnectTimeoutMs, int32_t(5000));
        cfg.set(MediaConfig::RtmpCommandTimeoutMs, int32_t(5000));
        extra(cfg);

        scn->io = MediaIO::create(cfg);
        REQUIRE(scn->io != nullptr);
        REQUIRE(scn->io->open().wait() == Error::Ok);
        REQUIRE(scn->io->isOpen());
        scn->sink = scn->io->sink(0);
        REQUIRE(scn->sink != nullptr);
        return scn;
}

}  // namespace

TEST_CASE("RtmpMediaIO sink: RtmpRepeatParameterSets=true injects SPS on bare IDRs") {
        auto scn = openInjectionScenario([](MediaConfig &cfg) {
                cfg.set(MediaConfig::RtmpRepeatParameterSets, true);
        });

        const ImageDesc videoDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::H264));
        const AudioDesc audioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2);
        Buffer          fullKeyframe = makeFakeAnnexBKeyframe();
        Buffer          bareIdr      = makeIdrOnlyAnnexB();

        // First keyframe — carries SPS/PPS/IDR.  The packetizer
        // populates its parameter-set cache from this frame.
        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, fullKeyframe);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                CompressedAudioPayload::Ptr ca =
                        CompressedAudioPayload::Ptr::create(audioDesc, makeFakeAacFrame(0x30));
                f.addPayload(ca);
                CHECK(scn->sink->writeFrame(f).wait() == Error::Ok);
        }
        // Second keyframe — only the IDR slice, no SPS/PPS.  Without
        // RtmpRepeatParameterSets the packetizer would forward the
        // bare IDR; with the policy active it must prepend the cached
        // parameter sets.
        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, bareIdr);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                CHECK(scn->sink->writeFrame(f).wait() == Error::Ok);
        }

        // Wait for the wire to actually drain to the fixture.  The
        // injection scenario polls the server fixture's counters every
        // 10 ms.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
                if (scn->publishSeen.load()
                    && scn->videoSeqHeadersSeen.load() >= 1
                    && scn->keyframesSeen.load() >= 2) {
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        // Give the inspector loop one more tick to refresh.
        std::this_thread::sleep_for(std::chrono::milliseconds(60));

        CHECK(scn->publishSeen.load());
        CHECK(scn->videoSeqHeadersSeen.load() >= 1);
        CHECK(scn->keyframesSeen.load() >= 2);
        // Both keyframes' AVCC payloads should carry an SPS NAL:
        //   - frame 1: SPS came in via the source Annex-B itself
        //   - frame 2: SPS was injected by the packetizer's cache
        CHECK(scn->keyframesWithSpsSeen.load() >= 2);
}

TEST_CASE("RtmpMediaIO sink: RtmpRepeatParameterSets=false skips SPS injection") {
        auto scn = openInjectionScenario([](MediaConfig &cfg) {
                cfg.set(MediaConfig::RtmpRepeatParameterSets, false);
        });

        const ImageDesc videoDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::H264));
        const AudioDesc audioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2);
        Buffer          fullKeyframe = makeFakeAnnexBKeyframe();
        Buffer          bareIdr      = makeIdrOnlyAnnexB();

        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, fullKeyframe);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                CompressedAudioPayload::Ptr ca =
                        CompressedAudioPayload::Ptr::create(audioDesc, makeFakeAacFrame(0x31));
                f.addPayload(ca);
                CHECK(scn->sink->writeFrame(f).wait() == Error::Ok);
        }
        {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, bareIdr);
                cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                CHECK(scn->sink->writeFrame(f).wait() == Error::Ok);
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
                if (scn->publishSeen.load() && scn->keyframesSeen.load() >= 2) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));

        CHECK(scn->publishSeen.load());
        CHECK(scn->keyframesSeen.load() >= 2);
        // Only the first keyframe carried SPS (in the source Annex-B);
        // the second was a bare IDR and we asked the packetizer not to
        // re-inject parameter sets.
        CHECK(scn->keyframesWithSpsSeen.load() == 1);
}

// ---------------------------------------------------------------------------
// Strand-side video pacing
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Spins up a FakeRtmpServer-driven scenario that opens an
 *        RtmpMediaIO sink with the supplied config and gives the test a
 *        ready-to-write @ref MediaIO plus the listening @c TcpServer's
 *        port so the caller can wire follow-up calls.
 *
 * Lifts the boilerplate out of the three pacing tests below so each
 * focuses on the pacing-specific assertions only.
 */
struct PacingScenario {
                std::unique_ptr<TcpServer>            server;
                std::thread                           serverThread;
                std::atomic<bool>                     running{true};
                MediaIO                              *io = nullptr;
                MediaIOSink                          *sink = nullptr;

                ~PacingScenario() {
                        running = false;
                        if (io != nullptr) {
                                io->close().wait();
                                delete io;
                        }
                        if (serverThread.joinable()) serverThread.join();
                }
};

/// Build a pacing scenario.  Caller provides extra config keys via @p extra.
std::unique_ptr<PacingScenario> openPacingScenario(
        std::function<void(MediaConfig &)> extra) {
        auto scn = std::make_unique<PacingScenario>();
        scn->server = std::make_unique<TcpServer>();
        REQUIRE(scn->server->listen(SocketAddress::localhost(0)) == Error::Ok);
        const uint16_t port = scn->server->serverAddress().port();

        scn->serverThread = std::thread([&srv = *scn->server, &running = scn->running]() {
                while (running.load()) {
                        Error err = srv.waitForNewConnection(200);
                        if (err == Error::Timeout) continue;
                        if (err.isError()) return;
                        TcpSocket *client = srv.nextPendingConnection();
                        if (client == nullptr) continue;
                        FakeRtmpServer fake(client);
                        // Run inline (no extra thread); the fixture
                        // unwinds when stop = true is set on cleanup or
                        // when the connection closes.
                        std::thread fakeThread([&]() { fake.run(); });
                        // Hold the connection open until we tell it to
                        // stop.  Each pacing test only writes a couple
                        // of frames, so the wait is short.
                        while (running.load()) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        fake.stop = true;
                        fakeThread.join();
                        delete client;
                        return;
                }
        });

        MediaConfig cfg;
        cfg.set(MediaConfig::Type, String("Rtmp"));
        cfg.set(MediaConfig::RtmpUrl, localhostUrl(port, "live/myStream"));
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::RtmpConnectTimeoutMs, int32_t(5000));
        cfg.set(MediaConfig::RtmpCommandTimeoutMs, int32_t(5000));
        extra(cfg);

        scn->io = MediaIO::create(cfg);
        REQUIRE(scn->io != nullptr);
        REQUIRE(scn->io->open().wait() == Error::Ok);
        REQUIRE(scn->io->isOpen());
        scn->sink = scn->io->sink(0);
        REQUIRE(scn->sink != nullptr);
        return scn;
}

}  // namespace

TEST_CASE("RtmpMediaIO sink: RtmpVideoPacing=None disables pacing") {
        auto scn = openPacingScenario([](MediaConfig &cfg) {
                cfg.set(MediaConfig::RtmpVideoPacing, RtmpVideoPacing::None);
                cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        });

        MediaIORequest sreq = scn->io->stats();
        CHECK(sreq.wait() == Error::Ok);
        CHECK(sreq.stats().getAs<String>(
                      RtmpMediaIO::StatsPacingClockKind, String()) == String("none"));
        CHECK(sreq.stats().getAs<int64_t>(
                      RtmpMediaIO::StatsPacingTicksOnTime, int64_t(-1)) == 0);
}

TEST_CASE("RtmpMediaIO sink: RtmpVideoPacing=Internal paces against FrameRate") {
        // Use 100 fps so two frames cost ~10 ms of pacing — fast enough
        // for CI but slow enough to verify the gate actually slept.
        auto scn = openPacingScenario([](MediaConfig &cfg) {
                cfg.set(MediaConfig::RtmpVideoPacing, RtmpVideoPacing::Internal);
                cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::RationalType(100u, 1u)));
        });

        // Stats immediately after Open show internal binding, gate
        // not yet armed by a wait() call.
        {
                MediaIORequest sreq = scn->io->stats();
                CHECK(sreq.wait() == Error::Ok);
                CHECK(sreq.stats().getAs<String>(
                              RtmpMediaIO::StatsPacingClockKind, String()) == String("internal"));
        }

        const ImageDesc videoDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::H264));
        const AudioDesc audioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2);
        Buffer          keyframe = makeFakeAnnexBKeyframe();

        // Send 3 frames; at 100 fps that's ~20 ms of accumulated
        // pacing (first frame is anchor and doesn't sleep).
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 3; ++i) {
                Frame f;
                CompressedVideoPayload::Ptr cv =
                        CompressedVideoPayload::Ptr::create(videoDesc, keyframe);
                if (i == 0) cv.modify()->addFlag(MediaPayload::Keyframe);
                f.addPayload(cv);
                CompressedAudioPayload::Ptr ca =
                        CompressedAudioPayload::Ptr::create(audioDesc, makeFakeAacFrame(uint8_t(i)));
                f.addPayload(ca);
                CHECK(scn->sink->writeFrame(f).wait() == Error::Ok);
        }
        const auto elapsed = std::chrono::steady_clock::now() - t0;
        const auto elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // 2 paced waits × 10 ms = 20 ms minimum (with scheduler slop).
        // Allow generous CI headroom on the upper bound.
        CHECK(elapsedMs >= 15);

        MediaIORequest sreq = scn->io->stats();
        CHECK(sreq.wait() == Error::Ok);
        CHECK(sreq.stats().getAs<String>(
                      RtmpMediaIO::StatsPacingClockKind, String()) == String("internal"));
        const int64_t onTime =
                sreq.stats().getAs<int64_t>(RtmpMediaIO::StatsPacingTicksOnTime, int64_t(-1));
        const int64_t late =
                sreq.stats().getAs<int64_t>(RtmpMediaIO::StatsPacingTicksLate, int64_t(-1));
        CHECK((onTime + late) >= 3);
}

TEST_CASE("RtmpMediaIO sink: RtmpVideoPacing=External waits for setClock binding") {
        auto scn = openPacingScenario([](MediaConfig &cfg) {
                cfg.set(MediaConfig::RtmpVideoPacing, RtmpVideoPacing::External);
                cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        });

        // Without a setClock call, the gate stays a no-op even though
        // mode=External.
        {
                MediaIORequest sreq = scn->io->stats();
                CHECK(sreq.wait() == Error::Ok);
                CHECK(sreq.stats().getAs<String>(
                              RtmpMediaIO::StatsPacingClockKind, String()) == String("none"));
        }

        // Bind a wall clock through the port group; the gate should
        // switch to "external".
        MediaIOPortGroup *group = scn->io->portGroup(0);
        REQUIRE(group != nullptr);
        Clock::Ptr extClock = Clock::Ptr::takeOwnership(new WallClock());
        CHECK(group->setClock(extClock).wait() == Error::Ok);

        {
                MediaIORequest sreq = scn->io->stats();
                CHECK(sreq.wait() == Error::Ok);
                CHECK(sreq.stats().getAs<String>(
                              RtmpMediaIO::StatsPacingClockKind, String()) == String("external"));
        }

        // Unbind the external clock; External-mode should fall back to
        // no-op (no Internal fallback).
        CHECK(group->setClock(Clock::Ptr()).wait() == Error::Ok);
        {
                MediaIORequest sreq = scn->io->stats();
                CHECK(sreq.wait() == Error::Ok);
                CHECK(sreq.stats().getAs<String>(
                              RtmpMediaIO::StatsPacingClockKind, String()) == String("none"));
        }
}
