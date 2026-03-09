/**
 * @file      numname.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/numname.h>
#include <promeki/numnameseq.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>

using namespace promeki;

TEST_CASE("NumName") {
        NumName s1("test.00004.dpx");
        CHECK(s1.prefix() == "test.");
        CHECK(s1.suffix() == ".dpx");
        CHECK(s1.isPadded() == true);
        CHECK(s1.digits() == 5);
        CHECK(s1.hashmask() == "test.#####.dpx");
        CHECK(s1.filemask() == "test.%05d.dpx");
        CHECK(s1.name(6438) == "test.06438.dpx");
}

TEST_CASE("NumNameSeq") {
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
        CHECK(nnl.size() == 2);
        CHECK(list.size() == 2);
        CHECK(nnl[0].name().hashmask() == "image####.png");
        CHECK(nnl[1].name().hashmask() == "image#.png");
        CHECK(list[0] == "image.png");
        CHECK(list[1] == "anotherfile.txt");
}
