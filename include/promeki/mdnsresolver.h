/**
 * @file      mdnsresolver.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/mdnsserviceinstance.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/objectbase.h>
#include <promeki/timerevent.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class MdnsBrowser;
class MdnsManager;

/**
 * @brief One-shot resolution of a single DNS-SD service instance.
 * @ingroup network
 *
 * Where @ref MdnsBrowser surfaces every instance of a service type
 * as it comes and goes, @c MdnsResolver focuses on a single instance
 * known by name and reports once with the complete picture (SRV
 * target + port, TXT attributes, IPv4 / IPv6 addresses) or with an
 * error if the instance cannot be resolved within the configured
 * timeout.
 *
 * Internally the resolver constructs a private @ref MdnsBrowser for
 * the target's service type and watches for its @c serviceFound /
 * @c serviceUpdated signals, filtering to the requested instance
 * name.  The browser's existing follow-up-query infrastructure
 * (directed SRV / TXT / A / AAAA) does the legwork — the resolver
 * just decides when the picture is complete enough to surface.
 *
 * @par Threading model
 *
 * Resolver lifecycle (@ref resolve / @ref stop) is intended to be
 * driven from the constructing thread.  Signals fire on the
 * connecting object's affinity loop per the project's standard
 * cross-thread marshalling.
 *
 * @par Example
 * @code
 * MdnsServiceInstance target;
 * target.setInstanceName("Studio Camera 2");
 * target.setType(MdnsServiceType("rtsp", MdnsServiceType::Protocol::Tcp));
 *
 * MdnsResolver r(target);
 * r.resolvedSignal.connect([](MdnsServiceInstance inst) {
 *     promekiInfo("resolved %s -> %s:%u",
 *                 inst.instanceName().cstr(),
 *                 inst.hostname().cstr(),
 *                 inst.port());
 * }, ctx);
 * r.failedSignal.connect([](Error e) { promekiWarn("resolve failed: %s", e.name().cstr()); }, ctx);
 * r.setTimeout(Duration::fromSeconds(3));
 * r.resolve();
 * @endcode
 */
class MdnsResolver : public ObjectBase {
                PROMEKI_OBJECT(MdnsResolver, ObjectBase)
        public:
                /** @brief Default overall resolution timeout in ms. */
                static constexpr int64_t DefaultTimeoutMs = 3000;

                /**
                 * @brief Constructs a resolver for the given target instance.
                 *
                 * @p target must have at least @ref MdnsServiceInstance::type
                 * and @ref MdnsServiceInstance::instanceName set;
                 * other fields are ignored at construction and are
                 * filled in by the resolution.
                 */
                explicit MdnsResolver(const MdnsServiceInstance &target, ObjectBase *parent = nullptr);

                /** @brief Destructor.  Calls @ref stop if active. */
                ~MdnsResolver() override;

                /** @brief Returns the configured target. */
                const MdnsServiceInstance &target() const { return _target; }

                /**
                 * @brief Attaches to (or detaches from) a manager.
                 *
                 * Wraps the internal @ref MdnsBrowser's @ref MdnsBrowser::setManager
                 * call so resolvers share the standard manager-binding
                 * semantics with browsers.
                 */
                void setManager(MdnsManager *manager);

                /** @brief Returns the explicitly attached manager, or @c nullptr. */
                MdnsManager *manager() const;

                /** @brief Sets the overall resolution timeout. */
                void setTimeout(const Duration &timeout);

                /** @brief Returns the configured overall resolution timeout. */
                const Duration &timeout() const { return _timeout; }

                /**
                 * @brief Begins resolution.
                 *
                 * Starts the internal browser (which fires the initial
                 * PTR query and registers continuous-query backoff).
                 * Re-arming @ref resolve on an already-active resolver
                 * is a no-op.
                 *
                 * @return @ref Error::Ok on success; @ref Error::NotReady
                 *         when no manager is reachable.
                 */
                Error resolve();

                /**
                 * @brief Cancels an in-flight resolution.
                 *
                 * Idempotent.  Does not emit @ref failedSignal —
                 * cancellation is a caller-driven action, not a
                 * resolution failure.
                 */
                void stop();

                /** @brief Returns @c true between successful @ref resolve and completion or @ref stop. */
                bool isActive() const { return _active.value(); }

                /** @brief Returns the snapshot accumulated so far (may be incomplete). */
                MdnsServiceInstance snapshot() const;

                /** @brief Emitted once when the instance is fully resolved.  @signal */
                PROMEKI_SIGNAL(resolved, MdnsServiceInstance);

                /** @brief Emitted once on timeout / unrecoverable error.  @signal */
                PROMEKI_SIGNAL(failed,   Error);

        protected:
                /** @brief Drives the single-shot timeout. */
                void timerEvent(TimerEvent *e) override;

        private:
                // Called by the internal browser's serviceFound /
                // serviceUpdated slots.
                void onBrowserFound(MdnsServiceInstance inst);
                void onBrowserUpdated(MdnsServiceInstance inst);
                void onBrowserLost(MdnsServiceInstance inst);

                // Returns @c true when the in-progress snapshot is
                // "complete enough" to fire @ref resolvedSignal.  The
                // criterion is: port + hostname known AND at least one
                // address (v4 or v6) recorded.
                static bool isComplete(const MdnsServiceInstance &inst);

                MdnsServiceInstance       _target;
                UniquePtr<MdnsBrowser>    _browser;
                MdnsServiceInstance       _snapshot;
                Duration                  _timeout;
                int                       _timerId = -1;
                Atomic<bool>              _active;
                Atomic<bool>              _emitted;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
