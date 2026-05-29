/**
 * @file      mdnsname.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RFC 1035 master-file-style label escape / unescape helpers.
 * @ingroup network
 *
 * DNS wire labels are length-prefixed byte sequences containing any
 * byte value 0x00..0xFF.  Their canonical text representation
 * (RFC 1035 §5.1) joins labels with @c '.' and escapes any literal
 * @c '.' or @c '\\' as @c "\\." / @c "\\\\".  This namespace
 * provides the conversion helpers so the rest of the mDNS engine can
 * carry text-form names without losing the label structure.
 *
 * Supported escapes:
 *  - @c "\\." — literal @c '.' inside a label.
 *  - @c "\\\\" — literal @c '\\' inside a label.
 *
 * Numeric three-digit escapes (e.g. @c "\\009") are @b not produced
 * by @ref mdnsEscapeLabel; non-printable bytes pass through verbatim.
 * The decode path (@ref mdnsSplitName / @ref mdnsUnescapeLabel)
 * accepts them when present in input but does not require them.
 */

/**
 * @brief Escapes one raw label's bytes for the text form.
 *
 * Each occurrence of @c '.' becomes @c "\\." and each occurrence of
 * @c '\\' becomes @c "\\\\".  Other bytes pass through.
 */
String mdnsEscapeLabel(const String &raw);

/**
 * @brief Inverse of @ref mdnsEscapeLabel.
 *
 * Recognises @c "\\." → @c '.', @c "\\\\" → @c '\\', and
 * @c "\\DDD" (three decimal digits) → byte @c DDD.  Other
 * back-slashes are treated as literal.
 */
String mdnsUnescapeLabel(const String &escaped);

/**
 * @brief Splits an escape-aware text FQDN into raw label bytes.
 *
 * Walks @p name, recognising @c "\\." and @c "\\\\" as escapes
 * rather than label boundaries.  The optional trailing root
 * marker (a single unescaped @c '.') is dropped.  Returns the
 * sequence of raw (unescaped) label byte strings.
 *
 * @code
 * mdnsSplitName(R"(Studio\.B Camera._http._tcp.local.)");
 * // → ["Studio.B Camera", "_http", "_tcp", "local"]
 * @endcode
 */
List<String> mdnsSplitName(const String &name);

/**
 * @brief Inverse of @ref mdnsSplitName.
 *
 * Joins the raw labels with @c '.' separators after escaping each
 * one with @ref mdnsEscapeLabel.  A trailing @c '.' is appended
 * (the root marker).
 */
String mdnsJoinName(const List<String> &rawLabels);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
