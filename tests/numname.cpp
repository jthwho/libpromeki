/**
 * @file      numname.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/numname.h>
#include <promeki/numnameseq.h>
#include <promeki/stringlist.h>

using namespace promeki;

PROMEKI_TEST_BEGIN(NumName)
        NumName s1("test.00004.dpx");
        PROMEKI_TEST(s1.prefix() == "test.");
        PROMEKI_TEST(s1.suffix() == ".dpx");
        PROMEKI_TEST(s1.isPadded() == true);
        PROMEKI_TEST(s1.digits() == 5);
        PROMEKI_TEST(s1.hashmask() == "test.#####.dpx");
        PROMEKI_TEST(s1.filemask() == "test.%05d.dpx");
        PROMEKI_TEST(s1.name(6438) == "test.06438.dpx");
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(NumNameSeq)
        StringList list = {
                "image1234.png",
                "image0001.png",
                "image.png",
                "image2.png",
                "image3.png",
                "image34.png",
                "anotherfile.txt"
        };
        NumNameSeq::List nnl = NumNameSeq::parseList(list);
        for(const auto item : nnl) {
                promekiInfo("%s [%d %d]", item.name().hashmask().cstr(), (int)item.head(), (int)item.tail());
        }
        PROMEKI_TEST(nnl.size() == 2);
        PROMEKI_TEST(list.size() == 2);
        PROMEKI_TEST(nnl[0].name().hashmask() == "image####.png");
        PROMEKI_TEST(nnl[1].name().hashmask() == "image#.png");
        PROMEKI_TEST(list[0] == "image.png");
        PROMEKI_TEST(list[1] == "anotherfile.txt");
PROMEKI_TEST_END()


