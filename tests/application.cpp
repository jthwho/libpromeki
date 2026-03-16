/**
 * @file      application.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/application.h>
#include <promeki/core/iodevice.h>

using namespace promeki;

TEST_CASE("Application: static accessors work without an instance") {
        Application::setAppName("no-instance");
        CHECK(Application::appName() == "no-instance");
        Application::setAppName("");
}

TEST_CASE("Application: arguments captured from argc/argv") {
        char arg0[] = "myapp";
        char arg1[] = "--verbose";
        char arg2[] = "input.wav";
        char *argv[] = { arg0, arg1, arg2 };
        Application app(3, argv);
        REQUIRE(Application::arguments().size() == 3);
        CHECK(Application::arguments()[0] == "myapp");
        CHECK(Application::arguments()[1] == "--verbose");
        CHECK(Application::arguments()[2] == "input.wav");
}

TEST_CASE("Application: arguments empty after destruction") {
        {
                char arg0[] = "test";
                char *argv[] = { arg0 };
                Application app(1, argv);
                CHECK_FALSE(Application::arguments().isEmpty());
        }
        CHECK(Application::arguments().isEmpty());
}

TEST_CASE("Application: setAppName and appName roundtrip") {
        Application::setAppName("test-application");
        CHECK(Application::appName() == "test-application");
        Application::setAppName("");
}

TEST_CASE("Application: setAppUUID and appUUID roundtrip") {
        UUID id("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        Application::setAppUUID(id);
        CHECK(Application::appUUID() == id);
        CHECK(Application::appUUID().isValid());
        Application::setAppUUID(UUID());
}

TEST_CASE("Application: stdinDevice returns readable device") {
        IODevice *dev = Application::stdinDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isReadable());
}

TEST_CASE("Application: stdoutDevice returns writable device") {
        IODevice *dev = Application::stdoutDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isWritable());
}

TEST_CASE("Application: stderrDevice returns writable device") {
        IODevice *dev = Application::stderrDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isWritable());
}

TEST_CASE("Application: not copyable or movable") {
        CHECK_FALSE(std::is_copy_constructible_v<Application>);
        CHECK_FALSE(std::is_move_constructible_v<Application>);
        CHECK_FALSE(std::is_copy_assignable_v<Application>);
        CHECK_FALSE(std::is_move_assignable_v<Application>);
}
