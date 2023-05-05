/*****************************************************************************
 * array.h
 * May 04, 2023
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
#include <array>

// The base template class for a point.  Put any code that's general to the concept of an 
// array of values in this class.
template <typename T, size_t NumValues> class Array {
        public:
                using DataType = std::array<T, NumValues>;

                Array() : d{} {}
                Array(const DataType &val) : d(val) {}
                Array(const DataType &&val) : d(std::move(val)) {}
                template<typename... Args> Array(Args... args) : d{static_cast<T>(args)...} {}
                ~Array() {}

                size_t size() const { return d.size(); }

                template <typename U, size_t OtherNumValues> Array(const Array<U, OtherNumValues>& other) {
                        static_assert(std::is_convertible_v<T, T>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for(size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? other[i] : T{};
                        }
                }

                template <typename U> Array<T, NumValues>& operator=(const Array<U, NumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(other[i]);
                        }
                        return *this;
                }

                template <typename U, size_t OtherNumValues> Array<T, NumValues>& operator=(const Array<U, OtherNumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? static_cast<T>(other[i]) : T{};
                        }
                        return *this;
                }

                template <typename U> Array<T, NumValues>& operator=(U value) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(value);
                        }
                        return *this;
                }

                T& operator[](size_t index) {
                        return d[index];
                }

                const T& operator[](size_t index) const {
                        return d[index];
                }
                
                Array<T, NumValues>& operator+=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] += other[i];
                        return *this;
                }

                Array<T, NumValues>& operator-=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] -= other[i];
                        return *this;
                }

                Array<T, NumValues>& operator*=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] *= other[i];
                        return *this;
                }

                Array<T, NumValues>& operator/=(const Array<T, NumValues> &other) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] /= other[i];
                        return *this;
                }

                Array<T, NumValues>& operator+=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] += scalar;
                        return *this;
                }

                Array<T, NumValues>& operator-=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] -= scalar;
                        return *this;
                }

                Array<T, NumValues>& operator*=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] *= scalar;
                        return *this;
                }

                Array<T, NumValues>& operator/=(const T &scalar) {
                        for (size_t i = 0; i < NumValues; ++i) d[i] /= scalar;
                        return *this;
                }

                T sum() const {
                        T ret{};
                        for(size_t i = 0; i < NumValues; ++i) ret += d[i];
                        return ret;
                }

                double mean() const {
                        double val = 0.0;
                        for(size_t i = 0; i < NumValues; ++i) val += static_cast<double>(d[i]);
                        val /= static_cast<double>(NumValues);
                        return val;
                }

                T *data() {
                        return d.data();
                }

                const T *data() const {
                        return d.data();
                }

                bool isZero() const {
                        for(size_t i = 0; i < NumValues; i++) {
                                if(d[i] != 0) return false;
                        }
                        return true;
                }

                Array<T, NumValues> lerp(const Array<T, NumValues> &other, double v) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) ret[i] = ((1.0 - v) * d[i]) + (v * other.d[i]);
                        return ret;
                }

                Array<T, NumValues> clamp(const Array<T, NumValues> &min, const Array<T, NumValues> &max) const {
                        Array<T, NumValues> ret;
                        for(size_t i = 0; i < d.size(); ++i) {
                                if(d[i] < min[d]) ret[i] = min[d];
                                else if(d[i] > max[d]) ret[i] = max[d];
                                else ret[i] = d[i];
                        }
                        return ret;
                }

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

