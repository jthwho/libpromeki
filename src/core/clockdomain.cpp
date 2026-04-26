/**
 * @file      clockdomain.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/clockdomain.h>
#include <promeki/metadata.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Data definition (opaque in the header)
// ============================================================================

struct ClockDomain::Data {
                ID         id;
                String     name;
                String     description;
                ClockEpoch epoch;
                Metadata   metadata;
};

// ============================================================================
// Registry
// ============================================================================

struct ClockDomainRegistry {
                Map<uint64_t, ClockDomain::Data> entries;

                ClockDomainRegistry() {
                        // Synthetic — frame-rate derived, per-stream epoch
                        {
                                ClockDomain::Data d;
                                d.id = ClockDomain::Synthetic;
                                d.name = "Synthetic";
                                d.description = "Computed from frame rate, drift-free";
                                d.epoch = ClockEpoch::PerStream;
                                add(std::move(d));
                        }
                        // SystemMonotonic — steady_clock, correlated within process
                        {
                                ClockDomain::Data d;
                                d.id = ClockDomain::SystemMonotonic;
                                d.name = "SystemMonotonic";
                                d.description = "std::chrono::steady_clock";
                                d.epoch = ClockEpoch::Correlated;
                                add(std::move(d));
                        }
                }

                void add(ClockDomain::Data &&d) {
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

// ============================================================================
// Static API
// ============================================================================

ClockDomain::ID ClockDomain::registerDomain(const String &name, const String &description, const ClockEpoch &epoch) {
        ID    id(name);
        auto &reg = registry();
        if (reg.find(id) != nullptr) return id;
        Data d;
        d.id = id;
        d.name = name;
        d.description = description;
        d.epoch = epoch;
        reg.add(std::move(d));
        return id;
}

void ClockDomain::setDomainMetadata(const ID &id, const Metadata &metadata) {
        auto &reg = registry();
        Data *d = reg.findMutable(id);
        if (d != nullptr) d->metadata = metadata;
}

ClockDomain::IDList ClockDomain::registeredIDs() {
        IDList ret;
        auto  &reg = registry();
        for (auto it = reg.entries.begin(); it != reg.entries.end(); ++it) {
                ret.pushToBack(it->second.id);
        }
        return ret;
}

ClockDomain ClockDomain::lookup(const String &name) {
        ID id = ID::find(name);
        if (!id.isValid()) return ClockDomain();
        auto &reg = registry();
        if (reg.find(id) != nullptr) return ClockDomain(id);
        return ClockDomain();
}

const ClockDomain::Data *ClockDomain::lookupData(const ID &id) {
        return registry().find(id);
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

PROMEKI_NAMESPACE_END
