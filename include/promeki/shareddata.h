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

namespace promeki {

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

template<typename T>
class SharedData {
	public:
		SharedData() : refCount(1) {}

		void ref() {
			refCount.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		void deref() {
			if(refCount.fetch_sub(1, std::memory_order_release) == 1) {
				std::atomic_thread_fence(std::memory_order_acquire);
				delete static_cast<T*>(this);
			}
		        return;
                }

		void refReset() {
			refCount = 1;
			return;
		}

	private:
		std::atomic<int> refCount;
};

template<typename T>
class SharedDataPtr {
	public:
		SharedDataPtr() : d(nullptr) {}

		SharedDataPtr(T *data) : d(data) {}

		SharedDataPtr(const SharedDataPtr &other) : d(other.d) {
			if(d) d->ref();
		}

		~SharedDataPtr() {
			if(d) d->deref();
		}

		SharedDataPtr &operator=(const SharedDataPtr &other) {
			if(this != &other) {
				if(d) d->deref();
				d = other.d;
				if(d) d->ref();
			}
			return *this;
		}

                bool isValid() const {
                        return d != nullptr;
                }

		T *data() const {
			return d;
		}

		const T *constData() const {
			return d;
		}

		void detach() {
			if(d == nullptr || d->refCount == 1) return;
			T *newData = new T(*d);
			d->deref();
			d = newData;
			d->refReset();
		}

		T *operator->() {
			detach();
			return d;
		}

		const T *operator->() const {
			return d;
		}

		T &operator*() {
			detach();
			return *d;
		}

		const T &operator*() const {
			return *d;
		}

	private:
		T *d;
};

// Like the normal SharedDataPtr, but doesn't detach (copy on write) on non-const access.
template<typename T>
class ExplicitSharedDataPtr {
	public:
		ExplicitSharedDataPtr() : d(nullptr) {}

		ExplicitSharedDataPtr(T *data) : d(data) {}

		ExplicitSharedDataPtr(const ExplicitSharedDataPtr &other) : d(other.d) {
			if(d) d->ref();
		}

		~ExplicitSharedDataPtr() {
			if(d) d->deref();
		}

		ExplicitSharedDataPtr &operator=(const ExplicitSharedDataPtr &other) {
			if(this != &other) {
				if(d) d->deref();
				d = other.d;
				if(d) d->ref();
			}
			return *this;
		}

                bool isValid() const {
                        return d != nullptr;
                }

		T *data() const {
			return d;
		}

		const T *constData() const {
			return d;
		}

		T *operator->() {
			return d;
		}

		const T *operator->() const {
			return d;
		}

		T &operator*() {
			return *d;
		}

		const T &operator*() const {
			return *d;
		}

	private:
		T *d;
};

} // namespace promeki


