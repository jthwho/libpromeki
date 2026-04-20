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

        // Register a cleanup
        ObjectBase *signalObject = static_cast<ObjectBase *>(signal->owner());
        ObjectBase *slotObject = static_cast<ObjectBase *>(slot->owner());
        slotObject->_cleanupList += Cleanup(
                signalObject,
                [signal](ObjectBase *ptr){ signal->disconnectFromObject(ptr); }
        );
}

// Context-aware Signal::connect overload.  Defined here because it
// needs ObjectBase and EventLoop to be complete — signal.h forward
// declares ObjectBase and only stores the declaration.  The wrapping
// bridge lambda captures @p owner and uses its @c eventLoop() at
// emit time, matching Qt's @c Qt::AutoConnection: direct invocation
// on the owner's own thread, @c postCallable marshalling otherwise.
template <typename... Args>
size_t Signal<Args...>::connect(Function slot, ObjectBase *owner) {
        // Passing a null @ref ObjectBase context is a programmer error
        // — the whole point of this overload is to anchor dispatch to
        // the owner's EventLoop, and there is no loop to anchor to
        // when @p owner is null.  Callers that explicitly want the
        // raw, same-thread-only behaviour should use the @c void*
        // overload with @c nullptr instead.
        PROMEKI_ASSERT(owner != nullptr);
        return connect(
                [slot = std::move(slot), owner](Args... args) {
                        EventLoop *ownerLoop   = owner->eventLoop();
                        EventLoop *currentLoop = EventLoop::current();
                        if(ownerLoop == nullptr || ownerLoop == currentLoop) {
                                slot(args...);
                                return;
                        }
                        // Copy args into a tuple, hop to the owner's
                        // EventLoop, then unpack and invoke.  Requires
                        // every Args type to be copy-constructible,
                        // which is the same restriction the Slot-based
                        // ObjectBase::connect path already carries via
                        // VariantList packing.
                        ownerLoop->postCallable(
                                [slot, tup = std::make_tuple(std::move(args)...)]() {
                                        std::apply(slot, tup);
                                });
                },
                static_cast<void *>(owner));
}

PROMEKI_NAMESPACE_END
