/**
 * @file      main.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * promeki-test — functional-test driver for libpromeki.
 *
 * The driver itself is suite-agnostic.  It enumerates every
 * @ref TestCase the registered case files produced, filters by an
 * optional regex against the dot-separated case name, and runs each
 * surviving case in turn.  Per-case it:
 *
 *   1. Builds @c <BaseFolder>/<safe(name)>/ on disk.
 *   2. Routes the global Logger into @c <TestFolder>/test.log so the
 *      per-case noise is scoped to its own folder.
 *   3. Builds a @ref TestContext with the resolved @ref TestParams
 *      (every CLI knob plus @c BaseFolder, @c TestFolder, @c LogFile).
 *   4. Times the test with @ref ElapsedTimer and records the
 *      pass / fail / skip / timeout outcome.
 *   5. Writes @c <TestFolder>/result.json with the outcome and any
 *      details the test recorded via @ref TestContext::setDetail.
 *
 * After every case has run, the driver writes @c <BaseFolder>/summary.json
 * with the full matrix and prints a human-readable summary table.
 *
 * Adding a new suite:
 *   - Add @c cases/<name>.cpp with a @c register<Name>Cases() function
 *     that calls @c TestRunner::registerCase(...) for each case.
 *   - Declare @c register<Name>Cases() in @c cases/cases.h.
 *   - Add the call to @c registerAllSuites() below.
 *   - Append the source file to @c CMakeLists.txt.
 */

#include "cases/cases.h"
#include "testcontext.h"
#include "testmedia.h"
#include "testparams.h"
#include "testrunner.h"

#include <promeki/application.h>
#include <promeki/cmdlineparser.h>
#include <promeki/datetime.h>
#include <promeki/dir.h>
#include <promeki/elapsedtimer.h>
#include <promeki/error.h>
#include <promeki/filepath.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/objectbase.tpp>
#include <promeki/regex.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

using namespace promeki;
using namespace promeki::promekitest;

namespace {

        // ---------------------------------------------------------------------------
        // CLI options
        // ---------------------------------------------------------------------------

        struct Options {
                        StringList regexes;            // --regex, repeatable
                        String     baseFolder;         // --base, default = Dir::temp() / promeki-test-<ts>
                        String     testmediaPath;      // --testmedia, default = resolved via env / source dir
                        int32_t    frames = 30;        // --frames
                        int32_t    phaseTimeoutMs = 10000; // --timeout-ms
                        bool       verbose = false;    // --verbose
                        bool       listOnly = false;   // --list
                        bool       logConsole = false; // --log-console
                        // Free-form key=value overrides set via -p / --param.
                        // Stored as (key, String value) — the per-test param
                        // database stamps each as a String entry; the test
                        // body is responsible for parsing the value into
                        // whatever type it actually wants (PixelFormat,
                        // VideoFormat, …).
                        StringList paramKeys;
                        StringList paramValues;
        };

        // ---------------------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------------------

        // Filesystem-safe rendering of a dot-separated test name.  The
        // dots themselves are kept (the per-folder name then mirrors
        // the test id for easy mental mapping); colons and slashes are
        // collapsed to underscores so a case like
        // @c "roundtrip.quicktime.mov.h264" lands at
        // @c <BaseFolder>/roundtrip.quicktime.mov.h264/.
        String safeFolderName(const String &caseName) {
                String out = caseName;
                out = out.replace(String(":"), String("_"));
                out = out.replace(String("/"), String("_"));
                out = out.replace(String("\\"), String("_"));
                out = out.replace(String(" "), String("_"));
                return out;
        }

        const char *statusWord(TestStatus s) {
                switch (s) {
                        case TestStatus::Pass: return "PASS";
                        case TestStatus::Fail: return "FAIL";
                        case TestStatus::Skip: return "SKIP";
                        case TestStatus::Timeout: return "TIME";
                }
                return "?";
        }

        // Color helpers — only emit ANSI when stdout is a TTY so log
        // captures stay clean.  Mirrors the precedent set by
        // scripts/roundtrip-codecs.sh.
        struct Colors {
                        const char *red = "";
                        const char *green = "";
                        const char *yellow = "";
                        const char *reset = "";
        };
        Colors makeColors() {
                Colors c;
                if (std::getenv("NO_COLOR") == nullptr && isatty(fileno(stdout))) {
                        c.red = "\033[0;31m";
                        c.green = "\033[0;32m";
                        c.yellow = "\033[0;33m";
                        c.reset = "\033[0m";
                }
                return c;
        }

        const char *colorFor(TestStatus s, const Colors &c) {
                switch (s) {
                        case TestStatus::Pass: return c.green;
                        case TestStatus::Fail: return c.red;
                        case TestStatus::Skip: return c.yellow;
                        case TestStatus::Timeout: return c.red;
                }
                return c.reset;
        }

        // Build the default @c BaseFolder.  We follow the user's
        // memory note and route through @ref Dir::temp so the
        // @ref LibraryOptions::TempDir override (pinned to
        // @c /mnt/data/tmp/promeki on this machine) wins; the
        // timestamp suffix keeps successive runs from clobbering each
        // other while still being human-readable.
        String defaultBaseFolder() {
                FilePath base = Dir::temp().path();
                String   stamp = DateTime::now().toString("%Y%m%d_%H%M%S");
                return (base / (String("promeki-test-") + stamp)).toString();
        }

        // Parse argv via the project's CmdLineParser.  Returns the
        // exit code from parseMain (0 on success, non-zero if the
        // parser detected an error).  The local registry of options
        // is built fresh on each call so the closures capture the
        // right Options instance.
        int parseOptions(int argc, char **argv, Options &opts, bool &showHelp) {
                CmdLineParser parser;
                parser.registerOptions({
                        {'h', "help", "Show this help text and exit",
                         CmdLineParser::OptionCallback([&]() {
                                 showHelp = true;
                                 return 0;
                         })},
                        {'l', "list", "Print every registered case (after filtering) and exit",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.listOnly = true;
                                 return 0;
                         })},
                        {'k', "regex", "Regex (ECMAScript) matched against dotted case name; repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.regexes.pushToBack(s);
                                 return 0;
                         })},
                        {'b', "base", "Base folder for per-test scratch space "
                                      "(default: Dir::temp()/promeki-test-<timestamp>)",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.baseFolder = s;
                                 return 0;
                         })},
                        {'m', "testmedia",
                         "Path to the testmedia/ corpus root (default: $PROMEKI_TESTMEDIA, "
                         "else <source>/testmedia symlink).  Data-driven suites skip when no "
                         "candidate is usable.",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.testmediaPath = s;
                                 return 0;
                         })},
                        {'n', "frames", "Frame count for tests that consume one (default: 30)",
                         CmdLineParser::OptionIntCallback([&](int v) {
                                 if (v <= 0) {
                                         std::fprintf(stderr, "promeki-test: --frames must be > 0\n");
                                         return 1;
                                 }
                                 opts.frames = static_cast<int32_t>(v);
                                 return 0;
                         })},
                        {'t', "timeout-ms", "Per-phase watchdog timeout in milliseconds (default: 10000)",
                         CmdLineParser::OptionIntCallback([&](int v) {
                                 if (v < 100) {
                                         std::fprintf(stderr, "promeki-test: --timeout-ms must be >= 100\n");
                                         return 1;
                                 }
                                 opts.phaseTimeoutMs = static_cast<int32_t>(v);
                                 return 0;
                         })},
                        {'v', "verbose", "Verbose logging (sets Logger to Debug)",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.verbose = true;
                                 return 0;
                         })},
                        {0, "log-console", "Keep stderr Logger output enabled (default: silenced)",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.logConsole = true;
                                 return 0;
                         })},
                        {'p', "param", "Set a per-test parameter (key=value); repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 // Split on the first '='.  Bare "-p key"
                                 // (no equals) is allowed and stores an
                                 // empty string — useful as a flag-style
                                 // toggle for tests that read the key by
                                 // presence.
                                 const char *raw = s.cstr();
                                 const size_t len = s.byteCount();
                                 size_t       eq = len;
                                 for (size_t i = 0; i < len; ++i) {
                                         if (raw[i] == '=') {
                                                 eq = i;
                                                 break;
                                         }
                                 }
                                 String key = (eq == len) ? s : String(raw, eq);
                                 String val = (eq == len) ? String() : String(raw + eq + 1, len - eq - 1);
                                 if (key.isEmpty()) {
                                         std::fprintf(stderr,
                                                      "promeki-test: -p argument must be 'key' or 'key=value' "
                                                      "(got '%s')\n",
                                                      s.cstr());
                                         return 1;
                                 }
                                 opts.paramKeys.pushToBack(key);
                                 opts.paramValues.pushToBack(val);
                                 return 0;
                         })},
                });
                return parser.parseMain(argc, argv);
        }

        void printHelp() {
                std::printf(
                        "Usage: promeki-test [OPTIONS]\n"
                        "\n"
                        "Functional-test runner for libpromeki.  Every registered case runs\n"
                        "unless filtered out via -k / --regex.  Per-test scratch space and\n"
                        "log files are created under --base (default: Dir::temp()/\n"
                        "promeki-test-<timestamp>); the run summary is written as\n"
                        "summary.json in the same folder.\n"
                        "\n"
                        "Options:\n"
                        "  -h, --help               Show this help text and exit\n"
                        "  -l, --list               Print the (filtered) case list and exit\n"
                        "  -k, --regex PATTERN      Restrict to cases whose dotted name matches\n"
                        "                             PATTERN (RegEx::search, ECMAScript grammar).\n"
                        "                             Repeatable — a case is included if ANY\n"
                        "                             pattern matches it.  Default: run everything.\n"
                        "  -b, --base DIR           Base folder for per-test scratch space\n"
                        "                             (default: Dir::temp()/promeki-test-<timestamp>)\n"
                        "  -m, --testmedia DIR      Path to the testmedia/ corpus root.  Used by\n"
                        "                             data-driven suites (speech-to-text today).\n"
                        "                             Falls back to $PROMEKI_TESTMEDIA, then to\n"
                        "                             <source>/testmedia (the in-tree symlink).\n"
                        "  -n, --frames N           Frames per test for tests that need one (default: 30)\n"
                        "  -t, --timeout-ms MS      Per-phase watchdog timeout (default: 10000)\n"
                        "  -v, --verbose            Verbose logging (Logger = Debug)\n"
                        "      --log-console        Keep stderr Logger output enabled.  By default\n"
                        "                             the runner silences stderr logging so the\n"
                        "                             [PASS|FAIL|SKIP] table stays readable.\n"
                        "  -p, --param KEY=VAL      Set a per-test parameter.  Stored as a String\n"
                        "                             on the test's TestParams; the test reads it\n"
                        "                             via params.getAs<...>().  Repeatable.\n"
                        "\n"
                        "Per-suite parameter keys:\n"
                        "  Codec.TpgPixelFormat=<name>   Pixel format TPG should emit on the\n"
                        "                                  codec.* suite (e.g. YUV8_422_Rec709).\n"
                        "                                  Empty / unset = let TPG pick.\n"
                        "\n"
                        "Examples:\n"
                        "  promeki-test                            # run every registered case\n"
                        "  promeki-test -l                         # list cases and exit\n"
                        "  promeki-test -k '^roundtrip\\.'          # all roundtrip cases\n"
                        "  promeki-test -k 'roundtrip\\.quicktime' # quicktime sub-suite only\n"
                        "  promeki-test -b /tmp/mytest -n 60       # custom base + 60 frames\n"
                        "  promeki-test -k '^codec\\.h264' \\\n"
                        "    -p Codec.TpgPixelFormat=YUV8_422_Rec709  # specific TPG input pix-fmt\n");
        }

        // ---------------------------------------------------------------------------
        // Filtering
        // ---------------------------------------------------------------------------

        List<TestCase> filterCases(const List<TestCase> &all, const StringList &patterns) {
                if (patterns.isEmpty()) return all;
                List<RegEx> compiled;
                for (size_t i = 0; i < patterns.size(); ++i) {
                        compiled.pushToBack(RegEx(patterns[i]));
                }
                List<TestCase> out;
                for (size_t i = 0; i < all.size(); ++i) {
                        bool keep = false;
                        for (size_t j = 0; j < compiled.size(); ++j) {
                                if (compiled[j].search(all[i].name())) {
                                        keep = true;
                                        break;
                                }
                        }
                        if (keep) out.pushToBack(all[i]);
                }
                return out;
        }

        // ---------------------------------------------------------------------------
        // Per-test execution
        // ---------------------------------------------------------------------------

        // Outcome of one test invocation as the driver sees it (the
        // test reports through TestContext, the driver layers on the
        // wall-time and folder bookkeeping).
        struct CaseOutcome {
                        String     name;
                        TestStatus status = TestStatus::Pass;
                        String     message;
                        int64_t    durationMs = 0;
                        FilePath   testFolder;
        };

        // Persist a single test's outcome + every detail it recorded
        // as @c result.json next to its log file.  We use
        // @c std::fopen rather than @ref File for the metadata write
        // so a failure here can't disturb the test's own File-related
        // state, and the JSON helpers already produce a String.
        void writeResultJson(const FilePath &folder, const CaseOutcome &outcome, const TestContext &ctx) {
                JsonObject root;
                root.set("name", outcome.name);
                root.set("status", String(statusWord(outcome.status)));
                if (!outcome.message.isEmpty()) {
                        root.set("message", outcome.message);
                }
                root.set("durationMs", outcome.durationMs);

                JsonObject details;
                ctx.details().forEach([&](const String &key, const Variant &val) {
                        details.setFromVariant(key, val);
                });
                root.set("details", details);

                // The resolved pipeline graph — every planner-injected
                // stage, every per-stage MediaConfig, every route — is
                // the most useful diagnostic when a test fails.  Tests
                // record it via @ref TestContext::setPipelineConfig and
                // we embed it verbatim under a top-level @c "pipeline"
                // key.  Empty when the test didn't drive a pipeline (or
                // failed before build), in which case we omit the key
                // to keep the JSON tidy.
                if (ctx.pipelineConfig().size() > 0) {
                        root.set("pipeline", ctx.pipelineConfig());
                }

                String  path = (folder / String("result.json")).toString();
                FILE   *fp = std::fopen(path.cstr(), "w");
                if (fp == nullptr) {
                        promekiWarn("promeki-test: could not open '%s' for write: %s", path.cstr(),
                                    std::strerror(errno));
                        return;
                }
                String text = root.toString(2);
                std::fwrite(text.cstr(), 1, text.byteCount(), fp);
                std::fputc('\n', fp);
                std::fclose(fp);
        }

        // Drive one test: build its folder, point the Logger at the
        // per-test log, run the case, capture wall time, persist the
        // result, and return a summary record for the run-level
        // dump.  The Logger is left pointing at this case's log on
        // exit; the caller switches it to the next case (or to a
        // run-level catch-all path) before the next invocation.
        CaseOutcome runOne(const TestCase &c, const Options &opts, const String &baseFolder,
                           const String &testmediaRoot) {
                CaseOutcome o;
                o.name = c.name();

                FilePath testFolder = FilePath(baseFolder) / safeFolderName(c.name());
                o.testFolder = testFolder;

                Dir d(testFolder);
                if (!d.exists()) {
                        Error me = d.mkpath();
                        if (me.isError()) {
                                o.status = TestStatus::Fail;
                                o.message = String("could not create test folder '") + testFolder.toString() +
                                            String("': ") + me.desc();
                                return o;
                        }
                }

                String logFile = (testFolder / String("test.log")).toString();
                Logger::defaultLogger().setLogFile(logFile);

                TestParams params;
                params.set(TestParams::BaseFolder, baseFolder);
                params.set(TestParams::TestFolder, testFolder.toString());
                params.set(TestParams::LogFile, logFile);
                params.set(TestParams::Verbose, opts.verbose);
                params.set(TestParams::Frames, opts.frames);
                params.set(TestParams::PhaseTimeoutMs, opts.phaseTimeoutMs);
                params.set(TestParams::TestMediaRoot, testmediaRoot);

                // Apply CLI -p / --param overrides last so they win
                // over the well-known defaults above.  Validation is
                // dropped to None for the override pass since the
                // overrides arrive as raw Strings — the receiving test
                // is responsible for parsing each value into whatever
                // native type it expects.  We restore Strict mode
                // before handing the params off to the test, so any
                // strongly-typed sets the test does itself still
                // validate.
                if (!opts.paramKeys.isEmpty()) {
                        params.setValidation(SpecValidation::None);
                        for (size_t i = 0; i < opts.paramKeys.size(); ++i) {
                                params.set(TestParams::ID(opts.paramKeys[i]), opts.paramValues[i]);
                        }
                        params.setValidation(SpecValidation::Strict);
                }

                TestContext ctx(params, testFolder);

                promekiInfo("=== BEGIN test : %s ===", c.name().cstr());
                ElapsedTimer timer;
                c.invoke(ctx);
                o.durationMs = timer.elapsed();
                promekiInfo("=== END   test : %s -> %s%s%s (%lld ms) ===", c.name().cstr(),
                            statusWord(ctx.status()), ctx.message().isEmpty() ? "" : " : ",
                            ctx.message().cstr(), (long long)o.durationMs);

                o.status = ctx.status();
                o.message = ctx.message();

                writeResultJson(testFolder, o, ctx);
                return o;
        }

        // ---------------------------------------------------------------------------
        // Reporting
        // ---------------------------------------------------------------------------

        void printCaseLine(const CaseOutcome &o, size_t nameWidth, const Colors &c) {
                std::printf("  [%s%-4s%s] %-*s  %lld ms", colorFor(o.status, c), statusWord(o.status), c.reset,
                            (int)nameWidth, o.name.cstr(), (long long)o.durationMs);
                if (!o.message.isEmpty()) {
                        std::printf("  (%s)", o.message.cstr());
                }
                std::putchar('\n');
                std::fflush(stdout);
        }

        void writeSummaryJson(const String &baseFolder, const List<CaseOutcome> &outcomes,
                              const Options &opts) {
                JsonObject root;
                root.set("baseFolder", baseFolder);
                root.set("started", DateTime::now().toString());
                root.set("frames", opts.frames);
                root.set("phaseTimeoutMs", opts.phaseTimeoutMs);
                root.set("verbose", opts.verbose);

                JsonArray cases;
                int       passed = 0;
                int       failed = 0;
                int       skipped = 0;
                int       timedOut = 0;
                for (size_t i = 0; i < outcomes.size(); ++i) {
                        const CaseOutcome &o = outcomes[i];
                        JsonObject         row;
                        row.set("name", o.name);
                        row.set("status", String(statusWord(o.status)));
                        if (!o.message.isEmpty()) row.set("message", o.message);
                        row.set("durationMs", o.durationMs);
                        row.set("folder", o.testFolder.toString());
                        cases.add(row);
                        switch (o.status) {
                                case TestStatus::Pass: ++passed; break;
                                case TestStatus::Fail: ++failed; break;
                                case TestStatus::Skip: ++skipped; break;
                                case TestStatus::Timeout: ++timedOut; break;
                        }
                }
                root.set("cases", cases);

                JsonObject totals;
                totals.set("total", static_cast<int>(outcomes.size()));
                totals.set("passed", passed);
                totals.set("failed", failed);
                totals.set("skipped", skipped);
                totals.set("timedOut", timedOut);
                root.set("totals", totals);

                String path = (FilePath(baseFolder) / String("summary.json")).toString();
                FILE  *fp = std::fopen(path.cstr(), "w");
                if (fp == nullptr) {
                        std::fprintf(stderr, "promeki-test: could not open '%s' for write: %s\n", path.cstr(),
                                     std::strerror(errno));
                        return;
                }
                String text = root.toString(2);
                std::fwrite(text.cstr(), 1, text.byteCount(), fp);
                std::fputc('\n', fp);
                std::fclose(fp);
        }

} // namespace

int main(int argc, char **argv) {
        Application app(argc, argv);
        Application::setAppName(String("promeki-test"));

        Options opts;
        bool    showHelp = false;
        int     parseRc = parseOptions(argc, argv, opts, showHelp);
        if (parseRc != 0) return parseRc;
        if (showHelp) {
                printHelp();
                return 0;
        }

        // Logger wiring.  Default behaviour matches the legacy
        // roundtrip-functest:
        //   - console (stderr) logging silenced so the per-case
        //     summary table isn't drowned out by backend chatter
        //   - per-test file sinks land inside each test folder
        //     (set inside @c runOne)
        //   - --log-console restores the stderr sink for interactive
        //     debugging
        //   - --verbose drops the level to Debug
        Logger &logger = Logger::defaultLogger();
        if (!opts.logConsole) logger.setConsoleLoggingEnabled(false);
        if (opts.verbose) logger.setLogLevel(Logger::LogLevel::Debug);

        // Resolve base folder.  Empty --base means "default", which
        // picks up the @ref LibraryOptions::TempDir pin via
        // @ref Dir::temp.  We mkpath the base eagerly so the very
        // first thing the user sees if @c --base points at an
        // unwritable spot is a clean error, not a per-case mkpath
        // failure spam.
        String baseFolder = opts.baseFolder.isEmpty() ? defaultBaseFolder() : opts.baseFolder;
        Dir    baseDir(FilePath{baseFolder});
        if (!baseDir.exists()) {
                Error e = baseDir.mkpath();
                if (e.isError()) {
                        std::fprintf(stderr, "promeki-test: mkpath '%s' failed: %s\n", baseFolder.cstr(),
                                     e.desc().cstr());
                        return 1;
                }
        }

        // Resolve the testmedia corpus root once, before suite
        // registration.  Empty means "no candidate usable" and any
        // data-driven suites skip themselves with a one-liner.
        // The resolved string is also stamped onto every TestParams
        // (see runOne) so individual cases have a single source of
        // truth for the corpus location.
        FilePath     testmediaRoot = resolveTestMediaRoot(opts.testmediaPath);
        const String testmediaRootStr = testmediaRoot.toString();

        // Suite registration happens after argv parsing so suites
        // can read TestParams defaults (frame count, etc.) when
        // deciding which cases to register.
        registerRoundtripCases();
        registerCodecCases();
        registerAudioCases();
        registerQuickTimeAudioCases();
        registerQuickTimeVideoCases();
        registerFfmpegVideoCases();
        registerCaptionsCases();
        registerFrameBridgeCases();
        registerRtpCases();
        registerRtpFfmpegCases();
        registerRtpChaosCases();
        registerNdiCases();
        registerVideoCarrierCases();
        registerTranscriptionCases(testmediaRoot);

        const List<TestCase> &all = TestRunner::registeredCases();
        List<TestCase>        cases = filterCases(all, opts.regexes);

        if (cases.isEmpty()) {
                std::fprintf(stderr, "promeki-test: no cases matched (registry has %zu candidate%s; "
                                     "%zu regex filter%s)\n",
                             (size_t)all.size(), all.size() == 1 ? "" : "s",
                             (size_t)opts.regexes.size(), opts.regexes.size() == 1 ? "" : "s");
                return 1;
        }

        if (opts.listOnly) {
                std::printf("%zu case%s:\n", (size_t)cases.size(), cases.size() == 1 ? "" : "s");
                for (size_t i = 0; i < cases.size(); ++i) {
                        std::printf("  %s", cases[i].name().cstr());
                        if (!cases[i].description().isEmpty()) {
                                std::printf("  -- %s", cases[i].description().cstr());
                        }
                        std::putchar('\n');
                }
                return 0;
        }

        // Pre-compute the widest case name so the per-case summary
        // line stays aligned regardless of suite mix.
        size_t nameWidth = 0;
        for (size_t i = 0; i < cases.size(); ++i) {
                if (cases[i].name().size() > nameWidth) nameWidth = cases[i].name().size();
        }

        std::printf("promeki-test\n");
        std::printf("  base folder:  %s\n", baseFolder.cstr());
        if (!testmediaRootStr.isEmpty()) {
                std::printf("  testmedia:    %s\n", testmediaRootStr.cstr());
        } else if (!opts.testmediaPath.isEmpty()) {
                std::printf("  testmedia:    (requested '%s' but no index.json found)\n",
                            opts.testmediaPath.cstr());
        } else {
                std::printf("  testmedia:    (not found — data-driven suites will skip)\n");
        }
        std::printf("  cases:        %zu of %zu (after filter)\n", (size_t)cases.size(), (size_t)all.size());
        std::printf("  frames:       %d\n", (int)opts.frames);
        std::printf("  timeout(ms):  %d\n", (int)opts.phaseTimeoutMs);
        std::putchar('\n');

        Colors             colors = makeColors();
        List<CaseOutcome>  outcomes;
        int                passed = 0;
        int                failed = 0;
        int                skipped = 0;
        int                timedOut = 0;
        for (size_t i = 0; i < cases.size(); ++i) {
                CaseOutcome o = runOne(cases[i], opts, baseFolder, testmediaRootStr);
                outcomes.pushToBack(o);
                printCaseLine(o, nameWidth, colors);
                switch (o.status) {
                        case TestStatus::Pass: ++passed; break;
                        case TestStatus::Fail: ++failed; break;
                        case TestStatus::Skip: ++skipped; break;
                        case TestStatus::Timeout: ++timedOut; break;
                }
        }

        // Restore the Logger to a run-level catch-all so any
        // post-run diagnostics aren't stranded in the last test's
        // log file.
        Logger::defaultLogger().setLogFile((FilePath(baseFolder) / String("promeki-test.log")).toString());

        writeSummaryJson(baseFolder, outcomes, opts);

        std::putchar('\n');
        std::printf("Summary: %d passed, %d failed, %d skipped, %d timed out of %zu\n", passed, failed, skipped,
                    timedOut, (size_t)outcomes.size());
        std::printf("Results: %s/summary.json\n", baseFolder.cstr());

        // Timeouts count as failures for exit purposes — a
        // deadlocked test is a real bug we can't reduce to a clean
        // PASS, so the run shouldn't claim success.
        const bool overallPass = (failed == 0 && timedOut == 0);
        std::printf("RESULT: %s%s%s\n", overallPass ? colors.green : colors.red, overallPass ? "PASS" : "FAIL",
                    colors.reset);
        return overallPass ? 0 : 1;
}
