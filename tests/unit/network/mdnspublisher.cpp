/**
 * @file      mdnspublisher.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/mdnspacket.h>
#include <promeki/mdnspublisher.h>
#include <promeki/mdnsrecord.h>
#include <promeki/objectbase.tpp>

using namespace promeki;

namespace {

        // Minimal packet builder mirroring the browser/manager test
        // helpers.  Used to feed conflict/query packets into the
        // publisher via handlePacket.
        class PacketBuilder {
                public:
                        void writeU16(uint16_t v) {
                                _data.push_back(static_cast<uint8_t>(v >> 8));
                                _data.push_back(static_cast<uint8_t>(v & 0xFF));
                        }
                        void writeU32(uint32_t v) {
                                _data.push_back(static_cast<uint8_t>(v >> 24));
                                _data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                                _data.push_back(static_cast<uint8_t>((v >> 8)  & 0xFF));
                                _data.push_back(static_cast<uint8_t>(v & 0xFF));
                        }
                        void writeName(const char *name) {
                                String s(name);
                                size_t start = 0;
                                size_t n     = s.size();
                                while (start < n) {
                                        size_t end = start;
                                        while (end < n && s[end] != '.') ++end;
                                        size_t segLen = end - start;
                                        _data.push_back(static_cast<uint8_t>(segLen));
                                        for (size_t i = start; i < end; ++i) {
                                                _data.push_back(static_cast<uint8_t>(s[i]));
                                        }
                                        start = end + 1;
                                }
                                _data.push_back(0);
                        }
                        void patchU16(size_t pos, uint16_t v) {
                                _data[pos]     = static_cast<uint8_t>(v >> 8);
                                _data[pos + 1] = static_cast<uint8_t>(v & 0xFF);
                        }
                        void writeHeader(uint16_t id, uint16_t flags, uint16_t qd, uint16_t an,
                                         uint16_t ns, uint16_t ar) {
                                writeU16(id); writeU16(flags); writeU16(qd);
                                writeU16(an); writeU16(ns); writeU16(ar);
                        }
                        size_t writeRecordHeader(const char *name, uint16_t type, uint16_t klass,
                                                 uint32_t ttl) {
                                writeName(name);
                                writeU16(type);
                                writeU16(klass);
                                writeU32(ttl);
                                size_t rdLengthPos = _data.size();
                                writeU16(0);
                                return rdLengthPos;
                        }
                        void writeSrv(const char *owner, uint16_t klass, uint32_t ttl,
                                      uint16_t prio, uint16_t weight, uint16_t port,
                                      const char *target) {
                                size_t rdLen = writeRecordHeader(owner, 33, klass, ttl);
                                size_t rdataStart = _data.size();
                                writeU16(prio); writeU16(weight); writeU16(port);
                                writeName(target);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                        }
                        void writePtr(const char *owner, uint16_t klass, uint32_t ttl,
                                      const char *target) {
                                size_t rdLen = writeRecordHeader(owner, 12, klass, ttl);
                                size_t rdataStart = _data.size();
                                writeName(target);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                        }
                        void writeQuestion(const char *name, uint16_t qtype, uint16_t qclass) {
                                writeName(name);
                                writeU16(qtype);
                                writeU16(qclass);
                        }
                        Buffer toBuffer() const {
                                Buffer b(_data.size());
                                std::memcpy(b.data(), _data.data(), _data.size());
                                b.setSize(_data.size());
                                return b;
                        }

                private:
                        std::vector<uint8_t> _data;
        };

        MdnsServiceInstance makeInstance() {
                MdnsServiceInstance inst;
                inst.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
                inst.setInstanceName("Studio Camera");
                inst.setHostname("camera.local.");
                inst.setPort(8080);
                List<Ipv4Address> v4;
                v4 += Ipv4Address(192, 168, 1, 7);
                inst.setIpv4Addresses(v4);
                return inst;
        }

        SocketAddress dummySender() {
                return SocketAddress(Ipv4Address(192, 168, 1, 1), 5353);
        }

} // namespace

TEST_CASE("MdnsPublisher: default state is Idle") {
        MdnsPublisher p;
        CHECK(p.state() == MdnsPublisher::State::Idle);
        CHECK_FALSE(p.isActive());
}

TEST_CASE("MdnsPublisher: publish rejects incomplete instances") {
        MdnsPublisher p;

        // No type / instance / hostname / port → Invalid every time.
        CHECK(p.publish() == Error::Invalid);

        MdnsServiceInstance inst;
        inst.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        p.setInstance(inst);
        CHECK(p.publish() == Error::Invalid);   // no instance name

        inst.setInstanceName("X");
        p.setInstance(inst);
        CHECK(p.publish() == Error::Invalid);   // no hostname

        inst.setHostname("host.local.");
        p.setInstance(inst);
        CHECK(p.publish() == Error::Invalid);   // no port

        inst.setPort(80);
        p.setInstance(inst);
        CHECK(p.publish() == Error::Invalid);   // no addresses
}

TEST_CASE("MdnsPublisher: composeRecords builds PTR+SRV+TXT+A") {
        // We can't call composeRecords directly (it's private) but
        // we can publish() with a complete instance and inspect the
        // records() snapshot.  The publisher needs a manager to
        // accept publish(); the Application's lazy global covers it
        // (the test thread may not have one — we tolerate NotReady).
        MdnsPublisher p;
        p.setInstance(makeInstance());
        Error err = p.publish();
        if (err == Error::NotReady) {
                // No application manager available in this test
                // process — covered by an integration test where a
                // live MdnsManager is provided.
                return;
        }
        REQUIRE(err.isOk());

        List<MdnsRecord> recs = p.records();
        REQUIRE(recs.size() == 4);
        // Order matters: PTR, SRV, TXT, A.
        CHECK(recs[0].type() == MdnsRecord::Type::Ptr);
        CHECK(recs[1].type() == MdnsRecord::Type::Srv);
        CHECK(recs[2].type() == MdnsRecord::Type::Txt);
        CHECK(recs[3].type() == MdnsRecord::Type::A);
        CHECK(recs[1].srvPort() == 8080);
        CHECK(recs[3].aAddress() == Ipv4Address(192, 168, 1, 7));
        p.withdraw();
}

TEST_CASE("MdnsPublisher: conflict signal fires when a probe-window packet "
          "claims our SRV target (manual-rename mode)") {
        MdnsPublisher p;
        p.setInstance(makeInstance());
        p.setAutoRename(false);               // route conflicts to conflictSignal
        Error err = p.publish();
        if (err == Error::NotReady) return;   // see composeRecords test
        REQUIRE(err.isOk());

        std::atomic<int>    conflictCount{0};
        MdnsServiceInstance conflicted;
        p.conflictSignal.connect([&](MdnsServiceInstance i) {
                conflictCount.fetch_add(1);
                conflicted = i;
        }, &p);

        // Hand-craft a response packet that lays claim to our
        // intended SRV name with a DIFFERENT target / port.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        // Same owner ("Studio Camera._http._tcp.local."), same SRV
        // type and class, different target host + port — that's a
        // conflict.
        b.writeSrv("Studio Camera._http._tcp.local.", 0x8001, 120,
                   0, 0, 9999, "other.local.");

        p.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());
        CHECK(conflictCount.load() == 1);
        CHECK(p.state() == MdnsPublisher::State::Conflicted);
        CHECK_FALSE(p.isActive());
}

TEST_CASE("MdnsPublisher: auto-rename appends \"(2)\" on first conflict") {
        MdnsPublisher p;
        p.setInstance(makeInstance());        // auto-rename on by default
        Error err = p.publish();
        if (err == Error::NotReady) return;
        REQUIRE(err.isOk());

        std::atomic<int>    renameCount{0};
        std::atomic<int>    conflictCount{0};
        MdnsServiceInstance lastNew;
        p.renamedSignal.connect([&](MdnsServiceInstance /*oldI*/, MdnsServiceInstance newI) {
                renameCount.fetch_add(1);
                lastNew = newI;
        }, &p);
        p.conflictSignal.connect([&](MdnsServiceInstance) {
                conflictCount.fetch_add(1);
        }, &p);

        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeSrv("Studio Camera._http._tcp.local.", 0x8001, 120,
                   0, 0, 9999, "other.local.");

        p.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());
        CHECK(renameCount.load() == 1);
        CHECK(conflictCount.load() == 0);
        CHECK(lastNew.instanceName() == String("Studio Camera (2)"));
        // Still active and re-probing under the new name.
        CHECK(p.state() == MdnsPublisher::State::Probing);
        CHECK(p.instance().instanceName() == String("Studio Camera (2)"));
}

TEST_CASE("MdnsPublisher: auto-rename suffix increments (\"(2)\" → \"(3)\")") {
        MdnsPublisher p;
        MdnsServiceInstance inst = makeInstance();
        inst.setInstanceName("Studio Camera (5)");   // existing suffix
        p.setInstance(inst);
        Error err = p.publish();
        if (err == Error::NotReady) return;
        REQUIRE(err.isOk());

        MdnsServiceInstance lastNew;
        p.renamedSignal.connect([&](MdnsServiceInstance, MdnsServiceInstance newI) {
                lastNew = newI;
        }, &p);

        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeSrv("Studio Camera (5)._http._tcp.local.", 0x8001, 120,
                   0, 0, 9999, "other.local.");

        p.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());
        CHECK(lastNew.instanceName() == String("Studio Camera (6)"));
}

TEST_CASE("MdnsPublisher: auto-rename gives up after MaxRenameAttempts and emits conflict") {
        MdnsPublisher p;
        p.setInstance(makeInstance());
        Error err = p.publish();
        if (err == Error::NotReady) return;
        REQUIRE(err.isOk());

        std::atomic<int> renameCount{0};
        std::atomic<int> conflictCount{0};
        p.renamedSignal.connect([&](MdnsServiceInstance, MdnsServiceInstance) {
                renameCount.fetch_add(1);
        }, &p);
        p.conflictSignal.connect([&](MdnsServiceInstance) {
                conflictCount.fetch_add(1);
        }, &p);

        // Feed conflicts targeting whichever name the publisher
        // currently holds — auto-rename mutates after each, so we
        // need to follow the bumping suffix.
        for (int i = 0; i <= MdnsPublisher::MaxRenameAttempts; ++i) {
                const String fqdn = p.instance().fqdn();
                PacketBuilder b;
                b.writeHeader(0, 0x8400, 0, 1, 0, 0);
                b.writeSrv(fqdn.cstr(), 0x8001, 120, 0, 0, 9999, "other.local.");
                p.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());
        }
        CHECK(renameCount.load() == MdnsPublisher::MaxRenameAttempts);
        CHECK(conflictCount.load() == 1);
        CHECK(p.state() == MdnsPublisher::State::Conflicted);
}

// ============================================================================
// Known-answer suppression (RFC 6762 §7.1) — responder side
// ============================================================================
//
// filterByKnownAnswers is a static helper so the suppression contract
// can be pinned without standing up a full @ref MdnsManager.  The
// jittered send path inside respondToQueries routes through this
// helper, so what is tested here is what fires on the wire.

namespace {

        // Builds a minimal query packet: one question against
        // @p qname / @p qtype plus a single PTR known-answer record
        // (Answer section) for @p kasOwner → @p kasTarget with the
        // requested TTL.  Mirrors how MdnsManager::buildQuery-
        // WithKnownAnswers stamps the wire after the RFC 6762 §7.1
        // fix.
        Buffer queryWithKnownPtr(const char *qname, uint16_t qtype,
                                 const char *kasOwner, const char *kasTarget,
                                 uint32_t ttlSeconds) {
                PacketBuilder b;
                b.writeHeader(/*id*/ 0, /*flags*/ 0x0000,
                              /*qd*/ 1, /*an*/ 1, /*ns*/ 0, /*ar*/ 0);
                b.writeQuestion(qname, qtype, /*class IN*/ 0x0001);
                b.writePtr(kasOwner, /*class IN*/ 0x0001, ttlSeconds, kasTarget);
                return b.toBuffer();
        }

} // namespace

TEST_CASE("MdnsPublisher::filterByKnownAnswers suppresses a matching PTR "
          "with fresh TTL") {
        List<MdnsRecord> ours;
        ours += MdnsRecord::ptr("_http._tcp.local.",
                                "Studio Camera._http._tcp.local.",
                                Duration::fromSeconds(120));

        // Known answer carries the same owner / target with a TTL
        // (90 s) > half of ours (60 s).
        Buffer q = queryWithKnownPtr("_http._tcp.local.", /*PTR*/ 12,
                                     "_http._tcp.local.",
                                     "Studio Camera._http._tcp.local.",
                                     /*ttl*/ 90);
        List<MdnsRecord> filtered = MdnsPublisher::filterByKnownAnswers(ours, q);
        CHECK(filtered.isEmpty());
}

TEST_CASE("MdnsPublisher::filterByKnownAnswers passes the answer through when "
          "the requester's cached TTL is aging below the half-life") {
        List<MdnsRecord> ours;
        ours += MdnsRecord::ptr("_http._tcp.local.",
                                "Studio Camera._http._tcp.local.",
                                Duration::fromSeconds(120));
        // Known answer has TTL 30 s — less than half of ours (60 s)
        // — so the responder still answers to refresh the cache.
        Buffer q = queryWithKnownPtr("_http._tcp.local.", /*PTR*/ 12,
                                     "_http._tcp.local.",
                                     "Studio Camera._http._tcp.local.",
                                     /*ttl*/ 30);
        List<MdnsRecord> filtered = MdnsPublisher::filterByKnownAnswers(ours, q);
        REQUIRE(filtered.size() == 1);
        CHECK(filtered[0].type() == MdnsRecord::Type::Ptr);
}

TEST_CASE("MdnsPublisher::filterByKnownAnswers passes through a different "
          "PTR target (no content match)") {
        List<MdnsRecord> ours;
        ours += MdnsRecord::ptr("_http._tcp.local.",
                                "Studio Camera._http._tcp.local.",
                                Duration::fromSeconds(120));
        // Known answer for a SIBLING instance — same type+name but
        // different rdata, so it does NOT cover ours.
        Buffer q = queryWithKnownPtr("_http._tcp.local.", /*PTR*/ 12,
                                     "_http._tcp.local.",
                                     "Other Camera._http._tcp.local.",
                                     /*ttl*/ 120);
        List<MdnsRecord> filtered = MdnsPublisher::filterByKnownAnswers(ours, q);
        REQUIRE(filtered.size() == 1);
        CHECK(filtered[0].type() == MdnsRecord::Type::Ptr);
}

TEST_CASE("MdnsPublisher::filterByKnownAnswers ignores known answers placed "
          "in the Authority section") {
        // RFC 6762 §7.1 confines known answers to the Answer
        // section.  An identical record placed in Authority (or
        // Additional) must not suppress the response — exercising
        // this guards against accidentally treating those sections
        // as authoritative for KAS.
        List<MdnsRecord> ours;
        ours += MdnsRecord::ptr("_http._tcp.local.",
                                "Studio Camera._http._tcp.local.",
                                Duration::fromSeconds(120));
        PacketBuilder b;
        b.writeHeader(0, 0x0000, 1, 0, 1, 0);  // qd=1, ns=1
        b.writeName("_http._tcp.local.");
        b.writeU16(12);
        b.writeU16(0x0001);
        // Same record but in Authority — should NOT suppress.
        b.writePtr("_http._tcp.local.", 0x0001, 120,
                   "Studio Camera._http._tcp.local.");
        List<MdnsRecord> filtered =
                MdnsPublisher::filterByKnownAnswers(ours, b.toBuffer());
        REQUIRE(filtered.size() == 1);
}
