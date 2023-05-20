/*****************************************************************************
 * list.h
 * May 15, 2023
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
#include <cstddef>
#include <vector>
#include <initializer_list>
#include <functional>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

template <typename T>
class List {
        public:
                using Data = typename std::vector<T>;
                using Iterator = typename std::vector<T>::iterator;
                using ConstIterator = typename std::vector<T>::const_iterator;
                using RevIterator = typename std::vector<T>::reverse_iterator;
                using ConstRevIterator = typename std::vector<T>::const_reverse_iterator;
                using TestFunc = std::function<bool(const T &)>;

                List() = default;
                explicit List(size_t size) : d(size) {}
                List(size_t size, const T &defaultValue) : d(size, defaultValue) {}
                List(const List &other) : d(other.d) {}
                List(List &&other) noexcept : d(std::move(other.d)) {}
                List(std::initializer_list<T> initList) : d(initList) {}
                virtual ~List() {};

                List &operator=(const List &other) {
                        d = other.d;
                        return *this;
                }

                List &operator=(List &&other) noexcept {
                        d = std::move(other.d);
                        return *this;
                }

                List &operator=(std::initializer_list<T> initList) {
                        d = initList;
                        return *this;
                }

                List &operator+=(const T &item) {
                        pushToBack(item);
                        return *this;
                }

                List &operator+=(T &&item) {
                        emplaceToBack(std::move(item));
                        return *this;
                }

                List &operator+=(const List &list) {
                        pushToBack(list);
                        return *this;
                }

                List &operator+=(List &&list) {
                        emplaceToBack(std::move(list));
                        return *this;
                }

                Iterator begin() noexcept {
                        return d.begin();
                }

                ConstIterator constBegin() const noexcept {
                        return d.cbegin();
                }

                RevIterator revBegin() noexcept {
                        return d.rbegin();
                }

                ConstRevIterator constRevBegin() const noexcept {
                        return d.crbegin();
                }

                Iterator end() noexcept {
                        return d.end();
                }

                ConstIterator cend() const noexcept {
                        return d.cend();
                }

                RevIterator revEnd() noexcept {
                        return d.rend();
                }

                ConstRevIterator constRevEnd() const noexcept {
                        return d.crend();
                }

                T &at(size_t index) {
                        return d.at(index);
                }

                const T &at(size_t index) const {
                        return d.at(index);
                }

                T &operator[](size_t index) {
                        return d[index];
                }

                const T &operator[](size_t index) const {
                        return d[index];
                }

                T &front() {
                        return d.front();
                }

                const T &front() const {
                        return d.front();
                }

                T &back() {
                        return d.back();
                }

                const T &back() const {
                        return d.back();
                }

                T *data() noexcept {
                        return d.data();
                }

                const T *data() const noexcept {
                        return d.data();
                }
 
                bool isEmpty() const noexcept {
                        return d.empty();
                }

                size_t size() const noexcept {
                        return d.size();
                }

                size_t maxSize() const noexcept {
                        return d.max_size();
                }

                void reserve(size_t newCapacity) {
                        d.reserve(newCapacity);
                }

                size_t capacity() const noexcept {
                        return d.capacity();
                }

                void shrink() {
                        d.shrink_to_fit();
                        return;
                }

                void clear() noexcept {
                        d.clear();
                        return;
                }

                Iterator insert(ConstIterator pos, const T &value) {
                        return d.insert(pos, value);
                }

                Iterator emplace(ConstIterator pos, T &&value) {
                        return d.emplace(pos, std::move(value));
                }

                Iterator remove(ConstIterator pos) {
                        return d.erase(pos);
                }

                Iterator remove(size_t index) {
                        return d.remove(index);
                }

                void removeIf(TestFunc func) {
                        d.erase(std::remove_if(d.begin(), d.end(), func));
                        return;
                }

                bool removeFirst(const T &value) {
                        bool ret = false;
                        for(auto item = begin(); item != end(); ++item) {
                                if(*item == value) {
                                        remove(item);
                                        ret = true;
                                        break;
                                }
                        }
                        return ret;
                }

                void pushToBack(const T &value) {
                        d.push_back(value);
                        return;
                }

                void pushToBack(const List<T> &list) {
                        d.insert(d.end(), list.d.begin(), list.d.end());
                        return;
                }

                void emplaceToBack(T &&value) {
                        d.emplace_back(std::move(value));
                        return;
                }

                void emplaceToBack(List<T> &&list) {
                        d.insert(d.end(), std::make_move_iterator(list.d.begin()), std::make_move_iterator(list.d.end()));
                        return;
                }

                void popFromBack() {
                        d.pop_back();
                        return;
                }

                void resize(size_t newSize) {
                        d.resize(newSize);
                        return;
                }

                void resize(size_t newSize, const T &value) {
                        d.resize(newSize, value);
                        return;
                }

                void swap(List<T> &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                friend bool operator==(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.vec == rhs.vec;
                }

                friend bool operator!=(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.vec != rhs.vec;
                }

                friend bool operator<(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.vec < rhs.vec;
                }

                friend bool operator>(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.vec > rhs.vec;
                }

                friend bool operator<=(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.vec <= rhs.vec;
                }

                friend bool operator>=(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.vec >= rhs.vec;
                }

        private:
                Data  d;
};

PROMEKI_NAMESPACE_END

