/*****************************************************************************
 * matrix3x3.h
 * April 30, 2023
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

#include <ostream>
#include <cmath>
#include <immintrin.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class Matrix3x3 {
        public:
                friend std::ostream& operator<<(std::ostream& os, const Matrix3x3& matrix) {
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        os << matrix.data[i][j] << ' ';
                                }
                                os << '\n';
                        }
                        return os;
                }

                static constexpr float IdentityMatrix[3][3] = {
                        {1.0f, 0.0f, 0.0f},
                        {0.0f, 1.0f, 0.0f},
                        {0.0f, 0.0f, 1.0f}
                };

                static Matrix3x3 scalingMatrix(float scale_x, float scale_y, float scale_z) {
                        Matrix3x3 result;
                        result.data[0][0] = scale_x;
                        result.data[1][1] = scale_y;
                        result.data[2][2] = scale_z;
                        return result;
                }

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

                Matrix3x3() {
                        zero();
                }

                Matrix3x3(float val[3][3]) {
                        set(val);
                }

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
                
                float dot(int row1, int row2) const {
                        __m128 vec1 = _mm_set_ps(0.0f, data[row1][2], data[row1][1], data[row1][0]);
                        __m128 vec2 = _mm_set_ps(0.0f, data[row2][2], data[row2][1], data[row2][0]);
                        __m128 mul = _mm_mul_ps(vec1, vec2);
                        mul = _mm_hadd_ps(mul, mul);
                        mul = _mm_hadd_ps(mul, mul);
                        return _mm_cvtss_f32(mul);
                }

                void zero() {
                        for(int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        data[i][j] = 0.0f;
                                }
                        }
                        return;
                }

                void set(float val[3][3]) {
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        data[i][j] = val[i][j];
                                }
                        }
                        return;
                }

                Matrix3x3 transpose() const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        result.data[i][j] = data[j][i];
                                }
                        }
                        return result;
                }

                float determinant() const {
                        return data[0][0] * (data[1][1] * data[2][2] - data[1][2] * data[2][1]) -
                                data[0][1] * (data[1][0] * data[2][2] - data[1][2] * data[2][0]) +
                                data[0][2] * (data[1][0] * data[2][1] - data[1][1] * data[2][0]);
                }

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

                float trace() const {
                        return data[0][0] + data[1][1] + data[2][2];
                }

                Matrix3x3 operator*(float scalar) const {
                        Matrix3x3 result;
                        for (int i = 0; i < 3; ++i) {
                                for (int j = 0; j < 3; ++j) {
                                        result.data[i][j] = data[i][j] * scalar;
                                }
                        }
                        return result;

                }

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

                bool operator!=(const Matrix3x3& other) const {
                        return !(*this == other);
                }


                float get(int row, int col) const {
                        if (row >= 0 && row < 3 && col >= 0 && col < 3) {
                                return data[row][col];
                        }
                        return 0.0f; // Invalid row or column, return 0.0f
                }

                void set(int row, int col, float value) {
                        if (row >= 0 && row < 3 && col >= 0 && col < 3) {
                                data[row][col] = value;
                        }
                }

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

