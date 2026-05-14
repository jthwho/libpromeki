/**
 * @file      testrunner.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Functional-test registry and driver for promeki-test.
 *
 * The runner mirrors the @ref BenchmarkRunner pattern: tests are
 * registered into a process-wide list (either at static init via
 * @ref PROMEKI_REGISTER_FTEST, or from a per-suite registration hook
 * called by @c main after @c CmdLineParser has populated the
 * parameter database), then @c main filters by regex and invokes
 * each match.
 *
 * Test names are dot-separated identifiers (e.g.
 * @c "roundtrip.quicktime.mov.h264") so the regex filter can pin a
 * suite (@c "^roundtrip\\."), a sub-group
 * (@c "^roundtrip\\.quicktime\\."), or a single case
 * (@c "^roundtrip\\.quicktime\\.mov\\.h264$") with the same matcher.
 */

#pragma once

#include <functional>
#include <promeki/function.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/util.h>
#include "testcontext.h"

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /**
         * @brief A single registered functional-test case.
         *
         * Pairs a dot-separated identifier with the function that
         * runs the test against a @ref TestContext.  Registered
         * through @ref TestRunner::registerCase, either directly
         * from a per-suite hook or via @ref PROMEKI_REGISTER_FTEST.
         */
        class TestCase {
                public:
                        /** @brief Test function signature. */
                        using Function = promeki::Function<void(TestContext &)>;

                        /**
                         * @brief Constructs a test case.
                         *
                         * @param name        Dot-separated identifier
                         *                    (@c "suite.subgroup.case").
                         * @param description Short human-readable description
                         *                    shown in @c --list output.
                         * @param fn          The test function.
                         */
                        TestCase(const String &name, const String &description, Function fn)
                            : _name(name), _description(description), _fn(std::move(fn)) {}

                        /** @brief Returns the dot-separated test identifier. */
                        const String &name() const { return _name; }

                        /** @brief Returns the short description (may be empty). */
                        const String &description() const { return _description; }

                        /** @brief Invokes the test function against @p ctx. */
                        void invoke(TestContext &ctx) const { _fn(ctx); }

                private:
                        String   _name;
                        String   _description;
                        Function _fn;
        };

        /**
         * @brief Process-wide registry of @ref TestCase objects.
         *
         * Static methods only — there is no driver loop here; the
         * driver lives in @c main.cpp so it can interleave logging
         * and folder-management policy with the case-by-case
         * invocation.  The registry exists so test source files can
         * advertise themselves without depending on @c main.
         */
        class TestRunner {
                public:
                        /**
                         * @brief Registers a test case into the process-wide
                         *        list.  Returns an integer so the
                         *        @ref PROMEKI_REGISTER_FTEST macro can store
                         *        it in an unused @c static int.
                         */
                        static int registerCase(const TestCase &theCase);

                        /** @brief Returns the registered case list. */
                        static const List<TestCase> &registeredCases();
        };

} // namespace promekitest

PROMEKI_NAMESPACE_END

/**
 * @brief Registers a functional-test case at static initialization time.
 *
 * Use this for tests whose existence does not depend on runtime
 * state.  Suites whose case set has to be enumerated at runtime
 * (e.g. roundtrip — its matrix is built from registry introspection)
 * should expose a @c registerXxxCases() entry point and let
 * @c main.cpp call it after argv parsing instead.
 *
 * @param Name        Dot-separated identifier
 *                    (@c "suite.subgroup.case").
 * @param Description Short description (string literal or C string).
 * @param Fn          @c void(::promekitest::TestContext &) function.
 */
#define PROMEKI_REGISTER_FTEST(Name, Description, Fn)                                                                  \
        [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_ftest_, PROMEKI_UNIQUE_ID) =                              \
                ::promeki::promekitest::TestRunner::registerCase(                                                      \
                        ::promeki::promekitest::TestCase((Name), (Description), (Fn)))
