/*****************************************************************************
 * image.cpp
 * April 29, 2023
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
#include <promeki/image.h>

using namespace promeki;


PROMEKI_TEST_BEGIN(Image)
        ImageDesc d(1920, 1080, PixelFormat::RGB10);
        promekiInfo("ImageDesc: %s", d.toString().cstr());
        PROMEKI_TEST(d.size().isValid());
        const PixelFormat &pfmt = d.pixelFormat();

        Image img1(d);
        promekiInfo("Image: %s", img1.desc().toString().cstr());
        PROMEKI_TEST(pfmt.stride(d.size()) == 1920 * 4);
        PROMEKI_TEST(pfmt.size(d.size()) == 1920 * 1080 * 4);
        PROMEKI_TEST(img1.size().isValid());
        PROMEKI_TEST(img1.desc().isValid());
        PROMEKI_TEST(img1.isValid());
        PROMEKI_TEST(img1.fill(42));

        char *data = static_cast<char *>(img1.plane().data());

        String hexDump;
        for(int i = 0; i < 16; i++) {
                hexDump += String::number(data[i], 16, 2, '0');
                hexDump += ' ';
        }
        promekiInfo("Data: %s", hexDump.cstr());
        PROMEKI_TEST(data[0] == 42);

PROMEKI_TEST_END()

