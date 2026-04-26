/**
 * @file      httpheaders.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpheaders.h>

PROMEKI_NAMESPACE_BEGIN

String HttpHeaders::foldName(const String &s) {
        // ASCII case fold per RFC 9110 §5.1.  Locale-independent so
        // the same key matches regardless of the calling thread's
        // locale settings.
        const size_t len = s.byteCount();
        const char  *src = s.cstr();
        std::string  out;
        out.resize(len);
        for (size_t i = 0; i < len; ++i) {
                char c = src[i];
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
                out[i] = c;
        }
        return String(std::move(out));
}

size_t HttpHeaders::findBucket(const String &lower) const {
        for (size_t i = 0; i < _index.size(); ++i) {
                if (_index[i].lower == lower) return i;
        }
        return static_cast<size_t>(-1);
}

size_t HttpHeaders::getOrCreateBucket(const String &lower) {
        size_t i = findBucket(lower);
        if (i != static_cast<size_t>(-1)) return i;
        _index.pushToBack(KeyBucket{lower, {}});
        return _index.size() - 1;
}

void HttpHeaders::set(const String &name, const String &value) {
        // Drop existing values for this header (case-insensitive),
        // then insert the new value with the casing the caller chose.
        remove(name);
        add(name, value);
}

void HttpHeaders::add(const String &name, const String &value) {
        const String lower = foldName(name);
        const size_t bucket = getOrCreateBucket(lower);
        const size_t entryIdx = _entries.size();
        _entries.pushToBack(Entry{name, value});
        _index[bucket].indices.pushToBack(entryIdx);
}

void HttpHeaders::remove(const String &name) {
        const String lower = foldName(name);
        const size_t bucket = findBucket(lower);
        if (bucket == static_cast<size_t>(-1)) return;

        // Mark the matching entries by clearing their name (we keep
        // them in place so existing indices remain valid).  forEach()
        // and count() skip cleared entries, and a future add() reuses
        // the index slot lazily — at the cost of a slowly growing
        // _entries vector if a header is set/removed in a tight loop.
        // Header churn at that rate is unrealistic in practice; the
        // simpler dataset wins.
        for (size_t i = 0; i < _index[bucket].indices.size(); ++i) {
                size_t e = _index[bucket].indices[i];
                _entries[e] = Entry{};
        }
        // Drop the bucket itself so subsequent contains() returns false
        // and the next add() rebuilds with fresh indices.
        for (size_t i = bucket + 1; i < _index.size(); ++i) {
                _index[i - 1] = _index[i];
        }
        _index.popFromBack();
}

bool HttpHeaders::contains(const String &name) const {
        return findBucket(foldName(name)) != static_cast<size_t>(-1);
}

String HttpHeaders::value(const String &name, const String &defaultValue) const {
        const size_t bucket = findBucket(foldName(name));
        if (bucket == static_cast<size_t>(-1)) return defaultValue;
        const auto &indices = _index[bucket].indices;
        if (indices.isEmpty()) return defaultValue;
        // First-stored value wins.  This matches the way single-valued
        // headers (Content-Type, Content-Length, Host) appear in
        // requests, where seeing duplicates is itself a malformed
        // request and the first occurrence is the canonical one.
        return _entries[indices[0]].value;
}

StringList HttpHeaders::values(const String &name) const {
        StringList   out;
        const size_t bucket = findBucket(foldName(name));
        if (bucket == static_cast<size_t>(-1)) return out;
        const auto &indices = _index[bucket].indices;
        for (size_t i = 0; i < indices.size(); ++i) {
                out.pushToBack(_entries[indices[i]].value);
        }
        return out;
}

void HttpHeaders::clear() {
        _entries.clear();
        _index.clear();
}

int HttpHeaders::count() const {
        // _entries can contain cleared slots from prior remove() calls.
        // The bucket index is the source of truth for live entries.
        int n = 0;
        for (size_t i = 0; i < _index.size(); ++i) {
                n += static_cast<int>(_index[i].indices.size());
        }
        return n;
}

void HttpHeaders::forEach(std::function<void(const String &name, const String &value)> func) const {
        // Iterate in registration order: walk _entries, skipping
        // cleared slots (where we sentinel'd the name to empty in
        // remove()).  This preserves arrival order across distinct
        // header names and keeps multi-valued runs contiguous.
        for (size_t i = 0; i < _entries.size(); ++i) {
                const Entry &e = _entries[i];
                if (e.name.isEmpty()) continue;
                func(e.name, e.value);
        }
}

bool HttpHeaders::operator==(const HttpHeaders &other) const {
        if (count() != other.count()) return false;
        // Order-sensitive equality on (canonical-name, value) pairs.
        // Two HttpHeaders that resulted from add()ing the same values
        // in the same order compare equal regardless of any prior
        // remove()-induced gaps because forEach skips them.
        Entry::List a, b;
        forEach([&](const String &n, const String &v) { a.pushToBack(Entry{n, v}); });
        other.forEach([&](const String &n, const String &v) { b.pushToBack(Entry{n, v}); });
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
                if (a[i].name != b[i].name || a[i].value != b[i].value) {
                        return false;
                }
        }
        return true;
}

PROMEKI_NAMESPACE_END
