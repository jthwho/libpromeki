/**
 * @file      benchmarkrunner.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/error.h>
#include <promeki/elapsedtimer.h>
#include <promeki/statsaccumulator.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

class JsonObject;
class BenchmarkRunner;

/**
 * @brief Per-run state passed to a benchmark case function.
 * @ingroup util
 *
 * A BenchmarkState carries the iteration count chosen by the runner,
 * measures wall time across the hot loop, and lets the case optionally
 * subtract out untimed setup regions and report throughput counters.
 *
 * Cases iterate either via the range-for adapter (preferred) or via
 * `keepRunning()`:
 *
 * @code
 * void bench_example(BenchmarkState &state) {
 *         // one-time setup (not timed)
 *         auto resource = heavySetup();
 *
 *         for(auto _ : state) {
 *                 (void)_;
 *                 hotCode(resource);
 *         }
 *
 *         state.setItemsProcessed(state.iterations());
 * }
 * @endcode
 *
 * The timer is started the first time the case pulls an iteration from
 * the state (via `begin()` or `keepRunning()`), so any work before the
 * loop is excluded automatically.  Call `pauseTiming()` /
 * `resumeTiming()` around any block of code inside the loop that should
 * not count against the measured time.
 *
 * @par Thread Safety
 * Not thread-safe.  A BenchmarkState is owned by the single
 * thread that runs the benchmark case; it must not be shared
 * across threads.  The owning @ref BenchmarkRunner schedules
 * cases serially.
 */
class BenchmarkState {
        public:
                /**
                 * @brief Range iterator yielding monotonically increasing iteration indices.
                 *
                 * The iterator stops the state's timer the moment the range
                 * loop completes, so any teardown that follows the loop in
                 * the case body is excluded from the measurement.
                 */
                class Iterator {
                        public:
                                Iterator(BenchmarkState *state, uint64_t pos) : _state(state), _pos(pos) {}

                                uint64_t operator*() const { return _pos; }

                                Iterator &operator++() {
                                        ++_pos;
                                        if (_state != nullptr && _pos >= _state->_iterations) {
                                                _state->finishTiming();
                                        }
                                        return *this;
                                }

                                bool operator!=(const Iterator &other) const { return _pos != other._pos; }

                        private:
                                BenchmarkState *_state;
                                uint64_t        _pos;
                };

                /** @brief Constructs a state object bound to the given iteration count. */
                explicit BenchmarkState(uint64_t iterations) : _iterations(iterations) {}

                /** @brief Number of iterations the runner chose for this invocation. */
                uint64_t iterations() const { return _iterations; }

                /**
                 * @brief Range-for begin; starts the internal timer on first call.
                 * @return Iterator pointing at iteration 0.
                 */
                Iterator begin() {
                        ensureTimerStarted();
                        return Iterator(this, 0);
                }

                /** @brief Range-for end; returns an iterator at the iteration count. */
                Iterator end() { return Iterator(this, _iterations); }

                /**
                 * @brief Imperative iteration primitive equivalent to the range-for loop.
                 *
                 * Starts the timer on the first call and advances the iteration
                 * counter on every call.  Returns false when the counter reaches
                 * `iterations()`.  Use either `keepRunning()` or the range-for
                 * form, not both.
                 *
                 * @return True while the case should continue running.
                 */
                bool keepRunning() {
                        if (_counter == 0) ensureTimerStarted();
                        if (_counter >= _iterations) {
                                finishTiming();
                                return false;
                        }
                        _counter++;
                        return true;
                }

                /**
                 * @brief Excludes the next region from the measured time.
                 *
                 * Safe to call multiple times; only the outermost pause/resume
                 * pair counts.  pauseTiming()/resumeTiming() may be called
                 * before the timer has been started (no-op in that case).
                 */
                void pauseTiming() {
                        if (!_timerStarted || _paused) return;
                        _paused = true;
                        _pauseStartNs = _timer.elapsedNs();
                        return;
                }

                /** @brief Resumes the measured-time window after a pauseTiming() call. */
                void resumeTiming() {
                        if (!_paused) return;
                        _paused = false;
                        _pausedNs += _timer.elapsedNs() - _pauseStartNs;
                        return;
                }

                /**
                 * @brief Tells the runner how many logical items the iterations processed.
                 *
                 * When set, the result gains an `items_per_sec` field.  A case
                 * that runs N iterations over a buffer of M elements would call
                 * `setItemsProcessed(state.iterations() * M)`.
                 *
                 * @param n Total items processed across all iterations.
                 */
                void setItemsProcessed(uint64_t n) { _itemsProcessed = n; }

                /**
                 * @brief Tells the runner how many bytes the iterations processed.
                 *
                 * When set, the result gains a `bytes_per_sec` field.
                 *
                 * @param n Total bytes processed across all iterations.
                 */
                void setBytesProcessed(uint64_t n) { _bytesProcessed = n; }

                /**
                 * @brief Sets a human-readable label to display alongside the case name.
                 * @param label Case-scoped label (e.g. "1920x1080 RGBA8 → UYVY").
                 */
                void setLabel(const String &label) { _label = label; }

                /**
                 * @brief Records a case-specific custom counter in the result.
                 *
                 * Custom counters appear as numeric fields under the `custom`
                 * object in the JSON output and are not interpreted by the
                 * runner.  Useful for reporting throughput-per-pixel,
                 * stage counts, or any other case-specific metric.
                 *
                 * @param key   Counter name.
                 * @param value Counter value.
                 */
                void setCounter(const String &key, double value) {
                        _counters.insert(key, value);
                        return;
                }

                /** @brief Returns the active label (may be empty). */
                const String &label() const { return _label; }

                /** @brief Returns the running total of items processed (0 if unset). */
                uint64_t itemsProcessed() const { return _itemsProcessed; }

                /** @brief Returns the running total of bytes processed (0 if unset). */
                uint64_t bytesProcessed() const { return _bytesProcessed; }

                /** @brief Returns the case-specific custom counters. */
                const Map<String, double> &counters() const { return _counters; }

                /**
                 * @brief Measured wall-clock time after the case returned, minus any paused regions.
                 * @return Effective wall time in nanoseconds.
                 */
                int64_t effectiveNs() const { return _effectiveNs; }

                /**
                 * @brief Raw wall-clock time the case actually ran, including any paused regions.
                 *
                 * Used by the runner's calibration loop to size iteration
                 * counts: calibration has to size against real wall
                 * time or a case that excludes most of its body via
                 * `pauseTiming()` can spin the calibrator into a
                 * runaway iteration growth.  The per-iteration ns
                 * statistics still use `effectiveNs()`.
                 *
                 * @return Raw wall time in nanoseconds.
                 */
                int64_t wallNs() const { return _wallNs; }

                /**
                 * @brief Returns true if the case iterated to completion.
                 *
                 * Used by the runner to detect a case that returned without
                 * ever entering the hot loop (almost always a bug).
                 */
                bool completed() const { return _completed; }

        private:
                friend class BenchmarkRunner;

                void ensureTimerStarted() {
                        if (_timerStarted) return;
                        _timerStarted = true;
                        _timer.start();
                }

                void finishTiming() {
                        if (_completed) return;
                        _completed = true;
                        if (_paused) {
                                _pausedNs += _timer.elapsedNs() - _pauseStartNs;
                                _paused = false;
                        }
                        _wallNs = _timer.elapsedNs();
                        _effectiveNs = _wallNs - _pausedNs;
                        if (_effectiveNs < 0) _effectiveNs = 0;
                        return;
                }

                /// @brief Called by the runner after the case returns to capture the wall time.
                void captureIfUnfinished() {
                        // If the case used range-for, iteration exits without
                        // calling keepRunning(), so finishTiming() hasn't run.
                        if (_completed) return;
                        if (_timerStarted) {
                                finishTiming();
                        } else {
                                // Case never touched the state at all — leave
                                // _completed false so the runner can flag it.
                        }
                }

                uint64_t            _iterations = 0;
                uint64_t            _counter = 0;
                bool                _timerStarted = false;
                bool                _paused = false;
                bool                _completed = false;
                ElapsedTimer        _timer;
                int64_t             _pauseStartNs = 0;
                int64_t             _pausedNs = 0;
                int64_t             _wallNs = 0;
                int64_t             _effectiveNs = 0;
                uint64_t            _itemsProcessed = 0;
                uint64_t            _bytesProcessed = 0;
                String              _label;
                Map<String, double> _counters;
};

/**
 * @brief Result of running a single BenchmarkCase.
 * @ingroup util
 *
 * A BenchmarkResult holds timing statistics aggregated across one or more
 * repeats of the same case at the same iteration count, plus any throughput
 * counters and custom metrics the case reported via BenchmarkState.  Results
 * round-trip through JSON so baselines can be stored on disk.
 */
class BenchmarkResult {
        public:
                /// @brief Name of the suite this result belongs to.
                String suite;
                /// @brief Case name within the suite.
                String name;
                /// @brief Optional per-run label set by the case.
                String label;
                /// @brief Optional free-form description set at registration.
                String description;
                /// @brief Iteration count used for each measurement.
                uint64_t iterations = 0;
                /// @brief Number of repeats used to compute stddev.
                uint64_t repeats = 0;
                /// @brief Mean nanoseconds per iteration across repeats.
                double avgNsPerIter = 0.0;
                /// @brief Minimum observed nanoseconds per iteration across repeats.
                double minNsPerIter = 0.0;
                /// @brief Maximum observed nanoseconds per iteration across repeats.
                double maxNsPerIter = 0.0;
                /// @brief Sample stddev of nanoseconds per iteration across repeats.
                double stddevNsPerIter = 0.0;
                /// @brief Derived items-per-second when the case reported itemsProcessed; 0 otherwise.
                double itemsPerSecond = 0.0;
                /// @brief Derived bytes-per-second when the case reported bytesProcessed; 0 otherwise.
                double bytesPerSecond = 0.0;
                /// @brief Case-specific custom metrics.
                Map<String, double> custom;
                /// @brief True if the case completed normally; false if it errored or never iterated.
                bool succeeded = true;
                /// @brief Error message when the case failed; empty otherwise.
                String errorMessage;

                /** @brief Serializes this result into its JSON representation. */
                JsonObject toJson() const;

                /**
                 * @brief Parses a BenchmarkResult from its JSON representation.
                 * @param obj JSON object as produced by `toJson()`.
                 * @param err Optional error output.
                 * @return The parsed result, or a default-constructed result on failure.
                 */
                static BenchmarkResult fromJson(const JsonObject &obj, Error *err = nullptr);
};

/**
 * @brief A single registered benchmark case.
 * @ingroup util
 *
 * BenchmarkCase pairs identifying metadata (suite + name + description)
 * with the case function itself.  Cases are registered at static
 * initialization time via `PROMEKI_REGISTER_BENCHMARK` and stored in a
 * process-wide registry.
 */
class BenchmarkCase {
        public:
                /** @brief Case function signature. */
                using Function = std::function<void(BenchmarkState &)>;

                /**
                 * @brief Constructs a benchmark case.
                 * @param suite       Suite name the case belongs to (e.g. `"csc"`).
                 * @param name        Case name within the suite (e.g. `"rgba8_to_yuv422"`).
                 * @param description Optional free-form description shown with `--list`.
                 * @param fn          The case function.
                 */
                BenchmarkCase(const String &suite, const String &name, const String &description, Function fn)
                    : _suite(suite), _name(name), _description(description), _fn(std::move(fn)) {}

                /** @brief Returns the suite name. */
                const String &suite() const { return _suite; }

                /** @brief Returns the case name. */
                const String &name() const { return _name; }

                /** @brief Returns the description (may be empty). */
                const String &description() const { return _description; }

                /** @brief Returns the fully-qualified id `"suite.name"`. */
                String fullName() const { return _suite + "." + _name; }

                /** @brief Invokes the case function against the given state. */
                void invoke(BenchmarkState &state) const { _fn(state); }

        private:
                String   _suite;
                String   _name;
                String   _description;
                Function _fn;
};

/**
 * @brief Drives registered BenchmarkCase instances and collects results.
 * @ingroup util
 *
 * The runner tunes iteration counts so each case runs for approximately
 * `minTimeMs` wall time, executes the case `repeats` times at that
 * iteration count, and records per-case statistics.  Results are
 * accumulated in memory and can be written to JSON for baseline tracking
 * or comparison.
 *
 * Cases are registered via the `PROMEKI_REGISTER_BENCHMARK` macro (see
 * `registerCase()` below).  The runner pulls from the process-wide
 * registry via `registeredCases()`.
 *
 * @par Example
 * @code
 * BenchmarkRunner runner;
 * runner.setMinTimeMs(500);
 * runner.setRepeats(3);
 * runner.setFilter("csc\\..*");  // only run CSC cases
 * runner.runAll();
 * runner.writeJson("promeki-bench.json");
 * printf("%s", runner.formatTable().cstr());
 * @endcode
 */
class BenchmarkRunner {
        public:
                /**
                 * @brief Registers a benchmark case into the process-wide registry.
                 *
                 * Called at static initialization time by the
                 * `PROMEKI_REGISTER_BENCHMARK` macro.  Returns an integer so
                 * the macro can assign it to an unused static variable.
                 *
                 * @param theCase The case to register.
                 * @return The case's index in the registry.
                 */
                static int registerCase(const BenchmarkCase &theCase);

                /** @brief Returns the process-wide list of registered cases. */
                static const List<BenchmarkCase> &registeredCases();

                /**
                 * @brief Formats every registered case as a columnized ASCII table.
                 *
                 * Returns a string with one case per row and three columns
                 * (`Suite`, `Case`, `Description`).  Suited for `--list`
                 * style output where the caller wants an at-a-glance view
                 * of every case the process knows about.  Column widths
                 * are computed from the actual content, so long case
                 * names or descriptions expand their column rather than
                 * getting truncated.
                 *
                 * @return The formatted table, or `(no cases registered)\n`
                 *         if the registry is empty.
                 */
                static String formatRegisteredCases();

                /** @brief Constructs a runner with default parameters. */
                BenchmarkRunner();

                /**
                 * @brief Sets the target wall-clock measurement window per case.
                 * @param ms Target time in milliseconds (default 500).
                 */
                void setMinTimeMs(unsigned int ms) { _minTimeMs = ms; }

                /**
                 * @brief Sets the warmup window used to calibrate iteration counts.
                 * @param ms Warmup time in milliseconds (default 100).
                 */
                void setWarmupMs(unsigned int ms) { _warmupMs = ms; }

                /**
                 * @brief Sets the minimum iteration count allowed after calibration.
                 * @param n Iteration floor (default 1).
                 */
                void setMinIterations(uint64_t n) { _minIterations = n; }

                /**
                 * @brief Sets the maximum iteration count allowed after calibration.
                 * @param n Iteration ceiling (default 1,000,000,000).
                 */
                void setMaxIterations(uint64_t n) { _maxIterations = n; }

                /**
                 * @brief Sets the number of measurement runs per case.
                 *
                 * Stddev is computed across the repeats.  A single repeat
                 * produces zero stddev.
                 *
                 * @param n Repeat count (default 1).
                 */
                void setRepeats(unsigned int n) { _repeats = n == 0 ? 1 : n; }

                /**
                 * @brief Sets a regex filter on the fully-qualified `suite.name` identifier.
                 *
                 * Empty pattern runs every case.  The pattern is matched
                 * against the full case name with `RegEx::search()` so
                 * substrings match as expected.
                 *
                 * @param pattern ECMAScript regex pattern.
                 */
                void setFilter(const String &pattern) { _filterPattern = pattern; }

                /**
                 * @brief Enables per-case progress printing on stdout.
                 * @param verbose True to print each case as it runs.
                 */
                void setVerbose(bool verbose) { _verbose = verbose; }

                /** @brief Returns the target measurement window in milliseconds. */
                unsigned int minTimeMs() const { return _minTimeMs; }

                /** @brief Returns the warmup window in milliseconds. */
                unsigned int warmupMs() const { return _warmupMs; }

                /** @brief Returns the iteration floor. */
                uint64_t minIterations() const { return _minIterations; }

                /** @brief Returns the iteration ceiling. */
                uint64_t maxIterations() const { return _maxIterations; }

                /** @brief Returns the measurement repeat count. */
                unsigned int repeats() const { return _repeats; }

                /** @brief Returns the active filter pattern (empty if no filter). */
                const String &filter() const { return _filterPattern; }

                /** @brief Returns the results accumulated so far. */
                const List<BenchmarkResult> &results() const { return _results; }

                /**
                 * @brief Returns how many registered cases match the current filter.
                 *
                 * Uses the same filter logic as `runAll()` so the number
                 * reflects exactly what a run would execute.  When no
                 * filter is set, returns `registeredCases().size()`.
                 *
                 * @return Number of cases that would be measured.
                 */
                int filteredCaseCount() const;

                /**
                 * @brief Rough estimate of wall time for a run in milliseconds.
                 *
                 * Computes `filteredCaseCount() * (warmupMs + repeats *
                 * minTimeMs)`.  This is a ballpark: calibration may run
                 * slightly over `warmupMs`, and a case that overshoots
                 * its per-iteration budget can be slower than predicted.
                 * Accurate enough for a banner that tells the user
                 * whether they are in for a 5-second run or a 2-hour
                 * one.
                 *
                 * @return Estimated total duration in milliseconds.
                 */
                int64_t estimatedDurationMs() const;

                /**
                 * @brief Runs every registered case matching the current filter.
                 * @return Error::Ok on success, or an aggregated failure code.
                 */
                Error runAll();

                /**
                 * @brief Runs a single case by full name.
                 * @param fullName Fully-qualified case name (`"suite.name"`).
                 * @return Error::Ok if the case was found and ran, Error::NotExist otherwise.
                 */
                Error runCaseByName(const String &fullName);

                /**
                 * @brief Runs a specific case directly.
                 * @param theCase The case to run.
                 * @return The accumulated BenchmarkResult.
                 */
                BenchmarkResult runCase(const BenchmarkCase &theCase);

                /** @brief Clears accumulated results. */
                void clearResults() { _results.clear(); }

                /**
                 * @brief Writes accumulated results to a JSON file in the shared schema.
                 * @param path Destination file path.
                 * @return Error::Ok on success, or a file-write error.
                 */
                Error writeJson(const String &path) const;

                /**
                 * @brief Serializes accumulated results into an in-memory JSON object.
                 *
                 * Useful for embedding in larger JSON documents or for
                 * comparison against a baseline without touching disk.
                 *
                 * @return The full JSON document.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Loads a baseline JSON document for later comparison.
                 * @param path Source file path.
                 * @param err  Optional error output.
                 * @return The parsed list of baseline results.
                 */
                static List<BenchmarkResult> loadBaseline(const String &path, Error *err = nullptr);

                /**
                 * @brief Formats a human-readable ASCII table of the current results.
                 *
                 * The table shows case name, iteration count, ns/iter, and
                 * any derived items/bytes-per-second columns that are
                 * populated.
                 */
                String formatTable() const;

                /**
                 * @brief Formats a delta table comparing current results to a baseline.
                 * @param baseline Baseline results produced by `loadBaseline()` or `results()`.
                 */
                String formatComparison(const List<BenchmarkResult> &baseline) const;

        private:
                BenchmarkResult measureCase(const BenchmarkCase &theCase);
                uint64_t        calibrateIterations(const BenchmarkCase &theCase);
                void            runOne(const BenchmarkCase &theCase, uint64_t iterations, BenchmarkState &state);
                /** @brief Returns true if @p theCase passes the active filter. */
                bool matchesFilter(const BenchmarkCase &theCase) const;

                unsigned int _minTimeMs = 500;
                unsigned int _warmupMs = 100;
                uint64_t     _minIterations = 1;
                uint64_t     _maxIterations = 1000000000ULL;
                unsigned int _repeats = 1;
                String       _filterPattern;
                bool         _verbose = false;
                // Column width used for the "running" progress line so
                // each case's result lines up in the same place on
                // screen; computed by runAll() from the filtered case
                // set and read by measureCase() when printing the case
                // name.  0 means "no padding" — measureCase falls back
                // to plain width when called outside a runAll() sweep.
                size_t                _progressNameWidth = 0;
                List<BenchmarkResult> _results;
};

/**
 * @brief Registers a benchmark case at static initialization time.
 * @ingroup util
 *
 * Usage:
 * @code
 * static void bench_myCase(BenchmarkState &state) {
 *         for(auto _ : state) { (void)_; hotCode(); }
 * }
 * PROMEKI_REGISTER_BENCHMARK("mysuite", "myCase",
 *                            "Short description of what this measures",
 *                            bench_myCase);
 * @endcode
 *
 * @param Suite       Suite name (string literal or C string).
 * @param Name        Case name (string literal or C string).
 * @param Description Short description (string literal or C string).
 * @param Fn          Case function with signature `void(BenchmarkState &)`.
 */
#define PROMEKI_REGISTER_BENCHMARK(Suite, Name, Description, Fn)                                                       \
        [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_benchmark_, PROMEKI_UNIQUE_ID) =                          \
                promeki::BenchmarkRunner::registerCase(promeki::BenchmarkCase((Suite), (Name), (Description), (Fn)));

PROMEKI_NAMESPACE_END
