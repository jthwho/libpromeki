/**
 * @file      ansistream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <sstream>
#include <doctest/doctest.h>
#include <promeki/core/ansistream.h>

using namespace promeki;

TEST_CASE("AnsiStream: construction from ostream") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as << "hello";
        CHECK(oss.str() == "hello");
}

TEST_CASE("AnsiStream: setAnsiEnabled controls output") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(false);
        as.setForeground(AnsiStream::Red);
        // With ANSI disabled, no escape codes should be emitted
        CHECK(oss.str().empty());
}

TEST_CASE("AnsiStream: setForeground emits escape code") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(true);
        as.setForeground(AnsiStream::Red);
        CHECK_FALSE(oss.str().empty());
        CHECK(oss.str().find("\033[") != std::string::npos);
}

TEST_CASE("AnsiStream: setBackground emits escape code") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(true);
        as.setBackground(AnsiStream::Blue);
        CHECK_FALSE(oss.str().empty());
        CHECK(oss.str().find("\033[") != std::string::npos);
}

TEST_CASE("AnsiStream: reset emits escape code") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(true);
        as.reset();
        CHECK_FALSE(oss.str().empty());
}

TEST_CASE("AnsiStream: cursor movement") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(true);
        as.cursorUp(3);
        CHECK(oss.str().find("3") != std::string::npos);
}

TEST_CASE("AnsiStream: clearScreen") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(true);
        as.clearScreen();
        CHECK_FALSE(oss.str().empty());
}

TEST_CASE("AnsiStream: chaining works") {
        std::ostringstream oss;
        AnsiStream as(oss);
        as.setAnsiEnabled(true);
        as.setForeground(AnsiStream::Green).reset();
        CHECK_FALSE(oss.str().empty());
}

TEST_CASE("AnsiStream: stdoutSupportsANSI returns bool") {
        // Just verify it doesn't crash
        bool result = AnsiStream::stdoutSupportsANSI();
        (void)result;
        CHECK(true);
}
