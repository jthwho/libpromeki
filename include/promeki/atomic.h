/**
 * @file      atomic.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Atomic variable wrapping std::atomic\<T\>.
 * @ingroup concurrency
 *
 * Provides load/store with acquire/release semantics, atomic
 * arithmetic for integral types, and compare-and-swap.
 * Non-copyable and non-movable.
 *
 * @par Thread Safety
 * Fully thread-safe by construction.  All public methods may be
 * invoked concurrently from any thread on a single instance — that
 * is the entire purpose of the type.  No external synchronization
 * is required for any operation listed below.
 *
 * @tparam T The value type.  Must satisfy std::atomic requirements.
 */
template <typename T> class Atomic {
        public:
                /**
                 * @brief Constructs an Atomic with the given initial value.
                 * @param val Initial value (default: default-constructed T).
                 */
                Atomic(T val = T{}) : _value(val) {}

                /** @brief Destructor. */
                ~Atomic() = default;

                Atomic(const Atomic &) = delete;
                Atomic &operator=(const Atomic &) = delete;
                Atomic(Atomic &&) = delete;
                Atomic &operator=(Atomic &&) = delete;

                /**
                 * @brief Loads the current value with acquire semantics.
                 * @return The current value.
                 */
                T value() const { return _value.load(std::memory_order_acquire); }

                /**
                 * @brief Stores a new value with release semantics.
                 * @param val The value to store.
                 */
                void setValue(T val) { _value.store(val, std::memory_order_release); }

                /**
                 * @brief Atomically adds @p val and returns the previous value.
                 * @param val The value to add.
                 * @return The value before the addition.
                 * @note Only available for integral types.
                 */
                T fetchAndAdd(T val) { return _value.fetch_add(val, std::memory_order_acq_rel); }

                /**
                 * @brief Atomically subtracts @p val and returns the previous value.
                 * @param val The value to subtract.
                 * @return The value before the subtraction.
                 * @note Only available for integral types.
                 */
                T fetchAndSub(T val) { return _value.fetch_sub(val, std::memory_order_acq_rel); }

                /**
                 * @brief Atomically compares and swaps.
                 *
                 * If the current value equals @p expected, replaces it with
                 * @p desired and returns true.  Otherwise, loads the current
                 * value into @p expected and returns false.
                 *
                 * @param expected Reference to the expected value; updated on failure.
                 * @param desired The value to store on success.
                 * @return True if the swap occurred.
                 */
                bool compareAndSwap(T &expected, T desired) {
                        return _value.compare_exchange_strong(expected, desired, std::memory_order_acq_rel,
                                                              std::memory_order_acquire);
                }

                /**
                 * @brief Atomically replaces the value and returns the previous one.
                 * @param desired The new value.
                 * @return The value before the exchange.
                 */
                T exchange(T desired) { return _value.exchange(desired, std::memory_order_acq_rel); }

        private:
                std::atomic<T> _value;
};

PROMEKI_NAMESPACE_END
