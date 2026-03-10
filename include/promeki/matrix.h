/**
 * @file      matrix.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <promeki/namespace.h>
#include <promeki/array.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Generic fixed-size matrix with compile-time dimensions.
 *
 * Provides standard matrix operations including arithmetic, transposition,
 * determinant, inverse, LU decomposition, and element-wise operations.
 *
 * @tparam T Element type (e.g. float, double).
 * @tparam W Number of columns (width).
 * @tparam H Number of rows (height).
 */
template <typename T, size_t W, size_t H>
class Matrix {
        public:
                /** @brief Type alias for a single row of the matrix. */
                using RowDataType = Array<T, W>;

                /** @brief Type alias for the entire matrix storage (array of rows). */
                using DataType = Array<RowDataType, H>;

                /**
                 * @brief Creates an identity matrix.
                 * @return An identity matrix with ones on the diagonal and zeros elsewhere.
                 */
                static Matrix<T, W, H> identity() {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (i == j) ? static_cast<T>(1) : static_cast<T>(0);
                                }
                        }
                        return result;
                }

                /**
                 * @brief Creates a rotation matrix for the given angle and dimension pair.
                 * @param radians The rotation angle in radians.
                 * @param dim The dimension index for the rotation plane (must be less than W-1).
                 * @param err Optional error output; set to Error::InvalidDimension on invalid dim.
                 * @return A rotation matrix, or a zero matrix on error.
                 */
                static Matrix<T, W, H> rotationMatrix(T radians, size_t dim, Error *err = nullptr) {
                        static_assert(W == H, "Matrix can only be calculated for square matrices");
                        static_assert(W >= 2, "Matrix size must be at least 2x2 for rotation matrix generation.");
                        if(dim >= W - 1) {
                                if(err != nullptr) *err = Error::InvalidDimension;
                                return Matrix<T, W, H>();
                        }

                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < W; ++i) result[i][i] = static_cast<T>(1);
                        result[dim][dim] = std::cos(radians);
                        result[dim][dim + 1] = -std::sin(radians);
                        result[dim + 1][dim] = std::sin(radians);
                        result[dim + 1][dim + 1] = std::cos(radians);
                        return result;
                }

                /** @brief Constructs a zero-initialized matrix. */
                Matrix() : d{} {}

                /**
                 * @brief Constructs a matrix from existing data.
                 * @param data The data to initialize from.
                 */
                Matrix(const DataType &data) : d(data) {}

                /**
                 * @brief Accesses a row by index.
                 * @param index The row index.
                 * @return A mutable reference to the row.
                 */
                RowDataType &operator[](size_t index) {
                        return d[index];
                }

                /**
                 * @brief Accesses a row by index (const).
                 * @param index The row index.
                 * @return A const reference to the row.
                 */
                const RowDataType &operator[](size_t index) const {
                        return d[index];
                }

                /** @brief Returns a mutable reference to the underlying data. */
                DataType& data() {
                        return d;
                }

                /** @brief Returns a const reference to the underlying data. */
                const DataType& data() const {
                        return d;
                }

                /** @brief Returns the number of columns. */
                size_t width() const {
                        return W;
                }

                /** @brief Returns the number of rows. */
                size_t height() const {
                        return H;
                }

                /** @brief Returns true if the matrix is square (W == H). */
                bool isSquare() const {
                        return W == H;
                }

                /**
                 * @brief Returns the transpose of this matrix.
                 * @return A new matrix with rows and columns swapped.
                 */
                Matrix<T, H, W> transpose() const {
                        Matrix<T, H, W> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[j][i] = d[i][j];
                                }
                        }
                        return result;
                }

                /**
                 * @brief Element-wise matrix addition.
                 * @param other The matrix to add.
                 * @return A new matrix containing the element-wise sum.
                 */
                Matrix<T, W, H> operator+(const Matrix<T, W, H>& other) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] + other[i][j];
                                }
                        }
                        return result;
                }

                /**
                 * @brief Element-wise matrix subtraction.
                 * @param other The matrix to subtract.
                 * @return A new matrix containing the element-wise difference.
                 */
                Matrix<T, W, H> operator-(const Matrix<T, W, H>& other) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] - other[i][j];
                                }
                        }
                        return result;
                }

                /**
                 * @brief Scalar multiplication.
                 * @param scalar The scalar to multiply each element by.
                 * @return A new matrix with each element multiplied by scalar.
                 */
                Matrix<T, W, H> operator*(const T& scalar) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] * scalar;
                                }
                        }
                        return result;
                }

                /**
                 * @brief Matrix multiplication.
                 * @tparam K Number of columns in the other matrix.
                 * @param other The right-hand-side matrix.
                 * @return The product matrix of dimensions W x K.
                 */
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

                /**
                 * @brief Scalar division.
                 * @param scalar The scalar to divide each element by.
                 * @return A new matrix with each element divided by scalar.
                 */
                Matrix<T, W, H> operator/(const T& scalar) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = d[i][j] / scalar;
                                }
                        }
                        return result;
                }

                /**
                 * @brief Computes the LU decomposition of this matrix.
                 * @param L Output lower triangular matrix.
                 * @param U Output upper triangular matrix.
                 */
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

                /**
                 * @brief Computes the determinant of a square matrix.
                 *
                 * Uses direct formulas for 2x2 and 3x3 matrices, and LU decomposition
                 * for larger sizes. Only valid for square matrices.
                 *
                 * @return The determinant value.
                 */
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

                /**
                 * @brief Computes the inverse of a square matrix.
                 *
                 * Uses direct formulas for 2x2 and 3x3, Gaussian elimination for larger.
                 * Only valid for square matrices.
                 *
                 * @param err Optional error output; set to Error::SingularMatrix if the matrix
                 *            is singular and cannot be inverted.
                 * @return The inverse matrix, or a zero matrix on failure.
                 */
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

                /**
                 * @brief Computes the dot product between two matrices treated as vectors.
                 *
                 * Both matrices must be either row vectors or column vectors.
                 *
                 * @tparam L Size of the other vector.
                 * @param other The other vector matrix.
                 * @return The scalar dot product.
                 */
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

                /**
                 * @brief Applies a function to each element of the matrix.
                 * @tparam Func A callable that takes a T and returns a T.
                 * @param func The function to apply.
                 * @return A new matrix with the function applied to each element.
                 */
                template<typename Func> Matrix<T, W, H> apply(Func&& func) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = func((*this)[i][j]);
                                }
                        }
                        return result;
                }

                /**
                 * @brief Computes the Frobenius norm of the matrix.
                 * @return The square root of the sum of squared elements.
                 */
                T frobeniusNorm() const {
                        T sum = 0;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        sum += (*this)[i][j] * (*this)[i][j];
                                }
                        }
                        return std::sqrt(sum);
                }

                /**
                 * @brief Computes the trace (sum of diagonal elements) of a square matrix.
                 * @return The trace value.
                 */
                T trace() const {
                        static_assert(W == H, "Trace can only be calculated for square matrices");
                        T sum = 0;
                        for (size_t i = 0; i < H; ++i) {
                                sum += (*this)[i][i];
                        }
                        return sum;
                }

                /**
                 * @brief Computes the sum of all elements in the matrix.
                 * @return The total sum.
                 */
                T sum() const {
                        T total = 0;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        total += (*this)[i][j];
                                }
                        }
                        return total;
                }

                /**
                 * @brief Computes the Hadamard (element-wise) product of two matrices.
                 * @param other The matrix to multiply element-wise.
                 * @return A new matrix containing the element-wise products.
                 */
                Matrix<T, W, H> hadamardProduct(const Matrix<T, W, H>& other) const {
                        Matrix<T, W, H> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (*this)[i][j] * other[i][j];
                                }
                        }
                        return result;
                }

                /**
                 * @brief Computes the outer product of two column vectors.
                 *
                 * This matrix must be a column vector (W == 1).
                 *
                 * @tparam L Length of the other column vector.
                 * @param other The other column vector.
                 * @return The outer product matrix of dimensions H x L.
                 */
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
                
                /**
                 * @brief Computes the sum of each row.
                 * @return A column vector containing the sum of each row.
                 */
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

                /**
                 * @brief Computes the sum of each column.
                 * @return A row vector containing the sum of each column.
                 */
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

                /**
                 * @brief Extracts the diagonal elements of a square matrix.
                 * @return A column vector containing the diagonal elements.
                 */
                Matrix<T, W, 1> diagonal() const {
                        static_assert(W == H, "Diagonal can only be calculated for square matrices");

                        Matrix<T, W, 1> result;
                        for (size_t i = 0; i < W; ++i) {
                                result[i][0] = (*this)[i][i];
                        }
                        return result;
                }
                
                /**
                 * @brief Computes the mean of each row.
                 * @return A column vector containing the mean of each row.
                 */
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

                /**
                 * @brief Computes the mean of each column.
                 * @return A row vector containing the mean of each column.
                 */
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

                /**
                 * @brief Rotates the matrix 90 degrees clockwise.
                 * @return A new matrix with dimensions transposed and elements rotated.
                 */
                Matrix<T, H, W> rotateCW() const {
                        Matrix<T, H, W> result;
                        for (size_t i = 0; i < H; ++i) {
                                for (size_t j = 0; j < W; ++j) {
                                        result[i][j] = (*this)[H - j - 1][i];
                                }
                        }
                        return result;
                }

                /**
                 * @brief Rotates the matrix 90 degrees counter-clockwise.
                 * @return A new matrix with dimensions transposed and elements rotated.
                 */
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

PROMEKI_NAMESPACE_END

