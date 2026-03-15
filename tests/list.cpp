/**
 * @file      list.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/list.h>
#include <promeki/core/string.h>

using namespace promeki;

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("List_DefaultConstruction") {
        List<int> list;
        CHECK(list.isEmpty());
        CHECK(list.size() == 0);
}

TEST_CASE("List_SizeConstruction") {
        List<int> list(5);
        CHECK(list.size() == 5);
        CHECK_FALSE(list.isEmpty());
        for(size_t i = 0; i < list.size(); i++) {
                CHECK(list[i] == 0);
        }
}

TEST_CASE("List_SizeDefaultValueConstruction") {
        List<int> list(3, 42);
        CHECK(list.size() == 3);
        CHECK(list[0] == 42);
        CHECK(list[1] == 42);
        CHECK(list[2] == 42);
}

TEST_CASE("List_InitializerListConstruction") {
        List<int> list = {10, 20, 30, 40};
        CHECK(list.size() == 4);
        CHECK(list[0] == 10);
        CHECK(list[1] == 20);
        CHECK(list[2] == 30);
        CHECK(list[3] == 40);
}

TEST_CASE("List_CopyConstruction") {
        List<int> a = {1, 2, 3};
        List<int> b(a);
        CHECK(b.size() == 3);
        CHECK(b[0] == 1);
        CHECK(b[1] == 2);
        CHECK(b[2] == 3);
        // Modifying copy doesn't affect original
        b[0] = 99;
        CHECK(a[0] == 1);
}

TEST_CASE("List_MoveConstruction") {
        List<int> a = {1, 2, 3};
        List<int> b(std::move(a));
        CHECK(b.size() == 3);
        CHECK(b[0] == 1);
        CHECK(a.isEmpty());
}

// ============================================================================
// Assignment
// ============================================================================

TEST_CASE("List_CopyAssignment") {
        List<int> a = {1, 2, 3};
        List<int> b;
        b = a;
        CHECK(b == a);
        b[0] = 99;
        CHECK(a[0] == 1);
}

TEST_CASE("List_MoveAssignment") {
        List<int> a = {1, 2, 3};
        List<int> b;
        b = std::move(a);
        CHECK(b.size() == 3);
        CHECK(a.isEmpty());
}

TEST_CASE("List_InitializerListAssignment") {
        List<int> list;
        list = {5, 10, 15};
        CHECK(list.size() == 3);
        CHECK(list[0] == 5);
        CHECK(list[1] == 10);
        CHECK(list[2] == 15);
}

// ============================================================================
// operator+=
// ============================================================================

TEST_CASE("List_PlusEqualsItem") {
        List<int> list;
        list += 1;
        list += 2;
        list += 3;
        CHECK(list.size() == 3);
        CHECK(list[0] == 1);
        CHECK(list[2] == 3);
}

TEST_CASE("List_PlusEqualsMoveItem") {
        List<String> list;
        String s = "hello";
        list += std::move(s);
        CHECK(list.size() == 1);
        CHECK(list[0] == "hello");
}

TEST_CASE("List_PlusEqualsList") {
        List<int> a = {1, 2};
        List<int> b = {3, 4};
        a += b;
        CHECK(a.size() == 4);
        CHECK(a[2] == 3);
        CHECK(a[3] == 4);
        // b is unchanged
        CHECK(b.size() == 2);
}

TEST_CASE("List_PlusEqualsMoveList") {
        List<int> a = {1, 2};
        List<int> b = {3, 4};
        a += std::move(b);
        CHECK(a.size() == 4);
        CHECK(a[2] == 3);
        CHECK(a[3] == 4);
}

// ============================================================================
// Element access
// ============================================================================

TEST_CASE("List_At") {
        List<int> list = {10, 20, 30};
        CHECK(list.at(0) == 10);
        CHECK(list.at(2) == 30);
        CHECK_THROWS_AS(list.at(5), std::out_of_range);
}

TEST_CASE("List_ConstAt") {
        const List<int> list = {10, 20, 30};
        CHECK(list.at(1) == 20);
        CHECK_THROWS_AS(list.at(5), std::out_of_range);
}

TEST_CASE("List_Subscript") {
        List<int> list = {10, 20, 30};
        CHECK(list[0] == 10);
        list[1] = 99;
        CHECK(list[1] == 99);
}

TEST_CASE("List_FrontBack") {
        List<int> list = {10, 20, 30};
        CHECK(list.front() == 10);
        CHECK(list.back() == 30);
        list.front() = 5;
        list.back() = 50;
        CHECK(list[0] == 5);
        CHECK(list[2] == 50);
}

TEST_CASE("List_ConstFrontBack") {
        const List<int> list = {10, 20, 30};
        CHECK(list.front() == 10);
        CHECK(list.back() == 30);
}

TEST_CASE("List_Data") {
        List<int> list = {1, 2, 3};
        int *p = list.data();
        CHECK(p[0] == 1);
        CHECK(p[2] == 3);
        p[1] = 42;
        CHECK(list[1] == 42);
}

TEST_CASE("List_ConstData") {
        const List<int> list = {1, 2, 3};
        const int *p = list.data();
        CHECK(p[0] == 1);
        CHECK(p[2] == 3);
}

// ============================================================================
// Size and capacity
// ============================================================================

TEST_CASE("List_IsEmpty") {
        List<int> list;
        CHECK(list.isEmpty());
        list += 1;
        CHECK_FALSE(list.isEmpty());
        list.clear();
        CHECK(list.isEmpty());
}

TEST_CASE("List_Size") {
        List<int> list = {1, 2, 3, 4, 5};
        CHECK(list.size() == 5);
}

TEST_CASE("List_ReserveCapacity") {
        List<int> list;
        list.reserve(100);
        CHECK(list.capacity() >= 100);
        CHECK(list.isEmpty());
}

TEST_CASE("List_Shrink") {
        List<int> list;
        list.reserve(1000);
        list += 1;
        list += 2;
        size_t bigCap = list.capacity();
        list.shrink();
        CHECK(list.capacity() <= bigCap);
        CHECK(list.size() == 2);
}

// ============================================================================
// Modifiers
// ============================================================================

TEST_CASE("List_Clear") {
        List<int> list = {1, 2, 3};
        list.clear();
        CHECK(list.isEmpty());
        CHECK(list.size() == 0);
}

TEST_CASE("List_InsertByIterator") {
        List<int> list = {1, 3};
        list.insert(list.cbegin() + 1, 2);
        CHECK(list.size() == 3);
        CHECK(list[0] == 1);
        CHECK(list[1] == 2);
        CHECK(list[2] == 3);
}

TEST_CASE("List_InsertByIndex") {
        List<int> list = {1, 3};
        list.insert((size_t)0, 0);
        CHECK(list.size() == 3);
        CHECK(list[0] == 0);
        CHECK(list[1] == 1);
        CHECK(list[2] == 3);
}

TEST_CASE("List_InsertMoveByIndex") {
        List<String> list;
        list += String("a");
        list += String("c");
        list.insert((size_t)1, String("b"));
        CHECK(list.size() == 3);
        CHECK(list[1] == "b");
}

TEST_CASE("List_EmplaceByIterator") {
        List<String> list;
        list += String("hello");
        list.emplace(list.cbegin(), "world");
        CHECK(list.size() == 2);
        CHECK(list[0] == "world");
        CHECK(list[1] == "hello");
}

TEST_CASE("List_EmplaceByIndex") {
        List<String> list;
        list += String("hello");
        list.emplace((size_t)0, "world");
        CHECK(list.size() == 2);
        CHECK(list[0] == "world");
}

TEST_CASE("List_RemoveByIterator") {
        List<int> list = {10, 20, 30};
        auto it = list.remove(list.cbegin() + 1);
        CHECK(list.size() == 2);
        CHECK(list[0] == 10);
        CHECK(list[1] == 30);
        CHECK(*it == 30);
}

TEST_CASE("List_RemoveByIndex") {
        List<int> list = {10, 20, 30};
        list.remove((size_t)0);
        CHECK(list.size() == 2);
        CHECK(list[0] == 20);
}

TEST_CASE("List_RemoveIf") {
        List<int> list = {1, 2, 3, 4, 5, 6};
        list.removeIf([](const int &v) { return v % 2 == 0; });
        CHECK(list.size() == 3);
        CHECK(list[0] == 1);
        CHECK(list[1] == 3);
        CHECK(list[2] == 5);
}

TEST_CASE("List_RemoveFirst") {
        List<int> list = {1, 2, 3, 2, 4};
        CHECK(list.removeFirst(2));
        CHECK(list.size() == 4);
        CHECK(list[0] == 1);
        CHECK(list[1] == 3);
        CHECK(list[2] == 2);
        CHECK(list[3] == 4);
}

TEST_CASE("List_RemoveFirstNotFound") {
        List<int> list = {1, 2, 3};
        CHECK_FALSE(list.removeFirst(99));
        CHECK(list.size() == 3);
}

TEST_CASE("List_PushToBack") {
        List<int> list;
        list.pushToBack(1);
        list.pushToBack(2);
        list.pushToBack(3);
        CHECK(list.size() == 3);
        CHECK(list[0] == 1);
        CHECK(list[2] == 3);
}

TEST_CASE("List_PushToBackList") {
        List<int> a = {1, 2};
        List<int> b = {3, 4};
        a.pushToBack(b);
        CHECK(a.size() == 4);
        CHECK(a[2] == 3);
        CHECK(a[3] == 4);
}

TEST_CASE("List_PushToBackMoveItem") {
        List<String> list;
        String s = "test";
        list.pushToBack(std::move(s));
        CHECK(list.size() == 1);
        CHECK(list[0] == "test");
}

TEST_CASE("List_PushToBackMoveList") {
        List<int> a = {1, 2};
        List<int> b = {3, 4};
        a.pushToBack(std::move(b));
        CHECK(a.size() == 4);
        CHECK(a[2] == 3);
        CHECK(a[3] == 4);
}

TEST_CASE("List_EmplaceToBack") {
        List<String> list;
        list.emplaceToBack("hello");
        list.emplaceToBack("world");
        CHECK(list.size() == 2);
        CHECK(list[0] == "hello");
        CHECK(list[1] == "world");
}

TEST_CASE("List_PopFromBack") {
        List<int> list = {1, 2, 3};
        list.popFromBack();
        CHECK(list.size() == 2);
        CHECK(list.back() == 2);
}

TEST_CASE("List_Resize") {
        List<int> list = {1, 2, 3};
        list.resize(5);
        CHECK(list.size() == 5);
        CHECK(list[3] == 0);
        CHECK(list[4] == 0);
        list.resize(2);
        CHECK(list.size() == 2);
        CHECK(list[1] == 2);
}

TEST_CASE("List_ResizeWithValue") {
        List<int> list;
        list.resize(3, 42);
        CHECK(list.size() == 3);
        CHECK(list[0] == 42);
        CHECK(list[1] == 42);
        CHECK(list[2] == 42);
}

TEST_CASE("List_Swap") {
        List<int> a = {1, 2};
        List<int> b = {3, 4, 5};
        a.swap(b);
        CHECK(a.size() == 3);
        CHECK(a[0] == 3);
        CHECK(b.size() == 2);
        CHECK(b[0] == 1);
}

TEST_CASE("List_Set") {
        List<int> list = {10, 20, 30};
        CHECK(list.set(1, 99));
        CHECK(list[1] == 99);
        CHECK_FALSE(list.set(5, 42));
}

// ============================================================================
// Algorithms
// ============================================================================

TEST_CASE("List_Sort") {
        List<int> list = {3, 1, 4, 1, 5, 9, 2, 6};
        List<int> sorted = list.sort();
        CHECK(sorted.size() == 8);
        CHECK(sorted[0] == 1);
        CHECK(sorted[1] == 1);
        CHECK(sorted[2] == 2);
        CHECK(sorted[3] == 3);
        CHECK(sorted[7] == 9);
        // Original unchanged
        CHECK(list[0] == 3);
}

TEST_CASE("List_Reverse") {
        List<int> list = {1, 2, 3, 4};
        List<int> rev = list.reverse();
        CHECK(rev.size() == 4);
        CHECK(rev[0] == 4);
        CHECK(rev[1] == 3);
        CHECK(rev[2] == 2);
        CHECK(rev[3] == 1);
        // Original unchanged
        CHECK(list[0] == 1);
}

TEST_CASE("List_Unique") {
        List<int> list = {3, 1, 2, 1, 3, 2, 4};
        List<int> u = list.unique();
        CHECK(u.size() == 4);
        CHECK(u[0] == 1);
        CHECK(u[1] == 2);
        CHECK(u[2] == 3);
        CHECK(u[3] == 4);
}

TEST_CASE("List_Contains") {
        List<int> list = {10, 20, 30};
        CHECK(list.contains(20));
        CHECK_FALSE(list.contains(99));
}

TEST_CASE("List_ContainsEmpty") {
        List<int> list;
        CHECK_FALSE(list.contains(1));
}

// ============================================================================
// Comparison operators
// ============================================================================

TEST_CASE("List_Equality") {
        List<int> a = {1, 2, 3};
        List<int> b = {1, 2, 3};
        List<int> c = {1, 2, 4};
        CHECK(a == b);
        CHECK_FALSE(a == c);
        CHECK(a != c);
        CHECK_FALSE(a != b);
}

TEST_CASE("List_LessThan") {
        List<int> a = {1, 2, 3};
        List<int> b = {1, 2, 4};
        List<int> c = {1, 2};
        CHECK(a < b);
        CHECK_FALSE(b < a);
        CHECK(c < a);
}

TEST_CASE("List_GreaterThan") {
        List<int> a = {1, 2, 4};
        List<int> b = {1, 2, 3};
        CHECK(a > b);
        CHECK_FALSE(b > a);
}

TEST_CASE("List_LessEqual") {
        List<int> a = {1, 2, 3};
        List<int> b = {1, 2, 3};
        List<int> c = {1, 2, 4};
        CHECK(a <= b);
        CHECK(a <= c);
        CHECK_FALSE(c <= a);
}

TEST_CASE("List_GreaterEqual") {
        List<int> a = {1, 2, 3};
        List<int> b = {1, 2, 3};
        List<int> c = {1, 2, 2};
        CHECK(a >= b);
        CHECK(a >= c);
        CHECK_FALSE(c >= a);
}

// ============================================================================
// Iterators
// ============================================================================

TEST_CASE("List_RangeFor") {
        List<int> list = {1, 2, 3};
        int sum = 0;
        for(const auto &v : list) sum += v;
        CHECK(sum == 6);
}

TEST_CASE("List_ReverseIteration") {
        List<int> list = {1, 2, 3};
        List<int> rev;
        for(auto it = list.rbegin(); it != list.rend(); ++it) {
                rev += *it;
        }
        CHECK(rev[0] == 3);
        CHECK(rev[1] == 2);
        CHECK(rev[2] == 1);
}

TEST_CASE("List_ConstIterator") {
        const List<int> list = {10, 20, 30};
        int sum = 0;
        for(auto it = list.constBegin(); it != list.constEnd(); ++it) {
                sum += *it;
        }
        CHECK(sum == 60);
}

// ============================================================================
// With non-trivial types
// ============================================================================

TEST_CASE("List_WithStrings") {
        List<String> list;
        list += String("hello");
        list += String("world");
        list += String("test");
        CHECK(list.size() == 3);
        CHECK(list.contains(String("world")));
        CHECK_FALSE(list.contains(String("missing")));

        List<String> sorted = list.sort();
        CHECK(sorted[0] == "hello");
        CHECK(sorted[1] == "test");
        CHECK(sorted[2] == "world");
}

TEST_CASE("List: forEach") {
        List<int> l = {1, 2, 3};
        int sum = 0;
        l.forEach([&](int v) { sum += v; });
        CHECK(sum == 6);
}

TEST_CASE("List: indexOf") {
        List<int> l = {10, 20, 30, 20, 40};
        CHECK(l.indexOf(20) == 1);
        CHECK(l.indexOf(40) == 4);
        CHECK(l.indexOf(99) == -1);
}

TEST_CASE("List: lastIndexOf") {
        List<int> l = {10, 20, 30, 20, 40};
        CHECK(l.lastIndexOf(20) == 3);
        CHECK(l.lastIndexOf(10) == 0);
        CHECK(l.lastIndexOf(99) == -1);
}

TEST_CASE("List: count") {
        List<int> l = {1, 2, 3, 2, 1, 2};
        CHECK(l.count(2) == 3);
        CHECK(l.count(1) == 2);
        CHECK(l.count(3) == 1);
        CHECK(l.count(99) == 0);
}

TEST_CASE("List: mid") {
        List<int> l = {10, 20, 30, 40, 50};
        auto m = l.mid(1, 3);
        CHECK(m.size() == 3);
        CHECK(m[0] == 20);
        CHECK(m[1] == 30);
        CHECK(m[2] == 40);
}

TEST_CASE("List: mid edge cases") {
        List<int> l = {1, 2, 3};
        auto m1 = l.mid(0, 10);
        CHECK(m1.size() == 3);
        auto m2 = l.mid(5, 2);
        CHECK(m2.isEmpty());
}
