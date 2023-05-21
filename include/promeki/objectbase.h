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
#include <promeki/list.h>
#include <promeki/util.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Creates a signal within a ObjectBase derived object.
 * You should use this macro to create your signal definitions with your object class definition
 */
#define PROMEKI_SIGNAL(name, ...) \
        Signal<__VA_ARGS__> name = Signal<__VA_ARGS__>(PROMEKI_STRINGIFY(name) "(" PROMEKI_STRINGIFY(__VA_ARGS__) ")", this);

PROMEKI_DEBUG(ObjectBase)

class ObjectBase;

using ObjectBaseList = List<ObjectBase *>;


#define PROMEKI_OBJECT \
        private: \
                MetaInfo __metaInfo = MetaInfo(this, typeid(*this).name()); \
        public: \
                virtual const MetaInfo &metaInfo() const { \
                        return __metaInfo; \
                }


/** 
 * @brief Base object for promeki.
 *
 * This object is used by promeki to provide certain objects with a base level of
 * funtionality which include:
 *
 * - Signals and slots
 * - Some level of meta type and reflection
 *
 * The object was modeled from the Qt QObject, although isn't quite as versitle
 * but it trades off that versatility for not needing an external meta object
 * compiler.
 * 
 */
class ObjectBase {
        public:
                /** 
                 * @brief Keeps track of a single signal.
                 *
                 * Normally, you'd use the PROMEKI_SIGNAL(signalName, Arg...) macro to create one
                 * or more of these objects in your ObjectBase derived class.  This object takes
                 * care of managing all the connections to this signal as well as allowing you
                 * to emit it at runtime.
                 */
                template <typename... Args> class Signal {
                        public:
                                using Prototype = void(Args...);
                                using PrototypeFunc = std::function<Prototype>;
                                using PrototypeFuncList = List<PrototypeFunc>;
                                using ParamContainer = std::tuple<std::remove_reference_t<Args>...>;

                                /**
                                 * @brief Signal constructor.  
                                 *
                                 * It must be given a name and parent object.  This is handled
                                 * for you in the PROMEKI_SIGNAL() macro.
                                 */
                                Signal(const char *n, ObjectBase *o) : _name(n), _owner(o) {
                                        promekiDebug("%s [%p] Signal(%p)", _name, this, _owner);
                                }

                                /**
                                 * @brief Returns the name of this signal
                                 */
                                const char *name() const { return _name; }

                                /** 
                                 * @brief Allows you to connect this signal to a normal lambda
                                 * @return The slot connection ID.
                                 *
                                 * NOTE: The lambda prototype must match the one used to define this signal.
                                 */
                                size_t connect(PrototypeFunc slot) {
                                        size_t slotID = _slots.size();
                                        _slots += Info(slot);
                                        promekiDebug("%s [%p] connect() = %d", _name, this, (int)slotID);
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
                                 * someObject->someSignal.connect(myObjectPtr, &MyObject::memberFunction);
                                 * @endcode
                                 */
                                template <typename T> size_t connect(T *obj, void (T::*memberFunction)(Args...)) {
                                        size_t slotID = _slots.size();
                                        _slots += Info(
                                                        ([obj, memberFunction](Args... args) {
                                                         (obj->*memberFunction)(args...);
                                                         }), obj
                                                      );
                                        obj->addCleanupFunc([&](ObjectBase *object) { disconnectFromObject(object); });
                                        promekiDebug("%s [%p] connect(%p) = %d", _name, this, obj, (int)slotID);
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
                                        promekiDebug("%s [%p] disconnect(%d), %d left", _name, this, (int)slotID, (int)_slots.size());
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
                                template <typename T> void disconnect(T *object, void (T::*memberFunction)(Args...)) {
                                        _slots.removeIf([object, memberFunction](const Info &info) { 
                                                        return info.object == object && info.func == memberFunction; 
                                                        });
                                        promekiDebug("%s [%p] disconnect(%p), %d left", _name, this, object, (int)_slots.size());
                                        return;
                                }

                                /**
                                 * @brief Disconnects any connected slots from object 
                                 */
                                void disconnectFromObject(ObjectBase *object) {
                                        _slots.removeIf([object](const Info &info) { return info.object == object; });
                                        promekiDebug("%s [%p] disconnectFromObject(%p), %d left", _name, this, object, (int)_slots.size());
                                        return;
                                }

                                /**
                                 * @brief Emits this signal.
                                 * This causes any connected slots to be called with the arguments you supplied to the emit.
                                 */
                                void emit(Args... args) {
                                        promekiDebug("%s [%p] emit", _name, this);
                                        for (const auto &slot : _slots) {
                                                ObjectBase *oldSender = slot.object->_signalSender;
                                                slot.object->_signalSender = _owner;
                                                slot.func(args...);
                                                slot.object->_signalSender = oldSender;
                                        }
                                        return;
                                }

                                /**
                                 * @brief Emits the signal using a ParamContainer
                                 * Normally, you won't need to call this as emit() will call this if
                                 * needed.  It functions by packing all the parameters into an object
                                 * and calling the slots with that object.  This is slower than a normal
                                 * emit, which is essentially a function call.  This will copy the arguments
                                 * into the object (regardless if given by reference) */
                                void packedEmit(const ParamContainer &params) {
                                        promekiDebug("%s [%p] emitFromContainer", _name, this);
                                        for (const auto &slot : _slots) {
                                                ObjectBase *oldSender = slot.object->_signalSender;
                                                slot.object->_signalSender = _owner;
                                                std::apply(slot.func, params);
                                                slot.object->_signalSender = oldSender;
                                        }
                                }

                                /**
                                 * @brief Packs the arguments into a ParamContainer
                                 * This packs all the signal parameters into ParamContainer object
                                 * that can be used to marshal the data to either defer the emit
                                 * or pass it as an event to another object (ex: on another thread).
                                 * Note, this will also make copies of any arguments passed by
                                 * reference as the references probably aren't going to be valid
                                 * by the time you want to use the container.
                                 */
                                ParamContainer packParams(Args&&... args) {
                                        return ParamContainer(std::forward<Args>(args)...);
                                }

                        private:
                                class Info {
                                        public:
                                                PrototypeFunc   func;
                                                ObjectBase      *object = nullptr;

                                                Info(PrototypeFunc f, ObjectBase *obj = nullptr) : func(f), object(obj) {}
                                };

                                const char              *_name;
                                ObjectBase              *_owner;
                                List<Info>              _slots;
                };

                /**
                 * @brief Captures all the metadata about this object.
                 */
                class MetaInfo {
                        public:
                                MetaInfo(ObjectBase *object, const char *name);

                                ObjectBase *object() const { return _object; }
                                const String &name() const { return _name; }

                        private:
                                ObjectBase      *_object;
                                String          _name;
                };

                PROMEKI_OBJECT // Needs to be below MetaInfo

                /**
                 * @brief Prototype used to register a cleanup function */
                using CleanupFunc = std::function<void(ObjectBase *)>;

                /**
                 * @brief Default ObjectBase constructor
                 * @param[in] p Parent object
                 *
                 * This is the default constructor you'll normally use to construct
                 * the ObjectBase object in your derived class constructor.  If your class
                 * is meant to have a parent object, you should pass it in.  This will ensure
                 * that this object is destroyed if the parent is destroyed.
                 */
                ObjectBase(ObjectBase *p = nullptr) : _parent(p) {
                        metaInfo();
                        if(_parent != nullptr) _parent->addChild(this);
                }

                virtual ~ObjectBase() {
                        aboutToDestroy.emit(this);
                        setParent(nullptr);
                        destroyChildren();
                        runCleanup();
                }

                /**
                 * @brief Returns the parent object, if one.  nullptr if none.
                 * @return Parent object pointer, or nullptr if none.
                 */
                ObjectBase *parent() const {
                        return _parent;
                }

                /**
                 * @brief Sets the parent of this object.
                 * If the object already has a parent, it will be removed as
                 * a child from the old parent and added as a child to the
                 * new one.
                 */
                void setParent(ObjectBase *p) {
                        if(_parent != nullptr) _parent->removeChild(this);
                        _parent = p;
                        if(_parent != nullptr) _parent->addChild(this);
                        return;
                }

                /**
                 * @brief Returns a list of children of this object
                 * @return List of children
                 */
                const ObjectBaseList &children() const {
                        return _children;
                }

                /**
                 * @brief Registers a function to be called when this object is destroyed.  
                 * This can be used to clean up dangling pointers, closing files, etc
                 */
                void addCleanupFunc(CleanupFunc func) {
                        _cleanup += func;
                        return;
                }

                /** Object is about to be destroyed
                 * This signal is emitted when the object is about to be destroyed.  
                 *
                 * NOTE: when this is emitted the object has mostly already been 
                 * torn down so you'll not be able to cast it to a derived object.
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(aboutToDestroy, ObjectBase *);

        protected:
                ObjectBase *signalSender() { return _signalSender; }

        private:
                ObjectBase              *_parent = nullptr;
                ObjectBase              *_signalSender = nullptr;
                ObjectBaseList          _children;
                List<CleanupFunc>       _cleanup;

                void addChild(ObjectBase *c) {
                        _children += c;
                        return;
                }

                void removeChild(ObjectBase *c) {
                        _children.removeFirst(c);
                        return;
                }

                void destroyChildren() {
                        for(auto child : _children) {
                                child->removeChild(this);
                                delete child;
                        }
                        _children.clear();
                        return;
                }

                void runCleanup() {
                        for(auto item : _cleanup) item(this);
                        _cleanup.clear();
                        return;
                }
};

PROMEKI_NAMESPACE_END

