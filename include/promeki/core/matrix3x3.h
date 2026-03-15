/**
 * @file      core/matrix3x3.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <ostream>
#include <cmath>
#include <immintrin.h>
#include <promeki/core/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A 3x3 floating-point matrix with SSE-accelerated operations.
 *
 * Provides standard linear algebra operations including addition, subtraction,
 * multiplication, transpose, inverse, and determinant. Element-wise operations
 * and vector transforms are also supported. Uses SSE intrinsics where beneficial.
 */
class Matrix3x3 {
        public:
                /** @brief Stream insertion operator for formatted output. */
                friend std::ostream& operator<<(std::ostream& os, const Matrix3x3& matrix) {
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        os << matrix.data[i][j] << ' ';
                                }
                                os << '\n';
                        }
                        return os;
                }

                /** @brief The 3x3 identity matrix constant. */
                static constexpr float IdentityMatrix[3][3] = {
                        {1.0f, 0.0f, 0.0f},
                        {0.0f, 1.0f, 0.0f},
                        {0.0f, 0.0f, 1.0f}
                };

                /**
                 * @brief Creates a diagonal scaling matrix.
                 * @param scale_x Scale factor along the X axis.
                 * @param scale_y Scale factor along the Y axis.
                 * @param scale_z Scale factor along the Z axis.
                 * @return The scaling matrix.
                 */
                static Matrix3x3 scalingMatrix(float scale_x, float scale_y, float scale_z) {
                        Matrix3x3 result;
                        result.data[0][0] = scale_x;
                        result.data[1][1] = scale_y;
                        result.data[2][2] = scale_z;
                        return result;
                }

                /**
                 * @brief Creates a rotation matrix around the specified axis.
                 * @param angle The rotation angle in radians.
                 * @param axis  The axis of rotation ('x', 'y', or 'z'). Returns identity for invalid axes.
                 * @return The rotation matrix.
                 */
                static Matrix3x3 rotationMatrix(float angle, char axis) {
                        Matrix3x3 result;
                        float cos_angle = std::cos(angle);
                        float sin_angle = std::sin(angle);
                        switch (axis) {
                                case 'x':
                                        result.data[0][0] = 1.0f;
                                        result.data[1][1] = cos_angle;
                                        result.data[1][2] = -sin_angle;
                                        result.data[2][1] = sin_angle;
                                        result.data[2][2] = cos_angle;
                                        break;
                                case 'y':
                                        result.data[0][0] = cos_angle;
                                        result.data[0][2] = sin_angle;
                                        result.data[1][1] = 1.0f;
                                        result.data[2][0] = -sin_angle;
                                        result.data[2][2] = cos_angle;
                                        break;
                                case 'z':
                                        result.data[0][0] = cos_angle;
                                        result.data[0][1] = -sin_angle;
                                        result.data[1][0] = sin_angle;
                                        result.data[1][1] = cos_angle;
                                        result.data[2][2] = 1.0f;
                                        break;
                                default:
                                        // Invalid axis, return identity matrix
                                        result.data[0][0] = 1.0f;
                                        result.data[1][1] = 1.0f;
                                        result.data[2][2] = 1.0f;
                                        break;
                        }
                        return result;
                }

                /** @brief Constructs a zero-initialized matrix. */
                Matrix3x3() {
                        zero();
                }

                /**
                 * @brief Constructs a matrix from a 3x3 float array.
                 * @param val The source array to copy values from.
                 */
                Matrix3x3(float val[3][3]) {
                        set(val);
                }

                /**
                 * @brief Returns the element-wise sum of two matrices.
                 * @param other The matrix to add.
                 * @return The resulting sum matrix.
                 */
                Matrix3x3 operator+(const Matrix3x3& other) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                __m128 row = _mm_loadu_ps(data[i]);
                                __m128 other_row = _mm_loadu_ps(other.data[i]);
                                __m128 sum = _mm_add_ps(row, other_row);
                                _mm_storeu_ps(result.data[i], sum);
                        }
                        return result;
                }

                /**
                 * @brief Returns the element-wise difference of two matrices.
                 * @param other The matrix to subtract.
                 * @return The resulting difference matrix.
                 */
                Matrix3x3 operator-(const Matrix3x3& other) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                __m128 row = _mm_loadu_ps(data[i]);
                                __m128 other_row = _mm_loadu_ps(other.data[i]);
                                __m128 diff = _mm_sub_ps(row, other_row);
                                _mm_storeu_ps(result.data[i], diff);
                        }
                        return result;
                }

                /**
                 * @brief Returns the matrix product of two matrices.
                 * @param other The right-hand matrix.
                 * @return The resulting product matrix.
                 */
                Matrix3x3 operator*(const Matrix3x3& other) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        float sum = 0.0f;
                                        for (int k = 0; k < 3; ++k) {
                                                sum += data[i][k] * other.data[k][j];
                                        }
                                        result.data[i][j] = sum;
                                }
                        }
                        return result;
                }
                
                /**
                 * @brief Computes the dot product of two rows in this matrix.
                 * @param row1 Index of the first row (0-2).
                 * @param row2 Index of the second row (0-2).
                 * @return The dot product of the two row vectors.
                 */
                float dot(int row1, int row2) const {
                        __m128 vec1 = _mm_set_ps(0.0f, data[row1][2], data[row1][1], data[row1][0]);
                        __m128 vec2 = _mm_set_ps(0.0f, data[row2][2], data[row2][1], data[row2][0]);
                        __m128 mul = _mm_mul_ps(vec1, vec2);
                        mul = _mm_hadd_ps(mul, mul);
                        mul = _mm_hadd_ps(mul, mul);
                        return _mm_cvtss_f32(mul);
                }

                /** @brief Sets all matrix elements to zero. */
                void zero() {
                        for(int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        data[i][j] = 0.0f;
                                }
                        }
                        return;
                }

                /**
                 * @brief Copies values from a 3x3 float array into this matrix.
                 * @param val The source array.
                 */
                void set(float val[3][3]) {
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        data[i][j] = val[i][j];
                                }
                        }
                        return;
                }

                /**
                 * @brief Returns the transpose of this matrix.
                 * @return The transposed matrix.
                 */
                Matrix3x3 transpose() const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        result.data[i][j] = data[j][i];
                                }
                        }
                        return result;
                }

                /**
                 * @brief Computes the determinant of this matrix.
                 * @return The determinant value.
                 */
                float determinant() const {
                        return data[0][0] * (data[1][1] * data[2][2] - data[1][2] * data[2][1]) -
                                data[0][1] * (data[1][0] * data[2][2] - data[1][2] * data[2][0]) +
                                data[0][2] * (data[1][0] * data[2][1] - data[1][1] * data[2][0]);
                }

                /**
                 * @brief Computes the inverse of this matrix.
                 * @return The inverse matrix, or a zero matrix if the matrix is singular.
                 */
                Matrix3x3 inverse() const {
                        Matrix3x3 result;
                        float det = determinant();
                        if (det == 0.0f) {
                                // Singular matrix, inverse does not exist
                                return result; // Return zero matrix
                        }
                        float inv_det = 1.0f / det;
                        result.data[0][0] = (data[1][1] * data[2][2] - data[1][2] * data[2][1]) * inv_det;
                        result.data[0][1] = (data[0][2] * data[2][1] - data[0][1] * data[2][2]) * inv_det;
                        result.data[0][2] = (data[0][1] * data[1][2] - data[0][2] * data[1][1]) * inv_det;
                        result.data[1][0] = (data[1][2] * data[2][0] - data[1][0] * data[2][2]) * inv_det;
                        result.data[1][1] = (data[0][0] * data[2][2] - data[0][2] * data[2][0]) * inv_det;
                        result.data[1][2] = (data[0][2] * data[1][0] - data[0][0] * data[1][2]) * inv_det;
                        result.data[2][0] = (data[1][0] * data[2][1] - data[1][1] * data[2][0]) * inv_det;
                        result.data[2][1] = (data[0][1] * data[2][0] - data[0][0] * data[2][1]) * inv_det;
                        result.data[2][2] = (data[0][0] * data[1][1] - data[0][1] * data[1][0]) * inv_det;
                        return result;
                }

                /**
                 * @brief Computes the trace (sum of diagonal elements) of this matrix.
                 * @return The trace value.
                 */
                float trace() const {
                        return data[0][0] + data[1][1] + data[2][2];
                }

                /**
                 * @brief Multiplies every element by a scalar.
                 * @param scalar The scalar multiplier.
                 * @return The scaled matrix.
                 */
                Matrix3x3 operator*(float scalar) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        result.data[i][j] = data[i][j] * scalar;
                                }
                        }
                        return result;

                }

                /**
                 * @brief Divides every element by a scalar.
                 * @param scalar The scalar divisor. Returns a zero matrix if zero.
                 * @return The divided matrix.
                 */
                Matrix3x3 operator/(float scalar) const {
                        Matrix3x3 result;
                        if (scalar != 0.0f) {
                                float inv_scalar = 1.0f / scalar;
                                for (int i = 0; i < 3; ++i) {
                                        for (int j = 0; j < 3; ++j) {
                                                result.data[i][j] = data[i][j] * inv_scalar;
                                        }
                                }
                        }
                        return result;
                }

                /** @brief Returns true if all elements are equal to the other matrix. */
                bool operator==(const Matrix3x3& other) const {
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        if (data[i][j] != other.data[i][j]) {
                                                return false;
                                        }
                                }
                        }
                        return true;
                }

                /** @brief Returns true if any element differs from the other matrix. */
                bool operator!=(const Matrix3x3& other) const {
                        return !(*this == other);
                }


                /**
                 * @brief Returns the element at the given row and column.
                 * @param row The row index (0-2).
                 * @param col The column index (0-2).
                 * @return The element value, or 0.0f if the indices are out of range.
                 */
                float get(int row, int col) const {
                        if (row >= 0 && row < 3 && col >= 0 && col < 3) {
                                return data[row][col];
                        }
                        return 0.0f; // Invalid row or column, return 0.0f
                }

                /**
                 * @brief Sets the element at the given row and column.
                 * @param row   The row index (0-2).
                 * @param col   The column index (0-2).
                 * @param value The value to set. Ignored if indices are out of range.
                 */
                void set(int row, int col, float value) {
                        if (row >= 0 && row < 3 && col >= 0 && col < 3) {
                                data[row][col] = value;
                        }
                }

                /**
                 * @brief Returns the Hadamard (element-wise) product of two matrices.
                 * @param other The matrix to multiply element-wise.
                 * @return The element-wise product matrix.
                 */
                Matrix3x3 elementMultiply(const Matrix3x3& other) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                __m128 row = _mm_set_ps(0.0f, data[i][2], data[i][1], data[i][0]);
                                __m128 other_row = _mm_set_ps(0.0f, other.data[i][2], other.data[i][1], other.data[i][0]);
                                __m128 mul = _mm_mul_ps(row, other_row);
                                _mm_storeu_ps(result.data[i], mul);
                        }
                        return result;
                }

                /**
                 * @brief Returns the element-wise quotient of two matrices.
                 *
                 * Division by zero for individual elements produces zero rather than NaN.
                 *
                 * @param other The divisor matrix.
                 * @return The element-wise quotient matrix.
                 */
                Matrix3x3 elementDivide(const Matrix3x3& other) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                __m128 row = _mm_set_ps(0.0f, data[i][2], data[i][1], data[i][0]);
                                __m128 other_row = _mm_set_ps(0.0f, other.data[i][2], other.data[i][1], other.data[i][0]);
                                // Create a mask to handle division by zero
                                __m128 mask = _mm_cmpneq_ps(other_row, _mm_set1_ps(0.0f));
                                __m128 div = _mm_div_ps(row, other_row);
                                // Apply the mask to avoid NaN values due to division by zero
                                div = _mm_and_ps(div, mask);
                                _mm_storeu_ps(result.data[i], div);
                        }
                        return result;
                }

                /**
                 * @brief Transforms a 3-element vector by this matrix in place.
                 * @param vector A 3-element float array that is multiplied by this matrix.
                 */
                void vectorTransform(float vector[3]) const {
                        float result[3];
                        result[0] = data[0][0] * vector[0] + data[0][1] * vector[1] + data[0][2] * vector[2];
                        result[1] = data[1][0] * vector[0] + data[1][1] * vector[1] + data[1][2] * vector[2];
                        result[2] = data[2][0] * vector[0] + data[2][1] * vector[1] + data[2][2] * vector[2];
                        vector[0] = result[0];
                        vector[1] = result[1];
                        vector[2] = result[2];
                }

        private:
                float data[3][3];
};


PROMEKI_NAMESPACE_END

