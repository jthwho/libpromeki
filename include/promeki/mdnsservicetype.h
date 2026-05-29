/**
 * @file      mdnsservicetype.h
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
#include <promeki/result.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class TextStream;

/**
 * @brief DNS-SD service type — the @c "_app._proto[.domain]" triple.
 * @ingroup network
 *
 * Identifies a class of services on the network, e.g. @c _http._tcp,
 * @c _ipp._tcp, @c _ravenna._tcp.  This is the @b type, not a specific
 * instance — a single browse for @c _http._tcp.local. may return many
 * concrete services (see @ref MdnsServiceInstance).
 *
 * Parsing follows RFC 6763 §4.  The canonical text form is
 * @code
 *     _<app>._<proto>[.<domain>][.]
 * @endcode
 * The leading underscore on each label is mandatory.  The protocol
 * label is restricted to @c _tcp or @c _udp.  The domain defaults to
 * @c "local" for multicast DNS; absent or @c "local" both round-trip
 * to @c "local".  A trailing @c "." is tolerated on input and never
 * emitted by @ref toString — use @ref toFqdn for the wire form with
 * an explicit root marker.
 *
 * Subtype browsing (RFC 6763 §7.1, the @c "_sub" label form) is a
 * @b browser concept; it does not belong on the service type itself.
 * The corresponding browse name is composed by @ref MdnsBrowser when
 * a subtype filter is set.
 *
 * Simple value type — no @c PROMEKI_SHARED_FINAL, copy / move are
 * trivial.  Registered as a @ref Variant payload via @ref PROMEKI_DATATYPE,
 * so service types ride cleanly through cross-thread signal dispatch
 * and @ref Metadata storage.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance is
 * conditionally thread-safe: const operations may be called from
 * multiple threads, but any mutation must be externally synchronized.
 *
 * @par Example
 * @code
 * auto r = MdnsServiceType::fromString("_http._tcp.local.");
 * REQUIRE(r.error.isOk());
 * MdnsServiceType t = r.value;
 *
 * t.app();              // "http"
 * t.proto();            // MdnsServiceType::Protocol::Tcp
 * t.domain();           // "local"
 * t.toString();         // "_http._tcp.local"
 * t.toFqdn();           // "_http._tcp.local."
 * @endcode
 */
class MdnsServiceType {
        public:
                PROMEKI_DATATYPE(MdnsServiceType, DataTypeMdnsServiceType, 1)

                /** @brief Writes the canonical text form to the stream. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads a canonical text form from the stream. */
                template <uint32_t V> static Result<MdnsServiceType> readFromStream(DataStream &s);

                /**
                 * @brief Transport protocol carried by the service type.
                 *
                 * RFC 6763 §4.1.2 defines exactly two valid values:
                 * @c _tcp and @c _udp.  @ref Invalid is the
                 * default-constructed state and signals that the service
                 * type has not been initialized.
                 */
                enum class Protocol : uint8_t {
                        Invalid = 0,
                        Tcp     = 1,
                        Udp     = 2,
                };

                /** @brief Default DNS-SD parent domain for multicast DNS. */
                static constexpr const char *DefaultDomain = "local";

                /** @brief Maximum length of the @ref app label per RFC 6335 §5.1. */
                static constexpr int MaxAppLabelLen = 15;

                /** @brief Default-constructed service type is invalid. */
                MdnsServiceType() = default;

                /**
                 * @brief Constructs from explicit components without parsing.
                 *
                 * @param app     Application label, e.g. @c "http".  The
                 *                leading underscore is added internally
                 *                by @ref toString and must @b not be
                 *                included here.  Must be 1..@ref MaxAppLabelLen
                 *                bytes long.  No validation is performed
                 *                by the constructor; pass through
                 *                @ref fromString to validate.
                 * @param proto   Transport protocol.
                 * @param domain  Parent DNS-SD domain.  Defaults to
                 *                @ref DefaultDomain.  Trailing dot is
                 *                stripped on storage.
                 */
                MdnsServiceType(const String &app, Protocol proto,
                                const String &domain = String(DefaultDomain));

                /**
                 * @brief Parses a canonical text form.
                 *
                 * Accepts any of:
                 *   - @c "_app._tcp"
                 *   - @c "_app._tcp."
                 *   - @c "_app._tcp.local"
                 *   - @c "_app._tcp.local."
                 *
                 * Returns @ref Error::Invalid for malformed input
                 * (missing underscores, unknown protocol label, app
                 * label out of length range, illegal characters).
                 */
                static Result<MdnsServiceType> fromString(const String &s);

                /** @brief Application label without leading underscore (e.g. @c "http"). */
                const String &app() const { return _app; }

                /** @brief Transport protocol. */
                Protocol proto() const { return _proto; }

                /** @brief Parent domain (no trailing dot). */
                const String &domain() const { return _domain; }

                /** @brief Returns @c true when the service type has been initialized. */
                bool isValid() const { return _proto != Protocol::Invalid && !_app.isEmpty(); }

                /**
                 * @brief Returns the canonical text form without trailing dot.
                 *
                 * Format: @c "_<app>._<proto>.<domain>".  When @ref domain
                 * matches @ref DefaultDomain the domain label is still
                 * emitted; round-trips with @ref fromString.
                 */
                String toString() const;

                /**
                 * @brief Returns the FQDN form with trailing dot (wire form).
                 *
                 * Format: @c "_<app>._<proto>.<domain>.".  This is the
                 * form that appears in @c PTR / @c SRV / @c TXT record
                 * names on the wire.
                 */
                String toFqdn() const;

                /**
                 * @brief Returns the FQDN browse name for a subtype filter.
                 *
                 * RFC 6763 §7.1 defines subtype browsing via the
                 * @c "_sub" label:
                 * @code
                 *     _<subtype>._sub._<app>._<proto>.<domain>.
                 * @endcode
                 *
                 * @param subtype Subtype label without the leading
                 *                underscore (e.g. @c "printer").  Empty
                 *                returns the same string as @ref toFqdn.
                 */
                String toSubtypeBrowseFqdn(const String &subtype) const;

                /** @brief Returns @c "tcp" or @c "udp"; empty for @ref Protocol::Invalid. */
                static String protocolToString(Protocol p);

                /** @brief Inverse of @ref protocolToString; case-insensitive. */
                static Protocol protocolFromString(const String &s);

                /** @brief Lexicographic compare on @ref app, then @ref proto, then @ref domain. */
                bool operator==(const MdnsServiceType &other) const;
                bool operator!=(const MdnsServiceType &other) const { return !(*this == other); }
                bool operator<(const MdnsServiceType &other) const;

        private:
                String   _app;
                Protocol _proto  = Protocol::Invalid;
                String   _domain = String(DefaultDomain);
};

/** @brief Streams the canonical text form to a @ref TextStream. */
TextStream &operator<<(TextStream &stream, const MdnsServiceType &t);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
