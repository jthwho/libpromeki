/**
 * @file      test.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/objectbase.h>

using namespace promeki;

PROMEKI_DEBUG(ObjectBaseTest)

class TestOne : public ObjectBase {
                PROMEKI_OBJECT(TestOne, ObjectBase);

        public:
                TestOne(ObjectBase *p = nullptr) : ObjectBase(p) {}

                PROMEKI_SIGNAL(somethingHappened, const String &);
                PROMEKI_SIGNAL(foo, const String &, bool, void *);

                void makeSomethingHappen() {
                        somethingHappenedSignal.emit("Something!");
                        return;
                }

                void makeSomethingElseHappen() {
                        somethingHappenedSignal.emit("Something Else!");
                        return;
                }
};

class TestTwo : public ObjectBase {
                PROMEKI_OBJECT(TestTwo, ObjectBase);

        public:
                TestTwo(ObjectBase *p = nullptr) : ObjectBase(p) {}

                PROMEKI_SLOT(handleSomething, const String &);
};

inline void TestTwo::handleSomething(const String &val) {
        promekiInfo("TestTwo::handleSomething(%s) %p", val.cstr(), signalSender());
        return;
}
