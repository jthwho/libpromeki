/**
 * @file      mdnstypebrowser.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnstypebrowser.h>
#include <promeki/application.h>
#include <promeki/mdnsmanager.h>
#include <promeki/mdnspacket.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        bool dnsEquals(const String &a, const String &b) {
                auto strip = [](const String &s) -> String {
                        if (s.size() > 0 && s[s.size() - 1] == '.') return s.substr(0, s.size() - 1);
                        return s;
                };
                return strip(a).compareIgnoreCase(strip(b)) == 0;
        }

        String foldName(const String &s) {
                String stripped = s;
                if (stripped.size() > 0 && stripped[stripped.size() - 1] == '.') {
                        stripped = stripped.substr(0, stripped.size() - 1);
                }
                return stripped.toLower();
        }

} // anonymous namespace

MdnsTypeBrowser::MdnsTypeBrowser(ObjectBase *parent) : ObjectBase(parent) {
        _active.setValue(false);
}

MdnsTypeBrowser::~MdnsTypeBrowser() {
        stop();
        setManager(nullptr);
}

void MdnsTypeBrowser::setManager(MdnsManager *manager) {
        if (_manager == manager) return;
        if (_manager != nullptr) _manager->unregisterTypeBrowser(this);
        _manager = manager;
        if (_manager != nullptr) _manager->registerTypeBrowser(this);
}

MdnsManager *MdnsTypeBrowser::effectiveManager() const {
        if (_manager != nullptr) return _manager;
        return Application::mdnsManager();
}

Error MdnsTypeBrowser::start() {
        if (effectiveManager() == nullptr) return Error::NotReady;
        _active.setValue(true);
        {
                Mutex::Locker lock(_entriesMtx);
                _currentInterval = Duration::fromMilliseconds(InitialQueryIntervalMs);
                _nextQueryAt     = TimeStamp::now() + _currentInterval;
        }
        return sendQuery();
}

void MdnsTypeBrowser::stop() {
        _active.setValue(false);
        Mutex::Locker lock(_entriesMtx);
        _currentInterval = Duration();
        _nextQueryAt     = TimeStamp();
}

Error MdnsTypeBrowser::sendQuery() {
        MdnsManager *mgr = effectiveManager();
        if (mgr == nullptr) return Error::NotReady;
        return mgr->sendMetaQuery();
}

void MdnsTypeBrowser::onManagerTick(const TimeStamp &now) {
        // Eviction sweep first — any expired entry triggers a
        // typeLost emission outside the lock.
        List<MdnsServiceType> lost;
        {
                Mutex::Locker lock(_entriesMtx);
                List<String> doomed;
                for (const auto &kv : _entries) {
                        const Entry &e = kv.second;
                        if (!e.lastSeen.isValid() || !e.ttl.isValid()) continue;
                        if (e.lastSeen + e.ttl > now) continue;
                        doomed += kv.first;
                }
                for (const String &k : doomed) {
                        auto it = _entries.find(k);
                        if (it == _entries.end()) continue;
                        lost += it->second.type;
                        _entries.remove(k);
                }
        }
        for (const MdnsServiceType &t : lost) typeLostSignal.emit(t);

        // Backoff schedule — same arithmetic as MdnsBrowser.
        if (!_active.value() || effectiveManager() == nullptr) return;
        bool fire = false;
        {
                Mutex::Locker lock(_entriesMtx);
                if (!_nextQueryAt.isValid()) return;
                if (now < _nextQueryAt) return;
                fire = true;
                Duration next = _currentInterval * 2;
                const Duration cap = Duration::fromMilliseconds(MaxQueryIntervalMs);
                if (next > cap) next = cap;
                _currentInterval = next;
                _nextQueryAt     = now + _currentInterval;
        }
        if (fire) sendQuery();
}

void MdnsTypeBrowser::handlePacket(const Buffer &data, const SocketAddress & /*sender*/,
                                   const NetworkInterface & /*iface*/) {
        auto r = MdnsPacket::parse(data);
        if (!r.second().isOk()) return;

        const String metaName = MdnsManager::metaBrowseFqdn();
        const TimeStamp now   = TimeStamp::now();
        List<MdnsServiceType> found;
        List<MdnsServiceType> lost;
        {
                Mutex::Locker lock(_entriesMtx);
                for (const MdnsParsedRecord &rec : r.first().records()) {
                        if (rec.type != MdnsParsedRecord::Type::Ptr) continue;
                        if (!dnsEquals(rec.name, metaName)) continue;
                        if (rec.ptrTarget.isEmpty()) continue;

                        auto parsed = MdnsServiceType::fromString(rec.ptrTarget);
                        if (parsed.second().isError()) continue;
                        const MdnsServiceType &t = parsed.first();
                        const String key = foldName(rec.ptrTarget);

                        // Goodbye for a known type.
                        if (rec.ttl == Duration::zero()) {
                                auto it = _entries.find(key);
                                if (it == _entries.end()) continue;
                                lost += it->second.type;
                                _entries.remove(key);
                                continue;
                        }

                        auto it = _entries.find(key);
                        if (it == _entries.end()) {
                                Entry e;
                                e.type     = t;
                                e.lastSeen = now;
                                e.ttl      = rec.ttl;
                                _entries.insert(key, std::move(e));
                                found += t;
                        } else {
                                it->second.lastSeen = now;
                                it->second.ttl      = rec.ttl;
                        }
                }
        }
        for (const MdnsServiceType &t : found) typeFoundSignal.emit(t);
        for (const MdnsServiceType &t : lost)  typeLostSignal.emit(t);
}

List<MdnsServiceType> MdnsTypeBrowser::types() const {
        List<MdnsServiceType> out;
        Mutex::Locker lock(_entriesMtx);
        for (const auto &kv : _entries) out += kv.second.type;
        return out;
}

void MdnsTypeBrowser::clearCache() {
        Mutex::Locker lock(_entriesMtx);
        _entries.clear();
}

PROMEKI_NAMESPACE_END
