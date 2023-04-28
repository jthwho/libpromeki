/*****************************************************************************
 * unittest.h
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

#pragma once

#include <promeki/string.h>
#include <promeki/util.h>
#include <functional>

#define PROMEKI_TEST_BEGIN(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_unittest_, PROMEKI_UNIQUE_ID) = \
        registerUnitTest({ PROMEKI_STRINGIFY(name), \
                        __FILE__, __LINE__, [](const UnitTest &unit) -> bool { 

#define PROMEKI_TEST_END() return true; }});

#define PROMEKI_TEST(test) \
        if(test) { \
                signalUnitTest(unit, PROMEKI_STRINGIFY(test), true); \
        } else { \
                signalUnitTest(unit, PROMEKI_STRINGIFY(test), false); \
                return false; \
        }

#define PROMEKI_TEST_MSG(msg) messageUnitTest(unit, msg);

namespace promeki {

struct UnitTest {
        String                                                          name;
        String                                                          file;
        int                                                             line;
        std::function<bool(const UnitTest &)>                           func;
};

int registerUnitTest(const UnitTest &&test);
bool runUnitTests();
void signalUnitTest(const UnitTest &test, const String &desc, bool state);
void messageUnitTest(const UnitTest &test, const String &msg);

}

