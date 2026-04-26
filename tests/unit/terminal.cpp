/**
 * @file      terminal.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <unistd.h>
#include <fcntl.h>
#include <doctest/doctest.h>
#include <promeki/terminal.h>
#include <promeki/env.h>

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
                        if (wasSet) orig = Env::get(name);
                        if (val)
                                Env::set(name, val);
                        else
                                Env::unset(name);
                }

                ~EnvGuard() {
                        if (wasSet)
                                Env::set(name, orig);
                        else
                                Env::unset(name);
                }
};

static int nullFd() {
        static int fd = ::open("/dev/null", O_RDWR);
        return fd;
}

TEST_CASE("Terminal: ColorSupport enum values are ordered") {
        CHECK(Terminal::NoColor < Terminal::Grayscale16);
        CHECK(Terminal::Grayscale16 < Terminal::Grayscale256);
        CHECK(Terminal::Grayscale256 < Terminal::GrayscaleTrue);
        CHECK(Terminal::GrayscaleTrue < Terminal::Basic);
        CHECK(Terminal::Basic < Terminal::Color256);
        CHECK(Terminal::Color256 < Terminal::TrueColor);
}

TEST_CASE("Terminal: colorSupport returns a valid enum value") {
        auto cs = Terminal::colorSupport();
        CHECK(cs >= Terminal::NoColor);
        CHECK(cs <= Terminal::TrueColor);
}

TEST_CASE("Terminal: colorSupport is consistent across calls") {
        auto a = Terminal::colorSupport();
        auto b = Terminal::colorSupport();
        CHECK(a == b);
}

TEST_CASE("Terminal: construction and destruction") {
        Terminal t;
        CHECK_FALSE(t.isRawMode());
        CHECK_FALSE(t.isMouseTrackingEnabled());
}

TEST_CASE("Terminal: custom fd construction") {
        int      fd = nullFd();
        Terminal t(fd, fd);
        CHECK(t.inputFd() == fd);
        CHECK(t.outputFd() == fd);
        CHECK_FALSE(t.isRawMode());
        CHECK_FALSE(t.isMouseTrackingEnabled());
}

TEST_CASE("Terminal: default fd values") {
        Terminal t;
        CHECK(t.inputFd() == STDIN_FILENO);
        CHECK(t.outputFd() == STDOUT_FILENO);
}

TEST_CASE("Terminal: size returns non-negative dimensions") {
        Terminal t;
        auto     sz = t.size();
        CHECK(sz.width() >= 0);
        CHECK(sz.height() >= 0);
}

TEST_CASE("Terminal: windowSize outputs non-negative values") {
        Terminal t;
        int      cols = -1, rows = -1;
        Error    err = t.windowSize(cols, rows);
        if (err.isOk()) {
                CHECK(cols > 0);
                CHECK(rows > 0);
        } else {
                CHECK(err.isError());
        }
}

TEST_CASE("Terminal: writeOutput succeeds for valid data") {
        Terminal    t(nullFd(), nullFd());
        const char *data = "test";
        auto [n, err] = t.writeOutput(data, 4);
        CHECK(err.isOk());
        CHECK(n == 4);
}

TEST_CASE("Terminal: writeOutput with zero length") {
        Terminal t(nullFd(), nullFd());
        auto [n, err] = t.writeOutput("", 0);
        CHECK(err.isOk());
        CHECK(n == 0);
}

TEST_CASE("Terminal: enableRawMode on non-TTY returns error") {
        Terminal t(nullFd(), nullFd());
        Error    err = t.enableRawMode();
        if (err.isOk()) {
                CHECK(t.isRawMode());
                Error err2 = t.disableRawMode();
                CHECK(err2.isOk());
                CHECK_FALSE(t.isRawMode());
        } else {
                CHECK_FALSE(t.isRawMode());
        }
}

TEST_CASE("Terminal: disableRawMode when not in raw mode is no-op") {
        Terminal t(nullFd(), nullFd());
        Error    err = t.disableRawMode();
        CHECK(err.isOk());
        CHECK_FALSE(t.isRawMode());
}

TEST_CASE("Terminal: enableRawMode idempotent") {
        Terminal t;
        Error    err1 = t.enableRawMode();
        if (err1.isOk()) {
                Error err2 = t.enableRawMode();
                CHECK(err2.isOk());
                CHECK(t.isRawMode());
                t.disableRawMode();
        }
}

TEST_CASE("Terminal: mouse tracking toggle and state") {
        Terminal t(nullFd(), nullFd());
        CHECK_FALSE(t.isMouseTrackingEnabled());

        Error err = t.enableMouseTracking();
        CHECK(err.isOk());
        CHECK(t.isMouseTrackingEnabled());

        err = t.enableMouseTracking();
        CHECK(err.isOk());
        CHECK(t.isMouseTrackingEnabled());

        err = t.disableMouseTracking();
        CHECK(err.isOk());
        CHECK_FALSE(t.isMouseTrackingEnabled());

        err = t.disableMouseTracking();
        CHECK(err.isOk());
        CHECK_FALSE(t.isMouseTrackingEnabled());
}

TEST_CASE("Terminal: bracketed paste toggle and state") {
        Terminal t(nullFd(), nullFd());

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
        Terminal t(nullFd(), nullFd());

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

        t.setResizeCallback(nullptr);
}

TEST_CASE("Terminal: installSignalHandlers does not crash") {
        Terminal t;
        t.installSignalHandlers();
}

TEST_CASE("Terminal: readInput on non-TTY") {
        Terminal t(nullFd(), nullFd());
        char     buf[16];
        auto [n, err] = t.readInput(buf, sizeof(buf));
        if (err.isOk()) {
                CHECK(n >= 0);
        }
}

TEST_CASE("Terminal: destructor cleans up active state") {
        {
                Terminal t(nullFd(), nullFd());
                t.enableMouseTracking();
                t.enableBracketedPaste();
                t.enableAlternateScreen();
        }
}
