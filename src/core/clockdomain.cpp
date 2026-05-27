/**
 * @file      clockdomain.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/clockdomain.h>
#include <promeki/metadata.h>
#include <promeki/map.h>
#include <promeki/mutex.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Data definition (opaque in the header)
// ============================================================================

struct ClockDomain::Data {
                ID                id;
                String            name;
                String            description;
                ClockEpoch        epoch;
                Metadata          metadata;
                WallClockProvider nowProvider;
};

// ============================================================================
// Registry
// ============================================================================

struct ClockDomainRegistry {
                Map<uint64_t, ClockDomain::Data> entries;
                // Provider mutations + reads serialise through @c lock so a
                // backend thread binding @c ClockDomain::Ptp on open does
                // not race a packetizer thread reading @ref nowUtcNs.
                mutable Mutex lock;

                ClockDomainRegistry() {
                        // Synthetic — frame-rate derived, per-stream epoch
                        {
                                ClockDomain::Data d;
                                d.id = ClockDomain::Synthetic;
                                d.name = "Synthetic";
                                d.description = "Computed from frame rate, drift-free";
                                d.epoch = ClockEpoch::PerStream;
                                addLocked(std::move(d));
                        }
                        // SystemMonotonic — steady_clock, correlated within process
                        {
                                ClockDomain::Data d;
                                d.id = ClockDomain::SystemMonotonic;
                                d.name = "SystemMonotonic";
                                d.description = "std::chrono::steady_clock";
                                d.epoch = ClockEpoch::Correlated;
                                addLocked(std::move(d));
                        }
                        // Ptp — IEEE 1588 / SMPTE ST 2059-2 absolute timescale.
                        // Registered eagerly so lookups always succeed; a
                        // PhcClock-backed provider is bound at runtime by
                        // RtpMediaIO when MediaConfig::RtpPtpDevicePath is set.
                        {
                                ClockDomain::Data d;
                                d.id = ClockDomain::Ptp;
                                d.name = "Ptp";
                                d.description =
                                        "IEEE 1588 / SMPTE ST 2059-2 PTP wallclock (TAI-derived)";
                                d.epoch = ClockEpoch::Absolute;
                                addLocked(std::move(d));
                        }
                }

                // Internal helper — caller holds the registry lock.
                void addLocked(ClockDomain::Data &&d) {
                        uint64_t key = d.id.id();
                        entries.insert(key, std::move(d));
                }

                const ClockDomain::Data *find(const ClockDomain::ID &id) const {
                        if (!id.isValid()) return nullptr;
                        auto it = entries.find(id.id());
                        if (it != entries.end()) return &it->second;
                        return nullptr;
                }

                ClockDomain::Data *findMutable(const ClockDomain::ID &id) {
                        if (!id.isValid()) return nullptr;
                        auto it = entries.find(id.id());
                        if (it != entries.end()) return &it->second;
                        return nullptr;
                }
};

static ClockDomainRegistry &registry() {
        static ClockDomainRegistry reg;
        return reg;
}

// ============================================================================
// Well-known IDs
// ============================================================================

const ClockDomain::ID ClockDomain::Synthetic("Synthetic");
const ClockDomain::ID ClockDomain::SystemMonotonic("SystemMonotonic");
const ClockDomain::ID ClockDomain::Ptp("Ptp");

// ============================================================================
// Static API
// ============================================================================

ClockDomain::ID ClockDomain::registerDomain(const String &name, const String &description, const ClockEpoch &epoch) {
        ID            id(name);
        auto         &reg = registry();
        Mutex::Locker lock(reg.lock);
        if (reg.find(id) != nullptr) return id;
        Data d;
        d.id = id;
        d.name = name;
        d.description = description;
        d.epoch = epoch;
        reg.addLocked(std::move(d));
        return id;
}

void ClockDomain::setDomainMetadata(const ID &id, const Metadata &metadata) {
        auto         &reg = registry();
        Mutex::Locker lock(reg.lock);
        Data         *d = reg.findMutable(id);
        if (d != nullptr) d->metadata = metadata;
}

void ClockDomain::setNowProvider(const ID &id, WallClockProvider provider) {
        auto         &reg = registry();
        Mutex::Locker lock(reg.lock);
        Data         *d = reg.findMutable(id);
        if (d != nullptr) d->nowProvider = std::move(provider);
}

int64_t ClockDomain::nowUtcNs(const ID &id) {
        // Copy the Function out of the registry under the lock so the
        // call site doesn't hold the registry lock across the
        // user-supplied provider (which can do arbitrary work — ioctls,
        // syscalls, log calls).  Function is a thin wrapper around
        // std::function, so the copy is cheap.
        auto             &reg = registry();
        WallClockProvider provider;
        {
                Mutex::Locker     lock(reg.lock);
                const Data       *d = reg.find(id);
                if (d == nullptr) return 0;
                provider = d->nowProvider;
        }
        if (!static_cast<bool>(provider)) return 0;
        const int64_t v = provider();
        return v > 0 ? v : 0;
}

bool ClockDomain::hasNowProvider(const ID &id) {
        auto         &reg = registry();
        Mutex::Locker lock(reg.lock);
        const Data   *d = reg.find(id);
        if (d == nullptr) return false;
        return static_cast<bool>(d->nowProvider);
}

ClockDomain::IDList ClockDomain::registeredIDs() {
        IDList        ret;
        auto         &reg = registry();
        Mutex::Locker lock(reg.lock);
        for (auto it = reg.entries.begin(); it != reg.entries.end(); ++it) {
                ret.pushToBack(it->second.id);
        }
        return ret;
}

ClockDomain ClockDomain::lookup(const String &name) {
        ID id = ID::find(name);
        if (!id.isValid()) return ClockDomain();
        // Probe under the lock, but construct the @c ClockDomain
        // handle outside it — the handle's constructor calls
        // @ref lookupData which acquires the same lock, and
        // @c promeki::Mutex is non-reentrant.
        bool exists;
        {
                auto         &reg = registry();
                Mutex::Locker lock(reg.lock);
                exists = reg.find(id) != nullptr;
        }
        if (exists) return ClockDomain(id);
        return ClockDomain();
}

const ClockDomain::Data *ClockDomain::lookupData(const ID &id) {
        auto         &reg = registry();
        Mutex::Locker lock(reg.lock);
        return reg.find(id);
}

// ============================================================================
// Construction
// ============================================================================

ClockDomain::ClockDomain(const ID &id) : d(lookupData(id)) {}

// ============================================================================
// Accessors
// ============================================================================

static const String     _emptyString;
static const ClockEpoch _defaultEpoch;
static const Metadata   _emptyMetadata;

bool ClockDomain::isValid() const {
        return d != nullptr;
}

ClockDomain::ID ClockDomain::id() const {
        return d ? d->id : ID();
}

const String &ClockDomain::name() const {
        return d ? d->name : _emptyString;
}

const String &ClockDomain::description() const {
        return d ? d->description : _emptyString;
}

const ClockEpoch &ClockDomain::epoch() const {
        return d ? d->epoch : _defaultEpoch;
}

const Metadata &ClockDomain::metadata() const {
        return d ? d->metadata : _emptyMetadata;
}

bool ClockDomain::isCrossStreamComparable() const {
        if (!d) return false;
        return d->epoch == ClockEpoch::Correlated || d->epoch == ClockEpoch::Absolute;
}

bool ClockDomain::isCrossMachineComparable() const {
        if (!d) return false;
        return d->epoch == ClockEpoch::Absolute;
}

String ClockDomain::toString() const {
        return d ? d->name : String();
}

int64_t ClockDomain::nowUtcNs() const {
        if (d == nullptr) return 0;
        return ClockDomain::nowUtcNs(d->id);
}

bool ClockDomain::hasNowProvider() const {
        if (d == nullptr) return false;
        return ClockDomain::hasNowProvider(d->id);
}

PROMEKI_NAMESPACE_END
