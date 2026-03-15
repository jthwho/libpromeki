/**
 * @file      random.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/core/random.h>
#include <promeki/core/buffer.h>
#include <promeki/core/error.h>

PROMEKI_NAMESPACE_BEGIN

Error Random::trueRandom(uint8_t *buf, size_t bytes) {
        std::random_device rd;
        // std::random_device produces unsigned int values.
        // We extract bytes from each call to minimize calls.
        constexpr size_t bytesPerWord = sizeof(unsigned int);
        size_t i = 0;
        while(i + bytesPerWord <= bytes) {
                unsigned int word = rd();
                std::memcpy(buf + i, &word, bytesPerWord);
                i += bytesPerWord;
        }
        if(i < bytes) {
                unsigned int word = rd();
                std::memcpy(buf + i, &word, bytes - i);
        }
        return Error();
}

Random &Random::global() {
        static thread_local Random instance;
        return instance;
}

Random::Random() {
        std::random_device rd;
        _engine.seed(rd());
}

Random::Random(uint64_t seed) : _engine(seed) {
}

void Random::seed(uint64_t seed) {
        _engine.seed(seed);
}

int Random::randomInt(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(_engine);
}

int64_t Random::randomInt64(int64_t min, int64_t max) {
        std::uniform_int_distribution<int64_t> dist(min, max);
        return dist(_engine);
}

double Random::randomDouble(double min, double max) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(_engine);
}

float Random::randomFloat(float min, float max) {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(_engine);
}

Buffer Random::randomBytes(size_t count) {
        Buffer buf(count);
        if(!buf.isValid()) return buf;
        uint8_t *p = static_cast<uint8_t *>(buf.data());
        std::uniform_int_distribution<int> dist(0, 255);
        for(size_t i = 0; i < count; i++) {
                p[i] = static_cast<uint8_t>(dist(_engine));
        }
        return buf;
}

bool Random::randomBool() {
        std::uniform_int_distribution<int> dist(0, 1);
        return dist(_engine) == 1;
}

PROMEKI_NAMESPACE_END
