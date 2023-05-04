/*****************************************************************************
 * unittest.cpp
 * April 26, 2023
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

#include <iostream>
#include <vector>
#include <promeki/unittest.h>
#include <promeki/logger.h>

namespace promeki {

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

}

