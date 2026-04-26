/**
 * @file      streamstring.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/streamstring.h>
#include <promeki/stringlist.h>

using namespace promeki;

// ============================================================================
// Default constructor (no callback)
// ============================================================================

TEST_CASE("StreamString_DefaultConstructor") {
        StreamString ss;
        ss.stream() << "Hello\n";

        // Line stays in the buffer since no callback cleared it
        CHECK(ss.line() == "Hello");
}

// ============================================================================
// Callback that clears
// ============================================================================

TEST_CASE("StreamString_CallbackClears") {
        StringList   captured;
        StreamString ss([&captured](String &line) {
                captured.pushToBack(line);
                return true;
        });

        ss.stream() << "Line 1\nLine 2\n";
        REQUIRE(captured.size() == 2);
        CHECK(captured[0] == "Line 1");
        CHECK(captured[1] == "Line 2");
        CHECK(ss.line().isEmpty());
}

// ============================================================================
// Callback that keeps accumulating
// ============================================================================

TEST_CASE("StreamString_CallbackKeeps") {
        int          callCount = 0;
        StreamString ss([&callCount](String &line) {
                callCount++;
                return false;
        });

        ss.stream() << "Part 1\nPart 2\n";
        CHECK(callCount == 2);
        CHECK(ss.line() == "Part 1Part 2");
}

// ============================================================================
// Callback that modifies the line
// ============================================================================

TEST_CASE("StreamString_CallbackModifies") {
        StreamString ss([](String &line) {
                line = line.toUpper();
                return false;
        });

        ss.stream() << "hello\n world\n";
        CHECK(ss.line() == "HELLO WORLD");
}

// ============================================================================
// Partial line and sync
// ============================================================================

TEST_CASE("StreamString_PartialLineSync") {
        StringList   captured;
        StreamString ss([&captured](String &line) {
                captured.pushToBack(line);
                return true;
        });

        ss.stream() << "No newline here";
        CHECK(captured.size() == 0);
        CHECK(ss.line() == "No newline here");

        ss.stream() << promeki::flush;
        REQUIRE(captured.size() == 1);
        CHECK(captured[0] == "No newline here");
        CHECK(ss.line().isEmpty());
}

// ============================================================================
// Empty lines are not flushed
// ============================================================================

TEST_CASE("StreamString_EmptyLinesSkipped") {
        int          callCount = 0;
        StreamString ss([&callCount](String &) {
                callCount++;
                return true;
        });

        ss.stream() << "\n\n\n";
        CHECK(callCount == 0);
}

// ============================================================================
// Mixed output with stream operators
// ============================================================================

TEST_CASE("StreamString_MixedOutput") {
        StringList   captured;
        StreamString ss([&captured](String &line) {
                captured.pushToBack(line);
                return true;
        });

        ss.stream() << "Value: " << 42 << " and " << 3.14 << promeki::endl;
        REQUIRE(captured.size() == 1);
        CHECK(captured[0].startsWith("Value: 42 and 3.14"));
}

// ============================================================================
// clear()
// ============================================================================

TEST_CASE("StreamString_Clear") {
        StreamString ss;
        ss.stream() << "Some content";
        CHECK(!ss.line().isEmpty());

        ss.clear();
        CHECK(ss.line().isEmpty());

        // Can still write after clear
        ss.stream() << "New content\n";
        CHECK(ss.line() == "New content");
}

// ============================================================================
// setOnNewLine()
// ============================================================================

TEST_CASE("StreamString_SetOnNewLine") {
        StreamString ss;
        ss.stream() << "Before callback\n";
        CHECK(ss.line() == "Before callback");

        StringList captured;
        ss.setOnNewLine([&captured](String &line) {
                captured.pushToBack(line);
                return true;
        });

        ss.clear();
        ss.stream() << "After callback\n";
        REQUIRE(captured.size() == 1);
        CHECK(captured[0] == "After callback");
}
