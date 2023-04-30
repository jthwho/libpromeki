/*****************************************************************************
 * shareddata.cpp
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
#include <promeki/shareddata.h>

using namespace promeki;


class SharedDataTest {
        public:
                class Data : public SharedData {
                        public:
                                int     *value = nullptr;
                                int     set;
                                int     clear;

                                Data(int *v, int s, int c) : value(v), set(s), clear(c) {
                                        promekiInfo("%p created, set value to %d", this, set);
                                        *value = set;
                                }

                                Data(const Data &o) : SharedData(o), value(o.value), set(o.set), clear(o.clear) {
                                        promekiInfo("%p copied from %p, set value to %d", this, &o, set);
                                        *value = set;
                                }

                                ~Data() {
                                        promekiInfo("%p destroyed, set value to %d", this, clear); 
                                        *value = clear;
                                }
                };

                SharedDataTest(int *value, int set, int clear) : d(new Data(value, set, clear)) { }

                int refCount() const {
                        return d->refCount();
                }

                int value() const {
                        return *d->value;
                }

                int set() const {
                        return d->set;
                }

                int clear() const {
                        return d->clear;
                }

                void change(int *v, int s, int c) {
                        d->value = v;
                        d->set = s;
                        d->clear = c;
                        *d->value = d->set;
                        return;
                }

        private:
                SharedDataPtr<Data> d;
};

using SharedDataTestPtr = SharedDataPtr<SharedDataTest>;
//using ExplicitSharedDataPtr = ExplicitSharedDataPtr<SharedDataTest>;

PROMEKI_TEST_BEGIN(SharedData)
        int value1 = 0;
        int value2 = 0;
        int value3 = 0;
        SharedDataTest p1(&value1, 1, 2);
        {
                SharedDataTest p2(&value2, 11, 12);
                PROMEKI_TEST(p1.value() == 1);
                PROMEKI_TEST(p2.value() == 11);
                PROMEKI_TEST(p1.refCount() == 1);
                PROMEKI_TEST(p2.refCount() == 1);

                // Only one pointer, should not detach
                p1.change(&value1, 3, 4);
                PROMEKI_TEST(p1.value() == 3);
                PROMEKI_TEST(p1.set() == 3);

                SharedDataTest p3(p1);
                PROMEKI_TEST(p3.value() == 3);
                PROMEKI_TEST(p1.refCount() == 2);
                PROMEKI_TEST(p3.refCount() == 2);
                PROMEKI_TEST(p1.set() == 3);
                PROMEKI_TEST(p3.set() == 3);

                p3.change(&value3, 6, 7);
                PROMEKI_TEST(p1.refCount() == 1);
                PROMEKI_TEST(p3.refCount() == 1);
                PROMEKI_TEST(p1.set() == 3);
                PROMEKI_TEST(p3.set() == 6);
        }
        promekiInfo("1: %d, 2: %d, 3: %d", value1, value2, value3);
        PROMEKI_TEST(value1 == 3); // p1 hasn't gone out of scope yet.
        PROMEKI_TEST(value2 == 12);
        PROMEKI_TEST(value3 == 7);

PROMEKI_TEST_END()

