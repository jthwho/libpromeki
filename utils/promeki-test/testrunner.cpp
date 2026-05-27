/**
 * @file      testrunner.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "testrunner.h"

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        // The registry lives in a function-local static so static-init
        // ordering across translation units doesn't matter — the first
        // call to either @ref registerCase or @ref registeredCases
        // brings it into existence.  Same trick @ref BenchmarkRunner
        // uses for its registry.
        static List<TestCase> &registry() {
                static List<TestCase> reg;
                return reg;
        }

        int TestRunner::registerCase(const TestCase &theCase) {
                List<TestCase> &reg = registry();
                reg.pushToBack(theCase);
                return static_cast<int>(reg.size()) - 1;
        }

        const List<TestCase> &TestRunner::registeredCases() {
                return registry();
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
