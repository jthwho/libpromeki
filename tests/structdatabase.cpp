/**
 * @file      structdatabase.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/structdatabase.h>

using namespace promeki;

namespace {
        struct TestEntry {
                int id = 0;
                String name;
                int value = 0;
        };
}

TEST_CASE("StructDatabase: default construction") {
        StructDatabase<int, TestEntry> db;
        CHECK(db.database().isEmpty());
}

TEST_CASE("StructDatabase: construction from initializer list") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0},
                {1, "First",  10},
                {2, "Second", 20}
        });
        CHECK(db.database().size() == 3);
}

TEST_CASE("StructDatabase: get by id") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0},
                {1, "First",  10},
                {2, "Second", 20}
        });
        const auto &entry = db.get(1);
        CHECK(entry.name == "First");
        CHECK(entry.value == 10);
}

TEST_CASE("StructDatabase: get unknown id falls back to id 0") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0},
                {1, "First",  10}
        });
        const auto &entry = db.get(99);
        CHECK(entry.name == "Unknown");
        CHECK(entry.id == 0);
}

TEST_CASE("StructDatabase: lookupKeyByName") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0},
                {1, "First",  10},
                {2, "Second", 20}
        });
        CHECK(db.lookupKeyByName("First") == 1);
        CHECK(db.lookupKeyByName("Second") == 2);
}

TEST_CASE("StructDatabase: lookupKeyByName unknown returns 0") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0},
                {1, "First",  10}
        });
        CHECK(db.lookupKeyByName("NonExistent") == 0);
}

TEST_CASE("StructDatabase: add entry") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0}
        });
        db.add({3, "Third", 30});
        const auto &entry = db.get(3);
        CHECK(entry.name == "Third");
        CHECK(entry.value == 30);
}

TEST_CASE("StructDatabase: database accessor") {
        StructDatabase<int, TestEntry> db({
                {0, "Unknown", 0},
                {1, "First",  10}
        });
        const auto &map = db.database();
        CHECK(map.size() == 2);
}
