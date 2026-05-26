/**
 * @file      testparams.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Parameter database for the promeki-test functional-test runner.
 *
 * Each registered test receives a @ref TestParams (a thin
 * @ref VariantDatabase subclass) carrying every command-line knob the
 * runner gathered, plus the resolved per-test folder and log path.
 * Common keys are declared here so any test can read them with the
 * usual database APIs:
 *
 *   ctx.params().getAs<int32_t>(TestParams::Frames, 30)
 *   ctx.params().getAs<String>(TestParams::TestFolder)
 *
 * Test-specific knobs (e.g. @c "Roundtrip.VideoFormat") are declared
 * by the test source file with @ref TestParams::declareID — that
 * function is the same one @ref PROMEKI_DECLARE_ID expands to, and
 * the per-tag spec registry is process-wide so a key declared in one
 * TU is visible to every other TU that asks for it.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantspec.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /**
         * @brief VariantDatabase carrying parameters for one test invocation.
         *
         * Subclass exists so common keys can be declared as
         * @c constexpr @c ID values at class scope (via
         * @ref PROMEKI_DECLARE_ID) without polluting a namespace
         * with one inline static per key.  Tests can still register
         * additional keys at any time by calling @ref declareID
         * directly — the spec registry is shared across every TU
         * that uses the same @c "FunctionalTest" tag.
         */
        class TestParams : public VariantDatabase<"FunctionalTest"> {
                public:
                        using Base = VariantDatabase<"FunctionalTest">;
                        using Base::Base;

                        /// @brief String — base directory under which per-test
                        ///        folders are created.  Default resolves to
                        ///        @c Dir::temp() / "promeki-test-<timestamp>".
                        PROMEKI_DECLARE_ID(BaseFolder,
                                           VariantSpec()
                                                   .setType(DataTypeString)
                                                   .setDefault(String())
                                                   .setDescription("Base directory under which per-test folders "
                                                                   "are created."));

                        /// @brief String — resolved per-test folder.  The
                        ///        runner sets this before invoking each case
                        ///        so tests that need scratch space don't have
                        ///        to reinvent the path layout.
                        PROMEKI_DECLARE_ID(TestFolder, VariantSpec()
                                                              .setType(DataTypeString)
                                                              .setDefault(String())
                                                              .setDescription("Resolved per-test scratch folder."));

                        /// @brief String — path to the per-test log file.
                        PROMEKI_DECLARE_ID(LogFile, VariantSpec()
                                                           .setType(DataTypeString)
                                                           .setDefault(String())
                                                           .setDescription("Path to the per-test log file."));

                        /// @brief Bool — true if the runner was started in verbose mode.
                        PROMEKI_DECLARE_ID(Verbose,
                                           VariantSpec()
                                                   .setType(DataTypeBool)
                                                   .setDefault(false)
                                                   .setDescription("Verbose logging requested for this run."));

                        /// @brief S32 — frames per test for tests that consume one.
                        PROMEKI_DECLARE_ID(Frames,
                                           VariantSpec()
                                                   .setType(DataTypeInt32)
                                                   .setDefault(int32_t(30))
                                                   .setRange(int32_t(1), int32_t(1000000))
                                                   .setDescription("Frame count for tests that need one."));

                        /// @brief S32 — per-phase watchdog timeout (ms).  Used
                        ///        by pipeline-driven tests so a deadlocked
                        ///        stage can't stall the whole matrix.
                        PROMEKI_DECLARE_ID(PhaseTimeoutMs,
                                           VariantSpec()
                                                   .setType(DataTypeInt32)
                                                   .setDefault(int32_t(10000))
                                                   .setRange(int32_t(100), int32_t(3600000))
                                                   .setDescription("Per-phase watchdog timeout in milliseconds."));

                        /// @brief String — absolute path to the resolved
                        ///        @c testmedia/ corpus root, or empty
                        ///        when no candidate was usable.
                        ///
                        /// The runner resolves the root once before
                        /// suite registration (via the CLI override,
                        /// the @c PROMEKI_TESTMEDIA env var, or the
                        /// default in-tree symlink) and stamps the
                        /// result here so individual tests have a
                        /// single source of truth for "where is the
                        /// corpus?" without having to redo the
                        /// discovery search themselves.
                        PROMEKI_DECLARE_ID(TestMediaRoot,
                                           VariantSpec()
                                                   .setType(DataTypeString)
                                                   .setDefault(String())
                                                   .setDescription("Resolved testmedia corpus root (empty if not found)."));
        };

} // namespace promekitest

PROMEKI_NAMESPACE_END
