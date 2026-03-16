/**
 * @file      ansistream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/ansistream.h>
#include <promeki/core/stringiodevice.h>

using namespace promeki;

TEST_CASE("AnsiStream: construction from IODevice") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << "hello";
        CHECK(str == "hello");
}

TEST_CASE("AnsiStream: setAnsiEnabled controls output") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(false);
        as.setForeground(AnsiStream::Red);
        // With ANSI disabled, no escape codes should be emitted
        CHECK(str.isEmpty());
}

TEST_CASE("AnsiStream: setForeground emits escape code") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(true);
        as.setForeground(AnsiStream::Red);
        CHECK_FALSE(str.isEmpty());
        CHECK(str.find("\033[") != String::npos);
}

TEST_CASE("AnsiStream: setBackground emits escape code") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(true);
        as.setBackground(AnsiStream::Blue);
        CHECK_FALSE(str.isEmpty());
        CHECK(str.find("\033[") != String::npos);
}

TEST_CASE("AnsiStream: reset emits escape code") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(true);
        as.reset();
        CHECK_FALSE(str.isEmpty());
}

TEST_CASE("AnsiStream: cursor movement") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(true);
        as.cursorUp(3);
        CHECK(str.find("3") != String::npos);
}

TEST_CASE("AnsiStream: clearScreen") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(true);
        as.clearScreen();
        CHECK_FALSE(str.isEmpty());
}

TEST_CASE("AnsiStream: chaining works") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.setAnsiEnabled(true);
        as.setForeground(AnsiStream::Green).reset();
        CHECK_FALSE(str.isEmpty());
}

TEST_CASE("AnsiStream: device accessor") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        CHECK(as.device() == &dev);
}

TEST_CASE("AnsiStream: write char") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write('A');
        as.write('B');
        CHECK(str == "AB");
}

TEST_CASE("AnsiStream: write int") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write(42);
        CHECK(str == "42");
}

TEST_CASE("AnsiStream: write C string") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write("test");
        CHECK(str == "test");
}

TEST_CASE("AnsiStream: write String") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as.write(String("hello"));
        CHECK(str == "hello");
}

TEST_CASE("AnsiStream: flush does not crash") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << "data";
        as.flush();
        CHECK(str == "data");
}

TEST_CASE("AnsiStream: operator<< char") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << 'X';
        CHECK(str == "X");
}

TEST_CASE("AnsiStream: operator<< int") {
        String str;
        StringIODevice dev(&str);
        dev.open(IODevice::WriteOnly);
        AnsiStream as(&dev);
        as << 99;
        CHECK(str == "99");
}

TEST_CASE("AnsiStream: stdoutSupportsANSI returns bool") {
        // Just verify it doesn't crash
        bool result = AnsiStream::stdoutSupportsANSI();
        (void)result;
        CHECK(true);
}
