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

        // Enable core dumps and install the library crash handler
        // before running any tests.  Using the library's own
        // CrashHandler means doctest runs exercise the same crash-
        // reporting path our applications use — any bug in the
        // handler will show up here first.  Core dumps are enabled
        // by flipping the LibraryOptions flag before install(),
        // which honours it.
        LibraryOptions::instance().set(LibraryOptions::CoreDumps, true);
        CrashHandler::install();

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
