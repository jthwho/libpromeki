#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <promeki/logger.h>
#include <promeki/streamstring.h>

using namespace promeki;

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
                        [](const DateTime &, Logger::LogLevel, const char *, int, const String &msg) -> String {
                                return msg;
                        }
                );
        }
        promekiLogSync();

        StreamString logbuf([](String &line) {
                promekiInfo("%s", line.cstr());
                return true;
        });

        doctest::Context context;
        context.applyCommandLine(filteredArgc, filteredArgv);
        context.setCout(&logbuf.stream());
        int res = context.run();

        promekiLogSync();
        delete[] filteredArgv;
        return res;
}
