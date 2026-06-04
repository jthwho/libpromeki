/**
 * @file      crashhandler.cpp
 * @copyright Jason Howard. All rights reserved.
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
#include <promeki/libraryoptions.h>
#include <promeki/memspace.h>
#include <promeki/platform.h>
#include <promeki/set.h>
#include <promeki/string.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#endif

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

// ============================================================================
// End-to-end crash path (forked child)
// ============================================================================

#if defined(PROMEKI_PLATFORM_POSIX)

namespace {

        /// Reads an entire file into a String.
        String slurp(const FilePath &p) {
                File f(p);
                f.open(IODevice::ReadOnly);
                Buffer c = f.readAll();
                f.close();
                return String::fromUtf8(reinterpret_cast<const char *>(c.data()), c.size());
        }

        /// Async-signal-safe cleanup hook used to prove the crash path runs
        /// registered handlers before printing the report.
        void crashMarkerHook(void *) {
                static const char msg[] = "CLEANUP-HOOK-RAN\n";
                ssize_t           r = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
                (void)r;
        }

        void noopCleanup(void *) {}

} // namespace

TEST_CASE("CrashHandler: cleanup handler registration") {
        CHECK(CrashHandler::addCleanupHandler(nullptr, nullptr) == -1);

        int h = CrashHandler::addCleanupHandler(&noopCleanup, nullptr);
        CHECK(h >= 0);

        // Removal is idempotent and tolerates unknown handles.
        CrashHandler::removeCleanupHandler(h);
        CrashHandler::removeCleanupHandler(h);
        CrashHandler::removeCleanupHandler(-1);
        CrashHandler::removeCleanupHandler(99999);

        // The freed slot is reusable.
        int h2 = CrashHandler::addCleanupHandler(&noopCleanup, nullptr);
        CHECK(h2 >= 0);
        CrashHandler::removeCleanupHandler(h2);
}

// Forks a child that actually crashes (raises SIGABRT) so we exercise the
// real signal-handler path — not just writeTrace().  Verifies that:
//   1. the crash log path is announced on stderr *before* the crash header
//      (so the location is visible even if a later stage stalls), and
//   2. the on-disk log is complete end-to-end (header → stack trace → END).
TEST_CASE("CrashHandler: crash path announces the log path first and writes a complete report") {
        const bool   wasInstalled = CrashHandler::isInstalled();
        const String savedDir = LibraryOptions::instance().getAs<String>(LibraryOptions::CrashLogDir);
        const int32_t savedTimeout =
                LibraryOptions::instance().getAs<int32_t>(LibraryOptions::CrashStackTraceTimeout);

        // Dedicated scratch dir so the only crash file present is ours.
        Dir scratch(Dir::temp().path() / String::sprintf("promeki-crashtest-%d", static_cast<int>(getpid())));
        REQUIRE(scratch.mkpath() == Error::Ok);

        // Keep the worst case fast: if a forked-from-multithreaded malloc lock
        // makes symbolization stall, the watchdog falls back after this many
        // seconds.  The path/log assertions hold either way.
        LibraryOptions::instance().set(LibraryOptions::CrashStackTraceTimeout, 3);
        // Guard against the watchdog being silently disabled: getAs() falls
        // back to T{} (==0) when the option is unresolved, which would make the
        // crash handler symbolize unguarded.
        CHECK(LibraryOptions::instance().getAs<int32_t>(LibraryOptions::CrashStackTraceTimeout, 10) == 3);
        // The handler captures its log path (and PID) at install() time, so
        // the file the child writes carries *this* PID — we therefore know its
        // location without re-installing in the (post-fork) child.
        LibraryOptions::instance().set(LibraryOptions::CrashLogDir, scratch.path().toString());
        CrashHandler::install();

        const String errPath = String::sprintf("%s/child-stderr.txt", scratch.path().toString().cstr());

        // Register a cleanup hook in the PARENT so the child inherits it across
        // fork (avoids taking the registration lock post-fork).  It must fire,
        // and fire before the crash report is printed.
        const int markerHandle = CrashHandler::addCleanupHandler(&crashMarkerHook, nullptr);
        CHECK(markerHandle >= 0);

        pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
                // CHILD — only async-signal-safe calls before the crash, since
                // we may have forked a multithreaded runner.  Suppress the core
                // dump the re-raised signal would produce, redirect stderr to a
                // file we can inspect, then crash for real.
                struct rlimit rl;
                rl.rlim_cur = 0;
                rl.rlim_max = 0;
                setrlimit(RLIMIT_CORE, &rl);
                int efd = ::open(errPath.cstr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (efd >= 0) {
                        dup2(efd, STDERR_FILENO);
                        ::close(efd);
                }
                ::raise(SIGABRT);
                _exit(0); // not reached — handler re-raises with SIG_DFL
        }

        int status = 0;
        REQUIRE(waitpid(pid, &status, 0) == pid);
        CHECK(WIFSIGNALED(status));

        // Exactly one crash log should exist in the scratch dir.
        List<FilePath> logs = scratch.entryList("promeki-crash-*.log");
        REQUIRE(logs.size() == 1);

        const String log = slurp(logs[0]);
        CHECK(log.find("=== CRASH") != String::npos);
        CHECK(log.find("--- Stack Trace ---") != String::npos);
        CHECK(log.find("=== END OF REPORT ===") != String::npos);

        // The child's stderr must announce the log path before the crash
        // header, and the announced path must be the file we found.
        const String err = slurp(FilePath(errPath));
        const size_t announce = err.find("Writing crash log to:");
        const size_t header = err.find("=== CRASH");
        CHECK(announce != String::npos);
        CHECK(header != String::npos);
        CHECK(announce < header);
        CHECK(err.find(logs[0].toString()) != String::npos);

        // The cleanup hook must have run, and before any crash report output.
        const size_t marker = err.find("CLEANUP-HOOK-RAN");
        CHECK(marker != String::npos);
        CHECK(marker < announce);

        // Cleanup + restore global state for later tests.
        CrashHandler::removeCleanupHandler(markerHandle);
        scratch.removeRecursively();
        LibraryOptions::instance().set(LibraryOptions::CrashLogDir, savedDir);
        LibraryOptions::instance().set(LibraryOptions::CrashStackTraceTimeout, savedTimeout);
        CrashHandler::install(); // rebuild snapshot for the restored options
        if (!wasInstalled) CrashHandler::uninstall();
}

#endif // PROMEKI_PLATFORM_POSIX
