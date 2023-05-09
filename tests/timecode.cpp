/*****************************************************************************
 * timecode.cpp
 * May 07, 2023
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
#include <promeki/timecode.h>

using namespace promeki;

PROMEKI_TEST_BEGIN(Timecode)
        const int framesToTest = 24 * 60 * 60 * 30;

        auto [ tcs1, err ] = Timecode::fromString("01:02:03:04");
        PROMEKI_TEST(err.isOk());
        PROMEKI_TEST(tcs1 == Timecode(1, 2, 3, 4));

        Timecode tc1(Timecode::DF30);
        promekiInfo("TC1: %s", tc1.toString().first.cstr());
        PROMEKI_TEST(tc1.toFrameNumber().first == 0);
        int last = 0;
        bool fnumCorrect = true;
        for(int i = 0; i < framesToTest; i++) {
                tc1++;
                int fnum = tc1.toFrameNumber().first;
                if(fnum != i + 1) {
                        promekiErr("Frame number isn't correct %d, i=%d", fnum, i);
                        fnumCorrect = false;
                        break;
                }
                if(last + 1 != fnum) {
                        promekiErr("Last frame number %d + 1 isn't this one %d", last, fnum);
                        fnumCorrect = false;
                        break;
                }
                Timecode tc = Timecode::fromFrameNumber(tc1.mode(), fnum);
                if(tc != tc1) {
                        promekiErr("Frame %d: does not match '%s' != '%s'", fnum, tc1.toString().first.cstr(), tc.toString().first.cstr());
                        fnumCorrect = false;
                        break;
                }
                last = fnum;
        }
        PROMEKI_TEST(fnumCorrect);
        promekiInfo("TC1: %s", tc1.toString().first.cstr());
        PROMEKI_TEST(tc1.toFrameNumber().first == framesToTest);
        /*
        for(int i = 0; i < 60; i++) {
                tc1--;
                promekiInfo("%s", tc1.toString().cstr());
        }
        */
        for(int i = 0; i < framesToTest; i++) tc1--;
        promekiInfo("TC1: %s", tc1.toString().first.cstr());
        PROMEKI_TEST(tc1.toFrameNumber().first == 0);



PROMEKI_TEST_END()


