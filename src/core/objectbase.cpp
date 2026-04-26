/**
 * @file      objectbase.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/objectbase.h>
#include <promeki/eventloop.h>
#include <promeki/timerevent.h>
#include <promeki/system.h>

PROMEKI_NAMESPACE_BEGIN

ObjectBase::ObjectBase(ObjectBase *p) : _parent(p) {
        if (_parent != nullptr) _parent->addChild(this);
        _eventLoop = EventLoop::current();
        return;
}

void ObjectBase::moveToThread(EventLoop *loop) {
        PROMEKI_ASSERT(_parent == nullptr);
        PROMEKI_ASSERT(_eventLoop == nullptr || _eventLoop == EventLoop::current());
        setEventLoopRecursive(loop);
        return;
}

void ObjectBase::setEventLoopRecursive(EventLoop *loop) {
        _eventLoop = loop;
        for (auto child : _childList) {
                child->setEventLoopRecursive(loop);
        }
        return;
}

int ObjectBase::startTimer(unsigned int intervalMs, bool singleShot) {
        if (_eventLoop == nullptr) return -1;
        return _eventLoop->startTimer(this, intervalMs, singleShot);
}

void ObjectBase::stopTimer(int timerId) {
        if (_eventLoop == nullptr) return;
        _eventLoop->stopTimer(timerId);
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
