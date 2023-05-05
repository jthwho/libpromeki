/*****************************************************************************
 * matrix.h
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

#include <cmath>
#include <promeki/array.h>
#include <promeki/error.h>

namespace promeki {

template <typename T, size_t W, size_t H>
class Matrix {
        public:
                using RowDataType = Array<T, W>;
                using DataType = Array<RowDataType, H>;

                static Matrix<T, W, H> identity() {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (i == j) ? static_cast<T>(1) : static_cast<T>(0);
                                }
                        }
                        return result;
                }

                static Matrix<T, W, H> rotationMatrix(T radians, size_t dim) {
                        static_assert(W == H, "Matrix can only be calculated for square matrices");
                        static_assert(W >= 2, "Matrix size must be at least 2x2 for rotation matrix generation.");
                        if(dim >= W - 1) throw std::invalid_argument("Invalid dimension for rotation.");

                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < W; ++i) result[i][i] = static_cast<T>(1);
                        result[dim][dim] = std::cos(radians);
                        result[dim][dim + 1] = -std::sin(radians);
                        result[dim + 1][dim] = std::sin(radians);
                        result[dim + 1][dim + 1] = std::cos(radians);
                        return result;
                }

                Matrix() : d{} {}
                Matrix(const DataType &data) : d(data) {}

                RowDataType &operator[](size_t index) {
                        return d[index];
                }

                const RowDataType &operator[](size_t index) const {
                        return d[index];
                }

                DataType& data() {
                        return d;
                }

                const DataType& data() const {
                        return d;
                }

                size_t width() const {
                        return W;
                }

                size_t height() const {
                        return H;
                }

                bool isSquare() const {
                        return W == H;
                }

                Matrix<T, H, W> transpose() const {
                        Matrix<T, H, W> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[j][i] = d[i][j];
                                }
                        }
                        return result;
                }

                Matrix<T, W, H> operator+(const Matrix<T, W, H>& other) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] + other[i][j];
                                }
                        }
                        return result;
                }

                Matrix<T, W, H> operator-(const Matrix<T, W, H>& other) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] - other[i][j];
                                }
                        }
                        return result;
                }

                Matrix<T, W, H> operator*(const T& scalar) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] * scalar;
                                }
                        }
                        return result;
                }

                template<size_t K> Matrix<T, W, K> operator*(const Matrix<T, H, K>& other) const {
                        Matrix<T, W, K> result;
                        for (size_t i = 0; i < W; ++i) {
                                for (size_t j = 0; j < K; ++j) {
                                        T sum = 0;
                                        for (size_t k = 0; k < H; ++k) {
                                                sum += d[i][k] * other[k][j];
                                        }
                                        result[i][j] = sum;
                                }
                        }
                        return result;
                }

                Matrix<T, W, H> operator/(const T& scalar) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] / scalar;
                                }
                        }
                        return result;
                }

                void LUdecomposition(Matrix<T, W, H>& L, Matrix<T, W, H>& U) const {
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        if (j < i) {
                                                L[j][i] = 0;
                                        } else {
                                                L[j][i] = (*this)[j][i];
                                                for (size_t k = 0; k < i; ++k) {
                                                        L[j][i] -= L[j][k] * U[k][i];
                                                }
                                        }
                                        if (i == j) {
                                                U[i][i] = 1;
                                        }
                                        if (i < j) {
                                                U[i][j] = 0;
                                        } else {
                                                U[i][j] = (*this)[i][j] / L[i][i];
                                                for (size_t k = 0; k < i; ++k) {
                                                        U[i][j] -= ((L[i][k] * U[k][j])) / L[i][i];
                                                }
                                        }
                                }
                        }
                }

                T determinant() const {
                        static_assert(W == H, "Determinant can only be calculated for square matrices");

                        if constexpr (W == 2) {
                                return d[0][0] * d[1][1] - d[0][1] * d[1][0];
                        } else if constexpr (W == 3) {
                                return d[0][0] * (d[1][1] * d[2][2] - d[1][2] * d[2][1]) -
                                        d[0][1] * (d[1][0] * d[2][2] - d[1][2] * d[2][0]) +
                                        d[0][2] * (d[1][0] * d[2][1] - d[1][1] * d[2][0]);
                        }
                        Matrix<T, W, H> L = Matrix<T, W, H>::identity();
                        Matrix<T, W, H> U = Matrix<T, W, H>::identity();

                        LUdecomposition(L, U);

                        T det = 1;
                        for (size_t i = 0; i < H; ++i) {
                                det *= L[i][i] * U[i][i];
                        }
                        return det;
                }

                Matrix<T, W, H> inverse(Error *err = nullptr) const {
                        static_assert(W == H, "Inverse can only be calculated for square matrices");

                        T det = determinant();
                        if(det == 0) {
                                if(err != nullptr) *err = Error::SingularMatrix;
                                return Matrix<T, W, H>();
                        }

                        Matrix<T, W, H> result;
                        if constexpr (W == 2) {
                                result[0][0] = d[1][1] / det;
                                result[0][1] = -d[0][1] / det;
                                result[1][0] = -d[1][0] / det;
                                result[1][1] = d[0][0] / det;
                        } else if constexpr (W == 3) {
                                result[0][0] = (d[1][1] * d[2][2] - d[1][2] * d[2][1]) / det;
                                result[0][1] = (d[0][2] * d[2][1] - d[0][1] * d[2][2]) / det;
                                result[0][2] = (d[0][1] * d[1][2] - d[0][2] * d[1][1]) / det;
                                result[1][0] = (d[1][2] * d[2][0] - d[1][0] * d[2][2]) / det;
                                result[1][1] = (d[0][0] * d[2][2] - d[0][2] * d[2][0]) / det;
                                result[1][2] = (d[0][2] * d[1][0] - d[0][0] * d[1][2]) / det;
                                result[2][0] = (d[1][0] * d[2][1] - d[1][1] * d[2][0]) / det;
                                result[2][1] = (d[0][1] * d[2][0] - d[0][0] * d[2][1]) / det;
                                result[2][2] = (d[0][0] * d[1][1] - d[0][1] * d[1][0]) / det;
                        } else {
                                // Fallback to Gaussian method
                                result = Matrix<T, W, H>::identity();
                                Matrix<T, W, H> temp = *this;

                                for (size_t i = 0; i < W; ++i) {
                                        // Find the pivot row
                                        size_t pivot = i;
                                        for (size_t j = i + 1; j < H; ++j) {
                                                if (std::abs(temp[j][i]) > std::abs(temp[pivot][i])) {
                                                        pivot = j;
                                                }
                                        }

                                        // Swap the rows
                                        if (pivot != i) {
                                                temp[i].swap(temp[pivot]);
                                                result[i].swap(result[pivot]);
                                        }

                                        // Divide the pivot row by the pivot element
                                        T pivotValue = temp[i][i];
                                        if(pivotValue == 0) {
                                                if(err != nullptr) *err = Error::SingularMatrix;
                                                return Matrix<T, W, H>();
                                        }

                                        for (size_t j = 0; j < W; ++j) {
                                                temp[i][j] /= pivotValue;
                                                result[i][j] /= pivotValue;
                                        }

                                        // Use the pivot row to eliminate the other rows
                                        for (size_t j = 0; j < H; ++j) {
                                                if (j != i) {
                                                        T factor = temp[j][i];
                                                        for (size_t k = 0; k < W; ++k) {
                                                                temp[j][k] -= factor * temp[i][k];
                                                                result[j][k] -= factor * result[i][k];
                                                        }
                                                }
                                        }
                                }
                        }
                        return result;
                }

                // Dot product between two matrices treated as vectors
                template<size_t L> T dot(const Matrix<T, L, 1>& other) const {
                        static_assert(W == 1 || H == 1, "Both matrices must have a single row or a single column");
                        static_assert(L == H || L == W, "The matrices must have the same number of elements");

                        size_t size = (W == 1) ? H : W;
                        T result = 0;

                        if constexpr (W == 1 && other.width() == 1) { // Both are column vectors
                                for (size_t i = 0; i < size; ++i) {
                                        result += (*this)[i][0] * other[i][0];
                                }
                        } else if constexpr (H == 1 && other.height() == 1) { // Both are row vectors
                                for (size_t i = 0; i < size; ++i) {
                                        result += (*this)[0][i] * other[0][i];
                                }
                        }
                        return result;
                }

                template<typename Func> Matrix<T, W, H> apply(Func&& func) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = func((*this)[i][j]);
                                }
                        }
                        return result;
                }

                T frobeniusNorm() const {
                        T sum = 0;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        sum += (*this)[i][j] * (*this)[i][j];
                                }
                        }
                        return std::sqrt(sum);
                }

                T trace() const {
                        static_assert(W == H, "Trace can only be calculated for square matrices");
                        T sum = 0;
                        for (size_t i = 0; i < H; ++i) {
                                sum += (*this)[i][i];
                        }
                        return sum;
                }

                T sum() const {
                        T total = 0;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        total += (*this)[i][j];
                                }
                        }
                        return total;
                }

                Matrix<T, W, H> hadamardProduct(const Matrix<T, W, H>& other) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (*this)[i][j] * other[i][j];
                                }
                        }
                        return result;
                }

                template<size_t L> Matrix<T, H, L> outer_product(const Matrix<T, L, 1>& other) const {
                        static_assert(W == 1, "The first matrix must have a single column");
                        Matrix<T, H, L> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < L; ++j) {
                                        result[i][j] = (*this)[i][0] * other[j][0];
                                }
                        }
                        return result;
                }
                
                Matrix<T, 1, H> rowSum() const {
                        Matrix<T, 1, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                T total = 0;
                                for (size_t j = 0; j < W; ++j) {
                                        total += (*this)[i][j];
                                }
                                result[0][i] = total;
                        }
                        return result;
                }

                Matrix<T, W, 1> colSum() const {
                        Matrix<T, W, 1> result;
                        for (size_t j = 0; j < W; ++j) {
                                T total = 0;
                                for (size_t i = 0; i < H; ++i) {
                                        total += (*this)[i][j];
                                }
                                result[j][0] = total;
                        }
                        return result;
                }

                Matrix<T, W, 1> diagonal() const {
                        static_assert(W == H, "Diagonal can only be calculated for square matrices");

                        Matrix<T, W, 1> result;
                        for (size_t i = 0; i < W; ++i) {
                                result[i][0] = (*this)[i][i];
                        }
                        return result;
                }
                
                Matrix<double, 1, H> rowMean() const {
                        Matrix<double, 1, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                double total = 0;
                                for (size_t j = 0; j < W; ++j) {
                                        total += (*this)[i][j];
                                }
                                result[0][i] = total / W;
                        }
                        return result;
                }

                // Calculate the column-wise mean of the matrix elements
                Matrix<double, W, 1> colMean() const {
                        Matrix<double, W, 1> result;
                        for (size_t j = 0; j < W; ++j) {
                                double total = 0;
                                for (size_t i = 0; i < H; ++i) {
                                        total += (*this)[i][j];
                                }
                                result[j][0] = total / H;
                        }
                        return result;
                }

                Matrix<T, H, W> rotateCW() const {
                        Matrix<T, H, W> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (*this)[H - j - 1][i];
                                }
                        }
                        return result;
                }

                Matrix<T, H, W> rotateCCW() const {
                        Matrix<T, H, W> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (*this)[j][W - i - 1];
                                }
                        }
                        return result;
                }

        private:
                DataType d;
};

} // namespace promeki
