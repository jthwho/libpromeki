/**
 * @file      event.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Base class for the event system.
 * @ingroup events
 *
 * Events carry a type identifier and an accepted/ignored state.  Type IDs
 * are allocated at runtime by registerType(), which uses a lock-free atomic
 * counter so that registration is thread-safe and zero-overhead at dispatch
 * time (integer comparison).
 *
 * Built-in event types (Timer, DeferredCall, Signal, Quit) are registered
 * as file-scope statics in event.cpp.  User-defined types should be
 * registered with Event::registerType().
 *
 * @par Thread Safety
 * @c Event::registerType is fully thread-safe (atomic counter).
 * A single Event instance is intended to be posted to one EventLoop
 * and consumed there; once posted, ownership transfers to the loop
 * and the producer must not touch it.  @c accept / @c ignore /
 * @c isAccepted are intended for use only on the dispatching
 * thread inside an event handler.
 */
class Event {
        public:
                /** @brief Integer type used to identify event kinds. */
                using Type = uint32_t;

                /** @brief Sentinel value representing an invalid or unset event type. */
                static constexpr Type InvalidType = 0;

                /**
                 * @brief Allocates and returns a unique event type ID.
                 *
                 * Each call returns a new, never-before-used ID.  Thread-safe.
                 *
                 * @return A unique Type value.
                 */
                static Type registerType();

                /** @brief Event type for TimerEvent. */
                static const Type Timer;

                /** @brief Event type for deferred callable delivery. */
                static const Type DeferredCall;

                /** @brief Event type for cross-thread signal dispatch. */
                static const Type SignalEvent;

                /** @brief Event type requesting an EventLoop to quit. */
                static const Type Quit;

                /**
                 * @brief Constructs an Event with the given type.
                 * @param type The event type identifier.
                 */
                Event(Type type) : _type(type) {}

                /** @brief Virtual destructor. */
                virtual ~Event() = default;

                /**
                 * @brief Returns the type identifier for this event.
                 * @return The event type.
                 */
                Type type() const { return _type; }

                /**
                 * @brief Returns whether this event has been accepted.
                 * @return @c true if accepted, @c false if ignored.
                 */
                bool isAccepted() const { return _accepted; }

                /**
                 * @brief Marks the event as accepted.
                 *
                 * An accepted event will not be propagated further by the
                 * event delivery machinery.
                 */
                void accept() { _accepted = true; }

                /**
                 * @brief Marks the event as ignored (not accepted).
                 *
                 * An ignored event may be propagated to a parent handler.
                 */
                void ignore() { _accepted = false; }

        private:
                Type _type;
                bool _accepted = false;
};

PROMEKI_NAMESPACE_END
