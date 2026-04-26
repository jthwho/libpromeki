/**
 * @file      matrix.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/matrix.h>
#include <promeki/error.h>

using namespace promeki;

TEST_CASE("Matrix: default construction is zero") {
        Matrix<double, 3, 3> m;
        for (size_t i = 0; i < 3; ++i)
                for (size_t j = 0; j < 3; ++j) CHECK(m[i][j] == 0.0);
}

TEST_CASE("Matrix: identity") {
        auto m = Matrix<double, 3, 3>::identity();
        CHECK(m[0][0] == 1.0);
        CHECK(m[1][1] == 1.0);
        CHECK(m[2][2] == 1.0);
        CHECK(m[0][1] == 0.0);
        CHECK(m[1][0] == 0.0);
}

TEST_CASE("Matrix: width and height") {
        Matrix<int, 4, 3> m;
        CHECK(m.width() == 4);
        CHECK(m.height() == 3);
}

TEST_CASE("Matrix: isSquare") {
        Matrix<int, 3, 3> sq;
        Matrix<int, 4, 3> rect;
        CHECK(sq.isSquare());
        CHECK_FALSE(rect.isSquare());
}

TEST_CASE("Matrix: addition") {
        auto a = Matrix<int, 2, 2>::identity();
        auto b = Matrix<int, 2, 2>::identity();
        auto c = a + b;
        CHECK(c[0][0] == 2);
        CHECK(c[1][1] == 2);
        CHECK(c[0][1] == 0);
}

TEST_CASE("Matrix: subtraction") {
        Matrix<int, 2, 2> a;
        a[0][0] = 5;
        a[0][1] = 3;
        a[1][0] = 2;
        a[1][1] = 7;
        auto b = Matrix<int, 2, 2>::identity();
        auto c = a - b;
        CHECK(c[0][0] == 4);
        CHECK(c[1][1] == 6);
}

TEST_CASE("Matrix: scalar multiplication") {
        auto m = Matrix<double, 2, 2>::identity();
        auto r = m * 3.0;
        CHECK(r[0][0] == 3.0);
        CHECK(r[1][1] == 3.0);
        CHECK(r[0][1] == 0.0);
}

TEST_CASE("Matrix: scalar division") {
        Matrix<double, 2, 2> m;
        m[0][0] = 6.0;
        m[0][1] = 4.0;
        m[1][0] = 2.0;
        m[1][1] = 8.0;
        auto r = m / 2.0;
        CHECK(r[0][0] == 3.0);
        CHECK(r[1][1] == 4.0);
}

TEST_CASE("Matrix: matrix multiplication") {
        Matrix<int, 2, 2> a;
        a[0][0] = 1;
        a[0][1] = 2;
        a[1][0] = 3;
        a[1][1] = 4;
        Matrix<int, 2, 2> b;
        b[0][0] = 5;
        b[0][1] = 6;
        b[1][0] = 7;
        b[1][1] = 8;
        auto c = a * b;
        CHECK(c[0][0] == 19);
        CHECK(c[0][1] == 22);
        CHECK(c[1][0] == 43);
        CHECK(c[1][1] == 50);
}

TEST_CASE("Matrix: transpose") {
        Matrix<int, 3, 2> m;
        m[0][0] = 1;
        m[0][1] = 2;
        m[0][2] = 3;
        m[1][0] = 4;
        m[1][1] = 5;
        m[1][2] = 6;
        auto t = m.transpose();
        CHECK(t[0][0] == 1);
        CHECK(t[0][1] == 4);
        CHECK(t[1][0] == 2);
        CHECK(t[2][0] == 3);
}

TEST_CASE("Matrix: determinant 2x2") {
        Matrix<double, 2, 2> m;
        m[0][0] = 3;
        m[0][1] = 8;
        m[1][0] = 4;
        m[1][1] = 6;
        CHECK(m.determinant() == doctest::Approx(-14.0));
}

TEST_CASE("Matrix: determinant 3x3") {
        Matrix<double, 3, 3> m;
        m[0][0] = 6;
        m[0][1] = 1;
        m[0][2] = 1;
        m[1][0] = 4;
        m[1][1] = -2;
        m[1][2] = 5;
        m[2][0] = 2;
        m[2][1] = 8;
        m[2][2] = 7;
        CHECK(m.determinant() == doctest::Approx(-306.0));
}

TEST_CASE("Matrix: inverse 2x2") {
        Matrix<double, 2, 2> m;
        m[0][0] = 4;
        m[0][1] = 7;
        m[1][0] = 2;
        m[1][1] = 6;
        Error err;
        auto  inv = m.inverse(&err);
        CHECK(err.isOk());
        CHECK(inv[0][0] == doctest::Approx(0.6));
        CHECK(inv[0][1] == doctest::Approx(-0.7));
        CHECK(inv[1][0] == doctest::Approx(-0.2));
        CHECK(inv[1][1] == doctest::Approx(0.4));
}

TEST_CASE("Matrix: inverse of singular matrix") {
        Matrix<double, 2, 2> m;
        m[0][0] = 1;
        m[0][1] = 2;
        m[1][0] = 2;
        m[1][1] = 4;
        Error err;
        m.inverse(&err);
        CHECK(err.isError());
        CHECK(err.code() == Error::SingularMatrix);
}

TEST_CASE("Matrix: trace") {
        Matrix<int, 3, 3> m;
        m[0][0] = 1;
        m[1][1] = 5;
        m[2][2] = 9;
        CHECK(m.trace() == 15);
}

TEST_CASE("Matrix: sum") {
        Matrix<int, 2, 2> m;
        m[0][0] = 1;
        m[0][1] = 2;
        m[1][0] = 3;
        m[1][1] = 4;
        CHECK(m.sum() == 10);
}

TEST_CASE("Matrix: hadamard product") {
        Matrix<int, 2, 2> a;
        a[0][0] = 1;
        a[0][1] = 2;
        a[1][0] = 3;
        a[1][1] = 4;
        Matrix<int, 2, 2> b;
        b[0][0] = 5;
        b[0][1] = 6;
        b[1][0] = 7;
        b[1][1] = 8;
        auto c = a.hadamardProduct(b);
        CHECK(c[0][0] == 5);
        CHECK(c[0][1] == 12);
        CHECK(c[1][0] == 21);
        CHECK(c[1][1] == 32);
}

TEST_CASE("Matrix: apply") {
        Matrix<int, 2, 2> m;
        m[0][0] = 1;
        m[0][1] = 2;
        m[1][0] = 3;
        m[1][1] = 4;
        auto r = m.apply([](int v) { return v * 2; });
        CHECK(r[0][0] == 2);
        CHECK(r[1][1] == 8);
}

TEST_CASE("Matrix: rotationMatrix valid dimension") {
        Error err;
        auto  r = Matrix<double, 3, 3>::rotationMatrix(0.0, 0, &err);
        CHECK(err.isOk());
        // Rotation of 0 radians should be identity
        CHECK(r[0][0] == doctest::Approx(1.0));
        CHECK(r[1][1] == doctest::Approx(1.0));
        CHECK(r[2][2] == doctest::Approx(1.0));
}

TEST_CASE("Matrix: rotationMatrix invalid dimension returns error") {
        Error err;
        auto  r = Matrix<double, 3, 3>::rotationMatrix(1.0, 2, &err);
        CHECK(err.isError());
        CHECK(err.code() == Error::InvalidDimension);
}

TEST_CASE("Matrix: identity * identity = identity") {
        auto id = Matrix<double, 3, 3>::identity();
        auto result = id * id;
        for (size_t i = 0; i < 3; ++i)
                for (size_t j = 0; j < 3; ++j) CHECK(result[i][j] == doctest::Approx(id[i][j]));
}
