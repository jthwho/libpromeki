/**
 * @file      objectbase.cpp
 * @copyright Jason Howard. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/objectbase.h>
#include <promeki/eventloop.h>
#include <promeki/thread.h>
#include <promeki/timerevent.h>
#include <promeki/system.h>

PROMEKI_NAMESPACE_BEGIN

Mutex &ObjectBase::objectBasePtrMutex() {
        // Process-wide mutex serializing every ObjectBasePtr link/unlink
        // and ObjectBase::runCleanup.  See the declaration in objectbase.h
        // for why a per-object mutex would race with destruction.
        static Mutex m;
        return m;
}

ObjectBase::ObjectBase(ObjectBase *p) : _parent(p) {
        if (_parent != nullptr) _parent->addChild(this);
        // Affinity is the Thread we were constructed on.  The
        // dispatch EventLoop is derived from that Thread via
        // @ref eventLoop, so there is no separately-tracked loop
        // pointer to fall out of sync.  @ref Thread::currentThread
        // is stable for the entire lifetime of the thread (set in
        // @ref Thread::adoptCurrentThread and at the top of
        // @ref Thread::run) whereas the prior
        // @c EventLoop::current capture was valid only inside
        // @c exec — that asymmetry was the bug surface we deleted.
        _thread = Thread::currentThread();
        return;
}

EventLoop *ObjectBase::eventLoop() const {
        // Out-of-line so callers don't need @c Thread complete.
        return _thread != nullptr ? _thread->threadEventLoop() : nullptr;
}

void ObjectBase::registerCleanup(ObjectBase *target, CleanupHandler fn) {
        if (!fn) return;
        _cleanupList += Cleanup{ObjectBasePtr<>(target), std::move(fn)};
}

void ObjectBase::setParent(ObjectBase *p) {
        // Thread-affinity invariant: a parent and its children must
        // share the same owning @ref Thread.  Cross-thread parenting
        // would require locking @c _parent / @c _childList against
        // concurrent mutation; we instead require the user to keep
        // the relationship single-threaded and use @ref deleteLater
        // for cross-thread teardown patterns.
        if (p != nullptr) {
                PROMEKI_ASSERT(_thread == p->_thread);
        }
        if (_parent != nullptr) _parent->removeChild(this);
        _parent = p;
        if (_parent != nullptr) _parent->addChild(this);
        return;
}

void ObjectBase::deleteLater() {
        EventLoop *loop = eventLoop();
        if (loop == nullptr) {
                // Almost always a user error: the object was
                // constructed on a thread that has no @ref Thread
                // wrapper, so there is no loop to post the delete
                // to.  Fall back to a synchronous delete (the only
                // sane option) but flag it loudly.
                promekiWarn("ObjectBase::deleteLater on object @ %p with no Thread affinity — "
                            "falling back to immediate delete on the calling thread.  "
                            "Adopt the constructing thread via Thread::adoptCurrentThread() "
                            "or moveToThread() before deleteLater.",
                            static_cast<const void *>(this));
                delete this;
                return;
        }
        // Post the parent-detach AND the delete to the owner's
        // EventLoop so both run on the owner's thread — required
        // because @c setParent now asserts thread affinity.  Capture
        // an @ref ObjectBasePtr so a parent that destructs first
        // (running its @c destroyChildren on the same owner thread,
        // before our callable lands) can null us out and the
        // callable becomes a no-op rather than a use-after-free.
        ObjectBasePtr<> selfPtr(this);
        static const auto kDeleteLaterLabel = EventLoop::Label{"ObjectBase.deleteLater"};
        loop->postCallable(kDeleteLaterLabel, [selfPtr]() mutable {
                ObjectBase *self = selfPtr.data();
                if (self == nullptr) return;
                self->setParent(nullptr);
                delete self;
        });
}

void ObjectBase::moveToThread(Thread *t) {
        PROMEKI_ASSERT(_parent == nullptr);
        PROMEKI_ASSERT(_thread == nullptr || _thread == Thread::currentThread());
        setOwnerThreadRecursive(t);
        return;
}

void ObjectBase::setOwnerThreadRecursive(Thread *t) {
        // EventLoop is derived from @c _thread, so moving the
        // affinity transitively moves the dispatch loop — no
        // separate field to keep in sync.
        _thread = t;
        for (auto child : _childList) {
                child->setOwnerThreadRecursive(t);
        }
        return;
}

int ObjectBase::startTimer(unsigned int intervalMs, bool singleShot) {
        EventLoop *loop = eventLoop();
        if (loop == nullptr) {
                promekiWarn("ObjectBase::startTimer(%ums%s) on object @ %p with no Thread affinity — "
                            "no timer started.  Construct the object on a Thread or call "
                            "moveToThread() before starting timers.",
                            intervalMs,
                            singleShot ? ", singleShot" : "",
                            static_cast<const void *>(this));
                return -1;
        }
        return loop->startTimer(this, intervalMs, singleShot);
}

void ObjectBase::stopTimer(int timerId) {
        EventLoop *loop = eventLoop();
        if (loop == nullptr) {
                promekiWarn("ObjectBase::stopTimer(%d) on object @ %p with no Thread affinity — no-op.",
                            timerId,
                            static_cast<const void *>(this));
                return;
        }
        loop->stopTimer(timerId);
        return;
}

void ObjectBase::event(Event *e) {
        if (e->type() == Event::Timer) {
                timerEvent(static_cast<TimerEvent *>(e));
                e->accept();
        }
        return;
}

void ObjectBase::timerEvent(TimerEvent *) {
        return;
}

const char *ObjectBase::MetaInfo::name() const {
        if (_demangledName.isEmpty()) {
                _demangledName = System::demangleSymbol(_name);
        }
        return _demangledName.cstr();
}

void ObjectBase::MetaInfo::dumpToLog() const {
        String pad;
        for (const MetaInfo *info = this; info != nullptr; info = info->parent()) {
                promekiInfo("%s%s", pad.cstr(), info->name());
                for (size_t i = 0; i < info->signalList().size(); ++i) {
                        const SignalMeta *signal = info->signalList()[i];
                        promekiInfo("%s  SIGNAL %d: %s", pad.cstr(), (int)i, signal->name());
                }
                for (size_t i = 0; i < info->slotList().size(); ++i) {
                        const SlotMeta *slot = info->slotList()[i];
                        promekiInfo("%s  SLOT   %d: %s", pad.cstr(), (int)i, slot->name());
                }
                pad += "  ";
        }
        return;
}

PROMEKI_NAMESPACE_END
