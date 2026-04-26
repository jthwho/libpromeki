/**
 * @file      url.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/url.h>
#include <promeki/stringlist.h>
#include <cctype>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Percent-encoding helpers
// ============================================================================
//
// RFC 3986 defines the unreserved set as ALPHA / DIGIT / "-" / "." /
// "_" / "~".  Every other byte of the UTF-8 encoding is a candidate
// for %XX escaping; context-specific safe sets (path, query) augment
// this with sub-delims and component separators that are legal at
// their position without quoting.

static bool isUnreserved(unsigned char c) {
        if (c >= 'A' && c <= 'Z') return true;
        if (c >= 'a' && c <= 'z') return true;
        if (c >= '0' && c <= '9') return true;
        return c == '-' || c == '.' || c == '_' || c == '~';
}

static bool isInSafeSet(unsigned char c, const char *safe) {
        if (safe == nullptr) return false;
        for (const char *p = safe; *p; ++p) {
                if (static_cast<unsigned char>(*p) == c) return true;
        }
        return false;
}

String Url::percentEncode(const String &s, const char *safe) {
        const char *bytes = s.cstr();
        if (bytes == nullptr) return String();
        String            out;
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; bytes[i] != '\0'; ++i) {
                unsigned char c = static_cast<unsigned char>(bytes[i]);
                if (isUnreserved(c) || isInSafeSet(c, safe)) {
                        out += static_cast<char>(c);
                } else {
                        char buf[4] = {'%', hex[(c >> 4) & 0xF], hex[c & 0xF], '\0'};
                        out += String(buf);
                }
        }
        return out;
}

static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
}

String Url::percentDecode(const String &s, Error *err) {
        const char *bytes = s.cstr();
        if (bytes == nullptr) return String();
        String out;
        for (size_t i = 0; bytes[i] != '\0'; ++i) {
                char c = bytes[i];
                if (c != '%') {
                        out += c;
                        continue;
                }
                // A lone or truncated '%' is malformed.  Report and
                // bail rather than emitting a partial escape — callers
                // rely on an all-or-nothing decode for correctness.
                if (bytes[i + 1] == '\0' || bytes[i + 2] == '\0') {
                        if (err != nullptr) *err = Error::Invalid;
                        return String();
                }
                int hi = hexValue(bytes[i + 1]);
                int lo = hexValue(bytes[i + 2]);
                if (hi < 0 || lo < 0) {
                        if (err != nullptr) *err = Error::Invalid;
                        return String();
                }
                char decoded = static_cast<char>((hi << 4) | lo);
                out += decoded;
                i += 2;
        }
        return out;
}

// ============================================================================
// Scheme validation
// ============================================================================
//
// RFC 3986 scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ).  Any
// deviation is a parse error — a "scheme" containing spaces or
// colons is simply not a URL.

static bool isValidScheme(const String &s) {
        if (s.isEmpty()) return false;
        char first = s.cstr()[0];
        if (!std::isalpha(static_cast<unsigned char>(first))) return false;
        for (size_t i = 1; i < s.size(); ++i) {
                unsigned char c = static_cast<unsigned char>(s.cstr()[i]);
                if (std::isalnum(c) || c == '+' || c == '-' || c == '.') continue;
                return false;
        }
        return true;
}

static String toLowerAscii(const String &s) {
        String out = s;
        // String::toLower() handles Unicode; for scheme tokens we want
        // a stable ASCII-only lowering.  Because valid schemes are
        // ASCII by construction the two are equivalent, but going
        // through toLower() keeps us consistent with the rest of the
        // library's case handling.
        return out.toLower();
}

// ============================================================================
// Parsing
// ============================================================================
//
// The parse follows RFC 3986 §3 closely: split off fragment, then
// query, then decide authority vs opaque based on the "//" prefix
// after the scheme delimiter.  We deliberately accept some loose
// inputs (lone '%' in a query value, unbracketed IPv6) because this
// class is used for library-local URIs like pmfb:// where strict
// conformance buys nothing.

Result<Url> Url::fromString(const String &s) {
        Url url;
        if (s.isEmpty()) {
                return Result<Url>(url, Error::Invalid);
        }

        // scheme
        size_t colon = s.find(':');
        if (colon == String::npos || colon == 0) {
                return Result<Url>(url, Error::Invalid);
        }
        String scheme = s.left(colon);
        if (!isValidScheme(scheme)) {
                return Result<Url>(url, Error::Invalid);
        }
        url._scheme = toLowerAscii(scheme);

        // Split off fragment (takes precedence — it ends everything).
        String remainder = s.mid(colon + 1);
        size_t hashPos = remainder.find('#');
        if (hashPos != String::npos) {
                String frag = remainder.mid(hashPos + 1);
                url._fragment = percentDecode(frag);
                remainder = remainder.left(hashPos);
        }

        // Split off query.
        size_t qPos = remainder.find('?');
        String queryStr;
        if (qPos != String::npos) {
                queryStr = remainder.mid(qPos + 1);
                remainder = remainder.left(qPos);
        }

        // Authority form starts with "//".  hier-part is either the
        // authority followed by path, or an opaque path.
        if (remainder.size() >= 2 && remainder.cstr()[0] == '/' && remainder.cstr()[1] == '/') {
                url._hasAuthority = true;
                String authAndPath = remainder.mid(2);
                // Authority terminates at first '/'; remainder is path.
                size_t slash = authAndPath.find('/');
                String authority;
                String path;
                if (slash == String::npos) {
                        authority = authAndPath;
                        path = String();
                } else {
                        authority = authAndPath.left(slash);
                        path = authAndPath.mid(slash);
                }

                // userinfo
                size_t atPos = authority.find('@');
                if (atPos != String::npos) {
                        url._userInfo = percentDecode(authority.left(atPos));
                        authority = authority.mid(atPos + 1);
                }
                // host[:port] — bracketed IPv6 literal or plain host.
                if (!authority.isEmpty() && authority.cstr()[0] == '[') {
                        size_t closeBracket = authority.find(']');
                        if (closeBracket == String::npos) {
                                return Result<Url>(url, Error::Invalid);
                        }
                        url._host = authority.mid(1, closeBracket - 1);
                        String afterBracket = authority.mid(closeBracket + 1);
                        if (!afterBracket.isEmpty() && afterBracket.cstr()[0] == ':') {
                                Error perr = Error::Ok;
                                int   p = afterBracket.mid(1).toInt(&perr);
                                if (perr.isError()) {
                                        return Result<Url>(url, Error::Invalid);
                                }
                                url._port = p;
                        }
                } else {
                        size_t portColon = authority.rfind(':');
                        if (portColon != String::npos) {
                                url._host = percentDecode(authority.left(portColon));
                                Error perr = Error::Ok;
                                int   p = authority.mid(portColon + 1).toInt(&perr);
                                if (perr.isError()) {
                                        return Result<Url>(url, Error::Invalid);
                                }
                                url._port = p;
                        } else {
                                url._host = percentDecode(authority);
                        }
                }
                url._path = percentDecode(path);
        } else {
                // Opaque form: the hier-part is the path.
                url._hasAuthority = false;
                url._path = percentDecode(remainder);
        }

        // Query map.  Empty values (foo=) and bare flags (foo) both
        // map to an empty String — the distinction is preserved by
        // the key's presence in the map either way.
        if (!queryStr.isEmpty()) {
                StringList pairs = queryStr.split("&");
                for (const String &pair : pairs) {
                        if (pair.isEmpty()) continue;
                        size_t eq = pair.find('=');
                        String key;
                        String value;
                        if (eq == String::npos) {
                                key = percentDecode(pair);
                        } else {
                                key = percentDecode(pair.left(eq));
                                value = percentDecode(pair.mid(eq + 1));
                        }
                        if (!key.isEmpty()) url._query.insert(key, value);
                }
        }
        return Result<Url>(url, Error::Ok);
}

// ============================================================================
// Serialization
// ============================================================================

Url &Url::setScheme(const String &s) {
        _scheme = toLowerAscii(s);
        return *this;
}

Url &Url::setHost(const String &s) {
        _host = s;
        if (!s.isEmpty()) _hasAuthority = true;
        return *this;
}

String Url::toString() const {
        if (!isValid()) return String();
        String out = _scheme;
        out += ":";

        if (_hasAuthority) {
                out += "//";
                if (!_userInfo.isEmpty()) {
                        // Userinfo safe set per RFC 3986 is
                        // sub-delims / ":" — we pass ":" so users like
                        // "alice:secret" round-trip, accepting that the
                        // colon in a password is technically supposed
                        // to be percent-encoded.  In practice every
                        // browser emits it raw.
                        out += percentEncode(_userInfo, ":");
                        out += "@";
                }
                // Host: if the string looks like an IPv6 literal (has
                // a ':' and no brackets already), wrap it in brackets.
                // Otherwise emit as-is; our percent-encoding is a
                // pragmatic pass since FrameBridge-style names may
                // legitimately contain characters outside host-subset.
                if (_host.find(':') != String::npos && (_host.isEmpty() || _host.cstr()[0] != '[')) {
                        out += "[";
                        out += _host;
                        out += "]";
                } else {
                        out += percentEncode(_host, "");
                }
                if (_port != PortUnset) {
                        out += ":";
                        out += String::number(_port);
                }
        }

        if (!_path.isEmpty()) {
                // Path safe set: "/", plus sub-delims we don't care
                // about disambiguating.  "/" must not be encoded
                // because it is the path separator.
                out += percentEncode(_path, "/:@");
        }

        if (!_query.isEmpty()) {
                out += "?";
                bool first = true;
                for (const auto &[k, v] : _query) {
                        if (!first) out += "&";
                        first = false;
                        out += percentEncode(k, ":/@");
                        out += "=";
                        out += percentEncode(v, ":/@");
                }
        }

        if (!_fragment.isEmpty()) {
                out += "#";
                out += percentEncode(_fragment, ":/@?");
        }
        return out;
}

bool Url::operator==(const Url &other) const {
        if (_scheme != other._scheme) return false;
        if (_hasAuthority != other._hasAuthority) return false;
        if (_userInfo != other._userInfo) return false;
        if (_host != other._host) return false;
        if (_port != other._port) return false;
        if (_path != other._path) return false;
        if (_fragment != other._fragment) return false;
        if (_query.size() != other._query.size()) return false;
        for (const auto &[k, v] : _query) {
                if (!other._query.contains(k)) return false;
                if (other._query.value(k) != v) return false;
        }
        return true;
}

PROMEKI_NAMESPACE_END
