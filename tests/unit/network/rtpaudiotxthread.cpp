/**
 * @file      rtpaudiotxthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>

#include <promeki/atomic.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/ipv4address.h>
#include <promeki/queue.h>
#include <promeki/rtpaudiotxthread.h>
#include <promeki/rtppayload.h>
#include <promeki/rtpsession.h>
#include <promeki/socketaddress.h>
#include <promeki/udpsocket.h>

using namespace promeki;

namespace {

// Stands up a real RtpSession bound to a loopback port pointing at
// a separately-bound receiver socket so the test can sniff every
// AES67 RTP datagram the TX thread emits.  The receiver socket has
// a short timeout so reads never block longer than necessary.
struct AudioTxHarness {
                UdpSocket     receiver;
                RtpSession    session;
                RtpPayloadL16 payload;

                AudioTxHarness(uint32_t sampleRate = 48000, int channels = 2)
                    : payload(sampleRate, channels) {
                        receiver.open(IODevice::ReadWrite);
                        receiver.setReceiveTimeout(200);
                        REQUIRE(receiver.bind(SocketAddress::any(0)).isOk());
                        const SocketAddress dest(Ipv4Address::loopback(),
                                                 receiver.localAddress().port());
                        session.setRemote(dest);
                        session.setSsrc(0xCAFEBABE);
                        session.setPayloadType(96);
                        session.setClockRate(sampleRate);
                        REQUIRE(session.start(SocketAddress::any(0)).isOk());
                }

                ~AudioTxHarness() {
                        session.stop();
                        receiver.close();
                }

                int64_t recv(uint8_t *buf, size_t cap) {
                        return receiver.readDatagram(buf, cap);
                }

                bool hasNoDatagram() {
                        receiver.setReceiveTimeout(50);
                        uint8_t buf[16];
                        const int64_t n = receiver.readDatagram(buf, sizeof(buf));
                        receiver.setReceiveTimeout(200);
                        return n <= 0;
                }
};

// Decodes the 12-byte RTP fixed header into its key fields.
struct RtpHeader {
                uint8_t  pt = 0;
                bool     marker = false;
                uint16_t seq = 0;
                uint32_t timestamp = 0;
                uint32_t ssrc = 0;
};

RtpHeader decodeHeader(const uint8_t *buf) {
        RtpHeader h;
        h.pt = static_cast<uint8_t>(buf[1] & 0x7F);
        h.marker = (buf[1] & 0x80) != 0;
        h.seq = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
        h.timestamp = (uint32_t(buf[4]) << 24) | (uint32_t(buf[5]) << 16) |
                      (uint32_t(buf[6]) << 8) | uint32_t(buf[7]);
        h.ssrc = (uint32_t(buf[8]) << 24) | (uint32_t(buf[9]) << 16) |
                 (uint32_t(buf[10]) << 8) | uint32_t(buf[11]);
        return h;
}

constexpr size_t kPacketSamples = 48;       // 1 ms at 48 kHz
constexpr size_t kPacketBytes = kPacketSamples * 2 /*ch*/ * 2 /*bytes*/;
constexpr int    kPacketTimeUs = 1000;

RtpAudioTxContext makeCtx(AudioTxHarness &h, Atomic<int64_t> &packets,
                          Atomic<int64_t> &bytes, Atomic<int64_t> &octets,
                          Atomic<int64_t> &silencePackets,
                          Atomic<int64_t> &silenceSamples) {
        RtpAudioTxContext ctx;
        ctx.storageDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        ctx.packetSamples = kPacketSamples;
        ctx.packetBytes = kPacketBytes;
        ctx.packetTimeUs = kPacketTimeUs;
        ctx.session = &h.session;
        ctx.payload = &h.payload;
        ctx.clockRate = 48000;
        ctx.packetsSent = &packets;
        ctx.bytesSent = &bytes;
        ctx.senderOctets = &octets;
        ctx.silencePacketsEmitted = &silencePackets;
        ctx.silenceSamplesEmitted = &silenceSamples;
        return ctx;
}

Buffer makeRampChunk(uint8_t startVal) {
        Buffer b(kPacketBytes);
        b.setSize(kPacketBytes);
        auto *p = static_cast<uint8_t *>(b.data());
        for (size_t i = 0; i < kPacketBytes; ++i) {
                p[i] = static_cast<uint8_t>((startVal + i) & 0xFF);
        }
        return b;
}

} // namespace

TEST_CASE("RtpAudioTxThread: emits queued chunks with monotone RTP timestamps") {
        AudioTxHarness  h;
        Atomic<int64_t> packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);

        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));

        // Push three real chunks, run three iterations.
        for (int i = 0; i < 3; ++i) {
                REQUIRE(tx.packetQueue().pushBlocking(makeRampChunk(static_cast<uint8_t>(i)))
                                .isOk());
        }
        for (int i = 0; i < 3; ++i) {
                CHECK(tx.runOnceForTest());
        }
        CHECK(packets.value() == 3);
        CHECK(silencePackets.value() == 0);
        CHECK(silenceSamples.value() == 0);
        // RTP-TS cursor advanced by exactly 3 × packetSamples.
        CHECK(tx.rtpTsCursor() == static_cast<uint32_t>(3 * kPacketSamples));

        // Verify each emitted packet's wire RTP-TS is monotone
        // and spaced by exactly packetSamples — no silence
        // intermixed.
        for (int i = 0; i < 3; ++i) {
                uint8_t buf[1500];
                const int64_t n = h.recv(buf, sizeof(buf));
                REQUIRE(n > 12);
                const RtpHeader hdr = decodeHeader(buf);
                CHECK(hdr.ssrc == 0xCAFEBABEu);
                CHECK_FALSE(hdr.marker);
                CHECK(hdr.timestamp == static_cast<uint32_t>(i * kPacketSamples));
                // Payload bytes — wire payload starts at byte 12.
                REQUIRE(static_cast<size_t>(n) == 12 + kPacketBytes);
                for (size_t s = 0; s < kPacketBytes; ++s) {
                        CHECK(buf[12 + s] == static_cast<uint8_t>((i + s) & 0xFF));
                }
        }
}

TEST_CASE("RtpAudioTxThread: emits silence at cadence when queue is empty") {
        AudioTxHarness  h;
        Atomic<int64_t> packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);

        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));

        // Drive 4 iterations with an empty queue — every iteration
        // must emit a silence packet.
        for (int i = 0; i < 4; ++i) {
                CHECK(tx.runOnceForTest());
        }
        CHECK(packets.value() == 4);
        CHECK(silencePackets.value() == 4);
        CHECK(silenceSamples.value() == static_cast<int64_t>(4 * kPacketSamples));

        // Verify wire RTP-TS contiguity across the silence run —
        // every packet advances the cursor by exactly packetSamples.
        for (int i = 0; i < 4; ++i) {
                uint8_t       buf[1500];
                const int64_t n = h.recv(buf, sizeof(buf));
                REQUIRE(n > 12);
                const RtpHeader hdr = decodeHeader(buf);
                CHECK(hdr.timestamp == static_cast<uint32_t>(i * kPacketSamples));
                // PCMI_S16BE silence is all-zero wire payload.
                REQUIRE(static_cast<size_t>(n) == 12 + kPacketBytes);
                for (size_t s = 0; s < kPacketBytes; ++s) {
                        CHECK(buf[12 + s] == 0);
                }
        }
}

TEST_CASE("RtpAudioTxThread: silence-fill preserves RTP-TS contiguity across a gap") {
        AudioTxHarness  h;
        Atomic<int64_t> packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);

        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));

        // 1 real, 2 silence, 1 real — interleaved cadence ticks.
        REQUIRE(tx.packetQueue().pushBlocking(makeRampChunk(0x10)).isOk());
        CHECK(tx.runOnceForTest()); // real
        CHECK(tx.runOnceForTest()); // silence (queue empty)
        CHECK(tx.runOnceForTest()); // silence
        REQUIRE(tx.packetQueue().pushBlocking(makeRampChunk(0x20)).isOk());
        CHECK(tx.runOnceForTest()); // real

        CHECK(packets.value() == 4);
        CHECK(silencePackets.value() == 2);
        CHECK(silenceSamples.value() == static_cast<int64_t>(2 * kPacketSamples));

        // The four wire packets should have RTP-TS 0, 48, 96, 144
        // — silence does not break the cursor.
        for (int i = 0; i < 4; ++i) {
                uint8_t       buf[1500];
                const int64_t n = h.recv(buf, sizeof(buf));
                REQUIRE(n > 12);
                const RtpHeader hdr = decodeHeader(buf);
                CHECK(hdr.timestamp == static_cast<uint32_t>(i * kPacketSamples));
        }
}

TEST_CASE("RtpAudioTxThread: noteRtpEmission flips hasEmissionRecord on first emit") {
        AudioTxHarness  h;
        Atomic<int64_t> packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);

        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));

        CHECK_FALSE(h.session.hasEmissionRecord());
        CHECK(tx.runOnceForTest()); // silence packet — counts as an emission
        CHECK(h.session.hasEmissionRecord());
}

TEST_CASE("RtpAudioTxThread: senderOctets accumulates payload bytes (no header)") {
        AudioTxHarness  h;
        Atomic<int64_t> packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);

        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));

        REQUIRE(tx.packetQueue().pushBlocking(makeRampChunk(0)).isOk());
        REQUIRE(tx.packetQueue().pushBlocking(makeRampChunk(0)).isOk());
        CHECK(tx.runOnceForTest());
        CHECK(tx.runOnceForTest());
        CHECK(packets.value() == 2);
        CHECK(octets.value() == static_cast<int64_t>(2 * kPacketBytes));
        CHECK(bytes.value() >= static_cast<int64_t>(2 * (kPacketBytes + 12)));
}

TEST_CASE("RtpAudioTxThread: wrong-size chunks are skipped without crashing") {
        AudioTxHarness  h;
        Atomic<int64_t> packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);

        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));

        // Push a chunk that's the wrong size — runOnce must return
        // false and advance the RTP-TS cursor regardless so wire
        // continuity is preserved.
        Buffer wrongSize(kPacketBytes / 2);
        wrongSize.setSize(kPacketBytes / 2);
        std::memset(wrongSize.data(), 0, kPacketBytes / 2);
        REQUIRE(tx.packetQueue().pushBlocking(std::move(wrongSize)).isOk());

        CHECK_FALSE(tx.runOnceForTest()); // bad chunk, no emit
        CHECK(packets.value() == 0);
        CHECK(tx.rtpTsCursor() == static_cast<uint32_t>(kPacketSamples));
        CHECK(h.hasNoDatagram());
}

TEST_CASE("RtpAudioTxThread: packetQueue depth derived from packetTimeUs") {
        // packetTimeUs = 1000 (1 ms), HeadroomUs = 1_000_000 → depth ~1000.
        AudioTxHarness   h;
        Atomic<int64_t>  packets(0), bytes(0), octets(0), silencePackets(0), silenceSamples(0);
        RtpAudioTxThread tx(makeCtx(h, packets, bytes, octets,
                                    silencePackets, silenceSamples));
        CHECK(tx.packetQueue().maxSize() >= RtpAudioTxThread::MinDepth);
        CHECK(tx.packetQueue().maxSize() ==
              static_cast<size_t>(RtpAudioTxThread::HeadroomUs / kPacketTimeUs));
}
