/**
 * @file      objectbase.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/objectbase.h>
#include <promeki/signal.h>
#include <promeki/slot.h>
#include <promeki/logger.h>
#include "test.h"

using namespace promeki;

TEST_CASE("ObjectBase") {

        TestOne::metaInfo().dumpToLog();
        TestTwo::metaInfo().dumpToLog();

        TestOne one;
        TestTwo two;

        promekiInfo("TestOne = %p", &one);
        promekiInfo("TestTwo = %p", &two);
        TestOne::connect(&one.somethingHappenedSignal, &two.handleSomethingSlot);
        one.makeSomethingHappen();

}
