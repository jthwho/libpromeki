/**
 * @file      unittest.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <iostream>
#include <vector>
#include <promeki/unittest.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

typedef std::vector<UnitTest> UnitTestVector;

static UnitTestVector &unitTestVector() {
        static UnitTestVector ret;
        return ret;
}

int registerUnitTest(const UnitTest &&test) {
        UnitTestVector &utv = unitTestVector();
        int ret = utv.size();
        utv.push_back(std::move(test));
        return ret;
}

bool runUnitTests(const RegEx &filter) {
        const UnitTestVector &utv = unitTestVector();
        int testsRun = 0;
        promekiInfo("Starting unit tests with filter '%s'", filter.pattern().cstr());
        for(const auto &item : unitTestVector()) {
                if(!filter.match(item.name)) continue;
                promekiInfo("Running Test '%s' from %s:%d", 
                        item.name.cstr(), item.file.cstr(), item.line);
                bool ret = item.func(item);
                if(!ret) {
                        promekiErr("Test '%s' failed", item.name.cstr());
                        return false;
                }
                testsRun++;
        }
        promekiInfo("Successfuly ran %d of %d total tests.", testsRun, (int)unitTestVector().size());
        return true;
}

PROMEKI_NAMESPACE_END

