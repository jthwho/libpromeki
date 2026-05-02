/** 
 * @file      objectbase.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <tuple>
#include <type_traits>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/util.h>
#include <promeki/logger.h>
#include <promeki/variant_fwd.h>
#include <promeki/signal.h>
#include <promeki/slot.h>

PROMEKI_NAMESPACE_BEGIN

class EventLoop;
class Event;
class TimerEvent;
class Thread;

#define PROMEKI_OBJECT(ObjectName, ParentObjectName)                                                                   \
public:                                                                                                                \
        static const MetaInfo &metaInfo() {                                                                            \
                static const MetaInfo __metaInfo(typeid(ObjectName).name(), &ParentObjectName::metaInfo());            \
                return __metaInfo;                                                                                     \
        }

#define PROMEKI_SIGNAL(SIGNALNAME, ...)                                                                                \
        static constexpr const char *SIGNALNAME##SignalName =                                                          \
                PROMEKI_STRINGIFY(SIGNALNAME) "(" PROMEKI_STRINGIFY_ARGS(__VA_ARGS__) ")";                             \
        static inline SignalMeta SIGNALNAME##SignalMeta = SignalMeta(metaInfo(), SIGNALNAME##SignalName);              \
        Signal<__VA_ARGS__>      SIGNALNAME##Signal = Signal<__VA_ARGS__>(this, SIGNALNAME##SignalName);

#define PROMEKI_SLOT(SLOTNAME, ...)                                                                                    \
        static constexpr const char *SLOTNAME##SlotName =                                                              \
                PROMEKI_STRINGIFY(SLOTNAME) "(" PROMEKI_STRINGIFY_ARGS(__VA_ARGS__) ")";                               \
        static inline SlotMeta SLOTNAME##SlotMeta = SlotMeta(metaInfo(), SLOTNAME##SlotName);                          \
        void                   SLOTNAME(__VA_ARGS__);                                                                  \
        Slot<__VA_ARGS__>      SLOTNAME##Slot =                                                                        \
                Slot<__VA_ARGS__>([this](auto &&...args) { this->SLOTNAME(std::forward<decltype(args)>(args)...); },   \
                                  this, SLOTNAME##SlotName);                                                           \
        int SLOTNAME##SlotID = registerSlot(&SLOTNAME##Slot);


class ObjectBase;

using ObjectBaseList = List<ObjectBase *>;

/**
 * @brief Object that holds a pointer to an ObjectBase-derived object.
 *
 * Registers itself with the tracked object so that when the object is
 * destroyed the internal pointer is atomically cleared, giving you a
 * safe, non-dangling reference.  The template parameter @p T selects
 * the concrete type returned by @c data(); it defaults to @c ObjectBase
 * so untyped usage @c ObjectBasePtr<> still works.
 *
 * @tparam T The ObjectBase-derived type being tracked.
 *
 * Class template argument deduction is supported via a deduction guide,
 * so @c ObjectBasePtr{obj} deduces @c ObjectBasePtr<Foo> from a @c Foo*.
 *
 * An @c ObjectBasePtr<Derived> converts implicitly to @c ObjectBasePtr<Base>
 * where @c Derived inherits from @c Base, mirroring raw pointer conversions.
 *
 * @note Thread safety: The internal pointer is stored as a std::atomic,
 * and the ObjectBase pointer map is protected by a Mutex. This allows
 * an ObjectBasePtr to be safely invalidated from a different thread
 * than the one holding it, as happens during cross-thread object
 * destruction. However, the ObjectBasePtr itself is not designed for
 * concurrent read/write from multiple threads without external
 * synchronization.
 */
template <typename T = ObjectBase> class ObjectBasePtr {
                friend class ObjectBase;
                template <typename U> friend class ObjectBasePtr;

        public:
                /** @brief Constructs a pointer tracking the given object. */
                ObjectBasePtr(T *object = nullptr) { linkTo(object); }

                /** @brief Copy constructor. Tracks the same object as the source. */
                ObjectBasePtr(const ObjectBasePtr &other) { linkFromSource(other.p); }

                /**
                 * @brief Converting copy-constructor from an ObjectBasePtr
                 * tracking a derived type.
                 * @tparam U Source type; must derive from @c T.
                 */
                template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U> && !std::is_same_v<T, U>>>
                ObjectBasePtr(const ObjectBasePtr<U> &other) {
                        linkFromSource(other.p);
                }

                /** @brief Destructor. Unlinks from the tracked object. */
                ~ObjectBasePtr() { unlink(); }

                /** @brief Copy assignment operator. Re-links to the new object. */
                ObjectBasePtr &operator=(const ObjectBasePtr &other) {
                        if (this == &other) return *this;
                        unlink();
                        linkFromSource(other.p);
                        return *this;
                }

                /** @brief Returns true if the tracked pointer is not null. */
                bool isValid() const { return p.load(std::memory_order_acquire) != nullptr; }

                /** @brief Returns a mutable pointer to the tracked object. */
                T *data() { return static_cast<T *>(p.load(std::memory_order_acquire)); }

                /** @brief Returns a const pointer to the tracked object. */
                const T *data() const { return static_cast<const T *>(p.load(std::memory_order_acquire)); }

        private:
                std::atomic<ObjectBase *> p{nullptr};

                // Atomically (under @ref ObjectBase::objectBasePtrMutex)
                // sets @c p to @p obj and registers @c &p in
                // @c obj->_pointerMap.  Used by every ObjectBasePtr
                // constructor / assignment so the store of @c p and
                // the addition to @c _pointerMap happen as a single
                // critical section — without this fold a concurrent
                // @ref ObjectBase::runCleanup can null every existing
                // tracker, release the mutex, and let a later @c link
                // write to freed memory while leaving the new tracker
                // dangling-but-non-null.
                void linkTo(ObjectBase *obj);

                // Like @ref linkTo but reads the source pointer from
                // another ObjectBasePtr's atomic under the same lock,
                // so a concurrent destruction of the tracked object
                // cannot slip between the read and the registration.
                void linkFromSource(const std::atomic<ObjectBase *> &source);

                void unlink();
};

/** @brief Deduction guide: @c ObjectBasePtr(Foo*) deduces to @c ObjectBasePtr<Foo>. */
template <typename T> ObjectBasePtr(T *) -> ObjectBasePtr<T>;


/**
 * @brief Base object for promeki.
 *
 * This object is used by promeki to provide certain objects with a base level of
 * functionality which include:
 *
 * - Signals and slots
 * - Some level of meta type and reflection
 *
 * The object was modeled from the Qt QObject, although isn't quite as versatile
 * but it trades off that versatility for not needing an external meta object
 * compiler.
 *
 * @par Thread Safety
 * Thread-affine: an ObjectBase instance belongs to the EventLoop
 * captured at construction time and must only be used from that
 * loop's thread.  Use @ref moveToThread to migrate ownership.
 * Cross-thread interaction goes through @ref Signal connections
 * (which marshal arguments via VariantList and dispatch through
 * the receiver's EventLoop) or via @c eventLoop()->postCallable.
 * The @ref ObjectBasePtr tracker is internally synchronized with
 * a Mutex so it's safe to invalidate from a foreign thread when
 * the tracked object is destroyed.
 */
class ObjectBase {
                template <typename U> friend class ObjectBasePtr;
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
                                const MetaInfo    *_parent;
                                const char        *_name;
                                mutable String     _demangledName;
                                mutable SignalList _signalList;
                                mutable SlotList   _slotList;
                };

                /** @brief Metadata entry describing a signal on an ObjectBase-derived class. */
                class SignalMeta {
                        public:
                                /**
                                 * @brief Constructs a SignalMeta and registers it with the given MetaInfo.
                                 * @param m The MetaInfo of the owning class.
                                 * @param n The signal prototype string.
                                 */
                                SignalMeta(const MetaInfo &m, const char *n) : _meta(m), _name(n) {
                                        m._signalList += this;
                                }

                                /** @brief Returns the signal prototype string. */
                                const char *name() const { return _name; }

                        private:
                                const MetaInfo &_meta;
                                const char     *_name;
                };

                /** @brief Metadata entry describing a slot on an ObjectBase-derived class. */
                class SlotMeta {
                        public:
                                /**
                                 * @brief Constructs a SlotMeta and registers it with the given MetaInfo.
                                 * @param m The MetaInfo of the owning class.
                                 * @param n The slot prototype string.
                                 */
                                SlotMeta(const MetaInfo &m, const char *n) : _meta(m), _name(n) { m._slotList += this; }

                                /** @brief Returns the slot prototype string. */
                                const char *name() const { return _name; }

                        private:
                                const MetaInfo &_meta;
                                const char     *_name;
                };

                /** @brief Returns the MetaInfo for the ObjectBase class. */
                static const MetaInfo &metaInfo() {
                        static const MetaInfo __metaInfo(typeid(ObjectBase).name());
                        return __metaInfo;
                }

                /**
                 * @brief connects a signal and slot together.
                 * This function assumes both the signal and slot exist in a ObjectBase or derived object
                 */
                template <typename... Args> static void connect(Signal<Args...> *signal, Slot<Args...> *slot);


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
                ObjectBase *parent() const { return _parent; }

                /**
                 * @brief Sets the parent of this object.
                 *
                 * If the object already has a parent, it will be removed
                 * as a child from the old parent and added as a child to
                 * the new one.
                 *
                 * @par Thread affinity
                 * @c setParent is thread-affine: @c this and @p p must
                 * share the same owning @ref Thread (or both have no
                 * Thread set).  Cross-thread parenting is rejected at
                 * runtime via @c PROMEKI_ASSERT.  Use
                 * @ref deleteLater for the common cross-thread teardown
                 * pattern; for migrations of an entire subtree, call
                 * @ref moveToThread on the root.
                 */
                void setParent(ObjectBase *p);

                /**
                 * @brief Returns a list of children of this object
                 * @return List of children
                 */
                const ObjectBaseList &childList() const { return _childList; }

                /**
                 * @brief Registers a slot with this object and assigns it an ID.
                 * @tparam Args The slot's parameter types.
                 * @param slot Pointer to the Slot to register.
                 * @return The assigned slot ID.
                 */
                template <typename... Args> int registerSlot(Slot<Args...> *slot) {
                        int ret = _slotList.size();
                        slot->setID(ret);
                        _slotList += SlotItem(ret, slot->prototype());
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
                 * @brief Returns the @ref Thread this object is affiliated with.
                 *
                 * Captured at construction time from
                 * @ref Thread::currentThread.  May be @c nullptr if the
                 * object was created on a thread that has no
                 * @ref Thread wrapper (e.g. before
                 * @ref Application::Application has run, or in a bare
                 * @c std::thread that never adopted itself).
                 *
                 * @return The owning Thread, or nullptr.
                 */
                Thread *thread() const { return _thread; }

                /**
                 * @brief Changes the @ref Thread affinity of this object.
                 *
                 * Must be called from the object's current thread.  The
                 * object must have no parent.  Children are moved
                 * recursively so the whole subtree ends up on @p t and
                 * its @ref EventLoop.  Asserts on violation of either
                 * constraint.
                 *
                 * @param t The new Thread to affiliate with.
                 */
                void moveToThread(Thread *t);

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

                /**
                 * @brief Schedules @c delete this for the next iteration of
                 *        this object's @ref EventLoop.
                 *
                 * Idiomatic Qt-style cross-thread teardown.  Posts a
                 * callable to the object's @c eventLoop() that performs
                 * the actual @c delete, so the destructor always runs
                 * on the object's affinity thread regardless of where
                 * @c deleteLater is invoked from.  Safe to call from
                 * any thread.
                 *
                 * Typical use: a worker-thread object that needs to be
                 * torn down by the owning thread.  Calling
                 * @c worker->deleteLater() then @c thread->quit()
                 * works correctly because @ref EventLoop drains every
                 * remaining queued item — including any cleanup
                 * callables posted from inside the destructor — before
                 * exiting.
                 *
                 * Detaches the object from its parent before posting
                 * the delete-callable.  Ownership transfers from the
                 * parent-child chain to the queued callable the moment
                 * @c deleteLater is invoked, so a parent that dies
                 * between now and the dispatch cannot double-delete
                 * the child via @ref destroyChildren.
                 *
                 * Falls back to a synchronous @c delete @c this when
                 * the object has no associated EventLoop.  Always run
                 * the call as the @em last action that touches the
                 * object on the calling thread; nothing further may
                 * dereference @c this once the post lands on the
                 * target loop.
                 */
                void deleteLater();

                /** @brief Cleanup-handler signature: receives @c this on invocation. */
                using CleanupHandler = std::function<void(ObjectBase *)>;

                /**
                 * @brief Registers a function to run during this object's
                 *        destruction.
                 *
                 * Cleanup handlers fire from the destructor (after
                 * @ref aboutToDestroy emits, after children are
                 * destroyed) and receive @c this as their argument.
                 * Each handler is gated by an @ref ObjectBasePtr to
                 * @p target — if @p target has already been destroyed
                 * by the time the handler would run, the framework
                 * skips it.  Pass a non-null @p target when the
                 * handler dereferences a foreign object that may
                 * outlive @c this in the other direction; pass
                 * @c nullptr to register a handler that always runs.
                 *
                 * Used internally to wire automatic
                 * signal/slot disconnect on receiver destruction —
                 * see @ref Signal::connect(Function, ObjectBase *).
                 *
                 * @param target Foreign object the handler depends on
                 *               (may be @c nullptr).
                 * @param fn     Cleanup handler — must be non-empty.
                 */
                void registerCleanup(ObjectBase *target, CleanupHandler fn);

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
                using CleanupFunc = CleanupHandler;

                struct SlotItem {
                                int         id;
                                const char *prototype;
                };

                struct Cleanup {
                                ObjectBasePtr<> object;
                                CleanupFunc     func;
                };

                ObjectBase    *_parent = nullptr;
                ObjectBase    *_signalSender = nullptr;
                Thread        *_thread = nullptr;
                EventLoop     *_eventLoop = nullptr;
                ObjectBaseList _childList;
                List<SlotItem> _slotList;
                Map<std::atomic<ObjectBase *> *,
                    std::atomic<ObjectBase *> *>
                        _pointerMap; ///< Keys are &ObjectBasePtr::p; stored as a type-erased handle so runCleanup() can null every tracker without knowing its @c T.
                List<Cleanup> _cleanupList;

                // A single process-wide mutex serializes every
                // ObjectBasePtr link / unlink (linkTo, linkFromSource,
                // unlink) with ObjectBase::runCleanup.  A per-object
                // mutex is unsafe here because unlink reads the obj
                // pointer via the atomic and *then* needs to take a
                // lock to manipulate the map — if we put that lock on
                // the object itself, the object can be destroyed in
                // between, leaving us with a use-after-free on the
                // mutex member.  Serializing through one global mutex
                // closes that window: an unlink that observes obj
                // != nullptr is guaranteed that runCleanup hasn't run
                // yet (and won't until we release).  The link path
                // folds the source-pointer load and the @c _pointerMap
                // registration into the same critical section so a
                // newly constructed tracker is either fully registered
                // before runCleanup nulls it, or never observes a
                // doomed pointer at all.
                static Mutex &objectBasePtrMutex();

                void setOwnerThreadRecursive(Thread *t, EventLoop *loop);

                void addChild(ObjectBase *c) {
                        _childList += c;
                        return;
                }

                void removeChild(ObjectBase *c) {
                        _childList.removeFirst(c);
                        return;
                }

                void destroyChildren() {
                        for (auto child : _childList) {
                                child->_parent = nullptr;
                                delete child;
                        }
                        _childList.clear();
                        return;
                }

                void runCleanup() {
                        // Null out any ObjectBasePtr's that are currently pointing
                        // to this object.  Hold the global mutex so a concurrent
                        // ObjectBasePtr link / unlink on another thread can't
                        // observe a half-destroyed object.
                        {
                                Mutex::Locker lock(objectBasePtrMutex());
                                for (auto item : _pointerMap) {
                                        item.first->store(nullptr, std::memory_order_release);
                                }
                                _pointerMap.clear();
                        }

                        // Walk down the cleanup list and run any cleanup functions
                        for (auto &item : _cleanupList) {
                                if (!item.object.isValid()) continue;
                                item.func(this);
                        }
                        _cleanupList.clear();
                        return;
                }
};

template <typename T> inline void ObjectBasePtr<T>::linkTo(ObjectBase *obj) {
        // Single critical section: store @c p and add to the tracked
        // object's @c _pointerMap atomically.  An interleaved
        // runCleanup can either run entirely before us (it sees no
        // entry, then we observe the cleared @c _pointerMap and
        // implicitly do nothing meaningful — though @c obj is then a
        // dangling pointer, see note below) or entirely after us (it
        // observes the new entry and nulls @c p).  Either way the
        // invariant "non-null @c p implies registered in the tracked
        // object's @c _pointerMap" holds for the duration of the
        // tracker's life.
        //
        // Note on the dangling-input case: callers of this constructor
        // pass a raw pointer they own.  If the caller's object is
        // destroyed before @c linkTo runs, that's a caller-side
        // lifetime bug, not something this class can plug.  The
        // mutex-protected variant @ref linkFromSource exists for the
        // copy paths where the source is itself an ObjectBasePtr and
        // can be invalidated concurrently — that one re-checks the
        // atomic under the lock.
        Mutex::Locker lock(ObjectBase::objectBasePtrMutex());
        p.store(obj, std::memory_order_relaxed);
        if (obj != nullptr) {
                obj->_pointerMap[&p] = &p;
        }
        return;
}

template <typename T>
inline void ObjectBasePtr<T>::linkFromSource(const std::atomic<ObjectBase *> &source) {
        // Lock first, then load the source: if a concurrent
        // runCleanup is mid-flight on the tracked object it has
        // already nulled the source's atomic before releasing the
        // mutex, so once we hold the lock the source either still
        // points at a live object (which can't be destroyed until we
        // release) or has been nulled (we observe nullptr and link to
        // nothing).  Performing the source load *outside* the lock —
        // as the previous mem-initializer pattern did — leaves a
        // window where @c p is set to a doomed pointer, runCleanup
        // misses us because we're not in @c _pointerMap yet, and the
        // subsequent registration writes through a freed @c obj.
        Mutex::Locker lock(ObjectBase::objectBasePtrMutex());
        ObjectBase   *obj = source.load(std::memory_order_relaxed);
        p.store(obj, std::memory_order_relaxed);
        if (obj != nullptr) {
                obj->_pointerMap[&p] = &p;
        }
        return;
}

template <typename T> inline void ObjectBasePtr<T>::unlink() {
        // Same ordering as the link path: acquire the global mutex
        // first so we can't race ObjectBase::runCleanup, which holds
        // the same lock while it walks _pointerMap and frees the
        // trackers.
        Mutex::Locker lock(ObjectBase::objectBasePtrMutex());
        ObjectBase   *obj = p.exchange(nullptr, std::memory_order_acq_rel);
        if (obj != nullptr) {
                auto it = obj->_pointerMap.find(&p);
                if (it != obj->_pointerMap.end()) {
                        obj->_pointerMap.remove(it);
                }
        }
}

PROMEKI_NAMESPACE_END

// The template bodies for ObjectBase::connect and
// Signal<Args...>::connect(Function, ObjectBase *) previously lived at
// the bottom of this file but pulled @c variant.h and @c eventloop.h
// into every TU that includes @c objectbase.h — ~270 of them.  They
// now live in @c objectbase.tpp; TUs that actually call those methods
// must include @c promeki/objectbase.tpp.
