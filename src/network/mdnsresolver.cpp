/**
 * @file      mdnsresolver.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnsresolver.h>
#include <promeki/mdnsbrowser.h>
#include <promeki/mdnsmanager.h>
#include <promeki/objectbase.tpp>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Case-insensitive instance-name comparison, tolerant of a
        // trailing dot on either side.
        bool sameInstanceName(const String &a, const String &b) {
                auto strip = [](const String &s) -> String {
                        if (s.size() > 0 && s[s.size() - 1] == '.') return s.substr(0, s.size() - 1);
                        return s;
                };
                return strip(a).compareIgnoreCase(strip(b)) == 0;
        }

} // anonymous namespace

MdnsResolver::MdnsResolver(const MdnsServiceInstance &target, ObjectBase *parent)
    : ObjectBase(parent), _target(target),
      _timeout(Duration::fromMilliseconds(DefaultTimeoutMs)) {
        _active.setValue(false);
        _emitted.setValue(false);
}

MdnsResolver::~MdnsResolver() {
        stop();
}

void MdnsResolver::setManager(MdnsManager *manager) {
        if (_browser) _browser->setManager(manager);
}

MdnsManager *MdnsResolver::manager() const {
        return _browser ? _browser->manager() : nullptr;
}

void MdnsResolver::setTimeout(const Duration &timeout) {
        _timeout = timeout.isValid() ? timeout : Duration::fromMilliseconds(DefaultTimeoutMs);
}

MdnsServiceInstance MdnsResolver::snapshot() const {
        return _snapshot;
}

Error MdnsResolver::resolve() {
        if (_active.value()) return Error::Ok;
        if (!_target.type().isValid() || _target.instanceName().isEmpty()) {
                return Error::Invalid;
        }

        // Construct the private browser on demand so a freshly
        // constructed (and immediately destroyed) resolver does
        // not touch the application-wide manager fallback.
        if (!_browser) {
                _browser = UniquePtr<MdnsBrowser>::create(_target.type(), this);
                _browser->serviceFoundSignal.connect(
                        [this](MdnsServiceInstance i) { onBrowserFound(i); }, this);
                _browser->serviceUpdatedSignal.connect(
                        [this](MdnsServiceInstance i) { onBrowserUpdated(i); }, this);
                _browser->serviceLostSignal.connect(
                        [this](MdnsServiceInstance i) { onBrowserLost(i); }, this);
        }

        _snapshot = MdnsServiceInstance();
        _snapshot.setType(_target.type());
        _snapshot.setInstanceName(_target.instanceName());
        _emitted.setValue(false);
        _active.setValue(true);

        Error err = _browser->start();
        if (err.isError()) {
                _active.setValue(false);
                return err;
        }

        // Single-shot timeout timer.  Posts to this resolver's
        // affinity EventLoop so the body runs on the caller's
        // thread, not the manager worker — matches the semantics
        // of the resolved / failed signals.
        const unsigned int timeoutMs = static_cast<unsigned int>(_timeout.milliseconds());
        _timerId = startTimer(timeoutMs, /*singleShot=*/true);

        // Many publishers respond to an unsolicited PTR with the
        // full RR set immediately.  We additionally fire a directed
        // SRV query for the target instance FQDN so a host that
        // answers only directed queries (some embedded responders)
        // still completes the resolve.
        MdnsManager *mgr = _browser->effectiveManager();
        if (mgr != nullptr) {
                MdnsServiceInstance forFqdn;
                forFqdn.setType(_target.type());
                forFqdn.setInstanceName(_target.instanceName());
                const String fqdn = forFqdn.fqdn();
                if (!fqdn.isEmpty()) {
                        mgr->sendQuery(fqdn, /*SRV*/ 33, 0);
                        mgr->sendQuery(fqdn, /*TXT*/ 16, 0);
                }
        }
        return Error::Ok;
}

void MdnsResolver::stop() {
        if (!_active.value()) return;
        _active.setValue(false);
        if (_timerId >= 0) {
                stopTimer(_timerId);
                _timerId = -1;
        }
        if (_browser) _browser->stop();
}

void MdnsResolver::timerEvent(TimerEvent *e) {
        if (e == nullptr || e->timerId() != _timerId) {
                ObjectBase::timerEvent(e);
                return;
        }
        _timerId = -1;
        if (!_active.value() || _emitted.value()) return;
        _emitted.setValue(true);
        failedSignal.emit(Error::Timeout);
        stop();
}

void MdnsResolver::onBrowserFound(MdnsServiceInstance inst) {
        if (!_active.value()) return;
        if (!sameInstanceName(inst.instanceName(), _target.instanceName())) return;
        _snapshot = inst;
        if (!_emitted.value() && isComplete(_snapshot)) {
                _emitted.setValue(true);
                resolvedSignal.emit(_snapshot);
                stop();
        }
}

void MdnsResolver::onBrowserUpdated(MdnsServiceInstance inst) {
        if (!_active.value()) return;
        if (!sameInstanceName(inst.instanceName(), _target.instanceName())) return;
        _snapshot = inst;
        if (!_emitted.value() && isComplete(_snapshot)) {
                _emitted.setValue(true);
                resolvedSignal.emit(_snapshot);
                stop();
        }
}

void MdnsResolver::onBrowserLost(MdnsServiceInstance inst) {
        // A Goodbye for our target while we're still resolving is
        // a clean failure — the publisher gave up before we got the
        // full picture.
        if (!_active.value()) return;
        if (!sameInstanceName(inst.instanceName(), _target.instanceName())) return;
        if (_emitted.value()) return;
        _emitted.setValue(true);
        failedSignal.emit(Error::ObjectGone);
        stop();
}

bool MdnsResolver::isComplete(const MdnsServiceInstance &inst) {
        if (inst.port() == 0)         return false;
        if (inst.hostname().isEmpty()) return false;
        if (inst.ipv4Addresses().isEmpty() && inst.ipv6Addresses().isEmpty()) return false;
        return true;
}

PROMEKI_NAMESPACE_END
