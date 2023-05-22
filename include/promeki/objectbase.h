/** 
 * @file objectbase.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#pragma once

#include <map>
#include <tuple>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/util.h>
#include <promeki/logger.h>
#include <promeki/util.h>
#include <promeki/signal.h>
#include <promeki/slot.h>

PROMEKI_NAMESPACE_BEGIN

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
                ObjectBasePtr(ObjectBase *object = nullptr) : p(object) { link(); }
                ObjectBasePtr(const ObjectBasePtr &object) : p(object.p) { link(); }
                ~ObjectBasePtr() { unlink(); }
                ObjectBasePtr &operator=(const ObjectBasePtr &object) {
                        unlink();
                        p = object.p;
                        link();
                        return *this;
                }

                bool isValid() const { return p != nullptr; }
                ObjectBase *data() { return p; }
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
                                MetaInfo(const char *n, const MetaInfo *p = nullptr) : _parent(p), _name(n) {}

                                const MetaInfo *parent() const { return _parent; }
                                const char *name() const;
                                const SignalList &signalList() const { return _signalList; }
                                const SlotList &slotList() const { return _slotList; }
                                void dumpToLog() const;

                        private:
                                const MetaInfo                  *_parent;
                                const char                      *_name;
                                mutable String                  _demangledName;
                                mutable SignalList              _signalList;
                                mutable SlotList                _slotList;
                };

                class SignalMeta {
                        public:
                                SignalMeta(const MetaInfo &m, const char *n) :
                                        _meta(m), _name(n) 
                                {
                                        m._signalList += this;
                                }

                                const char *name() const { return _name; }
                        private:
                                const MetaInfo  &_meta;
                                const char      *_name;
                };

                class SlotMeta {
                        public:
                                SlotMeta(const MetaInfo &m, const char *n) :
                                        _meta(m), _name(n) 
                                {
                                        m._slotList += this;
                                }

                                const char *name() const { return _name; }
                        private:
                                const MetaInfo  &_meta;
                                const char      *_name;
                };

                static const MetaInfo &metaInfo() {
                        static const MetaInfo __metaInfo(
                                typeid(ObjectBase).name()); 
                        return __metaInfo; 
                }

                /**
                 * @brief connects a signal and slot together.
                 * This function assumes both the signal and slot exist in a ObjectBase or derived object
                 */
                template <typename... Args> static void connect(Signal<Args...> *signal, Slot<Args...> *slot) {
                        if(signal == nullptr || slot == nullptr) return;

                        // Connect the slot to the signal.
                        signal->connect([signal, slot](Args... args) {
                                ObjectBase *signalObject = static_cast<ObjectBase *>(signal->owner());
                                ObjectBase *slotObject = static_cast<ObjectBase *>(slot->owner());
                                ObjectBase *prevSender = slotObject->_signalSender;
                                slotObject->_signalSender = signalObject;
                                slot->exec(args...);
                                slotObject->_signalSender = prevSender;
                        }, slot->owner());

                        // Register a cleanup
                        ObjectBase *signalObject = static_cast<ObjectBase *>(signal->owner());
                        ObjectBase *slotObject = static_cast<ObjectBase *>(slot->owner());
                        slotObject->_cleanupList += Cleanup(
                                signalObject, \
                                [signal](ObjectBase *ptr){ signal->disconnectFromObject(ptr); }
                        );
                }

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
                ObjectBase(ObjectBase *p = nullptr) : _parent(p) {
                        if(_parent != nullptr) _parent->addChild(this);
                }

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

        protected:
                ObjectBase *signalSender() { return _signalSender; }

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
                ObjectBaseList                                  _childList;
                List<SlotItem>                                  _slotList;
                std::map<ObjectBasePtr *, ObjectBasePtr *>      _pointerMap;
                List<Cleanup>                                   _cleanupList;

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
                                child->removeChild(this);
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
                p->_pointerMap.erase(it);
        }
        p = nullptr;
}


PROMEKI_NAMESPACE_END

