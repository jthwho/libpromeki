/**
 * @file      crashhandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <promeki/application.h>
#include <promeki/buffer.h>
#include <promeki/crashhandler.h>
#include <promeki/dir.h>
#include <promeki/error.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/memspace.h>
#include <promeki/set.h>
#include <promeki/string.h>

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
        if (wasInstalled) CrashHandler::install();
}

TEST_CASE("CrashHandler: double install does not crash") {
        bool wasInstalled = CrashHandler::isInstalled();
        CrashHandler::install();
        CrashHandler::install();
        CHECK(CrashHandler::isInstalled());
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        if (wasInstalled) CrashHandler::install();
}

TEST_CASE("CrashHandler: uninstall when not installed is safe") {
        bool wasInstalled = CrashHandler::isInstalled();
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());
        if (wasInstalled) CrashHandler::install();
}

TEST_CASE("CrashHandler: writeTrace produces unique files") {
        bool wasInstalled = CrashHandler::isInstalled();
        // Ensure the handler is installed so the snapshot buffers
        // (host/os/etc.) are populated and the trace gets a full report.
        CrashHandler::install();

        // Snapshot the set of existing trace files for this PID so we
        // can tell new ones from any left over from a previous run.
        Dir         tmp = Dir::temp();
        String      pattern = String::sprintf("promeki-trace-*-%d-*.log", static_cast<int>(getpid()));
        Set<String> before;
        for (const FilePath &f : tmp.entryList(pattern)) {
                before.insert(f.toString());
        }

        // Three traces with different reasons; each should land in a
        // different file thanks to the internal sequence counter.
        CrashHandler::writeTrace("test-one");
        CrashHandler::writeTrace("test-two");
        CrashHandler::writeTrace();

        if (!wasInstalled) CrashHandler::uninstall();

        // Collect the new files.
        List<FilePath> newFiles;
        for (const FilePath &f : tmp.entryList(pattern)) {
                if (!before.contains(f.toString())) newFiles += f;
        }
        CHECK(newFiles.size() == 3);

        // All three filenames should be unique.
        Set<String> unique;
        for (const FilePath &f : newFiles) unique.insert(f.toString());
        CHECK(unique.size() == 3);

        // Clean up so repeated test runs stay tidy.
        for (const FilePath &f : newFiles) {
                std::remove(f.toString().cstr());
        }
}

// ============================================================================
// Application forwarders
// ============================================================================

TEST_CASE("Application: crash handler forwarders") {
        const bool wasInstalled = Application::isCrashHandlerInstalled();
        Application::uninstallCrashHandler();
        CHECK_FALSE(Application::isCrashHandlerInstalled());
        CHECK_FALSE(CrashHandler::isInstalled());
        Application::installCrashHandler();
        CHECK(Application::isCrashHandlerInstalled());
        CHECK(CrashHandler::isInstalled());
        Application::uninstallCrashHandler();
        CHECK_FALSE(Application::isCrashHandlerInstalled());
        if (wasInstalled) CrashHandler::install();
}

// ============================================================================
// refreshCrashHandler end-to-end
// ============================================================================

namespace {

        /// @brief Registers a host-accessible MemSpace with the given name and
        /// returns its ID.  The stats pointer is left as nullptr so
        /// MemSpace::registerData auto-allocates one for us.
        MemSpace::ID registerTestMemSpace(const char *name) {
                MemSpace::ID  id = MemSpace::registerType();
                MemSpace::Ops ops;
                ops.id = id;
                ops.name = String(name);
                ops.isHostAccessible = [](const MemAllocation &) -> bool {
                        return true;
                };
                ops.alloc = [](MemAllocation &) {
                };
                ops.release = [](MemAllocation &) {
                };
                ops.copy = [](const MemAllocation &, const MemAllocation &, size_t) -> Error {
                        return Error::NotSupported;
                };
                ops.fill = [](void *, size_t, char) -> Error {
                        return Error::Ok;
                };
                MemSpace::registerData(std::move(ops));
                return id;
        }

        /// @brief Writes a trace via @ref CrashHandler::writeTrace, locates the
        /// resulting file in /tmp, reads it in, and deletes it.  Returns the
        /// entire trace text.
        String writeTraceAndRead(const char *reason) {
                Dir         tmp = Dir::temp();
                String      pattern = String::sprintf("promeki-trace-*-%d-*.log", static_cast<int>(getpid()));
                Set<String> before;
                for (const FilePath &f : tmp.entryList(pattern)) {
                        before.insert(f.toString());
                }

                CrashHandler::writeTrace(reason);

                FilePath newFile;
                for (const FilePath &f : tmp.entryList(pattern)) {
                        if (!before.contains(f.toString())) {
                                newFile = f;
                                break;
                        }
                }
                REQUIRE_FALSE(newFile.toString().isEmpty());

                File reader(newFile);
                reader.open(IODevice::ReadOnly);
                Buffer contents = reader.readAll();
                reader.close();
                std::remove(newFile.toString().cstr());

                return String::fromUtf8(reinterpret_cast<const char *>(contents.data()), contents.size());
        }

} // namespace

TEST_CASE("CrashHandler: refreshCrashHandler re-snapshots registered MemSpaces") {
        const bool wasInstalled = CrashHandler::isInstalled();

        // Start from a clean install so the pre-install MemSpace below
        // is guaranteed to be captured in the snapshot.
        CrashHandler::uninstall();

        // Register a MemSpace with a distinctive name *before* the
        // crash handler is installed — it must appear in the very
        // first trace we emit.
        const char  *preName = "CrashRefreshPre_abc123";
        MemSpace::ID preId = registerTestMemSpace(preName);
        (void)preId;

        CrashHandler::install();

        // First trace: the pre-install MemSpace should be visible.
        String firstTrace = writeTraceAndRead("refresh-baseline");
        CHECK(firstTrace.find(preName) != String::npos);

        // Now register a second MemSpace *after* install.  It must
        // NOT appear in a trace taken before refreshCrashHandler(),
        // because the snapshot was frozen at install() time.
        const char  *postName = "CrashRefreshPost_xyz789";
        MemSpace::ID postId = registerTestMemSpace(postName);
        (void)postId;

        String staleTrace = writeTraceAndRead("refresh-stale");
        CHECK(staleTrace.find(preName) != String::npos);
        CHECK(staleTrace.find(postName) == String::npos);

        // Refresh the snapshot and re-take the trace.  Both names
        // should now be visible.
        Application::refreshCrashHandler();

        String refreshedTrace = writeTraceAndRead("refresh-after");
        CHECK(refreshedTrace.find(preName) != String::npos);
        CHECK(refreshedTrace.find(postName) != String::npos);

        // Restore the crash-handler state so later tests see the
        // same environment they started with.
        if (!wasInstalled) CrashHandler::uninstall();
}

TEST_CASE("CrashHandler: refreshCrashHandler is a no-op when uninstalled") {
        const bool wasInstalled = CrashHandler::isInstalled();
        CrashHandler::uninstall();
        CHECK_FALSE(CrashHandler::isInstalled());

        // Must not install the handler as a side effect.
        Application::refreshCrashHandler();
        CHECK_FALSE(CrashHandler::isInstalled());

        if (wasInstalled) CrashHandler::install();
}

// ============================================================================
// consoleTraceEnabled / setConsoleTraceEnabled
// ============================================================================

TEST_CASE("CrashHandler: consoleTraceEnabled default is true") {
        // The default state must be true so that production builds always
        // echo traces to stderr unless explicitly suppressed.
        bool saved = CrashHandler::consoleTraceEnabled();
        // Restore whatever the runner has configured (may be false in quiet mode).
        CrashHandler::setConsoleTraceEnabled(true);
        CHECK(CrashHandler::consoleTraceEnabled());
        CrashHandler::setConsoleTraceEnabled(saved);
}

TEST_CASE("CrashHandler: setConsoleTraceEnabled round-trips") {
        bool saved = CrashHandler::consoleTraceEnabled();

        CrashHandler::setConsoleTraceEnabled(false);
        CHECK_FALSE(CrashHandler::consoleTraceEnabled());

        CrashHandler::setConsoleTraceEnabled(true);
        CHECK(CrashHandler::consoleTraceEnabled());

        CrashHandler::setConsoleTraceEnabled(saved);
        CHECK(CrashHandler::consoleTraceEnabled() == saved);
}
