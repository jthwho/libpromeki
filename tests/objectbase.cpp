/*****************************************************************************
 * objectbase.cpp
 * May 19, 2023
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
#include <promeki/objectbase.h>

using namespace promeki;

PROMEKI_DEBUG(ObjectBaseTest)

class TestOne : public ObjectBase {
        public:
                TestOne(ObjectBase *p = nullptr) : ObjectBase(p) {

                }

                PROMEKI_SIGNAL(somethingHappened, const String &);

                void makeSomethingHappen() {
                        promekiInfo("%p is going to emit somethingHappened", this);
                        somethingHappened.emit("Something!");
                        return;
                }

                void makeSomethingElseHappen() {
                        auto params = somethingHappened.packParams("Something Else!");
                        somethingHappened.packedEmit(params);
                        return;
                }
};

class TestTwo : public ObjectBase {
        public:
                TestTwo(ObjectBase *p = nullptr) : ObjectBase(p) {

                }

                void handleSomething(const String &val) {
                        promekiInfo("TestTwo::handleSomething(%s) %p", val.cstr(), signalSender());
                        return;
                }

};

PROMEKI_TEST_BEGIN(ObjectBase)

        TestOne one;
        TestTwo two;

        one.somethingHappened.connect(&two, &TestTwo::handleSomething);

        one.makeSomethingHappen();
        one.makeSomethingElseHappen();

PROMEKI_TEST_END()
