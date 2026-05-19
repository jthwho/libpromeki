/**
 * @file      event.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>

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

                /**
                 * @brief Allocates a unique event type ID and records a human-readable name for it.
                 *
                 * Equivalent to @ref registerType() except the supplied
                 * @p name is stored in an internal registry keyed by the
                 * returned Type value, so @ref typeName can look it up
                 * later.  Built-in event types are registered with names
                 * by the library.  User-defined types should prefer this
                 * overload over the parameterless one so diagnostic
                 * output (e.g. the per-loop event-stat formatter)
                 * surfaces the type name instead of a bare integer.
                 *
                 * Thread-safe.  Names are not required to be unique —
                 * the registry is purely for human-readable lookup —
                 * but reusing a name across distinct registrations is
                 * confusing and discouraged.
                 *
                 * @param name Human-readable name for the new type.
                 * @return A unique Type value.
                 */
                static Type registerType(const String &name);

                /**
                 * @brief Returns the name registered for an event type ID.
                 *
                 * Returns the name supplied to @ref registerType when
                 * the type was created.  Types created via the
                 * parameterless @ref registerType() overload, and
                 * unknown / @ref InvalidType ids, return an empty
                 * @ref String.  Thread-safe; the registry is locked
                 * for the duration of the lookup.
                 *
                 * @param type The event type ID to look up.
                 * @return The registered name, or an empty String.
                 */
                static String typeName(Type type);

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

#endif // PROMEKI_ENABLE_CORE
