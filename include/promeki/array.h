/**
 * @file array.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <array>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Fixed-size array container wrapping std::array.
 *
 * Extends std::array with element-wise arithmetic, interpolation,
 * clamping, and range-checking utilities.
 *
 * @tparam T Element type.
 * @tparam NumValues Number of elements (fixed at compile time).
 */
template <typename T, size_t NumValues> class Array {
        PROMEKI_SHARED_FINAL(Array)
        public:
                /** @brief Shared pointer type for Array. */
                using Ptr = SharedPtr<Array>;

                /** @brief Underlying std::array storage type. */
                using DataType = std::array<T, NumValues>;

                /**
                 * @brief Default constructor.
                 * Value-initializes all elements to zero/default.
                 */
                Array() : d{} {}

                /**
                 * @brief Constructs from a std::array lvalue reference.
                 * @param val The std::array to copy.
                 */
                Array(const DataType &val) : d(val) {}

                /**
                 * @brief Constructs from a std::array rvalue reference.
                 * @param val The std::array to move from.
                 */
                Array(const DataType &&val) : d(std::move(val)) {}

                /**
                 * @brief Constructs from a variadic argument list.
                 * @tparam Args Argument types (must be convertible to T).
                 * @param args Values used to initialize each element.
                 */
                template<typename... Args> Array(Args... args) : d{static_cast<T>(args)...} {}

                /** @brief Destructor. */
                ~Array() {}

                /**
                 * @brief Returns the number of elements in the array.
                 * @return The compile-time fixed size NumValues.
                 */
                size_t size() const { return d.size(); }

                /**
                 * @brief Constructs from another Array of a potentially different size.
                 *
                 * The source array must have a size less than or equal to this array.
                 * Extra elements are value-initialized. Fails at compile time if
                 * the source array is larger.
                 *
                 * @tparam U Source element type (must be convertible to T).
                 * @tparam OtherNumValues Source array size.
                 * @param other The source array to copy from.
                 */
                template <typename U, size_t OtherNumValues> Array(const Array<U, OtherNumValues>& other) {
                        static_assert(std::is_convertible_v<T, T>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for(size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? other[i] : T{};
                        }
                }

                /**
                 * @brief Assigns from another Array of the same size but potentially different type.
                 *
                 * Fails at compile time if types cannot be converted.
                 *
                 * @tparam U Source element type.
                 * @param other The source array to assign from.
                 * @return Reference to this array.
                 */
                template <typename U> Array<T, NumValues>& operator=(const Array<U, NumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(other[i]);
                        }
                        return *this;
                }

                /**
                 * @brief Assigns from another Array of a different type and size.
                 *
                 * The source must not be larger than this array. Extra elements
                 * are value-initialized. Fails at compile time if types cannot
                 * be converted or the source array is too large.
                 *
                 * @tparam U Source element type.
                 * @tparam OtherNumValues Source array size.
                 * @param other The source array to assign from.
                 * @return Reference to this array.
                 */
                template <typename U, size_t OtherNumValues> Array<T, NumValues>& operator=(const Array<U, OtherNumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? static_cast<T>(other[i]) : T{};
                        }
                        return *this;
                }

                /**
                 * @brief Assigns a scalar value to all elements.
                 *
                 * Fails at compile time if the type cannot be converted.
                 *
                 * @tparam U Scalar type.
                 * @param value The value to assign to every element.
                 * @return Reference to this array.
                 */
                template <typename U> Array<T, NumValues>& operator=(U value) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(value);
                        }
                        return *this;
                }

                /**
                 * @brief Returns a mutable reference to the element at @p index.
                 *
                 * No bounds checking is performed.
                 *
                 * @param index Zero-based element index.
                 * @return Reference to the element.
                 */
                T& operator[](size_t index) {
                        return d[index];
                }

                /**
                 * @brief Returns a const reference to the element at @p index.
                 *
                 * No bounds checking is performed.
                 *
                 * @param index Zero-based element index.
                 * @return Const reference to the element.
                 */
                const T& operator[](size_t index) const {
                        return d[index];
                }

                /**
                 * @brief Adds another array element-wise to this one.
                 * @param other The array to add.
                 * @return Reference to this array.
                 */
                Array<T, NumValues>& operator+=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] += other[i];
                        return *this;
                }

                /**
                 * @brief Subtracts another array element-wise from this one.
                 * @param other The array to subtract.
                 * @return Reference to this array.
                 */
                 Array<T, NumValues>& operator-=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] -= other[i];
                        return *this;
                }

                /**
                 * @brief Multiplies this array element-wise by another.
                 * @param other The array to multiply by.
                 * @return Reference to this array.
                 */
                 Array<T, NumValues>& operator*=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] *= other[i];
                        return *this;
                }

                /**
                 * @brief Divides this array element-wise by another.
                 * @param other The array to divide by.
                 * @return Reference to this array.
                 */
                 Array<T, NumValues>& operator/=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] /= other[i];
                        return *this;
                }

                /**
                 * @brief Adds a scalar value to all elements.
                 * @param scalar The value to add.
                 * @return Reference to this array.
                 */
                 Array<T, NumValues>& operator+=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] += scalar;
                        return *this;
                }

                /**
                 * @brief Subtracts a scalar value from all elements.
                 * @param scalar The value to subtract.
                 * @return Reference to this array.
                 */
                 Array<T, NumValues>& operator-=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] -= scalar;
                        return *this;
                }

                /**
                 * @brief Multiplies all elements by a scalar value.
                 * @param scalar The value to multiply by.
                 * @return Reference to this array.
                 */
                Array<T, NumValues>& operator*=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] *= scalar;
                        return *this;
                }

                /**
                 * @brief Divides all elements by a scalar value.
                 * @param scalar The value to divide by.
                 * @return Reference to this array.
                 */
                Array<T, NumValues>& operator/=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] /= scalar;
                        return *this;
                }

                /**
                 * @brief Returns the sum of all elements.
                 * @return The sum, starting from a value-initialized accumulator.
                 */
                T sum() const {
                        T ret{};
                        for(size_t i = 0; i < NumValues; ++i) ret += d[i];
                        return ret;
                }

                /**
                 * @brief Returns the arithmetic mean of all elements.
                 * @return The mean as a double.
                 */
                double mean() const {
                        double val = 0.0;
                        for(size_t i = 0; i < NumValues; ++i) val += static_cast<double>(d[i]);
                        val /= static_cast<double>(NumValues);
                        return val;
                }

                /**
                 * @brief Returns a mutable pointer to the underlying contiguous storage.
                 * @return Pointer to the first element.
                 */
                T *data() {
                        return d.data();
                }

                /**
                 * @brief Returns a const pointer to the underlying contiguous storage.
                 * @return Const pointer to the first element.
                 */
                const T *data() const {
                        return d.data();
                }

                /**
                 * @brief Returns true if all elements are zero.
                 * @return True if every element compares equal to zero.
                 */
                bool isZero() const {
                        for(size_t i = 0; i < NumValues; i++) {
                                if(d[i] != 0) return false;
                        }
                        return true;
                }

                /**
                 * @brief Returns a linearly interpolated array between this one and another.
                 * @param other The target array to interpolate toward.
                 * @param v Interpolation factor (0.0 returns this array, 1.0 returns @p other).
                 * @return A new array with each element interpolated.
                 */
                Array<T, NumValues> lerp(const Array<T, NumValues> &other, double v) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) ret[i] = ((1.0 - v) * d[i]) + (v * other.d[i]);
                        return ret;
                }

                /**
                 * @brief Returns a new array with each element clamped to the given range.
                 * @param min Per-element minimum values.
                 * @param max Per-element maximum values.
                 * @return A new array with each element clamped to [min, max].
                 */
                Array<T, NumValues> clamp(const Array<T, NumValues> &min, const Array<T, NumValues> &max) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) {
                                if(d[i] < min[d]) ret[i] = min[d];
                                else if(d[i] > max[d]) ret[i] = max[d];
                                else ret[i] = d[i];
                        }
                        return ret;
                }

                /**
                 * @brief Returns true if all elements fall within the given range.
                 * @param min Per-element minimum values (inclusive).
                 * @param max Per-element maximum values (inclusive).
                 * @return True if every element is between the corresponding min and max.
                 */
                bool isBetween(const Array<T, NumValues> &min, const Array<T, NumValues> &max) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) {
                                if(d[i] < min[i] || d[i] > max[i]) return false;
                        }
                        return true;
                }

                /** @brief Returns the element-wise sum of two arrays. */
                friend Array<T, NumValues> operator+(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs += rhs;
                        return lhs;
                }

                /** @brief Returns the element-wise difference of two arrays. */
                friend Array<T, NumValues> operator-(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs -= rhs;
                        return lhs;
                }

                /** @brief Returns the element-wise product of two arrays. */
                friend Array<T, NumValues> operator*(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs *= rhs;
                        return lhs;
                }

                /** @brief Returns the element-wise quotient of two arrays. */
                friend Array<T, NumValues> operator/(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs /= rhs;
                        return lhs;
                }

                /** @brief Returns a new array with a scalar added to each element. */
                friend Array<T, NumValues> operator+(Array<T, NumValues> lhs, const T &scalar) {
                        lhs += scalar;
                        return lhs;
                }

                /** @brief Returns a new array with a scalar subtracted from each element. */
                friend Array<T, NumValues> operator-(Array<T, NumValues> lhs, const T &scalar) {
                        lhs -= scalar;
                        return lhs;
                }

                /** @brief Returns a new array with each element multiplied by a scalar. */
                friend Array<T, NumValues> operator*(Array<T, NumValues> lhs, const T &scalar) {
                        lhs *= scalar;
                        return lhs;
                }

                /** @brief Returns a new array with each element divided by a scalar. */
                friend Array<T, NumValues> operator/(Array<T, NumValues> lhs, const T &scalar) {
                        lhs /= scalar;
                        return lhs;
                }

                /** @brief Returns true if all elements of both arrays are equal. */
                friend bool operator==(const Array<T, NumValues> &lhs, const Array<T, NumValues> &rhs) {
                        for(size_t i = 0; i < NumValues; ++i) if(lhs[i] != rhs[i]) return false;
                        return true;
                }

                /** @brief Returns true if any element differs between the two arrays. */
                friend bool operator!=(const Array<T, NumValues>& lhs, const Array<T, NumValues>& rhs) {
                        return !(lhs == rhs);
                }

        protected:
                DataType d;
};

PROMEKI_NAMESPACE_END

