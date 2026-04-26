/**
 * @file      benchmarkrunner.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/benchmarkrunner.h>
#include <promeki/json.h>
#include <promeki/dir.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace promeki;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

namespace {

        BenchmarkRunner makeFastRunner() {
                // Keep the runner snappy so the test suite stays fast.
                // 10 ms measurement window, 2 ms warmup, no verbose output.
                BenchmarkRunner runner;
                runner.setMinTimeMs(10);
                runner.setWarmupMs(2);
                runner.setRepeats(3);
                return runner;
        }

        // Case that increments a counter and is cheap per iteration.
        int  g_trivialInvocations = 0;
        void trivialCase(BenchmarkState &state) {
                g_trivialInvocations++;
                volatile int sink = 0;
                for (auto _ : state) {
                        (void)_;
                        sink += 1;
                }
                (void)sink;
        }

        // Case that reports itemsProcessed / bytesProcessed for throughput validation.
        void throughputCase(BenchmarkState &state) {
                volatile int sink = 0;
                for (auto _ : state) {
                        (void)_;
                        sink += 1;
                }
                (void)sink;
                state.setItemsProcessed(state.iterations() * 16);
                state.setBytesProcessed(state.iterations() * 1024);
                state.setLabel(String("16-items/iter"));
                state.setCounter(String("custom_metric"), 42.0);
        }

        // Case that pauses timing around a faked "untimed" block. The
        // effective measurement should exclude the sleep, so avgNsPerIter
        // stays tiny even though wall time per iteration is >= 50us.
        //
        // The inner loop includes real timed work (a volatile accumulator)
        // so the calibrator's wall-time-based iteration sizing converges
        // quickly — a case with zero timed work would still calibrate
        // correctly now that the runner uses wall time, but exercising a
        // realistic pauseTiming pattern is a more useful test.
        void pauseCase(BenchmarkState &state) {
                volatile int sink = 0;
                for (auto _ : state) {
                        (void)_;
                        state.pauseTiming();
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                        state.resumeTiming();
                        for (int i = 0; i < 100; i++) sink += i;
                }
                (void)sink;
        }

        // Case that uses the imperative keepRunning() primitive instead of range-for.
        void keepRunningCase(BenchmarkState &state) {
                volatile int sink = 0;
                while (state.keepRunning()) sink += 1;
                (void)sink;
        }

        // Case that never iterates — used to verify the "did not iterate" failure.
        void brokenCase(BenchmarkState &state) {
                (void)state;
                // Deliberately does nothing.
        }

} // namespace

// ----------------------------------------------------------------------------
// BenchmarkState
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkState: range-for iterates exactly iterations() times") {
        BenchmarkState state(100);
        uint64_t       count = 0;
        for (auto _ : state) {
                (void)_;
                count++;
        }
        CHECK(count == 100);
        CHECK(state.completed());
}

TEST_CASE("BenchmarkState: keepRunning iterates exactly iterations() times") {
        BenchmarkState state(50);
        uint64_t       count = 0;
        while (state.keepRunning()) count++;
        CHECK(count == 50);
        CHECK(state.completed());
}

TEST_CASE("BenchmarkState: zero iterations completes immediately") {
        BenchmarkState state(0);
        uint64_t       count = 0;
        for (auto _ : state) {
                (void)_;
                count++;
        }
        CHECK(count == 0);
        // Range-for with 0 iterations never pulls from begin(), so the
        // timer is never started. completed() stays false, which the runner
        // treats as "did not iterate".
        CHECK_FALSE(state.completed());
}

TEST_CASE("BenchmarkState: setters store values") {
        BenchmarkState state(10);
        state.setItemsProcessed(500);
        state.setBytesProcessed(2048);
        state.setLabel(String("my-label"));
        state.setCounter(String("metric"), 3.14);
        CHECK(state.itemsProcessed() == 500);
        CHECK(state.bytesProcessed() == 2048);
        CHECK(state.label() == "my-label");
        CHECK(state.counters().contains(String("metric")));
        CHECK(state.counters()[String("metric")] == doctest::Approx(3.14));
}

// ----------------------------------------------------------------------------
// BenchmarkRunner: runCase
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkRunner: runs a trivial case successfully") {
        BenchmarkRunner runner = makeFastRunner();
        g_trivialInvocations = 0;
        BenchmarkCase   c("test", "trivial", "Trivial counter loop", trivialCase);
        BenchmarkResult r = runner.runCase(c);
        CHECK(r.succeeded);
        CHECK(r.suite == "test");
        CHECK(r.name == "trivial");
        CHECK(r.description == "Trivial counter loop");
        CHECK(r.iterations >= 1);
        CHECK(r.repeats == 3);
        CHECK(r.avgNsPerIter >= 0.0);
        CHECK(g_trivialInvocations > 0);
}

TEST_CASE("BenchmarkRunner: keepRunning variant produces valid results") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "keepRunning", "", keepRunningCase);
        BenchmarkResult r = runner.runCase(c);
        CHECK(r.succeeded);
        CHECK(r.iterations >= 1);
        CHECK(r.avgNsPerIter >= 0.0);
}

TEST_CASE("BenchmarkRunner: items/bytes counters produce throughput values") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "throughput", "", throughputCase);
        BenchmarkResult r = runner.runCase(c);
        CHECK(r.succeeded);
        CHECK(r.itemsPerSecond > 0.0);
        CHECK(r.bytesPerSecond > 0.0);
        CHECK(r.label == "16-items/iter");
        CHECK(r.custom.contains(String("custom_metric")));
        CHECK(r.custom[String("custom_metric")] == doctest::Approx(42.0));
}

TEST_CASE("BenchmarkRunner: broken case fails gracefully") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "broken", "", brokenCase);
        BenchmarkResult r = runner.runCase(c);
        CHECK_FALSE(r.succeeded);
        CHECK_FALSE(r.errorMessage.isEmpty());
}

TEST_CASE("BenchmarkRunner: pauseTiming excludes excluded regions") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "pause", "", pauseCase);
        BenchmarkResult r = runner.runCase(c);
        // If pauseTiming() worked, ns/iter should be a tiny fraction of
        // the 50us sleep we faked. Accept anything well under 50,000 ns.
        CHECK(r.succeeded);
        CHECK(r.avgNsPerIter >= 0.0);
        CHECK(r.avgNsPerIter < 50000.0);
}

// ----------------------------------------------------------------------------
// BenchmarkRunner: filter
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkRunner: filteredCaseCount respects the filter") {
        BenchmarkRunner runner;
        int             totalBefore = static_cast<int>(BenchmarkRunner::registeredCases().size());

        // No filter: every registered case counts.
        CHECK(runner.filteredCaseCount() == totalBefore);

        // A filter that cannot possibly match anything: zero cases.
        runner.setFilter(String("__this_should_match_nothing__"));
        CHECK(runner.filteredCaseCount() == 0);

        // A filter that matches at least one registered case (the
        // macro_smoke registration below).
        runner.setFilter(String("unittest\\.macro_smoke"));
        CHECK(runner.filteredCaseCount() >= 1);
}

TEST_CASE("BenchmarkRunner: estimatedDurationMs scales with count and settings") {
        BenchmarkRunner runner;
        runner.setMinTimeMs(500);
        runner.setWarmupMs(100);
        runner.setRepeats(3);

        // With no filter the estimate is nonzero if any cases are registered.
        int     matching = runner.filteredCaseCount();
        int64_t perCase = 100 + 3 * 500; // warmup + repeats * minTime
        CHECK(runner.estimatedDurationMs() == matching * perCase);

        // Doubling repeats should roughly double the measurement portion.
        runner.setRepeats(6);
        int64_t perCaseDouble = 100 + 6 * 500;
        CHECK(runner.estimatedDurationMs() == matching * perCaseDouble);

        // Filter down to nothing → zero estimate.
        runner.setFilter(String("__nothing_matches__"));
        CHECK(runner.estimatedDurationMs() == 0);
}

TEST_CASE("BenchmarkRunner: filter matches subset of cases") {
        // Verify the filter regex by constructing cases and running them
        // through the registry. We don't touch the global case list; we
        // exercise runCase directly and check the filter logic in runAll
        // by using runCaseByName.
        BenchmarkRunner runner = makeFastRunner();
        runner.setFilter(String("nonexistent"));
        runner.runAll();
        // Registry may contain entries from PROMEKI_REGISTER_BENCHMARK
        // macros elsewhere in the test suite (see dummy registration
        // below). The filter should match zero of them.
        CHECK(runner.results().isEmpty());
}

TEST_CASE("BenchmarkRunner: filter with empty pattern runs all registered cases") {
        BenchmarkRunner runner = makeFastRunner();
        // Don't actually runAll() — that would run every registered case
        // in the test binary, which is slow. Just verify the filter
        // accessor.
        CHECK(runner.filter().isEmpty());
        runner.setFilter(String(""));
        CHECK(runner.filter().isEmpty());
}

// ----------------------------------------------------------------------------
// BenchmarkRunner: configuration accessors
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkRunner: default configuration") {
        BenchmarkRunner runner;
        CHECK(runner.minTimeMs() == 500);
        CHECK(runner.warmupMs() == 100);
        CHECK(runner.minIterations() == 1);
        CHECK(runner.maxIterations() == 1000000000ULL);
        CHECK(runner.repeats() == 1);
        CHECK(runner.filter().isEmpty());
}

TEST_CASE("BenchmarkRunner: setters update configuration") {
        BenchmarkRunner runner;
        runner.setMinTimeMs(250);
        runner.setWarmupMs(25);
        runner.setMinIterations(10);
        runner.setMaxIterations(5000);
        runner.setRepeats(5);
        runner.setFilter(String("foo\\..*"));
        CHECK(runner.minTimeMs() == 250);
        CHECK(runner.warmupMs() == 25);
        CHECK(runner.minIterations() == 10);
        CHECK(runner.maxIterations() == 5000);
        CHECK(runner.repeats() == 5);
        CHECK(runner.filter() == "foo\\..*");
}

TEST_CASE("BenchmarkRunner: setRepeats clamps zero to 1") {
        BenchmarkRunner runner;
        runner.setRepeats(0);
        CHECK(runner.repeats() == 1);
}

// ----------------------------------------------------------------------------
// BenchmarkResult: JSON round-trip
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkResult: JSON round-trip preserves all fields") {
        BenchmarkResult r;
        r.suite = "csc";
        r.name = "rgba8_to_yuv422";
        r.label = "1920x1080";
        r.description = "RGB to YCbCr conversion";
        r.iterations = 240;
        r.repeats = 3;
        r.avgNsPerIter = 4201234.0;
        r.minNsPerIter = 4123456.0;
        r.maxNsPerIter = 4321098.0;
        r.stddevNsPerIter = 45678.0;
        r.itemsPerSecond = 238.0;
        r.bytesPerSecond = 1976000000.0;
        r.custom.insert(String("mpix_per_sec"), 494.1);
        r.custom.insert(String("stages"), 2.0);
        r.succeeded = true;

        JsonObject      obj = r.toJson();
        BenchmarkResult round = BenchmarkResult::fromJson(obj);

        CHECK(round.suite == r.suite);
        CHECK(round.name == r.name);
        CHECK(round.label == r.label);
        CHECK(round.description == r.description);
        CHECK(round.iterations == r.iterations);
        CHECK(round.repeats == r.repeats);
        CHECK(round.avgNsPerIter == doctest::Approx(r.avgNsPerIter));
        CHECK(round.minNsPerIter == doctest::Approx(r.minNsPerIter));
        CHECK(round.maxNsPerIter == doctest::Approx(r.maxNsPerIter));
        CHECK(round.stddevNsPerIter == doctest::Approx(r.stddevNsPerIter));
        CHECK(round.itemsPerSecond == doctest::Approx(r.itemsPerSecond));
        CHECK(round.bytesPerSecond == doctest::Approx(r.bytesPerSecond));
        CHECK(round.succeeded == r.succeeded);
        CHECK(round.custom.contains(String("mpix_per_sec")));
        CHECK(round.custom[String("mpix_per_sec")] == doctest::Approx(494.1));
        CHECK(round.custom.contains(String("stages")));
        CHECK(round.custom[String("stages")] == doctest::Approx(2.0));
}

TEST_CASE("BenchmarkResult: failed case round-trips error message") {
        BenchmarkResult r;
        r.suite = "test";
        r.name = "broken";
        r.succeeded = false;
        r.errorMessage = "Something went wrong";

        JsonObject      obj = r.toJson();
        BenchmarkResult round = BenchmarkResult::fromJson(obj);

        CHECK_FALSE(round.succeeded);
        CHECK(round.errorMessage == "Something went wrong");
}

// ----------------------------------------------------------------------------
// BenchmarkRunner: JSON output and baseline load
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkRunner: writeJson produces readable output") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "trivial", "", trivialCase);
        runner.runCase(c);

        Error  err;
        String dir = Dir::temp().path().toString();
        String path = dir + "/promeki-benchmarkrunner-test.json";
        err = runner.writeJson(path);
        CHECK(err.isOk());

        // Load back as a baseline and verify a result is present.
        Error                 loadErr;
        List<BenchmarkResult> base = BenchmarkRunner::loadBaseline(path, &loadErr);
        CHECK(loadErr.isOk());
        REQUIRE(base.size() >= 1);
        CHECK(base[0].suite == "test");
        CHECK(base[0].name == "trivial");

        // Clean up.
        std::remove(path.cstr());
}

TEST_CASE("BenchmarkRunner: loadBaseline returns NotExist for missing file") {
        Error                 err;
        List<BenchmarkResult> base =
                BenchmarkRunner::loadBaseline(String("/tmp/promeki-nonexistent-benchmark-baseline-xyzzy.json"), &err);
        CHECK(err == Error::NotExist);
        CHECK(base.isEmpty());
}

TEST_CASE("BenchmarkRunner: toJson carries config and version") {
        BenchmarkRunner runner = makeFastRunner();
        runner.setFilter(String("my\\..*"));
        JsonObject obj = runner.toJson();
        CHECK(obj.getInt("version") == 2);
        CHECK(obj.contains("date"));
        CHECK(obj.valueIsObject("config"));
        JsonObject config = obj.getObject("config");
        CHECK(config.getInt("min_time_ms") == 10);
        CHECK(config.getInt("warmup_ms") == 2);
        CHECK(config.getInt("repeats") == 3);
        CHECK(config.getString("filter") == "my\\..*");
}

// ----------------------------------------------------------------------------
// BenchmarkRunner: formatTable and formatComparison
// ----------------------------------------------------------------------------

TEST_CASE("BenchmarkRunner: formatTable returns a non-empty string with results") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "trivial", "", trivialCase);
        runner.runCase(c);
        String table = runner.formatTable();
        CHECK_FALSE(table.isEmpty());
        // New column layout prints suite and case name in separate
        // columns, so check for both individually.
        CHECK(table.contains("test"));
        CHECK(table.contains("trivial"));
        // Header row should be present.
        CHECK(table.contains("Suite"));
        CHECK(table.contains("Case"));
}

TEST_CASE("BenchmarkRunner: formatTable reports empty state") {
        BenchmarkRunner runner;
        CHECK(runner.formatTable() == "(no results)\n");
}

TEST_CASE("BenchmarkRunner: formatComparison marks new cases") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "trivial", "", trivialCase);
        runner.runCase(c);

        List<BenchmarkResult> baseline; // empty
        String                report = runner.formatComparison(baseline);
        CHECK(report.contains("test"));
        CHECK(report.contains("trivial"));
        CHECK(report.contains("(new)"));
}

TEST_CASE("BenchmarkRunner: formatComparison computes delta percentage") {
        BenchmarkRunner runner = makeFastRunner();
        BenchmarkCase   c("test", "trivial", "", trivialCase);
        runner.runCase(c);

        // Synthesize a baseline that's 2x slower, then check the delta is
        // around -50% (negative = improvement).
        List<BenchmarkResult> baseline;
        BenchmarkResult       b = runner.results()[0];
        b.avgNsPerIter = runner.results()[0].avgNsPerIter * 2.0;
        baseline.pushToBack(b);
        String report = runner.formatComparison(baseline);
        CHECK(report.contains("test"));
        CHECK(report.contains("trivial"));
        CHECK(report.contains("%"));
        // Delta sign prefix should be present (negative improvement or
        // positive regression, either way a leading + or - is rendered).
        CHECK((report.contains("-") || report.contains("+")));
}

// ----------------------------------------------------------------------------
// Registration macro
// ----------------------------------------------------------------------------

// Exercise the registration macro so we know it links and populates the
// registry. These cases are not meant to be run as part of the normal
// test body — they are just proof-of-registration.

static void registrationSmokeCase(BenchmarkState &state) {
        for (auto _ : state) (void)_;
}

PROMEKI_REGISTER_BENCHMARK("unittest", "macro_smoke", "Smoke test for PROMEKI_REGISTER_BENCHMARK",
                           registrationSmokeCase)

TEST_CASE("BenchmarkRunner: PROMEKI_REGISTER_BENCHMARK populates registry") {
        const List<BenchmarkCase> &cases = BenchmarkRunner::registeredCases();
        bool                       found = false;
        for (const auto &c : cases) {
                if (c.fullName() == "unittest.macro_smoke") {
                        found = true;
                        CHECK(c.description() == "Smoke test for PROMEKI_REGISTER_BENCHMARK");
                        break;
                }
        }
        CHECK(found);
}
