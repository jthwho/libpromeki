/**
 * @file      objectbase.tpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Out-of-line template definitions for the Variant-marshalling signal
 * dispatch machinery on @ref promeki::ObjectBase and the context-aware
 * overload of @ref promeki::Signal::connect.  These bodies pull in
 * @c variant.h and @c eventloop.h, so they are kept out of
 * @c objectbase.h proper: that header is included by ~270 TUs and
 * those bodies are only instantiated in a handful.
 *
 * Include this file from any TU that calls
 * @c ObjectBase::connect(Signal*, Slot*) or
 * @c Signal<Args...>::connect(Function, ObjectBase *).
 *
 * @par Cross-thread receiver liveness
 *
 * Both connect overloads automatically tear down their wiring when
 * the receiving @ref ObjectBase is destroyed and gracefully drop any
 * cross-thread callable that was already in flight when the receiver
 * died:
 *
 *  -# At connect time the receiver's @c _cleanupList gets a
 *     @ref ObjectBase::registerCleanup entry (gated by an
 *     @ref ObjectBasePtr to the signal owner) that calls
 *     @c Signal::disconnectFromObject — so once @c ~ObjectBase starts
 *     on the receiver no NEW slot invocations can be posted.
 *  -# Inside the bridge lambda the captured receiver is held via an
 *     @ref ObjectBasePtr, not a raw pointer.  Both the same-thread
 *     fast path and the @c postCallable inner lambda check
 *     @c isValid() before invoking the user slot, so a callable that
 *     was queued before the receiver died and dispatched after is a
 *     no-op instead of a UAF.
 *
 * The framework therefore guarantees that the user slot is not called
 * once the receiver has finished destruction.  That covers the
 * "captured @c this" case (which is by far the most common); user
 * slots that capture additional raw pointers must still ensure those
 * remain valid for the slot's lifetime.
 */

#pragma once

#include <promeki/objectbase.h>
#include <promeki/eventloop.h>
#include <promeki/signal.tpp>
#include <promeki/slot.tpp>

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

        // Auto-disconnect on slot-owner destruction.  The cleanup is
        // gated by an ObjectBasePtr to @p signal->owner() so that a
        // signal-owner-dies-first scenario doesn't dereference into a
        // freed Signal.
        ObjectBase *signalObject = static_cast<ObjectBase *>(signal->owner());
        ObjectBase *slotObject = static_cast<ObjectBase *>(slot->owner());
        slotObject->registerCleanup(
                signalObject,
                [signal](ObjectBase *ptr) { signal->disconnectFromObject(ptr); }
        );
}

// Context-aware Signal::connect overload.  Defined here because it
// needs ObjectBase and EventLoop to be complete — signal.h forward
// declares ObjectBase and only stores the declaration.  The wrapping
// bridge lambda holds an @ref ObjectBasePtr to @p owner so the
// dispatch path can short-circuit if the receiver has been destroyed
// since the call was queued.  Same-thread emits route through the
// fast path; cross-thread emits go through the owner's
// EventLoop::postCallable, matching Qt's @c Qt::AutoConnection.
template <typename... Args>
size_t Signal<Args...>::connect(Function slot, ObjectBase *owner) {
        // Passing a null @ref ObjectBase context is a programmer error
        // — the whole point of this overload is to anchor dispatch to
        // the owner's EventLoop, and there is no loop to anchor to
        // when @p owner is null.  Callers that explicitly want the
        // raw, same-thread-only behaviour should use the @c void*
        // overload with @c nullptr instead.
        PROMEKI_ASSERT(owner != nullptr);

        const size_t id = connect(
                [slot = std::move(slot), ownerPtr = ObjectBasePtr<>(owner)](Args... args) {
                        // Liveness gate #1: catch the rare same-thread
                        // case where the receiver has already started
                        // destruction before the emit reaches us.
                        const ObjectBase *liveOwner = ownerPtr.data();
                        if(liveOwner == nullptr) return;

                        EventLoop *ownerLoop   = liveOwner->eventLoop();
                        EventLoop *currentLoop = EventLoop::current();
                        if(ownerLoop == nullptr || ownerLoop == currentLoop) {
                                slot(args...);
                                return;
                        }
                        // Cross-thread: copy args into a tuple, hop to
                        // the owner's EventLoop, then re-check
                        // liveness inside the post-callable so a
                        // callable that was queued just before the
                        // receiver died doesn't fire on dead memory.
                        ownerLoop->postCallable(
                                [slot, ownerPtr, tup = std::make_tuple(std::move(args)...)]() {
                                        if(!ownerPtr.isValid()) return;
                                        std::apply(slot, tup);
                                });
                },
                static_cast<void *>(owner));

        // Auto-disconnect on receiver destruction.  Gated on the
        // signal owner so a signal-owner-dies-first race skips the
        // disconnect (the Signal is gone by then anyway and the slot
        // list with it).  When the signal has no ObjectBase owner
        // (raw Signal not embedded in a PROMEKI_SIGNAL macro) we
        // skip the auto-disconnect — the void* overload's same-
        // thread-only contract still applies.
        ObjectBase *signalOwner = static_cast<ObjectBase *>(this->owner());
        if(signalOwner != nullptr) {
                owner->registerCleanup(signalOwner, [this, id](ObjectBase *) { this->disconnect(id); });
        }
        return id;
}

PROMEKI_NAMESPACE_END
