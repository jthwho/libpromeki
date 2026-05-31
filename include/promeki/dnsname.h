/**
 * @file      dnsname.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RFC 1035 §3.1 / §4.1.4 DNS-name codec (compression-aware).
 * @ingroup network
 *
 * Encodes and decodes the wire form used by every DNS message section
 * (Questions, Answers, Authority, Additional) as well as inside the
 * rdata of @c PTR, @c CNAME, @c NS, @c MX, @c SRV, and @c SOA records.
 *
 * @par Wire format
 *  - A name is a sequence of @b labels terminated by a zero-length label.
 *  - Each label is a single length byte (0-63) followed by that many
 *    arbitrary bytes.  Label boundaries do @b not align on
 *    @c '.' in the text form — the dot is purely a textual joiner.
 *  - The high two bits of the length byte signal a @b compression
 *    pointer (RFC 1035 §4.1.4): @c 11xxxxxx xxxxxxxx is a 14-bit
 *    offset into the message at which to resume decoding.  Pointers
 *    only ever target a position earlier in the message, and the
 *    decoder caps the total bytes consumed at @ref MaxNameWireBytes
 *    to defeat malicious pointer loops.
 *  - The total wire length of a fully-expanded name is capped at
 *    @ref MaxNameTextBytes after escape (RFC 1035 §2.3.4).
 *
 * @par Text form
 *  - Labels join with @c '.' separators.
 *  - Literal @c '.' inside a label is escaped as @c "\\." (a label
 *    can legally contain any byte 0x00-0xFF; this matters for
 *    DNS-SD instance names that often contain spaces and dots).
 *  - Literal @c '\\' inside a label is escaped as @c "\\\\".
 *  - Non-printable bytes can be encoded as @c "\\DDD" (three decimal
 *    digits) on decode; the encoder does @b not produce these — it
 *    passes non-printable bytes through verbatim.
 *  - A trailing @c '.' is the root marker; @ref decodeName always
 *    emits it, and @ref encodeName accepts inputs with or without.
 *
 * @par Why not use mjansson/mdns
 *  The mDNS engine has historically leaned on
 *  @c thirdparty/mdns/mdns.h for compression-aware name parsing.
 *  That dependency only ships when @c PROMEKI_ENABLE_MDNS is on;
 *  the unicast DNS resolver builds whenever
 *  @c PROMEKI_ENABLE_NETWORK is on, so it needs its own codec.
 *  The semantics are identical — the same fixtures round-trip
 *  byte-for-byte against either implementation.
 */

/** @brief Hard ceiling on the total wire-form bytes a single name may consume (RFC 1035 §2.3.4 + pointer guard). */
inline constexpr size_t MaxNameWireBytes = 255;

/** @brief Hard ceiling on the escaped text form a single name expands to. */
inline constexpr size_t MaxNameTextBytes = 1024;

/** @brief Maximum number of compression-pointer hops before giving up (loop guard). */
inline constexpr int MaxNamePointerHops = 16;

/**
 * @brief Escapes one raw label's bytes for the text form.
 *
 * Each occurrence of @c '.' becomes @c "\\." and each occurrence of
 * @c '\\' becomes @c "\\\\".  Other bytes pass through verbatim.
 */
String dnsEscapeLabel(const String &raw);

/**
 * @brief Inverse of @ref dnsEscapeLabel.
 *
 * Recognises @c "\\." → @c '.', @c "\\\\" → @c '\\', and
 * @c "\\DDD" (three decimal digits) → byte @c DDD.  Other
 * back-slashes are treated as literal.
 */
String dnsUnescapeLabel(const String &escaped);

/**
 * @brief Splits an escape-aware text FQDN into raw label bytes.
 *
 * Walks @p name, recognising @c "\\." and @c "\\\\" as escapes
 * rather than label boundaries.  The optional trailing root marker
 * (a single unescaped @c '.') is dropped.  Returns the sequence of
 * raw (unescaped) label byte strings.
 *
 * @code
 * dnsSplitName(R"(Studio\.B Camera._http._tcp.local.)");
 * // -> ["Studio.B Camera", "_http", "_tcp", "local"]
 * @endcode
 */
List<String> dnsSplitName(const String &name);

/**
 * @brief Inverse of @ref dnsSplitName.
 *
 * Joins the raw labels with @c '.' separators after escaping each
 * one with @ref dnsEscapeLabel.  A trailing @c '.' (root marker) is
 * appended.
 */
String dnsJoinName(const List<String> &rawLabels);

/**
 * @brief Decoded form of one DNS name plus the cursor it leaves
 *        behind in the source buffer.
 *
 * Returned by @ref decodeName so the caller can walk past the name
 * to the type/class/TTL fields that follow.  @ref nextOffset always
 * lands on the byte immediately after the terminating root label
 * (or after the first compression-pointer hop, whichever ends the
 * encoding) — it is @b not the post-pointer-expansion position.
 */
struct DnsNameDecodeResult {
        /** @brief Escape-aware text form, terminated with a trailing @c '.'. */
        String name;
        /** @brief Offset of the byte immediately after the encoded name in the source buffer. */
        size_t nextOffset = 0;
};

/**
 * @brief Decodes one DNS name starting at @p offset in @p data.
 *
 * Honours compression pointers per RFC 1035 §4.1.4: a label byte with
 * its top two bits set redirects to the 14-bit offset that follows.
 * Pointers may chain, but only forward-then-backward — every hop must
 * land at a position strictly less than its source byte.  Encountering
 * a forward pointer, a malformed label length (61–63 are not pointer
 * markers and 1+ are valid label lengths; 64–191 are reserved and
 * rejected), or exceeding @ref MaxNamePointerHops yields
 * @ref Error::ParseFailed.
 *
 * @param data    Pointer to the start of the enclosing DNS message.
 * @param len     Total byte length of @p data.
 * @param offset  Byte offset into @p data where the name begins.
 * @return The decoded name + the offset of the byte after the on-wire
 *         encoding, or @ref Error::ParseFailed when the name is
 *         truncated or malformed.
 */
Result<DnsNameDecodeResult> decodeName(const uint8_t *data, size_t len, size_t offset);

/**
 * @brief Advances @p offset past one encoded name without materialising it.
 *
 * Useful when scanning to a record's fixed-position fields without
 * paying the per-name allocation cost (e.g. iterating answers to find
 * the @c OPT pseudo-record).  Same error semantics as
 * @ref decodeName.
 *
 * @param data    Pointer to the start of the enclosing DNS message.
 * @param len     Total byte length of @p data.
 * @param offset  In/out cursor; advanced past the name on success.
 * @return @ref Error::Ok on success, @ref Error::ParseFailed otherwise.
 */
Error skipName(const uint8_t *data, size_t len, size_t &offset);

/**
 * @brief Per-message compression dictionary used by @ref encodeName.
 *
 * Maps already-emitted name suffixes (canonical lowercase form, with
 * trailing root marker) to their byte offset within the in-progress
 * packet.  Compression points are 14 bits wide so any value above
 * @c 0x3FFF cannot be referenced; the encoder silently skips the
 * back-reference for such offsets.
 */
using DnsNameCompressionMap = Map<String, uint16_t>;

/**
 * @brief Encodes one DNS name into the on-wire form, with compression.
 *
 * Appends to @p out the length-prefixed labels of @p name, terminating
 * with a root label (or a compression pointer if @p name's suffix
 * matches a previously-encoded sequence in @p dict).  When @p dict is
 * non-null it is consulted for back-references and updated for every
 * new suffix this encode introduces.  A null @p dict disables
 * compression entirely (useful for tests and for fields that
 * RFC 3597 forbids from compressing).
 *
 * Compression follows the rule used by every mainstream resolver:
 * walk the name suffix-first, longest first; if any suffix has a
 * registered offset @c &lt;= @c 0x3FFF, emit it as a pointer and
 * stop.  Suffixes longer than what fits in the wire (after the
 * current append) are added to @p dict for future references.
 *
 * @param name  Text form of the name to encode.  An empty string is
 *              treated as the root name and emits a single zero
 *              terminator.
 * @param out   Byte sequence being constructed; appended in place.
 * @param dict  Optional compression dictionary; pass @c nullptr to
 *              disable compression.
 * @return @ref Error::Ok on success, @ref Error::Invalid when a label
 *         exceeds 63 bytes or the encoded name would exceed
 *         @ref MaxNameWireBytes.
 */
Error encodeName(const String &name, List<uint8_t> &out,
                 DnsNameCompressionMap *dict = nullptr);

/**
 * @brief Returns the canonical (lower-cased, root-terminated) form of @p name.
 *
 * Names compare case-insensitively per RFC 1035 §2.3.3.  The
 * canonical form is used as the @ref DnsNameCompressionMap key and
 * as the @ref DnsCache lookup key so two queries that differ only
 * in case hit the same entry.
 *
 * Lower-cases ASCII letters in place; non-ASCII bytes pass through
 * untouched (IDN names must already be in punycode form per
 * RFC 5891).  Always appends a trailing @c '.'.
 */
String dnsCanonicalName(const String &name);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
