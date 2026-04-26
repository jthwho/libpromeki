/**
 * @file      url.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/map.h>
#include <promeki/error.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Uniform Resource Locator / Identifier.
 * @ingroup util
 *
 * Parses and builds URIs per the common subset of RFC 3986.  Two
 * serialized forms round-trip through this class:
 *  - Authority form: @c scheme://[userinfo@]host[:port][/path][?query][#fragment]
 *  - Opaque form:    @c scheme:opaque[?query][#fragment]
 *
 * The query component is exposed as an ordered @ref Map of key → value
 * pairs (percent-decoded).  The raw query string is not preserved
 * across a round-trip when the caller edits individual query values;
 * parsing is destructive to ordering within duplicate keys, which
 * matches how virtually every HTTP application actually uses query
 * strings.
 *
 * @par Case sensitivity
 * Per RFC 3986:
 *  - @b Scheme is case-insensitive; @ref fromString lowercases it so
 *    callers always see the canonical form (@c "pmfb", never
 *    @c "PMFB").
 *  - @b Host is case-insensitive in the spec but preserved verbatim
 *    here because libpromeki schemes like @c pmfb:// use the host
 *    field as a logical name where case may matter to the backend.
 *  - @b Path, @b query, and @b fragment are case-sensitive — both
 *    keys and values in @ref query() are preserved exactly as
 *    written.  Consumers that match query keys against an identifier
 *    registry (see @ref MediaIO::applyQueryToConfig) therefore
 *    require an exact-case match; this is a deliberate forgiveness
 *    tradeoff that prevents near-miss spellings from resolving to
 *    the wrong key.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — const operations are safe, but
 * concurrent mutation (setScheme, setHost, setQuery, etc.) must
 * be externally synchronized.
 *
 * @par Example
 * @code
 * Url u = Url::fromString("pmfb://studio-a?FrameBridgeRingDepth=4").first();
 * u.scheme();                               // "pmfb"
 * u.host();                                 // "studio-a"
 * u.queryValue("FrameBridgeRingDepth");     // "4"
 * u.queryValue("framebridgeringdepth");     // "" (case-sensitive miss)
 * String out = u.toString();                // round-trip
 * @endcode
 */
class Url {
        public:
                /** @brief Sentinel for @ref port indicating "not specified". */
                static constexpr int PortUnset = -1;

                /**
                 * @brief Parses a URL/URI string.
                 *
                 * The parser is forgiving for the kinds of things libpromeki
                 * URLs need — see the class-level docs.  Any structural
                 * failure (missing scheme, malformed authority, bad port,
                 * unbracketed IPv6) yields @c Error::Invalid; the partially
                 * built Url is still returned in @c Result::first() so
                 * callers can inspect it for diagnostics.
                 *
                 * @param s The input string.
                 * @return The parsed Url on success, or @c Error::Invalid
                 *         (with the partially-built Url in @c first()) on
                 *         a parse failure.
                 */
                static Result<Url> fromString(const String &s);

                /**
                 * @brief Percent-encodes a string per RFC 3986.
                 *
                 * Unreserved characters (@c ALPHA / @c DIGIT / @c - / @c . /
                 * @c _ / @c ~) are passed through; every other byte of the
                 * UTF-8 encoding is emitted as @c %XX.  Characters listed
                 * in @p safe are additionally passed through — callers
                 * pass context-specific safe sets (e.g. @c "/" for paths,
                 * @c ":@/" for query values that may embed colons).
                 *
                 * @param s    Input string.
                 * @param safe Optional additional characters to leave unescaped.
                 * @return The percent-encoded string.
                 */
                static String percentEncode(const String &s, const char *safe = nullptr);

                /**
                 * @brief Decodes a percent-encoded string.
                 * @param s   The percent-encoded input.
                 * @param err Optional error output; set to
                 *            @ref Error::Invalid on a malformed escape.
                 * @return The decoded string.
                 */
                static String percentDecode(const String &s, Error *err = nullptr);

                /** @brief Constructs an empty (invalid) Url. */
                Url() = default;

                /**
                 * @brief Convenience constructor equivalent to @ref fromString.
                 *
                 * Discards any parse error — the resulting object will
                 * report @c isValid() == false on a malformed input.
                 * Prefer @ref fromString when you need to distinguish
                 * "invalid input" from "empty Url".
                 *
                 * @param s The URL/URI string.
                 */
                explicit Url(const String &s) : Url(fromString(s).first()) {}

                /** @brief Convenience constructor from a C-string. */
                explicit Url(const char *s) : Url(fromString(String(s)).first()) {}

                /**
                 * @brief A Url is valid if it has a non-empty scheme.
                 *
                 * Anything less than that cannot be serialized back to a
                 * meaningful URI, so the scheme is the minimum test.
                 */
                bool isValid() const { return !_scheme.isEmpty(); }

                /** @brief Scheme component (lowercased, e.g. @c "pmfb"). */
                const String &scheme() const { return _scheme; }

                /** @brief Userinfo component without the trailing @c @. */
                const String &userInfo() const { return _userInfo; }

                /**
                 * @brief Host component (IPv6 literals are stored without brackets).
                 *
                 * For schemes that use @e host as a logical name (like
                 * @c pmfb://), this is the name itself.
                 */
                const String &host() const { return _host; }

                /**
                 * @brief Port, or @ref PortUnset when no port was specified.
                 */
                int port() const { return _port; }

                /** @brief Path component, including any leading @c /. */
                const String &path() const { return _path; }

                /**
                 * @brief Full query map (percent-decoded keys and values).
                 *
                 * Keys and values are stored with the exact case the
                 * URL was parsed with — see the class-level @c "Case
                 * sensitivity" section.  Lookups against this map
                 * (including @ref queryValue and
                 * @ref hasQueryValue) are case-sensitive.
                 */
                const Map<String, String> &query() const { return _query; }

                /**
                 * @brief Returns the value for query key @p key, or @p defaultValue.
                 *
                 * Lookup is case-sensitive: @c "Ring" and @c "ring"
                 * are distinct keys.
                 *
                 * @param key          The query parameter name.
                 * @param defaultValue Fallback when the key is absent.
                 */
                String queryValue(const String &key, const String &defaultValue = String()) const {
                        return _query.value(key, defaultValue);
                }

                /**
                 * @brief Returns true if @p key is present in the query map.
                 *
                 * Lookup is case-sensitive; see @ref queryValue.
                 */
                bool hasQueryValue(const String &key) const { return _query.contains(key); }

                /** @brief Fragment component without the leading @c #. */
                const String &fragment() const { return _fragment; }

                /**
                 * @brief Whether the serialized form has an authority component.
                 *
                 * True for URIs that start with @c scheme://, false for
                 * opaque URIs like @c mailto:foo@example.com.  Set
                 * automatically by @ref fromString; call
                 * @ref setHasAuthority to flip between forms when
                 * building a URL by hand.
                 */
                bool hasAuthority() const { return _hasAuthority; }

                /** @brief Replaces the scheme.  Coerced to lowercase. */
                Url &setScheme(const String &s);

                /** @brief Replaces the userinfo component. */
                Url &setUserInfo(const String &s) {
                        _userInfo = s;
                        return *this;
                }

                /**
                 * @brief Replaces the host.
                 *
                 * Setting a non-empty host implies authority form, so
                 * @ref hasAuthority is set to @c true.
                 */
                Url &setHost(const String &s);

                /** @brief Replaces the port (pass @ref PortUnset to clear). */
                Url &setPort(int p) {
                        _port = p;
                        return *this;
                }

                /** @brief Replaces the path. */
                Url &setPath(const String &s) {
                        _path = s;
                        return *this;
                }

                /** @brief Replaces the entire query map. */
                Url &setQuery(const Map<String, String> &q) {
                        _query = q;
                        return *this;
                }

                /** @brief Inserts or overwrites a single query key. */
                Url &setQueryValue(const String &key, const String &value) {
                        _query.insert(key, value);
                        return *this;
                }

                /** @brief Removes a single query key.  No-op if absent. */
                Url &removeQueryValue(const String &key) {
                        _query.remove(key);
                        return *this;
                }

                /** @brief Replaces the fragment component. */
                Url &setFragment(const String &s) {
                        _fragment = s;
                        return *this;
                }

                /**
                 * @brief Selects authority vs opaque serialization form.
                 *
                 * Callers rarely need this — @ref fromString and
                 * @ref setHost set the right value automatically.  Flip
                 * it manually only when building an opaque URI whose
                 * first path character would otherwise be mistaken for
                 * authority.
                 */
                Url &setHasAuthority(bool v) {
                        _hasAuthority = v;
                        return *this;
                }

                /**
                 * @brief Serializes back to the canonical URI form.
                 *
                 * Output is the authority form when @ref hasAuthority
                 * is true, opaque form otherwise.  Components are
                 * percent-encoded with their context-appropriate safe
                 * sets.
                 */
                String toString() const;

                /** @brief Equality on all components. */
                bool operator==(const Url &other) const;

                /** @brief Negated @ref operator==. */
                bool operator!=(const Url &other) const { return !(*this == other); }

        private:
                String              _scheme;
                String              _userInfo;
                String              _host;
                int                 _port = PortUnset;
                String              _path;
                Map<String, String> _query;
                String              _fragment;
                bool                _hasAuthority = false;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Url);
