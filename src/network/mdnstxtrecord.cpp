/**
 * @file      mdnstxtrecord.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnstxtrecord.h>
#include <promeki/datastream.h>
#include <promeki/textstream.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

int MdnsTxtRecord::findEntry(const String &key) const {
        const int n = static_cast<int>(_entries.size());
        for (int i = 0; i < n; ++i) {
                if (_entries[i].key.compareIgnoreCase(key) == 0) return i;
        }
        return -1;
}

void MdnsTxtRecord::set(const String &key, const String &value) {
        int idx = findEntry(key);
        if (idx >= 0) {
                // Preserve original casing of the key; only the
                // value and presence flip on update.
                _entries[idx].presence = Presence::Present;
                _entries[idx].value    = value;
                return;
        }
        Entry e;
        e.key      = key;
        e.presence = Presence::Present;
        e.value    = value;
        _entries.pushToBack(std::move(e));
}

void MdnsTxtRecord::setKey(const String &key) {
        int idx = findEntry(key);
        if (idx >= 0) {
                _entries[idx].presence = Presence::KeyOnly;
                _entries[idx].value    = String();
                return;
        }
        Entry e;
        e.key      = key;
        e.presence = Presence::KeyOnly;
        _entries.pushToBack(std::move(e));
}

void MdnsTxtRecord::setEmpty(const String &key) {
        int idx = findEntry(key);
        if (idx >= 0) {
                _entries[idx].presence = Presence::Empty;
                _entries[idx].value    = String();
                return;
        }
        Entry e;
        e.key      = key;
        e.presence = Presence::Empty;
        _entries.pushToBack(std::move(e));
}

void MdnsTxtRecord::remove(const String &key) {
        int idx = findEntry(key);
        if (idx < 0) return;
        _entries.remove(idx);
}

MdnsTxtRecord::Presence MdnsTxtRecord::presence(const String &key) const {
        int idx = findEntry(key);
        if (idx < 0) return Presence::Absent;
        return _entries[idx].presence;
}

String MdnsTxtRecord::value(const String &key, const String &defaultValue) const {
        int idx = findEntry(key);
        if (idx < 0) return defaultValue;
        // KeyOnly and Empty contribute no value bytes — both surface
        // as empty String to callers that don't consult presence().
        return _entries[idx].value;
}

StringList MdnsTxtRecord::keys() const {
        StringList out;
        for (const Entry &e : _entries) out += e.key;
        return out;
}

void MdnsTxtRecord::forEach(Function<void(const String &, Presence, const String &)> func) const {
        if (!func) return;
        for (const Entry &e : _entries) {
                func(e.key, e.presence, e.value);
        }
}

bool MdnsTxtRecord::operator==(const MdnsTxtRecord &other) const {
        if (_entries.size() != other._entries.size()) return false;
        const int n = static_cast<int>(_entries.size());
        for (int i = 0; i < n; ++i) {
                const Entry &a = _entries[i];
                const Entry &b = other._entries[i];
                if (a.presence != b.presence) return false;
                if (a.key.compareIgnoreCase(b.key) != 0) return false;
                if (a.presence == Presence::Present && a.value != b.value) return false;
        }
        return true;
}

// ============================================================================
// Wire codec — RFC 6763 §6.
// ============================================================================

String MdnsTxtRecord::entryToWire(const Entry &e) {
        String wire;
        wire += e.key;
        if (e.presence == Presence::Empty) {
                wire += '=';
        } else if (e.presence == Presence::Present) {
                wire += '=';
                wire += e.value;
        }
        const size_t limit = static_cast<size_t>(MaxEntryBytes);
        if (wire.byteCount() > limit) {
                // UTF-8 boundary-aware byte truncation.  A naïve cut
                // at byte index @c limit can land in the middle of a
                // multi-byte sequence — the entry would still be
                // length-valid on the wire but would render as a
                // mojibake glyph for any downstream consumer that
                // assumes well-formed UTF-8.  Back up until the byte
                // at the cut is either a single-byte (top bit 0) or a
                // leading multi-byte (top two bits 11) byte.
                size_t cut = limit;
                const unsigned char *raw = reinterpret_cast<const unsigned char *>(wire.cstr());
                while (cut > 0 && (raw[cut] & 0xC0) == 0x80) --cut;
                wire = String(wire.cstr(), cut);
        }
        return wire;
}

Buffer MdnsTxtRecord::encode() const {
        // Two-pass to size the buffer exactly: first sum widths, then
        // allocate, then fill.  An otherwise-empty record encodes to
        // a single zero-length entry per RFC 1035.
        size_t total = 0;
        for (const Entry &e : _entries) {
                total += 1 + entryToWire(e).byteCount();
        }
        if (_entries.isEmpty()) total = 1;

        Buffer   buf(total > 0 ? total : 1);
        uint8_t *p = static_cast<uint8_t *>(buf.data());
        if (_entries.isEmpty()) {
                p[0] = 0;
                buf.setSize(1);
                return buf;
        }
        size_t pos = 0;
        for (const Entry &e : _entries) {
                String wire = entryToWire(e);
                size_t n    = wire.byteCount();
                p[pos++] = static_cast<uint8_t>(n);
                if (n > 0) {
                        std::memcpy(p + pos, wire.cstr(), n);
                        pos += n;
                }
        }
        buf.setSize(pos);
        return buf;
}

Result<MdnsTxtRecord> MdnsTxtRecord::decode(const Buffer &b) {
        return decode(static_cast<const uint8_t *>(b.data()), b.size());
}

Result<MdnsTxtRecord> MdnsTxtRecord::decode(const uint8_t *bytes, size_t len) {
        MdnsTxtRecord rec;
        if (bytes == nullptr) {
                if (len == 0) return makeResult(std::move(rec));
                return makeError<MdnsTxtRecord>(Error::ParseFailed);
        }
        size_t pos = 0;
        while (pos < len) {
                size_t entryLen = bytes[pos];
                pos += 1;
                if (entryLen == 0) {
                        // Empty padding entry — legal per RFC 1035
                        // when the record would otherwise carry no
                        // bytes.  Drop it.
                        continue;
                }
                if (pos + entryLen > len) {
                        // Truncated entry — RFC 6763 §6.1 says a
                        // length byte must always be matched by its
                        // payload.
                        return makeError<MdnsTxtRecord>(Error::ParseFailed);
                }
                const char *payload = reinterpret_cast<const char *>(bytes + pos);
                pos += entryLen;

                // Split on the first '=' (none → KeyOnly, leading '='
                // → key is empty which we treat as a malformed entry
                // and skip per RFC 6763 §6.4).
                size_t eq = entryLen;
                for (size_t i = 0; i < entryLen; ++i) {
                        if (payload[i] == '=') { eq = i; break; }
                }
                if (eq == 0) continue; // empty key, skip

                String key(payload, eq);
                if (rec.findEntry(key) >= 0) {
                        // RFC 6763 §6.4 — first occurrence wins.
                        continue;
                }
                if (eq == entryLen) {
                        rec.setKey(key);
                } else if (eq == entryLen - 1) {
                        rec.setEmpty(key);
                } else {
                        String value(payload + eq + 1, entryLen - eq - 1);
                        rec.set(key, value);
                }
        }
        return makeResult(std::move(rec));
}

TextStream &operator<<(TextStream &stream, const MdnsTxtRecord &txt) {
        stream << '{';
        bool first = true;
        txt.forEach([&](const String &key, MdnsTxtRecord::Presence p, const String &value) {
                if (!first) stream << ", ";
                first = false;
                stream << key;
                if (p == MdnsTxtRecord::Presence::Empty) {
                        stream << '=';
                } else if (p == MdnsTxtRecord::Presence::Present) {
                        stream << '=';
                        stream << value;
                }
        });
        stream << '}';
        return stream;
}

// ============================================================================
// DataStream wire form: length-prefixed Buffer of the RFC 6763 §6 payload.
// ============================================================================

Error MdnsTxtRecord::writeToStream(DataStream &s) const {
        s << encode();
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<MdnsTxtRecord> MdnsTxtRecord::readFromStream<1>(DataStream &s) {
        Buffer buf;
        s >> buf;
        if (s.status() != DataStream::Ok) return makeError<MdnsTxtRecord>(s.toError());
        return MdnsTxtRecord::decode(buf);
}

PROMEKI_NAMESPACE_END
