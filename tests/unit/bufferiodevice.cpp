/**
 * @file      bufferiodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>
#include <promeki/result.h>

using namespace promeki;

TEST_CASE("BufferIODevice: default state") {
        BufferIODevice dev;
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.buffer() == nullptr);
}

TEST_CASE("BufferIODevice: open without buffer fails") {
        BufferIODevice dev;
        Error          err = dev.open(IODevice::ReadWrite);
        CHECK(err.isError());
        CHECK(err.code() == Error::Invalid);
}

TEST_CASE("BufferIODevice: open with valid buffer") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        CHECK(dev.buffer() == &buf);

        Error err = dev.open(IODevice::ReadWrite);
        CHECK(err.isOk());
        CHECK(dev.isOpen());
        CHECK(dev.isReadable());
        CHECK(dev.isWritable());
        dev.close();
}

TEST_CASE("BufferIODevice: double open returns AlreadyOpen") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        Error err = dev.open(IODevice::ReadOnly);
        CHECK(err.code() == Error::AlreadyOpen);
        dev.close();
}

TEST_CASE("BufferIODevice: write and read back") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        const char *msg = "Hello, BufferIODevice!";
        size_t      len = std::strlen(msg);
        int64_t     written = dev.write(msg, static_cast<int64_t>(len));
        CHECK(written == static_cast<int64_t>(len));
        CHECK(buf.size() == len);

        dev.seek(0);
        char    out[64] = {};
        int64_t bytesRead = dev.read(out, static_cast<int64_t>(len));
        CHECK(bytesRead == static_cast<int64_t>(len));
        CHECK(std::strcmp(out, msg) == 0);
        dev.close();
}

TEST_CASE("BufferIODevice: seek and position") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        dev.write("0123456789", 10);
        CHECK(dev.pos() == 10);
        CHECK(value(dev.size()) == 10);

        CHECK(dev.seek(5).isOk());
        CHECK(dev.pos() == 5);

        CHECK(dev.seek(0).isOk());
        CHECK(dev.pos() == 0);

        CHECK(dev.seek(-1).isError());
        dev.close();
}

TEST_CASE("BufferIODevice: bytesAvailable") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        dev.write("abcdef", 6);
        dev.seek(0);
        CHECK(dev.bytesAvailable() == 6);
        dev.seek(3);
        CHECK(dev.bytesAvailable() == 3);
        dev.seek(6);
        CHECK(dev.bytesAvailable() == 0);
        dev.close();
}

TEST_CASE("BufferIODevice: atEnd") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        dev.write("test", 4);
        CHECK(dev.atEnd());

        dev.seek(0);
        CHECK_FALSE(dev.atEnd());

        dev.seek(4);
        CHECK(dev.atEnd());
        dev.close();
}

TEST_CASE("BufferIODevice: isSequential returns false") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        CHECK_FALSE(dev.isSequential());
}

TEST_CASE("BufferIODevice: read returns 0 at end") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        dev.write("abc", 3);

        char    out[4] = {};
        int64_t n = dev.read(out, 4);
        CHECK(n == 0);
        dev.close();
}

TEST_CASE("BufferIODevice: partial read") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        dev.write("Hello", 5);
        dev.seek(3);

        char    out[10] = {};
        int64_t n = dev.read(out, 10);
        CHECK(n == 2);
        CHECK(out[0] == 'l');
        CHECK(out[1] == 'o');
        dev.close();
}

TEST_CASE("BufferIODevice: write fails when buffer too small") {
        Buffer         buf(4);
        BufferIODevice dev(&buf);
        dev.open(IODevice::WriteOnly);

        int64_t n = dev.write("too long for buffer", 19);
        CHECK(n == -1);
        CHECK(dev.error().code() == Error::BufferTooSmall);
        dev.close();
}

TEST_CASE("BufferIODevice: read on write-only returns -1") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::WriteOnly);
        char out[4];
        CHECK(dev.read(out, 4) == -1);
        dev.close();
}

TEST_CASE("BufferIODevice: write on read-only returns -1") {
        Buffer buf(256);
        // Pre-fill so there's data to read
        std::memset(buf.data(), 'A', 4);
        buf.setSize(4);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadOnly);
        CHECK(dev.write("abc", 3) == -1);
        dev.close();
}

TEST_CASE("BufferIODevice: setBuffer") {
        Buffer         buf1(64);
        Buffer         buf2(128);
        BufferIODevice dev(&buf1);
        CHECK(dev.buffer() == &buf1);
        dev.setBuffer(&buf2);
        CHECK(dev.buffer() == &buf2);
}

TEST_CASE("BufferIODevice: aboutToClose signal") {
        Buffer         buf(64);
        BufferIODevice dev(&buf);
        bool           fired = false;
        dev.aboutToCloseSignal.connect([&fired]() { fired = true; });
        dev.open(IODevice::ReadWrite);
        dev.close();
        CHECK(fired);
}

TEST_CASE("BufferIODevice: bytesWritten signal") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        int64_t        reported = 0;
        dev.bytesWrittenSignal.connect([&reported](int64_t n) { reported = n; });
        dev.open(IODevice::WriteOnly);
        dev.write("test", 4);
        CHECK(reported == 4);
        dev.close();
}

TEST_CASE("BufferIODevice: multiple writes accumulate") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        dev.write("abc", 3);
        dev.write("def", 3);
        dev.write("ghi", 3);
        CHECK(value(dev.size()) == 9);
        CHECK(dev.pos() == 9);

        dev.seek(0);
        char out[10] = {};
        dev.read(out, 9);
        CHECK(std::strcmp(out, "abcdefghi") == 0);
        dev.close();
}

TEST_CASE("BufferIODevice: overwrite in middle") {
        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        dev.write("AAAAAAA", 7);
        dev.seek(2);
        dev.write("BBB", 3);

        dev.seek(0);
        char out[8] = {};
        dev.read(out, 7);
        CHECK(out[0] == 'A');
        CHECK(out[1] == 'A');
        CHECK(out[2] == 'B');
        CHECK(out[3] == 'B');
        CHECK(out[4] == 'B');
        CHECK(out[5] == 'A');
        CHECK(out[6] == 'A');
        dev.close();
}

TEST_CASE("BufferIODevice: close when not open returns error") {
        Buffer         buf(64);
        BufferIODevice dev(&buf);
        Error          err = dev.close();
        CHECK(err.code() == Error::NotOpen);
}

TEST_CASE("BufferIODevice: destructor auto-closes") {
        Buffer buf(64);
        bool   fired = false;
        {
                BufferIODevice dev(&buf);
                dev.aboutToCloseSignal.connect([&fired]() { fired = true; });
                dev.open(IODevice::ReadWrite);
                dev.write("test", 4);
        } // destructor should close
        CHECK(fired);
}

TEST_CASE("BufferIODevice: seek on closed device returns error") {
        Buffer         buf(64);
        BufferIODevice dev(&buf);
        Error          err = dev.seek(0);
        CHECK(err.isError());
}

TEST_CASE("BufferIODevice: bytesAvailable on closed device") {
        Buffer         buf(64);
        BufferIODevice dev(&buf);
        CHECK(dev.bytesAvailable() == 0);
}

TEST_CASE("BufferIODevice: errorOccurred signal on write failure") {
        Buffer         buf(4);
        BufferIODevice dev(&buf);
        Error          reported;
        dev.errorOccurredSignal.connect([&reported](const Error &e) { reported = e; });
        dev.open(IODevice::WriteOnly);
        dev.write("this is way too long", 20);
        CHECK(reported.code() == Error::BufferTooSmall);
}
