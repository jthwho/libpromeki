/**
 * @file      list.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <cstddef>
#include <vector>
#include <initializer_list>
#include <functional>
#include <algorithm>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Dynamic array container wrapping std::vector.
 *
 * Provides a Qt-inspired API over std::vector with consistent naming
 * conventions matching the rest of libpromeki.
 *
 * @tparam T Element type.
 */
template <typename T>
class List {
        PROMEKI_SHARED_FINAL(List)
        public:
                /** @brief Shared pointer type for List. */
                using Ptr = SharedPtr<List>;

                /** @brief Underlying std::vector storage type. */
                using Data = typename std::vector<T>;

                /** @brief Mutable forward iterator. */
                using Iterator = typename std::vector<T>::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename std::vector<T>::const_iterator;

                /** @brief Mutable reverse iterator. */
                using RevIterator = typename std::vector<T>::reverse_iterator;

                /** @brief Const reverse iterator. */
                using ConstRevIterator = typename std::vector<T>::const_reverse_iterator;

                /** @brief Predicate function type used by removeIf(). */
                using TestFunc = std::function<bool(const T &)>;

                /** @brief Default constructor. Creates an empty list. */
                List() = default;

                /**
                 * @brief Constructs a list with a given number of default-constructed elements.
                 * @param size Number of elements to create.
                 */
                explicit List(size_t size) : d(size) {}

                /**
                 * @brief Constructs a list with a given number of copies of a value.
                 * @param size Number of elements to create.
                 * @param defaultValue Value to copy into each element.
                 */
                List(size_t size, const T &defaultValue) : d(size, defaultValue) {}

                /** @brief Copy constructor. */
                List(const List &other) : d(other.d) {}

                /** @brief Move constructor. */
                List(List &&other) noexcept : d(std::move(other.d)) {}

                /**
                 * @brief Constructs a list from an initializer list.
                 * @param initList Brace-enclosed list of values.
                 */
                List(std::initializer_list<T> initList) : d(initList) {}

                /** @brief Destructor. */
                ~List() = default;

                /** @brief Copy assignment operator. */
                List &operator=(const List &other) {
                        d = other.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                List &operator=(List &&other) noexcept {
                        d = std::move(other.d);
                        return *this;
                }

                /**
                 * @brief Assigns from an initializer list, replacing all contents.
                 * @param initList Brace-enclosed list of values.
                 * @return Reference to this list.
                 */
                List &operator=(std::initializer_list<T> initList) {
                        d = initList;
                        return *this;
                }

                /**
                 * @brief Appends a single item to the back of the list.
                 * @param item The item to append.
                 * @return Reference to this list.
                 */
                List &operator+=(const T &item) {
                        pushToBack(item);
                        return *this;
                }

                /**
                 * @brief Appends a single item to the back of the list (move overload).
                 * @param item The item to move-append.
                 * @return Reference to this list.
                 */
                List &operator+=(T &&item) {
                        pushToBack(std::move(item));
                        return *this;
                }

                /**
                 * @brief Appends all items from another list to the back of this list.
                 * @param list The list to append.
                 * @return Reference to this list.
                 */
                List &operator+=(const List &list) {
                        pushToBack(list);
                        return *this;
                }

                /**
                 * @brief Appends all items from another list (move overload).
                 * @param list The list to move-append.
                 * @return Reference to this list.
                 */
                List &operator+=(List &&list) {
                        pushToBack(std::move(list));
                        return *this;
                }

                /** @brief Returns a mutable iterator to the first element. */
                Iterator begin() noexcept {
                        return d.begin();
                }

                /** @brief Returns a const iterator to the first element. */
                ConstIterator begin() const noexcept {
                        return d.cbegin();
                }

                /** @brief Returns a const iterator to the first element. */
                ConstIterator cbegin() const noexcept {
                        return d.cbegin();
                }

                /// @copydoc cbegin()
                ConstIterator constBegin() const noexcept {
                        return d.cbegin();
                }

                /** @brief Returns a mutable reverse iterator to the last element. */
                RevIterator rbegin() noexcept {
                        return d.rbegin();
                }

                /// @copydoc rbegin()
                RevIterator revBegin() noexcept {
                        return d.rbegin();
                }

                /** @brief Returns a const reverse iterator to the last element. */
                ConstRevIterator crbegin() const noexcept {
                        return d.crbegin();
                }

                /// @copydoc crbegin()
                ConstRevIterator constRevBegin() const noexcept {
                        return d.crbegin();
                }

                /** @brief Returns a mutable iterator to one past the last element. */
                Iterator end() noexcept {
                        return d.end();
                }

                /** @brief Returns a const iterator to one past the last element. */
                ConstIterator end() const noexcept {
                        return d.cend();
                }

                /** @brief Returns a const iterator to one past the last element. */
                ConstIterator cend() const noexcept {
                        return d.cend();
                }

                /// @copydoc cend()
                ConstIterator constEnd() const noexcept {
                        return d.cend();
                }

                /** @brief Returns a mutable reverse iterator to one before the first element. */
                RevIterator rend() noexcept {
                        return d.rend();
                }

                /// @copydoc rend()
                RevIterator revEnd() noexcept {
                        return d.rend();
                }

                /** @brief Returns a const reverse iterator to one before the first element. */
                ConstRevIterator crend() const noexcept {
                        return d.crend();
                }

                /// @copydoc crend()
                ConstRevIterator constRevEnd() const noexcept {
                        return d.crend();
                }

                /**
                 * @brief Returns a reference to the element at @p index with bounds checking.
                 * @param index Zero-based element index.
                 * @return Reference to the element.
                 */
                T &at(size_t index) {
                        return d.at(index);
                }

                /// @copydoc at()
                const T &at(size_t index) const {
                        return d.at(index);
                }

                /**
                 * @brief Returns a reference to the element at @p index without bounds checking.
                 * @param index Zero-based element index.
                 * @return Reference to the element.
                 */
                T &operator[](size_t index) {
                        return d[index];
                }

                /// @copydoc operator[]()
                const T &operator[](size_t index) const {
                        return d[index];
                }

                /** @brief Returns a reference to the first element. */
                T &front() {
                        return d.front();
                }

                /// @copydoc front()
                const T &front() const {
                        return d.front();
                }

                /** @brief Returns a reference to the last element. */
                T &back() {
                        return d.back();
                }

                /// @copydoc back()
                const T &back() const {
                        return d.back();
                }

                /** @brief Returns a pointer to the underlying contiguous storage. */
                T *data() noexcept {
                        return d.data();
                }

                /// @copydoc data()
                const T *data() const noexcept {
                        return d.data();
                }

                /** @brief Returns true if the list has no elements. */
                bool isEmpty() const noexcept {
                        return d.empty();
                }

                /** @brief Returns the number of elements in the list. */
                size_t size() const noexcept {
                        return d.size();
                }

                /** @brief Returns the maximum number of elements the list can theoretically hold. */
                size_t maxSize() const noexcept {
                        return d.max_size();
                }

                /**
                 * @brief Pre-allocates storage for at least @p newCapacity elements.
                 * @param newCapacity Minimum capacity to reserve.
                 */
                void reserve(size_t newCapacity) {
                        d.reserve(newCapacity);
                }

                /** @brief Returns the number of elements the list can hold without reallocating. */
                size_t capacity() const noexcept {
                        return d.capacity();
                }

                /** @brief Releases unused memory by shrinking capacity to fit the current size. */
                void shrink() {
                        d.shrink_to_fit();
                        return;
                }

                /** @brief Removes all elements from the list. */
                void clear() noexcept {
                        d.clear();
                        return;
                }

                /**
                 * @brief Inserts a value before the position given by an iterator.
                 * @param pos Iterator to the insertion point.
                 * @param value The value to insert.
                 * @return Iterator to the newly inserted element.
                 */
                Iterator insert(ConstIterator pos, const T &value) {
                        return d.insert(pos, value);
                }

                /**
                 * @brief Inserts a value before the position given by an iterator (move overload).
                 * @param pos Iterator to the insertion point.
                 * @param value The value to move-insert.
                 * @return Iterator to the newly inserted element.
                 */
                Iterator insert(ConstIterator pos, T &&value) {
                        return d.insert(pos, std::move(value));
                }

                /**
                 * @brief Inserts a value before the given index.
                 * @param pos Zero-based index of the insertion point.
                 * @param value The value to insert.
                 * @return Iterator to the newly inserted element.
                 */
                Iterator insert(size_t pos, const T &value) {
                        return d.insert(constBegin() + pos, value);
                }

                /**
                 * @brief Inserts a value before the given index (move overload).
                 * @param pos Zero-based index of the insertion point.
                 * @param value The value to move-insert.
                 * @return Iterator to the newly inserted element.
                 */
                Iterator insert(size_t pos, T &&value) {
                        return d.insert(constBegin() + pos, std::move(value));
                }

                /**
                 * @brief Emplaces an object right before the given position.
                 * @tparam Args Constructor argument types.
                 * @param pos Iterator to the insertion point.
                 * @param args Arguments forwarded to the element constructor.
                 * @return Iterator to the newly constructed element.
                 */
                template <typename... Args> Iterator emplace(ConstIterator pos, Args &&...args) {
                        return d.emplace(pos, std::forward<Args>(args)...);
                }

                /**
                 * @brief Emplaces an object right before the given index.
                 * @tparam Args Constructor argument types.
                 * @param pos Zero-based index of the insertion point.
                 * @param args Arguments forwarded to the element constructor.
                 * @return Iterator to the newly constructed element.
                 */
                template <typename... Args> Iterator emplace(size_t pos, Args &&...args) {
                        return d.emplace(constBegin() + pos, std::forward<Args>(args)...);
                }

                /**
                 * @brief Removes the element at the given iterator position.
                 * @param pos Iterator to the element to remove.
                 * @return Iterator to the element following the removed one.
                 */
                Iterator remove(ConstIterator pos) {
                        return d.erase(pos);
                }

                /**
                 * @brief Removes elements in the range [first, last).
                 * @param first Iterator to the first element to remove.
                 * @param last  Iterator past the last element to remove.
                 * @return Iterator to the element following the last removed one.
                 */
                Iterator erase(ConstIterator first, ConstIterator last) {
                        return d.erase(first, last);
                }

                /**
                 * @brief Removes the element at the given index.
                 * @param index Zero-based index of the element to remove.
                 * @return Iterator to the element following the removed one.
                 */
                Iterator remove(size_t index) {
                        return d.erase(d.begin() + index);
                }

                /**
                 * @brief Runs a test function on all the items and removes them if it returns true.
                 * @param func Predicate returning true for elements to remove.
                 */
                void removeIf(TestFunc func) {
                        d.erase(std::remove_if(d.begin(), d.end(), func), d.end());
                        return;
                }

                /**
                 * @brief Removes the first instance of value from the list.
                 * @param value The value to search for and remove.
                 * @return True if an item was found and removed, false otherwise.
                 */
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

                /**
                 * @brief Pushes an item onto the back of the list.
                 * @param value The value to append.
                 */
                void pushToBack(const T &value) {
                        d.push_back(value);
                        return;
                }

                /**
                 * @brief Pushes all items from another list to the back of this list.
                 * @param list The list whose items are appended.
                 */
                void pushToBack(const List<T> &list) {
                        d.insert(d.end(), list.d.begin(), list.d.end());
                        return;
                }

                /**
                 * @brief Moves an item onto the back of the list.
                 * @param value The value to move-append.
                 */
                void pushToBack(T &&value) {
                        d.push_back(std::move(value));
                        return;
                }

                /**
                 * @brief Moves all items from another list to the back of this list.
                 * @param list The list whose items are move-appended.
                 */
                void pushToBack(List<T> &&list) {
                        d.insert(d.end(), std::make_move_iterator(list.d.begin()), std::make_move_iterator(list.d.end()));
                        return;
                }

                /**
                 * @brief Emplaces an object on the back of the list.
                 * @tparam Args Constructor argument types.
                 * @param args Arguments forwarded to the element constructor.
                 * @return Reference to the newly constructed element.
                 */
                template <typename... Args> T &emplaceToBack(Args &&...args) {
                        return d.emplace_back(std::forward<Args>(args)...);
                }

                /** @brief Removes the last element from the list. */
                void popFromBack() {
                        d.pop_back();
                        return;
                }

                /**
                 * @brief Resizes the list.
                 *
                 * If the new size is smaller, items beyond new size will be removed.
                 * If the new size is larger, new items will be default constructed.
                 *
                 * @param newSize The desired number of elements.
                 */
                void resize(size_t newSize) {
                        d.resize(newSize);
                        return;
                }

                /**
                 * @brief Resizes the list, constructs any new items with the given value.
                 * @param newSize The desired number of elements.
                 * @param value Value used to initialize any newly created elements.
                 */
                void resize(size_t newSize, const T &value) {
                        d.resize(newSize, value);
                        return;
                }

                /**
                 * @brief Swaps the list data with another list of the same type.
                 * @param other The list to swap with.
                 */
                void swap(List<T> &other) noexcept {
                        d.swap(other.d);
                        return;
                }

                /**
                 * @brief Sets an item in the list by index.
                 * @param index Zero-based index of the element to set.
                 * @param val The new value.
                 * @return True if the index was valid and the item was set, false otherwise.
                 */
                bool set(size_t index, const T &val) {
                        if(index >= size()) return false;
                        d[index] = val;
                        return true;
                }

                /**
                 * @brief Returns a sorted copy of this list.
                 * @return A new list with elements sorted in ascending order.
                 */
                List<T> sort() const {
                        auto ret = *this;
                        std::sort(ret.begin(), ret.end());
                        return ret;
                }

                /**
                 * @brief Returns a reversed copy of this list.
                 * @return A new list with elements in reverse order.
                 */
                List<T> reverse() const {
                        auto ret = *this;
                        std::reverse(ret.begin(), ret.end());
                        return ret;
                }

                /**
                 * @brief Returns a list of all the unique items in this list.
                 * @return A new sorted list with duplicate elements removed.
                 */
                List<T> unique() {
                        auto ret = sort();
                        ret.d.erase(std::unique(ret.begin(), ret.end()), ret.end());
                        return ret;
                }

                /**
                 * @brief Returns true if the list contains the given value.
                 * @param val The value to search for.
                 * @return True if found, false otherwise.
                 */
                bool contains(const T &val) const {
                        for(const auto &item : d) {
                                if(item == val) return true;
                        }
                        return false;
                }

                /** @brief Returns true if both lists have identical contents. */
                friend bool operator==(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.d == rhs.d;
                }

                /** @brief Returns true if the lists differ. */
                friend bool operator!=(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.d != rhs.d;
                }

                /** @brief Lexicographic less-than comparison. */
                friend bool operator<(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.d < rhs.d;
                }

                /** @brief Lexicographic greater-than comparison. */
                friend bool operator>(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.d > rhs.d;
                }

                /** @brief Lexicographic less-than-or-equal comparison. */
                friend bool operator<=(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.d <= rhs.d;
                }

                /** @brief Lexicographic greater-than-or-equal comparison. */
                friend bool operator>=(const List<T> &lhs, const List<T> &rhs) {
                        return lhs.d >= rhs.d;
                }

        private:
                Data  d;
};

PROMEKI_NAMESPACE_END
