/**
 * @file      core/span.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <span>
#include <promeki/core/namespace.h>

PROMEKI_NAMESPACE_BEGIN

// Forward declarations
template <typename T> class List;
template <typename T, size_t N> class Array;

/**
 * @brief Non-owning view over contiguous storage, wrapping std::span.
 * @ingroup core_containers
 *
 * Provides a Qt-inspired API over std::span with consistent naming
 * conventions matching the rest of libpromeki. Non-owning view —
 * no PROMEKI_SHARED_FINAL.
 *
 * @tparam T Element type.
 */
template <typename T>
class Span {
        public:
                /** @brief Mutable forward iterator. */
                using Iterator = typename std::span<T>::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = typename std::span<const T>::iterator;

                /** @brief Mutable reverse iterator. */
                using RevIterator = typename std::span<T>::reverse_iterator;

                /** @brief Const reverse iterator. */
                using ConstRevIterator = typename std::span<const T>::reverse_iterator;

                /** @brief Default constructor. Creates an empty span. */
                Span() = default;

                /**
                 * @brief Constructs a span from a pointer and size.
                 * @param ptr Pointer to the first element.
                 * @param count Number of elements.
                 */
                Span(T *ptr, size_t count) : d(ptr, count) {}

                /**
                 * @brief Constructs a span from a List.
                 * @param list The list to view.
                 */
                Span(List<T> &list) : d(list.data(), list.size()) {}

                /**
                 * @brief Constructs a span from a C array.
                 * @tparam N Array size.
                 * @param arr The C array to view.
                 */
                template <size_t N>
                Span(T (&arr)[N]) : d(arr, N) {}

                /**
                 * @brief Constructs a span from an Array.
                 * @tparam N Array size.
                 * @param arr The Array to view.
                 */
                template <size_t N>
                Span(Array<T, N> &arr) : d(arr.data(), N) {}

                /** @brief Copy constructor. */
                Span(const Span &other) = default;

                /** @brief Copy assignment operator. */
                Span &operator=(const Span &other) = default;

                // -- Iterators --

                /** @brief Returns a mutable iterator to the first element. */
                Iterator begin() noexcept { return d.begin(); }

                /** @brief Returns a const iterator to the first element. */
                ConstIterator begin() const noexcept { return std::span<const T>(d.data(), d.size()).begin(); }

                /** @brief Returns a mutable iterator to one past the last element. */
                Iterator end() noexcept { return d.end(); }

                /** @brief Returns a const iterator to one past the last element. */
                ConstIterator end() const noexcept { return std::span<const T>(d.data(), d.size()).end(); }

                /** @brief Returns a const iterator to the first element. */
                ConstIterator constBegin() const noexcept { return begin(); }

                /** @brief Returns a const iterator to one past the last element. */
                ConstIterator constEnd() const noexcept { return end(); }

                /** @brief Returns a mutable reverse iterator to the last element. */
                RevIterator rbegin() noexcept { return d.rbegin(); }

                /// @copydoc rbegin()
                RevIterator revBegin() noexcept { return d.rbegin(); }

                /** @brief Returns a mutable reverse iterator to one before the first element. */
                RevIterator rend() noexcept { return d.rend(); }

                /// @copydoc rend()
                RevIterator revEnd() noexcept { return d.rend(); }

                /** @brief Returns a const reverse iterator to the last element. */
                ConstRevIterator crbegin() const noexcept { return std::span<const T>(d.data(), d.size()).rbegin(); }

                /// @copydoc crbegin()
                ConstRevIterator constRevBegin() const noexcept { return crbegin(); }

                /** @brief Returns a const reverse iterator to one before the first element. */
                ConstRevIterator crend() const noexcept { return std::span<const T>(d.data(), d.size()).rend(); }

                /// @copydoc crend()
                ConstRevIterator constRevEnd() const noexcept { return crend(); }

                // -- Access --

                /**
                 * @brief Returns a reference to the element at @p index without bounds checking.
                 * @param index Zero-based element index.
                 * @return Reference to the element.
                 */
                T &operator[](size_t index) { return d[index]; }

                /// @copydoc operator[]()
                const T &operator[](size_t index) const { return d[index]; }

                /** @brief Returns a reference to the first element. */
                T &front() { return d.front(); }

                /// @copydoc front()
                const T &front() const { return d.front(); }

                /** @brief Returns a reference to the last element. */
                T &back() { return d.back(); }

                /// @copydoc back()
                const T &back() const { return d.back(); }

                /** @brief Returns a pointer to the underlying contiguous storage. */
                T *data() noexcept { return d.data(); }

                /// @copydoc data()
                const T *data() const noexcept { return d.data(); }

                // -- Capacity --

                /** @brief Returns true if the span has no elements. */
                bool isEmpty() const noexcept { return d.empty(); }

                /** @brief Returns the number of elements. */
                size_t size() const noexcept { return d.size(); }

                /** @brief Returns the size in bytes. */
                size_t sizeBytes() const noexcept { return d.size_bytes(); }

                // -- Sub-views --

                /**
                 * @brief Returns a sub-span starting at @p offset with @p count elements.
                 * @param offset Starting element index.
                 * @param count Number of elements.
                 * @return A new Span viewing the sub-range.
                 */
                Span subspan(size_t offset, size_t count) {
                        return Span(d.data() + offset, count);
                }

                /**
                 * @brief Returns a span of the first @p count elements.
                 * @param count Number of elements.
                 * @return A new Span viewing the first elements.
                 */
                Span first(size_t count) {
                        return Span(d.data(), count);
                }

                /**
                 * @brief Returns a span of the last @p count elements.
                 * @param count Number of elements.
                 * @return A new Span viewing the last elements.
                 */
                Span last(size_t count) {
                        return Span(d.data() + d.size() - count, count);
                }

                // -- Convenience --

                /**
                 * @brief Calls @p func for every element.
                 * @tparam Func Callable with signature void(const T &).
                 * @param func The function to invoke.
                 */
                template <typename Func>
                void forEach(Func &&func) const {
                        for(size_t i = 0; i < d.size(); ++i) func(d[i]);
                        return;
                }

        private:
                std::span<T> d;
};

PROMEKI_NAMESPACE_END
