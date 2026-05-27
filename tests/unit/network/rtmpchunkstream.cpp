/**
 * @file      rtmpchunkstream.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstring>

#include <promeki/buffer.h>
#include <promeki/iodevice.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/rtmpchunkstream.h>
#include <promeki/rtmpmessage.h>

using namespace promeki;

namespace {

/**
 * @brief Tiny sequential pipe-like IODevice for chunk-stream tests.
 *
 * Bytes written via @ref write are appended to an internal queue and
 * become readable via @ref read.  No threading, no socket — purely a
 * byte FIFO.  Mirrors the byte-shape contract any real socket would
 * satisfy for the chunk-stream layer.
 */
class PipeDevice : public IODevice {
        public:
                PipeDevice() = default;

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
                        return static_cast<int64_t>(_q.size() - _readPos);
                }

                int64_t read(void *data, int64_t maxSize) override {
                        int64_t have = bytesAvailable();
                        if (have <= 0) return 0;
                        int64_t take = (maxSize < have) ? maxSize : have;
                        std::memcpy(data, _q.data() + _readPos, take);
                        _readPos += static_cast<size_t>(take);
                        if (_readPos > 1024 && _readPos == _q.size()) {
                                _q.clear();
                                _readPos = 0;
                        }
                        return take;
                }

                int64_t write(const void *data, int64_t maxSize) override {
                        size_t before = _q.size();
                        _q.resize(before + static_cast<size_t>(maxSize));
                        std::memcpy(_q.data() + before, data, static_cast<size_t>(maxSize));
                        return maxSize;
                }

                bool waitForReadyRead(unsigned int /*timeoutMs*/) override {
                        return bytesAvailable() > 0;
                }

        private:
                List<uint8_t> _q;
                size_t        _readPos = 0;
                bool          _open = false;
};

/** @brief Fills @p sz bytes with a deterministic test pattern. */
Buffer makePayload(uint8_t seed, size_t sz) {
        Buffer b(sz);
        b.setSize(sz);
        auto *p = static_cast<uint8_t *>(b.data());
        for (size_t i = 0; i < sz; i++) p[i] = static_cast<uint8_t>(seed + (i * 31 + 7));
        return b;
}

bool buffersEqual(const Buffer &a, const Buffer &b) {
        if (a.size() != b.size()) return false;
        if (a.size() == 0) return true;
        return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Round-trip basics
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: single-chunk video message round-trips") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = 1;
        msg.timestamp = 100;
        msg.payload = makePayload(0xA0, 64);

        CHECK(writer.writeMessage(msg) == Error::Ok);

        // Pipe transfer.
        uint8_t buf[8192];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().type == RtmpMessage::VideoMessage);
        CHECK(got.first().streamId == 1);
        CHECK(got.first().timestamp == 100);
        CHECK(got.first().chunkStreamId == 6);  // default for VideoMessage
        CHECK(buffersEqual(got.first().payload, msg.payload));
}

TEST_CASE("RtmpChunkStream: oversize message fragments across chunks and reassembles") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        // Default chunk size is 128.  Payload of 5 KB forces ~40 chunks.
        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = 1;
        msg.timestamp = 1000;
        msg.payload = makePayload(0x5A, 5 * 1024);

        CHECK(writer.writeMessage(msg) == Error::Ok);

        // Transfer all bytes.
        uint8_t buf[16384];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().payload.size() == 5 * 1024);
        CHECK(buffersEqual(got.first().payload, msg.payload));
}

// ---------------------------------------------------------------------------
// Header-type compression (fmt 0 → 1 → 2 → 3)
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: subsequent same-stream messages use type-1 / 2 / 3 headers") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage a;
        a.type = RtmpMessage::AudioMessage;
        a.streamId = 1;
        a.timestamp = 0;
        a.payload = makePayload(0x11, 32);
        CHECK(writer.writeMessage(a) == Error::Ok);

        // Second — same length + type + streamId, monotonic ts → type 1.
        RtmpMessage b = a;
        b.timestamp = 40;
        CHECK(writer.writeMessage(b) == Error::Ok);

        // Third — same delta as before → type 2.
        RtmpMessage c = a;
        c.timestamp = 80;
        CHECK(writer.writeMessage(c) == Error::Ok);

        // Fourth — same delta and length: type 3 candidate.
        RtmpMessage d = a;
        d.timestamp = 120;
        CHECK(writer.writeMessage(d) == Error::Ok);

        uint8_t buf[16384];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        for (int i = 0; i < 4; i++) {
                Result<RtmpMessage> got = reader.readMessage();
                REQUIRE(got.second().isOk());
                CHECK(got.first().type == RtmpMessage::AudioMessage);
                CHECK(got.first().streamId == 1);
                CHECK(got.first().timestamp == static_cast<uint32_t>(40 * i));
                CHECK(buffersEqual(got.first().payload, a.payload));
        }
}

// ---------------------------------------------------------------------------
// Extended timestamp
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: extended timestamp (≥ 0xFFFFFF) round-trips") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = 7;
        msg.timestamp = 0xFFFFFF;  // exact marker — triggers extended-ts encoding
        msg.payload = makePayload(0x77, 256);  // multi-chunk to exercise continuation
        CHECK(writer.writeMessage(msg) == Error::Ok);

        RtmpMessage bigTs;
        bigTs.type = RtmpMessage::VideoMessage;
        bigTs.streamId = 7;
        bigTs.timestamp = 0x12345678;  // well above the 24-bit field
        bigTs.payload = makePayload(0x88, 256);
        CHECK(writer.writeMessage(bigTs) == Error::Ok);

        uint8_t buf[8192];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().timestamp == 0xFFFFFF);
        CHECK(buffersEqual(got.first().payload, msg.payload));

        Result<RtmpMessage> got2 = reader.readMessage();
        REQUIRE(got2.second().isOk());
        CHECK(got2.first().timestamp == 0x12345678);
        CHECK(buffersEqual(got2.first().payload, bigTs.payload));
}

// ---------------------------------------------------------------------------
// Chunk-size raise mid-stream
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: SetChunkSize raises peer + local chunk size") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        CHECK(writer.setLocalChunkSize(4096) == Error::Ok);
        CHECK(writer.localChunkSize() == 4096);

        RtmpMessage msg;
        msg.type = RtmpMessage::AudioMessage;
        msg.streamId = 1;
        msg.timestamp = 50;
        msg.payload = makePayload(0xC3, 6 * 1024);
        CHECK(writer.writeMessage(msg) == Error::Ok);

        uint8_t buf[16384];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        // First message: SetChunkSize control.  The chunk layer
        // applies it to peerChunkSize before returning.
        Result<RtmpMessage> ctrl = reader.readMessage();
        REQUIRE(ctrl.second().isOk());
        CHECK(ctrl.first().type == RtmpMessage::SetChunkSize);
        CHECK(reader.peerChunkSize() == 4096);

        // Second: the audio body reassembles under the new chunk size.
        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().type == RtmpMessage::AudioMessage);
        CHECK(got.first().payload.size() == 6 * 1024);
        CHECK(buffersEqual(got.first().payload, msg.payload));
}

TEST_CASE("RtmpChunkStream: setLocalChunkSize rejects out-of-range values") {
        PipeDevice tx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        CHECK(writer.setLocalChunkSize(64) == Error::InvalidArgument);
        CHECK(writer.setLocalChunkSize(70000) == Error::InvalidArgument);
        CHECK(writer.localChunkSize() == RtmpChunkStream::DefaultChunkSize);
}

// ---------------------------------------------------------------------------
// Interleaved CS-ids
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: independent CS-ids reassemble interleaved messages") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage v;
        v.type = RtmpMessage::VideoMessage;
        v.streamId = 1;
        v.timestamp = 100;
        v.payload = makePayload(0xAA, 200);
        CHECK(writer.writeMessage(v) == Error::Ok);

        RtmpMessage a;
        a.type = RtmpMessage::AudioMessage;
        a.streamId = 1;
        a.timestamp = 110;
        a.payload = makePayload(0xBB, 200);
        CHECK(writer.writeMessage(a) == Error::Ok);

        uint8_t buf[8192];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> g1 = reader.readMessage();
        REQUIRE(g1.second().isOk());
        CHECK(g1.first().chunkStreamId == 6);
        CHECK(g1.first().type == RtmpMessage::VideoMessage);
        CHECK(buffersEqual(g1.first().payload, v.payload));

        Result<RtmpMessage> g2 = reader.readMessage();
        REQUIRE(g2.second().isOk());
        CHECK(g2.first().chunkStreamId == 4);
        CHECK(g2.first().type == RtmpMessage::AudioMessage);
        CHECK(buffersEqual(g2.first().payload, a.payload));
}

// ---------------------------------------------------------------------------
// Back-pressure
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: 1 MiB payload at chunk size 4096 — no byte lost") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        CHECK(writer.setLocalChunkSize(4096) == Error::Ok);

        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = 1;
        msg.timestamp = 0;
        msg.payload = makePayload(0x42, 1 * 1024 * 1024);
        CHECK(writer.writeMessage(msg) == Error::Ok);

        uint8_t  hop[32 * 1024];
        for (;;) {
                int64_t n = tx.read(hop, sizeof(hop));
                if (n <= 0) break;
                CHECK(rx.write(hop, n) == n);
        }

        Result<RtmpMessage> ctrl = reader.readMessage();
        REQUIRE(ctrl.second().isOk());
        CHECK(ctrl.first().type == RtmpMessage::SetChunkSize);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().payload.size() == 1 * 1024 * 1024);
        CHECK(buffersEqual(got.first().payload, msg.payload));
}

// ---------------------------------------------------------------------------
// CS-id encoding forms
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: large CS-id uses 2-byte basic header (csid 200)") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = 5;
        msg.timestamp = 33;
        msg.chunkStreamId = 200;
        msg.payload = makePayload(0xEE, 90);
        CHECK(writer.writeMessage(msg) == Error::Ok);

        uint8_t buf[1024];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().chunkStreamId == 200);
        CHECK(got.first().type == RtmpMessage::VideoMessage);
        CHECK(buffersEqual(got.first().payload, msg.payload));
}

TEST_CASE("RtmpChunkStream: large CS-id uses 3-byte basic header (csid 10000)") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = 5;
        msg.timestamp = 33;
        msg.chunkStreamId = 10000;
        msg.payload = makePayload(0xCC, 90);
        CHECK(writer.writeMessage(msg) == Error::Ok);

        uint8_t buf[1024];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().chunkStreamId == 10000);
        CHECK(buffersEqual(got.first().payload, msg.payload));
}

// ---------------------------------------------------------------------------
// Acknowledgement emission
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: reader emits Acknowledgement after crossing peer window-ack") {
        // Pair the reader against a duplex PipeDevice so it can both
        // receive forwarded data and emit Acknowledgement back out.
        PipeDevice fwd;
        REQUIRE(fwd.open(IODevice::WriteOnly) == Error::Ok);
        PipeDevice both;
        REQUIRE(both.open(IODevice::ReadWrite) == Error::Ok);

        RtmpChunkStream writer(&fwd);
        RtmpChunkStream reader(&both);

        // Step 1: writer advertises a tiny window-ack-size; forward
        // it to reader so reader.peerWindowAckSize() flips to 512.
        CHECK(writer.setLocalWindowAckSize(512) == Error::Ok);
        uint8_t hop[4096];
        int64_t n = fwd.read(hop, sizeof(hop));
        REQUIRE(n > 0);
        CHECK(both.write(hop, n) == n);
        Result<RtmpMessage> winMsg = reader.readMessage();
        REQUIRE(winMsg.second().isOk());
        CHECK(winMsg.first().type == RtmpMessage::WindowAckSize);
        CHECK(reader.peerWindowAckSize() == 512);

        // Step 2: writer sends a 600-byte audio message — crosses
        // the threshold.
        RtmpMessage big;
        big.type = RtmpMessage::AudioMessage;
        big.streamId = 1;
        big.timestamp = 0;
        big.payload = makePayload(0xD7, 600);
        CHECK(writer.writeMessage(big) == Error::Ok);

        n = fwd.read(hop, sizeof(hop));
        REQUIRE(n > 0);
        CHECK(both.write(hop, n) == n);

        Result<RtmpMessage> audio = reader.readMessage();
        REQUIRE(audio.second().isOk());
        CHECK(audio.first().type == RtmpMessage::AudioMessage);
        CHECK(reader.bytesReceived() >= 600);
        CHECK(reader.lastAckBytesAcked() > 0);

        // Drain the reader's outbound Ack via a separate parser.
        PipeDevice ackRx;
        REQUIRE(ackRx.open(IODevice::ReadOnly) == Error::Ok);
        n = both.read(hop, sizeof(hop));
        REQUIRE(n > 0);
        CHECK(ackRx.write(hop, n) == n);

        RtmpChunkStream     ackReader(&ackRx);
        Result<RtmpMessage> ack = ackReader.readMessage();
        REQUIRE(ack.second().isOk());
        CHECK(ack.first().type == RtmpMessage::Acknowledgement);
}

// ---------------------------------------------------------------------------
// Invalid / boundary conditions
// ---------------------------------------------------------------------------

TEST_CASE("RtmpChunkStream: writeMessage on a null device returns Error::Invalid") {
        RtmpChunkStream writer(nullptr);
        RtmpMessage msg;
        msg.type = RtmpMessage::AudioMessage;
        msg.streamId = 1;
        msg.payload = makePayload(0, 10);
        CHECK(writer.writeMessage(msg) == Error::Invalid);
}

TEST_CASE("RtmpChunkStream: readMessage on a null device returns Error::Invalid") {
        RtmpChunkStream reader(nullptr);
        auto r = reader.readMessage();
        CHECK(r.second() == Error::Invalid);
}

TEST_CASE("RtmpChunkStream: zero-length payload round-trips") {
        PipeDevice tx, rx;
        REQUIRE(tx.open(IODevice::WriteOnly) == Error::Ok);
        REQUIRE(rx.open(IODevice::ReadOnly) == Error::Ok);
        RtmpChunkStream writer(&tx);
        RtmpChunkStream reader(&rx);

        RtmpMessage msg;
        msg.type = RtmpMessage::UserControl;
        msg.streamId = 0;
        msg.timestamp = 0;
        msg.payload = Buffer();  // empty
        CHECK(writer.writeMessage(msg) == Error::Ok);

        uint8_t buf[64];
        int64_t n = tx.read(buf, sizeof(buf));
        REQUIRE(n > 0);
        CHECK(rx.write(buf, n) == n);

        Result<RtmpMessage> got = reader.readMessage();
        REQUIRE(got.second().isOk());
        CHECK(got.first().type == RtmpMessage::UserControl);
        CHECK(got.first().payload.size() == 0);
}
