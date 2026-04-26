#define DOCTEST_CONFIG_IMPLEMENT
#include <ostream>
#include <streambuf>
#include <cstdlib>
#include <doctest/doctest.h>
#include <promeki/crashhandler.h>
#include <promeki/libraryoptions.h>
#include <promeki/logger.h>
#include <promeki/string.h>

using namespace promeki;

/**
 * @brief Minimal std::streambuf adapter for routing doctest output to the logger.
 *
 * This is the only std:: stream usage remaining — isolated to test infrastructure
 * because doctest::Context::setCout() requires a std::ostream*.
 */
class LogStreambuf : public std::streambuf {
        protected:
                int overflow(int ch) override {
                        if (ch == '\n' || ch == EOF) {
                                if (!_line.isEmpty()) {
                                        promekiInfo("%s", _line.cstr());
                                        _line.clear();
                                }
                        } else {
                                _line += static_cast<char>(ch);
                        }
                        return ch;
                }

                int sync() override {
                        if (!_line.isEmpty()) {
                                promekiInfo("%s", _line.cstr());
                                _line.clear();
                        }
                        return 0;
                }

        private:
                String _line;
};

int main(int argc, char **argv) {
        bool   verbose = false;
        bool   useDefaultLogger = false;
        int    filteredArgc = 0;
        char **filteredArgv = new char *[argc];
        for (int i = 0; i < argc; i++) {
                if (std::string(argv[i]) == "--verbose") {
                        verbose = true;
                } else if (std::string(argv[i]) == "--logger") {
                        useDefaultLogger = true;
                        verbose = true;
                } else {
                        filteredArgv[filteredArgc++] = argv[i];
                }
        }

        if (!useDefaultLogger) {
                Logger::defaultLogger().setConsoleFormatter(
                        [](const Logger::LogFormat &fmt) -> String { return fmt.entry->msg; });
        }

        if (!verbose) {
                Logger::defaultLogger().setConsoleLoggingEnabled(false);
                CrashHandler::setConsoleTraceEnabled(false);
        }

        LibraryOptions::instance().set(LibraryOptions::CoreDumps, true);
        CrashHandler::install();

        promekiLogSync();

        LogStreambuf logbuf;
        std::ostream logstream(&logbuf);

        doctest::Context context;
        context.applyCommandLine(filteredArgc, filteredArgv);
        if (verbose) context.setCout(&logstream);
        int res = context.run();

        promekiLogSync();
        delete[] filteredArgv;
        return res;
}
