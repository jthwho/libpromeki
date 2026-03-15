/**
 * @file      stringiodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/core/stringiodevice.h>
#include <promeki/core/string.h>
#include <promeki/core/result.h>

using namespace promeki;

TEST_CASE("StringIODevice: default state") {
        StringIODevice dev;
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.string() == nullptr);
}

TEST_CASE("StringIODevice: open without string fails") {
        StringIODevice dev;
        Error err = dev.open(IODevice::ReadWrite);
        CHECK(err.isError());
        CHECK(err.code() == Error::Invalid);
}

TEST_CASE("StringIODevice: open with valid string") {
        String str;
        StringIODevice dev(&str);
        CHECK(dev.string() == &str);
        Error err = dev.open(IODevice::ReadWrite);
        CHECK(err.isOk());
        CHECK(dev.isOpen());
        CHECK(dev.isReadable());
        CHECK(dev.isWritable());
        dev.close();
}

TEST_CASE("StringIODevice: double open returns AlreadyOpen") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::ReadWrite);
        Error err = dev.open(IODevice::ReadOnly);
        CHECK(err.code() == Error::AlreadyOpen);
        dev.close();
}

TEST_CASE("StringIODevice: write appends to string") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        const char *data = "Hello";
        int64_t n = dev.write(data, 5);
        CHECK(n == 5);
        CHECK(str == "Hello");
        const char *more = " World";
        n = dev.write(more, 6);
        CHECK(n == 6);
        CHECK(str == "Hello World");
        dev.close();
}

TEST_CASE("StringIODevice: read from string") {
        String str("Hello World");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        char buf[32] = {};
        int64_t n = dev.read(buf, 5);
        CHECK(n == 5);
        CHECK(std::memcmp(buf, "Hello", 5) == 0);
        n = dev.read(buf, 6);
        CHECK(n == 6);
        CHECK(std::memcmp(buf, " World", 6) == 0);
        dev.close();
}

TEST_CASE("StringIODevice: read past end returns 0") {
        String str("Hi");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        char buf[32] = {};
        int64_t n = dev.read(buf, 2);
        CHECK(n == 2);
        n = dev.read(buf, 1);
        CHECK(n == 0);
}

TEST_CASE("StringIODevice: seek and pos") {
        String str("abcdefg");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        CHECK(dev.pos() == 0);
        dev.seek(3);
        CHECK(dev.pos() == 3);
        char buf[4] = {};
        dev.read(buf, 4);
        CHECK(std::memcmp(buf, "defg", 4) == 0);
}

TEST_CASE("StringIODevice: seek negative fails") {
        String str("abc");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        Error err = dev.seek(-1);
        CHECK(err.code() == Error::OutOfRange);
}

TEST_CASE("StringIODevice: size returns byte count") {
        String str("Hello");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        auto [sz, err] = dev.size();
        CHECK(err.isOk());
        CHECK(sz == 5);
}

TEST_CASE("StringIODevice: atEnd") {
        String str("ab");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        CHECK_FALSE(dev.atEnd());
        dev.seek(2);
        CHECK(dev.atEnd());
}

TEST_CASE("StringIODevice: bytesAvailable") {
        String str("abcde");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        CHECK(dev.bytesAvailable() == 5);
        dev.seek(3);
        CHECK(dev.bytesAvailable() == 2);
}

TEST_CASE("StringIODevice: isSequential returns false") {
        String str;
        StringIODevice dev(&str);
        CHECK_FALSE(dev.isSequential());
}

TEST_CASE("StringIODevice: setString") {
        StringIODevice dev;
        String str("test");
        dev.setString(&str);
        CHECK(dev.string() == &str);
        dev.open(IODevice::ReadOnly);
        char buf[4] = {};
        dev.read(buf, 4);
        CHECK(std::memcmp(buf, "test", 4) == 0);
}

TEST_CASE("StringIODevice: write at middle overwrites") {
        String str("abcdefg");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadWrite);
        dev.seek(2);
        const char *data = "XY";
        dev.write(data, 2);
        CHECK(str == "abXYefg");
}

TEST_CASE("StringIODevice: round-trip write then read") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::ReadWrite);
        const char *data = "Hello 42";
        dev.write(data, 8);
        dev.seek(0);
        char buf[8] = {};
        int64_t n = dev.read(buf, 8);
        CHECK(n == 8);
        CHECK(std::memcmp(buf, "Hello 42", 8) == 0);
}

TEST_CASE("StringIODevice: close when not open returns NotOpen") {
        StringIODevice dev;
        Error err = dev.close();
        CHECK(err.code() == Error::NotOpen);
}

TEST_CASE("StringIODevice: read on WriteOnly returns -1") {
        String str("hello");
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        char buf[4];
        int64_t n = dev.read(buf, 4);
        CHECK(n == -1);
        dev.close();
}

TEST_CASE("StringIODevice: write on ReadOnly returns -1") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::ReadOnly);
        int64_t n = dev.write("hi", 2);
        CHECK(n == -1);
        dev.close();
}

TEST_CASE("StringIODevice: write with zero maxSize returns 0") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        int64_t n = dev.write("hi", 0);
        CHECK(n == 0);
        CHECK(str.isEmpty());
        dev.close();
}

TEST_CASE("StringIODevice: write past end pads with spaces") {
        String str("ab");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadWrite);
        dev.seek(5);
        dev.write("X", 1);
        CHECK(str == "ab   X");
        dev.close();
}

TEST_CASE("StringIODevice: bytesAvailable when not open returns 0") {
        String str("hello");
        StringIODevice dev(&str);
        CHECK(dev.bytesAvailable() == 0);
}

TEST_CASE("StringIODevice: atEnd when not open returns true") {
        String str("hello");
        StringIODevice dev(&str);
        CHECK(dev.atEnd());
}

TEST_CASE("StringIODevice: size with null string returns error") {
        StringIODevice dev;
        auto [sz, err] = dev.size();
        CHECK(err.isError());
        CHECK(err.code() == Error::Invalid);
}

TEST_CASE("StringIODevice: seek when not open returns NotOpen") {
        String str("hello");
        StringIODevice dev(&str);
        Error err = dev.seek(0);
        CHECK(err.code() == Error::NotOpen);
}

TEST_CASE("StringIODevice: write overwrites and extends past end") {
        String str("abc");
        StringIODevice dev(&str);
        dev.open(IODevice::ReadWrite);
        dev.seek(1);
        dev.write("WXYZ", 4);
        CHECK(str == "aWXYZ");
        dev.close();
}
