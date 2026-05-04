/**
 * @file      sharedptr.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Helper for the @c _promeki_clone macros.
 * @ingroup util
 *
 * Returns a heap-allocated copy of @c *self when @c T is
 * copy-constructible, and aborts otherwise.  The abort branch
 * exists so types that genuinely cannot be copied (own a
 * @c Promise, @c UniquePtr, @c std::atomic, etc.) can still
 * participate in the shared-object machinery, paired with
 * @c SharedPtr<T,false> so the abort path is never reached at
 * runtime.
 *
 * The function is a template so the @c if @c constexpr branch
 * that uses @c new @c T(*self) is only instantiated when @c T
 * actually supports copy construction; otherwise the call
 * compiles down to a single @c std::abort().
 *
 * Not intended for direct use — invoked from @ref PROMEKI_SHARED_BASE,
 * @ref PROMEKI_SHARED_DERIVED, and @ref PROMEKI_SHARED_FINAL.
 */
template <typename T> T *promekiCloneOrAbort(const T *self) {
        if constexpr (std::is_copy_constructible_v<T>) {
                return new T(*self);
        } else {
                (void)self;
                std::abort();
        }
}

/**
 * @brief Macro to mark the polymorphic root of a shared-object hierarchy.
 * @ingroup util
 *
 * Adds the reference count and a virtual @c _promeki_clone()
 * that returns @c BASE*.  The clone body delegates to
 * @c promekiCloneOrAbort, so the same macro handles both
 * copyable bases (the normal CoW case) and non-copyable bases
 * (where copy-on-write must be disabled at the @c SharedPtr
 * level): types whose copy constructor is deleted or whose
 * members forbid copying compile cleanly into an
 * abort-on-clone path instead of failing the macro
 * expansion.
 *
 * Pair with @ref PROMEKI_SHARED_DERIVED on each concrete leaf
 * and (optionally) @ref PROMEKI_SHARED_ABSTRACT on intermediate
 * abstract layers.  For non-copyable bases, callers @b must use
 * @c SharedPtr<BASE, false> so @c modify() never reaches the
 * abort path.
 *
 * Example:
 *
 * @code
 * class MyBaseClass {
 *     PROMEKI_SHARED_BASE(MyBaseClass)
 *     public:
 *        virtual ~MyBaseClass();
 *        // The rest of your class definition
 * };
 * @endcode
 */
#define PROMEKI_SHARED_BASE(BASE)                                                                                      \
public:                                                                                                                \
        RefCount      _promeki_refct;                                                                                  \
        virtual BASE *_promeki_clone() const {                                                                         \
                return ::promeki::promekiCloneOrAbort<BASE>(this);                                                     \
        }

/**
 * @brief Macro for concrete leaves of a shared-object hierarchy.
 *
 * Adds a covariant @c _promeki_clone() override that returns
 * @c DERIVED* (so @c SharedPtr<DERIVED>::modify() detaches into
 * a typed pointer without a downcast).  The body delegates to
 * @c promekiCloneOrAbort, so leaves that are themselves
 * non-copyable compile into an abort-on-clone path instead of
 * needing a separate macro.
 *
 * Assumes the base class is already marked with
 * @ref PROMEKI_SHARED_BASE (or hand-rolls an equivalent virtual
 * @c _promeki_clone).
 *
 * Example:
 *
 * @code
 * class MyDerivedClass : public MyBaseClass {
 *     PROMEKI_SHARED_DERIVED(MyDerivedClass)
 *     public:
 *         virtual ~MyDerivedClass();
 *         // The rest of your class definition
 * };
 * @endcode
 */
#define PROMEKI_SHARED_DERIVED(DERIVED)                                                                                \
public:                                                                                                                \
        DERIVED *_promeki_clone() const override {                                                                     \
                return ::promeki::promekiCloneOrAbort<DERIVED>(this);                                                  \
        }

/**
 * @brief Macro for abstract intermediate classes in a shared hierarchy.
 *
 * Use this on a class that derives from a base already marked with
 * @ref PROMEKI_SHARED_BASE (or a hand-rolled equivalent) and that
 * stays @em abstract — concrete leaves below supply the actual
 * clone.  The macro adds a covariant pure-virtual override of
 * @c _promeki_clone so @c SharedPtr<INTERMEDIATE>::modify() detaches
 * directly into an @c INTERMEDIATE pointer without a downcast, and
 * @c sharedPointerCast<INTERMEDIATE>(basePtr) yields a typed Ptr whose
 * @c modify() is valid without further work at the call site.
 *
 * Example:
 *
 * @code
 * class VideoPayload : public MediaPayload {
 *     PROMEKI_SHARED_ABSTRACT(VideoPayload)
 *     public:
 *         / concrete leaves provide _promeki_clone
 * };
 * @endcode
 */
#define PROMEKI_SHARED_ABSTRACT(INTERMEDIATE)                                                                          \
public:                                                                                                                \
        INTERMEDIATE *_promeki_clone() const override = 0;

/**
 * @brief Macro for non-polymorphic native shared objects.
 *
 * Use this instead of @ref PROMEKI_SHARED_BASE when the class will
 * never be subclassed (e.g. private inner Data classes, plain
 * value types).  It avoids the vtable cost by using a
 * non-virtual @c _promeki_clone().  The class does not need a
 * virtual destructor.
 *
 * The body delegates to @c promekiCloneOrAbort, so types that
 * cannot be copied (own a @c Promise, @c UniquePtr, etc.)
 * compile into an abort-on-clone path; pair those with
 * @c SharedPtr<TYPE, false> so the abort path is never taken
 * at runtime.
 *
 * Example:
 *
 * @code
 * class Data {
 *     PROMEKI_SHARED_FINAL(Data)
 *     public:
 *         int value = 0;
 * };
 * @endcode
 */
#define PROMEKI_SHARED_FINAL(TYPE)                                                                                     \
public:                                                                                                                \
        RefCount _promeki_refct;                                                                                       \
        TYPE    *_promeki_clone() const {                                                                              \
                return ::promeki::promekiCloneOrAbort<TYPE>(this);                                                     \
        }

/**
 * @brief An atomic reference count object
 *
 * Since it's atomic, operations are guaranteed to be consistent across threads.  This also
 * means there's going to be an increased cost over a simple reference count.  However,
 * this is generally still faster than any locking semantics that would be required to
 * syncronize data between threads.
 */
class RefCount {
        public:
                /** @brief Threshold at or above which the refcount is considered immortal. */
                static constexpr int Immortal = 0x40000000;

                /** @brief Constructs a reference count initialized to 1. */
                RefCount() : v(1) {}

                /** @brief Copy constructor resets the reference count to 1 for the new object. */
                RefCount(const RefCount &o) : v(1) {}

                /** @brief Copy assignment resets the reference count to 1. */
                RefCount &operator=(const RefCount &) {
                        v.store(1, std::memory_order_relaxed);
                        return *this;
                }

                /**
                 * @brief Atomically increments the reference count.
                 *
                 * No-op for immortal objects. Uses relaxed memory ordering since only
                 * the atomic increment itself needs to be consistent.
                 */
                void inc() {
                        if (v.load(std::memory_order_relaxed) >= Immortal) return;
                        v.fetch_add(1, std::memory_order_relaxed);
                        return;
                }

                /**
                 * @brief Atomically decrements the reference count.
                 * @return True if the count has reached zero (caller should delete the object).
                 *
                 * Returns false for immortal objects (never deleted).
                 */
                bool dec() {
                        if (v.load(std::memory_order_relaxed) >= Immortal) return false;
                        return v.fetch_sub(1, std::memory_order_acq_rel) == 1;
                }

                /** @brief Returns the current reference count value. */
                int value() const { return v.load(std::memory_order_relaxed); }

                /** @brief Returns true if this refcount is immortal (will never reach zero). */
                bool isImmortal() const { return v.load(std::memory_order_relaxed) >= Immortal; }

                /** @brief Marks this refcount as immortal. inc/dec become no-ops. */
                void setImmortal() { v.store(Immortal, std::memory_order_relaxed); }

        private:
                std::atomic<int> v;
};

/**
  * @brief A proxy class for managing reference counting of objects that do not natively support it.
  *
  * This class acts as a proxy to manage the lifetime of objects that do not have built-in reference
  * counting. It uses an internal RefCount object to track the number of references and ensures that
  * the managed object is deleted when the reference count drops to zero.  Generally you don't need
  * to use this object directly, since it will be created by SharedPtr when SharedPtr has been typed
  * with an object that isn't a natively reference counted object (i.e. has no _promeki_refct member)
  *
  * Limitations:
  * - This class assumes that the managed object can be safely deleted.
  * - Copy of objects that use polymorphism can't be done.
  * - It does not support custom deleters.
  *
  * @tparam T The type of the object being managed.
  */
template <typename T> class SharedPtrProxy {
        public:
                RefCount _promeki_refct;

                SharedPtrProxy(T *o) : _object(o) {}
                ~SharedPtrProxy() { delete _object; }
                T *_promeki_clone() const {
                        // When using a proxy data type, i.e. not a "native" shared object,
                        // there's no way to make a copy from a derived object work.
                        assert(typeid(*_object) == typeid(T));
                        return new T(*_object);
                }
                T *object() const { return _object; }

        private:
                T *_object = nullptr;
};

template <typename T, typename = void> struct IsSharedObject : std::false_type {};
template <typename T> struct IsSharedObject<T, std::void_t<decltype(&T::_promeki_refct)>> : std::true_type {};

/**
  * @brief A smart pointer class with reference counting and optional copy-on-write semantics.
  *
  * This class provides a reference-counted smart pointer for managing the lifetime of objects.
  * It supports both native reference counting and proxy-based reference counting for objects
  * that do not natively support it. Additionally, it offers (default enabled) copy-on-write
  * semantics to optimize performance when multiple references exist.
  *
  * Typical use case:
  * @code
  * // Create a new shared object in-place
  * auto ptr = SharedPtr<MyClass>::create(arg1, arg2);
  *
  * // Or take ownership of an existing raw pointer
  * auto ptr2 = SharedPtr<MyClass>::takeOwnership(existingRawPtr);
  *
  * SharedPtr<MyClass> copy = ptr; // Reference count is incremented
  *
  * // You can now push the copy into another thread and the reference counting
  * // will be thread safe.
  * anotherThread->pushToThread(copy);
  *
  * // Anytime you're calling a const function, you can dereference directly.
  * // If using CopyOnWrite, this will not cause a copy operation to happen
  * ptr->constFunc();
  *
  * // Anytime you're calling a non-const function, i.e. a function that may
  * // modify the object, you must access it via the modify() function.  If
  * // CopyOnWrite is enabled, this will cause a copy of the object (if it
  * // has a reference count > 1) and that new copy will be returned (and now
  * // stored as the version of the object pointed to by this SharedPtr).
  * ptr.modify()->nonConstFunc();
  * @endcode
  *
  * Thread Safety:
  * - The reference counting operations are thread-safe.
  * - It is assumed that multiple threads may hold SharedPtr instances to the same object.
  * - Only one thread will delete the shared data when the reference count drops to zero.
  * - Individual SharedPtr instances are not thread-safe and should be used with appropriate
  *   synchronization if shared between threads.  If you're doing this, however, you're probably
  *   doing something wrong.
  *
  * Limitations:
  * - This class does not support custom deleters.
  * - Copy-on-write might introduce performance overhead in some scenarios.
  * - Using non native objects is possible, however has the additonal limitations:
  *   - They incur an extra level of indirection, as a proxy object is used to hold the reference count
  *     and the pointer to the non-native object.
  *   - They can't do CopyOnWrite *if* they use polymorphism, since if T is the base class only the
  *     base class copy constructors will be used (thus splitting your object).  There's an assert
  *     that ensures this will fail.  If you need polymorphism, use native objects.
  *
  * @tparam T The type of the object being managed.
  * @tparam CopyOnWrite Whether copy-on-write semantics are enabled.
  * @tparam ST The storage type for the managed object, for native objects this is the same as T.
  *         For non-native types, this is a proxy object.
  */
template <typename T, bool CopyOnWrite = true,
          typename ST = std::conditional_t<IsSharedObject<T>::value, T, SharedPtrProxy<T>>>
class SharedPtr {
        public:
                // All SharedPtr instantiations are friends of each other so the
                // upcast/downcast helpers can reach the private _data pointer
                // across type boundaries.
                template <typename OT, bool OCoW, typename OST> friend class SharedPtr;

                // sharedPointerCast needs access to _data for the dynamic_cast
                // round-trip; befriend every instantiation of the free function.
                template <typename DerivedT, typename BaseT, bool CoWT, typename STT>
                friend SharedPtr<DerivedT, CoWT, DerivedT> sharedPointerCast(const SharedPtr<BaseT, CoWT, STT> &sp);

                static constexpr bool isNative = IsSharedObject<T>::value;

                // Any attempt to modify the object when it's shared (i.e. a reference
                // count > 1), the object will be copied first and that new object
                // will be the one that's modified.
                static constexpr bool isCopyOnWrite = CopyOnWrite;

                SharedPtr() = default;

                SharedPtr(const SharedPtr &sp) : _data(sp._data) { acquire(); }
                SharedPtr(SharedPtr &&sp) noexcept : _data(sp._data) { sp._data = nullptr; }

                /**
                 * @brief Implicit upcast from SharedPtr of a derived native type.
                 *
                 * Only participates in overload resolution when @c U publicly
                 * derives from @c T, @c U != @c T, both types are native shared
                 * objects, and both use the default storage type (storage = the
                 * object itself).  This lets a @c SharedPtr<CompressedVideoPayload>
                 * convert to a @c SharedPtr<MediaPayload> without cloning, with
                 * the refcount sitting on the shared base's @c _promeki_refct.
                 */
                template <typename U, bool UCoW, typename UST,
                          typename = std::enable_if_t<std::is_base_of_v<T, U> && !std::is_same_v<T, U> &&
                                                      IsSharedObject<T>::value && IsSharedObject<U>::value &&
                                                      std::is_same_v<UST, U>>>
                SharedPtr(const SharedPtr<U, UCoW, UST> &sp) : _data(sp._data) {
                        acquire();
                }

                template <typename U, bool UCoW, typename UST,
                          typename = std::enable_if_t<std::is_base_of_v<T, U> && !std::is_same_v<T, U> &&
                                                      IsSharedObject<T>::value && IsSharedObject<U>::value &&
                                                      std::is_same_v<UST, U>>>
                SharedPtr(SharedPtr<U, UCoW, UST> &&sp) noexcept : _data(sp._data) {
                        sp._data = nullptr;
                }

                ~SharedPtr() { release(); }

                template <typename... Args> static SharedPtr create(Args &&...args) {
                        SharedPtr sp;
                        sp.setData(new T(std::forward<Args>(args)...));
                        return sp;
                }

                static SharedPtr takeOwnership(T *obj) {
                        SharedPtr sp;
                        if (obj != nullptr) sp.setData(obj);
                        return sp;
                }

                SharedPtr &operator=(const SharedPtr &sp) {
                        if (&sp == this) return *this; // we're setting to ourself, nothing changes.
                        release();
                        _data = sp._data;
                        acquire();
                        return *this;
                }

                SharedPtr &operator=(SharedPtr &&sp) noexcept {
                        if (&sp == this) return *this;
                        release();
                        _data = sp._data;
                        sp._data = nullptr;
                        return *this;
                }

                void swap(SharedPtr &other) noexcept { std::swap(_data, other._data); }

                void clear() {
                        release();
                        _data = nullptr;
                        return;
                }

                void detach() {
                        // We only detach if not null and ref count > 1
                        if (_data == nullptr || _data->_promeki_refct.value() < 2) return;
                        T *copy = _data->_promeki_clone();
                        release();
                        setData(copy);
                        // No need to acquire, since new Data object will have a refct of 1
                        return;
                }

                bool isNull() const { return _data == nullptr; }

                bool isValid() const { return _data != nullptr; }

                explicit operator bool() const { return _data != nullptr; }

                bool operator==(const SharedPtr &other) const { return _data == other._data; }

                bool operator!=(const SharedPtr &other) const { return _data != other._data; }

                bool operator==(std::nullptr_t) const { return _data == nullptr; }

                bool operator!=(std::nullptr_t) const { return _data != nullptr; }

                int referenceCount() const { return _data == nullptr ? 0 : _data->_promeki_refct.value(); }

                const T *ptr() const {
                        assert(_data != nullptr);
                        if constexpr (IsSharedObject<T>::value) {
                                return _data;
                        } else {
                                return _data->object();
                        }
                }

                T *modify() {
                        assert(_data != nullptr);
                        if constexpr (CopyOnWrite) detach();
                        if constexpr (IsSharedObject<T>::value) {
                                return _data;
                        } else {
                                return _data->object();
                        }
                }

                const T *operator->() const { return ptr(); }

                const T &operator*() const { return *ptr(); }

        private:
                ST *_data = nullptr;

                // This function assumes you've already assigned _data to the Data you'd like to acquire
                void acquire() {
                        if (_data == nullptr) return;
                        _data->_promeki_refct.inc();
                        return;
                }

                // This function releases _data, and if the reference count has dropped to zero, deletes it
                // however it does not change _data so it will be stale after calling this function.
                // This assumes you know this and are about to change _data (i.e. in a swap situation)
                void release() {
                        if (_data == nullptr) return;
                        if (_data->_promeki_refct.dec()) delete _data;
                        return;
                }

                constexpr void setData(T *obj) {
                        if constexpr (IsSharedObject<T>::value) {
                                _data = obj;
                        } else {
                                _data = new ST(obj);
                        }
                        return;
                }
};

/**
 * @brief Dynamic downcast across a polymorphic SharedPtr hierarchy.
 * @ingroup util
 *
 * Returns a @c SharedPtr<Derived> that shares ownership with @p sp when
 * the pointee is actually a @c Derived (or further derived from it),
 * and an empty @c SharedPtr<Derived> otherwise.  Both @c Base and
 * @c Derived must be native shared objects (use @ref PROMEKI_SHARED_BASE /
 * @ref PROMEKI_SHARED_DERIVED).  The original @c sp is left unchanged.
 *
 * @par Example
 * @code
 * MediaPayload::Ptr mp = encoder->receivePayload();
 * if(auto vp = sharedPointerCast<CompressedVideoPayload>(mp)) {
 *         // vp is CompressedVideoPayload::Ptr sharing ownership with mp
 * }
 * @endcode
 */
template <typename Derived, typename Base, bool CoW, typename ST>
SharedPtr<Derived, CoW, Derived> sharedPointerCast(const SharedPtr<Base, CoW, ST> &sp) {
        static_assert(IsSharedObject<Base>::value, "sharedPointerCast requires native shared objects");
        static_assert(IsSharedObject<Derived>::value, "sharedPointerCast requires native shared objects");
        static_assert(std::is_base_of_v<Base, Derived>, "Derived must publicly derive from Base");
        SharedPtr<Derived, CoW, Derived> result;
        if (sp.isNull()) return result;
        Derived *d = dynamic_cast<Derived *>(sp._data);
        if (d == nullptr) return result;
        result._data = d;
        result._data->_promeki_refct.inc();
        return result;
}

PROMEKI_NAMESPACE_END
