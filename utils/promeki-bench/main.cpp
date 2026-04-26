/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * promeki-bench — unified benchmark driver for libpromeki.
 *
 * The driver itself is suite-agnostic.  Every flag it exposes is
 * generic: filter, output, baseline, measurement window, warmup,
 * repeat count, and a single `-p/--param key[=value|+=value]` that
 * lets suites consume whatever knobs they need from a shared
 * `BenchParams` bag.  Suites live under `cases/*.cpp` and register
 * themselves through a per-suite hook declared in `cases/cases.h`.
 *
 * To add a new suite:
 *   1. Add `cases/<name>.cpp` with a `register<Name>Cases()` function
 *      that reads any `<name>.*` params out of `BenchParams` and
 *      calls `BenchmarkRunner::registerCase()` for each case.
 *   2. Declare `register<Name>Cases()` and `<Name>ParamHelp()` in
 *      `cases/cases.h`.
 *   3. Add calls to both in `registerAllSuites()` and in the help
 *      text block inside `printHelp()` below.
 *   4. Append the suite's source file to
 *      `utils/promeki-bench/CMakeLists.txt`.
 */

#include "benchparams.h"
#include "cases/cases.h"

#include <promeki/benchmarkrunner.h>
#include <promeki/cmdlineparser.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/buildinfo.h>
#include <promeki/duration.h>
#include <promeki/logger.h>
#include <cstdio>

using namespace promeki;
using benchutil::benchParams;

namespace {

        /**
 * @brief Returns true if @p typeStr is a build type suitable for benchmarking.
 *
 * `Release`, `RelWithDebInfo`, and `MinSizeRel` all produce optimized
 * code (`-O2`/`-O3`/`-Os` plus `NDEBUG`), close enough to release
 * performance that benchmark numbers are meaningful.  `Debug` and
 * an empty string (no `CMAKE_BUILD_TYPE` set — no optimization flags
 * added automatically) are flagged.
 */
        bool isOptimizedBuildType(const String &typeStr) {
                if (typeStr == "Release") return true;
                if (typeStr == "RelWithDebInfo") return true;
                if (typeStr == "MinSizeRel") return true;
                return false;
        }

        /**
 * @brief Returns true if the promeki-bench binary was itself built optimized.
 *
 * `NDEBUG` is the canonical cross-compiler signal that optimization
 * is enabled: CMake defines it for `Release`, `RelWithDebInfo`, and
 * `MinSizeRel`, and leaves it off for `Debug`.  The detection
 * happens at compile time of this translation unit, so it reflects
 * the build flags used for promeki-bench itself — independent of
 * whatever mode the library was built in.
 */
        constexpr bool benchBinaryIsOptimized() {
#ifdef NDEBUG
                return true;
#else
                return false;
#endif
        }

        /**
 * @brief Prints warning lines when the library or bench binary is unoptimized.
 *
 * Called by `main()` right after the banner so the user sees the
 * warning before any benchmark cases run — debug-build timings are
 * typically 5-10× slower than release and would silently mislead
 * anyone comparing results against a baseline.  Goes to stdout (not
 * stderr) so the warning sits chronologically between the banner
 * and the case progress, regardless of pipe buffering.
 */
        void printBuildWarnings() {
                const BuildInfo *bi = getBuildInfo();
                String           libType = (bi && bi->type && bi->type[0]) ? String(bi->type) : String();

                bool libOk = isOptimizedBuildType(libType);
                bool benchOk = benchBinaryIsOptimized();
                if (libOk && benchOk) return;

                if (!libOk) {
                        if (libType.isEmpty()) {
                                std::printf("  !! libpromeki build type is unset — results will likely not\n"
                                            "  !! reflect release performance.  Reconfigure with\n"
                                            "  !! -DCMAKE_BUILD_TYPE=Release before benchmarking.\n");
                        } else {
                                std::printf("  !! libpromeki was built as '%s' — results will not reflect\n"
                                            "  !! release performance.  Reconfigure with\n"
                                            "  !! -DCMAKE_BUILD_TYPE=Release before benchmarking.\n",
                                            libType.cstr());
                        }
                }
                if (!benchOk) {
                        std::printf("  !! promeki-bench was compiled without NDEBUG — the driver itself\n"
                                    "  !! is running unoptimized and will inflate per-case overhead.\n");
                }
                std::putchar('\n');
        }

        /**
 * @brief Registers every suite's cases after BenchParams is populated.
 *
 * Each case file exposes a `register*Cases()` entry point via
 * `cases/cases.h`.  New suites land here as they are written.
 */
        void registerAllSuites() {
                benchutil::registerCscCases();
                benchutil::registerImageDataCases();
                benchutil::registerInspectorCases();
        }

        /**
 * @brief Prints top-level usage plus per-suite parameter help.
 *
 * Leads with a short synopsis, follows with the generic flag list
 * produced by `CmdLineParser::generateUsage()`, then appends each
 * suite's parameter reference block.  Suites contribute their own
 * block so the help stays accurate without driver edits.
 */
        void printHelp(const CmdLineParser &parser) {
                std::printf("Usage: promeki-bench [options]\n"
                            "\n"
                            "Library-native benchmark driver.  Every registered case runs unless\n"
                            "filtered out; pass -l / --list to see the available cases and -p\n"
                            "to supply per-suite parameters.\n"
                            "\n"
                            "Options:\n");
                StringList lines = parser.generateUsage();
                for (const auto &line : lines) {
                        std::printf("  %s\n", line.cstr());
                }
                std::printf("\n"
                            "Parameters (-p key[=value|+=value]):\n"
                            "  Parameters are interpreted by individual suites.  `key=value`\n"
                            "  sets a scalar (last write wins); `key+=value` appends to a list.\n"
                            "  Bare `-p key` stores an empty string (useful as a flag).\n"
                            "\n");
                std::fputs(benchutil::cscParamHelp().cstr(), stdout);
                std::putchar('\n');
                std::fputs(benchutil::imageDataParamHelp().cstr(), stdout);
                std::putchar('\n');
                std::fputs(benchutil::inspectorParamHelp().cstr(), stdout);
                std::printf("\n"
                            "Examples:\n"
                            "  promeki-bench                          # run every registered case\n"
                            "  promeki-bench -l                       # list cases and exit\n"
                            "  promeki-bench -f 'csc\\\\.RGBA.*'         # regex filter by suite.name\n"
                            "  promeki-bench -p csc.width=3840 -p csc.height=2160\n"
                            "  promeki-bench -p csc.src+=RGBA8_sRGB -p csc.dst+=YUV8_422_Rec709\n"
                            "  promeki-bench -p csc.config.CscPath=Scalar\n"
                            "  promeki-bench -o run.json              # write JSON\n"
                            "  promeki-bench -b previous.json         # compare against baseline\n");
        }

        /**
 * @brief Prints every registered case as a columnized table to stdout.
 */
        void listCases() {
                const List<BenchmarkCase> &cases = BenchmarkRunner::registeredCases();
                std::printf("Registered benchmark cases (%d):\n\n", static_cast<int>(cases.size()));
                std::fputs(BenchmarkRunner::formatRegisteredCases().cstr(), stdout);
        }

} // namespace

int main(int argc, char **argv) {
        // Silence info-level library logs.  Library backends (TPG,
        // Inspector, image loaders, etc.) emit configuration dumps
        // and per-frame status at info — perfectly useful in
        // production but pure noise inside a measurement tool that
        // wants a clean numbers-only output.  Warnings and errors
        // still come through so a benched case that breaks is
        // visible.  Errors logged via promekiErr (e.g. an open()
        // failure inside a case) still surface, and the per-case
        // "invalid" counter catches anything else.
        Logger::defaultLogger().setLogLevel(Logger::Warn);

        String       outputPath;
        String       baselinePath;
        String       filter;
        unsigned int minTimeMs = 500;
        unsigned int warmupMs = 100;
        unsigned int repeats = 1;
        bool         quiet = false;
        bool         showHelp = false;
        bool         listBenches = false;

        CmdLineParser parser;
        parser.registerOptions({
                {'h', "help", "Show this help text and exit", CmdLineParser::OptionCallback([&]() {
                         showHelp = true;
                         return 0;
                 })},
                {'l', "list", "List every registered benchmark case and exit", CmdLineParser::OptionCallback([&]() {
                         listBenches = true;
                         return 0;
                 })},
                {'f', "filter", "Regex (ECMAScript) matched against 'suite.name'",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         filter = s;
                         return 0;
                 })},
                {'o', "output", "Write full JSON results to this file",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         outputPath = s;
                         return 0;
                 })},
                {'b', "baseline", "Compare results against a previously-saved JSON baseline",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         baselinePath = s;
                         return 0;
                 })},
                {'t', "min-time", "Target measurement window per case in ms (default: 500)",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         if (v > 0) minTimeMs = static_cast<unsigned int>(v);
                         return 0;
                 })},
                {'W', "warmup", "Warmup window per case in ms (default: 100)",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         if (v > 0) warmupMs = static_cast<unsigned int>(v);
                         return 0;
                 })},
                {'r', "repeats", "Number of measurement runs per case (default: 1)",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         if (v > 0) repeats = static_cast<unsigned int>(v);
                         return 0;
                 })},
                {'q', "quiet", "Suppress per-case progress output", CmdLineParser::OptionCallback([&]() {
                         quiet = true;
                         return 0;
                 })},
                {'p', "param", "Set a case parameter (key=value, key+=value, or bare key)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         Error e = benchParams().parseArg(s);
                         if (e.isError()) {
                                 std::fprintf(stderr, "promeki-bench: invalid -p argument '%s': %s\n", s.cstr(),
                                              e.name().cstr());
                                 return 1;
                         }
                         return 0;
                 })},
        });

        int ret = parser.parseMain(argc, argv);
        if (ret != 0) return ret;

        if (showHelp) {
                printHelp(parser);
                return 0;
        }

        // Register every suite's cases after BenchParams is populated
        // from the command line so parameter-driven registration
        // decisions (e.g. CSC cross-product) see the final values.
        registerAllSuites();

        if (listBenches) {
                listCases();
                return 0;
        }

        // Configure the runner.
        BenchmarkRunner runner;
        runner.setMinTimeMs(minTimeMs);
        runner.setWarmupMs(warmupMs);
        runner.setRepeats(repeats);
        runner.setVerbose(!quiet);
        if (!filter.isEmpty()) runner.setFilter(filter);

        // Self-identifying banner so logs can be recreated later.  We
        // also show how many cases actually match the current filter
        // and an estimated run time so the user can decide whether a
        // 5-second run or a 2-hour run is about to start.
        int    totalRegistered = static_cast<int>(BenchmarkRunner::registeredCases().size());
        int    matching = runner.filteredCaseCount();
        String estimate = Duration::fromMilliseconds(runner.estimatedDurationMs()).toString();

        const BuildInfo *bi = getBuildInfo();
        std::printf("promeki-bench\n");
        if (bi && bi->repoident) std::printf("  build:      %s\n", bi->repoident);
        {
                String      libType = (bi && bi->type && bi->type[0]) ? String(bi->type) : String("(unset)");
                const char *benchType = benchBinaryIsOptimized() ? "NDEBUG" : "debug";
                std::printf("  build type: libpromeki=%s, promeki-bench=%s\n", libType.cstr(), benchType);
        }
        std::printf("  min_time:   %u ms\n", minTimeMs);
        std::printf("  warmup:     %u ms\n", warmupMs);
        std::printf("  repeats:    %u\n", repeats);
        if (!filter.isEmpty()) {
                std::printf("  filter:     %s\n", filter.cstr());
                std::printf("  cases:      %d of %d (after filter)\n", matching, totalRegistered);
        } else {
                std::printf("  cases:      %d\n", matching);
        }
        std::printf("  est. time:  ~%s\n", estimate.cstr());
        std::putchar('\n');

        // If either half of the pipeline is a non-release build, tell
        // the user loudly before any measurements start.  Numbers from
        // a debug run will silently drift far from release performance
        // and mislead anyone comparing against a baseline.
        printBuildWarnings();

        Error runErr = runner.runAll();
        if (runErr.isError()) {
                std::fprintf(stderr, "promeki-bench: runAll returned %s\n", runErr.name().cstr());
        }

        // Human-readable table.
        std::putchar('\n');
        std::fputs(runner.formatTable().cstr(), stdout);

        // Optional baseline comparison.
        if (!baselinePath.isEmpty()) {
                Error                 baseErr;
                List<BenchmarkResult> base = BenchmarkRunner::loadBaseline(baselinePath, &baseErr);
                if (baseErr.isError()) {
                        std::fprintf(stderr, "promeki-bench: could not load baseline '%s': %s\n", baselinePath.cstr(),
                                     baseErr.name().cstr());
                } else {
                        std::printf("\nBaseline comparison vs %s\n", baselinePath.cstr());
                        std::fputs(runner.formatComparison(base).cstr(), stdout);
                }
        }

        // Optional JSON output.
        if (!outputPath.isEmpty()) {
                Error wErr = runner.writeJson(outputPath);
                if (wErr.isError()) {
                        std::fprintf(stderr, "promeki-bench: failed to write '%s': %s\n", outputPath.cstr(),
                                     wErr.name().cstr());
                        return 1;
                }
                std::printf("\nResults written to %s\n", outputPath.cstr());
        }

        return 0;
}
