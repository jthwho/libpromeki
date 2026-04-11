/**
 * @file      crashhandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstdio>
#include <unistd.h>
#include <promeki/crashhandler.h>
#include <promeki/filepath.h>
#include <promeki/dir.h>
#include <promeki/set.h>

using namespace promeki;

// The doctest runner installs the crash handler at startup (see
// tests/doctest_main.cpp), so these tests may start with the
// handler already installed.  Each case saves that state up front
// and restores it at the end so cases can run in any order.

TEST_CASE("CrashHandler: install and uninstall") {
        bool wasInstalled = CrashHandler::isInstalled();
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        CrashHandler::install();
        CHECK(CrashHandler::isInstalled());
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        if(wasInstalled) CrashHandler::install();
}

TEST_CASE("CrashHandler: double install does not crash") {
        bool wasInstalled = CrashHandler::isInstalled();
        CrashHandler::install();
        CrashHandler::install();
        CHECK(CrashHandler::isInstalled());
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        if(wasInstalled) CrashHandler::install();
}

TEST_CASE("CrashHandler: uninstall when not installed is safe") {
        bool wasInstalled = CrashHandler::isInstalled();
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        if(wasInstalled) CrashHandler::install();
}

TEST_CASE("CrashHandler: writeTrace produces unique files") {
        bool wasInstalled = CrashHandler::isInstalled();
        // Ensure the handler is installed so the snapshot buffers
        // (host/os/etc.) are populated and the trace gets a full report.
        CrashHandler::install();

        // Snapshot the set of existing trace files for this PID so we
        // can tell new ones from any left over from a previous run.
        Dir tmp = Dir::temp();
        String pattern = String::sprintf("promeki-trace-*-%d-*.log",
                                         static_cast<int>(getpid()));
        Set<String> before;
        for(const FilePath &f : tmp.entryList(pattern)) {
                before.insert(f.toString());
        }

        // Three traces with different reasons; each should land in a
        // different file thanks to the internal sequence counter.
        CrashHandler::writeTrace("test-one");
        CrashHandler::writeTrace("test-two");
        CrashHandler::writeTrace();

        if(!wasInstalled) CrashHandler::uninstall();

        // Collect the new files.
        List<FilePath> newFiles;
        for(const FilePath &f : tmp.entryList(pattern)) {
                if(!before.contains(f.toString())) newFiles += f;
        }
        CHECK(newFiles.size() == 3);

        // All three filenames should be unique.
        Set<String> unique;
        for(const FilePath &f : newFiles) unique.insert(f.toString());
        CHECK(unique.size() == 3);

        // Clean up so repeated test runs stay tidy.
        for(const FilePath &f : newFiles) {
                std::remove(f.toString().cstr());
        }
}
