/**
 * @file      stringlist.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/stringlist.h>

using namespace promeki;

TEST_CASE("StringList: default construction") {
        StringList sl;
        CHECK(sl.isEmpty());
        CHECK(sl.size() == 0);
}

TEST_CASE("StringList: construction from C array") {
        const char *args[] = {"hello", "world", "foo"};
        StringList sl(3, args);
        CHECK(sl.size() == 3);
        CHECK(sl[0] == "hello");
        CHECK(sl[1] == "world");
        CHECK(sl[2] == "foo");
}

TEST_CASE("StringList: join with comma") {
        StringList sl;
        sl.pushToBack("a");
        sl.pushToBack("b");
        sl.pushToBack("c");
        CHECK(sl.join(", ") == "a, b, c");
}

TEST_CASE("StringList: join with empty delimiter") {
        StringList sl;
        sl.pushToBack("foo");
        sl.pushToBack("bar");
        CHECK(sl.join("") == "foobar");
}

TEST_CASE("StringList: join single element") {
        StringList sl;
        sl.pushToBack("only");
        CHECK(sl.join(", ") == "only");
}

TEST_CASE("StringList: join empty list") {
        StringList sl;
        CHECK(sl.join(", ") == "");
}

TEST_CASE("StringList: inherits List operations") {
        StringList sl;
        sl.pushToBack("first");
        sl.pushToBack("second");
        CHECK(sl.size() == 2);
        CHECK(sl.front() == "first");
        CHECK(sl.back() == "second");
}

TEST_CASE("StringList: initializer list construction") {
        StringList sl = {"one", "two", "three"};
        CHECK(sl.size() == 3);
        CHECK(sl[0] == "one");
}

TEST_CASE("StringList: copy") {
        StringList a = {"a", "b"};
        StringList b(a);
        CHECK(b.size() == 2);
        CHECK(b[0] == "a");
}

TEST_CASE("StringList: filter") {
        StringList sl = {"apple", "banana", "avocado", "blueberry", "apricot"};
        StringList filtered = sl.filter([](const String &s) {
                return s.startsWith('a');
        });
        CHECK(filtered.size() == 3);
        CHECK(filtered[0] == "apple");
        CHECK(filtered[1] == "avocado");
        CHECK(filtered[2] == "apricot");
}

TEST_CASE("StringList: filter no matches") {
        StringList sl = {"apple", "banana"};
        StringList filtered = sl.filter([](const String &s) {
                return s.startsWith('z');
        });
        CHECK(filtered.isEmpty());
}

TEST_CASE("StringList: filter all match") {
        StringList sl = {"a", "ab", "abc"};
        StringList filtered = sl.filter([](const String &s) {
                return s.startsWith('a');
        });
        CHECK(filtered.size() == 3);
}

TEST_CASE("StringList: indexOf found") {
        StringList sl = {"hello", "world", "foo"};
        auto r0 = sl.indexOf("hello");
        CHECK(r0.second() == Error::Ok);
        CHECK(r0.first() == 0u);
        auto r1 = sl.indexOf("world");
        CHECK(r1.second() == Error::Ok);
        CHECK(r1.first() == 1u);
        auto r2 = sl.indexOf("foo");
        CHECK(r2.second() == Error::Ok);
        CHECK(r2.first() == 2u);
}

TEST_CASE("StringList: indexOf not found") {
        StringList sl = {"hello", "world"};
        auto r = sl.indexOf("missing");
        CHECK(r.second() == Error::NotFound);
}

TEST_CASE("StringList: indexOf empty list") {
        StringList sl;
        auto r = sl.indexOf("anything");
        CHECK(r.second() == Error::NotFound);
}

TEST_CASE("StringList: split and rejoin roundtrip") {
        String original = "one:two:three:four";
        StringList parts = original.split(":");
        CHECK(parts.size() == 4);
        CHECK(parts.join(":") == original);
}

TEST_CASE("StringList: SharedPtr modify retains StringList type after CoW") {
        StringList::Ptr a = StringList::Ptr::create();
        a.modify()->pushToBack("hello");
        a.modify()->pushToBack("world");
        StringList::Ptr b = a;
        CHECK(b.referenceCount() == 2);
        b.modify()->pushToBack("foo");
        CHECK(a.referenceCount() == 1);
        CHECK(b.referenceCount() == 1);
        CHECK(a->size() == 2);
        CHECK(b->size() == 3);
        CHECK(a->join(",") == "hello,world");
        CHECK(b->join(",") == "hello,world,foo");
}

TEST_CASE("StringList: Ptr alias is SharedPtr<StringList> not SharedPtr<List<String>>") {
        static_assert(std::is_same_v<StringList::Ptr, SharedPtr<StringList>>);
}
