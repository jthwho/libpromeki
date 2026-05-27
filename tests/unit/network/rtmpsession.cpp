/**
 * @file      rtmpsession.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include <promeki/amf0.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/iodevice.h>
#include <promeki/result.h>
#include <promeki/rtmpchunkstream.h>
#include <promeki/rtmpmessage.h>
#include <promeki/rtmpsession.h>

using namespace promeki;

namespace {

/**
 * @brief Bidirectional in-process socket pair for session tests.
 *
 * Two @c TestSocket instances are wired together via @ref bind.
 * Each holds a private inbound byte queue; @c write appends to the
 * peer's queue, @c read drains the local queue.  Thread-safe enough
 * that a client driving the protocol on one thread and a fake-server
 * worker on another don't trample each other.
 */
class TestSocket : public IODevice {
        public:
                static void bind(TestSocket &a, TestSocket &b) {
                        a._peer = &b;
                        b._peer = &a;
                }

                Error open(OpenMode) override {
                        _open = true;
                        return Error::Ok;
                }

                Error close() override {
                        _open = false;
                        return Error::Ok;
                }

                bool isOpen() const override { return _open; }
                bool isSequential() const override { return true; }

                int64_t bytesAvailable() const override {
                        std::lock_guard<std::mutex> lk(_mutex);
                        return static_cast<int64_t>(_rx.size());
                }

                int64_t read(void *data, int64_t maxSize) override {
                        std::lock_guard<std::mutex> lk(_mutex);
                        int64_t have = static_cast<int64_t>(_rx.size());
                        if (have <= 0) return 0;
                        int64_t take = (maxSize < have) ? maxSize : have;
                        for (int64_t i = 0; i < take; i++) {
                                static_cast<uint8_t *>(data)[i] = _rx.front();
                                _rx.pop_front();
                        }
                        return take;
                }

                int64_t write(const void *data, int64_t maxSize) override {
                        TestSocket *peer = _peer;
                        if (peer == nullptr) return -1;
                        std::lock_guard<std::mutex> lk(peer->_mutex);
                        for (int64_t i = 0; i < maxSize; i++) {
                                peer->_rx.push_back(static_cast<const uint8_t *>(data)[i]);
                        }
                        return maxSize;
                }

                bool waitForReadyRead(unsigned int timeoutMs) override {
                        auto deadline = std::chrono::steady_clock::now()
                                        + std::chrono::milliseconds(timeoutMs);
                        for (;;) {
                                if (bytesAvailable() > 0) return true;
                                if (timeoutMs == 0) return false;
                                if (std::chrono::steady_clock::now() >= deadline) return false;
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                }

        private:
                mutable std::mutex     _mutex;
                std::deque<uint8_t>    _rx;
                TestSocket            *_peer = nullptr;
                bool                   _open = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// onStatusToError mapping
// ---------------------------------------------------------------------------

TEST_CASE("RtmpSession::onStatusToError: NetConnection status codes") {
        CHECK(RtmpSession::onStatusToError("NetConnection.Connect.Success") == Error::Ok);
        CHECK(RtmpSession::onStatusToError("NetConnection.Connect.Rejected") == Error::PermissionDenied);
        CHECK(RtmpSession::onStatusToError("NetConnection.Connect.InvalidApp") == Error::InvalidArgument);
        CHECK(RtmpSession::onStatusToError("NetConnection.Connect.Failed") == Error::ConnectionRefused);
        CHECK(RtmpSession::onStatusToError("NetConnection.Connect.AppShutdown") == Error::Cancelled);
        CHECK(RtmpSession::onStatusToError("NetConnection.Connect.Closed") == Error::Cancelled);
}

TEST_CASE("RtmpSession::onStatusToError: NetStream.Publish codes") {
        CHECK(RtmpSession::onStatusToError("NetStream.Publish.Start") == Error::Ok);
        CHECK(RtmpSession::onStatusToError("NetStream.Publish.BadName") == Error::Exists);
        CHECK(RtmpSession::onStatusToError("NetStream.Publish.Denied") == Error::PermissionDenied);
}

TEST_CASE("RtmpSession::onStatusToError: NetStream.Play codes") {
        CHECK(RtmpSession::onStatusToError("NetStream.Play.Start") == Error::Ok);
        CHECK(RtmpSession::onStatusToError("NetStream.Play.Reset") == Error::Ok);
        CHECK(RtmpSession::onStatusToError("NetStream.Play.StreamNotFound") == Error::NotFound);
        CHECK(RtmpSession::onStatusToError("NetStream.Play.Failed") == Error::IOError);
}

TEST_CASE("RtmpSession::onStatusToError: NetStream.Authenticate.UsherToken") {
        CHECK(RtmpSession::onStatusToError("NetStream.Authenticate.UsherToken")
              == Error::AuthenticationRequired);
}

TEST_CASE("RtmpSession::onStatusToError: unknown codes map to ProtocolError") {
        CHECK(RtmpSession::onStatusToError("Some.Unknown.Code") == Error::ProtocolError);
        CHECK(RtmpSession::onStatusToError("") == Error::ProtocolError);
}

// ---------------------------------------------------------------------------
// attach / null-device guards
// ---------------------------------------------------------------------------

TEST_CASE("RtmpSession: sendMessage on a session with no device returns Error::Invalid") {
        RtmpSession session(RtmpRole::Client);
        RtmpMessage msg;
        msg.type = RtmpMessage::AudioMessage;
        msg.streamId = 1;
        CHECK(session.sendMessage(msg) == Error::Invalid);
}

TEST_CASE("RtmpSession: readMessage on a session with no device returns Error::Invalid") {
        RtmpSession session(RtmpRole::Client);
        auto r = session.readMessage(10);
        CHECK(r.second() == Error::Invalid);
}

TEST_CASE("RtmpSession: attach replaces the device + builds a chunk stream") {
        TestSocket a, b;
        TestSocket::bind(a, b);
        REQUIRE(a.open(IODevice::ReadWrite) == Error::Ok);

        RtmpSession session(RtmpRole::Client);
        CHECK(session.device() == nullptr);
        CHECK(session.attach(&a) == Error::Ok);
        CHECK(session.device() == &a);
        CHECK(session.chunkStream() != nullptr);

        CHECK(session.attach(nullptr) == Error::Ok);
        CHECK(session.chunkStream() == nullptr);
}

// ---------------------------------------------------------------------------
// PingRequest → PingResponse echo
// ---------------------------------------------------------------------------

TEST_CASE("RtmpSession: PingRequest gets a PingResponse echo back") {
        TestSocket clientSock, serverSock;
        TestSocket::bind(clientSock, serverSock);
        REQUIRE(clientSock.open(IODevice::ReadWrite) == Error::Ok);
        REQUIRE(serverSock.open(IODevice::ReadWrite) == Error::Ok);

        RtmpSession session(RtmpRole::Client);
        REQUIRE(session.attach(&clientSock) == Error::Ok);

        // Build a UserControl PingRequest manually via a separate
        // chunk stream pointed at the server's end.
        RtmpChunkStream serverChunk(&serverSock);

        RtmpMessage ping;
        ping.type = RtmpMessage::UserControl;
        ping.streamId = 0;
        ping.chunkStreamId = 2;
        ping.timestamp = 0;
        ping.payload = Buffer(6);
        ping.payload.setSize(6);
        auto *p = static_cast<uint8_t *>(ping.payload.data());
        p[0] = 0x00; p[1] = 0x06;            // event type = PingRequest
        p[2] = 0xDE; p[3] = 0xAD; p[4] = 0xBE; p[5] = 0xEF;
        REQUIRE(serverChunk.writeMessage(ping) == Error::Ok);

        // Client drains the PingRequest — the session must respond
        // automatically before returning.
        auto got = session.readMessage(500);
        REQUIRE(got.second().isOk());
        CHECK(got.first().type == RtmpMessage::UserControl);

        auto resp = serverChunk.readMessage(500);
        REQUIRE(resp.second().isOk());
        CHECK(resp.first().type == RtmpMessage::UserControl);
        REQUIRE(resp.first().payload.size() >= 6);
        const auto *rp = static_cast<const uint8_t *>(resp.first().payload.data());
        CHECK(rp[0] == 0x00);
        CHECK(rp[1] == 0x07);  // PingResponse
        CHECK(rp[2] == 0xDE);
        CHECK(rp[3] == 0xAD);
        CHECK(rp[4] == 0xBE);
        CHECK(rp[5] == 0xEF);
}

// ---------------------------------------------------------------------------
// connect flow: client + thread-backed fake server
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Tiny single-shot fake server that handles one connect +
 *        one createStream + one publish exchange.
 */
struct FakeServer {
                IODevice          *device;
                RtmpChunkStream    chunk;
                Amf0Value          lastConnectCmd;
                uint32_t           allocatedStreamId = 1;
                bool               publishSeen = false;
                bool               playSeen = false;
                String             lastStreamKey;
                String             lastPublishMode;
                std::atomic<bool>  stop{false};

                explicit FakeServer(IODevice *d) : device(d), chunk(d) {}

                /**
                 * @brief Replies to one inbound command message.  Returns
                 *        @c true if a `publish` or `play` was just acked
                 *        and the test loop should exit.
                 */
                bool handleOne() {
                        auto got = chunk.readMessage(100);
                        if (got.second().isError()) return false;
                        if (got.first().type != RtmpMessage::CommandMessageAmf0) return false;
                        auto parsed = Amf0Reader::read(
                                static_cast<const uint8_t *>(got.first().payload.data()),
                                got.first().payload.size());
                        if (parsed.second().isError()) return false;
                        const auto &vals = parsed.first();
                        if (vals.size() < 2 || !vals[0].isString() || !vals[1].isNumber()) return false;
                        String command = vals[0].asString();
                        double txnId   = vals[1].asNumber();

                        if (command == "connect") {
                                if (vals.size() >= 3) lastConnectCmd = vals[2];
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
                                {
                                        RtmpMessage m;
                                        m.type = RtmpMessage::UserControl;
                                        m.streamId = 0;
                                        m.chunkStreamId = 2;
                                        m.payload = Buffer(6);
                                        m.payload.setSize(6);
                                        std::memset(m.payload.data(), 0, 6);
                                        chunk.writeMessage(m);
                                }
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("_result");
                                w.writeNumber(txnId);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code", Amf0Value(String("NetConnection.Connect.Success")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = 0;
                                reply.chunkStreamId = 3;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                return false;
                        }
                        if (command == "createStream") {
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("_result");
                                w.writeNumber(txnId);
                                w.writeNull();
                                w.writeNumber(static_cast<double>(allocatedStreamId));
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = 0;
                                reply.chunkStreamId = 3;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                return false;
                        }
                        if (command == "releaseStream" || command == "FCPublish"
                            || command == "FCUnpublish" || command == "FCSubscribe") {
                                return false;
                        }
                        if (command == "publish") {
                                if (vals.size() >= 4 && vals[3].isString()) {
                                        lastStreamKey = vals[3].asString();
                                }
                                if (vals.size() >= 5 && vals[4].isString()) {
                                        lastPublishMode = vals[4].asString();
                                }
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("onStatus");
                                w.writeNumber(txnId);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code", Amf0Value(String("NetStream.Publish.Start")));
                                info.setField("description", Amf0Value(String("Publishing")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = got.first().streamId;
                                reply.chunkStreamId = 5;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                publishSeen = true;
                                return true;
                        }
                        if (command == "play") {
                                if (vals.size() >= 4 && vals[3].isString()) {
                                        lastStreamKey = vals[3].asString();
                                }
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("onStatus");
                                w.writeNumber(txnId);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("status")));
                                info.setField("code", Amf0Value(String("NetStream.Play.Start")));
                                info.setField("description", Amf0Value(String("Playing")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = got.first().streamId;
                                reply.chunkStreamId = 5;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                playSeen = true;
                                return true;
                        }
                        return false;
                }
};

}  // namespace

TEST_CASE("RtmpSession: connect + createStream + publish round-trips via a fake server") {
        TestSocket clientSock, serverSock;
        TestSocket::bind(clientSock, serverSock);
        REQUIRE(clientSock.open(IODevice::ReadWrite) == Error::Ok);
        REQUIRE(serverSock.open(IODevice::ReadWrite) == Error::Ok);

        RtmpSession client(RtmpRole::Client);
        REQUIRE(client.attach(&clientSock) == Error::Ok);

        FakeServer        server(&serverSock);
        std::atomic<bool> serverDone{false};
        std::thread serverThread([&]() {
                while (!server.stop.load() && !serverDone.load()) {
                        if (server.handleOne()) serverDone.store(true);
                }
        });

        RtmpConnectOptions opts;
        opts.app = "live";
        opts.tcUrl = "rtmp://localhost/live";
        CHECK(client.connect(opts, 5000) == Error::Ok);

        auto sid = client.createStream(5000);
        CHECK(sid.second().isOk());
        CHECK(sid.first() == 1);

        CHECK(client.publish(sid.first(), "myStreamKey", "live", 5000) == Error::Ok);

        server.stop.store(true);
        serverThread.join();

        CHECK(server.publishSeen == true);
        CHECK(server.lastStreamKey == String("myStreamKey"));
        CHECK(server.lastPublishMode == String("live"));
        REQUIRE(server.lastConnectCmd.isObject());
        REQUIRE(server.lastConnectCmd.contains("app"));
        CHECK(server.lastConnectCmd.find("app")->asString() == String("live"));
        CHECK(server.lastConnectCmd.find("tcUrl")->asString() == String("rtmp://localhost/live"));
}

TEST_CASE("RtmpSession: play succeeds end-to-end against the fake server") {
        TestSocket clientSock, serverSock;
        TestSocket::bind(clientSock, serverSock);
        REQUIRE(clientSock.open(IODevice::ReadWrite) == Error::Ok);
        REQUIRE(serverSock.open(IODevice::ReadWrite) == Error::Ok);

        RtmpSession client(RtmpRole::Client);
        REQUIRE(client.attach(&clientSock) == Error::Ok);

        FakeServer        server(&serverSock);
        std::atomic<bool> serverDone{false};
        std::thread serverThread([&]() {
                while (!server.stop.load() && !serverDone.load()) {
                        if (server.handleOne()) serverDone.store(true);
                }
        });

        RtmpConnectOptions opts;
        opts.app = "live";
        opts.tcUrl = "rtmp://localhost/live";
        CHECK(client.connect(opts, 5000) == Error::Ok);

        auto sid = client.createStream(5000);
        CHECK(sid.second().isOk());

        CHECK(client.play(sid.first(), "playKey", -2.0, -1.0, 5000) == Error::Ok);

        server.stop.store(true);
        serverThread.join();

        CHECK(server.playSeen == true);
        CHECK(server.lastStreamKey == String("playKey"));
}

TEST_CASE("RtmpSession: publish gets PermissionDenied on NetStream.Publish.Denied") {
        TestSocket clientSock, serverSock;
        TestSocket::bind(clientSock, serverSock);
        REQUIRE(clientSock.open(IODevice::ReadWrite) == Error::Ok);
        REQUIRE(serverSock.open(IODevice::ReadWrite) == Error::Ok);

        RtmpSession client(RtmpRole::Client);
        REQUIRE(client.attach(&clientSock) == Error::Ok);

        std::atomic<bool> stop{false};
        std::thread serverThread([&]() {
                RtmpChunkStream chunk(&serverSock);
                bool done = false;
                while (!stop.load() && !done) {
                        auto got = chunk.readMessage(100);
                        if (got.second().isError()) continue;
                        if (got.first().type != RtmpMessage::CommandMessageAmf0) continue;
                        auto parsed = Amf0Reader::read(
                                static_cast<const uint8_t *>(got.first().payload.data()),
                                got.first().payload.size());
                        if (parsed.second().isError()) continue;
                        const auto &vals = parsed.first();
                        if (vals.size() < 2) continue;
                        String cmd = vals[0].asString();
                        double txn = vals[1].asNumber();
                        if (cmd == "connect") {
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
                        } else if (cmd == "createStream") {
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
                        } else if (cmd == "publish") {
                                Buffer body;
                                Amf0Writer w(body);
                                w.writeString("onStatus");
                                w.writeNumber(txn);
                                w.writeNull();
                                Amf0Value info = Amf0Value::object();
                                info.setField("level", Amf0Value(String("error")));
                                info.setField("code", Amf0Value(String("NetStream.Publish.Denied")));
                                w.writeValue(info);
                                RtmpMessage reply;
                                reply.type = RtmpMessage::CommandMessageAmf0;
                                reply.streamId = got.first().streamId;
                                reply.chunkStreamId = 5;
                                reply.payload = body;
                                chunk.writeMessage(reply);
                                done = true;
                        }
                }
        });

        RtmpConnectOptions opts;
        opts.app = "live";
        opts.tcUrl = "rtmp://localhost/live";
        CHECK(client.connect(opts, 5000) == Error::Ok);
        auto sid = client.createStream(5000);
        REQUIRE(sid.second().isOk());

        Error err = client.publish(sid.first(), "denied", "live", 5000);
        CHECK(err == Error::PermissionDenied);

        stop.store(true);
        serverThread.join();
}

TEST_CASE("RtmpSession: connect times out when the server never replies") {
        TestSocket clientSock, serverSock;
        TestSocket::bind(clientSock, serverSock);
        REQUIRE(clientSock.open(IODevice::ReadWrite) == Error::Ok);
        REQUIRE(serverSock.open(IODevice::ReadWrite) == Error::Ok);

        RtmpSession client(RtmpRole::Client);
        REQUIRE(client.attach(&clientSock) == Error::Ok);

        RtmpConnectOptions opts;
        opts.app = "live";
        opts.tcUrl = "rtmp://localhost/live";
        Error err = client.connect(opts, 150);
        CHECK(err == Error::Timeout);
}
