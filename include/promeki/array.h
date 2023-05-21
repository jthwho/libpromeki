/** 
 * @file array.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#pragma once

#include <cstddef>
#include <array>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Wrapper around std::array
 * The goal of this object it to make working with std::array easier and to
 * extend the functionality.
 */
template <typename T, size_t NumValues> class Array {
        public:
                /**
                 * @brief std::array definition of the underlying data
                 */
                using DataType = std::array<T, NumValues>;

                /**
                 * @brief Default constructor.
                 * Constructs the std::array object, but leaves it uninitialized.
                 */
                Array() : d{} {}

                /**
                 * @brief Constructs an object from a std::array reference */
                Array(const DataType &val) : d(val) {}

                /**
                 * @brief Constructs an object from a std::array rvalue */
                Array(const DataType &&val) : d(std::move(val)) {}

                /**
                 * @brief Constructs an object from an argument list
                 */
                template<typename... Args> Array(Args... args) : d{static_cast<T>(args)...} {}

                ~Array() {}

                /**
                 * @brief Returns the number of elements in the array.
                 * This is fixed at compile time.
                 */
                size_t size() const { return d.size(); }

                /**
                 * @brief Constructs an object from another Array object of different size. 
                 * The other array object must have a size equal to or greater than this object.
                 * If the other is smaller, you'll get a static assert compile time error */
                template <typename U, size_t OtherNumValues> Array(const Array<U, OtherNumValues>& other) {
                        static_assert(std::is_convertible_v<T, T>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for(size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? other[i] : T{};
                        }
                }

                /** 
                 * @brief Assigns from another Array object of a different or same type.
                 * Will fail to compile if types can't be converted.
                 */
                template <typename U> Array<T, NumValues>& operator=(const Array<U, NumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(other[i]);
                        }
                        return *this;
                }

                /**
                 * @brief Assigns from another Array object of a different or same type and size
                 * Will fail to compile if types can't be converted or other array is too small.
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
                 * @brief Assigns all items in the array from a given type
                 * Will fail to compile if type can't be converted */
                template <typename U> Array<T, NumValues>& operator=(U value) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(value);
                        }
                        return *this;
                }

                /**
                 * @brief Returns an item reference of item at index
                 * NOTE: This does not do any bounds checking
                 */
                T& operator[](size_t index) {
                        return d[index];
                }

                /**
                 * @brief Returns a const item reference of item at index
                 * NOTE: This does not do any bounds checking
                 */
                const T& operator[](size_t index) const {
                        return d[index];
                }

                /**
                 * @brief Adds array to this one.
                 */
                Array<T, NumValues>& operator+=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] += other[i];
                        return *this;
                }

                /**
                 * @brief Subtracts array from this one.
                 */
                 Array<T, NumValues>& operator-=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] -= other[i];
                        return *this;
                }

                /**
                 * @brief Multiplies array with this one
                 */
                 Array<T, NumValues>& operator*=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] *= other[i];
                        return *this;
                }

                /**
                 * @brief Divides an array with this one.
                 */
                 Array<T, NumValues>& operator/=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] /= other[i];
                        return *this;
                }

                /**
                 * @brief Adds a scaler value to all indexes.
                 */
                 Array<T, NumValues>& operator+=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] += scalar;
                        return *this;
                }

                /**
                 * @brief Subtracts a scaler value from all indexes
                 */
                 Array<T, NumValues>& operator-=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] -= scalar;
                        return *this;
                }

                /**
                 * @brief Multiplies every index by a scaler
                 */
                Array<T, NumValues>& operator*=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] *= scalar;
                        return *this;
                }

                /**
                 * @brief Divides every index by a scaler 
                 */
                Array<T, NumValues>& operator/=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] /= scalar;
                        return *this;
                }

                /**
                 * @brief Returns the sum of all the array indexes
                 */
                T sum() const {
                        T ret{};
                        for(size_t i = 0; i < NumValues; ++i) ret += d[i];
                        return ret;
                }

                /**
                 * @brief Returns the mean of all the array items
                 */
                double mean() const {
                        double val = 0.0;
                        for(size_t i = 0; i < NumValues; ++i) val += static_cast<double>(d[i]);
                        val /= static_cast<double>(NumValues);
                        return val;
                }

                /**
                 * @brief Returns a naked array pointer to the array data
                 */
                T *data() {
                        return d.data();
                }

                /**
                 * @brief Returns a const naked array pointer to the array data
                 */
                const T *data() const {
                        return d.data();
                }

                /**
                 * @brief Returns true if all elements are zero
                 */
                bool isZero() const {
                        for(size_t i = 0; i < NumValues; i++) {
                                if(d[i] != 0) return false;
                        }
                        return true;
                }

                /**
                 * @brief Provides a linear interpolated array between this one and other
                 * @param[in] other Other array to lerp between
                 * @param[in] v Lerp value (0.0 = this array, 1.0 = other array)
                 */
                Array<T, NumValues> lerp(const Array<T, NumValues> &other, double v) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) ret[i] = ((1.0 - v) * d[i]) + (v * other.d[i]);
                        return ret;
                }

                /**
                 * @brief Clamps items in the array to values between >= min and <= max
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
                 * @brief Returns true if all the elements are between the min and max given
                 */
                bool isBetween(const Array<T, NumValues> &min, const Array<T, NumValues> &max) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) {
                                if(d[i] < min[i] || d[i] > max[i]) return false;
                        }
                        return true;
                }

                friend Array<T, NumValues> operator+(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs += rhs;
                        return lhs;
                }

                friend Array<T, NumValues> operator-(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs -= rhs;
                        return lhs;
                }

                friend Array<T, NumValues> operator*(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs *= rhs;
                        return lhs;
                }

                friend Array<T, NumValues> operator/(Array<T, NumValues> lhs, const Array<T, NumValues> &rhs) {
                        lhs /= rhs;
                        return lhs;
                }

                friend Array<T, NumValues> operator+(Array<T, NumValues> lhs, const T &scalar) {
                        lhs += scalar;
                        return lhs;
                }

                friend Array<T, NumValues> operator-(Array<T, NumValues> lhs, const T &scalar) {
                        lhs -= scalar;
                        return lhs;
                }

                friend Array<T, NumValues> operator*(Array<T, NumValues> lhs, const T &scalar) {
                        lhs *= scalar;
                        return lhs;
                }

                friend Array<T, NumValues> operator/(Array<T, NumValues> lhs, const T &scalar) {
                        lhs /= scalar;
                        return lhs;
                }

                friend bool operator==(const Array<T, NumValues> &lhs, const Array<T, NumValues> &rhs) {
                        for(size_t i = 0; i < NumValues; ++i) if(lhs[i] != rhs[i]) return false;
                        return true;
                }

                friend bool operator!=(const Array<T, NumValues>& lhs, const Array<T, NumValues>& rhs) {
                        return !(lhs == rhs);
                }

        protected:
                DataType d;
};

PROMEKI_NAMESPACE_END


