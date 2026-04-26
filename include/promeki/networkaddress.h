/**
 * @file      networkaddress.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <variant>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/result.h>
#include <promeki/platform.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
#include <sys/socket.h>
#include <netinet/in.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class TextStream;

/**
 * @brief High-level network address that can represent IPv4, IPv6, or an unresolved hostname.
 * @ingroup network
 *
 * NetworkAddress is a polymorphic address type that holds either a
 * resolved IP address (Ipv4Address or Ipv6Address) or an unresolved
 * hostname string. It uses std::variant internally for type-safe
 * storage without virtual dispatch.
 *
 * When constructed from a string, it first attempts to parse as an
 * IPv4 literal, then IPv6, and finally stores the string as an
 * unresolved hostname. DNS resolution is not performed automatically;
 * use DnsResolver to resolve hostname addresses.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @par Example
 * @code
 * // Resolved addresses
 * NetworkAddress a(Ipv4Address(192, 168, 1, 1));
 * CHECK(a.isIPv4());
 * CHECK(a.isResolved());
 *
 * // Unresolved hostname
 * NetworkAddress b("ndi-source.local");
 * CHECK(b.isHostname());
 * CHECK_FALSE(b.isResolved());
 *
 * // Auto-detection from string
 * auto [addr, err] = NetworkAddress::fromString("10.0.0.1");
 * CHECK(addr.isIPv4());
 * @endcode
 */
class NetworkAddress {
        public:
                /** @brief The kind of address stored. */
                enum Type {
                        None = 0, ///< No address set.
                        IPv4,     ///< Resolved IPv4 address.
                        IPv6,     ///< Resolved IPv6 address.
                        Hostname  ///< Unresolved hostname string.
                };

                /**
                 * @brief Parses a string into a NetworkAddress.
                 *
                 * Tries IPv4 literal, then IPv6 literal, then stores as hostname.
                 * Never fails (any non-empty string is a valid hostname).
                 *
                 * @param str The string to parse.
                 * @return A Result containing the address and Error::Ok,
                 *         or a null address and Error::Invalid if the string is empty.
                 */
                static Result<NetworkAddress> fromString(const String &str);

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
                /**
                 * @brief Constructs from a POSIX/Windows sockaddr structure.
                 *
                 * Extracts the IP address from a sockaddr_in or sockaddr_in6.
                 * The port is ignored; use the port from the sockaddr separately.
                 *
                 * @param addr Pointer to a sockaddr (sockaddr_in or sockaddr_in6).
                 * @param len Length of the sockaddr structure.
                 * @return A Result containing the address and Error::Ok, or
                 *         a null address and Error::Invalid if the sockaddr is
                 *         not a recognized address family.
                 */
                static Result<NetworkAddress> fromSockAddr(const struct sockaddr *addr, size_t len);
#endif

                /** @brief Default constructor. Creates a null address. */
                NetworkAddress() = default;

                /**
                 * @brief Constructs from an IPv4 address.
                 * @param addr The IPv4 address.
                 */
                NetworkAddress(const Ipv4Address &addr) : _data(addr) {}

                /**
                 * @brief Constructs from an IPv6 address.
                 * @param addr The IPv6 address.
                 */
                NetworkAddress(const Ipv6Address &addr) : _data(addr) {}

                /**
                 * @brief Constructs from a hostname string (unresolved).
                 * @param hostname The hostname to store.
                 */
                explicit NetworkAddress(const String &hostname) : _data(hostname) {}

                /** @brief Returns the type of address stored. */
                Type type() const;

                /** @brief Returns true if no address is stored. */
                bool isNull() const { return std::holds_alternative<std::monostate>(_data); }

                /** @brief Returns true if this holds an IPv4 address. */
                bool isIPv4() const { return std::holds_alternative<Ipv4Address>(_data); }

                /** @brief Returns true if this holds an IPv6 address. */
                bool isIPv6() const { return std::holds_alternative<Ipv6Address>(_data); }

                /** @brief Returns true if this holds an unresolved hostname. */
                bool isHostname() const { return std::holds_alternative<String>(_data); }

                /** @brief Returns true if this holds a resolved IP address (IPv4 or IPv6). */
                bool isResolved() const { return isIPv4() || isIPv6(); }

                /**
                 * @brief Returns the stored IPv4 address.
                 * @return The Ipv4Address, or a null Ipv4Address if not IPv4.
                 */
                Ipv4Address toIpv4() const;

                /**
                 * @brief Returns the stored IPv6 address.
                 * @return The Ipv6Address, or a null Ipv6Address if not IPv6.
                 */
                Ipv6Address toIpv6() const;

                /**
                 * @brief Returns the stored hostname.
                 * @return The hostname string, or an empty string if not a hostname.
                 */
                String hostname() const;

                /**
                 * @brief Returns true if the address is a loopback address.
                 *
                 * Returns true for 127.0.0.0/8, ::1, or the hostname "localhost".
                 */
                bool isLoopback() const;

                /**
                 * @brief Returns true if the address is a multicast address.
                 *
                 * Returns true for 224.0.0.0/4 (IPv4) or ff00::/8 (IPv6).
                 * Always returns false for hostnames.
                 */
                bool isMulticast() const;

                /**
                 * @brief Returns true if the address is link-local.
                 *
                 * Returns true for 169.254.0.0/16 (IPv4) or fe80::/10 (IPv6).
                 * Always returns false for hostnames.
                 */
                bool isLinkLocal() const;

                /**
                 * @brief Returns a string representation.
                 *
                 * Returns the dotted-quad for IPv4, colon-hex for IPv6,
                 * or the hostname string.
                 * @return The address as a string.
                 */
                String toString() const;

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
                /**
                 * @brief Fills a sockaddr_storage with this address.
                 *
                 * Only works for resolved IPv4 or IPv6 addresses.
                 * The port field in the sockaddr is set to zero; callers
                 * should set it separately via sin_port / sin6_port.
                 *
                 * @param[out] storage The structure to fill.
                 * @return The size of the filled sockaddr structure
                 *         (sizeof(sockaddr_in) or sizeof(sockaddr_in6)),
                 *         or 0 if the address is null or unresolved.
                 */
                size_t toSockAddr(struct sockaddr_storage *storage) const;
#endif

                /** @brief Equality comparison. */
                bool operator==(const NetworkAddress &other) const { return _data == other._data; }
                /** @brief Inequality comparison. */
                bool operator!=(const NetworkAddress &other) const { return _data != other._data; }

        private:
                std::variant<std::monostate, Ipv4Address, Ipv6Address, String> _data;
};

/** @brief Writes the network address to the stream. */
TextStream &operator<<(TextStream &stream, const NetworkAddress &addr);

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::NetworkAddress);
