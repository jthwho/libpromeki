/*****************************************************************************
 * shareddata.h
 * April 09, 2023
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

#pragma once

#include <iostream>
#include <memory>
#include <atomic>
#include <promeki/namespace.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

/* Usage Example: 
 
class MyData : public SharedData<MyData> {
public:
    MyData() : value(0) {}
    MyData(int v) : value(v) {}
    MyData(const MyData &other) : value(other.value) {}

    int value;
};

int main() {
    SharedDataPtr<MyData> data1(new MyData(5));
    SharedDataPtr<MyData> data2(data1);

    std::cout << "Data1: " << data1->value << std::endl; // 5
    std::cout << "Data2: " << data2->value << std::endl; // 5

    data1->value = 10;
    std::cout << "Data1: " << data1->value << std::endl; // 10
    std::cout << "Data2: " << data2->value << std::endl; // 5
}
*/

class SharedData {
	public:
                ~SharedData() = default;
		SharedData() : __ref(0) { }
                SharedData(const SharedData &o) : __ref(0) { }
                SharedData &operator=(const SharedData &) = delete;

		void ref() const {
			__ref.fetch_add(1, std::memory_order_relaxed);
                        return;
		}

                // Returns true if the data has been fully deref'ed
	        bool deref() const {
			return __ref.fetch_sub(1, std::memory_order_release) == 1;
                }

                int refCount() const {
                        return __ref;
                }

        private:
                mutable std::atomic<int> __ref;
};

template<typename T>
class SharedDataPtr {
	public:
		SharedDataPtr() : d(nullptr) { 
                        //promekiInfo("%p created (empty)", this);
                }

                SharedDataPtr(T *data) : d(data) {
                        //promekiInfo("%p created with data %p", this, d);
                        if(d != nullptr) d->ref();
                }
	
                SharedDataPtr(const SharedDataPtr &other) : d(other.d) { 
                        //promekiInfo("%p copied with data %p", this, d);
                        if(d != nullptr) d->ref(); 
                }
		
                ~SharedDataPtr() { 
                        //promekiInfo("%p destroyed, data %p", this, d);
                        if(d != nullptr && d->deref()) delete d;
                }
		
                SharedDataPtr &operator=(const SharedDataPtr &other) {
                        //promekiInfo("%p assign from %p, old %p, new %p", this, &other, d, other.d);
			if(d == other.d) return *this;
			if(d != nullptr && d->deref()) delete d;
			d = other.d;
			if(d != nullptr) d->ref();
			return *this;
		}

                bool isValid() const {
                        return d != nullptr;
                }
                
                void detach() {
                        if(d == nullptr || d->refCount() == 1) return;
                        //promekiInfo("%p detach start with %p", this, d);
                        T *x = new T(*d);
                        x->ref();
                        if(d->deref()) delete d;
                        d = x;
                        //promekiInfo("%p detach end with %p", this, d);
                        return;
                }

		T *operator->() {
                        //promekiInfo("%p non const ->", this);
			detach();
			return d;
		}

		const T *operator->() const {
			return d;
		}

		T &operator*() {
                        //promekiInfo("%p non const *", this);
			detach();
			return *d;
		}

		const T &operator*() const {
			return *d;
		}

	private:
		T *d = nullptr;
};

template<typename T>
class ExplicitSharedDataPtr {
	public:
		ExplicitSharedDataPtr() : d(nullptr) { 
                        //promekiInfo("%p created (empty)", this);
                }

                ExplicitSharedDataPtr(T *data) : d(data) {
                        //promekiInfo("%p created with data %p", this, d);
                        if(d != nullptr) d->ref();
                }
	
                ExplicitSharedDataPtr(const ExplicitSharedDataPtr &other) : d(other.d) { 
                        //promekiInfo("%p copied with data %p", this, d);
                        if(d != nullptr) d->ref(); 
                }
		
                ~ExplicitSharedDataPtr() { 
                        //promekiInfo("%p destroyed, data %p", this, d);
                        if(d != nullptr && d->deref()) delete d;
                }
		
                ExplicitSharedDataPtr &operator=(const ExplicitSharedDataPtr &other) {
                        //promekiInfo("%p assign from %p, old %p, new %p", this, &other, d, other.d);
			if(d == other.d) return *this;
			if(d != nullptr && d->deref()) delete d;
			d = other.d;
			if(d != nullptr) d->ref();
			return *this;
		}

                bool isValid() const {
                        return d != nullptr;
                }
                
                void detach() {
                        if(d == nullptr || d->refCount() == 1) return;
                        //promekiInfo("%p detach start with %p", this, d);
                        T *x = new T(*d);
                        x->ref();
                        if(d->deref()) delete d;
                        d = x;
                        //promekiInfo("%p detach end with %p", this, d);
                        return;
                }

		T *operator->() {
                        //promekiInfo("%p non const ->", this);
			return d;
		}

		const T *operator->() const {
			return d;
		}

		T &operator*() {
                        //promekiInfo("%p non const *", this);
			return *d;
		}

		const T &operator*() const {
			return *d;
		}

	private:
		T *d = nullptr;
};

PROMEKI_NAMESPACE_END

