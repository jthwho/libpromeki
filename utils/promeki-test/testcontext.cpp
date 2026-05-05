/**
 * @file      testcontext.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "testcontext.h"

#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        TestContext::TestContext(const TestParams &params, const FilePath &testFolder)
            : _params(params), _testFolder(testFolder) {}

        // First-call-wins semantics for status setting.  A test that
        // calls @c setFail and then @c setPass should land on Fail —
        // the first signal almost certainly reflects what actually
        // went wrong, and a later "pass" is usually code that didn't
        // notice the earlier failure.  We log a debug line so the
        // discrepancy is visible to anyone reading the per-test log.
        static void noteOverride(const char *kind, const String &existing, const String &incoming) {
                promekiDebug("TestContext: ignoring %s after status was already set "
                             "(existing='%s', new='%s')",
                             kind, existing.cstr(), incoming.cstr());
        }

        void TestContext::setPass() {
                if (_resultSet) {
                        noteOverride("setPass", _message, String());
                        return;
                }
                _status = TestStatus::Pass;
                _message = String();
                _resultSet = true;
        }

        void TestContext::setFail(const String &reason) {
                if (_resultSet) {
                        noteOverride("setFail", _message, reason);
                        return;
                }
                _status = TestStatus::Fail;
                _message = reason;
                _resultSet = true;
        }

        void TestContext::setSkip(const String &reason) {
                if (_resultSet) {
                        noteOverride("setSkip", _message, reason);
                        return;
                }
                _status = TestStatus::Skip;
                _message = reason;
                _resultSet = true;
        }

        void TestContext::setTimeout(const String &reason) {
                if (_resultSet) {
                        noteOverride("setTimeout", _message, reason);
                        return;
                }
                _status = TestStatus::Timeout;
                _message = reason;
                _resultSet = true;
        }

        void TestContext::setDetail(const String &key, const Variant &value) {
                _details.insert(key, value);
        }

        void TestContext::setPipelineConfig(const JsonObject &config) {
                _pipelineConfig = config;
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
