/**
 * @file      mdnstxtrecord.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_MDNS
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/datatype.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/buffer.h>
#include <promeki/result.h>
#include <promeki/error.h>
#include <promeki/function.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class TextStream;

/**
 * @brief DNS-SD TXT record key / value collection.
 * @ingroup network
 *
 * Carries the key/value attributes associated with a DNS-SD service
 * instance.  The on-the-wire form is defined by RFC 6763 §6 as a
 * sequence of length-prefixed text entries; each entry is one of
 * three shapes:
 *
 * <table>
 * <tr><th>Wire form</th><th>@ref Presence</th><th>Meaning</th></tr>
 * <tr><td>@c "key"</td>      <td>@ref Presence::KeyOnly</td><td>Boolean key (no @c '=' sent).</td></tr>
 * <tr><td>@c "key="</td>     <td>@ref Presence::Empty</td>  <td>Key with an explicitly empty value.</td></tr>
 * <tr><td>@c "key=value"</td><td>@ref Presence::Present</td><td>Key with a value.</td></tr>
 * </table>
 *
 * These three are distinct per the RFC: a consumer that only checks
 * @ref contains cannot tell @ref Presence::Empty apart from
 * @ref Presence::Present, but the original publisher's intent is
 * preserved through @ref MdnsTxtRecord round-trips so re-broadcasting
 * a received record does not silently mutate it.
 *
 * Keys are case-insensitive per RFC 6763 §6.4 — @c set("APP", "v")
 * followed by @c value("app") returns @c "v".  The casing used at
 * first @ref set / @ref setKey / @ref setEmpty is preserved for
 * iteration and re-encoding (the publisher's choice survives the
 * round-trip).
 *
 * Simple value type — no @c PROMEKI_SHARED_FINAL.  Registered as a
 * @ref Variant payload via @ref PROMEKI_DATATYPE.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance is
 * conditionally thread-safe: const operations may be called from
 * multiple threads, but any mutation must be externally synchronized.
 *
 * @par Example
 * @code
 * MdnsTxtRecord txt;
 * txt.set("path", "/admin");
 * txt.set("version", "2.1");
 * txt.setKey("tls");                 // boolean — "tls" with no '='
 * txt.setEmpty("flags");             // key with empty value
 *
 * txt.value("path");                 // "/admin"
 * txt.presence("tls");               // Presence::KeyOnly
 *
 * Buffer wire = txt.encode();
 * MdnsTxtRecord back = MdnsTxtRecord::decode(wire).value;
 * assert(back == txt);
 * @endcode
 */
class MdnsTxtRecord {
        public:
                PROMEKI_DATATYPE(MdnsTxtRecord, DataTypeMdnsTxtRecord, 1)

                /** @brief Writes the RFC 6763 §6 wire form via the @ref Buffer codec. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads the RFC 6763 §6 wire form via the @ref Buffer codec. */
                template <uint32_t V> static Result<MdnsTxtRecord> readFromStream(DataStream &s);

                /**
                 * @brief Distinguishes the three RFC 6763 §6 entry shapes.
                 *
                 * @ref Absent is returned by @ref presence for keys that
                 * do not appear in the record.  The other three values
                 * map directly to the wire forms in the class summary.
                 */
                enum class Presence : uint8_t {
                        Absent  = 0,
                        KeyOnly = 1,
                        Empty   = 2,
                        Present = 3,
                };

                /** @brief Maximum bytes per entry on the wire (RFC 6763 §6.1). */
                static constexpr int MaxEntryBytes = 255;

                /** @brief Constructs an empty record. */
                MdnsTxtRecord() = default;

                /**
                 * @brief Sets @p key to @p value (@ref Presence::Present).
                 *
                 * If the key already exists (case-insensitive) the
                 * existing entry's value is replaced and the original
                 * key casing preserved.  Otherwise a new entry is
                 * appended in registration order.
                 */
                void set(const String &key, const String &value);

                /**
                 * @brief Records a boolean key (@ref Presence::KeyOnly).
                 *
                 * On the wire the entry is emitted as @c "key" with no
                 * @c '='.  Consumers can detect this via @ref presence;
                 * @ref value returns an empty string for both
                 * @ref Presence::KeyOnly and @ref Presence::Empty.
                 */
                void setKey(const String &key);

                /**
                 * @brief Records a key with an explicitly empty value (@ref Presence::Empty).
                 *
                 * On the wire the entry is emitted as @c "key=" — the
                 * key is followed by @c '=' with no further bytes.
                 * Distinguished from @ref Presence::KeyOnly because some
                 * DNS-SD profiles treat the difference as semantic.
                 */
                void setEmpty(const String &key);

                /**
                 * @brief Removes the entry for @p key (case-insensitive).
                 *
                 * No-op if the key is not present.
                 */
                void remove(const String &key);

                /** @brief Removes every entry. */
                void clear() { _entries.clear(); }

                /** @brief Returns the @ref Presence of @p key. */
                Presence presence(const String &key) const;

                /** @brief @c true when @ref presence is not @ref Presence::Absent. */
                bool contains(const String &key) const { return presence(key) != Presence::Absent; }

                /**
                 * @brief Returns the value stored for @p key, or @p defaultValue.
                 *
                 * Returns @p defaultValue for @ref Presence::Absent.
                 * Returns an empty @c String for @ref Presence::KeyOnly
                 * and @ref Presence::Empty — use @ref presence when the
                 * distinction matters.
                 */
                String value(const String &key, const String &defaultValue = String()) const;

                /** @brief @c true when no entries have been recorded. */
                bool isEmpty() const { return _entries.isEmpty(); }

                /** @brief Number of distinct keys recorded. */
                int count() const { return static_cast<int>(_entries.size()); }

                /** @brief Keys in registration order, with their original casing. */
                StringList keys() const;

                /**
                 * @brief Visits every entry in registration order.
                 *
                 * The @p key passed to @p func uses the casing recorded
                 * at first registration; @p value is empty for
                 * @ref Presence::KeyOnly and @ref Presence::Empty.
                 */
                void forEach(Function<void(const String &key, Presence p, const String &value)> func) const;

                /**
                 * @brief Encodes the record to its RFC 6763 §6 wire form.
                 *
                 * Each entry contributes one length-prefixed byte
                 * sequence (1-byte length, then up to 255 bytes of
                 * @c "key" / @c "key=" / @c "key=value" payload).
                 * Entries whose @c "key=value" form exceeds
                 * @ref MaxEntryBytes are silently truncated at the
                 * value end &mdash; this matches the @c mDNSResponder
                 * behaviour and keeps the encoder infallible.
                 *
                 * Per RFC 1035, a TXT rdata field must contain at
                 * least one length byte.  An otherwise-empty record
                 * encodes to the single byte @c 0x00 (a zero-length
                 * entry); RFC 6763 §6.1 requires receivers to treat
                 * that as the empty record.
                 */
                Buffer encode() const;

                /**
                 * @brief Decodes an RFC 6763 §6 wire-form payload.
                 *
                 * Walks the buffer entry-by-entry.  Zero-length entries
                 * are skipped (RFC 1035 padding).  Duplicate keys
                 * collapse on first occurrence (RFC 6763 §6.4).
                 * Entries whose length byte runs off the end of the
                 * buffer fail the decode with @ref Error::ParseFailed;
                 * structurally-valid entries whose contents are not
                 * recognized are kept verbatim.
                 */
                static Result<MdnsTxtRecord> decode(const Buffer &b);

                /** @brief Decode overload for raw byte ranges. */
                static Result<MdnsTxtRecord> decode(const uint8_t *bytes, size_t len);

                /** @brief Equality on the full ordered set of (key, presence, value) entries. */
                bool operator==(const MdnsTxtRecord &other) const;
                bool operator!=(const MdnsTxtRecord &other) const { return !(*this == other); }

        private:
                struct Entry {
                        using List = ::promeki::List<Entry>;
                        String   key;
                        Presence presence = Presence::Absent;
                        String   value;
                };

                // Returns the index of the entry whose key matches @p key
                // case-insensitively, or -1 if no entry exists.
                int findEntry(const String &key) const;

                // Returns the on-the-wire byte string for a single
                // entry, truncated at @ref MaxEntryBytes.  Defined as
                // a private static method so it can reach the private
                // @ref Entry layout without an anonymous-namespace
                // friend.
                static String entryToWire(const Entry &e);

                Entry::List _entries;
};

/** @brief Streams a one-line debug representation to a @ref TextStream. */
TextStream &operator<<(TextStream &stream, const MdnsTxtRecord &txt);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
