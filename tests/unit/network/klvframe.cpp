/**
 * @file      klvframe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/klvframe.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>
#include <promeki/datastream.h>
#include <promeki/fourcc.h>
#include <promeki/localserver.h>
#include <promeki/localsocket.h>
#include <promeki/dir.h>
#include <promeki/uuid.h>

using namespace promeki;

// ============================================================================
// In-memory round-trip via BufferIODevice
// ============================================================================

TEST_CASE("KlvFrame: round-trip one frame through a buffer") {
        Buffer         store(1024);
        BufferIODevice dev(&store);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());

        const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
        {
                KlvWriter w(&dev);
                CHECK(w.writeFrame(FourCC("HELO"), payload, sizeof(payload)).isOk());
        }
        dev.seek(0);
        {
                KlvReader r(&dev);
                KlvFrame  frame;
                CHECK(r.readFrame(frame).isOk());
                CHECK(frame.key == FourCC("HELO"));
                CHECK(frame.value.size() == sizeof(payload));
                CHECK(std::memcmp(frame.value.data(), payload, sizeof(payload)) == 0);
        }
}

TEST_CASE("KlvFrame: round-trip multiple frames, mixed sizes") {
        Buffer         store(1024);
        BufferIODevice dev(&store);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                KlvWriter w(&dev);
                CHECK(w.writeFrame(FourCC("ONE ")).isOk()); // zero-length
                CHECK(w.writeFrame(FourCC("TWO "), "xy", 2).isOk());
                const char *msg = "hello world";
                CHECK(w.writeFrame(FourCC("THRE"), msg, 11).isOk());
        }
        dev.seek(0);
        {
                KlvReader r(&dev);
                KlvFrame  f;
                CHECK(r.readFrame(f).isOk());
                CHECK(f.key == FourCC("ONE "));
                CHECK(f.value.size() == 0);

                CHECK(r.readFrame(f).isOk());
                CHECK(f.key == FourCC("TWO "));
                CHECK(f.value.size() == 2);
                CHECK(std::memcmp(f.value.data(), "xy", 2) == 0);

                CHECK(r.readFrame(f).isOk());
                CHECK(f.key == FourCC("THRE"));
                CHECK(f.value.size() == 11);
                CHECK(std::memcmp(f.value.data(), "hello world", 11) == 0);

                // Next read is EOF.
                CHECK(r.readFrame(f) == Error::EndOfFile);
        }
}

TEST_CASE("KlvFrame: wire layout is Key|Length|Value, Length big-endian") {
        Buffer         store(64);
        BufferIODevice dev(&store);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                KlvWriter   w(&dev);
                const char *payload = "ABC";
                CHECK(w.writeFrame(FourCC("TICK"), payload, 3).isOk());
        }
        REQUIRE(dev.pos() == 11); // 4 + 4 + 3
        const uint8_t *raw = static_cast<const uint8_t *>(store.data());
        CHECK(raw[0] == 'T');
        CHECK(raw[1] == 'I');
        CHECK(raw[2] == 'C');
        CHECK(raw[3] == 'K');
        CHECK(raw[4] == 0x00);
        CHECK(raw[5] == 0x00);
        CHECK(raw[6] == 0x00);
        CHECK(raw[7] == 0x03); // Length = 3 BE
        CHECK(raw[8] == 'A');
        CHECK(raw[9] == 'B');
        CHECK(raw[10] == 'C');
}

TEST_CASE("KlvFrame: skipValue lets a reader forward past unknown frames") {
        Buffer         store(1024);
        BufferIODevice dev(&store);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                KlvWriter w(&dev);
                CHECK(w.writeFrame(FourCC("KNOW"), "ok", 2).isOk());
                CHECK(w.writeFrame(FourCC("WTF?"), "ignored payload", 15).isOk());
                CHECK(w.writeFrame(FourCC("KNOW"), "yo", 2).isOk());
        }
        dev.seek(0);
        {
                KlvReader r(&dev);
                FourCC    key(0, 0, 0, 0);
                uint32_t  sz = 0;

                // 1: known, read it
                CHECK(r.readHeader(key, sz).isOk());
                CHECK(key == FourCC("KNOW"));
                CHECK(sz == 2);
                char buf[8] = {};
                CHECK(r.readValue(buf, sz).isOk());
                CHECK(std::memcmp(buf, "ok", 2) == 0);

                // 2: unknown, skip
                CHECK(r.readHeader(key, sz).isOk());
                CHECK(key == FourCC("WTF?"));
                CHECK(sz == 15);
                CHECK(r.skipValue(sz).isOk());

                // 3: known, read it
                CHECK(r.readHeader(key, sz).isOk());
                CHECK(key == FourCC("KNOW"));
                std::memset(buf, 0, sizeof(buf));
                CHECK(r.readValue(buf, sz).isOk());
                CHECK(std::memcmp(buf, "yo", 2) == 0);

                // 4: EOF
                CHECK(r.readHeader(key, sz) == Error::EndOfFile);
        }
}

TEST_CASE("KlvFrame: readFrame rejects oversize payload") {
        Buffer         store(64);
        BufferIODevice dev(&store);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                KlvWriter w(&dev);
                CHECK(w.writeFrame(FourCC("BIG "), "xxxxxxxx", 8).isOk());
        }
        dev.seek(0);
        {
                KlvReader r(&dev);
                KlvFrame  f;
                CHECK(r.readFrame(f, /*maxValueBytes=*/4) == Error::TooLarge);
                CHECK(f.key == FourCC("BIG "));
        }
}

TEST_CASE("KlvFrame: EOF mid-header is reported as IOError, mid-value too") {
        // Header truncated — only 3 bytes written where 8 are required.
        {
                Buffer         store(4);
                BufferIODevice dev(&store);
                REQUIRE(dev.open(IODevice::ReadWrite).isOk());
                const uint8_t partial[3] = {'H', 'E', 'L'};
                dev.write(partial, 3);
                dev.seek(0);

                KlvReader r(&dev);
                FourCC    key(0, 0, 0, 0);
                uint32_t  sz = 0;
                CHECK(r.readHeader(key, sz) == Error::IOError);
        }
        // Value truncated — header claims 16 bytes but only 4 follow.
        {
                Buffer         store(32);
                BufferIODevice dev(&store);
                REQUIRE(dev.open(IODevice::ReadWrite).isOk());
                uint8_t hdr[8] = {'T', 'I', 'C', 'K', 0, 0, 0, 16};
                dev.write(hdr, sizeof(hdr));
                dev.write("abcd", 4);
                dev.seek(0);

                KlvReader r(&dev);
                KlvFrame  f;
                CHECK(r.readFrame(f) == Error::IOError);
        }
}

// ============================================================================
// End-to-end: KLV carrying DataStream-encoded value over a LocalSocket.
// ============================================================================

static String uniqueSocketPath(const char *tag) {
        return Dir::temp()
                .path()
                .join(String("promeki-test-klv-") + String(tag) + String("-") + UUID::generateV4().toString() +
                      String(".sock"))
                .toString();
}

TEST_CASE("KlvFrame: DataStream value over LocalSocket") {
        if (!LocalServer::isSupported()) return;
        String path = uniqueSocketPath("ds");

        LocalServer server;
        REQUIRE(server.listen(path).isOk());

        LocalSocket client;
        REQUIRE(client.connectTo(path).isOk());
        REQUIRE(server.waitForNewConnection(2000).isOk());
        LocalSocket *ss = server.nextPendingConnection();
        REQUIRE(ss != nullptr);

        // Build a DataStream-encoded blob and send as one KLV frame.
        Buffer         blob(128);
        BufferIODevice blobDev(&blob);
        REQUIRE(blobDev.open(IODevice::ReadWrite).isOk());
        DataStream ws = DataStream::createWriter(&blobDev);
        ws << String("hello") << int32_t(42);
        REQUIRE(ws.status() == DataStream::Ok);
        // blob now holds wire bytes; blobDev.pos() is the used size.
        uint32_t blobSize = static_cast<uint32_t>(blobDev.pos());

        KlvWriter w(&client);
        CHECK(w.writeFrame(FourCC("HELO"), blob.data(), blobSize).isOk());

        // Receiver side.
        KlvReader r(ss);
        KlvFrame  frame;
        CHECK(r.readFrame(frame).isOk());
        CHECK(frame.key == FourCC("HELO"));
        REQUIRE(frame.value.size() == blobSize);

        // Decode via DataStream over the received buffer.
        Buffer         rxCopy = frame.value;
        BufferIODevice rxDev(&rxCopy);
        REQUIRE(rxDev.open(IODevice::ReadWrite).isOk());
        DataStream rs = DataStream::createReader(&rxDev);
        String     s;
        int32_t    n = 0;
        rs >> s >> n;
        CHECK(rs.status() == DataStream::Ok);
        CHECK(s == String("hello"));
        CHECK(n == 42);

        delete ss;
}
