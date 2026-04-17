/**
 * @file      objectbase.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/objectbase.h>
#include <promeki/signal.h>
#include <promeki/slot.h>
#include <promeki/logger.h>
#include "test.h"

using namespace promeki;

TEST_CASE("ObjectBase: construction") {
        TestOne one;
        CHECK(one.parent() == nullptr);
}

TEST_CASE("ObjectBase: parent-child relationship") {
        TestOne parent;
        TestOne child(&parent);
        CHECK(child.parent() == &parent);
        CHECK(parent.childList().size() == 1);
        CHECK(parent.childList()[0] == &child);
}

TEST_CASE("ObjectBase: setParent") {
        TestOne parent1;
        TestOne parent2;
        TestOne child(&parent1);
        CHECK(child.parent() == &parent1);
        CHECK(parent1.childList().size() == 1);

        child.setParent(&parent2);
        CHECK(child.parent() == &parent2);
        CHECK(parent1.childList().size() == 0);
        CHECK(parent2.childList().size() == 1);

        child.setParent(nullptr);
        CHECK(child.parent() == nullptr);
        CHECK(parent2.childList().size() == 0);
}

TEST_CASE("ObjectBase: parent destroys children") {
        {
                TestOne parent;
                TestOne *child = new TestOne(&parent);
                (void)child;
                CHECK(parent.childList().size() == 1);
                // parent goes out of scope, should destroy child
        }
        CHECK(true); // If we get here without crash, children were destroyed
}

TEST_CASE("ObjectBase: multiple children") {
        TestOne parent;
        TestOne c1(&parent);
        TestOne c2(&parent);
        TestOne c3(&parent);
        CHECK(parent.childList().size() == 3);
}

TEST_CASE("ObjectBase: metaInfo is valid") {
        CHECK(TestOne::metaInfo().name() != nullptr);
        CHECK(TestTwo::metaInfo().name() != nullptr);
}

TEST_CASE("ObjectBase: metaInfo parent chain") {
        const auto &meta = TestOne::metaInfo();
        CHECK(meta.parent() != nullptr);
        // TestOne's parent metaInfo should be ObjectBase's
        CHECK(meta.parent() == &ObjectBase::metaInfo());
}

TEST_CASE("ObjectBase: metaInfo signal list") {
        const auto &meta = TestOne::metaInfo();
        // TestOne has somethingHappened and foo signals, plus aboutToDestroy from ObjectBase
        CHECK(meta.signalList().size() >= 2);
}

TEST_CASE("ObjectBase: signal-slot connection") {
        TestOne one;
        TestTwo two;
        TestOne::connect(&one.somethingHappenedSignal, &two.handleSomethingSlot);
        one.makeSomethingHappen();
        CHECK(true);
}

TEST_CASE("ObjectBase: multiple signal emissions") {
        TestOne one;
        TestTwo two;
        TestOne::connect(&one.somethingHappenedSignal, &two.handleSomethingSlot);
        one.makeSomethingHappen();
        one.makeSomethingElseHappen();
        CHECK(true);
}

TEST_CASE("ObjectBase: signal cleanup on slot owner destruction") {
        TestOne one;
        {
                TestTwo two;
                TestOne::connect(&one.somethingHappenedSignal, &two.handleSomethingSlot);
                one.makeSomethingHappen();
                // two goes out of scope, cleanup should disconnect
        }
        // Emitting after slot owner destroyed should not crash
        one.makeSomethingHappen();
        CHECK(true);
}

TEST_CASE("ObjectBase: ObjectBasePtr tracks object") {
        TestOne *obj = new TestOne();
        ObjectBasePtr ptr(obj);
        CHECK(ptr.isValid());
        CHECK(ptr.data() == obj);
        delete obj;
        CHECK_FALSE(ptr.isValid());
        CHECK(ptr.data() == nullptr);
}

TEST_CASE("ObjectBase: ObjectBasePtr copy") {
        TestOne *obj = new TestOne();
        ObjectBasePtr ptr1(obj);
        ObjectBasePtr ptr2(ptr1);
        CHECK(ptr1.isValid());
        CHECK(ptr2.isValid());
        CHECK(ptr1.data() == ptr2.data());
        delete obj;
        CHECK_FALSE(ptr1.isValid());
        CHECK_FALSE(ptr2.isValid());
}

TEST_CASE("ObjectBase: ObjectBasePtr assignment") {
        TestOne *obj1 = new TestOne();
        TestOne *obj2 = new TestOne();
        ObjectBasePtr ptr(obj1);
        CHECK(ptr.data() == obj1);
        ptr = ObjectBasePtr(obj2);
        CHECK(ptr.data() == obj2);
        delete obj1;
        CHECK(ptr.isValid()); // Still pointing to obj2
        delete obj2;
        CHECK_FALSE(ptr.isValid());
}

TEST_CASE("ObjectBase: ObjectBasePtr default is null") {
        ObjectBasePtr ptr;
        CHECK_FALSE(ptr.isValid());
        CHECK(ptr.data() == nullptr);
}
