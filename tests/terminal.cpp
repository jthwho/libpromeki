/**
 * @file      terminal.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

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
