// Minimal libpromeki consumer.  Uses only core facilities so it links
// against even the leanest feature configuration (e.g. the proav-embedded
// preset).  See examples/downstream/CMakeLists.txt for the build wiring.

#include <cstdio>

#include <promeki/buildident.h>   // PROMEKI_VERIFY_BUILD_IDENT_OR_ABORT()
#include <promeki/buildinfo.h>    // formatBuildInfo()
#include <promeki/string.h>

int main() {
        // Stale-binary guard: abort at startup if this executable was
        // compiled against a different libpromeki than the .so it is now
        // dynamically linked to.  The macro captures this translation
        // unit's compile-time PROMEKI_BUILD_IDENT and compares it to the
        // library's runtime value.  Cheap insurance for downstream apps.
        PROMEKI_VERIFY_BUILD_IDENT_OR_ABORT();

        using namespace promeki;

        const String banner =
                formatBuildInfo("{name} {version}{extra} ({type}, {ref})");
        std::printf("Linked against %s\n", banner.cstr());
        return 0;
}
