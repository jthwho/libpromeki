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
        promekiInfo("Registered Test: %s", test.name.cstr());
        return ret;
}

bool runUnitTests() {
        const UnitTestVector &utv = unitTestVector();
        promekiInfo("Total Unit Tests: %d", (int)utv.size());
        for(int i = 0; i < utv.size(); i++) {
                const UnitTest &ut = utv.at(i);
                promekiInfo("Running Test '%s' from %s:%d", 
                        ut.name.cstr(), ut.file.cstr(), ut.line);
                bool ret = ut.func(ut);
                if(!ret) {
                        promekiErr("Test '%s' failed", ut.name.cstr());
                        return false;
                }
        }
        promekiInfo("All tests pass.  Good job.");
        return true;
}

}

