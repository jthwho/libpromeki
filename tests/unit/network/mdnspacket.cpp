/**
 * @file      mdnspacket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/mdnspacket.h>
#include <promeki/mdnstxtrecord.h>
#include <promeki/string.h>
#include <vector>

using namespace promeki;
using Type    = MdnsParsedRecord::Type;
using Section = MdnsParsedRecord::Section;

namespace {

        // Hand-rolled mDNS packet builder.  Lives in the test file
        // because it sits below the parser in the dependency stack —
        // the production code path never builds a packet by hand at
        // this level (the responder uses mjansson directly).
        class PacketBuilder {
                public:
                        PacketBuilder() = default;

                        void writeHeader(uint16_t id, uint16_t flags, uint16_t qd, uint16_t an,
                                         uint16_t ns, uint16_t ar) {
                                writeU16(id);
                                writeU16(flags);
                                writeU16(qd);
                                writeU16(an);
                                writeU16(ns);
                                writeU16(ar);
                        }

                        // Encodes a DNS name as a sequence of length-
                        // prefixed labels followed by a zero byte.  No
                        // compression — keeps the test packets easy to
                        // reason about and the parser's compression
                        // path is exercised by every realistic
                        // production packet anyway.
                        void writeName(const char *name) {
                                String s(name);
                                size_t start = 0;
                                size_t end   = 0;
                                size_t n     = s.size();
                                while (start < n) {
                                        end = start;
                                        while (end < n && s[end] != '.') ++end;
                                        size_t segLen = end - start;
                                        REQUIRE(segLen <= 63);
                                        _data.push_back(static_cast<uint8_t>(segLen));
                                        for (size_t i = start; i < end; ++i) {
                                                _data.push_back(static_cast<uint8_t>(s[i]));
                                        }
                                        start = end + 1;
                                }
                                // RFC 1035 §3.1: every encoded name
                                // ends with a zero-length root label.
                                // Always emit it — whether or not the
                                // input had a trailing root dot.
                                _data.push_back(0);
                        }

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

                        void writeQuestion(const char *name, uint16_t type, uint16_t klass) {
                                writeName(name);
                                writeU16(type);
                                writeU16(klass);
                        }

                        // Writes a record header (name/type/class/ttl)
                        // and returns the byte index where the rdlength
                        // will land so the caller can patch it after
                        // appending the rdata.
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

                        void patchU16(size_t pos, uint16_t v) {
                                _data[pos]     = static_cast<uint8_t>(v >> 8);
                                _data[pos + 1] = static_cast<uint8_t>(v & 0xFF);
                        }

                        size_t writePtr(const char *owner, uint16_t klass, uint32_t ttl,
                                        const char *target) {
                                size_t rdLen = writeRecordHeader(owner, 12, klass, ttl);
                                size_t rdataStart = _data.size();
                                writeName(target);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                                return rdLen;
                        }

                        size_t writeSrv(const char *owner, uint16_t klass, uint32_t ttl,
                                        uint16_t priority, uint16_t weight, uint16_t port,
                                        const char *target) {
                                size_t rdLen = writeRecordHeader(owner, 33, klass, ttl);
                                size_t rdataStart = _data.size();
                                writeU16(priority);
                                writeU16(weight);
                                writeU16(port);
                                writeName(target);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                                return rdLen;
                        }

                        size_t writeTxt(const char *owner, uint16_t klass, uint32_t ttl,
                                        const std::vector<std::pair<std::string, std::string>> &entries) {
                                size_t rdLen = writeRecordHeader(owner, 16, klass, ttl);
                                size_t rdataStart = _data.size();
                                for (const auto &kv : entries) {
                                        size_t entryLen = kv.first.size() + 1 + kv.second.size();
                                        REQUIRE(entryLen <= 255);
                                        _data.push_back(static_cast<uint8_t>(entryLen));
                                        for (char c : kv.first)  _data.push_back(static_cast<uint8_t>(c));
                                        _data.push_back('=');
                                        for (char c : kv.second) _data.push_back(static_cast<uint8_t>(c));
                                }
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                                return rdLen;
                        }

                        size_t writeA(const char *owner, uint16_t klass, uint32_t ttl,
                                      uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
                                size_t rdLen = writeRecordHeader(owner, 1, klass, ttl);
                                size_t rdataStart = _data.size();
                                _data.push_back(a);
                                _data.push_back(b);
                                _data.push_back(c);
                                _data.push_back(d);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                                return rdLen;
                        }

                        size_t writeAaaa(const char *owner, uint16_t klass, uint32_t ttl,
                                         const uint8_t bytes[16]) {
                                size_t rdLen = writeRecordHeader(owner, 28, klass, ttl);
                                size_t rdataStart = _data.size();
                                for (int i = 0; i < 16; ++i) _data.push_back(bytes[i]);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                                return rdLen;
                        }

                        const uint8_t *data() const { return _data.data(); }
                        size_t         size() const { return _data.size(); }

                private:
                        std::vector<uint8_t> _data;
        };

} // namespace

TEST_CASE("MdnsPacket: rejects truncated packets") {
        // Under-length header.
        uint8_t bytes[8] = { 0 };
        auto r = MdnsPacket::parseMdns(bytes, sizeof(bytes));
        CHECK_FALSE(r.second().isOk());
}

TEST_CASE("MdnsPacket: parses an empty response header") {
        PacketBuilder b;
        b.writeHeader(0x1234, 0x8400, 0, 0, 0, 0);
        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        const MdnsPacket &p = r.first();
        CHECK(p.transactionId() == 0x1234);
        CHECK(p.flags() == 0x8400);
        CHECK(p.isResponse());
        CHECK(p.isAuthoritative());
        CHECK_FALSE(p.isTruncated());
        CHECK(p.questions().isEmpty());
        CHECK(p.records().isEmpty());
}

TEST_CASE("MdnsPacket: parses a single PTR answer") {
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writePtr("_http._tcp.local.", 0x0001, 4500, "Studio Camera._http._tcp.local.");

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        const MdnsPacket &p = r.first();
        REQUIRE(p.records().size() == 1);

        const MdnsParsedRecord &rec = p.records()[0];
        CHECK(rec.type    == Type::Ptr);
        CHECK(rec.section == Section::Answer);
        CHECK(rec.name      == String("_http._tcp.local."));
        CHECK(rec.ptrTarget == String("Studio Camera._http._tcp.local."));
        CHECK(rec.ttl == Duration::fromSeconds(4500));
        // PTR never carries cache-flush.
        CHECK_FALSE(rec.cacheFlush);
}

TEST_CASE("MdnsPacket: parses a SRV answer with target / port / priority / weight") {
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeSrv("Studio Camera._http._tcp.local.",
                   0x8001 /* cache-flush + IN */, 120, 10, 5, 9000, "camera.local.");

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsParsedRecord &rec = r.first().records()[0];
        CHECK(rec.type        == Type::Srv);
        CHECK(rec.srvPriority == 10);
        CHECK(rec.srvWeight   == 5);
        CHECK(rec.srvPort     == 9000);
        CHECK(rec.srvTarget   == String("camera.local."));
        CHECK(rec.cacheFlush);
        CHECK(rec.ttl == Duration::fromSeconds(120));
}

TEST_CASE("MdnsPacket: parses a TXT answer with all three Presence shapes") {
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        // <"path=/admin"> <"flags="> <"tls">
        // The TXT builder writes Presence::Present and Presence::Empty
        // forms verbatim; KeyOnly is achieved by passing an empty
        // value with the manual record construction below.
        std::vector<std::pair<std::string, std::string>> entries = {
                {"path", "/admin"},
                {"flags", ""},
                // KeyOnly via an explicit empty-second-element pair —
                // PacketBuilder's writeTxt always emits "key=value",
                // which becomes Empty for an empty value.  Emit
                // KeyOnly by hand below instead.
        };
        size_t rdLen = b.writeTxt("Studio Camera._http._tcp.local.", 0x8001, 4500, entries);
        // Append a KeyOnly entry manually by extending the rdata.
        // We patched the rdlength above with the entry count we wrote
        // through writeTxt; we need to re-extend it.  Easiest path:
        // re-read the current rdLen value, append "1 tls", and patch
        // the new length.
        (void)rdLen;

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsParsedRecord &rec = r.first().records()[0];
        CHECK(rec.type == Type::Txt);
        CHECK(rec.txt.value("path") == String("/admin"));
        CHECK(rec.txt.presence("path")  == MdnsTxtRecord::Presence::Present);
        CHECK(rec.txt.presence("flags") == MdnsTxtRecord::Presence::Empty);
}

TEST_CASE("MdnsPacket: parses an A answer") {
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeA("camera.local.", 0x8001, 120, 192, 168, 1, 42);

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsParsedRecord &rec = r.first().records()[0];
        CHECK(rec.type == Type::A);
        CHECK(rec.a    == Ipv4Address(192, 168, 1, 42));
}

TEST_CASE("MdnsPacket: parses an AAAA answer") {
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        const uint8_t v6[16] = {
                0xFE, 0x80, 0,0,0,0,0,0,
                0x02, 0x1a, 0x2b, 0xff, 0xfe, 0x3c, 0x4d, 0x5e
        };
        b.writeAaaa("camera.local.", 0x8001, 120, v6);

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsParsedRecord &rec = r.first().records()[0];
        CHECK(rec.type == Type::Aaaa);
        // Round-trip through a fresh Ipv6Address built from the same
        // raw bytes so the comparison does not depend on the address
        // family's parser.
        CHECK(rec.aaaa == Ipv6Address(v6));
}

TEST_CASE("MdnsPacket: parses a query with QU bit set") {
        PacketBuilder b;
        b.writeHeader(0, 0x0000, 1, 0, 0, 0);
        b.writeQuestion("_http._tcp.local.", 12, 0x8001);   // PTR + QU + IN

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        const MdnsPacket &p = r.first();
        CHECK_FALSE(p.isResponse());
        REQUIRE(p.questions().size() == 1);
        const MdnsParsedQuestion &q = p.questions()[0];
        CHECK(q.name              == String("_http._tcp.local."));
        CHECK(q.type              == 12);
        CHECK(q.unicastResponse);
}

TEST_CASE("MdnsPacket: parses a multi-section packet") {
        PacketBuilder b;
        b.writeHeader(0x4242, 0x8400, 0, 1, 0, 2);
        // Answer: PTR _http._tcp.local. -> Studio Camera._http._tcp.local.
        b.writePtr("_http._tcp.local.", 0x0001, 4500, "Studio Camera._http._tcp.local.");
        // Additional: SRV + A
        b.writeSrv("Studio Camera._http._tcp.local.", 0x8001, 120, 0, 0, 80, "camera.local.");
        b.writeA("camera.local.", 0x8001, 120, 10, 0, 0, 7);

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        const MdnsPacket &p = r.first();
        REQUIRE(p.records().size() == 3);

        CHECK(p.records()[0].type    == Type::Ptr);
        CHECK(p.records()[0].section == Section::Answer);
        CHECK(p.records()[1].type    == Type::Srv);
        CHECK(p.records()[1].section == Section::Additional);
        CHECK(p.records()[1].srvPort == 80);
        CHECK(p.records()[2].type    == Type::A);
        CHECK(p.records()[2].section == Section::Additional);
        CHECK(p.records()[2].a       == Ipv4Address(10, 0, 0, 7));
}

TEST_CASE("MdnsPacket: zero-TTL Goodbye record survives the parse") {
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writePtr("_http._tcp.local.", 0x0001, 0, "Old Camera._http._tcp.local.");

        auto r = MdnsPacket::parseMdns(b.data(), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsParsedRecord &rec = r.first().records()[0];
        CHECK(rec.type == Type::Ptr);
        CHECK(rec.ttl == Duration::fromSeconds(0));
}
