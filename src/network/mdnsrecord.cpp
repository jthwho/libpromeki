/**
 * @file      mdnsrecord.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/mdnsname.h>
#include <promeki/mdnsrecord.h>
#include <promeki/set.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Factory methods
// ============================================================================

MdnsRecord MdnsRecord::ptr(const String &owner, const String &target, const Duration &ttl) {
        MdnsRecord r;
        r._type       = Type::Ptr;
        r._name       = owner;
        r._ttl        = ttl;
        r._cacheFlush = false;  // RFC 6762 §10.2 — never on PTR.
        r._ptrTarget  = target;
        return r;
}

MdnsRecord MdnsRecord::srv(const String &owner, const String &target, uint16_t port,
                           uint16_t priority, uint16_t weight, const Duration &ttl) {
        MdnsRecord r;
        r._type        = Type::Srv;
        r._name        = owner;
        r._ttl         = ttl;
        r._cacheFlush  = true;
        r._srvPriority = priority;
        r._srvWeight   = weight;
        r._srvPort     = port;
        r._srvTarget   = target;
        return r;
}

MdnsRecord MdnsRecord::txt(const String &owner, const MdnsTxtRecord &txt, const Duration &ttl) {
        MdnsRecord r;
        r._type       = Type::Txt;
        r._name       = owner;
        r._ttl        = ttl;
        r._cacheFlush = true;
        r._txt        = txt;
        return r;
}

MdnsRecord MdnsRecord::a(const String &owner, const Ipv4Address &address, const Duration &ttl) {
        MdnsRecord r;
        r._type       = Type::A;
        r._name       = owner;
        r._ttl        = ttl;
        r._cacheFlush = true;
        r._a          = address;
        return r;
}

MdnsRecord MdnsRecord::aaaa(const String &owner, const Ipv6Address &address, const Duration &ttl) {
        MdnsRecord r;
        r._type       = Type::Aaaa;
        r._name       = owner;
        r._ttl        = ttl;
        r._cacheFlush = true;
        r._aaaa       = address;
        return r;
}

bool MdnsRecord::operator==(const MdnsRecord &other) const {
        if (_type != other._type)             return false;
        if (_name != other._name)             return false;
        if (_ttl  != other._ttl)              return false;
        if (_cacheFlush != other._cacheFlush) return false;
        switch (_type) {
                case Type::Ptr:  return _ptrTarget == other._ptrTarget;
                case Type::Srv:  return _srvPriority == other._srvPriority &&
                                        _srvWeight   == other._srvWeight   &&
                                        _srvPort     == other._srvPort     &&
                                        _srvTarget   == other._srvTarget;
                case Type::Txt:  return _txt == other._txt;
                case Type::A:    return _a   == other._a;
                case Type::Aaaa: return _aaaa == other._aaaa;
                case Type::Unknown: break;
        }
        return true;
}

// ============================================================================
// Wire encoder.
// ============================================================================
//
// The encoder is intentionally compact: it does not implement DNS
// name-compression pointers.  mDNS senders are not required to
// compress — RFC 1035 §4.1.4 lets the sender choose, and skipping
// compression keeps the encoder small + deterministic + safe against
// the historic "pointer loop" parser bugs that plague compressed
// names.  Receivers (mjansson) handle either form transparently.
//
namespace {

        constexpr uint16_t ClassIN          = 0x0001;
        constexpr uint16_t CacheFlushBit    = 0x8000;
        constexpr uint16_t UnicastRespBit   = 0x8000;

        // Returns the byte length of @p name when encoded as a
        // sequence of length-prefixed labels followed by the
        // trailing zero root marker.  Escape-aware: @c "\\." in the
        // input is treated as a literal byte inside a label rather
        // than a label boundary, per RFC 1035 §5.1.
        size_t encodedNameLen(const String &name) {
                const List<String> labels = mdnsSplitName(name);
                size_t total = 1;   // trailing zero root marker
                for (const String &lab : labels) total += 1 + lab.size();
                return total;
        }

        // Appends the wire encoding of @p name to @p out at @p pos
        // and returns the new position.  Escape-aware (see
        // @ref encodedNameLen).
        size_t writeName(uint8_t *out, size_t pos, const String &name) {
                const List<String> labels = mdnsSplitName(name);
                for (const String &lab : labels) {
                        size_t      n   = lab.size();
                        const char *src = lab.cstr();
                        out[pos++] = static_cast<uint8_t>(n);
                        for (size_t i = 0; i < n; ++i) {
                                out[pos++] = static_cast<uint8_t>(src[i]);
                        }
                }
                out[pos++] = 0;
                return pos;
        }

        void writeU16(uint8_t *out, size_t pos, uint16_t v) {
                out[pos]     = static_cast<uint8_t>(v >> 8);
                out[pos + 1] = static_cast<uint8_t>(v & 0xFF);
        }
        void writeU32(uint8_t *out, size_t pos, uint32_t v) {
                out[pos]     = static_cast<uint8_t>((v >> 24) & 0xFF);
                out[pos + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
                out[pos + 2] = static_cast<uint8_t>((v >>  8) & 0xFF);
                out[pos + 3] = static_cast<uint8_t>(v & 0xFF);
        }

        // Returns the rdata byte length for the record, used to size
        // the buffer and to fill in the rdlength field.
        size_t rdataLen(const MdnsRecord &r) {
                switch (r.type()) {
                        case MdnsRecord::Type::Ptr:  return encodedNameLen(r.ptrTarget());
                        case MdnsRecord::Type::Srv:  return 6 + encodedNameLen(r.srvTarget());
                        case MdnsRecord::Type::Txt:  return r.txtRecord().encode().size();
                        case MdnsRecord::Type::A:    return 4;
                        case MdnsRecord::Type::Aaaa: return 16;
                        case MdnsRecord::Type::Unknown: break;
                }
                return 0;
        }

        // Appends one record to @p out starting at @p pos.  Caller
        // sized the buffer with @ref encodedNameLen + 10 + rdataLen.
        size_t writeRecord(uint8_t *out, size_t pos, const MdnsRecord &r) {
                pos = writeName(out, pos, r.name());

                writeU16(out, pos, static_cast<uint16_t>(r.type()));
                pos += 2;

                uint16_t klass = ClassIN;
                // RFC 6762 §10.2: cache-flush bit is suppressed on PTR.
                if (r.cacheFlush() && r.type() != MdnsRecord::Type::Ptr) klass |= CacheFlushBit;
                writeU16(out, pos, klass);
                pos += 2;

                int64_t ttlSecs = r.ttl().seconds();
                if (ttlSecs < 0) ttlSecs = 0;
                if (ttlSecs > 0xFFFFFFFF) ttlSecs = 0xFFFFFFFF;
                writeU32(out, pos, static_cast<uint32_t>(ttlSecs));
                pos += 4;

                const size_t rdlenPos = pos;
                writeU16(out, pos, 0);   // placeholder, patched after rdata write
                pos += 2;
                const size_t rdataStart = pos;

                switch (r.type()) {
                        case MdnsRecord::Type::Ptr:
                                pos = writeName(out, pos, r.ptrTarget());
                                break;
                        case MdnsRecord::Type::Srv:
                                writeU16(out, pos,     r.srvPriority()); pos += 2;
                                writeU16(out, pos,     r.srvWeight());   pos += 2;
                                writeU16(out, pos,     r.srvPort());     pos += 2;
                                pos = writeName(out, pos, r.srvTarget());
                                break;
                        case MdnsRecord::Type::Txt: {
                                Buffer wire = r.txtRecord().encode();
                                std::memcpy(out + pos, wire.data(), wire.size());
                                pos += wire.size();
                                break;
                        }
                        case MdnsRecord::Type::A: {
                                const uint32_t v = r.aAddress().toUint32();
                                out[pos++] = static_cast<uint8_t>((v >> 24) & 0xFF);
                                out[pos++] = static_cast<uint8_t>((v >> 16) & 0xFF);
                                out[pos++] = static_cast<uint8_t>((v >>  8) & 0xFF);
                                out[pos++] = static_cast<uint8_t>(v & 0xFF);
                                break;
                        }
                        case MdnsRecord::Type::Aaaa: {
                                const uint8_t *raw = r.aaaaAddress().raw();
                                std::memcpy(out + pos, raw, 16);
                                pos += 16;
                                break;
                        }
                        case MdnsRecord::Type::Unknown:
                                break;
                }
                writeU16(out, rdlenPos, static_cast<uint16_t>(pos - rdataStart));
                return pos;
        }

} // anonymous namespace

Buffer mdnsBuildAnnounce(const List<MdnsRecord> &records, uint16_t transactionId) {
        size_t total = 12;  // header
        for (const MdnsRecord &r : records) {
                if (r.type() == MdnsRecord::Type::Unknown) continue;
                total += encodedNameLen(r.name()) + 10 /* type+class+ttl+rdlen */
                       + rdataLen(r);
        }

        Buffer    buf(total);
        uint8_t  *out = static_cast<uint8_t *>(buf.data());

        // Header: QR=1, AA=1 — authoritative response.
        writeU16(out,  0, transactionId);
        writeU16(out,  2, 0x8400);
        writeU16(out,  4, 0);   // qdcount
        uint16_t ancount = 0;
        for (const MdnsRecord &r : records) {
                if (r.type() != MdnsRecord::Type::Unknown) ++ancount;
        }
        writeU16(out,  6, ancount);
        writeU16(out,  8, 0);   // nscount
        writeU16(out, 10, 0);   // arcount

        size_t pos = 12;
        for (const MdnsRecord &r : records) {
                if (r.type() == MdnsRecord::Type::Unknown) continue;
                pos = writeRecord(out, pos, r);
        }
        buf.setSize(pos);
        return buf;
}

Buffer mdnsBuildGoodbye(const List<MdnsRecord> &records, uint16_t transactionId) {
        List<MdnsRecord> zeroed;
        for (const MdnsRecord &r : records) {
                if (r.type() == MdnsRecord::Type::Unknown) continue;
                // Re-create the record with TTL=0; we cannot mutate
                // _ttl directly because every field is private and
                // the only construction path is through the named
                // factories.  Goodbye RR set is small (~4 records per
                // service), the rebuild is cheap.
                MdnsRecord g;
                switch (r.type()) {
                        case MdnsRecord::Type::Ptr:
                                g = MdnsRecord::ptr(r.name(), r.ptrTarget(), Duration::zero());
                                break;
                        case MdnsRecord::Type::Srv:
                                g = MdnsRecord::srv(r.name(), r.srvTarget(), r.srvPort(),
                                                    r.srvPriority(), r.srvWeight(), Duration::zero());
                                break;
                        case MdnsRecord::Type::Txt:
                                g = MdnsRecord::txt(r.name(), r.txtRecord(), Duration::zero());
                                break;
                        case MdnsRecord::Type::A:
                                g = MdnsRecord::a(r.name(), r.aAddress(), Duration::zero());
                                break;
                        case MdnsRecord::Type::Aaaa:
                                g = MdnsRecord::aaaa(r.name(), r.aaaaAddress(), Duration::zero());
                                break;
                        case MdnsRecord::Type::Unknown:
                                continue;
                }
                g.setCacheFlush(r.cacheFlush());
                zeroed += g;
        }
        return mdnsBuildAnnounce(zeroed, transactionId);
}

Buffer mdnsBuildProbe(const List<MdnsRecord> &records, uint16_t transactionId) {
        // RFC 6762 §8.1: one question per distinct owner name, QTYPE
        // = ANY (255), QU bit set.  Authority section carries the
        // tentative records.
        Set<String> uniqueNames;
        for (const MdnsRecord &r : records) {
                if (r.type() != MdnsRecord::Type::Unknown) uniqueNames.insert(r.name());
        }

        size_t total = 12;
        for (const String &name : uniqueNames) {
                total += encodedNameLen(name) + 4;  // qtype + qclass
        }
        for (const MdnsRecord &r : records) {
                if (r.type() == MdnsRecord::Type::Unknown) continue;
                total += encodedNameLen(r.name()) + 10 + rdataLen(r);
        }

        Buffer   buf(total);
        uint8_t *out = static_cast<uint8_t *>(buf.data());
        writeU16(out,  0, transactionId);
        writeU16(out,  2, 0x0000);   // standard query
        writeU16(out,  4, static_cast<uint16_t>(uniqueNames.size()));
        writeU16(out,  6, 0);
        writeU16(out,  8, static_cast<uint16_t>([&]() {
                uint16_t n = 0;
                for (const MdnsRecord &r : records) {
                        if (r.type() != MdnsRecord::Type::Unknown) ++n;
                }
                return n;
        }()));
        writeU16(out, 10, 0);

        size_t pos = 12;
        for (const String &name : uniqueNames) {
                pos = writeName(out, pos, name);
                writeU16(out, pos, 255);              pos += 2;  // QTYPE ANY
                writeU16(out, pos, ClassIN | UnicastRespBit); pos += 2;
        }
        for (const MdnsRecord &r : records) {
                if (r.type() == MdnsRecord::Type::Unknown) continue;
                pos = writeRecord(out, pos, r);
        }
        buf.setSize(pos);
        return buf;
}

PROMEKI_NAMESPACE_END
