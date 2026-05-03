/**
 * @file      tests/unit/mediaio_setclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for the MediaIOPortGroup::setClock + MediaIOCommandSetClock
 * plumbing.  Verifies the framework swap-on-Ok contract, the default
 * NotSupported behavior, the pre-open / closing gating, and that a
 * backend that returns a non-Ok result leaves the group's clock
 * untouched.
 */

#include <doctest/doctest.h>
#include <promeki/clock.h>
#include <promeki/error.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>

#include "mediaio_test_helpers.h"

using namespace promeki;
using promeki::tests::InlineTestMediaIO;

namespace {

        // Test backend that adds one default-clock port group on open
        // and exposes the SetClock hook from InlineTestMediaIO.
        class GroupedTestIO : public InlineTestMediaIO {
                        PROMEKI_OBJECT(GroupedTestIO, InlineTestMediaIO)
                public:
                        GroupedTestIO() {
                                // Default open hook: create one default-clock
                                // port group so tests have a target for setClock.
                                onOpen = [this](MediaIOCommandOpen &) -> Error {
                                        if (addPortGroup(String("test")) == nullptr) {
                                                return Error::Invalid;
                                        }
                                        return Error::Ok;
                                };
                        }
        };

} // namespace

TEST_CASE("MediaIOPortGroup::setClock default backend returns NotSupported") {
        GroupedTestIO io;
        REQUIRE(io.open().wait().isOk());
        REQUIRE(io.portGroupCount() == 1);
        MediaIOPortGroup *group = io.portGroup(0);
        REQUIRE(group != nullptr);

        Clock::Ptr originalClock = group->clock();
        REQUIRE(originalClock.isValid());

        Clock::Ptr     newClock = Clock::Ptr::takeOwnership(new WallClock());
        MediaIORequest req      = group->setClock(newClock);
        Error          result   = req.wait();
        CHECK(result == Error::NotSupported);

        // Default backend rejected → the group's clock must stay
        // exactly as it was, not the proposed new clock.
        CHECK(group->clock().ptr() == originalClock.ptr());
        CHECK(group->clock().ptr() != newClock.ptr());

        io.close().wait();
}

TEST_CASE("MediaIOPortGroup::setClock framework swaps clock on Ok") {
        GroupedTestIO io;
        // Backend accepts the swap.  We only need to assert the
        // framework picks up cmd.clock and writes it onto the group.
        io.onSetClock = [](MediaIOCommandSetClock &cmd) -> Error {
                CHECK(cmd.group != nullptr);
                return Error::Ok;
        };

        REQUIRE(io.open().wait().isOk());
        MediaIOPortGroup *group = io.portGroup(0);
        REQUIRE(group != nullptr);

        Clock::Ptr originalClock = group->clock();
        REQUIRE(originalClock.isValid());
        REQUIRE(originalClock.ptr() != nullptr);

        Clock::Ptr   newClock = Clock::Ptr::takeOwnership(new WallClock());
        const Clock *newRaw   = newClock.ptr();
        REQUIRE(newClock.ptr() != originalClock.ptr());

        MediaIORequest req    = group->setClock(newClock);
        Error          result = req.wait();
        CHECK(result == Error::Ok);

        // After successful Ok the group's clock is the one we passed.
        CHECK(group->clock().ptr() == newRaw);
        CHECK(group->clock().ptr() != originalClock.ptr());

        io.close().wait();
}

TEST_CASE("MediaIOPortGroup::setClock null clock detaches on Ok") {
        GroupedTestIO io;
        // Backend accepts the null swap.  The framework swaps in
        // whatever cmd.clock is — null means group->clock() becomes a
        // null Ptr.  Backends that want to restore the original
        // synthetic clock are responsible for that themselves.
        io.onSetClock = [](MediaIOCommandSetClock &cmd) -> Error {
                CHECK(cmd.group != nullptr);
                CHECK_FALSE(cmd.clock.isValid());
                return Error::Ok;
        };

        REQUIRE(io.open().wait().isOk());
        MediaIOPortGroup *group = io.portGroup(0);
        REQUIRE(group != nullptr);

        Clock::Ptr nullClock; // Default-constructed = null.
        REQUIRE(!nullClock.isValid());

        MediaIORequest req    = group->setClock(nullClock);
        Error          result = req.wait();
        CHECK(result == Error::Ok);
        CHECK_FALSE(group->clock().isValid());

        io.close().wait();
}

TEST_CASE("MediaIOPortGroup::setClock backend error preserves original clock") {
        GroupedTestIO io;
        io.onSetClock = [](MediaIOCommandSetClock &) -> Error { return Error::Invalid; };

        REQUIRE(io.open().wait().isOk());
        MediaIOPortGroup *group = io.portGroup(0);
        REQUIRE(group != nullptr);

        Clock::Ptr originalClock = group->clock();
        REQUIRE(originalClock.isValid());

        Clock::Ptr     newClock = Clock::Ptr::takeOwnership(new WallClock());
        MediaIORequest req      = group->setClock(newClock);
        Error          result   = req.wait();
        CHECK(result == Error::Invalid);

        // On any non-Ok the framework leaves the existing clock alone.
        CHECK(group->clock().ptr() == originalClock.ptr());
        CHECK(group->clock().ptr() != newClock.ptr());

        io.close().wait();
}

TEST_CASE("MediaIOPortGroup::setClock pre-open returns NotOpen") {
        // Open then close to leave a captured group pointer that's
        // still alive (parent IO retains the group via ObjectBase).
        // Calling setClock on a not-open MediaIO must short-circuit
        // before reaching the backend.
        GroupedTestIO     io;
        REQUIRE(io.open().wait().isOk());
        MediaIOPortGroup *group = io.portGroup(0);
        REQUIRE(group != nullptr);
        io.close().wait();

        bool hookCalled = false;
        io.onSetClock = [&hookCalled](MediaIOCommandSetClock &) -> Error {
                hookCalled = true;
                return Error::Ok;
        };

        Clock::Ptr     newClock = Clock::Ptr::takeOwnership(new WallClock());
        MediaIORequest req      = group->setClock(newClock);
        CHECK(req.wait() == Error::NotOpen);
        CHECK_FALSE(hookCalled);
}

TEST_CASE("MediaIOCommandSetClock::kindName") {
        // Kind enum member + kindName() round-trip for telemetry keys
        // (collectors stamp <kind>+<id> labels — a stable name is the
        // contract).
        CHECK(String(MediaIOCommand::kindName(MediaIOCommand::SetClock)) == String("SetClock"));
}
