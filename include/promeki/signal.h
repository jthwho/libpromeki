/** 
 * @file objectbase.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#pragma once

#include <tuple>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

/** 
 * @brief Keeps track of a single signal
 */
template <typename... Args> class Signal {
        public:
                using Function = std::function<void(Args...)>;

                template <typename T> struct removeConstAndRef {
                        using type = std::remove_const_t<std::remove_reference_t<T>>;
                };

                template <typename T> using RemoveConstAndRef = typename removeConstAndRef<T>::type;

                /**
                 * @brief Packs the arguments into a VariantList
                 * This packs all the signal parameters into VariantList object
                 * that can be used to marshal the data to either defer the emit
                 * or pass it as an event to another object (ex: on another thread).
                 * Note, this will also make copies of any arguments passed by
                 * reference as the references probably aren't going to be valid
                 * by the time you want to use the container.
                 */
                static VariantList pack(Args... args) {
                        return { Variant(RemoveConstAndRef<Args>(args))... };
                }

                /**
                 * @brief Signal constructor.
                 * @param[in] owner Pointer of the object/data that owns this signal.
                 */
                Signal(void *owner = nullptr, const char *protoype = nullptr) : 
                        _owner(owner), _prototype(nullptr) {}

                /**
                 * @brief Returns the owner of this signal, or nullptr if not defined
                 */
                void *owner() const { return _owner; }

                /**
                 * @brief Returns the prototype string, or nullptr if not defined
                 */
                const char *prototype() const { return _prototype; }

                /** 
                 * @brief Allows you to connect this signal to a normal lambda
                 * @return The slot connection ID.
                 *
                 * NOTE: The lambda prototype must match the one used to define this signal.
                 */
                size_t connect(Function slot, void *ptr = nullptr) {
                        size_t slotID = _slots.size();
                        _slots += Info(slot, ptr);
                        return slotID;
                }

                /** 
                 * @brief Connect this signal to an object member function.
                 * @return The slot connection ID
                 * NOTE: The object member function prototype must be the same
                 * prototype this object was declared as.
                 *
                 * Example usage:
                 * @code{.cpp}
                 * someSignal.connect(myObjectPtr, &MyObject::memberFunction);
                 * @endcode
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
                 * @brief Disconnect a slot by the slot ID
                 *
                 * You can use the slot ID you were given when you called connect() to disconnect that
                 * slot from this signal 
                 */
                void disconnect(size_t slotID) {
                        _slots.remove(slotID);
                        return;
                }

                /**
                 * @brief Disconnect a slot by the slot object and member function
                 * This allows you to disconnect a slot by the member function
                 *
                 * Example usage:
                 * @code{.cpp}
                 * someObject->someSignal.disconnect(myObjectPtr, &MyObject::memberFunction);
                 * @endcode
                 */
                template <typename T> void disconnect(const T *object, void (T::*memberFunction)(Args...)) {
                        _slots.removeIf([object, memberFunction](const Info &info) { 
                                        return static_cast<const T *>(info.object) == object && info.func == memberFunction;
                        });
                        return;
                }

                /**
                 * @brief Disconnects any connected slots from object 
                 */
                template <typename T> void disconnectFromObject(const T *object) {
                        _slots.removeIf([object](const Info &info) { return static_cast<const T *>(info.object) == object; });
                        return;
                }

                /**
                 * @brief Emits this signal.
                 * This causes any connected slots to be called with the arguments you supplied to the emit.
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

