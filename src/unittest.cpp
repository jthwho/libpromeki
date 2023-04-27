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

namespace promeki {

PROMEKI_TEST_BEGIN(unittest)

        int a = 42;
        PROMEKI_TEST(a == 42)

PROMEKI_TEST_END()


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

void signalUnitTest(const UnitTest &unit, const String &test, bool status) {
        std::cout << "  " << (status ? "PASS" : "FAIL") << ": " << test << std::endl;
        return;
}

void messageUnitTest(const UnitTest &unit, const String &msg) {
        std::cout << "  " << "-- " << msg << " --" << std::endl;
        return;
}

bool runUnitTests() {
        const UnitTestVector &utv = unitTestVector();
        std::cout << "Total Unit Tests: " << utv.size() << std::endl;
        for(int i = 0; i < utv.size(); i++) {
                const UnitTest &ut = utv.at(i);
                std::cout << "Running Test Unit '" << ut.name << "' from " << ut.file << ":" << ut.line << std::endl;
                bool ret = ut.func(ut);
                if(!ret) {
                        std::cout << "ERROR: Test Failed In This Unit" << std::endl;
                        return false;
                }
        }
        std::cout << "SUCCESS.  All tests passed" << std::endl;
        return true;
}

}

