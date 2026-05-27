/**
 * @file      random.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
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
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — concurrent @c next / @c reseed
 * calls require external synchronization.  @ref global returns a
 * thread-local instance, so callers using @c global() do not need
 * to synchronize.  @ref trueRandom is safe to call from any thread
 * (each call constructs its own @c std::random_device).
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
                 * @brief Returns a Gaussian (normal) distributed random double.
                 * @param mean   Mean of the distribution.
                 * @param stddev Standard deviation of the distribution (must be &gt; 0).
                 * @return A random double drawn from N(mean, stddev²).
                 */
                double randomNormalDouble(double mean, double stddev);

                /**
                 * @brief Returns a Gaussian (normal) distributed random float.
                 * @param mean   Mean of the distribution.
                 * @param stddev Standard deviation of the distribution (must be &gt; 0).
                 * @return A random float drawn from N(mean, stddev²).
                 */
                float randomNormalFloat(float mean, float stddev);

                /**
                 * @brief Returns an exponentially distributed random double.
                 *
                 * The exponential distribution describes the time between
                 * events in a Poisson process with rate parameter @p lambda.
                 *
                 * @param lambda Rate parameter (must be &gt; 0).
                 * @return A random double drawn from Exp(lambda).
                 */
                double randomExponentialDouble(double lambda);

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

                // -- UniformRandomBitGenerator concept --
                //
                // The four members below let Random be passed directly to
                // standard algorithms that take a URBG, e.g. @c std::shuffle
                // and @c std::sample, and to the constructors of any
                // standard distribution (@c std::normal_distribution and
                // friends).

                /** @brief URBG concept: the type produced by @c operator(). */
                using result_type = std::mt19937_64::result_type;

                /** @brief URBG concept: smallest value @c operator() can return. */
                static constexpr result_type min() { return std::mt19937_64::min(); }

                /** @brief URBG concept: largest value @c operator() can return. */
                static constexpr result_type max() { return std::mt19937_64::max(); }

                /** @brief URBG concept: draws the next 64-bit pseudo-random value. */
                result_type operator()() { return _engine(); }

        private:
                std::mt19937_64 _engine;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
