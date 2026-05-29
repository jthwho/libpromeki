/**
 * @file      mdnspublisher.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnspublisher.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/logger.h>
#include <promeki/mdnsmanager.h>
#include <promeki/mdnspacket.h>
#include <promeki/random.h>
#include <promeki/thread.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // RFC 6762 §6: a responder MUST delay its reply to a
        // multicast query by a random interval between 20 and 120
        // milliseconds before transmitting.  The randomisation
        // coalesces near-simultaneous responses from independent
        // publishers on the same link.
        constexpr unsigned int ResponseJitterMinMs = 20;
        constexpr unsigned int ResponseJitterMaxMs = 120;

        // Case-insensitive, trailing-dot-tolerant DNS name compare —
        // same predicate the browser uses.
        bool dnsEquals(const String &a, const String &b) {
                auto strip = [](const String &s) -> String {
                        if (s.size() > 0 && s[s.size() - 1] == '.') return s.substr(0, s.size() - 1);
                        return s;
                };
                return strip(a).compareIgnoreCase(strip(b)) == 0;
        }

        // Returns the on-the-wire record type code the
        // @ref MdnsParsedRecord::Type enum carries.
        uint16_t parsedTypeCode(MdnsParsedRecord::Type t) {
                switch (t) {
                        case MdnsParsedRecord::Type::A:    return 1;
                        case MdnsParsedRecord::Type::Ptr:  return 12;
                        case MdnsParsedRecord::Type::Txt:  return 16;
                        case MdnsParsedRecord::Type::Aaaa: return 28;
                        case MdnsParsedRecord::Type::Srv:  return 33;
                        case MdnsParsedRecord::Type::Unknown: return 0;
                }
                return 0;
        }

        // RFC 1035 §3.2.2 QTYPE codes the publisher cares about.
        constexpr uint16_t QTypeAny = 255;

} // anonymous namespace

MdnsPublisher::MdnsPublisher(ObjectBase *parent) : ObjectBase(parent) {
        _state.setValue(State::Idle);
        _probeIdx.setValue(0);
        _announceIdx.setValue(0);
        _renameAttempts.setValue(0);
}

MdnsPublisher::~MdnsPublisher() {
        withdraw();
        setManager(nullptr);
}

void MdnsPublisher::setInstance(const MdnsServiceInstance &instance) {
        Mutex::Locker lock(_stateMtx);
        _instance = instance;
}

void MdnsPublisher::setManager(MdnsManager *manager) {
        if (_manager == manager) return;
        if (_manager != nullptr) _manager->unregisterPublisher(this);
        _manager = manager;
        if (_manager != nullptr) _manager->registerPublisher(this);
}

MdnsManager *MdnsPublisher::effectiveManager() const {
        if (_manager != nullptr) return _manager;
        return Application::mdnsManager();
}

List<MdnsRecord> MdnsPublisher::records() const {
        Mutex::Locker lock(_stateMtx);
        return _records;
}

Error MdnsPublisher::publish() {
        if (isActive()) return Error::Busy;
        if (!_instance.type().isValid())              return Error::Invalid;
        if (_instance.instanceName().isEmpty())       return Error::Invalid;
        if (_instance.hostname().isEmpty())           return Error::Invalid;
        if (_instance.port() == 0)                    return Error::Invalid;
        if (_instance.ipv4Addresses().isEmpty() && _instance.ipv6Addresses().isEmpty()) {
                return Error::Invalid;
        }

        MdnsManager *mgr = effectiveManager();
        if (mgr == nullptr) return Error::NotReady;

        {
                Mutex::Locker lock(_stateMtx);
                _records = composeRecords(_instance);
        }

        _probeIdx.setValue(0);
        _announceIdx.setValue(0);
        _renameAttempts.setValue(0);
        enterState(State::Probing);
        // First probe goes out immediately on the next tick.  We set
        // _nextActionAt to "now" so onManagerTick picks it up the
        // first time the manager fires.
        {
                Mutex::Locker lock(_stateMtx);
                _nextActionAt = TimeStamp::now();
        }
        return Error::Ok;
}

void MdnsPublisher::withdraw() {
        if (state() == State::Idle) return;

        MdnsManager *mgr = effectiveManager();
        List<MdnsRecord> goodbyeSet;
        MdnsServiceInstance forSignal;
        {
                Mutex::Locker lock(_stateMtx);
                goodbyeSet = _records;
                forSignal  = _instance;
                _records.clear();
                _nextActionAt = TimeStamp();
        }
        // Only send a goodbye if we ever got far enough to publish
        // anything.  Probes are unilateral — there's nothing to
        // retract for them.
        if (!goodbyeSet.isEmpty() && mgr != nullptr) {
                enterState(State::Withdrawing);
                Buffer pkt = mdnsBuildGoodbye(goodbyeSet, /*txId*/ 0);
                if (mgr->socket() != nullptr) {
                        mgr->writeMulticast(pkt, /*ipv6*/ false);
                }
                if (mgr->socketV6() != nullptr) {
                        mgr->writeMulticast(pkt, /*ipv6*/ true);
                }
                withdrawnSignal.emit(forSignal);
        }
        enterState(State::Idle);
        _probeIdx.setValue(0);
        _announceIdx.setValue(0);
}

void MdnsPublisher::handlePacket(const Buffer &data, const SocketAddress & /*sender*/,
                                 const NetworkInterface & /*iface*/) {
        switch (state()) {
                case State::Probing:
                        if (detectConflict(data)) {
                                // Two paths out of a probe-window
                                // conflict: auto-rename (RFC 6762 §9
                                // default) re-enters Probing with a
                                // mutated instance label, while
                                // manual-rename mode transitions to
                                // Conflicted and lets the caller
                                // decide.  After @ref MaxRenameAttempts
                                // consecutive conflicts the
                                // auto-rename path also gives up and
                                // falls through to the manual-mode
                                // behaviour — same final state, same
                                // @ref conflict signal.
                                const bool canRename = _autoRename &&
                                        _renameAttempts.value() < MaxRenameAttempts;
                                MdnsServiceInstance oldInstance;
                                MdnsServiceInstance newInstance;
                                if (canRename) {
                                        Mutex::Locker lock(_stateMtx);
                                        oldInstance = _instance;
                                        newInstance = bumpInstanceName(_instance);
                                        _instance   = newInstance;
                                        _records    = composeRecords(_instance);
                                        // Re-arm the probe schedule
                                        // from scratch; the previous
                                        // probe burst is discarded.
                                        _probeIdx.setValue(0);
                                        _nextActionAt = TimeStamp::now();
                                }
                                if (canRename) {
                                        _renameAttempts.fetchAndAdd(1);
                                        renamedSignal.emit(oldInstance, newInstance);
                                        // State stays Probing — the
                                        // next manager tick re-fires
                                        // probe #1 against the new
                                        // name.
                                        return;
                                }
                                MdnsServiceInstance copy;
                                {
                                        Mutex::Locker lock(_stateMtx);
                                        copy = _instance;
                                        _records.clear();
                                        _nextActionAt = TimeStamp();
                                }
                                enterState(State::Conflicted);
                                conflictSignal.emit(copy);
                        }
                        break;
                case State::Published:
                        respondToQueries(data);
                        break;
                case State::Idle:
                case State::Announcing:
                case State::Conflicted:
                case State::Withdrawing:
                        break;
        }
}

void MdnsPublisher::onManagerTick(const TimeStamp &now) {
        if (!isActive()) return;
        advance(now);
}

void MdnsPublisher::enterState(State s) {
        _state.setValue(s);
}

void MdnsPublisher::advance(const TimeStamp &now) {
        TimeStamp nextActionAt;
        State     st = state();
        {
                Mutex::Locker lock(_stateMtx);
                nextActionAt = _nextActionAt;
        }
        if (!nextActionAt.isValid() || now < nextActionAt) return;

        MdnsManager *mgr = effectiveManager();
        if (mgr == nullptr) return;

        if (st == State::Probing) {
                const int sent = _probeIdx.value();
                if (sent < ProbeCount) {
                        // Probe only the names the publisher claims
                        // exclusively (SRV / TXT / A / AAAA).  The
                        // PTR record's owner is the shared service
                        // type FQDN — every publisher of that type
                        // contributes — so including it in the probe
                        // question list would draw legitimate
                        // responses from siblings and look like a
                        // conflict.  RFC 6762 §8.1 calls out that
                        // probing is for "the records they will own".
                        List<MdnsRecord> tentative;
                        {
                                Mutex::Locker lock(_stateMtx);
                                for (const MdnsRecord &r : _records) {
                                        if (r.type() != MdnsRecord::Type::Ptr) tentative += r;
                                }
                        }
                        Buffer pkt = mdnsBuildProbe(tentative, /*txId*/ 0);
                        if (mgr->socket() != nullptr)   mgr->writeMulticast(pkt, false);
                        if (mgr->socketV6() != nullptr) mgr->writeMulticast(pkt, true);
                        _probeIdx.setValue(sent + 1);
                        {
                                Mutex::Locker lock(_stateMtx);
                                _nextActionAt = now + Duration::fromMilliseconds(ProbeIntervalMs);
                        }
                } else {
                        // Probes complete — move to Announcing.  Schedule
                        // the first announce one probe-interval later
                        // (the spec lets us announce immediately but
                        // matching the cadence keeps the wire steady).
                        enterState(State::Announcing);
                        _announceIdx.setValue(0);
                        Mutex::Locker lock(_stateMtx);
                        _nextActionAt = now;
                }
                return;
        }

        if (st == State::Announcing) {
                const int sent = _announceIdx.value();
                if (sent < AnnounceCount) {
                        List<MdnsRecord> full;
                        MdnsServiceInstance forSignal;
                        {
                                Mutex::Locker lock(_stateMtx);
                                full      = _records;
                                forSignal = _instance;
                        }
                        Buffer pkt = mdnsBuildAnnounce(full, /*txId*/ 0);
                        if (mgr->socket() != nullptr)   mgr->writeMulticast(pkt, false);
                        if (mgr->socketV6() != nullptr) mgr->writeMulticast(pkt, true);
                        if (sent == 0) {
                                announcedSignal.emit(forSignal);
                        }
                        _announceIdx.setValue(sent + 1);
                        Mutex::Locker lock(_stateMtx);
                        _nextActionAt = now + Duration::fromMilliseconds(AnnounceIntervalMs);
                } else {
                        // Done announcing.  Move to Published; we
                        // will re-announce on TTL/2 + respond to
                        // queries from here on.
                        enterState(State::Published);
                        Mutex::Locker lock(_stateMtx);
                        // Re-announce on roughly half the TTL of the
                        // smallest record, capped to avoid runaway
                        // arithmetic when TTL is huge.
                        Duration smallest = Duration::fromSeconds(MdnsRecord::DefaultTtlSeconds);
                        for (const MdnsRecord &r : _records) {
                                if (r.ttl().isValid() && r.ttl() < smallest) smallest = r.ttl();
                        }
                        _nextActionAt = now + smallest / 2;
                }
                return;
        }

        if (st == State::Published) {
                // Periodic re-announce — same payload as the initial
                // announce burst, but only one packet per cycle.
                List<MdnsRecord> full;
                {
                        Mutex::Locker lock(_stateMtx);
                        full = _records;
                }
                Buffer pkt = mdnsBuildAnnounce(full, /*txId*/ 0);
                if (mgr->socket() != nullptr)   mgr->writeMulticast(pkt, false);
                if (mgr->socketV6() != nullptr) mgr->writeMulticast(pkt, true);
                Mutex::Locker lock(_stateMtx);
                Duration smallest = Duration::fromSeconds(MdnsRecord::DefaultTtlSeconds);
                for (const MdnsRecord &r : _records) {
                        if (r.ttl().isValid() && r.ttl() < smallest) smallest = r.ttl();
                }
                _nextActionAt = now + smallest / 2;
                return;
        }
}

bool MdnsPublisher::detectConflict(const Buffer &data) {
        auto r = MdnsPacket::parse(data);
        if (!r.second().isOk()) return false;

        // Only ANSWER + AUTHORITY sections constitute claims that
        // would conflict.  Additional-section freebies are
        // informational and do not own the name.
        List<MdnsRecord> tentative;
        {
                Mutex::Locker lock(_stateMtx);
                tentative = _records;
        }
        for (const MdnsParsedRecord &rec : r.first().records()) {
                if (rec.section != MdnsParsedRecord::Section::Answer &&
                    rec.section != MdnsParsedRecord::Section::Authority) continue;
                for (const MdnsRecord &mine : tentative) {
                        if (parsedTypeCode(rec.type) != static_cast<uint16_t>(mine.type())) continue;
                        if (!dnsEquals(rec.name, mine.name())) continue;
                        // Same name + type + class.  For unique-by-name
                        // record types (SRV/TXT/A/AAAA) any mismatch on
                        // payload is a conflict; for SRV we compare
                        // priority + weight + port + target.  PTR is
                        // shared so a parallel PTR is not a conflict.
                        switch (mine.type()) {
                                case MdnsRecord::Type::Srv:
                                        if (rec.srvPriority != mine.srvPriority() ||
                                            rec.srvWeight   != mine.srvWeight()   ||
                                            rec.srvPort     != mine.srvPort()     ||
                                            !dnsEquals(rec.srvTarget, mine.srvTarget())) {
                                                return true;
                                        }
                                        break;
                                case MdnsRecord::Type::Txt:
                                        if (rec.txt != mine.txtRecord()) return true;
                                        break;
                                case MdnsRecord::Type::A:
                                        if (rec.a != mine.aAddress()) return true;
                                        break;
                                case MdnsRecord::Type::Aaaa:
                                        if (rec.aaaa != mine.aaaaAddress()) return true;
                                        break;
                                case MdnsRecord::Type::Ptr:
                                case MdnsRecord::Type::Unknown:
                                        break;
                        }
                }
        }
        return false;
}

void MdnsPublisher::respondToQueries(const Buffer &data) {
        auto r = MdnsPacket::parse(data);
        if (!r.second().isOk()) return;
        const MdnsPacket &pkt = r.first();
        if (pkt.isResponse()) return;  // not a query

        // Match each question against our record set.  We aggregate
        // matches across all questions and emit one response packet
        // with all matches in the Answer section.
        List<MdnsRecord> tentative;
        {
                Mutex::Locker lock(_stateMtx);
                tentative = _records;
        }
        List<MdnsRecord> answers;
        for (const MdnsParsedQuestion &q : pkt.questions()) {
                for (const MdnsRecord &mine : tentative) {
                        const bool nameMatches = dnsEquals(q.name, mine.name());
                        if (!nameMatches) continue;
                        const bool typeMatches =
                            (q.type == QTypeAny) ||
                            (q.type == static_cast<uint16_t>(mine.type()));
                        if (!typeMatches) continue;
                        // Avoid duplicates if the same record matches
                        // multiple questions in a single packet.
                        bool already = false;
                        for (const MdnsRecord &out : answers) {
                                if (out == mine) { already = true; break; }
                        }
                        if (!already) answers += mine;
                }
        }

        // RFC 6762 §7.1 known-answer suppression on the responder
        // side: drop any candidate answer that already appears in
        // the requester's Answer section, but only if the
        // requester's cached TTL is at least half of ours.  When
        // the requester's TTL has aged below the half-life
        // threshold the response is sent anyway to refresh the
        // cache before it expires.  Implementation lives in
        // @ref filterByKnownAnswers so unit tests can pin the
        // contract directly.
        answers = filterByKnownAnswers(answers, data);

        if (answers.isEmpty()) return;

        MdnsManager *mgr = effectiveManager();
        if (mgr == nullptr) return;

        // RFC 6762 §6 jitter: delay 20-120 ms before sending so
        // multiple publishers responding to the same multicast query
        // do not collide on the wire.  Schedule the send on the
        // manager's worker EventLoop so it stays on the same thread
        // as the rest of the manager's IO; gate the callback with
        // an @ref ObjectBasePtr so a publisher tear-down (withdraw
        // or destruction) between scheduling and firing turns the
        // callable into a no-op rather than a use-after-free.
        EventLoop *loop = mgr->threadEventLoop();
        if (loop == nullptr) {
                // No worker loop yet — manager hasn't started, or
                // the platform has no EventLoop facility wired up.
                // Fall back to the synchronous path so the responder
                // still functions, just without the jitter.
                Buffer respPkt = mdnsBuildAnnounce(answers, /*txId*/ pkt.transactionId());
                if (mgr->socket() != nullptr)   mgr->writeMulticast(respPkt, false);
                if (mgr->socketV6() != nullptr) mgr->writeMulticast(respPkt, true);
                return;
        }

        const unsigned int delayMs = static_cast<unsigned int>(
                Random::global().randomInt(static_cast<int>(ResponseJitterMinMs),
                                           static_cast<int>(ResponseJitterMaxMs)));
        ObjectBasePtr<MdnsPublisher> selfPtr(this);
        const uint16_t txId = pkt.transactionId();
        loop->startTimer(delayMs,
                         [selfPtr, mgr, answers, txId]() {
                                 // The publisher may have been
                                 // withdrawn or destroyed by the time
                                 // this timer fires.  Bail silently
                                 // in either case.
                                 const MdnsPublisher *self = selfPtr.data();
                                 if (self == nullptr) return;
                                 if (self->state() != State::Published) return;
                                 Buffer respPkt = mdnsBuildAnnounce(answers, txId);
                                 if (mgr->socket() != nullptr)   mgr->writeMulticast(respPkt, false);
                                 if (mgr->socketV6() != nullptr) mgr->writeMulticast(respPkt, true);
                         },
                         /*singleShot*/ true);
}

List<MdnsRecord> MdnsPublisher::filterByKnownAnswers(const List<MdnsRecord> &answers,
                                                     const Buffer &inboundQuery) {
        auto r = MdnsPacket::parse(inboundQuery);
        if (!r.second().isOk()) return answers;
        const MdnsPacket &pkt = r.first();

        List<MdnsRecord> filtered;
        for (const MdnsRecord &mine : answers) {
                bool suppress = false;
                for (const MdnsParsedRecord &kr : pkt.records()) {
                        if (kr.section != MdnsParsedRecord::Section::Answer) continue;
                        if (parsedTypeCode(kr.type) != static_cast<uint16_t>(mine.type())) continue;
                        if (!dnsEquals(kr.name, mine.name())) continue;
                        // Type-specific content comparison — matches
                        // the conflict-detection table so the
                        // semantics stay consistent.
                        bool contentMatches = false;
                        switch (mine.type()) {
                                case MdnsRecord::Type::Ptr:
                                        contentMatches = dnsEquals(kr.ptrTarget, mine.ptrTarget());
                                        break;
                                case MdnsRecord::Type::Srv:
                                        contentMatches =
                                                kr.srvPriority == mine.srvPriority() &&
                                                kr.srvWeight   == mine.srvWeight()   &&
                                                kr.srvPort     == mine.srvPort()     &&
                                                dnsEquals(kr.srvTarget, mine.srvTarget());
                                        break;
                                case MdnsRecord::Type::Txt:
                                        contentMatches = (kr.txt == mine.txtRecord());
                                        break;
                                case MdnsRecord::Type::A:
                                        contentMatches = (kr.a == mine.aAddress());
                                        break;
                                case MdnsRecord::Type::Aaaa:
                                        contentMatches = (kr.aaaa == mine.aaaaAddress());
                                        break;
                                case MdnsRecord::Type::Unknown:
                                        contentMatches = false;
                                        break;
                        }
                        if (!contentMatches) continue;
                        const int64_t mineTtl  = mine.ttl().seconds();
                        const int64_t knownTtl = kr.ttl.seconds();
                        if (knownTtl * 2 >= mineTtl) {
                                suppress = true;
                                break;
                        }
                }
                if (!suppress) filtered += mine;
        }
        return filtered;
}

MdnsServiceInstance MdnsPublisher::bumpInstanceName(const MdnsServiceInstance &in) {
        // Parse a trailing " (N)" suffix where N is a positive
        // decimal integer.  Anything that does not match — including
        // the original label without any suffix at all — gets a fresh
        // " (2)" appended (Apple's first-rename convention).  This
        // matches Avahi's avahi_alternative_service_name and Bonjour's
        // built-in conflict handler.
        const String &name = in.instanceName();
        String baseName    = name;
        int    nextIndex   = 2;

        const size_t sz = name.size();
        if (sz >= 4 && name[sz - 1] == ')') {
                size_t open = sz - 2;
                size_t end  = sz - 1;        // ')'
                bool   allDigits = true;
                bool   anyDigit  = false;
                while (open > 0) {
                        const char c = name[open];
                        if (c == '(') break;
                        if (c < '0' || c > '9') { allDigits = false; break; }
                        anyDigit = true;
                        --open;
                }
                if (allDigits && anyDigit && open > 0 && name[open] == '(' &&
                    open >= 2 && name[open - 1] == ' ') {
                        // name = "<base> (<digits>)"
                        const String numStr = name.substr(open + 1, end - open - 1);
                        int parsed = 0;
                        for (size_t i = 0; i < numStr.size(); ++i) {
                                parsed = parsed * 10 + (numStr[i] - '0');
                                if (parsed < 0) { parsed = 0; break; }  // overflow guard
                        }
                        if (parsed > 0) {
                                baseName  = name.substr(0, open - 1);
                                nextIndex = parsed + 1;
                        }
                }
        }

        MdnsServiceInstance out = in;
        out.setInstanceName(baseName + " (" + String::number(nextIndex) + ")");
        return out;
}

List<MdnsRecord> MdnsPublisher::composeRecords(const MdnsServiceInstance &i) {
        List<MdnsRecord> out;
        const String typeFqdn   = i.type().toFqdn();
        const String fqdn       = i.fqdn();
        const String host       = i.hostname();
        const Duration ttl      = Duration::fromSeconds(MdnsRecord::DefaultTtlSeconds);
        // PTR : type → instance fqdn.  Shared by every publisher of
        // this type — cache-flush MUST stay off.
        out += MdnsRecord::ptr(typeFqdn, fqdn, ttl);
        // SRV : instance fqdn → host:port.
        out += MdnsRecord::srv(fqdn, host, i.port(), /*prio*/ 0, /*weight*/ 0, ttl);
        // TXT : attributes.
        out += MdnsRecord::txt(fqdn, i.txt(), ttl);
        // A / AAAA per address.
        for (const Ipv4Address &a : i.ipv4Addresses()) {
                out += MdnsRecord::a(host, a, ttl);
        }
        for (const Ipv6Address &a : i.ipv6Addresses()) {
                out += MdnsRecord::aaaa(host, a, ttl);
        }
        return out;
}

PROMEKI_NAMESPACE_END
