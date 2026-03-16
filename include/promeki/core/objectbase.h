/** 
 * @file      core/objectbase.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <tuple>
#include <functional>
#include <promeki/core/namespace.h>
#include <promeki/core/list.h>
#include <promeki/core/map.h>
#include <promeki/core/util.h>
#include <promeki/core/logger.h>
#include <promeki/core/signal.h>
#include <promeki/core/slot.h>

PROMEKI_NAMESPACE_BEGIN

class EventLoop;
class Event;
class TimerEvent;

#define PROMEKI_OBJECT(ObjectName, ParentObjectName) \
        public: \
                static const MetaInfo &metaInfo() { \
                        static const MetaInfo __metaInfo( \
                                typeid(ObjectName).name(), \
                                &ParentObjectName::metaInfo() \
                        ); \
                        return __metaInfo; \
                }
               
#define PROMEKI_SIGNAL(SIGNALNAME, ...) \
        static constexpr const char *SIGNALNAME##SignalName = PROMEKI_STRINGIFY(SIGNALNAME) "(" PROMEKI_STRINGIFY_ARGS(__VA_ARGS__) ")"; \
        static inline SignalMeta SIGNALNAME##SignalMeta = SignalMeta(metaInfo(), SIGNALNAME##SignalName); \
        Signal<__VA_ARGS__> SIGNALNAME##Signal = Signal<__VA_ARGS__>(this, SIGNALNAME##SignalName);

#define PROMEKI_SLOT(SLOTNAME, ...) \
        static constexpr const char *SLOTNAME##SlotName = PROMEKI_STRINGIFY(SLOTNAME) "(" PROMEKI_STRINGIFY_ARGS(__VA_ARGS__) ")"; \
        static inline SlotMeta SLOTNAME##SlotMeta = SlotMeta(metaInfo(), SLOTNAME##SlotName); \
        void SLOTNAME(__VA_ARGS__); \
        Slot<__VA_ARGS__> SLOTNAME##Slot = Slot<__VA_ARGS__>( \
                [this](auto&&... args) { this->SLOTNAME(std::forward<decltype(args)>(args)...); }, \
                this, SLOTNAME##SlotName \
        ); \
        int SLOTNAME##SlotID = registerSlot(&SLOTNAME##Slot);
 

PROMEKI_DEBUG(ObjectBase)

class ObjectBase;

using ObjectBaseList = List<ObjectBase *>;

/**
 * @brief Object that holds a pointer to an ObjectBase (or derived) object.
 * This class will register itself with the given ObjectBase object.  When
 * the registered ObjectBase object is destroyed, it will null the internal
 * pointer.  You can use this object to ensure you don't have dangling
 * pointers to ObjectBase objects. */
class ObjectBasePtr {
        friend class ObjectBase;
        public:
                /**
                 * @brief Constructs a pointer tracking the given ObjectBase.
 * @ingroup core_events
 *
                 * @param object The ObjectBase to track, or nullptr.
                 */
                ObjectBasePtr(ObjectBase *object = nullptr) : p(object) { link(); }

                /** @brief Copy constructor. Tracks the same ObjectBase as the source. */
                ObjectBasePtr(const ObjectBasePtr &object) : p(object.p) { link(); }

                /** @brief Destructor. Unlinks from the tracked ObjectBase. */
                ~ObjectBasePtr() { unlink(); }

                /** @brief Copy assignment operator. Re-links to the new ObjectBase. */
                ObjectBasePtr &operator=(const ObjectBasePtr &object) {
                        unlink();
                        p = object.p;
                        link();
                        return *this;
                }

                /** @brief Returns true if the tracked pointer is not null. */
                bool isValid() const { return p != nullptr; }

                /** @brief Returns a mutable pointer to the tracked ObjectBase. */
                ObjectBase *data() { return p; }

                /** @brief Returns a const pointer to the tracked ObjectBase. */
                const ObjectBase *data() const { return p; }

        private:
                ObjectBase *p = nullptr;

                void link();
                void unlink();
};


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
        friend class ObjectBasePtr;
        friend class EventLoop;
        public:
                class SignalMeta;
                class SlotMeta;

                /**
                 * @brief Captures all the metadata about this object.
                 */
                class MetaInfo {
                        friend class SignalMeta;
                        friend class SlotMeta;
                        public:
                                using SignalList = List<SignalMeta *>;
                                using SlotList = List<SlotMeta *>;
                                /**
                                 * @brief Constructs MetaInfo with a type name and optional parent.
                                 * @param n The mangled type name (typically from typeid).
                                 * @param p The parent MetaInfo, or nullptr for the root.
                                 */
                                MetaInfo(const char *n, const MetaInfo *p = nullptr) : _parent(p), _name(n) {}

                                /** @brief Returns the parent MetaInfo, or nullptr if this is the root. */
                                const MetaInfo *parent() const { return _parent; }

                                /** @brief Returns the demangled type name. */
                                const char *name() const;

                                /** @brief Returns the list of signals registered for this type. */
                                const SignalList &signalList() const { return _signalList; }

                                /** @brief Returns the list of slots registered for this type. */
                                const SlotList &slotList() const { return _slotList; }

                                /** @brief Dumps all MetaInfo fields to the log output. */
                                void dumpToLog() const;

                        private:
                                const MetaInfo                  *_parent;
                                const char                      *_name;
                                mutable String                  _demangledName;
                                mutable SignalList              _signalList;
                                mutable SlotList                _slotList;
                };

                /** @brief Metadata entry describing a signal on an ObjectBase-derived class. */
                class SignalMeta {
                        public:
                                /**
                                 * @brief Constructs a SignalMeta and registers it with the given MetaInfo.
                                 * @param m The MetaInfo of the owning class.
                                 * @param n The signal prototype string.
                                 */
                                SignalMeta(const MetaInfo &m, const char *n) :
                                        _meta(m), _name(n)
                                {
                                        m._signalList += this;
                                }

                                /** @brief Returns the signal prototype string. */
                                const char *name() const { return _name; }
                        private:
                                const MetaInfo  &_meta;
                                const char      *_name;
                };

                /** @brief Metadata entry describing a slot on an ObjectBase-derived class. */
                class SlotMeta {
                        public:
                                /**
                                 * @brief Constructs a SlotMeta and registers it with the given MetaInfo.
                                 * @param m The MetaInfo of the owning class.
                                 * @param n The slot prototype string.
                                 */
                                SlotMeta(const MetaInfo &m, const char *n) :
                                        _meta(m), _name(n)
                                {
                                        m._slotList += this;
                                }

                                /** @brief Returns the slot prototype string. */
                                const char *name() const { return _name; }
                        private:
                                const MetaInfo  &_meta;
                                const char      *_name;
                };

                /** @brief Returns the MetaInfo for the ObjectBase class. */
                static const MetaInfo &metaInfo() {
                        static const MetaInfo __metaInfo(
                                typeid(ObjectBase).name());
                        return __metaInfo;
                }

                /**
                 * @brief connects a signal and slot together.
                 * This function assumes both the signal and slot exist in a ObjectBase or derived object
                 */
                template <typename... Args> static void connect(Signal<Args...> *signal, Slot<Args...> *slot);


                /** @brief Function type for invoking a slot with a list of Variants. */
                using SlotVariantFunc = std::function<void(const VariantList &)>;

                /**
                 * @brief Default ObjectBase constructor
                 * @param[in] p Parent object
                 *
                 * This is the default constructor you'll normally use to construct
                 * the ObjectBase object in your derived class constructor.  If your class
                 * is meant to have a parent object, you should pass it in.  This will ensure
                 * that this object is destroyed if the parent is destroyed.
                 */
                ObjectBase(ObjectBase *p = nullptr);


                /** @brief Destructor. Emits aboutToDestroy, detaches from parent, and destroys children. */
                virtual ~ObjectBase() {
                        aboutToDestroySignal.emit(this);
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
                const ObjectBaseList &childList() const {
                        return _childList;
                }

                /**
                 * @brief Registers a slot with this object and assigns it an ID.
                 * @tparam Args The slot's parameter types.
                 * @param slot Pointer to the Slot to register.
                 * @return The assigned slot ID.
                 */
                template <typename... Args> int registerSlot(Slot<Args...> *slot) {
                        int ret = _slotList.size();
                        slot->setID(ret);
                        _slotList += SlotItem(
                                ret,
                                slot->prototype(),
                                [slot](const VariantList &args) { slot->exec(args); }
                        );
                        return ret;
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

                /**
                 * @brief Returns the EventLoop this object is affiliated with.
                 * @return The EventLoop set at construction time, or nullptr.
                 */
                EventLoop *eventLoop() const { return _eventLoop; }

                /**
                 * @brief Changes the EventLoop affinity of this object.
                 *
                 * Must be called from the object's current thread.  The object
                 * must have no parent.  Children are moved recursively.
                 * Asserts on violation of either constraint.
                 *
                 * @param loop The new EventLoop to affiliate with.
                 */
                void moveToThread(EventLoop *loop);

                /**
                 * @brief Starts a timer on this object's EventLoop.
                 *
                 * TimerEvent will be delivered to this object's timerEvent()
                 * method each time the timer fires.
                 *
                 * @param intervalMs The timer interval in milliseconds.
                 * @param singleShot If true, the timer fires once and is removed.
                 * @return The timer ID, or -1 if no EventLoop is available.
                 */
                int startTimer(unsigned int intervalMs, bool singleShot = false);

                /**
                 * @brief Stops a timer previously started with startTimer().
                 * @param timerId The timer ID returned by startTimer().
                 */
                void stopTimer(int timerId);

        protected:
                /** @brief Returns the ObjectBase that emitted the signal currently being handled. */
                ObjectBase *signalSender() { return _signalSender; }

                /**
                 * @brief Called by EventLoop to deliver events to this object.
                 *
                 * The default implementation dispatches TimerEvent to
                 * timerEvent() and accepts it.
                 *
                 * @param e The event to handle.
                 */
                virtual void event(Event *e);

                /**
                 * @brief Called when a timer fires for this object.
                 *
                 * Override this in derived classes to handle timer events.
                 * The default implementation does nothing.
                 *
                 * @param e The timer event.
                 */
                virtual void timerEvent(TimerEvent *e);


        private:
                using CleanupFunc = std::function<void(ObjectBase *)>;

                struct SlotItem {
                        int                     id;
                        const char              *prototype;
                        SlotVariantFunc         variantFunc;
                };

                struct Cleanup {
                        ObjectBasePtr           object;
                        CleanupFunc             func;
                };

                ObjectBase                                      *_parent = nullptr;
                ObjectBase                                      *_signalSender = nullptr;
                EventLoop                                       *_eventLoop = nullptr;
                ObjectBaseList                                  _childList;
                List<SlotItem>                                  _slotList;
                Map<ObjectBasePtr *, ObjectBasePtr *>           _pointerMap;
                List<Cleanup>                                   _cleanupList;

                void setEventLoopRecursive(EventLoop *loop);

                void addChild(ObjectBase *c) {
                        _childList += c;
                        return;
                }

                void removeChild(ObjectBase *c) {
                        _childList.removeFirst(c);
                        return;
                }

                void destroyChildren() {
                        for(auto child : _childList) {
                                child->_parent = nullptr;
                                delete child;
                        }
                        _childList.clear();
                        return;
                }

                void runCleanup() {
                        // Null out any ObjectBasePtr's that are currently pointing
                        // to this object.
                        for(auto item : _pointerMap) item.first->p = nullptr;
                        _pointerMap.clear();

                        // Walk down the cleanup list and run any cleanup functions
                        for(auto &item : _cleanupList) {
                                if(!item.object.isValid()) continue;
                                item.func(this);
                        }
                        _cleanupList.clear();
                        return;
                }
};

inline void ObjectBasePtr::link() {
        if(p != nullptr) p->_pointerMap[this] = this;
        return;
}

inline void ObjectBasePtr::unlink() {
        if(p != nullptr) {
                auto it = p->_pointerMap.find(this);
                p->_pointerMap.remove(it);
        }
        p = nullptr;
}

PROMEKI_NAMESPACE_END

// connect() template requires EventLoop to be complete for cross-thread dispatch.
#include <promeki/core/eventloop.h>

PROMEKI_NAMESPACE_BEGIN

template <typename... Args>
void ObjectBase::connect(Signal<Args...> *signal, Slot<Args...> *slot) {
        if(signal == nullptr || slot == nullptr) return;

        signal->connect([signal, slot](Args... args) {
                ObjectBase *signalObject = static_cast<ObjectBase *>(signal->owner());
                ObjectBase *slotObject = static_cast<ObjectBase *>(slot->owner());

                EventLoop *slotLoop = slotObject->_eventLoop;
                EventLoop *currentLoop = EventLoop::current();

                // Same thread or no event loop: direct call (zero overhead path)
                if(slotLoop == nullptr || slotLoop == currentLoop) {
                        ObjectBase *prevSender = slotObject->_signalSender;
                        slotObject->_signalSender = signalObject;
                        slot->exec(args...);
                        slotObject->_signalSender = prevSender;
                } else {
                        // Cross-thread: marshal via VariantList and post
                        VariantList packed = Signal<Args...>::pack(args...);
                        ObjectBasePtr senderTracker(signalObject);
                        slotLoop->postCallable(
                                [slot, slotObject, packed = std::move(packed),
                                 senderTracker = std::move(senderTracker)]() {
                                        ObjectBase *prevSender = slotObject->_signalSender;
                                        slotObject->_signalSender =
                                                senderTracker.isValid()
                                                ? const_cast<ObjectBase *>(senderTracker.data())
                                                : nullptr;
                                        slot->exec(packed);
                                        slotObject->_signalSender = prevSender;
                                }
                        );
                }
        }, slot->owner());

        // Register a cleanup
        ObjectBase *signalObject = static_cast<ObjectBase *>(signal->owner());
        ObjectBase *slotObject = static_cast<ObjectBase *>(slot->owner());
        slotObject->_cleanupList += Cleanup(
                signalObject,
                [signal](ObjectBase *ptr){ signal->disconnectFromObject(ptr); }
        );
}

PROMEKI_NAMESPACE_END

