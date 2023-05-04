/*****************************************************************************
 * stringlist.h
 * May 02, 2023
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

#include <vector>
#include <promeki/string.h>
#include <promeki/logger.h>

namespace promeki {

class StringList {
        public:
                using Iterator = std::vector<String>::iterator;
                using ConstIterator = std::vector<String>::const_iterator;

                StringList() = default;
                explicit StringList(size_t n) : d(n) {}
                StringList(size_t n, const String& value) : d(n, value) {}
                StringList(std::initializer_list<String> init) : d(init) {}
                StringList(size_t ct, const char **items) : d{} {
                        for(int i = 0; i < ct; i++) {
                                add(String(items[i]));
                        }
                }
                template<typename Iterator> StringList(Iterator begin, Iterator end) : d(begin, end) {}

                const String &operator[](int index) const {
                        return d[index];
                }

                String &operator[](int index) {
                        return d[index];
                }

                bool operator==(const StringList& other) const {
                        return d == other.d;
                }

                bool operator!=(const StringList& other) const {
                        return !(*this == other);
                }

                StringList& operator+=(const StringList& other) {
                        d.insert(d.end(), other.d.begin(), other.d.end());
                        return *this;
                }

                StringList& operator+=(const String &str) {
                        d.push_back(str);
                        return *this;
                }

                void add(const String &val) {
                        d.push_back(val);
                        return;
                }

                void addToFront(const String &val) {
                        d.insert(d.begin(), val);
                        return;
                }

                void removeLast() {
                        if(!isEmpty()) d.pop_back();
                        return;
                }

                void removeFirst() {
                        if(!isEmpty()) d.erase(d.begin());
                        return;
                }

                String firstItem() const {
                        return isEmpty() ? String() : d.front();
                }

                String lastItem() const {
                        return isEmpty() ? String() : d.back();
                }

                String popFirst() {
                        String ret = firstItem();
                        removeFirst();
                        return ret;
                }

                String popLast() {
                        String ret = lastItem();
                        removeLast();
                        return ret;
                }

                void erase(const ConstIterator &val) {
                        d.erase(val);
                        return;
                }

                void erase(const Iterator &val) {
                        d.erase(val);
                        return;
                }

                void clear() {
                        d.clear();
                        return;
                }

                bool isEmpty() const {
                        return d.empty();
                }

                size_t size() const {
                        return d.size();
                }

                Iterator begin() {
                        return d.begin();
                }

                Iterator end() {
                        return d.end();
                }

                ConstIterator begin() const {
                        return d.begin();
                }

                ConstIterator end() const {
                        return d.end();
                }

                ConstIterator cbegin() const {
                        return d.cbegin();
                }

                ConstIterator cend() const {
                        return d.cend();
                }

                Iterator insert(ConstIterator pos, const String &val) {
                        return d.insert(pos, val);
                }

                Iterator insert(ConstIterator pos, size_t ct, const String &val) {
                        return d.insert(pos, ct, val);
                }

                template <typename InputIt> Iterator insert(ConstIterator pos, InputIt first, InputIt last) {
                        return d.insert(pos, first, last);
                }

                //Iterator insert(ConstIterator pos, initializer_list<String> list) {
                //        return d.insert(pos, list);
                //}

                StringList sort() const {
                        StringList ret = *this;
                        std::sort(ret.begin(), ret.end());
                        return ret;
                }

                StringList reverse() const {
                        StringList ret = *this;
                        std::reverse(ret.begin(), ret.end());
                        return ret;
                }

                StringList unique() {
                        StringList ret = sort();
                        d.erase(std::unique(ret.begin(), ret.end()), ret.end());
                        return ret;
                }

                String join(const String& delimiter) const {
                        String result;
                        for(const auto &str : d) {
                                result += str;
                                if(&str != &d.back()) result += delimiter;
                        }
                        return result;
                }
                
                int find(const String& str, size_t pos = 0) const {
                        if(pos >= d.size()) return -1;
                        auto it = std::find(d.begin() + pos, d.end(), str);
                        if(it == d.end()) return -1;
                        return static_cast<int>(std::distance(d.begin(), it));
                }

                template<typename Func> void forEach(Func func) {
                        for(auto& str : d) func(str);
                        return;
                }

                StringList subList(size_t start, size_t len) const {
                        if (start >= d.size()) {
                                return StringList();
                        }
                        auto end = std::min(start + len, d.size());
                        return StringList(d.begin() + start, d.begin() + end);
                }

                void remove(const String& str) {
                        d.erase(std::remove(d.begin(), d.end(), str), d.end());
                }

                void removeAt(size_t index) {
                        if (index < d.size()) {
                                d.erase(d.begin() + index);
                        }
                }

                void replace(const String& oldStr, const String& newStr) {
                        std::replace(d.begin(), d.end(), oldStr, newStr);
                        return;
                }
                
                friend StringList operator+(StringList lhs, const StringList& rhs) {
                        lhs += rhs;
                        return lhs;
                }

        private:        
                std::vector<String> d;

};

} // namespace promeki
