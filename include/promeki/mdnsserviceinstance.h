/**
 * @file      mdnsserviceinstance.h
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
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/error.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/mdnstxtrecord.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class TextStream;

/**
 * @brief Concrete DNS-SD service instance discovered or to be published.
 * @ingroup network
 *
 * Combines everything a browser learns about one service: the
 * @ref MdnsServiceType, the @ref instanceName (the human-readable
 * label the publisher chose), the @ref hostname / @ref port the
 * service is reachable on, the resolved @ref ipv4Addresses /
 * @ref ipv6Addresses, the @ref MdnsTxtRecord attributes, plus
 * bookkeeping (the @ref interfaceIndex it was heard on, when it
 * was @ref lastSeen, and the cache @ref ttl).
 *
 * The FQDN form &mdash; @c "<instance>._<app>._<proto>.<domain>."
 * &mdash; is composed by @ref fqdn from the @ref instanceName and
 * the embedded @ref MdnsServiceType.  Spaces and other non-letter
 * bytes in the instance label are emitted verbatim; the on-the-wire
 * label-byte stuffing is handled by the backend, not by the value
 * type.
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
 * MdnsServiceInstance inst;
 * inst.setInstanceName("Studio Camera 2");
 * inst.setType(MdnsServiceType("ravenna", MdnsServiceType::Protocol::Tcp));
 * inst.setHostname("camera2.local.");
 * inst.setPort(9000);
 *
 * inst.fqdn();  // "Studio Camera 2._ravenna._tcp.local."
 * @endcode
 */
class MdnsServiceInstance {
        public:
                PROMEKI_DATATYPE(MdnsServiceInstance, DataTypeMdnsServiceInstance, 1)

                /** @brief Writes the canonical field tuple to the stream. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads the canonical field tuple from the stream. */
                template <uint32_t V> static Result<MdnsServiceInstance> readFromStream(DataStream &s);

                /** @brief Sentinel returned by @ref interfaceIndex when unknown. */
                static constexpr int InvalidInterfaceIndex = -1;

                MdnsServiceInstance() = default;

                /** @brief Publisher-chosen human-readable label (e.g. @c "Studio Camera 2"). */
                const String &instanceName() const { return _instanceName; }
                void setInstanceName(const String &v) { _instanceName = v; }

                /** @brief The @ref MdnsServiceType this instance belongs to. */
                const MdnsServiceType &type() const { return _type; }
                void setType(const MdnsServiceType &v) { _type = v; }

                /** @brief Host the @c SRV target points to (with or without trailing dot). */
                const String &hostname() const { return _hostname; }
                void setHostname(const String &v) { _hostname = v; }

                /** @brief Transport port number (@c SRV target port). */
                uint16_t port() const { return _port; }
                void setPort(uint16_t v) { _port = v; }

                /** @brief Resolved IPv4 addresses (may be empty). */
                const List<Ipv4Address> &ipv4Addresses() const { return _v4; }
                void setIpv4Addresses(const List<Ipv4Address> &v) { _v4 = v; }

                /** @brief Resolved IPv6 addresses (may be empty). */
                const List<Ipv6Address> &ipv6Addresses() const { return _v6; }
                void setIpv6Addresses(const List<Ipv6Address> &v) { _v6 = v; }

                /** @brief Attached @ref MdnsTxtRecord (may be empty). */
                const MdnsTxtRecord &txt() const { return _txt; }
                void setTxt(const MdnsTxtRecord &v) { _txt = v; }

                /**
                 * @brief OS interface index this instance was last heard on.
                 *
                 * Matches the @c if_nametoindex value carried by
                 * @ref NetworkInterface::index().  Negative when the
                 * instance has not yet been associated with a specific
                 * interface (e.g. a published instance pending probe).
                 */
                int interfaceIndex() const { return _interfaceIndex; }
                void setInterfaceIndex(int v) { _interfaceIndex = v; }

                /**
                 * @brief Wall-clock time of the most recent record refresh.
                 *
                 * Default-constructed @ref TimeStamp (i.e. @ref TimeStamp::Invalid)
                 * means the instance has not yet been confirmed by an
                 * inbound mDNS response.
                 */
                const TimeStamp &lastSeen() const { return _lastSeen; }
                void setLastSeen(const TimeStamp &v) { _lastSeen = v; }

                /**
                 * @brief Remaining cache lifetime as of @ref lastSeen.
                 *
                 * Sourced from the smallest TTL among the @c PTR / @c SRV
                 * / @c TXT records that contributed to this instance.
                 * Default-constructed (invalid) @ref Duration when not
                 * yet populated.
                 */
                const Duration &ttl() const { return _ttl; }
                void setTtl(const Duration &v) { _ttl = v; }

                /** @brief @c true when @ref instanceName and @ref type are set. */
                bool isValid() const { return !_instanceName.isEmpty() && _type.isValid(); }

                /**
                 * @brief Returns the FQDN @c "<instance>._<app>._<proto>.<domain>.".
                 *
                 * Empty when @ref isValid is @c false.
                 */
                String fqdn() const;

                /**
                 * @brief Identity equality — compares @ref type and
                 *        @ref instanceName only.
                 *
                 * Two announcements of the same service moments apart
                 * carry identical @ref type + @ref instanceName but
                 * different @ref lastSeen / @ref ttl timestamps; tying
                 * equality to identity rather than the full snapshot
                 * keeps application-level dedupe (e.g.
                 * @c List::contains) doing the obvious thing.  Use
                 * @ref hasSameContent when "did the publisher change
                 * anything user-visible" is the question, or
                 * @ref hasSameSnapshot to include timing too.
                 */
                bool operator==(const MdnsServiceInstance &other) const;
                bool operator!=(const MdnsServiceInstance &other) const { return !(*this == other); }

                /**
                 * @brief Identity + publisher-controlled state.
                 *
                 * Compares @ref type, @ref instanceName, @ref hostname,
                 * @ref port, @ref ipv4Addresses, @ref ipv6Addresses,
                 * and @ref txt.  Used by @ref MdnsBrowser to decide
                 * whether to emit @c serviceUpdated.  Excludes the
                 * timing fields and @ref interfaceIndex (the
                 * receive-side attribution metadata) which change
                 * independently of the publisher.
                 */
                bool hasSameContent(const MdnsServiceInstance &other) const;

                /**
                 * @brief Full-field comparison including timing and
                 *        receive-side attribution.
                 *
                 * Exposed for callers that need bit-identical equality
                 * (round-trip tests, snapshot dedupe in a stream of
                 * observations).  Production code rarely needs this.
                 */
                bool hasSameSnapshot(const MdnsServiceInstance &other) const;

                /** @brief Returns a single-line debug representation. */
                String toString() const;

        private:
                String           _instanceName;
                MdnsServiceType  _type;
                String           _hostname;
                uint16_t         _port = 0;
                List<Ipv4Address> _v4;
                List<Ipv6Address> _v6;
                MdnsTxtRecord    _txt;
                int              _interfaceIndex = InvalidInterfaceIndex;
                TimeStamp        _lastSeen;
                Duration         _ttl;
};

/** @brief Streams the debug form to a @ref TextStream. */
TextStream &operator<<(TextStream &stream, const MdnsServiceInstance &inst);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MDNS
