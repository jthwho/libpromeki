/*****************************************************************************
 * objectbase.cpp
 * May 19, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

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
