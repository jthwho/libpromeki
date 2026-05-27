/**
 * @file      testcontext.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Per-invocation context handed to every functional test.
 *
 * The runner constructs a @ref TestContext for each registered case,
 * populates it with the resolved @ref TestParams (test folder + log
 * file path + every command-line knob), points the global Logger at
 * the per-test log file, then calls into the test function.  The test
 * reports its result by calling @ref setPass / @ref setFail /
 * @ref setSkip / @ref setTimeout, optionally records numeric or
 * string details with @ref setDetail, and returns.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/filepath.h>
#include <promeki/json.h>
#include <promeki/variant.h>
#include <promeki/map.h>
#include "testparams.h"

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /**
         * @brief Pass / Fail / Skip / Timeout outcome of one test.
         *
         * Defined as an enum class on @ref TestContext so the names
         * are scoped (@c TestStatus::Pass) rather than colliding with
         * MediaIO / Test status enums elsewhere in the codebase.
         */
        enum class TestStatus {
                Pass,    ///< Test ran and verified the expected outcome.
                Fail,    ///< Test ran but the result was wrong.
                Skip,    ///< Pre-condition not met; the test could not run meaningfully.
                Timeout, ///< Test was cut off by the per-phase watchdog.
        };

        /**
         * @brief Per-invocation state object passed by reference to each test.
         *
         * Tests do everything through this object so the runner can
         * keep all I/O policy (where the scratch folder lives, where
         * the log goes, how long to wait) in one place.  The object
         * is built once per invocation and discarded — there is no
         * shared state to worry about across tests.
         */
        class TestContext {
                public:
                        /**
                         * @brief Constructs a context for one test invocation.
                         *
                         * @param params      Resolved parameter set, including
                         *                    @ref TestParams::TestFolder and
                         *                    @ref TestParams::LogFile.
                         * @param testFolder  The same folder as
                         *                    @c params[TestFolder] but cached
                         *                    here as a @ref FilePath so tests
                         *                    don't have to re-parse the string
                         *                    on every access.
                         */
                        TestContext(const TestParams &params, const FilePath &testFolder);

                        /** @brief Returns the resolved parameters for this test. */
                        const TestParams &params() const { return _params; }

                        /** @brief Returns the per-test scratch folder. */
                        const FilePath &testFolder() const { return _testFolder; }

                        /// @name Result reporting
                        /// @{

                        /**
                         * @brief Marks the test as passed.
                         *
                         * Tests that don't explicitly set a status are
                         * treated as passed when the function returns —
                         * calling @c setPass is optional but makes the
                         * intent obvious in source.
                         */
                        void setPass();

                        /**
                         * @brief Marks the test as failed with a one-line reason.
                         *
                         * The reason shows up in the per-case summary
                         * line and in @c result.json, so callers should
                         * make it a single human-readable sentence
                         * (e.g. @c "decoder produced 0 frames").
                         */
                        void setFail(const String &reason);

                        /**
                         * @brief Marks the test as skipped.
                         *
                         * Skips count separately from failures in the
                         * summary.  Use this when a precondition isn't
                         * met (missing backend, unsupported codec) so
                         * the result page doesn't drown legitimate
                         * failures in noise.
                         */
                        void setSkip(const String &reason);

                        /**
                         * @brief Marks the test as timed out.
                         *
                         * Timeouts count as failures for the run's
                         * exit code but are tracked separately in the
                         * summary so a deadlocked test is visually
                         * distinct from a wrong-result failure.
                         */
                        void setTimeout(const String &reason);

                        /// @}

                        /// @name Per-test detail recording
                        /// @{

                        /**
                         * @brief Records a key/value pair to be persisted
                         *        with the test's result.
                         *
                         * Details land in the per-test @c result.json
                         * file and the run-wide @c summary.json so
                         * downstream tooling can inspect what happened
                         * without re-running the test.  Typical entries
                         * are counters (frames written / processed,
                         * bytes on disk) and small strings (codec
                         * label, file path).
                         */
                        void setDetail(const String &key, const Variant &value);

                        /**
                         * @brief Records the resolved @ref MediaPipelineConfig
                         *        the pipeline actually ran with.
                         *
                         * Tests that drive a @ref MediaPipeline call this
                         * with the post-autoplan JSON snapshot from
                         * @ref MediaPipeline::config so the per-test
                         * @c result.json includes the full stage list,
                         * routes, and per-stage @c MediaConfig — useful
                         * for diagnosing why a discontinuity showed up
                         * (which CSC the planner spliced in, which
                         * encoder backend was selected, etc.).
                         *
                         * Multiple calls overwrite — the last call
                         * before the test returns wins.  Tests that run
                         * separate write- and read-side pipelines should
                         * combine into a single object before storing.
                         */
                        void setPipelineConfig(const JsonObject &config);

                        /// @}

                        /// @name Result accessors used by the runner
                        /// @{

                        /** @brief Returns the resolved status (Pass when never set). */
                        TestStatus status() const { return _status; }

                        /** @brief Returns the reason for Fail / Skip / Timeout (empty otherwise). */
                        const String &message() const { return _message; }

                        /** @brief Returns the recorded details map. */
                        const Map<String, Variant> &details() const { return _details; }

                        /** @brief Returns the recorded pipeline config (empty if not set). */
                        const JsonObject &pipelineConfig() const { return _pipelineConfig; }

                        /** @brief Returns true if the test explicitly called one of the result setters. */
                        bool resultWasSet() const { return _resultSet; }

                        /// @}

                private:
                        TestParams           _params;
                        FilePath             _testFolder;
                        TestStatus           _status = TestStatus::Pass;
                        String               _message;
                        Map<String, Variant> _details;
                        JsonObject           _pipelineConfig;
                        bool                 _resultSet = false;
        };

} // namespace promekitest

PROMEKI_NAMESPACE_END
