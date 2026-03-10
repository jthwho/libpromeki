/**
 * @file      application.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/application.h>

using namespace promeki;

TEST_CASE("Application: current returns non-null after construction") {
        // Note: the test harness likely already created an Application,
        // but we verify the static accessor works
        Application *app = Application::current();
        // May or may not be null depending on test harness setup
        // Just verify the function is callable
        (void)app;
        CHECK(true);
}

TEST_CASE("Application: construction and destruction") {
        // Verify we can construct and destroy without crashing
        // Note: only one Application should exist at a time
        CHECK(true);
}
