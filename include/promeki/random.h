/**
 * @file      random.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <random>
#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class Buffer;
class Error;

/**
 * @brief Pseudo-random number generator wrapping std::mt19937_64.
 * @ingroup util
 *
 * Random provides two distinct categories of random data:
 *
 * **Pseudo-random (PRNG):** All instance methods use a deterministic
 * Mersenne Twister engine (std::mt19937_64). Given the same seed, the
 * sequence is perfectly reproducible — useful for simulations, testing,
 * and any case where repeatability matters. The default constructor
 * seeds from std::random_device, so unseeded instances produce
 * non-reproducible sequences that are statistically uniform but
 * **not cryptographically secure**. A thread-local global instance is
 * available via global() for convenience.
 *
 * **True random:** The static trueRandom() method reads directly from
 * the platform's hardware/OS entropy source (std::random_device) on
 * every call. This is suitable for cryptographic seeds, UUIDs, nonces,
 * and key generation. It is slower than PRNG output and should not be
 * used for bulk data generation.
 */
class Random {
        public:
                /**
                 * @brief Fills a buffer with true random bytes from the OS entropy source.
                 *
                 * Reads directly from std::random_device on every call. Unlike the
                 * instance methods, this does not use a deterministic PRNG — each
                 * byte is drawn from the platform's entropy pool. Suitable for
                 * cryptographic seeding, UUID generation, and nonce creation.
                 *
                 * @param buf  Pointer to the destination buffer.
                 * @param bytes Number of bytes to generate.
                 * @return Error::Ok on success.
                 */
                static Error trueRandom(uint8_t *buf, size_t bytes);

                /**
                 * @brief Returns a thread-local global Random instance.
                 * @return Reference to the thread-local instance.
                 */
                static Random &global();

                /**
                 * @brief Constructs a Random seeded from std::random_device.
                 */
                Random();

                /**
                 * @brief Constructs a Random with a specific seed.
                 * @param seed The seed value for the engine.
                 */
                explicit Random(uint64_t seed);

                /**
                 * @brief Reseeds the engine.
                 * @param seed The new seed value.
                 */
                void seed(uint64_t seed);

                /**
                 * @brief Returns a uniformly distributed random integer in [min, max].
                 * @param min Lower bound (inclusive).
                 * @param max Upper bound (inclusive).
                 * @return A random integer.
                 */
                int randomInt(int min, int max);

                /**
                 * @brief Returns a uniformly distributed random 64-bit integer in [min, max].
                 * @param min Lower bound (inclusive).
                 * @param max Upper bound (inclusive).
                 * @return A random 64-bit integer.
                 */
                int64_t randomInt64(int64_t min, int64_t max);

                /**
                 * @brief Returns a uniformly distributed random double in [min, max).
                 * @param min Lower bound (inclusive).
                 * @param max Upper bound (exclusive).
                 * @return A random double.
                 */
                double randomDouble(double min, double max);

                /**
                 * @brief Returns a uniformly distributed random float in [min, max).
                 * @param min Lower bound (inclusive).
                 * @param max Upper bound (exclusive).
                 * @return A random float.
                 */
                float randomFloat(float min, float max);

                /**
                 * @brief Returns a Buffer filled with random bytes.
                 * @param count Number of random bytes.
                 * @return A Buffer of the given size filled with random data.
                 */
                Buffer randomBytes(size_t count);

                /**
                 * @brief Returns a random boolean with 50/50 probability.
                 * @return true or false with equal probability.
                 */
                bool randomBool();

        private:
                std::mt19937_64 _engine;
};

PROMEKI_NAMESPACE_END
