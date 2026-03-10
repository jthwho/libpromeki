/**
 * @file      matrix3x3.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <sstream>
#include <doctest/doctest.h>
#include <promeki/matrix3x3.h>

using namespace promeki;

TEST_CASE("Matrix3x3: default construction is zero") {
        Matrix3x3 m;
        for(int i = 0; i < 3; ++i)
                for(int j = 0; j < 3; ++j)
                        CHECK(m.get(i, j) == 0.0f);
}

TEST_CASE("Matrix3x3: set and get") {
        Matrix3x3 m;
        m.set(1, 2, 3.14f);
        CHECK(m.get(1, 2) == doctest::Approx(3.14f));
}

TEST_CASE("Matrix3x3: get out of bounds returns 0") {
        Matrix3x3 m;
        CHECK(m.get(5, 0) == 0.0f);
        CHECK(m.get(0, 5) == 0.0f);
        CHECK(m.get(-1, 0) == 0.0f);
}

TEST_CASE("Matrix3x3: construction from array") {
        float data[3][3] = {
                {1, 2, 3},
                {4, 5, 6},
                {7, 8, 9}
        };
        Matrix3x3 m(data);
        CHECK(m.get(0, 0) == 1.0f);
        CHECK(m.get(1, 1) == 5.0f);
        CHECK(m.get(2, 2) == 9.0f);
}

TEST_CASE("Matrix3x3: scaling matrix") {
        auto m = Matrix3x3::scalingMatrix(2.0f, 3.0f, 4.0f);
        CHECK(m.get(0, 0) == 2.0f);
        CHECK(m.get(1, 1) == 3.0f);
        CHECK(m.get(2, 2) == 4.0f);
        CHECK(m.get(0, 1) == 0.0f);
}

TEST_CASE("Matrix3x3: addition") {
        float a_data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        float b_data[3][3] = {{9,8,7},{6,5,4},{3,2,1}};
        Matrix3x3 a(a_data);
        Matrix3x3 b(b_data);
        auto c = a + b;
        CHECK(c.get(0, 0) == doctest::Approx(10.0f));
        CHECK(c.get(1, 1) == doctest::Approx(10.0f));
        CHECK(c.get(2, 2) == doctest::Approx(10.0f));
}

TEST_CASE("Matrix3x3: subtraction") {
        float a_data[3][3] = {{5,5,5},{5,5,5},{5,5,5}};
        float b_data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        Matrix3x3 a(a_data);
        Matrix3x3 b(b_data);
        auto c = a - b;
        CHECK(c.get(0, 0) == doctest::Approx(4.0f));
        CHECK(c.get(1, 1) == doctest::Approx(0.0f));
}

TEST_CASE("Matrix3x3: matrix multiplication") {
        auto id = Matrix3x3::scalingMatrix(1.0f, 1.0f, 1.0f);
        float data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        Matrix3x3 m(data);
        auto r = m * id;
        CHECK(r.get(0, 0) == doctest::Approx(1.0f));
        CHECK(r.get(1, 1) == doctest::Approx(5.0f));
        CHECK(r.get(2, 2) == doctest::Approx(9.0f));
}

TEST_CASE("Matrix3x3: scalar multiplication") {
        float data[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
        Matrix3x3 m(data);
        auto r = m * 3.0f;
        CHECK(r.get(0, 0) == doctest::Approx(3.0f));
        CHECK(r.get(1, 1) == doctest::Approx(3.0f));
}

TEST_CASE("Matrix3x3: scalar division") {
        float data[3][3] = {{6,0,0},{0,6,0},{0,0,6}};
        Matrix3x3 m(data);
        auto r = m / 2.0f;
        CHECK(r.get(0, 0) == doctest::Approx(3.0f));
}

TEST_CASE("Matrix3x3: transpose") {
        float data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        Matrix3x3 m(data);
        auto t = m.transpose();
        CHECK(t.get(0, 1) == doctest::Approx(4.0f));
        CHECK(t.get(1, 0) == doctest::Approx(2.0f));
        CHECK(t.get(2, 0) == doctest::Approx(3.0f));
}

TEST_CASE("Matrix3x3: determinant") {
        float data[3][3] = {{6,1,1},{4,-2,5},{2,8,7}};
        Matrix3x3 m(data);
        CHECK(m.determinant() == doctest::Approx(-306.0f));
}

TEST_CASE("Matrix3x3: inverse of identity") {
        float data[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
        Matrix3x3 m(data);
        auto inv = m.inverse();
        CHECK(inv.get(0, 0) == doctest::Approx(1.0f));
        CHECK(inv.get(1, 1) == doctest::Approx(1.0f));
        CHECK(inv.get(2, 2) == doctest::Approx(1.0f));
}

TEST_CASE("Matrix3x3: inverse of singular returns zero") {
        float data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        Matrix3x3 m(data);
        auto inv = m.inverse();
        // Singular matrix - det is 0, returns zero matrix
        CHECK(inv.get(0, 0) == 0.0f);
}

TEST_CASE("Matrix3x3: trace") {
        float data[3][3] = {{1,0,0},{0,5,0},{0,0,9}};
        Matrix3x3 m(data);
        CHECK(m.trace() == doctest::Approx(15.0f));
}

TEST_CASE("Matrix3x3: equality") {
        float data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        Matrix3x3 a(data);
        Matrix3x3 b(data);
        CHECK(a == b);
}

TEST_CASE("Matrix3x3: inequality") {
        float a_data[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
        float b_data[3][3] = {{2,0,0},{0,2,0},{0,0,2}};
        Matrix3x3 a(a_data);
        Matrix3x3 b(b_data);
        CHECK(a != b);
}

TEST_CASE("Matrix3x3: zero") {
        float data[3][3] = {{1,2,3},{4,5,6},{7,8,9}};
        Matrix3x3 m(data);
        m.zero();
        for(int i = 0; i < 3; ++i)
                for(int j = 0; j < 3; ++j)
                        CHECK(m.get(i, j) == 0.0f);
}

TEST_CASE("Matrix3x3: vectorTransform") {
        auto m = Matrix3x3::scalingMatrix(2.0f, 3.0f, 4.0f);
        float v[3] = {1.0f, 1.0f, 1.0f};
        m.vectorTransform(v);
        CHECK(v[0] == doctest::Approx(2.0f));
        CHECK(v[1] == doctest::Approx(3.0f));
        CHECK(v[2] == doctest::Approx(4.0f));
}

TEST_CASE("Matrix3x3: rotation matrix z-axis") {
        float angle = M_PI / 2.0f; // 90 degrees
        auto m = Matrix3x3::rotationMatrix(angle, 'z');
        float v[3] = {1.0f, 0.0f, 0.0f};
        m.vectorTransform(v);
        CHECK(v[0] == doctest::Approx(0.0f).epsilon(0.001f));
        CHECK(v[1] == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("Matrix3x3: ostream operator") {
        Matrix3x3 m;
        m.set(0, 0, 1.0f);
        std::ostringstream os;
        os << m;
        CHECK_FALSE(os.str().empty());
}
