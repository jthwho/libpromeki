/**
 * @file      ntv2device.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2device.h>

#include <promeki/logger.h>
#include <promeki/ntv2clock.h>
#include <promeki/ntv2format.h>
#include <promeki/string.h>

#include <ntv2card.h>
#include <ntv2devicefeatures.h>
#include <ntv2devicescanner.h>
#include <ntv2enums.h>
#include <ntv2utils.h>

#include <unistd.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Application signature used in AcquireStreamForApplication.
        // 'P','R','M','K' — process-wide identifier so AJA's other
        // owners (retail services, demos) see who currently owns the
        // card.  Pick something that won't collide with the demos'
        // 'D','E','M','O' tag if they happen to share a host.
        constexpr ULWord kPromekiAppSignature =
                (uint32_t('P') << 24) | (uint32_t('R') << 16) | (uint32_t('M') << 8) | uint32_t('K');

        // Encode (kind, index) → int for the port-owner table key.
        // Kind value is the TypedEnum's underlying int; index is 1-based.
        // Multiplier well above any reasonable index keeps SDI and HDMI
        // ports in disjoint regions of the key space.
        int portKey(const VideoConnectorKind &kind, int index) {
                return kind.value() * 100 + index;
        }

} // namespace

// ============================================================================
// Ntv2Device
// ============================================================================

Ntv2Device::Ntv2Device() = default;

Ntv2Device::~Ntv2Device() {
        // shutdown() must have been called by the registry first.  The
        // unique_ptr to the card is released here as the safety net.
}

CNTV2Card &Ntv2Device::card() {
        return *_card;
}

Error Ntv2Device::initialize(int deviceIndex, const String &locator, bool retailServices,
                             bool multiFormat) {
        _card.reset(new CNTV2Card());

        if (deviceIndex >= 0) {
                if (!CNTV2DeviceScanner::GetDeviceAtIndex(static_cast<ULWord>(deviceIndex), *_card)) {
                        promekiErr("Ntv2Device: GetDeviceAtIndex(%d) failed", deviceIndex);
                        return Error::DeviceNotFound;
                }
        } else {
                if (locator.isEmpty()) {
                        promekiErr("Ntv2Device: locator empty and deviceIndex < 0");
                        return Error::InvalidArgument;
                }
                if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument(locator.cstr(), *_card)) {
                        promekiErr("Ntv2Device: '%s' not found via CNTV2DeviceScanner", locator.cstr());
                        return Error::DeviceNotFound;
                }
        }

        if (!_card->IsDeviceReady()) {
                promekiErr("Ntv2Device: '%s' not ready", _card->GetDisplayName().c_str());
                return Error::DeviceError;
        }

        _deviceIndex = static_cast<int>(_card->GetIndexNumber());
        _displayName = String(_card->GetDisplayName().c_str());

        std::string serialStr;
        if (_card->GetSerialNumberString(serialStr) && !serialStr.empty()) {
                _serial = String(serialStr.c_str());
                _key    = String("ntv2:serial:") + _serial;
        } else {
                _key = String("ntv2:") + String::number(_deviceIndex);
        }

        if (!retailServices) {
                if (!_card->AcquireStreamForApplication(kPromekiAppSignature,
                                                        static_cast<int32_t>(::getpid()))) {
                        promekiErr("Ntv2Device: AcquireStreamForApplication failed on '%s' "
                                   "(another app owns the card)",
                                   _displayName.cstr());
                        _card.reset();
                        return Error::Busy;
                }
                NTV2TaskMode prior = NTV2_DISABLE_TASKS;
                _card->GetTaskMode(prior);
                _card->SetTaskMode(NTV2_OEM_TASKS);
        }

        if (!_caps.probe(*_card)) {
                promekiErr("Ntv2Device: capability probe failed on '%s'", _displayName.cstr());
                if (!retailServices) {
                        _card->ReleaseStreamForApplication(kPromekiAppSignature,
                                                           static_cast<int32_t>(::getpid()));
                }
                _card.reset();
                return Error::DeviceError;
        }

        if (_caps.canDoMultiFormat()) {
                _card->SetMultiFormatMode(multiFormat);
        }

        promekiInfo("Ntv2Device: opened '%s' (idx=%d, serial='%s') — %s",
                    _displayName.cstr(), _deviceIndex, _serial.cstr(),
                    _caps.toString().cstr());
        return Error::Ok;
}

void Ntv2Device::shutdown(bool retailServices) {
        // Drop the lazily-constructed shared clock first so its
        // destructor runs while we still hold the card handle.
        _sampleClock = Clock::Ptr();

        if (!_card) return;
        if (!retailServices) {
                _card->ReleaseStreamForApplication(kPromekiAppSignature,
                                                   static_cast<int32_t>(::getpid()));
        }
        _card.reset();
}

// ---- Reservations ----

Error Ntv2Device::reserveChannel(int channel, Ntv2MediaIO *owner) {
        if (channel < 1 || channel > _caps.channelCount()) {
                promekiErr("Ntv2Device::reserveChannel: channel %d out of range (1..%d)",
                           channel, _caps.channelCount());
                return Error::InvalidArgument;
        }
        Mutex::Locker lk(_mutex);
        auto it = _channelOwners.find(channel);
        if (it != _channelOwners.end()) {
                if (it->second == owner) return Error::Ok;
                promekiErr("Ntv2Device::reserveChannel: channel %d already held by another MediaIO",
                           channel);
                return Error::Busy;
        }
        _channelOwners.insert(channel, owner);
        return Error::Ok;
}

Error Ntv2Device::releaseChannel(int channel, Ntv2MediaIO *owner) {
        Mutex::Locker lk(_mutex);
        auto it = _channelOwners.find(channel);
        if (it != _channelOwners.end() && it->second == owner) {
                _channelOwners.remove(it);
        }
        return Error::Ok;
}

Error Ntv2Device::reservePorts(const SdiSignalConfig::PortList &ports, Ntv2MediaIO *owner) {
        Mutex::Locker lk(_mutex);
        // Validate every port first so a conflict on the last one
        // doesn't leave earlier ones held.
        for (size_t i = 0; i < ports.size(); ++i) {
                const VideoPortRef &p = ports[i];
                if (!p.isValid()) {
                        promekiErr("Ntv2Device::reservePorts: invalid VideoPortRef at index %zu", i);
                        return Error::InvalidArgument;
                }
                const int key = portKey(p.kind(), p.index());
                auto it = _portOwners.find(key);
                if (it != _portOwners.end() && it->second != owner) {
                        promekiErr("Ntv2Device::reservePorts: port %s already held by another MediaIO",
                                   p.toString().cstr());
                        return Error::Busy;
                }
        }
        for (size_t i = 0; i < ports.size(); ++i) {
                _portOwners.insert(portKey(ports[i].kind(), ports[i].index()), owner);
        }
        return Error::Ok;
}

void Ntv2Device::releasePortsOwnedBy(Ntv2MediaIO *owner) {
        Mutex::Locker lk(_mutex);
        // Map<>::remove() in-loop would invalidate the iterator —
        // collect the keys first, then remove.  N is tiny (at most a
        // handful of ports per release call) so the two-pass cost is
        // immaterial.
        List<int> toRemove;
        for (const auto &entry : _portOwners) {
                if (entry.second == owner) toRemove.pushToBack(entry.first);
        }
        for (size_t i = 0; i < toRemove.size(); ++i) {
                _portOwners.remove(toRemove[i]);
        }
}

Error Ntv2Device::reserveAudioSystem(int sysIndex, Ntv2MediaIO *owner) {
        if (sysIndex < 1 || sysIndex > _caps.audioSystemCount()) {
                promekiErr("Ntv2Device::reserveAudioSystem: system %d out of range (1..%d)",
                           sysIndex, _caps.audioSystemCount());
                return Error::InvalidArgument;
        }
        Mutex::Locker lk(_mutex);
        auto it = _audioSystemOwners.find(sysIndex);
        if (it != _audioSystemOwners.end()) {
                if (it->second == owner) return Error::Ok;
                promekiErr("Ntv2Device::reserveAudioSystem: system %d already held by another MediaIO",
                           sysIndex);
                return Error::Busy;
        }
        _audioSystemOwners.insert(sysIndex, owner);
        return Error::Ok;
}

Error Ntv2Device::releaseAudioSystem(int sysIndex, Ntv2MediaIO *owner) {
        Mutex::Locker lk(_mutex);
        auto it = _audioSystemOwners.find(sysIndex);
        if (it != _audioSystemOwners.end() && it->second == owner) {
                _audioSystemOwners.remove(it);
        }
        return Error::Ok;
}

int Ntv2Device::firstReservedAudioSystem() const {
        Mutex::Locker lk(_mutex);
        if (_audioSystemOwners.isEmpty()) return 0;
        // Map<int,V> is ordered by key — the begin() entry is the
        // lowest-numbered reserved system, which is what the clock
        // wants for canonical ordering across cards.
        return _audioSystemOwners.begin()->first;
}

// ---- Reference clock ----

Error Ntv2Device::setReference(const VideoReferenceConfig &ref, Ntv2MediaIO *requester) {
        if (ref.source() == VideoReferenceSource::FreeRun && !_refSet) {
                // First-acquire default — silently apply free-run.
        }
        const int newRefRaw = Ntv2Format::referenceFor(ref);

        Mutex::Locker lk(_mutex);
        if (_refSet && _currentReferenceRaw != newRefRaw && requester != _refOwner) {
                promekiWarn("Ntv2Device::setReference: device '%s' reference changing under "
                            "concurrent owners — applying new request",
                            _displayName.cstr());
        }
        const bool changed = !_refSet || _currentReferenceRaw != newRefRaw;
        _currentReferenceRaw = newRefRaw;
        _refOwner            = requester;
        _refSet              = true;
        if (changed && _card) {
                _card->SetReference(static_cast<NTV2ReferenceSource>(newRefRaw));
        }
        return Error::Ok;
}

// ---- Sample clock ----

Clock::Ptr Ntv2Device::sampleClock() {
        Mutex::Locker lk(_mutex);
        if (_sampleClock.isValid()) return _sampleClock;

        // kRegAud1Counter is a single FPGA-resident free-running 48 kHz
        // counter — no per-audio-system selection needed.  VBI fallback
        // engages only when the cap says the card has no audio
        // subsystem at all (no shipping NTV2 hardware is in that bucket
        // today, but the gate keeps us safe against future variants).
        const bool useVbiFallback = !_caps.hasAudioCounter();

        _sampleClock = Clock::Ptr::takeOwnership(
                new Ntv2DeviceClock(this, useVbiFallback));
        return _sampleClock;
}

// ---- Static helpers ----

int Ntv2Device::resolveDeviceIndex(const String &locator) {
        if (locator.isEmpty()) return -1;
        CNTV2Card tmp;
        if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument(locator.cstr(), tmp)) return -1;
        return static_cast<int>(tmp.GetIndexNumber());
}

// ============================================================================
// Ntv2DeviceRegistry
// ============================================================================

Ntv2DeviceRegistry &Ntv2DeviceRegistry::instance() {
        static Ntv2DeviceRegistry s;
        return s;
}

Ntv2DeviceRegistry::~Ntv2DeviceRegistry() {
        // The registry outlives every Ntv2MediaIO, so on process exit
        // the map should already be empty.  If anyone left a device
        // hanging the entry's UniquePtr will free it; the shutdown()
        // call below makes the cleanup explicit so the AJA driver sees
        // a balanced acquire/release pair.
        Mutex::Locker lk(_mutex);
        for (auto &entry : _entries) {
                entry.second.device->shutdown(entry.second.retailServices);
        }
        _entries.clear();
}

Error Ntv2DeviceRegistry::acquire(int deviceIndex, const String &locator, bool retailServices,
                                  bool multiFormat, Ntv2Device **outDevice) {
        if (outDevice == nullptr) return Error::InvalidArgument;
        *outDevice = nullptr;

        // Resolve the locator to an index outside the registry mutex
        // because the SDK scan can be slow.  When the caller passed an
        // explicit index that step is a no-op.
        int resolvedIdx = deviceIndex;
        if (resolvedIdx < 0) {
                resolvedIdx = Ntv2Device::resolveDeviceIndex(locator);
                if (resolvedIdx < 0) {
                        promekiErr("Ntv2DeviceRegistry::acquire: '%s' not found", locator.cstr());
                        return Error::DeviceNotFound;
                }
        }

        // Compute the canonical key without opening the card — by
        // index, with no serial.  If a future acquire by serial maps to
        // the same physical card, the device's _key remains the
        // index-based one, which is fine for registry identity.
        const String key = String("ntv2:idx:") + String::number(resolvedIdx);

        Mutex::Locker lk(_mutex);
        auto it = _entries.find(key);
        if (it != _entries.end()) {
                it->second.refCount += 1;
                *outDevice = it->second.device.get();
                return Error::Ok;
        }

        Entry entry;
        entry.device         = UniquePtr<Ntv2Device>::takeOwnership(new Ntv2Device());
        entry.refCount       = 1;
        entry.retailServices = retailServices;

        Error err = entry.device->initialize(resolvedIdx, locator, retailServices, multiFormat);
        if (err.isError()) return err;

        *outDevice = entry.device.get();
        _entries.insert(key, std::move(entry));
        return Error::Ok;
}

void Ntv2DeviceRegistry::release(Ntv2Device *device) {
        if (device == nullptr) return;
        Mutex::Locker lk(_mutex);
        // Linear search — the live-devices map is tiny (one entry per
        // physical card in the host).  Keeping the lookup map keyed by
        // string-key would force a hash recompute on every release;
        // walking the map is simpler.
        for (auto it = _entries.begin(); it != _entries.end(); ) {
                if (it->second.device.get() == device) {
                        it->second.refCount -= 1;
                        if (it->second.refCount <= 0) {
                                it->second.device->shutdown(it->second.retailServices);
                                it = _entries.remove(it);
                                return;
                        }
                        return;
                }
                ++it;
        }
}

// ============================================================================
// Ntv2DeviceTestAccess — hardware-free test seam
// ============================================================================

UniquePtr<Ntv2Device> Ntv2DeviceTestAccess::create(int deviceIndex, const String &displayName,
                                                   const Ntv2Capabilities &caps) {
        // Direct construction via the friend declaration — bypasses
        // Ntv2DeviceRegistry so no real CNTV2Card gets opened.  The
        // device's _card pointer stays null; setReference, sampleClock
        // etc. all gate on _card before touching hardware.
        UniquePtr<Ntv2Device> dev = UniquePtr<Ntv2Device>::takeOwnership(new Ntv2Device());
        dev->_caps         = caps;
        dev->_deviceIndex  = deviceIndex;
        dev->_displayName  = displayName;
        dev->_key          = String("ntv2:test:") + String::number(deviceIndex);
        return dev;
}

size_t Ntv2DeviceTestAccess::channelOwnerCount(const Ntv2Device &dev) {
        Mutex::Locker lk(dev._mutex);
        return dev._channelOwners.size();
}

const Ntv2MediaIO *Ntv2DeviceTestAccess::channelOwner(const Ntv2Device &dev, int channel) {
        Mutex::Locker lk(dev._mutex);
        auto it = dev._channelOwners.find(channel);
        return it == dev._channelOwners.end() ? nullptr : it->second;
}

size_t Ntv2DeviceTestAccess::portOwnerCount(const Ntv2Device &dev) {
        Mutex::Locker lk(dev._mutex);
        return dev._portOwners.size();
}

size_t Ntv2DeviceTestAccess::audioSystemOwnerCount(const Ntv2Device &dev) {
        Mutex::Locker lk(dev._mutex);
        return dev._audioSystemOwners.size();
}

const Ntv2MediaIO *Ntv2DeviceTestAccess::audioSystemOwner(const Ntv2Device &dev, int sysIndex) {
        Mutex::Locker lk(dev._mutex);
        auto it = dev._audioSystemOwners.find(sysIndex);
        return it == dev._audioSystemOwners.end() ? nullptr : it->second;
}

bool Ntv2DeviceTestAccess::referenceSet(const Ntv2Device &dev) {
        Mutex::Locker lk(dev._mutex);
        return dev._refSet;
}

int Ntv2DeviceTestAccess::currentReferenceRaw(const Ntv2Device &dev) {
        Mutex::Locker lk(dev._mutex);
        return dev._currentReferenceRaw;
}

const Ntv2MediaIO *Ntv2DeviceTestAccess::referenceOwner(const Ntv2Device &dev) {
        Mutex::Locker lk(dev._mutex);
        return dev._refOwner;
}

List<Ntv2Device *> Ntv2DeviceRegistry::liveDevices() const {
        List<Ntv2Device *> out;
        Mutex::Locker      lk(_mutex);
        for (const auto &entry : _entries) {
                out.pushToBack(entry.second.device.get());
        }
        return out;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
