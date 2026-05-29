/**
 * @file      mdnspublisher.h
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
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mdnsrecord.h>
#include <promeki/mdnsserviceinstance.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/mutex.h>
#include <promeki/networkinterface.h>
#include <promeki/objectbase.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class MdnsManager;

/**
 * @brief Publishes a single DNS-SD service instance to the link.
 * @ingroup network
 *
 * Drives the RFC 6762 publisher state machine for one service
 * instance: @b probe → @b announce → @b respond → @b goodbye.  One
 * @c MdnsPublisher publishes one instance; multi-instance publishing
 * uses multiple publisher objects sharing the same @ref MdnsManager.
 *
 * @par State machine
 *
 *  - @ref State::Idle — initial / withdrawn.
 *  - @ref State::Probing — after @ref publish, sending the RFC 6762
 *    §8.1 sequence of three probe queries spaced @ref ProbeIntervalMs
 *    apart.  Each probe carries @c QTYPE=ANY on the instance FQDN
 *    plus the tentative @c SRV / @c TXT / @c A / @c AAAA records in
 *    the Authority section.  Any conflicting response observed
 *    during this window aborts the probe and emits @ref conflictSignal.
 *  - @ref State::Announcing — probes cleared; sending the two
 *    announcements spaced @ref AnnounceIntervalMs apart.  Each
 *    announcement carries the full @c PTR + @c SRV + @c TXT +
 *    @c A / @c AAAA set with cache-flush bits set on the unique
 *    types.
 *  - @ref State::Published — the publisher is operational and
 *    responds to inbound queries that match its records.
 *  - @ref State::Withdrawing — transient state during @ref withdraw,
 *    while the goodbye packet is being emitted.
 *
 * @par Threading model
 *
 * The publisher is driven by the @ref MdnsManager worker thread via
 * @ref handlePacket (inbound query / response inspection) and
 * @ref onManagerTick (probe / announce schedule).  Public
 * configuration setters and @ref publish / @ref withdraw are
 * intended to be called from the constructing thread.  Signal slots
 * are dispatched through the connecting object's @ref EventLoop per
 * the project's standard cross-thread marshalling.
 *
 * @par Example
 * @code
 * MdnsServiceInstance inst;
 * inst.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
 * inst.setInstanceName("Studio Camera");
 * inst.setHostname("camera.local.");
 * inst.setPort(8080);
 * inst.setIpv4Addresses({ Ipv4Address(192, 168, 1, 7) });
 *
 * MdnsPublisher p;
 * p.setInstance(inst);
 * p.announcedSignal.connect([](MdnsServiceInstance i) {
 *     promekiInfo("announced: %s", i.fqdn().cstr());
 * }, ctx);
 * p.publish();
 * @endcode
 *
 * @par Conflict handling
 *
 * On a probe-window conflict the publisher follows RFC 6762 §9 by
 * default: it appends a numeric suffix (@c "Studio Camera (2)" →
 * @c "(3)" → …) and re-enters the probe phase, capped by
 * @ref MaxRenameAttempts.  The @ref renamed signal fires on every
 * mutation; @ref announced carries the final accepted name.  Callers
 * that want manual control can disable the behaviour via
 * @ref setAutoRename — the conflict then routes to @ref State::Conflicted
 * and @ref conflict fires, as before.  After @ref MaxRenameAttempts
 * consecutive conflicts auto-rename gives up and behaves the same as
 * a manual-mode conflict.
 *
 * @par Thread Safety
 * Construction and @ref setManager are intended to happen on the
 * owning thread.  @ref handlePacket / @ref onManagerTick are safe
 * to call concurrently — internal state is mutex-protected.
 */
class MdnsPublisher : public ObjectBase {
                PROMEKI_OBJECT(MdnsPublisher, ObjectBase)
        public:
                /** @brief Publisher lifecycle states. */
                enum class State : uint8_t {
                        Idle         = 0,
                        Probing      = 1,
                        Announcing   = 2,
                        Published    = 3,
                        Conflicted   = 4,
                        Withdrawing  = 5,
                };

                /** @brief Probe spacing in ms (RFC 6762 §8.1 — 250 ms). */
                static constexpr int64_t ProbeIntervalMs = 250;

                /** @brief Probe count (RFC 6762 §8.1 — three probes). */
                static constexpr int ProbeCount = 3;

                /** @brief Announce spacing in ms (RFC 6762 §8.3 — 1 s minimum). */
                static constexpr int64_t AnnounceIntervalMs = 1000;

                /** @brief Announce count (RFC 6762 §8.3 — two announcements). */
                static constexpr int AnnounceCount = 2;

                /**
                 * @brief Hard cap on consecutive auto-rename retries.
                 *
                 * After this many probe-window conflicts in a row the
                 * publisher gives up auto-rename and transitions to
                 * @ref State::Conflicted, mirroring the manual-mode
                 * behaviour.  RFC 6762 §9 does not pin a specific
                 * cap; eight is a generous-but-finite bound that
                 * matches Avahi's default.
                 */
                static constexpr int MaxRenameAttempts = 8;

                /** @brief Constructs an idle publisher. */
                explicit MdnsPublisher(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Calls @ref withdraw if @ref isActive. */
                ~MdnsPublisher() override;

                /**
                 * @brief Sets the instance to publish.
                 *
                 * Must be called before @ref publish.  Changing the
                 * instance on a live publisher requires @ref withdraw
                 * first; this is rejected by @ref publish.
                 */
                void setInstance(const MdnsServiceInstance &instance);

                /** @brief Returns the configured instance. */
                const MdnsServiceInstance &instance() const { return _instance; }

                /**
                 * @brief Sets whether the publisher auto-renames on conflict.
                 *
                 * Defaults to @c true (RFC 6762 §9 behaviour).  When
                 * @c false a probe-window conflict transitions the
                 * publisher to @ref State::Conflicted and emits
                 * @ref conflict; callers handle the rename + retry
                 * themselves.  Safe to call before or after @ref publish.
                 */
                void setAutoRename(bool enable) { _autoRename = enable; }

                /** @brief Returns whether auto-rename is enabled. */
                bool autoRename() const { return _autoRename; }

                /**
                 * @brief Attaches the publisher to (or detaches from) a manager.
                 *
                 * Re-pointing to a different manager unregisters from
                 * the previous one first then registers.  Passing
                 * @c nullptr re-engages the implicit fallback to
                 * @ref Application::mdnsManager.
                 */
                void setManager(MdnsManager *manager);

                /** @brief Returns the explicitly attached manager, or @c nullptr. */
                MdnsManager *manager() const { return _manager; }

                /** @brief Returns the manager actually used (explicit or application fallback). */
                MdnsManager *effectiveManager() const;

                /** @brief Returns the current state. */
                State state() const { return _state.value(); }

                /** @brief Returns @c true between successful @ref publish and @ref withdraw. */
                bool isActive() const { return state() != State::Idle && state() != State::Conflicted; }

                /**
                 * @brief Begins publishing.
                 *
                 * Computes the record set from @ref instance, transitions
                 * to @ref State::Probing, and arms the probe timer.
                 *
                 * @return @ref Error::Ok on success.
                 *         @ref Error::Invalid when the instance has no
                 *         type, no instance name, no hostname, or no
                 *         addresses.
                 *         @ref Error::NotReady when no manager is
                 *         reachable.
                 *         @ref Error::Busy when @ref isActive returns
                 *         @c true.
                 */
                Error publish();

                /**
                 * @brief Withdraws the published instance.
                 *
                 * Emits the RFC 6762 §10.1 Goodbye packet (TTL=0 on
                 * every record) before returning to @ref State::Idle.
                 * Idempotent — a withdraw on an Idle publisher is a
                 * no-op.
                 */
                void withdraw();

                /**
                 * @brief The record set the publisher owns once it
                 *        reaches @ref State::Published.
                 *
                 * Empty in @ref State::Idle and during probe; the
                 * announce machinery composes the set from
                 * @ref instance and freezes it before sending the
                 * first announcement.
                 */
                List<MdnsRecord> records() const;

                /**
                 * @brief Per-packet entry point called by the manager.
                 *
                 * Drives two paths:
                 *  - During @ref State::Probing the inbound packet is
                 *    scanned for records that conflict with our
                 *    tentative set; a conflict aborts the publish.
                 *  - In @ref State::Published the inbound packet is
                 *    scanned for queries that match our records and
                 *    a response is emitted on the manager's send
                 *    path.
                 */
                void handlePacket(const Buffer &data, const SocketAddress &sender,
                                  const NetworkInterface &iface);

                /**
                 * @brief Called by the manager at each housekeeping tick.
                 *
                 * Advances the probe / announce schedule.
                 */
                void onManagerTick(const TimeStamp &now);

                /** @brief Emitted once the first announcement has been sent. @signal */
                PROMEKI_SIGNAL(announced, MdnsServiceInstance);
                /**
                 * @brief Emitted when a conflict aborts the publish.
                 *
                 * Fires only in manual-rename mode (after
                 * @ref setAutoRename(false)) or after
                 * @ref MaxRenameAttempts has been exhausted in
                 * auto-rename mode.  Successful auto-renames emit
                 * @ref renamed instead and stay active.
                 * @signal
                 */
                PROMEKI_SIGNAL(conflict,  MdnsServiceInstance);
                /**
                 * @brief Emitted on every auto-rename mutation.
                 *
                 * The first argument is the previous instance (with
                 * the old name); the second is the new one (with the
                 * mutated label and recomposed FQDN).  Fires from
                 * inside the probe-window conflict path before the
                 * re-probe is sent.  In manual-rename mode this
                 * signal never fires.
                 * @signal
                 */
                PROMEKI_SIGNAL(renamed,   MdnsServiceInstance, MdnsServiceInstance);
                /** @brief Emitted after the Goodbye has been sent. @signal */
                PROMEKI_SIGNAL(withdrawn, MdnsServiceInstance);

                /**
                 * @brief RFC 6762 §7.1 known-answer suppression.
                 *
                 * Returns a copy of @p answers with every record
                 * dropped that already appears in @p inboundQuery's
                 * Answer section AND whose requester-side TTL is at
                 * least half of ours.  Records whose requester-side
                 * TTL has aged below the half-life threshold are NOT
                 * suppressed — the requester needs the refresh.
                 *
                 * The function is pure: it does not touch publisher
                 * state, does not allocate beyond the result, and is
                 * safe to call from any thread.  Exposed @c static
                 * so unit tests can pin the suppression contract
                 * without standing up a full @ref MdnsManager.
                 *
                 * @param answers      Candidate response records.
                 * @param inboundQuery Raw wire bytes of the query
                 *                     whose KAS list is being honoured.
                 * @return The suppressed-down record list.
                 */
                static List<MdnsRecord> filterByKnownAnswers(const List<MdnsRecord> &answers,
                                                             const Buffer &inboundQuery);

        private:
                // Builds the full record set from @ref _instance.
                static List<MdnsRecord> composeRecords(const MdnsServiceInstance &i);

                // Transitions the state machine and fires any associated
                // signals.  Called from publish / onManagerTick /
                // withdraw.
                void enterState(State s);
                void advance(const TimeStamp &now);

                // Examines inbound records for conflicts with our
                // tentative record set; returns @c true if a conflict
                // was found.
                bool detectConflict(const Buffer &data);

                // Examines inbound questions for matches against our
                // record set and dispatches a response packet via the
                // manager.
                void respondToQueries(const Buffer &data);

                // RFC 6762 §9 instance-name mutation: parse any
                // trailing " (N)" suffix and bump it; otherwise
                // append " (2)".  Returns the new instance with the
                // mutated label and recomposed FQDN.
                static MdnsServiceInstance bumpInstanceName(const MdnsServiceInstance &in);

                MdnsServiceInstance      _instance;
                MdnsManager             *_manager = nullptr;
                mutable Mutex            _stateMtx;
                List<MdnsRecord>         _records;

                Atomic<State>            _state;
                Atomic<int>              _probeIdx;       ///< Number of probes already sent.
                Atomic<int>              _announceIdx;    ///< Number of announcements already sent.
                Atomic<int>              _renameAttempts; ///< Auto-renames performed since last successful publish.
                bool                     _autoRename = true;

                // Wall-clock for the next probe / announce / withdraw
                // step.  Owned by @ref _stateMtx.
                TimeStamp                _nextActionAt;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
