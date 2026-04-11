/**
 * @file      libraryoptions.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/libraryoptions.h>
#include <promeki/env.h>

using namespace promeki;

TEST_CASE("LibraryOptions: spec defaults are correct") {
        // Verifies the declared default values directly from the
        // spec registry — independent of any singleton state
        // (the doctest runner enables core dumps at startup, so
        // the singleton's runtime value won't match the spec default).
        const VariantSpec *s;

        s = LibraryOptions::spec(LibraryOptions::CrashHandler);
        REQUIRE(s != nullptr);
        CHECK(s->defaultValue().get<bool>() == true);

        s = LibraryOptions::spec(LibraryOptions::CoreDumps);
        REQUIRE(s != nullptr);
        CHECK(s->defaultValue().get<bool>() == false);

        s = LibraryOptions::spec(LibraryOptions::CrashLogDir);
        REQUIRE(s != nullptr);
        CHECK(s->defaultValue().get<String>().isEmpty());

        s = LibraryOptions::spec(LibraryOptions::CaptureEnvironment);
        REQUIRE(s != nullptr);
        CHECK(s->defaultValue().get<bool>() == true);
}

TEST_CASE("LibraryOptions: programmatic set overrides default") {
        LibraryOptions &opts = LibraryOptions::instance();
        bool prev = opts.getAs<bool>(LibraryOptions::CoreDumps);
        opts.set(LibraryOptions::CoreDumps, true);
        CHECK(opts.getAs<bool>(LibraryOptions::CoreDumps) == true);
        opts.set(LibraryOptions::CoreDumps, prev);
}

TEST_CASE("LibraryOptions: loadFromEnvironment parses bool") {
        LibraryOptions &opts = LibraryOptions::instance();
        bool prev = opts.getAs<bool>(LibraryOptions::CoreDumps);
        // Force the opposite of the current value so loadFromEnvironment
        // has something to do.
        opts.set(LibraryOptions::CoreDumps, !prev);
        Env::set("PROMEKI_OPT_CoreDumps", prev ? "true" : "false");
        opts.loadFromEnvironment();
        CHECK(opts.getAs<bool>(LibraryOptions::CoreDumps) == prev);
        // Restore.
        Env::unset("PROMEKI_OPT_CoreDumps");
        opts.set(LibraryOptions::CoreDumps, prev);
}

TEST_CASE("LibraryOptions: loadFromEnvironment parses string") {
        Env::set("PROMEKI_OPT_CrashLogDir", "/var/log/crashes");
        LibraryOptions &opts = LibraryOptions::instance();
        opts.loadFromEnvironment();
        CHECK(opts.getAs<String>(LibraryOptions::CrashLogDir) == "/var/log/crashes");
        // Restore.
        opts.set(LibraryOptions::CrashLogDir, String());
        Env::unset("PROMEKI_OPT_CrashLogDir");
}

TEST_CASE("LibraryOptions: unknown env var is ignored") {
        Env::set("PROMEKI_OPT_NonExistentOption", "123");
        LibraryOptions &opts = LibraryOptions::instance();
        // Should not crash, just log a warning.
        opts.loadFromEnvironment();
        Env::unset("PROMEKI_OPT_NonExistentOption");
}
