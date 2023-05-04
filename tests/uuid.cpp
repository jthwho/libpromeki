/*****************************************************************************
 * uuid.cpp
 * May 03, 2023
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
#include <promeki/uuid.h>

using namespace promeki;

PROMEKI_TEST_BEGIN(UUID)
        UUID v1;
        UUID v2 = UUID::generate();
        UUID v3("94eb2454-5116-4814-889f-7eb9bcb58bf1");
        UUID v4("94EB2454-5116-4814-889F-7EB9BCB58BF1");
        UUID v5("94EB2454-X116-4814-889F-7EB9BCB58BF1");

        String u1("91809c4d-3682-4868-800c-05b871b84c0b");
        UUID v6(u1);

        promekiInfo("v1: %s", v1.toString().cstr());
        promekiInfo("v2: %s", v2.toString().cstr());
        promekiInfo("v3: %s", v3.toString().cstr());
        promekiInfo("v4: %s", v4.toString().cstr());
        promekiInfo("v5: %s", v5.toString().cstr());

        PROMEKI_TEST(!v1.isValid());
        PROMEKI_TEST(v2.isValid());
        PROMEKI_TEST(v3.isValid());
        PROMEKI_TEST(v4.isValid());
        PROMEKI_TEST(!v5.isValid());
        PROMEKI_TEST(v3 == v4);
        PROMEKI_TEST(v6.toString() == u1);

PROMEKI_TEST_END()


