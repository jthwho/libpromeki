/**
 * @file      objectbase.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/objectbase.h>
#include <promeki/signal.h>
#include <promeki/slot.h>
#include "test.h"

using namespace promeki;

PROMEKI_TEST_BEGIN(ObjectBase)
        
        TestOne::metaInfo().dumpToLog();
        TestTwo::metaInfo().dumpToLog();

        TestOne one;
        TestTwo two;

        promekiInfo("TestOne = %p", &one);
        promekiInfo("TestTwo = %p", &two);
        TestOne::connect(&one.somethingHappenedSignal, &two.handleSomethingSlot);
        one.makeSomethingHappen();

PROMEKI_TEST_END()
