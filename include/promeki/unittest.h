/**
 * @file      unittest.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/util.h>
#include <promeki/logger.h>
#include <promeki/regex.h>
#include <functional>

#define PROMEKI_TEST_BEGIN(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_unittest_, PROMEKI_UNIQUE_ID) = \
        registerUnitTest({ PROMEKI_STRINGIFY(name), \
                        __FILE__, __LINE__, [](const UnitTest &unit) -> bool { 

#define PROMEKI_TEST_END() return true; }});

#define PROMEKI_TEST(test) \
        if(test) { \
                promekiInfo("PASS: %s", PROMEKI_STRINGIFY(test)); \
        } else { \
                promekiErr("FAIL: %s", PROMEKI_STRINGIFY(test)); \
                return false; \
        }

#define PROMEKI_TEST_MSG(msg) promekiInfo("----: %s", String(msg).cstr());

PROMEKI_NAMESPACE_BEGIN

struct UnitTest {
        String                                                          name;
        String                                                          file;
        int                                                             line;
        std::function<bool(const UnitTest &)>                           func;
};

int registerUnitTest(const UnitTest &&test);
bool runUnitTests(const RegEx &testNameFilter = ".+"); // Run all tests by default.

PROMEKI_NAMESPACE_END

