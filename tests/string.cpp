/*****************************************************************************
 * string.cpp
 * April 27, 2023
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
#include <promeki/unittest.h>
#include <promeki/string.h>

using namespace promeki;

PROMEKI_TEST_BEGIN(String)
        String nullstr;
        String s1 = "String 1";
        String s2("String 2");
        String s3(s2);
        String s4(" \t \n \r WHITE \n\t\nSPACE  \t\n\t\t\n   ");
        String s5("item1,item2,item3");

        PROMEKI_TEST(s1.toUpper() == "STRING 1");
        PROMEKI_TEST(s1.toLower() == "string 1");
        PROMEKI_TEST(s1 == "String 1");
        PROMEKI_TEST(s2 == "String 2");
        PROMEKI_TEST(s3 == "String 2");
        PROMEKI_TEST(s2 == s3);
        PROMEKI_TEST(s4.trim() == "WHITE \n\t\nSPACE");
        auto s5split = s5.split(",");
        PROMEKI_TEST(s5split.size() == 3);
        PROMEKI_TEST(s5split.at(0) == "item1");
        PROMEKI_TEST(s5split.at(1) == "item2");
        PROMEKI_TEST(s5split.at(2) == "item3");
        PROMEKI_TEST(s1.startsWith("Stri"));
        PROMEKI_TEST(!s1.startsWith("StrI"));
        PROMEKI_TEST(s1.endsWith("g 1"));
        PROMEKI_TEST(!s1.endsWith("gg1"));
        PROMEKI_TEST(s5.count("item") == 3);
        PROMEKI_TEST(s1.reverse() == "1 gnirtS");
        PROMEKI_TEST(String("1234").isNumeric());
        PROMEKI_TEST(!s1.isNumeric());
        PROMEKI_TEST(String::dec(1234) == "1234");
        PROMEKI_TEST(String::dec(1234, 6) == "  1234");
        PROMEKI_TEST(String::dec(1234, 6, 'x') == "xx1234");
        PROMEKI_TEST(String::hex(0x42, 4) == "0x0042");
        PROMEKI_TEST(String::bin(0b11111, 8) == "0b00011111");
        PROMEKI_TEST(String::number(42435) == "42435");
        PROMEKI_TEST(String::number(0x3472, 16) == "3472");
        PROMEKI_TEST(String::number(12345, 10, 10) == "     12345");
        PROMEKI_TEST(String::number(12345, 10, -10) == "12345     ");
        PROMEKI_TEST(String::number(12345, 20, 10, ' ', true) == "  b20:1AH5");
        PROMEKI_TEST(String::number(12345, 4, 6, ' ', true) == "b4:3000321");
        PROMEKI_TEST(String("%3 %2 %1").arg(3).arg(2).arg(1) == "1 2 3");
        PROMEKI_TEST(String("Two hundred and twenty-six billion, four hundred eighty-three million, One Hundred And Thirty-Four Thousand Two Hundred and Ninety-Six").parseNumberWords() == 226483134296);

        for(char &c : s2) c = 'x';
        PROMEKI_TEST(s2 == "xxxxxxxx");

PROMEKI_TEST_END()


