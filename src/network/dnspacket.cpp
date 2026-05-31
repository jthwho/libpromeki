/**
 * @file      dnspacket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dnspacket.h>
#include <promeki/dnsname.h>
#include <promeki/logger.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // mDNS-specific bit overlays on the wire CLASS field.
        constexpr uint16_t CacheFlushBit = 0x8000;
        constexpr uint16_t UnicastBit    = 0x8000;
        constexpr uint16_t ClassMask     = 0x7FFF;

        // Big-endian readers for fixed-width integers; the wire form
        // is network byte order throughout.
        inline uint16_t readU16(const uint8_t *p) {
                return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        }
        inline uint32_t readU32(const uint8_t *p) {
                return (static_cast<uint32_t>(p[0]) << 24) |
                       (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8)  |
                       static_cast<uint32_t>(p[3]);
        }
        inline void writeU16(List<uint8_t> &out, uint16_t v) {
                out += static_cast<uint8_t>((v >> 8) & 0xFF);
                out += static_cast<uint8_t>(v & 0xFF);
        }
        inline void writeU32(List<uint8_t> &out, uint32_t v) {
                out += static_cast<uint8_t>((v >> 24) & 0xFF);
                out += static_cast<uint8_t>((v >> 16) & 0xFF);
                out += static_cast<uint8_t>((v >> 8) & 0xFF);
                out += static_cast<uint8_t>(v & 0xFF);
        }

        // Reads a length-prefixed text-string ("character-string", RFC
        // 1035 §3.3).  Returns the bytes (no null-terminator) and
        // advances the cursor.
        Result<String> readCharString(const uint8_t *data, size_t len, size_t &cursor) {
                if (cursor >= len) return makeError<String>(Error::ParseFailed);
                const size_t n = static_cast<size_t>(data[cursor]);
                if (cursor + 1 + n > len) return makeError<String>(Error::ParseFailed);
                String s(reinterpret_cast<const char *>(data + cursor + 1), n);
                cursor += 1 + n;
                return makeResult(std::move(s));
        }

        // Decodes a record's rdata into the appropriate fields on @p rec.
        // @p rdataStart / @p rdataLen point at the *rdata* bytes only;
        // @p data / @p len are the enclosing message (needed for
        // compression-pointer expansion inside rdata for PTR / NS /
        // CNAME / MX / SRV / SOA targets).
        Error parseRdata(DnsRecord &rec,
                         const uint8_t *data, size_t len,
                         size_t rdataStart, size_t rdataLen,
                         bool mdnsMode) {
                const uint8_t *rd = data + rdataStart;
                switch (rec.type) {
                        case DnsRecord::Type::A: {
                                if (rdataLen < 4) return Error(Error::ParseFailed);
                                rec.a = Ipv4Address(rd[0], rd[1], rd[2], rd[3]);
                                break;
                        }
                        case DnsRecord::Type::Aaaa: {
                                if (rdataLen < 16) return Error(Error::ParseFailed);
                                rec.aaaa = Ipv6Address(rd);
                                break;
                        }
                        case DnsRecord::Type::Ptr: {
                                auto r = decodeName(data, len, rdataStart);
                                if (r.second().isError()) return r.second();
                                rec.ptrTarget = r.first().name;
                                break;
                        }
                        case DnsRecord::Type::Cname: {
                                auto r = decodeName(data, len, rdataStart);
                                if (r.second().isError()) return r.second();
                                rec.cnameTarget = r.first().name;
                                // Mirror to ptrTarget for the
                                // historical-mDNS-callers who walked
                                // PTR-style targets; doesn't hurt for
                                // CNAME consumers because they read
                                // cnameTarget directly.
                                rec.ptrTarget   = r.first().name;
                                break;
                        }
                        case DnsRecord::Type::Ns: {
                                auto r = decodeName(data, len, rdataStart);
                                if (r.second().isError()) return r.second();
                                rec.nsTarget  = r.first().name;
                                rec.ptrTarget = r.first().name;
                                break;
                        }
                        case DnsRecord::Type::Mx: {
                                if (rdataLen < 3) return Error(Error::ParseFailed);
                                rec.mxPreference = readU16(rd);
                                auto r           = decodeName(data, len, rdataStart + 2);
                                if (r.second().isError()) return r.second();
                                rec.mxExchange = r.first().name;
                                break;
                        }
                        case DnsRecord::Type::Srv: {
                                if (rdataLen < 7) return Error(Error::ParseFailed);
                                rec.srvPriority = readU16(rd + 0);
                                rec.srvWeight   = readU16(rd + 2);
                                rec.srvPort     = readU16(rd + 4);
                                auto r          = decodeName(data, len, rdataStart + 6);
                                if (r.second().isError()) return r.second();
                                rec.srvTarget = r.first().name;
                                break;
                        }
                        case DnsRecord::Type::Soa: {
                                size_t cursor = rdataStart;
                                auto   m      = decodeName(data, len, cursor);
                                if (m.second().isError()) return m.second();
                                rec.soaMname = m.first().name;
                                cursor       = m.first().nextOffset;
                                auto rn      = decodeName(data, len, cursor);
                                if (rn.second().isError()) return rn.second();
                                rec.soaRname = rn.first().name;
                                cursor       = rn.first().nextOffset;
                                if (cursor + 20 > len) return Error(Error::ParseFailed);
                                rec.soaSerial  = readU32(data + cursor);
                                rec.soaRefresh = readU32(data + cursor + 4);
                                rec.soaRetry   = readU32(data + cursor + 8);
                                rec.soaExpire  = readU32(data + cursor + 12);
                                rec.soaMinimum = readU32(data + cursor + 16);
                                break;
                        }
                        case DnsRecord::Type::Txt: {
#if PROMEKI_ENABLE_MDNS
                                if (mdnsMode) {
                                        auto r = MdnsTxtRecord::decode(rd, rdataLen);
                                        if (r.second().isOk()) rec.txt = r.first();
                                        else return Error(Error::ParseFailed);
                                        break;
                                }
#else
                                (void)mdnsMode;
#endif
                                // Unicast TXT: keep the raw bytes;
                                // callers walk the length-prefixed
                                // string list themselves.
                                break;
                        }
                        case DnsRecord::Type::Naptr: {
                                if (rdataLen < 7) return Error(Error::ParseFailed);
                                rec.naptrOrder      = readU16(rd + 0);
                                rec.naptrPreference = readU16(rd + 2);
                                size_t cursor       = rdataStart + 4;
                                auto   flags        = readCharString(data, len, cursor);
                                if (flags.second().isError()) return flags.second();
                                rec.naptrFlags      = flags.first();
                                auto svc            = readCharString(data, len, cursor);
                                if (svc.second().isError()) return svc.second();
                                rec.naptrService    = svc.first();
                                auto rgx            = readCharString(data, len, cursor);
                                if (rgx.second().isError()) return rgx.second();
                                rec.naptrRegexp     = rgx.first();
                                auto rep            = decodeName(data, len, cursor);
                                if (rep.second().isError()) return rep.second();
                                rec.naptrReplacement = rep.first().name;
                                break;
                        }
                        case DnsRecord::Type::Caa: {
                                if (rdataLen < 2) return Error(Error::ParseFailed);
                                rec.caaFlags        = rd[0];
                                const size_t tagLen = static_cast<size_t>(rd[1]);
                                if (2 + tagLen > rdataLen) return Error(Error::ParseFailed);
                                rec.caaTag   = String(reinterpret_cast<const char *>(rd + 2), tagLen);
                                const size_t valLen = rdataLen - 2 - tagLen;
                                rec.caaValue =
                                    String(reinterpret_cast<const char *>(rd + 2 + tagLen), valLen);
                                break;
                        }
                        default:
                                // Unknown / opaque type — keep the
                                // raw rdata bytes for the caller.
                                break;
                }
                return Error();
        }

        // Common parse path for both unicast DNS and mDNS modes.
        // @p mdnsMode controls the cache-flush / QU bit overlay on
        // the CLASS field and the TXT-decode behaviour.
        Result<DnsPacket> parsePacket(const uint8_t *bytes, size_t len, bool mdnsMode) {
                if (bytes == nullptr || len < static_cast<size_t>(DnsPacket::HeaderSize)) {
                        return makeError<DnsPacket>(Error::ParseFailed);
                }

                DnsPacket pkt;
                pkt.setTransactionId(readU16(bytes + 0));
                pkt.setFlags(readU16(bytes + 2));
                const uint16_t qdcount = readU16(bytes + 4);
                const uint16_t ancount = readU16(bytes + 6);
                const uint16_t nscount = readU16(bytes + 8);
                const uint16_t arcount = readU16(bytes + 10);

                size_t cursor = static_cast<size_t>(DnsPacket::HeaderSize);

                // Questions.
                for (uint16_t i = 0; i < qdcount; ++i) {
                        auto name = decodeName(bytes, len, cursor);
                        if (name.second().isError()) {
                                return makeError<DnsPacket>(Error::ParseFailed);
                        }
                        cursor = name.first().nextOffset;
                        if (cursor + 4 > len) {
                                return makeError<DnsPacket>(Error::ParseFailed);
                        }
                        DnsQuestion q;
                        q.name        = name.first().name;
                        q.type        = readU16(bytes + cursor);
                        const uint16_t klassWire = readU16(bytes + cursor + 2);
                        if (mdnsMode) {
                                q.unicastResponse = (klassWire & UnicastBit) != 0;
                                q.klass           = klassWire & ClassMask;
                        } else {
                                q.klass = klassWire;
                        }
                        cursor += 4;
                        pkt.questions().pushToBack(std::move(q));
                }

                // Records (Answer + Authority + Additional).
                auto parseSection = [&](DnsRecord::Section section, uint16_t count) -> Error {
                        for (uint16_t i = 0; i < count; ++i) {
                                auto name = decodeName(bytes, len, cursor);
                                if (name.second().isError()) return Error(Error::ParseFailed);
                                cursor = name.first().nextOffset;
                                if (cursor + 10 > len) return Error(Error::ParseFailed);

                                DnsRecord rec;
                                rec.section          = section;
                                rec.name             = name.first().name;
                                const uint16_t rt    = readU16(bytes + cursor);
                                const uint16_t kl    = readU16(bytes + cursor + 2);
                                const uint32_t ttl   = readU32(bytes + cursor + 4);
                                const uint16_t rdlen = readU16(bytes + cursor + 8);
                                cursor += 10;
                                rec.type = static_cast<DnsRecord::Type>(rt);
                                rec.ttl  = Duration::fromSeconds(static_cast<int64_t>(ttl));
                                if (mdnsMode) {
                                        rec.cacheFlush = (kl & CacheFlushBit) != 0;
                                        rec.klass      = kl & ClassMask;
                                        // RFC 6762 §10.2: PTR never
                                        // carries the cache-flush bit.
                                        if (rec.type == DnsRecord::Type::Ptr) rec.cacheFlush = false;
                                } else {
                                        rec.klass = kl;
                                }
                                if (cursor + rdlen > len) return Error(Error::ParseFailed);

                                // Keep the raw rdata bytes for
                                // every record — field decoders
                                // populate the typed fields when
                                // they recognise the type.
                                rec.rawRdata.clear();
                                for (uint16_t j = 0; j < rdlen; ++j) {
                                        rec.rawRdata += bytes[cursor + j];
                                }
                                Error e = parseRdata(rec, bytes, len, cursor, rdlen, mdnsMode);
                                if (e.isError()) return e;
                                cursor += rdlen;
                                pkt.records().pushToBack(std::move(rec));
                        }
                        return Error();
                };

                Error e;
                e = parseSection(DnsRecord::Section::Answer,     ancount); if (e.isError()) return makeError<DnsPacket>(e);
                e = parseSection(DnsRecord::Section::Authority,  nscount); if (e.isError()) return makeError<DnsPacket>(e);
                e = parseSection(DnsRecord::Section::Additional, arcount); if (e.isError()) return makeError<DnsPacket>(e);

                return makeResult(std::move(pkt));
        }

        // Builder-side rdata encoder.  Returns Error::Invalid when
        // the record's payload can't be serialised (e.g. label too
        // long, encode failure on an embedded name).
        Error encodeRdata(const DnsRecord &rec, List<uint8_t> &out,
                          DnsNameCompressionMap *dict, bool mdnsMode) {
                switch (rec.type) {
                        case DnsRecord::Type::A: {
                                out += rec.a.octet(0);
                                out += rec.a.octet(1);
                                out += rec.a.octet(2);
                                out += rec.a.octet(3);
                                return Error();
                        }
                        case DnsRecord::Type::Aaaa: {
                                const uint8_t *r = rec.aaaa.raw();
                                for (size_t i = 0; i < 16; ++i) out += r[i];
                                return Error();
                        }
                        case DnsRecord::Type::Ptr:   return encodeName(rec.ptrTarget,   out, dict);
                        case DnsRecord::Type::Cname: return encodeName(rec.cnameTarget.isEmpty() ? rec.ptrTarget : rec.cnameTarget, out, dict);
                        case DnsRecord::Type::Ns:    return encodeName(rec.nsTarget.isEmpty() ? rec.ptrTarget : rec.nsTarget, out, dict);
                        case DnsRecord::Type::Mx: {
                                writeU16(out, rec.mxPreference);
                                return encodeName(rec.mxExchange, out, dict);
                        }
                        case DnsRecord::Type::Srv: {
                                writeU16(out, rec.srvPriority);
                                writeU16(out, rec.srvWeight);
                                writeU16(out, rec.srvPort);
                                // SRV rdata MUST NOT be compressed
                                // per RFC 2782; pass nullptr for the
                                // dictionary so the target name is
                                // emitted in its full uncompressed
                                // form.
                                return encodeName(rec.srvTarget, out, nullptr);
                        }
                        case DnsRecord::Type::Soa: {
                                Error e1 = encodeName(rec.soaMname, out, dict);
                                if (e1.isError()) return e1;
                                Error e2 = encodeName(rec.soaRname, out, dict);
                                if (e2.isError()) return e2;
                                writeU32(out, rec.soaSerial);
                                writeU32(out, rec.soaRefresh);
                                writeU32(out, rec.soaRetry);
                                writeU32(out, rec.soaExpire);
                                writeU32(out, rec.soaMinimum);
                                return Error();
                        }
                        case DnsRecord::Type::Txt: {
#if PROMEKI_ENABLE_MDNS
                                if (mdnsMode) {
                                        Buffer enc = rec.txt.encode();
                                        const uint8_t *eb =
                                            static_cast<const uint8_t *>(enc.data());
                                        for (size_t i = 0; i < enc.size(); ++i) out += eb[i];
                                        return Error();
                                }
#else
                                (void)mdnsMode;
#endif
                                // Unicast TXT: emit the raw rdata
                                // bytes we held on read.  Callers
                                // that build TXT from scratch on
                                // the unicast side must populate
                                // @c rawRdata with the proper
                                // length-prefixed-string form.
                                for (uint8_t b : rec.rawRdata) out += b;
                                return Error();
                        }
                        case DnsRecord::Type::Naptr: {
                                writeU16(out, rec.naptrOrder);
                                writeU16(out, rec.naptrPreference);
                                auto writeStr = [&](const String &s) {
                                        out += static_cast<uint8_t>(s.size() & 0xFF);
                                        const char *p = s.cstr();
                                        const size_t n = (p != nullptr) ? s.size() : 0;
                                        for (size_t i = 0; i < n; ++i) {
                                                out += static_cast<uint8_t>(p[i]);
                                        }
                                };
                                writeStr(rec.naptrFlags);
                                writeStr(rec.naptrService);
                                writeStr(rec.naptrRegexp);
                                // NAPTR replacement is a domain name
                                // and per RFC 3403 §4.1 MUST NOT be
                                // compressed.
                                return encodeName(rec.naptrReplacement, out, nullptr);
                        }
                        case DnsRecord::Type::Caa: {
                                out += rec.caaFlags;
                                out += static_cast<uint8_t>(rec.caaTag.size() & 0xFF);
                                {
                                        const char *p = rec.caaTag.cstr();
                                        const size_t n = (p != nullptr) ? rec.caaTag.size() : 0;
                                        for (size_t i = 0; i < n; ++i) out += static_cast<uint8_t>(p[i]);
                                }
                                {
                                        const char *p = rec.caaValue.cstr();
                                        const size_t n = (p != nullptr) ? rec.caaValue.size() : 0;
                                        for (size_t i = 0; i < n; ++i) out += static_cast<uint8_t>(p[i]);
                                }
                                return Error();
                        }
                        default:
                                // Opaque / unsupported: emit the raw
                                // bytes the caller supplied (likely
                                // empty if they didn't fill them in).
                                for (uint8_t b : rec.rawRdata) out += b;
                                return Error();
                }
        }

} // anonymous namespace

Result<DnsPacket> DnsPacket::parse(const Buffer &b) {
        return parsePacket(static_cast<const uint8_t *>(b.data()), b.size(), /*mdnsMode=*/false);
}

Result<DnsPacket> DnsPacket::parse(const uint8_t *data, size_t len) {
        return parsePacket(data, len, /*mdnsMode=*/false);
}

Result<DnsPacket> DnsPacket::parseMdns(const Buffer &b) {
        return parsePacket(static_cast<const uint8_t *>(b.data()), b.size(), /*mdnsMode=*/true);
}

Result<DnsPacket> DnsPacket::parseMdns(const uint8_t *data, size_t len) {
        return parsePacket(data, len, /*mdnsMode=*/true);
}

List<DnsRecord> DnsPacket::recordsInSection(DnsRecord::Section section) const {
        List<DnsRecord> out;
        for (const DnsRecord &rec : _records) {
                if (rec.section == section) out += rec;
        }
        return out;
}

// ===== Builder ===============================================================

DnsPacket::Builder::Builder() = default;

DnsPacket::Builder &DnsPacket::Builder::setTransactionId(uint16_t id) {
        _id = id;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setResponse(bool r) {
        _flags = (_flags & ~uint16_t{0x8000}) | (r ? uint16_t{0x8000} : uint16_t{0});
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setAuthoritative(bool aa) {
        _flags = (_flags & ~uint16_t{0x0400}) | (aa ? uint16_t{0x0400} : uint16_t{0});
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setTruncated(bool tc) {
        _flags = (_flags & ~uint16_t{0x0200}) | (tc ? uint16_t{0x0200} : uint16_t{0});
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setRecursionDesired(bool rd) {
        _flags = (_flags & ~uint16_t{0x0100}) | (rd ? uint16_t{0x0100} : uint16_t{0});
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setRecursionAvailable(bool ra) {
        _flags = (_flags & ~uint16_t{0x0080}) | (ra ? uint16_t{0x0080} : uint16_t{0});
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setOpcode(uint8_t opcode) {
        _flags = (_flags & ~uint16_t{0x7800}) |
                 (static_cast<uint16_t>(opcode & 0x0F) << 11);
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setRcode(uint8_t rcode) {
        _flags = (_flags & ~uint16_t{0x000F}) | static_cast<uint16_t>(rcode & 0x0F);
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::setCompressionEnabled(bool enable) {
        _compress = enable;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::addQuestion(const String &name, DnsRecord::Type type,
                                                    uint16_t klass, bool unicastResponse) {
        DnsQuestion q;
        q.name             = name;
        q.type             = static_cast<uint16_t>(type);
        q.klass            = klass;
        q.unicastResponse  = unicastResponse;
        _questions += q;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::addQuestion(const DnsQuestion &q) {
        _questions += q;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::addAnswer(const DnsRecord &r) {
        _answers += r;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::addAuthority(const DnsRecord &r) {
        _authority += r;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::addAdditional(const DnsRecord &r) {
        _additional += r;
        return *this;
}

DnsPacket::Builder &DnsPacket::Builder::addEdns0(uint16_t udpPayloadSize, bool doBit) {
        DnsRecord rec;
        rec.type    = DnsRecord::Type::Opt;
        rec.name    = String(".");
        // OPT pseudo-record per RFC 6891: CLASS is overloaded as
        // requestor's UDP payload size, TTL is overloaded as extended
        // RCODE (8 bits) | version (8 bits) | flags (16 bits).
        rec.klass   = udpPayloadSize;
        const uint32_t ttl =
            (static_cast<uint32_t>(0) << 24) |    // extended RCODE
            (static_cast<uint32_t>(0) << 16) |    // EDNS version 0
            (doBit ? static_cast<uint32_t>(0x8000) : static_cast<uint32_t>(0)); // DO bit
        rec.ttl     = Duration::fromSeconds(static_cast<int64_t>(ttl));
        rec.section = DnsRecord::Section::Additional;
        _additional += rec;
        return *this;
}

Buffer DnsPacket::Builder::finish() const {
        _lastEncodeFailed = false;
        List<uint8_t>         out;
        DnsNameCompressionMap dict;
        DnsNameCompressionMap *dp = _compress ? &dict : nullptr;

        // Reserve the header bytes; we'll fill in the counts after
        // encoding each section because the per-section RR counts are
        // taken from the actual encoded list lengths (not from
        // whatever the caller set on the packet shell).
        writeU16(out, _id);
        writeU16(out, _flags);
        writeU16(out, static_cast<uint16_t>(_questions.size()));
        writeU16(out, static_cast<uint16_t>(_answers.size()));
        writeU16(out, static_cast<uint16_t>(_authority.size()));
        writeU16(out, static_cast<uint16_t>(_additional.size()));

        // Question section.
        for (const DnsQuestion &q : _questions) {
                Error e = encodeName(q.name, out, dp);
                if (e.isError()) { _lastEncodeFailed = true; return Buffer(); }
                writeU16(out, q.type);
                const uint16_t klassWire = q.klass | (q.unicastResponse ? UnicastBit : uint16_t{0});
                writeU16(out, klassWire);
        }

        // Helper: encode one record into @p out.
        auto encodeRecord = [&](const DnsRecord &rec) -> Error {
                Error e = encodeName(rec.name, out, dp);
                if (e.isError()) return e;
                // OPT pseudo-records use the special CLASS/TTL
                // overloads written above; everything else uses the
                // normal class + ttl fields, with mDNS cache-flush
                // applied to the wire CLASS where appropriate.
                writeU16(out, static_cast<uint16_t>(rec.type));
                uint16_t klassWire = rec.klass;
                if (rec.cacheFlush && rec.type != DnsRecord::Type::Ptr) {
                        klassWire |= CacheFlushBit;
                }
                writeU16(out, klassWire);
                // TTL: clamp negative to zero, cap to uint32 max.
                int64_t ttlSec = rec.ttl.seconds();
                if (ttlSec < 0) ttlSec = 0;
                if (ttlSec > 0xFFFFFFFFLL) ttlSec = 0xFFFFFFFFLL;
                writeU32(out, static_cast<uint32_t>(ttlSec));
                // Placeholder for the 16-bit RDLENGTH; back-patch
                // once the rdata bytes have been emitted.
                const size_t rdLenPos = out.size();
                writeU16(out, 0);
                const size_t rdStart = out.size();
                // mDNS-mode TXT only matters at parse; on encode we
                // always emit raw bytes (mDNS records will have
                // populated MdnsTxtRecord at build via the helper
                // factories — see encodeRdata).
                Error re = encodeRdata(rec, out, dp, /*mdnsMode=*/true);
                if (re.isError()) return re;
                const size_t rdEnd = out.size();
                const size_t rdLen = rdEnd - rdStart;
                if (rdLen > 0xFFFF) return Error(Error::Invalid);
                // Back-patch.
                out[rdLenPos]     = static_cast<uint8_t>((rdLen >> 8) & 0xFF);
                out[rdLenPos + 1] = static_cast<uint8_t>(rdLen & 0xFF);
                return Error();
        };

        for (const DnsRecord &r : _answers) {
                if (encodeRecord(r).isError()) {
                        _lastEncodeFailed = true;
                        return Buffer();
                }
        }
        for (const DnsRecord &r : _authority) {
                if (encodeRecord(r).isError()) {
                        _lastEncodeFailed = true;
                        return Buffer();
                }
        }
        for (const DnsRecord &r : _additional) {
                if (encodeRecord(r).isError()) {
                        _lastEncodeFailed = true;
                        return Buffer();
                }
        }

        // Copy into a Buffer.
        Buffer buf(out.size());
        if (out.size() > 0) {
                Error ce = buf.copyFrom(&out[0], out.size(), 0);
                if (ce.isError()) {
                        _lastEncodeFailed = true;
                        return Buffer();
                }
        }
        buf.setSize(out.size());
        return buf;
}

PROMEKI_NAMESPACE_END
