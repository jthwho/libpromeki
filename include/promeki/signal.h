/**
 * @file      signal.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <tuple>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/variant_fwd.h>

PROMEKI_NAMESPACE_BEGIN

class ObjectBase;

/**
 * @brief Type-safe signal/slot mechanism for decoupled event notification.
 * @ingroup events
 *
 * Allows connecting callable slots (lambdas or member functions) that are
 * invoked when the signal is emitted. Template parameters define the
 * argument types passed through the signal.
 *
 * @tparam Args The argument types carried by the signal.
 */
template <typename... Args> class Signal {
        public:
                /** @brief Callable type that slots must conform to. */
                using Function = std::function<void(Args...)>;

                /** @brief Metafunction that strips const and reference qualifiers from a type. */
                template <typename T> struct removeConstAndRef {
                        using type = std::remove_const_t<std::remove_reference_t<T>>;
                };

                /** @brief Convenience alias for removeConstAndRef. */
                template <typename T> using RemoveConstAndRef = typename removeConstAndRef<T>::type;

                /**
                 * @brief Packs the arguments into a VariantList.
                 *
                 * Packs all the signal parameters into a VariantList object
                 * that can be used to marshal the data to either defer the emit
                 * or pass it as an event to another object (e.g. on another thread).
                 * Note, this will also make copies of any arguments passed by
                 * reference as the references probably won't be valid
                 * by the time you want to use the container.
                 *
                 * The definition lives in @c signal.tpp so that @c signal.h
                 * does not pull the full @c variant.h through its 40+ member
                 * type headers.  Callers that need to invoke pack() must
                 * include @c promeki/signal.tpp.
                 *
                 * @param args The signal arguments to pack.
                 * @return A VariantList containing copies of all arguments.
                 */
                static VariantList pack(Args... args);

                /**
                 * @brief Signal constructor.
                 * @param[in] owner Pointer to the object/data that owns this signal.
                 * @param[in] prototype Optional prototype string describing the signal signature.
                 */
                Signal(void *owner = nullptr, const char *prototype = nullptr) :
                        _owner(owner), _prototype(prototype) {}

                /**
                 * @brief Returns the owner of this signal, or nullptr if not defined
                 */
                void *owner() const { return _owner; }

                /**
                 * @brief Returns the prototype string, or nullptr if not defined
                 */
                const char *prototype() const { return _prototype; }

                /**
                 * @brief Connects this signal to a callable (lambda or function).
                 *
                 * The callable prototype must match the argument types used to
                 * define this signal.
                 *
                 * @param slot The callable to invoke when the signal is emitted.
                 * @param ptr  Optional pointer associated with this connection,
                 *             used for later disconnection by object.
                 * @return The slot connection ID.
                 */
                size_t connect(Function slot, void *ptr = nullptr) {
                        size_t slotID = _slots.size();
                        _slots += Info(slot, ptr);
                        return slotID;
                }

                /**
                 * @brief Connects a callable with an ObjectBase context
                 *        (Qt-style cross-thread dispatch).
                 *
                 * When the signal is emitted from a thread whose
                 * @ref EventLoop differs from the owner's
                 * @ref ObjectBase::eventLoop, the call is marshalled
                 * onto the owner's EventLoop via @c postCallable and
                 * invoked asynchronously there — matching Qt's
                 * @c Qt::AutoConnection semantics.  When the signal
                 * emits on the owner's own thread the slot is invoked
                 * directly with zero overhead.
                 *
                 * The overload exists as a peer to the untyped
                 * @c connect(Function, void*) so callers that pass an
                 * ObjectBase-derived @c this pointer get the correct
                 * threading behaviour without having to reach for
                 * @ref ObjectBase::connect; plain @c void* callers keep
                 * the raw, same-thread-only semantics.
                 *
                 * Args are captured by value into a @c std::tuple and
                 * unpacked after the post, so argument types must be
                 * copy-constructible.
                 *
                 * @param slot  The callable to invoke.
                 * @param owner ObjectBase context whose EventLoop
                 *              governs dispatch.  Also used for
                 *              disconnect-by-object lookup.
                 * @return The slot connection ID.
                 */
                size_t connect(Function slot, ObjectBase *owner);

                /**
                 * @brief Connects this signal to an object member function.
                 *
                 * The member function prototype must match the argument types
                 * used to define this signal.
                 *
                 * Example usage:
                 * @code{.cpp}
                 * someSignal.connect(myObjectPtr, &MyObject::memberFunction);
                 * @endcode
                 *
                 * @tparam T The object type.
                 * @param obj Pointer to the object instance.
                 * @param memberFunction Pointer to the member function to call.
                 * @return The slot connection ID.
                 */
                template <typename T> size_t connect(T *obj, void (T::*memberFunction)(Args...)) {
                        size_t slotID = _slots.size();
                        _slots += Info(
                                ([obj, memberFunction](Args... args) {
                                         (obj->*memberFunction)(args...);
                                }), 
                                obj
                        );
                        return slotID;
                }

                /**
                 * @brief Disconnects a slot by its connection ID.
                 *
                 * Use the ID returned by connect() to disconnect that slot
                 * from this signal.
                 *
                 * @param slotID The connection ID returned by connect().
                 */
                void disconnect(size_t slotID) {
                        _slots.remove(slotID);
                        return;
                }

                /**
                 * @brief Disconnects a slot by its object and member function.
                 *
                 * Example usage:
                 * @code{.cpp}
                 * someObject->someSignal.disconnect(myObjectPtr, &MyObject::memberFunction);
                 * @endcode
                 *
                 * @tparam T The object type.
                 * @param object Pointer to the object whose slot should be removed.
                 * @param memberFunction Pointer to the member function to disconnect.
                 */
                template <typename T> void disconnect(const T *object, void (T::*memberFunction)(Args...)) {
                        _slots.removeIf([object, memberFunction](const Info &info) { 
                                        return static_cast<const T *>(info.object) == object && info.func == memberFunction;
                        });
                        return;
                }

                /**
                 * @brief Disconnects all slots connected from the given object.
                 *
                 * @tparam T The object type.
                 * @param object Pointer to the object whose slots should be removed.
                 */
                template <typename T> void disconnectFromObject(const T *object) {
                        _slots.removeIf([object](const Info &info) { return static_cast<const T *>(info.object) == object; });
                        return;
                }

                /**
                 * @brief Emits this signal.
                 *
                 * Invokes every connected slot with the supplied arguments,
                 * in the order they were connected.
                 *
                 * @param args The arguments to forward to each slot.
                 */
                void emit(Args... args) const {
                        for (const auto &slot : _slots) slot.func(args...);
                        return;
                }


        private:
                class Info {
                        public:
                                Function        func;
                                const void      *object = nullptr;

                                Info(Function f, const void *obj = nullptr) : func(f), object(obj) {}
                };

                void            *_owner = nullptr;
                const char      *_prototype = nullptr;
                List<Info>      _slots;
};

PROMEKI_NAMESPACE_END

