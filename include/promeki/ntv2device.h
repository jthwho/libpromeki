/**
 * @file      ntv2device.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/atomic.h>
#include <promeki/clock.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/ntv2capabilities.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/videoreferenceconfig.h>

class CNTV2Card;

PROMEKI_NAMESPACE_BEGIN

class Ntv2DeviceRegistry;
class Ntv2MediaIO;

/**
 * @brief Process-wide owner of one AJA card and the resource
 *        reservations across its logical channels.
 * @ingroup proav
 *
 * Lifecycle is reference-counted by @ref Ntv2DeviceRegistry: the
 * first @ref Ntv2DeviceRegistry::acquire call for a given device
 * constructs the @ref Ntv2Device (opens @c CNTV2Card, acquires the
 * stream for the application, switches to OEM tasks); the last
 * @ref Ntv2DeviceRegistry::release call tears it down.  Multiple
 * @ref Ntv2MediaIO instances opening different logical channels on
 * the same card share one @ref Ntv2Device.
 *
 * @par Resource reservations
 *
 * The device tracks three categories of per-channel reservations so
 * concurrent @ref Ntv2MediaIO instances don't step on each other:
 *
 *  - **Framebuffer channels** — the logical AutoCirculate slot a
 *    MediaIO owns.  One MediaIO per channel.
 *  - **Physical ports** — the SDI / HDMI connectors a channel uses.
 *    A quad-link channel reserves four SDI ports; a single-link
 *    channel reserves one.  No connector may belong to two channels.
 *  - **Audio systems** — one independent audio capture / playout
 *    engine per reserved system.
 *
 * Each reservation method takes the owning @ref Ntv2MediaIO so the
 * device can identify the holder in error messages and ignore
 * idempotent re-reservation by the same owner.
 *
 * @par Threading
 *
 * The internal @ref Mutex guards every reservation table and the
 * card-level register access pattern (routing, channel enable,
 * reference selection).  AutoCirculate transfers themselves do not
 * take this mutex — each channel's worker drives its own
 * AutoCirculate state, and AJA's driver serialises channel-scoped
 * operations on its own.
 */
class Ntv2Device {
        public:
                /**
                 * @brief Returns the raw @c CNTV2Card handle.
                 *
                 * Used by backends that need to drive registers directly
                 * (AutoCirculate setup, signal routing, ANC engine
                 * configuration).  The caller is responsible for taking
                 * @ref mutex around any access pattern that races across
                 * channels.
                 */
                CNTV2Card &card();

                /** @brief Device mutex — taken for cross-channel register access. */
                Mutex &mutex() { return _mutex; }

                /** @brief Cached capability snapshot. */
                const Ntv2Capabilities &caps() const { return _caps; }

                /**
                 * @brief Returns the canonical display name (e.g. "Kona 5").
                 */
                const String &displayName() const { return _displayName; }

                /**
                 * @brief Returns the printable serial number, when
                 *        available.
                 *
                 * Empty when the card or driver doesn't expose one.
                 */
                const String &serial() const { return _serial; }

                /**
                 * @brief Stable per-device key used by the registry
                 *        and the clock's domain.
                 *
                 * Format: @c "ntv2:<deviceIndex>" for index-bound
                 * acquisitions, @c "ntv2:serial:<serial>" when the
                 * board exposes a serial number.
                 */
                const String &key() const { return _key; }

                /** @brief Device index as enumerated by @c CNTV2DeviceScanner. */
                int deviceIndex() const { return _deviceIndex; }

                // ---- Reservations ----

                /**
                 * @brief Reserves logical channel @p channel for @p owner.
                 *
                 * @param channel  1-based channel index.
                 * @param owner    The MediaIO claiming the channel.
                 * @return @c Error::Ok on success, @c Error::Busy when
                 *         another MediaIO already owns the channel,
                 *         @c Error::InvalidArgument when @p channel is
                 *         out of range.
                 */
                Error reserveChannel(int channel, Ntv2MediaIO *owner);

                /**
                 * @brief Releases a previously-reserved channel.
                 *
                 * Idempotent — releasing a channel not held by
                 * @p owner returns @c Error::Ok with no side effects.
                 */
                Error releaseChannel(int channel, Ntv2MediaIO *owner);

                /**
                 * @brief Reserves the SDI / HDMI ports listed in @p ports
                 *        for @p owner.
                 *
                 * All ports succeed or all fail — on any single-port
                 * conflict the entire reservation is rolled back so the
                 * caller can fail Open atomically.
                 */
                Error reservePorts(const SdiSignalConfig::PortList &ports, Ntv2MediaIO *owner);

                /**
                 * @brief Releases every port currently held by @p owner.
                 *
                 * Convenience for the close path so callers don't have
                 * to track which ports they reserved at open time.
                 */
                void releasePortsOwnedBy(Ntv2MediaIO *owner);

                /**
                 * @brief Reserves audio system @p sysIndex for @p owner.
                 *
                 * @param sysIndex 1-based audio system index (matches
                 *                 AJA's @c NTV2_AUDIOSYSTEM_1.. numbering
                 *                 + 1).
                 * @param owner    The MediaIO claiming the system.
                 */
                Error reserveAudioSystem(int sysIndex, Ntv2MediaIO *owner);

                /** @brief Releases @p sysIndex from @p owner. */
                Error releaseAudioSystem(int sysIndex, Ntv2MediaIO *owner);

                // ---- Reference clock ----

                /**
                 * @brief Applies a device-wide reference clock setting.
                 *
                 * Logs a warning naming both the prior owner and the new
                 * @p requester when an existing reference conflicts with
                 * the new request, but the request still wins (the
                 * user-explicit setting is honoured; earlier channels do
                 * not retroactively fail).  Pass an empty / default
                 * @p ref to leave the current reference in place.
                 */
                Error setReference(const VideoReferenceConfig &ref, Ntv2MediaIO *requester);

                // ---- Sample clock ----

                /**
                 * @brief Lazily constructs and returns the device's
                 *        shared sample-counter @ref Clock.
                 *
                 * Every channel on this card gets the same
                 * @ref Clock::Ptr — cross-channel timestamps on the same
                 * card share an epoch by construction.  The clock
                 * reads the FPGA-resident @c kRegAud1Counter (via
                 * @c CNTV2Card::GetRawAudioTimer), so no audio-system
                 * reservation is required.  VBI fallback engages only
                 * when @ref Ntv2Capabilities::hasAudioCounter is false
                 * (no audio subsystem at all).
                 *
                 * The clock is released when the device's refcount
                 * reaches zero (last @ref Ntv2DeviceRegistry::release).
                 */
                Clock::Ptr sampleClock();

                // ---- Static helpers ----

                /**
                 * @brief Resolves a textual device argument to a 0-based
                 *        device index.
                 *
                 * Accepts an integer ("0", "1"), a name shorthand
                 * ("kona5", "corvid44"), or "serial:NNN".  Returns -1
                 * when no match is found.  Pure SDK-side lookup; no
                 * side effects on any device.
                 */
                static int resolveDeviceIndex(const String &locator);

                // Destructor is public so the registry's UniquePtr can
                // delete the device; construction stays restricted via
                // the private default constructor + friend declaration.
                ~Ntv2Device();

        private:
                friend class Ntv2DeviceRegistry;
                friend struct Ntv2DeviceTestAccess;

                // Constructed only through the registry.
                Ntv2Device();

                Ntv2Device(const Ntv2Device &) = delete;
                Ntv2Device &operator=(const Ntv2Device &) = delete;

                Error initialize(int deviceIndex, const String &locator, bool retailServices,
                                 bool multiFormat);
                void  shutdown(bool retailServices);

                // Returns the first audio system (1-based) currently
                // reserved by any channel, or 0 when none.  No longer
                // load-bearing for the device clock (which reads the
                // shared FPGA counter); retained for code paths that
                // still want to know whether any audio is in flight.
                int firstReservedAudioSystem() const;

                UniquePtr<CNTV2Card> _card;
                Ntv2Capabilities     _caps;
                mutable Mutex        _mutex;

                String _key;
                String _displayName;
                String _serial;
                int    _deviceIndex = -1;

                // Reservation tables.  Channels and audio systems use
                // sparse Map<int, Ntv2MediaIO*> rather than arrays so
                // the unused range stays cheap.
                Map<int, Ntv2MediaIO *> _channelOwners;
                Map<int, Ntv2MediaIO *> _audioSystemOwners;

                // Ports: (kindValue * 100 + index) → owner.  Encoded as
                // int so the same Map shape works for SDI + HDMI without
                // a custom key type.
                Map<int, Ntv2MediaIO *> _portOwners;

                // Reference clock state — used for the warn-on-conflict
                // path.  Both fields default to "unset" so the very
                // first request applies without a spurious warning.
                bool         _refSet = false;
                int          _currentReferenceRaw = 0;  // AJA NTV2ReferenceSource as int
                Ntv2MediaIO *_refOwner = nullptr;

                // Lazily constructed shared clock — see sampleClock().
                Clock::Ptr _sampleClock;
};

/**
 * @brief Process singleton that vends refcounted @ref Ntv2Device handles.
 * @ingroup proav
 *
 * Two open calls naming the same physical card share one
 * @ref Ntv2Device instance — the registry refcounts the underlying
 * @ref Ntv2Device by its @ref Ntv2Device::key.  The registry is
 * internally synchronized and safe to call from any thread.
 *
 * The acquire / release pattern (rather than refcounted handles)
 * keeps the lifetime story explicit at the open/close boundary so
 * the device's resource reservations and the registry's refcount
 * stay in sync without depending on @ref SharedPtr destruction
 * order.
 */
class Ntv2DeviceRegistry {
        public:
                /** @brief Returns the process-wide singleton. */
                static Ntv2DeviceRegistry &instance();

                /**
                 * @brief Acquires (or constructs) the device matching @p deviceIndex.
                 *
                 * @param deviceIndex   0-based index from
                 *                      @c CNTV2DeviceScanner::GetNumDevices,
                 *                      or -1 to use @p locator.
                 * @param locator       String form (name shorthand or
                 *                      "serial:NNN") used when
                 *                      @p deviceIndex is -1.
                 * @param retailServices Inverse of "switch to OEM tasks
                 *                       on first acquire".
                 * @param multiFormat   Initial @c MultiFormatMode setting
                 *                      applied on first acquire.
                 * @param outDevice     Receives the acquired device pointer.
                 * @return @c Error::Ok on success.
                 *
                 * On success the caller must eventually call
                 * @ref release with the same pointer.  Repeated acquires
                 * of the same device return the same pointer with the
                 * refcount bumped; the @p retailServices / @p multiFormat
                 * settings are honoured on the *first* acquire only.
                 */
                Error acquire(int deviceIndex, const String &locator, bool retailServices,
                              bool multiFormat, Ntv2Device **outDevice);

                /**
                 * @brief Releases a previously-acquired device.
                 *
                 * Decrements the refcount; on transition to zero the
                 * device is shut down and the registry entry removed.
                 */
                void release(Ntv2Device *device);

                /**
                 * @brief Returns the list of currently-acquired devices.
                 *
                 * Snapshot — the underlying registry may change between
                 * the call and the caller's use.  Intended for tooling
                 * (probe output) and tests.
                 */
                List<Ntv2Device *> liveDevices() const;

        private:
                Ntv2DeviceRegistry() = default;
                ~Ntv2DeviceRegistry();

                Ntv2DeviceRegistry(const Ntv2DeviceRegistry &) = delete;
                Ntv2DeviceRegistry &operator=(const Ntv2DeviceRegistry &) = delete;

                struct Entry {
                                UniquePtr<Ntv2Device> device;
                                int                   refCount = 0;
                                bool                  retailServices = false;
                };

                mutable Mutex     _mutex;
                Map<String, Entry> _entries;
};

/**
 * @brief Hardware-free test seam for @ref Ntv2Device.
 * @ingroup proav
 *
 * Builds an @ref Ntv2Device with a hand-crafted
 * @ref Ntv2Capabilities snapshot and leaves @c _card null, so the
 * reservation tables, the reference-clock state, and the sample
 * clock can all be exercised in unit tests without opening a real
 * AJA card.  Production code never touches this struct.
 *
 * Inspectors return read-only views into the private member state
 * so tests can assert on internal bookkeeping (channel owners, port
 * owners, audio-system owners, reference state) directly.
 */
struct Ntv2DeviceTestAccess {
                /**
                 * @brief Constructs an @ref Ntv2Device with the given
                 *        capability shape and a synthetic identity.
                 *
                 * No @c CNTV2Card is opened — methods that would poke
                 * a real card (notably @ref Ntv2Device::setReference's
                 * write-to-card branch) detect the null @c _card and
                 * silently skip the hardware side.
                 */
                static UniquePtr<Ntv2Device> create(int deviceIndex, const String &displayName,
                                                    const Ntv2Capabilities &caps);

                /** @brief Number of channels currently reserved. */
                static size_t channelOwnerCount(const Ntv2Device &dev);

                /** @brief Channel-owner accessor; returns @c nullptr when unowned. */
                static const Ntv2MediaIO *channelOwner(const Ntv2Device &dev, int channel);

                /** @brief Number of ports currently reserved. */
                static size_t portOwnerCount(const Ntv2Device &dev);

                /** @brief Number of audio systems currently reserved. */
                static size_t audioSystemOwnerCount(const Ntv2Device &dev);

                /** @brief Audio-system owner accessor; @c nullptr when unowned. */
                static const Ntv2MediaIO *audioSystemOwner(const Ntv2Device &dev, int sysIndex);

                /** @brief @c true once @ref Ntv2Device::setReference has been called. */
                static bool referenceSet(const Ntv2Device &dev);

                /** @brief The raw NTV2ReferenceSource int stored on the last setReference call. */
                static int currentReferenceRaw(const Ntv2Device &dev);

                /** @brief Last requester recorded by @ref Ntv2Device::setReference. */
                static const Ntv2MediaIO *referenceOwner(const Ntv2Device &dev);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
