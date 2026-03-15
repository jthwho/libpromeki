/**
 * @file      event.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/event.h>
#include <promeki/core/timerevent.h>

using namespace promeki;

TEST_CASE("Event: type registration produces unique IDs") {
        Event::Type t1 = Event::registerType();
        Event::Type t2 = Event::registerType();
        CHECK(t1 != t2);
        CHECK(t1 != Event::InvalidType);
        CHECK(t2 != Event::InvalidType);
}

TEST_CASE("Event: built-in types are valid") {
        CHECK(Event::Timer != Event::InvalidType);
        CHECK(Event::DeferredCall != Event::InvalidType);
        CHECK(Event::SignalEvent != Event::InvalidType);
        CHECK(Event::Quit != Event::InvalidType);
}

TEST_CASE("Event: built-in types are distinct") {
        CHECK(Event::Timer != Event::DeferredCall);
        CHECK(Event::Timer != Event::SignalEvent);
        CHECK(Event::Timer != Event::Quit);
        CHECK(Event::DeferredCall != Event::SignalEvent);
        CHECK(Event::DeferredCall != Event::Quit);
        CHECK(Event::SignalEvent != Event::Quit);
}

TEST_CASE("Event: construction and type accessor") {
        Event e(Event::Timer);
        CHECK(e.type() == Event::Timer);
}

TEST_CASE("Event: accept and ignore") {
        Event e(Event::Timer);
        CHECK_FALSE(e.isAccepted());
        e.accept();
        CHECK(e.isAccepted());
        e.ignore();
        CHECK_FALSE(e.isAccepted());
}

TEST_CASE("TimerEvent: construction and timerId") {
        TimerEvent te(42);
        CHECK(te.type() == Event::Timer);
        CHECK(te.timerId() == 42);
}

TEST_CASE("TimerEvent: different timer IDs") {
        TimerEvent te1(1);
        TimerEvent te2(99);
        CHECK(te1.timerId() == 1);
        CHECK(te2.timerId() == 99);
}
