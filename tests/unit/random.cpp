/**
 * @file      random.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/random.h>
#include <promeki/buffer.h>
#include <promeki/error.h>

using namespace promeki;

TEST_CASE("Random: default construction seeds from random_device") {
        Random rng;
        int    val = rng.randomInt(0, 100);
        CHECK(val >= 0);
        CHECK(val <= 100);
}

TEST_CASE("Random: randomInt range") {
        Random rng(42);
        for (int i = 0; i < 1000; i++) {
                int val = rng.randomInt(10, 20);
                CHECK(val >= 10);
                CHECK(val <= 20);
        }
}

TEST_CASE("Random: randomInt64 range") {
        Random rng(42);
        for (int i = 0; i < 1000; i++) {
                int64_t val = rng.randomInt64(-1000, 1000);
                CHECK(val >= -1000);
                CHECK(val <= 1000);
        }
}

TEST_CASE("Random: randomDouble range") {
        Random rng(42);
        for (int i = 0; i < 1000; i++) {
                double val = rng.randomDouble(1.0, 2.0);
                CHECK(val >= 1.0);
                CHECK(val < 2.0);
        }
}

TEST_CASE("Random: randomFloat range") {
        Random rng(42);
        for (int i = 0; i < 1000; i++) {
                float val = rng.randomFloat(-1.0f, 1.0f);
                CHECK(val >= -1.0f);
                CHECK(val < 1.0f);
        }
}

TEST_CASE("Random: randomBool produces both values") {
        Random rng(42);
        bool   gotTrue = false;
        bool   gotFalse = false;
        for (int i = 0; i < 100; i++) {
                if (rng.randomBool())
                        gotTrue = true;
                else
                        gotFalse = true;
                if (gotTrue && gotFalse) break;
        }
        CHECK(gotTrue);
        CHECK(gotFalse);
}

TEST_CASE("Random: randomBytes returns correct size") {
        Random rng(42);
        Buffer buf = rng.randomBytes(256);
        CHECK(buf.isValid());
        CHECK(buf.size() == 256);
}

TEST_CASE("Random: randomBytes produces non-trivial data") {
        Random   rng(42);
        Buffer   buf = rng.randomBytes(64);
        uint8_t *p = static_cast<uint8_t *>(buf.data());
        bool     allSame = true;
        for (size_t i = 1; i < 64; i++) {
                if (p[i] != p[0]) {
                        allSame = false;
                        break;
                }
        }
        CHECK_FALSE(allSame);
}

TEST_CASE("Random: reproducibility with same seed") {
        Random rng1(12345);
        Random rng2(12345);
        for (int i = 0; i < 100; i++) {
                CHECK(rng1.randomInt(0, 1000000) == rng2.randomInt(0, 1000000));
        }
}

TEST_CASE("Random: seed() reseeds the engine") {
        Random rng(99);
        int    first = rng.randomInt(0, 1000000);
        rng.seed(99);
        int second = rng.randomInt(0, 1000000);
        CHECK(first == second);
}

TEST_CASE("Random: global() returns a valid instance") {
        Random &g = Random::global();
        int     val = g.randomInt(0, 100);
        CHECK(val >= 0);
        CHECK(val <= 100);
}

TEST_CASE("Random: randomBytes zero length") {
        Random rng(42);
        Buffer buf = rng.randomBytes(0);
        CHECK(buf.size() == 0);
}

TEST_CASE("Random: trueRandom fills buffer") {
        uint8_t buf[64] = {};
        Error   err = Random::trueRandom(buf, sizeof(buf));
        CHECK(err.isOk());
        // Verify it's not all zeros (overwhelmingly unlikely for 64 bytes)
        bool allZero = true;
        for (size_t i = 0; i < sizeof(buf); i++) {
                if (buf[i] != 0) {
                        allZero = false;
                        break;
                }
        }
        CHECK_FALSE(allZero);
}

TEST_CASE("Random: trueRandom produces non-trivial data") {
        uint8_t buf[64];
        Random::trueRandom(buf, sizeof(buf));
        bool allSame = true;
        for (size_t i = 1; i < sizeof(buf); i++) {
                if (buf[i] != buf[0]) {
                        allSame = false;
                        break;
                }
        }
        CHECK_FALSE(allSame);
}

TEST_CASE("Random: trueRandom zero length succeeds") {
        uint8_t dummy = 0xAA;
        Error   err = Random::trueRandom(&dummy, 0);
        CHECK(err.isOk());
        CHECK(dummy == 0xAA); // Untouched
}

TEST_CASE("Random: trueRandom small sizes") {
        uint8_t buf[1];
        Error   err = Random::trueRandom(buf, 1);
        CHECK(err.isOk());

        uint8_t buf3[3];
        err = Random::trueRandom(buf3, 3);
        CHECK(err.isOk());
}

TEST_CASE("Random: randomInt with min == max") {
        Random rng(42);
        for (int i = 0; i < 10; i++) {
                CHECK(rng.randomInt(5, 5) == 5);
        }
}

TEST_CASE("Random: randomInt64 with min == max") {
        Random rng(42);
        for (int i = 0; i < 10; i++) {
                CHECK(rng.randomInt64(100, 100) == 100);
        }
}

TEST_CASE("Random: different seeds produce different sequences") {
        Random rng1(111);
        Random rng2(222);
        bool   anyDifferent = false;
        for (int i = 0; i < 10; i++) {
                if (rng1.randomInt(0, 1000000) != rng2.randomInt(0, 1000000)) {
                        anyDifferent = true;
                        break;
                }
        }
        CHECK(anyDifferent);
}

TEST_CASE("Random: randomBytes with different seeds produce different data") {
        Random   rng1(111);
        Random   rng2(222);
        Buffer   b1 = rng1.randomBytes(32);
        Buffer   b2 = rng2.randomBytes(32);
        uint8_t *p1 = static_cast<uint8_t *>(b1.data());
        uint8_t *p2 = static_cast<uint8_t *>(b2.data());
        bool     anyDifferent = false;
        for (size_t i = 0; i < 32; i++) {
                if (p1[i] != p2[i]) {
                        anyDifferent = true;
                        break;
                }
        }
        CHECK(anyDifferent);
}
