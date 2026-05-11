/**
 * @file      rtmpclient.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <promeki/amf0.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/flvtag.h>
#include <promeki/result.h>
#include <promeki/rtmpchunkstream.h>
#include <promeki/rtmpclient.h>
#include <promeki/rtmphandshake.h>
#include <promeki/rtmpmessage.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/tcpserver.h>
#include <promeki/tcpsocket.h>
#include <promeki/url.h>

using namespace promeki;

namespace {

/**
 * @brief Drives the server-side of the RTMP exchange against a
 *        single accepted TcpSocket.
 *
 * Handles handshake, connect, releaseStream / FCPublish (silent
 * acks), createStream, publish, and then captures audio + video
 * messages until @c stop is set.
 */
class FakeRtmpServer {
        public:
                explicit FakeRtmpServer(TcpSocket *sock) : _sock(sock) {}

                bool                          publishSeen = false;
                bool                          playSeen = false;
                std::atomic<int>              audioReceived{0};
                std::atomic<int>              videoReceived{0};
                std::atomic<int>              metadataReceived{0};
                std::atomic<bool>             stop{false};

                void run() {
                        // Drive the server-side handshake until Done.
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

                        // After handshake, drive the chunk-stream + command flow.
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
                                } else if (msg.type == RtmpMessage::AudioMessage) {
                                        ++audioReceived;
                                } else if (msg.type == RtmpMessage::DataMessageAmf0) {
                                        ++metadataReceived;
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
                                return;  // silently accept
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
                                playSeen = true;
                                return;
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

}  // namespace

// ---------------------------------------------------------------------------
// URL handling
// ---------------------------------------------------------------------------

TEST_CASE("RtmpClient: open rejects unsupported URL schemes") {
        RtmpClient c;
        Url        u("http://example.com/live/key");
        CHECK(c.open(u, {}, 100) == Error::InvalidArgument);
}

TEST_CASE("RtmpClient: open rejects an empty host") {
        RtmpClient c;
        Url        u("rtmp:///live/key");
        CHECK(c.open(u, {}, 100) == Error::InvalidArgument);
}

TEST_CASE("RtmpClient::splitPath maps URL path to app and streamKey") {
        String app, key;

        // Empty path → both empty.
        RtmpClient::splitPath(Url("rtmp://host"), app, key);
        CHECK(app == String());
        CHECK(key == String());

        // Two-segment path → app + streamKey.
        RtmpClient::splitPath(Url("rtmp://host/live/myStream"), app, key);
        CHECK(app == String("live"));
        CHECK(key == String("myStream"));

        // Multi-segment path → everything before the last '/' is the app.
        RtmpClient::splitPath(Url("rtmp://host/a/b/c/key"), app, key);
        CHECK(app == String("a/b/c"));
        CHECK(key == String("key"));

        // Single-segment path → the segment is the app (not the stream
        // key).  Servers like YouTube reject AMF0 `connect` with an
        // empty `app`, and callers supply the stream key via
        // MediaConfig::RtmpStreamKey when using this form.
        RtmpClient::splitPath(Url("rtmp://a.rtmp.youtube.com/live2"), app, key);
        CHECK(app == String("live2"));
        CHECK(key == String());

        // Trailing slash → app filled, stream key empty.
        RtmpClient::splitPath(Url("rtmp://host/live/"), app, key);
        CHECK(app == String("live"));
        CHECK(key == String());
}

// ---------------------------------------------------------------------------
// End-to-end loopback round-trip
// ---------------------------------------------------------------------------

TEST_CASE("RtmpClient: end-to-end publish round-trip via a local TCP fixture") {
        TcpServer server;
        REQUIRE(server.listen(SocketAddress::localhost(0)) == Error::Ok);
        const uint16_t port = server.serverAddress().port();

        std::atomic<bool>         running{true};
        std::atomic<bool>         publishObserved{false};
        std::atomic<int>          videoReceived{0};
        std::atomic<int>          audioReceived{0};
        std::thread               serverThread([&]() {
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
                                   || fake.videoReceived.load() < 3
                                   || fake.audioReceived.load() < 3)) {
                                if (std::chrono::steady_clock::now() >= deadline) break;
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        publishObserved.store(fake.publishSeen);
                        videoReceived.store(fake.videoReceived.load());
                        audioReceived.store(fake.audioReceived.load());
                        fake.stop = true;
                        fakeThread.join();
                        delete client;
                        running = false;
                }
        });

        RtmpClient client;
        Url        url = localhostUrl(port, "live/myStream");
        Error      err = client.open(url, {}, 5000);
        REQUIRE(err == Error::Ok);
        CHECK(client.isOpen());
        CHECK(client.app() == String("live"));
        CHECK(client.streamKey() == String("myStream"));

        CHECK(client.publish(String(), "live", 5000) == Error::Ok);
        CHECK(client.streamId() == 1);

        for (int i = 0; i < 3; i++) {
                FlvVideoTag v;
                v.frameType = FlvVideoTag::Keyframe;
                v.codec = FlvVideoTag::Avc;
                v.packetType = FlvVideoTag::Nalu;
                v.compositionTimeOffsetMs = 0;
                v.data = Buffer(8);
                v.data.setSize(8);
                std::memset(v.data.data(), static_cast<uint8_t>(0x10 + i), 8);
                CHECK(client.sendVideo(v, 40 * i) == Error::Ok);

                FlvAudioTag a;
                a.format        = FlvAudioTag::Aac;
                a.rate          = FlvAudioTag::Rate44000;
                a.size          = FlvAudioTag::Bits16;
                a.channelType   = FlvAudioTag::Stereo;
                a.aacPacketType = FlvAudioTag::Raw;
                a.data = Buffer(6);
                a.data.setSize(6);
                std::memset(a.data.data(), static_cast<uint8_t>(0x20 + i), 6);
                CHECK(client.sendAudio(a, 23 * i) == Error::Ok);
        }

        // Wait for the writer thread to flush.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (client.videoMessagesSent() < 3 || client.audioMessagesSent() < 3) {
                if (std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(client.videoMessagesSent() >= 3);
        CHECK(client.audioMessagesSent() >= 3);
        CHECK(client.bytesSent() > 0);

        running = false;
        client.close();
        serverThread.join();
        CHECK(publishObserved.load());
        CHECK(videoReceived.load() >= 3);
        CHECK(audioReceived.load() >= 3);
}

TEST_CASE("RtmpClient: open fails cleanly on a refused connection") {
        // Bind a server, then close it immediately to free the port — any
        // subsequent connect to that port should be refused.
        uint16_t port = 0;
        {
                TcpServer probe;
                REQUIRE(probe.listen(SocketAddress::localhost(0)) == Error::Ok);
                port = probe.serverAddress().port();
                probe.close();
        }

        RtmpClient client;
        Url        url = localhostUrl(port, "live/x");
        Error      err = client.open(url, {}, 500);
        CHECK(err.isError());
        CHECK(!client.isOpen());
}

// ---------------------------------------------------------------------------
// Sub-operations on unopened client
// ---------------------------------------------------------------------------

TEST_CASE("RtmpClient: sendVideo / sendAudio on an unopened client returns Error::Invalid") {
        RtmpClient  c;
        FlvVideoTag v;
        v.frameType = FlvVideoTag::Keyframe;
        v.codec = FlvVideoTag::Avc;
        v.data = Buffer(4);
        v.data.setSize(4);
        CHECK(c.sendVideo(v, 0) == Error::Invalid);

        FlvAudioTag a;
        a.format        = FlvAudioTag::Aac;
        a.rate          = FlvAudioTag::Rate44000;
        a.size          = FlvAudioTag::Bits16;
        a.channelType   = FlvAudioTag::Stereo;
        a.aacPacketType = FlvAudioTag::Raw;
        a.data = Buffer(2);
        a.data.setSize(2);
        CHECK(c.sendAudio(a, 0) == Error::Invalid);
}

TEST_CASE("RtmpClient: publish on an unopened client returns Error::Invalid") {
        RtmpClient c;
        CHECK(c.publish("anything", "live", 100) == Error::Invalid);
}

TEST_CASE("RtmpClient: takeVideo on an idle client returns Error::Invalid") {
        RtmpClient c;
        auto r = c.takeVideo(50);
        CHECK(r.second() == Error::Invalid);
}

TEST_CASE("RtmpClient: close on a never-opened client is a no-op") {
        RtmpClient c;
        CHECK(c.close() == Error::Ok);
        CHECK(!c.isOpen());
}

// ---------------------------------------------------------------------------
// onStatus correlation via expectedMsid (txnId == 0 path — RTMP §7.2.2)
// ---------------------------------------------------------------------------

/**
 * @brief Fake server that replies to publish with onStatus txnId=0.
 *
 * Real-world RTMP servers (YouTube, Twitch, nginx-rtmp) send onStatus
 * with txnId=0 per the spec.  This exercises the expectedMsid scan
 * path in RtmpSession::handleInboundCommand.
 */
class FakeRtmpServerZeroTxn {
        public:
                explicit FakeRtmpServerZeroTxn(TcpSocket *sock) : _sock(sock) {}

                bool              publishSeen = false;
                std::atomic<bool> stop{false};

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
                                if (msg.type == RtmpMessage::CommandMessageAmf0)
                                        handleCommand(chunk, msg);
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
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("_result");
                                w.writeNumber(txn);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code",  Amf0Value(String("NetConnection.Connect.Success")));
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
                        if (cmd == "releaseStream" || cmd == "FCPublish") return;
                        if (cmd == "publish") {
                                // Send onStatus with txnId=0 (RTMP §7.2.2 — real servers
                                // do this; correlation must fall back to expectedMsid scan).
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("onStatus");
                                w.writeNumber(0.0);   // <-- the critical zero txnId
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code",  Amf0Value(String("NetStream.Publish.Start")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = msg.streamId;   // msid must match
                                reply.chunkStreamId = 5;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                publishSeen = true;
                                return;
                        }
                }

                TcpSocket *_sock;
};

TEST_CASE("RtmpClient: publish succeeds when server sends onStatus with txnId=0 (RTMP spec path)") {
        // This exercises RtmpSession::handleInboundCommand's expectedMsid
        // scan — the code path required for YouTube / Twitch / nginx-rtmp.
        TcpServer server;
        REQUIRE(server.listen(SocketAddress::localhost(0)) == Error::Ok);
        const uint16_t port = server.serverAddress().port();

        std::atomic<bool> running{true};
        std::atomic<bool> publishObserved{false};
        std::thread       serverThread([&]() {
                while (running.load()) {
                        Error err = server.waitForNewConnection(200);
                        if (err == Error::Timeout) continue;
                        if (err.isError()) return;
                        TcpSocket *client = server.nextPendingConnection();
                        if (client == nullptr) continue;
                        FakeRtmpServerZeroTxn fake(client);
                        std::thread fakeThread([&]() { fake.run(); });

                        auto deadline = std::chrono::steady_clock::now()
                                        + std::chrono::seconds(5);
                        while (running.load() && !fake.publishSeen) {
                                if (std::chrono::steady_clock::now() >= deadline) break;
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        publishObserved.store(fake.publishSeen);
                        fake.stop = true;
                        fakeThread.join();
                        delete client;
                        running = false;
                }
        });

        RtmpClient client;
        Url        url = localhostUrl(port, "live2/myKey");
        Error      err = client.open(url, {}, 5000);
        REQUIRE(err == Error::Ok);
        Error publishErr = client.publish(String(), "live", 5000);
        CHECK(publishErr == Error::Ok);

        running = false;
        client.close();
        serverThread.join();
        CHECK(publishObserved.load());
}
