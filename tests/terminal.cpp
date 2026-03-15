/**
 * @file      terminal.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/terminal.h>
#include <promeki/core/env.h>

using namespace promeki;

// Helper RAII guard that saves, sets, and restores environment variables
// used by Terminal::colorSupport().  Because colorSupport() caches its
// result with a static, we cannot test multiple scenarios in a single
// process.  These tests therefore verify the detection logic indirectly
// where possible and exercise the API surface.

struct EnvGuard {
        const char *name;
        String      orig;
        bool        wasSet;

        EnvGuard(const char *n, const char *val) : name(n) {
                wasSet = Env::isSet(name);
                if(wasSet) orig = Env::get(name);
                if(val) Env::set(name, val);
                else    Env::unset(name);
        }

        ~EnvGuard() {
                if(wasSet) Env::set(name, orig);
                else       Env::unset(name);
        }
};

TEST_CASE("Terminal: ColorSupport enum values are ordered") {
        CHECK(Terminal::NoColor   < Terminal::Basic);
        CHECK(Terminal::Basic     < Terminal::Color256);
        CHECK(Terminal::Color256  < Terminal::TrueColor);
}

TEST_CASE("Terminal: colorSupport returns a valid enum value") {
        auto cs = Terminal::colorSupport();
        CHECK(cs >= Terminal::NoColor);
        CHECK(cs <= Terminal::TrueColor);
}

TEST_CASE("Terminal: colorSupport is consistent across calls") {
        // The result is cached, so repeated calls must return the same value.
        auto a = Terminal::colorSupport();
        auto b = Terminal::colorSupport();
        CHECK(a == b);
}

TEST_CASE("Terminal: construction and destruction") {
        // Verify basic lifecycle doesn't crash
        Terminal t;
        CHECK_FALSE(t.isRawMode());
        CHECK_FALSE(t.isMouseTrackingEnabled());
}

TEST_CASE("Terminal: size returns non-negative dimensions") {
        Terminal t;
        auto sz = t.size();
        // In a CI / non-TTY environment the size may be 0x0,
        // but it should never be negative.
        CHECK(sz.width() >= 0);
        CHECK(sz.height() >= 0);
}

TEST_CASE("Terminal: windowSize outputs non-negative values") {
        Terminal t;
        int cols = -1, rows = -1;
        Error err = t.windowSize(cols, rows);
        // May fail on non-TTY, but if it succeeds, values must be sane
        if(err.isOk()) {
                CHECK(cols > 0);
                CHECK(rows > 0);
        } else {
                // On non-TTY, windowSize returns error; cols/rows stay as-is or 0
                CHECK(err.isError());
        }
}

TEST_CASE("Terminal: writeOutput succeeds for valid data") {
        Terminal t;
        const char *data = "test";
        auto [n, err] = t.writeOutput(data, 4);
        // writeOutput writes to STDOUT which works even for pipes
        CHECK(err.isOk());
        CHECK(n == 4);
}

TEST_CASE("Terminal: writeOutput with zero length") {
        Terminal t;
        auto [n, err] = t.writeOutput("", 0);
        CHECK(err.isOk());
        CHECK(n == 0);
}

TEST_CASE("Terminal: enableRawMode on non-TTY returns error") {
        // In CI/pipe environments, stdin is not a TTY so enableRawMode fails.
        // If it IS a TTY, it should succeed and we clean up.
        Terminal t;
        Error err = t.enableRawMode();
        if(err.isOk()) {
                CHECK(t.isRawMode());
                Error err2 = t.disableRawMode();
                CHECK(err2.isOk());
                CHECK_FALSE(t.isRawMode());
        } else {
                CHECK_FALSE(t.isRawMode());
        }
}

TEST_CASE("Terminal: disableRawMode when not in raw mode is no-op") {
        Terminal t;
        Error err = t.disableRawMode();
        CHECK(err.isOk());
        CHECK_FALSE(t.isRawMode());
}

TEST_CASE("Terminal: enableRawMode idempotent") {
        Terminal t;
        Error err1 = t.enableRawMode();
        if(err1.isOk()) {
                // Second call should be a no-op (already in raw mode)
                Error err2 = t.enableRawMode();
                CHECK(err2.isOk());
                CHECK(t.isRawMode());
                t.disableRawMode();
        }
}

TEST_CASE("Terminal: mouse tracking toggle and state") {
        Terminal t;
        CHECK_FALSE(t.isMouseTrackingEnabled());

        Error err = t.enableMouseTracking();
        CHECK(err.isOk());
        CHECK(t.isMouseTrackingEnabled());

        // Idempotent — enabling again is a no-op
        err = t.enableMouseTracking();
        CHECK(err.isOk());
        CHECK(t.isMouseTrackingEnabled());

        err = t.disableMouseTracking();
        CHECK(err.isOk());
        CHECK_FALSE(t.isMouseTrackingEnabled());

        // Idempotent — disabling again is a no-op
        err = t.disableMouseTracking();
        CHECK(err.isOk());
        CHECK_FALSE(t.isMouseTrackingEnabled());
}

TEST_CASE("Terminal: bracketed paste toggle and state") {
        Terminal t;

        Error err = t.enableBracketedPaste();
        CHECK(err.isOk());

        err = t.enableBracketedPaste();
        CHECK(err.isOk());

        err = t.disableBracketedPaste();
        CHECK(err.isOk());

        err = t.disableBracketedPaste();
        CHECK(err.isOk());
}

TEST_CASE("Terminal: alternate screen toggle and state") {
        Terminal t;

        Error err = t.enableAlternateScreen();
        CHECK(err.isOk());

        err = t.enableAlternateScreen();
        CHECK(err.isOk());

        err = t.disableAlternateScreen();
        CHECK(err.isOk());

        err = t.disableAlternateScreen();
        CHECK(err.isOk());
}

TEST_CASE("Terminal: setResizeCallback accepts and clears") {
        Terminal t;

        bool called = false;
        t.setResizeCallback([&called](int, int) { called = true; });

        // Clear callback
        t.setResizeCallback(nullptr);
        // No crash = pass
}

TEST_CASE("Terminal: installSignalHandlers does not crash") {
        Terminal t;
        t.installSignalHandlers();
        // No crash = pass
}

TEST_CASE("Terminal: readInput on non-TTY") {
        Terminal t;
        char buf[16];
        auto [n, err] = t.readInput(buf, sizeof(buf));
        // On a non-TTY, readInput may return 0 (EOF) or an error.
        // Either outcome is valid; it should not crash.
        if(err.isOk()) {
                CHECK(n >= 0);
        }
}

TEST_CASE("Terminal: destructor cleans up active state") {
        // Verify that enabling features and then destroying the Terminal
        // does not crash (destructor calls disable methods).
        {
                Terminal t;
                t.enableMouseTracking();
                t.enableBracketedPaste();
                t.enableAlternateScreen();
                // ~Terminal should clean all of these up
        }
        // No crash = pass
}
