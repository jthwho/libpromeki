/**
 * @file      atomic.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <atomic>
#include <type_traits>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Library-side memory-ordering tag.
 * @ingroup concurrency
 *
 * Mirrors the values of @c std::memory_order so callers can choose
 * an ordering without including @c <atomic> directly.  The enum
 * values are not numerically identical to @c std::memory_order_*
 * — translation happens inside @ref Atomic and @ref AtomicRef.
 */
enum class MemoryOrder {
        Relaxed,  ///< No ordering constraints — only atomicity.
        Consume,  ///< Acquire-on-dependent-load (treated as Acquire in practice).
        Acquire,  ///< Acquire semantics on loads.
        Release,  ///< Release semantics on stores.
        AcqRel,   ///< Both acquire and release semantics (RMW operations).
        SeqCst    ///< Sequentially consistent (total order across all SeqCst ops).
};

/**
 * @brief Translates a library @ref MemoryOrder to a std::memory_order.
 * @ingroup concurrency
 *
 * Inline so the compiler folds it to a constant at every call site.
 */
inline constexpr std::memory_order toStdMemoryOrder(MemoryOrder mo) {
        switch (mo) {
                case MemoryOrder::Relaxed: return std::memory_order_relaxed;
                case MemoryOrder::Consume: return std::memory_order_consume;
                case MemoryOrder::Acquire: return std::memory_order_acquire;
                case MemoryOrder::Release: return std::memory_order_release;
                case MemoryOrder::AcqRel:  return std::memory_order_acq_rel;
                case MemoryOrder::SeqCst:  return std::memory_order_seq_cst;
        }
        return std::memory_order_seq_cst;
}

/**
 * @brief Inserts a stand-alone memory fence.
 * @ingroup concurrency
 *
 * Equivalent to @c std::atomic_thread_fence with the translated
 * standard order.  Useful when an algorithm needs to order
 * non-atomic accesses against an atomic load/store on another
 * memory location — e.g. a seqlock that checks the sequence
 * counter before and after a block of plain reads.
 */
inline void atomicThreadFence(MemoryOrder mo) {
        std::atomic_thread_fence(toStdMemoryOrder(mo));
}

/**
 * @brief Atomic variable wrapping std::atomic\<T\>.
 * @ingroup concurrency
 *
 * Provides load / store / fetch_* / compareExchange operations with
 * optional explicit @ref MemoryOrder, matching the std::atomic
 * surface but with library-style camelCase names.  When no order is
 * given the default-named methods preserve the historical
 * acquire-on-load / release-on-store / acq_rel-on-RMW behaviour.
 *
 * Non-copyable and non-movable, mirroring @c std::atomic.
 *
 * @par Thread Safety
 * Fully thread-safe by construction.  All public methods may be
 * invoked concurrently from any thread on a single instance — that
 * is the entire purpose of the type.  No external synchronization
 * is required for any operation listed below.
 *
 * @par Example
 * @code
 * Atomic<int> counter{0};
 * counter.fetchAndAdd(1);                                // acq_rel
 * counter.fetchAndAdd(1, MemoryOrder::Relaxed);          // explicit
 * int v = counter.load(MemoryOrder::Relaxed);
 * counter.store(0, MemoryOrder::Release);
 *
 * int expected = 5;
 * if(counter.compareExchangeStrong(expected, 6,
 *                                  MemoryOrder::AcqRel,
 *                                  MemoryOrder::Acquire)) {
 *     // counter was 5, now 6
 * }
 * @endcode
 *
 * @tparam T The value type.  Must satisfy std::atomic requirements.
 */
template <typename T> class Atomic {
                static_assert(std::is_trivially_copyable_v<T>,
                              "Atomic<T> requires T to be trivially copyable (std::atomic requirement)");

        public:
                /** @brief Underlying value type. */
                using ValueType = T;

                /**
                 * @brief Default-constructs an Atomic holding a value-initialised T.
                 *
                 * Marked @c explicit so that converting an @c int / pointer /
                 * etc. into an @c Atomic\<T\> requires a deliberate
                 * constructor call (@c Atomic\<int\> @c counter{0}, not
                 * @c counter @c = @c 5 or implicit conversion in argument
                 * passing).
                 */
                explicit Atomic() : _value(T{}) {}

                /** @brief Constructs an Atomic with the given initial value. */
                explicit Atomic(T val) : _value(val) {}

                /** @brief Destructor. */
                ~Atomic() = default;

                Atomic(const Atomic &) = delete;
                Atomic &operator=(const Atomic &) = delete;
                Atomic(Atomic &&) = delete;
                Atomic &operator=(Atomic &&) = delete;

                // --------------------------------------------------------
                // Default-order accessors (preserved API).
                // --------------------------------------------------------

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
                 * @brief Named shorthand for strong CAS with AcqRel success / Acquire failure.
                 *
                 * Distinct from @ref compareExchangeStrong(T&,T,MemoryOrder)
                 * whose default is @c SeqCst — this overload exists because
                 * AcqRel/Acquire is the @em typical ordering for refcount,
                 * one-shot flag, and CAS-loop idioms, and a named entry
                 * point keeps those call sites short.  If the failure path
                 * really wants @c Relaxed (monotonic max updates,
                 * statistics counters, etc.), call @ref compareExchangeWeak
                 * with explicit orders.
                 *
                 * On failure, @p expected is updated with the observed
                 * value (same contract as @c std::atomic::compare_exchange_strong).
                 *
                 * @param expected Reference to the expected value; updated on failure.
                 * @param desired  The value to store on success.
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

                // --------------------------------------------------------
                // Explicit-order accessors.
                //
                // Defaults follow the C++ convention: SeqCst on
                // load / store / exchange / compare-exchange (the
                // strongest, safest choice when callers don't think
                // hard about it), AcqRel on read-modify-write
                // (fetch_*) since RMW with seq_cst is a meaningful
                // extra cost on most ISAs and AcqRel is what a
                // refcount-style operation actually needs.  Mirrored
                // identically by @ref AtomicRef.
                // --------------------------------------------------------

                /** @brief Loads with the given memory order. */
                T load(MemoryOrder mo = MemoryOrder::SeqCst) const {
                        return _value.load(toStdMemoryOrder(mo));
                }

                /** @brief Stores with the given memory order. */
                void store(T val, MemoryOrder mo = MemoryOrder::SeqCst) {
                        _value.store(val, toStdMemoryOrder(mo));
                }

                /** @brief Atomically replaces the value with the given memory order. */
                T exchange(T desired, MemoryOrder mo) {
                        return _value.exchange(desired, toStdMemoryOrder(mo));
                }

                /** @brief fetch_add with the given memory order. */
                T fetchAndAdd(T val, MemoryOrder mo = MemoryOrder::AcqRel) {
                        return _value.fetch_add(val, toStdMemoryOrder(mo));
                }

                /** @brief fetch_sub with the given memory order. */
                T fetchAndSub(T val, MemoryOrder mo = MemoryOrder::AcqRel) {
                        return _value.fetch_sub(val, toStdMemoryOrder(mo));
                }

                /** @brief fetch_and with the given memory order (integral types). */
                T fetchAndAnd(T val, MemoryOrder mo = MemoryOrder::AcqRel) {
                        return _value.fetch_and(val, toStdMemoryOrder(mo));
                }

                /** @brief fetch_or with the given memory order (integral types). */
                T fetchAndOr(T val, MemoryOrder mo = MemoryOrder::AcqRel) {
                        return _value.fetch_or(val, toStdMemoryOrder(mo));
                }

                /** @brief fetch_xor with the given memory order (integral types). */
                T fetchAndXor(T val, MemoryOrder mo = MemoryOrder::AcqRel) {
                        return _value.fetch_xor(val, toStdMemoryOrder(mo));
                }

                /**
                 * @brief Strong compare-and-exchange with explicit success/failure orders.
                 *
                 * On failure, @p expected is updated with the observed value.
                 */
                bool compareExchangeStrong(T &expected, T desired, MemoryOrder success, MemoryOrder failure) {
                        return _value.compare_exchange_strong(expected, desired, toStdMemoryOrder(success),
                                                              toStdMemoryOrder(failure));
                }

                /** @brief Strong compare-and-exchange with a single memory order applied to both paths. */
                bool compareExchangeStrong(T &expected, T desired, MemoryOrder mo = MemoryOrder::SeqCst) {
                        return _value.compare_exchange_strong(expected, desired, toStdMemoryOrder(mo));
                }

                /**
                 * @brief Weak compare-and-exchange with explicit success/failure orders.
                 *
                 * May fail spuriously even when @p expected matches the
                 * current value, so call sites should retry in a loop.
                 * On failure, @p expected is updated with the observed value.
                 */
                bool compareExchangeWeak(T &expected, T desired, MemoryOrder success, MemoryOrder failure) {
                        return _value.compare_exchange_weak(expected, desired, toStdMemoryOrder(success),
                                                            toStdMemoryOrder(failure));
                }

                /** @brief Weak compare-and-exchange with a single memory order applied to both paths. */
                bool compareExchangeWeak(T &expected, T desired, MemoryOrder mo = MemoryOrder::SeqCst) {
                        return _value.compare_exchange_weak(expected, desired, toStdMemoryOrder(mo));
                }

                // --------------------------------------------------------
                // Convenience: implicit conversion to T (SeqCst load).
                // --------------------------------------------------------

                /**
                 * @brief Pre-increment (acq_rel).
                 * @return The new value.
                 * @note Only available for integral / pointer types.
                 */
                T operator++() { return _value.fetch_add(1, std::memory_order_acq_rel) + 1; }

                /**
                 * @brief Post-increment (acq_rel).
                 * @return The previous value.
                 * @note Only available for integral / pointer types.
                 */
                T operator++(int) { return _value.fetch_add(1, std::memory_order_acq_rel); }

                /**
                 * @brief Pre-decrement (acq_rel).
                 * @return The new value.
                 * @note Only available for integral / pointer types.
                 */
                T operator--() { return _value.fetch_sub(1, std::memory_order_acq_rel) - 1; }

                /**
                 * @brief Post-decrement (acq_rel).
                 * @return The previous value.
                 * @note Only available for integral / pointer types.
                 */
                T operator--(int) { return _value.fetch_sub(1, std::memory_order_acq_rel); }

        private:
                std::atomic<T> _value;
};

/**
 * @brief Reference-style atomic over externally-owned storage.
 * @ingroup concurrency
 *
 * Library wrapper around @c std::atomic_ref\<T\>.  Use this to apply
 * atomic semantics to a memory location whose lifetime is managed
 * elsewhere — most commonly fields embedded in a fixed-layout
 * struct (e.g. a memory-mapped shared region) where the storage
 * cannot itself be a @ref Atomic\<T\>.
 *
 * The referenced object must be properly aligned (see
 * @c std::atomic_ref<T>::required_alignment) and remain alive for
 * the entire lifetime of the @ref AtomicRef.  Multiple
 * @ref AtomicRef instances may concurrently refer to the same
 * storage — this is explicitly allowed by C++20.  What is
 * undefined behaviour is mixing @ref AtomicRef access with plain
 * (non-atomic) access to the same storage: if any thread holds an
 * @ref AtomicRef to an object, @em all accesses to that object
 * (from every thread, including the constructor of the
 * @ref AtomicRef itself) must go through @ref AtomicRef.
 *
 * @par Thread Safety
 * Thread-safe across all operations exposed below.  Plain
 * (non-atomic) accesses to the same storage during the lifetime of
 * an AtomicRef are undefined behaviour, per the rule above.
 *
 * @par Example
 * @code
 * uint64_t *slot = ...;  // pointer into a mmap'd region
 * AtomicRef<uint64_t> ref(*slot);
 * uint64_t seq = ref.load(MemoryOrder::Acquire);
 * ref.store(seq + 1, MemoryOrder::Release);
 * @endcode
 *
 * @tparam T The value type.  Must satisfy std::atomic_ref requirements.
 */
template <typename T> class AtomicRef {
                static_assert(std::is_trivially_copyable_v<T>,
                              "AtomicRef<T> requires T to be trivially copyable (std::atomic_ref requirement)");

        public:
                /** @brief Underlying value type. */
                using ValueType = T;

                /**
                 * @brief Constructs an AtomicRef bound to @p obj.
                 *
                 * @p obj must remain alive for the lifetime of this
                 * AtomicRef.  Non-aligned storage is undefined
                 * behaviour — see std::atomic_ref::required_alignment.
                 */
                explicit AtomicRef(T &obj) : _ref(obj) {}

                AtomicRef(const AtomicRef &) = default;
                AtomicRef &operator=(const AtomicRef &) = delete;
                AtomicRef(AtomicRef &&) = default;
                AtomicRef &operator=(AtomicRef &&) = delete;

                /** @brief Loads with the given memory order. */
                T load(MemoryOrder mo = MemoryOrder::SeqCst) const {
                        return _ref.load(toStdMemoryOrder(mo));
                }

                /** @brief Stores with the given memory order. */
                void store(T val, MemoryOrder mo = MemoryOrder::SeqCst) const {
                        _ref.store(val, toStdMemoryOrder(mo));
                }

                /** @brief Atomically replaces the value with the given memory order. */
                T exchange(T desired, MemoryOrder mo = MemoryOrder::SeqCst) const {
                        return _ref.exchange(desired, toStdMemoryOrder(mo));
                }

                /** @brief fetch_add with the given memory order. */
                T fetchAndAdd(T val, MemoryOrder mo = MemoryOrder::AcqRel) const {
                        return _ref.fetch_add(val, toStdMemoryOrder(mo));
                }

                /** @brief fetch_sub with the given memory order. */
                T fetchAndSub(T val, MemoryOrder mo = MemoryOrder::AcqRel) const {
                        return _ref.fetch_sub(val, toStdMemoryOrder(mo));
                }

                /**
                 * @brief Strong compare-and-exchange with explicit success/failure orders.
                 *
                 * On failure, @p expected is updated with the observed value.
                 */
                bool compareExchangeStrong(T &expected, T desired, MemoryOrder success, MemoryOrder failure) const {
                        return _ref.compare_exchange_strong(expected, desired, toStdMemoryOrder(success),
                                                            toStdMemoryOrder(failure));
                }

                /** @brief Strong compare-and-exchange with a single memory order applied to both paths. */
                bool compareExchangeStrong(T &expected, T desired, MemoryOrder mo = MemoryOrder::SeqCst) const {
                        return _ref.compare_exchange_strong(expected, desired, toStdMemoryOrder(mo));
                }

                /**
                 * @brief Weak compare-and-exchange with explicit success/failure orders.
                 *
                 * May fail spuriously; retry in a loop.
                 */
                bool compareExchangeWeak(T &expected, T desired, MemoryOrder success, MemoryOrder failure) const {
                        return _ref.compare_exchange_weak(expected, desired, toStdMemoryOrder(success),
                                                          toStdMemoryOrder(failure));
                }

                /** @brief Weak compare-and-exchange with a single memory order applied to both paths. */
                bool compareExchangeWeak(T &expected, T desired, MemoryOrder mo = MemoryOrder::SeqCst) const {
                        return _ref.compare_exchange_weak(expected, desired, toStdMemoryOrder(mo));
                }

        private:
                std::atomic_ref<T> _ref;
};

/**
 * @brief Lock-free boolean flag wrapping std::atomic_flag.
 * @ingroup concurrency
 *
 * The simplest possible atomic primitive — a single boolean with
 * @ref testAndSet and @ref clear operations.  Guaranteed lock-free
 * on every C++ implementation (unlike @ref Atomic\<bool\>, which is
 * only @em usually lock-free), so it is the right building block for
 * low-level spinlocks and one-shot init guards on platforms where
 * the cost of a real mutex would be prohibitive.
 *
 * Default-constructed instances start in the cleared state.  The
 * member is initialised with @c ATOMIC_FLAG_INIT for compatibility
 * with libstdc++ versions where default-init of @c std::atomic_flag
 * is not yet guaranteed cleared — callers never need to repeat that
 * macro at the use site.
 *
 * Non-copyable and non-movable, mirroring @c std::atomic_flag.
 *
 * @par Thread Safety
 * Fully thread-safe.  All public methods may be invoked concurrently
 * from any thread on a single instance.
 *
 * @par Example
 * @code
 * static AtomicFlag inFlight;            // starts cleared
 * while(inFlight.testAndSet(MemoryOrder::Acquire)) {
 *     // another thread holds it — spin or yield.
 * }
 * // ... critical section ...
 * inFlight.clear(MemoryOrder::Release);
 * @endcode
 */
class AtomicFlag {
        public:
                /** @brief Constructs a flag in the cleared state. */
                AtomicFlag() noexcept = default;

                /** @brief Destructor. */
                ~AtomicFlag() = default;

                AtomicFlag(const AtomicFlag &) = delete;
                AtomicFlag &operator=(const AtomicFlag &) = delete;
                AtomicFlag(AtomicFlag &&) = delete;
                AtomicFlag &operator=(AtomicFlag &&) = delete;

                /**
                 * @brief Atomically sets the flag and returns its previous value.
                 *
                 * Returning false means the caller is the first to set
                 * the flag (the typical "I won the race" path); true
                 * means another thread got there first.
                 *
                 * @param mo Memory order (default acquire — the
                 *           usual spin-lock acquisition order).
                 * @return The flag's value @em before this call.
                 */
                bool testAndSet(MemoryOrder mo = MemoryOrder::Acquire) noexcept {
                        return _flag.test_and_set(toStdMemoryOrder(mo));
                }

                /**
                 * @brief Atomically clears the flag.
                 *
                 * @param mo Memory order (default release — the
                 *           usual spin-lock release order).
                 */
                void clear(MemoryOrder mo = MemoryOrder::Release) noexcept {
                        _flag.clear(toStdMemoryOrder(mo));
                }

                /**
                 * @brief Reads the flag without modifying it (C++20).
                 *
                 * @param mo Memory order (default acquire).
                 * @return The current value of the flag.
                 */
                bool test(MemoryOrder mo = MemoryOrder::Acquire) const noexcept {
                        return _flag.test(toStdMemoryOrder(mo));
                }

        private:
                std::atomic_flag _flag = ATOMIC_FLAG_INIT;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
