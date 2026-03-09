#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <promeki/logger.h>
#include <promeki/streamstring.h>

using namespace promeki;

int main(int argc, char **argv) {
        promekiLogSync();

        StreamString logbuf([](String &line) {
                promekiInfo("%s", line.cstr());
                return true;
        });

        doctest::Context context;
        context.applyCommandLine(argc, argv);
        context.setCout(&logbuf.stream());
        int res = context.run();

        promekiLogSync();
        return res;
}
