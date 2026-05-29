/**
 * @file      mdnspacket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnspacket.h>
#include <promeki/mdnsname.h>
#include <promeki/logger.h>

#include <cstdint>
#include <cstring>

// The vendored mjansson/mdns header is a single header — including
// it pulls in every static inline implementation directly into this
// TU.  That gives us the parser helpers @c mdns_records_parse,
// @c mdns_string_extract, @c mdns_record_parse_* with no extra link
// step.  The header also includes <sys/socket.h> and friends on
// POSIX, so this TU must build with platform-host headers available.
#include <mdns.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr uint16_t CacheFlushBit = 0x8000;
        constexpr uint16_t UnicastBit    = 0x8000;
        constexpr uint16_t ClassMask     = 0x7FFF;

        // Scratch buffer sizes used inside the mjansson callback for
        // name extraction.  RFC 1035 caps a single DNS name at 255
        // bytes including length prefixes — we round up generously to
        // tolerate misencoded inputs without an in-place realloc.
        constexpr size_t NameScratchBytes = 512;
        constexpr size_t TxtScratchEntries = 64;

        // Reads a big-endian 16-bit integer at the given byte offset
        // into the wire buffer.
        uint16_t readU16(const uint8_t *p) {
                return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        }

        // Walks @p buffer's compression chain starting at @p offset
        // and returns the labels' raw bytes as a list.  Uses
        // mjansson's substring iterator directly so the labels stay
        // separated even when one of them contains an embedded
        // @c '.'.  Returns an empty list on parser failure.
        List<String> extractLabels(const void *buffer, size_t bufSize, size_t offset) {
                List<String> out;
                const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
                size_t       cur     = offset;
                unsigned int counter = 0;
                for (;;) {
                        mdns_string_pair_t pair = mdns_get_next_substring(buffer, bufSize, cur);
                        if (pair.offset == MDNS_INVALID_POS || counter++ > MDNS_MAX_SUBSTRINGS) break;
                        if (pair.length == 0) break;   // root label terminator
                        out += String(reinterpret_cast<const char *>(bytes + pair.offset),
                                      pair.length);
                        cur = pair.offset + pair.length;
                }
                return out;
        }

        // Returns @p offset's name as an escape-aware text form.
        // Each label's bytes are escaped (`.` → `\.`, `\\` → `\\\\`)
        // before being joined; the trailing root marker is appended.
        // Returns the empty String when no labels can be extracted.
        String extractName(const void *buffer, size_t bufSize, size_t offset) {
                List<String> labels = extractLabels(buffer, bufSize, offset);
                if (labels.isEmpty()) return String();
                return mdnsJoinName(labels);
        }

        MdnsParsedRecord::Type rTypeFromWire(uint16_t r) {
                switch (r) {
                        case MDNS_RECORDTYPE_A:    return MdnsParsedRecord::Type::A;
                        case MDNS_RECORDTYPE_PTR:  return MdnsParsedRecord::Type::Ptr;
                        case MDNS_RECORDTYPE_TXT:  return MdnsParsedRecord::Type::Txt;
                        case MDNS_RECORDTYPE_AAAA: return MdnsParsedRecord::Type::Aaaa;
                        case MDNS_RECORDTYPE_SRV:  return MdnsParsedRecord::Type::Srv;
                }
                return MdnsParsedRecord::Type::Unknown;
        }

        MdnsParsedRecord::Section sectionFromWire(mdns_entry_type_t e) {
                switch (e) {
                        case MDNS_ENTRYTYPE_QUESTION:   return MdnsParsedRecord::Section::Question;
                        case MDNS_ENTRYTYPE_ANSWER:     return MdnsParsedRecord::Section::Answer;
                        case MDNS_ENTRYTYPE_AUTHORITY:  return MdnsParsedRecord::Section::Authority;
                        case MDNS_ENTRYTYPE_ADDITIONAL: return MdnsParsedRecord::Section::Additional;
                }
                return MdnsParsedRecord::Section::Answer;
        }

        // User-data block threaded through mjansson's record callback.
        struct Sink {
                List<MdnsParsedRecord> *out;
                bool                    parseError = false;
        };

        int recordCallback(int /*sock*/, const struct sockaddr * /*from*/, size_t /*addrlen*/,
                           mdns_entry_type_t entry, uint16_t /*query_id*/, uint16_t rtype,
                           uint16_t rclass, uint32_t ttl, const void *data, size_t size,
                           size_t name_offset, size_t /*name_length*/, size_t record_offset,
                           size_t record_length, void *user_data) {
                Sink *sink = static_cast<Sink *>(user_data);
                if (sink == nullptr || sink->out == nullptr) return 0;

                MdnsParsedRecord rec;
                rec.section    = sectionFromWire(entry);
                rec.type       = rTypeFromWire(rtype);
                rec.cacheFlush = (rclass & CacheFlushBit) != 0;
                rec.ttl        = Duration::fromSeconds(static_cast<int64_t>(ttl));
                rec.name       = extractName(data, size, name_offset);
                // RFC 6762 §10.2: PTR records never carry the cache-
                // flush bit; mask it back to match how mjansson treats
                // it in mdns_record_update_rclass_ttl.
                if (rec.type == MdnsParsedRecord::Type::Ptr) rec.cacheFlush = false;

                switch (rec.type) {
                        case MdnsParsedRecord::Type::Ptr: {
                                // PTR rdata is just the target name —
                                // walk its labels directly so we keep
                                // any embedded @c '.' inside a single
                                // label rather than collapsing it
                                // into a separator on the text form.
                                rec.ptrTarget = extractName(data, size, record_offset);
                                break;
                        }
                        case MdnsParsedRecord::Type::Srv: {
                                // SRV rdata is priority + weight + port
                                // (6 bytes) followed by the target
                                // name.  Pull priority / weight / port
                                // via mjansson then extract the name
                                // ourselves to keep escape semantics
                                // consistent with the PTR path.
                                if (record_length < 6) break;
                                const uint8_t *raw =
                                    static_cast<const uint8_t *>(data) + record_offset;
                                rec.srvPriority = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
                                rec.srvWeight   = static_cast<uint16_t>((raw[2] << 8) | raw[3]);
                                rec.srvPort     = static_cast<uint16_t>((raw[4] << 8) | raw[5]);
                                rec.srvTarget   = extractName(data, size, record_offset + 6);
                                break;
                        }
                        case MdnsParsedRecord::Type::A: {
                                struct sockaddr_in addr;
                                std::memset(&addr, 0, sizeof(addr));
                                mdns_record_parse_a(data, size, record_offset, record_length, &addr);
                                // @c sin_addr.s_addr lives in network
                                // byte order; @ref Ipv4Address packs
                                // octets MSB-first into its internal
                                // uint32.  Build it from the byte
                                // sequence so the host's endianness
                                // never matters.
                                const uint8_t *raw =
                                    reinterpret_cast<const uint8_t *>(&addr.sin_addr.s_addr);
                                rec.a = Ipv4Address(raw[0], raw[1], raw[2], raw[3]);
                                break;
                        }
                        case MdnsParsedRecord::Type::Aaaa: {
                                struct sockaddr_in6 addr;
                                std::memset(&addr, 0, sizeof(addr));
                                mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
                                rec.aaaa = Ipv6Address(addr.sin6_addr.s6_addr);
                                break;
                        }
                        case MdnsParsedRecord::Type::Txt: {
                                // Pull the rdata range straight out
                                // of the source buffer and hand it to
                                // our own TXT decoder so the Presence
                                // (KeyOnly / Empty / Present)
                                // distinction is preserved through the
                                // parse.
                                const uint8_t *bytes = static_cast<const uint8_t *>(data) + record_offset;
                                auto r = MdnsTxtRecord::decode(bytes, record_length);
                                if (r.second().isOk()) {
                                        rec.txt = r.first();
                                } else {
                                        sink->parseError = true;
                                }
                                break;
                        }
                        case MdnsParsedRecord::Type::Unknown:
                                // Forward-compat: keep the name + TTL
                                // so consumers that recognise the
                                // numeric type can still operate.
                                break;
                }

                sink->out->pushToBack(std::move(rec));
                return 0;
        }

} // anonymous namespace

Result<MdnsPacket> mdnsParsePacket(const uint8_t *bytes, size_t len) {
                if (bytes == nullptr) {
                        return makeError<MdnsPacket>(Error::ParseFailed);
                }
                if (len < static_cast<size_t>(MdnsPacket::HeaderSize)) {
                        return makeError<MdnsPacket>(Error::ParseFailed);
                }

                MdnsPacket pkt;
                pkt._id    = readU16(bytes + 0);
                pkt._flags = readU16(bytes + 2);
                uint16_t questions  = readU16(bytes + 4);
                uint16_t answerRrs  = readU16(bytes + 6);
                uint16_t authRrs    = readU16(bytes + 8);
                uint16_t addRrs     = readU16(bytes + 10);

                // Walk the question section by hand — mjansson does
                // the same in @c mdns_query_recv, but does not expose
                // the questions to the callback so we'd lose the QU
                // bit and the per-question type if we leaned on it.
                size_t offset = static_cast<size_t>(MdnsPacket::HeaderSize);
                for (uint16_t i = 0; i < questions; ++i) {
                        size_t       nameStart = offset;
                        const int ok = mdns_string_skip(bytes, len, &offset);
                        if (!ok) return makeError<MdnsPacket>(Error::ParseFailed);
                        if (offset + 4 > len) return makeError<MdnsPacket>(Error::ParseFailed);

                        MdnsParsedQuestion q;
                        q.name             = extractName(bytes, len, nameStart);
                        q.type             = readU16(bytes + offset);
                        uint16_t klass     = readU16(bytes + offset + 2);
                        q.unicastResponse  = (klass & UnicastBit) != 0;
                        offset += 4;
                        pkt._questions.pushToBack(std::move(q));
                }

                Sink sink{ &pkt._records, false };
                size_t cursor = offset;

                mdns_records_parse(/*sock*/ -1, /*from*/ nullptr, /*addrlen*/ 0, bytes, len, &cursor,
                                   MDNS_ENTRYTYPE_ANSWER, pkt._id, answerRrs,
                                   &recordCallback, &sink);
                mdns_records_parse(-1, nullptr, 0, bytes, len, &cursor,
                                   MDNS_ENTRYTYPE_AUTHORITY, pkt._id, authRrs,
                                   &recordCallback, &sink);
                mdns_records_parse(-1, nullptr, 0, bytes, len, &cursor,
                                   MDNS_ENTRYTYPE_ADDITIONAL, pkt._id, addRrs,
                                   &recordCallback, &sink);

                // mjansson's parser quietly stops at the first bad
                // record-length field; the count it returns is the
                // number of successful parses.  We tolerate a partial
                // read of an over-long section the same way Bonjour
                // does and surface the packet anyway; only an outright
                // TXT decoder failure escalates to ParseFailed.
                if (sink.parseError) {
                        return makeError<MdnsPacket>(Error::ParseFailed);
                }

        (void)ClassMask;  // reserved for future class-field
                          // inspection beyond cache-flush.
        return makeResult(std::move(pkt));
}

Result<MdnsPacket> MdnsPacket::parse(const Buffer &b) {
        return mdnsParsePacket(static_cast<const uint8_t *>(b.data()), b.size());
}

Result<MdnsPacket> MdnsPacket::parse(const uint8_t *data, size_t len) {
        return mdnsParsePacket(data, len);
}

PROMEKI_NAMESPACE_END
