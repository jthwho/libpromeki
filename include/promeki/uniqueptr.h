/**
 * @file      uniqueptr.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cassert>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A move-only smart pointer with exclusive ownership semantics.
 * @ingroup util
 *
 * This class provides exclusive-ownership lifetime management for a
 * heap-allocated object.  Unlike @ref SharedPtr, there is no reference
 * counting, no copy-on-write, and no proxy wrapper for non-native types.
 * Ownership can only be transferred via move operations.
 *
 * Typical use case:
 * @code
 * // Create a new object in-place
 * auto ptr = UniquePtr<MyClass>::create(arg1, arg2);
 *
 * // Or take ownership of an existing raw pointer
 * auto ptr2 = UniquePtr<MyClass>::takeOwnership(existingRawPtr);
 *
 * // Transfer ownership with std::move
 * UniquePtr<MyClass> other = std::move(ptr);
 * // ptr is now null
 *
 * // Access the owned object
 * other->doSomething();
 * @endcode
 *
 * Thread Safety:
 * - A @c UniquePtr is not thread-safe.  It is assumed that exclusive
 *   ownership is maintained by a single thread at a time.  Transferring
 *   ownership between threads requires external synchronization by the
 *   caller.
 *
 * Limitations:
 * - No custom deleters; the owned object is always destroyed with
 *   @c delete.
 * - When upcasting to a base class (either directly or via move), the
 *   base class must have a virtual destructor for correct destruction
 *   to occur, just as with @c std::unique_ptr.
 *
 * @tparam T The type of the object being managed.
 */
template <typename T> class UniquePtr {
        public:
                // All UniquePtr instantiations are friends of each other so the
                // upcast/downcast helpers can reach the private _ptr across
                // type boundaries.
                template <typename U> friend class UniquePtr;

                /** @brief Constructs a null UniquePtr. */
                UniquePtr() = default;

                /** @brief Constructs a null UniquePtr from nullptr. */
                UniquePtr(std::nullptr_t) noexcept {}

                /** @brief UniquePtr is not copyable. */
                UniquePtr(const UniquePtr &) = delete;

                /** @brief UniquePtr is not copyable. */
                UniquePtr &operator=(const UniquePtr &) = delete;

                /** @brief Move-constructs from another UniquePtr, leaving the source null. */
                UniquePtr(UniquePtr &&o) noexcept : _ptr(o._ptr) { o._ptr = nullptr; }

                /**
                 * @brief Implicit move-construct from a UniquePtr of a derived type.
                 *
                 * Only participates in overload resolution when @c U publicly
                 * derives from @c T and @c U != @c T.  The base class should
                 * have a virtual destructor; otherwise deleting through the
                 * base pointer is undefined behavior.
                 */
                template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U> && !std::is_same_v<T, U>>>
                UniquePtr(UniquePtr<U> &&o) noexcept : _ptr(o._ptr) {
                        o._ptr = nullptr;
                }

                ~UniquePtr() { clear(); }

                /** @brief Move-assigns from another UniquePtr, releasing any prior object. */
                UniquePtr &operator=(UniquePtr &&o) noexcept {
                        if (&o == this) return *this;
                        clear();
                        _ptr = o._ptr;
                        o._ptr = nullptr;
                        return *this;
                }

                /**
                 * @brief Move-assign from a UniquePtr of a derived type.
                 *
                 * Releases any currently owned object, then takes ownership of
                 * the derived pointer, leaving the source null.
                 */
                template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U> && !std::is_same_v<T, U>>>
                UniquePtr &operator=(UniquePtr<U> &&o) noexcept {
                        clear();
                        _ptr = o._ptr;
                        o._ptr = nullptr;
                        return *this;
                }

                /**
                 * @brief Constructs a new T in-place and returns a UniquePtr owning it.
                 * @param args Arguments forwarded to T's constructor.
                 */
                template <typename... Args> static UniquePtr create(Args &&...args) {
                        UniquePtr up;
                        up._ptr = new T(std::forward<Args>(args)...);
                        return up;
                }

                /**
                 * @brief Takes exclusive ownership of an existing raw pointer.
                 * @param obj The raw pointer to adopt.  May be null.
                 *
                 * After this call, the UniquePtr is responsible for deleting
                 * @p obj.  The caller must not retain any other owning pointer
                 * to the same object.
                 */
                static UniquePtr takeOwnership(T *obj) {
                        UniquePtr up;
                        up._ptr = obj;
                        return up;
                }

                /** @brief Deletes the owned object (if any) and becomes null. */
                void clear() {
                        if (_ptr == nullptr) return;
                        delete _ptr;
                        _ptr = nullptr;
                        return;
                }

                /**
                 * @brief Releases ownership and returns the raw pointer.
                 * @return The previously owned pointer, or null if this was null.
                 *
                 * After this call, the UniquePtr is null and no longer
                 * responsible for deletion.  The caller takes ownership of the
                 * returned pointer and is responsible for deleting it.
                 */
                T *release() {
                        T *p = _ptr;
                        _ptr = nullptr;
                        return p;
                }

                /**
                 * @brief Replaces the owned object with a new one.
                 * @param obj The new pointer to adopt.  Defaults to null.
                 *
                 * The previously owned object, if any, is deleted via
                 * @c delete, then @p obj is taken as the new owned
                 * pointer.  Matches the contract of
                 * @c std::unique_ptr::reset — calling
                 * @c reset(get()) is a deliberate use-after-free, not
                 * a no-op, so callers must not pass back the
                 * already-owned pointer.
                 */
                void reset(T *obj = nullptr) {
                        delete _ptr;
                        _ptr = obj;
                        return;
                }

                /** @brief Swaps the managed pointer with @p other. */
                void swap(UniquePtr &other) noexcept { std::swap(_ptr, other._ptr); }

                /** @brief Returns true if this UniquePtr owns no object. */
                bool isNull() const { return _ptr == nullptr; }

                /** @brief Returns true if this UniquePtr owns an object. */
                bool isValid() const { return _ptr != nullptr; }

                /** @brief Returns true if this UniquePtr owns an object. */
                explicit operator bool() const { return _ptr != nullptr; }

                bool operator==(const UniquePtr &other) const { return _ptr == other._ptr; }
                bool operator!=(const UniquePtr &other) const { return _ptr != other._ptr; }
                bool operator==(std::nullptr_t) const { return _ptr == nullptr; }
                bool operator!=(std::nullptr_t) const { return _ptr != nullptr; }

                /**
                 * @brief Returns the raw pointer to the owned object.
                 *
                 * Asserts that the UniquePtr is non-null.  Use @ref isNull
                 * first if the caller cannot guarantee ownership, or use
                 * @ref get to obtain a possibly-null raw pointer.
                 */
                T *ptr() const {
                        assert(_ptr != nullptr);
                        return _ptr;
                }

                /**
                 * @brief Returns the raw pointer without asserting non-null.
                 *
                 * Returns @c nullptr when the UniquePtr owns no object.
                 * Useful for exposing the owned pointer through an accessor
                 * that may legitimately return null.
                 */
                T *get() const { return _ptr; }

                T *operator->() const { return ptr(); }
                T &operator*() const { return *ptr(); }

        private:
                T *_ptr = nullptr;
};

/** @brief Non-member swap for UniquePtr. */
template <typename T> void swap(UniquePtr<T> &a, UniquePtr<T> &b) noexcept {
        a.swap(b);
}

/**
 * @brief Array specialization of @ref UniquePtr.
 * @ingroup util
 *
 * Exclusive-ownership manager for a heap-allocated array.  The
 * destructor uses @c delete[], matching the contract of
 * @c std::unique_ptr<T[]>.  Use this when the element type is
 * non-movable (e.g. @ref Atomic<T> or @c std::atomic<T>) and
 * therefore cannot be stored in @ref List<T>.
 *
 * The API mirrors the single-object specialization where possible,
 * with the additions:
 *
 *   - @ref createArray creates a default-initialised array of @p n
 *     elements via @c new T[n].
 *   - @c operator[] returns a reference to the @p i'th element.
 *
 * @note No size is tracked; the caller is responsible for not
 *       indexing past the original allocation.  Mirrors
 *       @c std::unique_ptr<T[]>.
 *
 * @par Example
 * @code
 * auto buf = UniquePtr<Atomic<float>[]>::createArray(channels);
 * for(size_t i = 0; i < channels; ++i) {
 *         buf[i].store(0.0f, MemoryOrder::Relaxed);
 * }
 * @endcode
 */
template <typename T> class UniquePtr<T[]> {
        public:
                /** @brief Constructs a null UniquePtr. */
                UniquePtr() = default;

                /** @brief Constructs a null UniquePtr from nullptr. */
                UniquePtr(std::nullptr_t) noexcept {}

                /** @brief UniquePtr is not copyable. */
                UniquePtr(const UniquePtr &) = delete;

                /** @brief UniquePtr is not copyable. */
                UniquePtr &operator=(const UniquePtr &) = delete;

                /** @brief Move-constructs from another UniquePtr, leaving the source null. */
                UniquePtr(UniquePtr &&o) noexcept : _ptr(o._ptr) { o._ptr = nullptr; }

                ~UniquePtr() { clear(); }

                /** @brief Move-assigns from another UniquePtr, releasing any prior array. */
                UniquePtr &operator=(UniquePtr &&o) noexcept {
                        if (&o == this) return *this;
                        clear();
                        _ptr = o._ptr;
                        o._ptr = nullptr;
                        return *this;
                }

                /**
                 * @brief Allocates an array of @p n default-initialised elements.
                 *
                 * Equivalent to @c new T[n].  For trivially default-
                 * constructible types this leaves the storage
                 * @b uninitialised; callers that need zeroed memory
                 * should use @ref createArrayValueInit (or perform
                 * their own initialisation after construction).
                 */
                static UniquePtr createArray(size_t n) {
                        UniquePtr up;
                        up._ptr = new T[n];
                        return up;
                }

                /**
                 * @brief Allocates an array of @p n value-initialised elements.
                 *
                 * Equivalent to @c new T[n](), so trivially
                 * default-constructible types come back zeroed and
                 * non-trivial types get their default constructor
                 * invoked on every element.  Use this when the
                 * caller would otherwise immediately follow
                 * @ref createArray with an explicit zero-fill loop —
                 * the value-init form lets the compiler choose the
                 * cheapest initialisation idiom (typically a single
                 * @c memset for scalar types).
                 */
                static UniquePtr createArrayValueInit(size_t n) {
                        UniquePtr up;
                        up._ptr = new T[n]();
                        return up;
                }

                /**
                 * @brief Takes exclusive ownership of an existing raw array pointer.
                 * @param obj The raw pointer to adopt.  May be null.
                 *
                 * The caller must have obtained @p obj from @c new[]
                 * (matching delete[]); the UniquePtr's destructor
                 * uses @c delete[].
                 */
                static UniquePtr takeOwnership(T *obj) {
                        UniquePtr up;
                        up._ptr = obj;
                        return up;
                }

                /** @brief Deletes the owned array (if any) and becomes null. */
                void clear() {
                        if (_ptr == nullptr) return;
                        delete[] _ptr;
                        _ptr = nullptr;
                        return;
                }

                /**
                 * @brief Releases ownership and returns the raw pointer.
                 * @return The previously owned pointer, or null if this was null.
                 */
                T *release() {
                        T *p = _ptr;
                        _ptr = nullptr;
                        return p;
                }

                /**
                 * @brief Replaces the owned array with a new one.
                 *
                 * The previously owned array, if any, is freed via
                 * @c delete[], then @p obj is taken as the new owned
                 * pointer.  Matches the contract of
                 * @c std::unique_ptr<T[]>::reset — calling
                 * @c reset(get()) is a deliberate use-after-free, not
                 * a no-op, so callers must not pass back the
                 * already-owned pointer.
                 */
                void reset(T *obj = nullptr) {
                        delete[] _ptr;
                        _ptr = obj;
                        return;
                }

                /** @brief Swaps the managed array pointer with @p other. */
                void swap(UniquePtr &other) noexcept { std::swap(_ptr, other._ptr); }

                /** @brief Returns true if this UniquePtr owns no array. */
                bool isNull() const { return _ptr == nullptr; }

                /** @brief Returns true if this UniquePtr owns an array. */
                bool isValid() const { return _ptr != nullptr; }

                /** @brief Returns true if this UniquePtr owns an array. */
                explicit operator bool() const { return _ptr != nullptr; }

                bool operator==(const UniquePtr &other) const { return _ptr == other._ptr; }
                bool operator!=(const UniquePtr &other) const { return _ptr != other._ptr; }
                bool operator==(std::nullptr_t) const { return _ptr == nullptr; }
                bool operator!=(std::nullptr_t) const { return _ptr != nullptr; }

                /**
                 * @brief Returns the raw pointer to the owned array.
                 *
                 * Asserts that the UniquePtr is non-null.  Use @ref isNull
                 * first if the caller cannot guarantee ownership, or use
                 * @ref get to obtain a possibly-null raw pointer.
                 */
                T *ptr() const {
                        assert(_ptr != nullptr);
                        return _ptr;
                }

                /**
                 * @brief Returns the raw pointer without asserting non-null.
                 */
                T *get() const { return _ptr; }

                /**
                 * @brief Returns a reference to the @p i'th array element.
                 *
                 * Asserts that the UniquePtr is non-null.  Out-of-range
                 * indices are not bounds-checked — matching
                 * @c std::unique_ptr<T[]>.
                 */
                T &operator[](size_t i) const {
                        assert(_ptr != nullptr);
                        return _ptr[i];
                }

        private:
                T *_ptr = nullptr;
};

/** @brief Non-member swap for the array specialization of UniquePtr. */
template <typename T> void swap(UniquePtr<T[]> &a, UniquePtr<T[]> &b) noexcept {
        a.swap(b);
}

/**
 * @brief Dynamic downcast across a polymorphic UniquePtr hierarchy.
 * @ingroup util
 *
 * Attempts a @c dynamic_cast from @c Base to @c Derived.  On success,
 * ownership is transferred from @p up into the returned UniquePtr and
 * @p up is left null.  On failure (including when @p up is null), an
 * empty UniquePtr is returned and @p up is left unchanged — so the
 * caller retains ownership of the original base object.
 *
 * @par Example
 * @code
 * UniquePtr<MediaPayload> mp = ...;
 * if(auto vp = uniquePointerCast<CompressedVideoPayload>(std::move(mp))) {
 *         // vp owns the payload; mp is now null
 * } else {
 *         // cast failed; mp still owns the original payload
 * }
 * @endcode
 */
template <typename Derived, typename Base> UniquePtr<Derived> uniquePointerCast(UniquePtr<Base> &&up) {
        static_assert(std::is_base_of_v<Base, Derived>, "Derived must publicly derive from Base");
        if (up.isNull()) return UniquePtr<Derived>();
        Derived *d = dynamic_cast<Derived *>(up.ptr());
        if (d == nullptr) return UniquePtr<Derived>();
        up.release();
        return UniquePtr<Derived>::takeOwnership(d);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
