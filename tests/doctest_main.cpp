#define DOCTEST_CONFIG_IMPLEMENT
#include <ostream>
#include <streambuf>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/logger.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/util.h>

#if defined(__unix__) || defined(__APPLE__)
#       include <sys/resource.h>
#       include <unistd.h>
#endif

using namespace promeki;

namespace {

#if defined(__unix__) || defined(__APPLE__)

/**
 * @brief Raises the core file size limit as high as the kernel will permit.
 *
 * Doctest tests occasionally crash with intermittent races (e.g. the
 * thread.cpp signalling tests).  Without a core file the only forensic
 * material is the doctest line number, which is rarely enough to
 * understand a SIGSEGV.  Enabling cores at the start of @c main means
 * the next time a test crashes the kernel writes a usable core file
 * (subject to the system @c core_pattern, which is the user's call
 * to configure — Ubuntu's apport pattern silently swallows them).
 *
 * The function logs the resulting core size limit so it's obvious
 * from the run header whether cores are actually available.  No-op
 * (and reports as such) on platforms that lack @c RLIMIT_CORE.
 */
void enableCoreDumps() {
        struct rlimit rl;
        if(getrlimit(RLIMIT_CORE, &rl) != 0) {
                promekiWarn("doctest_main: getrlimit(RLIMIT_CORE) failed: %s",
                            std::strerror(errno));
                return;
        }
        rl.rlim_cur = rl.rlim_max;
        if(setrlimit(RLIMIT_CORE, &rl) != 0) {
                promekiWarn("doctest_main: setrlimit(RLIMIT_CORE) failed: %s",
                            std::strerror(errno));
                return;
        }
        if(rl.rlim_cur == RLIM_INFINITY) {
                promekiInfo("doctest_main: core dumps enabled (unlimited)");
        } else {
                promekiInfo("doctest_main: core dumps enabled (max %llu bytes)",
                            static_cast<unsigned long long>(rl.rlim_cur));
        }
}

/**
 * @brief Crash signal handler — prints a stack trace and re-raises.
 *
 * Logged via @ref promekiStackTrace so the demangled frames land
 * in the same log stream as the rest of the test output.  After
 * dumping the trace the handler restores the default disposition
 * for the offending signal and re-raises it, which triggers the
 * normal kernel core-dump path (provided @ref enableCoreDumps has
 * lifted @c RLIMIT_CORE first).
 *
 * @note This handler is intentionally NOT signal-safe in the strict
 *       POSIX sense — promekiStackTrace allocates and the logger
 *       takes a mutex.  That is acceptable here because we are
 *       already on the path to abnormal termination; a deadlock
 *       inside the handler is no worse than the original crash.
 *       For a production crash reporter the right answer would be
 *       a separate signal-safe writer that emits raw frames to
 *       stderr.
 */
void crashSignalHandler(int signo) {
        const char *name = "UNKNOWN";
        switch(signo) {
                case SIGSEGV: name = "SIGSEGV"; break;
                case SIGABRT: name = "SIGABRT"; break;
                case SIGBUS:  name = "SIGBUS";  break;
                case SIGFPE:  name = "SIGFPE";  break;
                case SIGILL:  name = "SIGILL";  break;
                default: break;
        }
        promekiErr("doctest_main: caught %s (%d) — stack trace:", name, signo);
        StringList frames = promekiStackTrace(true);
        for(size_t i = 0; i < frames.size(); i++) {
                promekiErr("%s", frames[i].cstr());
        }
        promekiLogSync();

        // Restore the default handler and re-raise so the kernel
        // produces a core file.
        std::signal(signo, SIG_DFL);
        std::raise(signo);
}

void installCrashHandlers() {
        std::signal(SIGSEGV, crashSignalHandler);
        std::signal(SIGABRT, crashSignalHandler);
        std::signal(SIGBUS,  crashSignalHandler);
        std::signal(SIGFPE,  crashSignalHandler);
        std::signal(SIGILL,  crashSignalHandler);
}

#else  // non-POSIX platforms

void enableCoreDumps() {
        promekiInfo("doctest_main: core dumps not supported on this platform");
}

void installCrashHandlers() {}

#endif

} // namespace

/**
 * @brief Minimal std::streambuf adapter for routing doctest output to the logger.
 *
 * This is the only std:: stream usage remaining — isolated to test infrastructure
 * because doctest::Context::setCout() requires a std::ostream*.
 */
class LogStreambuf : public std::streambuf {
        protected:
                int overflow(int ch) override {
                        if(ch == '\n' || ch == EOF) {
                                if(!_line.isEmpty()) {
                                        promekiInfo("%s", _line.cstr());
                                        _line.clear();
                                }
                        } else {
                                _line += static_cast<char>(ch);
                        }
                        return ch;
                }

                int sync() override {
                        if(!_line.isEmpty()) {
                                promekiInfo("%s", _line.cstr());
                                _line.clear();
                        }
                        return 0;
                }

        private:
                String _line;
};

int main(int argc, char **argv) {
        // Check for --logger flag before passing args to doctest.
        // When set, the default logger formatters are preserved.
        bool useDefaultLogger = false;
        int filteredArgc = 0;
        char **filteredArgv = new char *[argc];
        for(int i = 0; i < argc; i++) {
                if(std::string(argv[i]) == "--logger") {
                        useDefaultLogger = true;
                } else {
                        filteredArgv[filteredArgc++] = argv[i];
                }
        }

        if(!useDefaultLogger) {
                Logger::defaultLogger().setConsoleFormatter(
                        [](const Logger::LogFormat &fmt) -> String {
                                return fmt.entry->msg;
                        }
                );
        }

        // Lift @c RLIMIT_CORE and install crash signal handlers
        // before running any tests, so the next intermittent
        // segfault leaves a usable core file and a dumped stack
        // trace in the log.  These calls are no-ops on platforms
        // that lack POSIX core-dump infrastructure.
        enableCoreDumps();
        installCrashHandlers();

        promekiLogSync();

        LogStreambuf logbuf;
        std::ostream logstream(&logbuf);

        doctest::Context context;
        context.applyCommandLine(filteredArgc, filteredArgv);
        context.setCout(&logstream);
        int res = context.run();

        promekiLogSync();
        delete[] filteredArgv;
        return res;
}
