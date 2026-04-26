/**
 * @file      socketaddress.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/result.h>
#include <promeki/platform.h>
#include <promeki/networkaddress.h>

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
 * @brief Network address with port number.
 * @ingroup network
 *
 * SocketAddress combines a NetworkAddress (IPv4, IPv6, or hostname)
 * with a 16-bit port number. This is the address type used by all
 * socket classes for bind, connect, and send/receive operations.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @par Example
 * @code
 * auto [addr, err] = SocketAddress::fromString("192.168.1.1:5004");
 * CHECK(addr.address().isIPv4());
 * CHECK(addr.port() == 5004);
 *
 * SocketAddress mc(Ipv4Address(239, 0, 0, 1), 5004);
 * CHECK(mc.isMulticast());
 * @endcode
 */
class SocketAddress {
        public:
                /**
                 * @brief Parses a "host:port" string into a SocketAddress.
                 *
                 * Supports the following formats:
                 * - `"192.168.1.1:5004"` — IPv4 with port
                 * - `"[::1]:5004"` — IPv6 with port (brackets required)
                 * - `"hostname:5004"` — hostname with port
                 *
                 * @param hostPort The string to parse.
                 * @return A Result containing the parsed address and Error::Ok,
                 *         or a null address and Error::Invalid on parse failure.
                 */
                static Result<SocketAddress> fromString(const String &hostPort);

                /**
                 * @brief Returns INADDR_ANY with the given port.
                 * @param port The port number.
                 * @return A SocketAddress bound to any interface on the given port.
                 */
                static SocketAddress any(uint16_t port) { return SocketAddress(Ipv4Address::any(), port); }

                /**
                 * @brief Returns localhost (127.0.0.1) with the given port.
                 * @param port The port number.
                 * @return A SocketAddress bound to localhost on the given port.
                 */
                static SocketAddress localhost(uint16_t port) { return SocketAddress(Ipv4Address::loopback(), port); }

                /** @brief Default constructor. Creates a null address with port 0. */
                SocketAddress() = default;

                /**
                 * @brief Constructs from a NetworkAddress and port.
                 * @param address The network address.
                 * @param port The port number.
                 */
                SocketAddress(const NetworkAddress &address, uint16_t port) : _address(address), _port(port) {}

                /**
                 * @brief Constructs from an IPv4 address and port.
                 * @param addr The IPv4 address.
                 * @param port The port number.
                 */
                SocketAddress(const Ipv4Address &addr, uint16_t port) : _address(addr), _port(port) {}

                /**
                 * @brief Constructs from an IPv6 address and port.
                 * @param addr The IPv6 address.
                 * @param port The port number.
                 */
                SocketAddress(const Ipv6Address &addr, uint16_t port) : _address(addr), _port(port) {}

                /** @brief Returns the network address component. */
                const NetworkAddress &address() const { return _address; }

                /** @brief Sets the network address component. */
                void setAddress(const NetworkAddress &address) { _address = address; }

                /** @brief Returns the port number. */
                uint16_t port() const { return _port; }

                /** @brief Sets the port number. */
                void setPort(uint16_t port) { _port = port; }

                /** @brief Returns true if no address is set and port is 0. */
                bool isNull() const { return _address.isNull() && _port == 0; }

                /** @brief Returns true if the address is IPv4. */
                bool isIPv4() const { return _address.isIPv4(); }

                /** @brief Returns true if the address is IPv6. */
                bool isIPv6() const { return _address.isIPv6(); }

                /** @brief Returns true if the address is a loopback address. */
                bool isLoopback() const { return _address.isLoopback(); }

                /** @brief Returns true if the address is a multicast address. */
                bool isMulticast() const { return _address.isMulticast(); }

                /**
                 * @brief Returns a "host:port" string representation.
                 *
                 * IPv6 addresses are enclosed in brackets: `"[::1]:5004"`.
                 * @return The address as a string.
                 */
                String toString() const;

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)
                /**
                 * @brief Constructs from a POSIX/Windows sockaddr structure.
                 * @param addr Pointer to a sockaddr (sockaddr_in or sockaddr_in6).
                 * @param len Length of the sockaddr structure.
                 * @return A Result containing the address and Error::Ok, or
                 *         Error::Invalid if the sockaddr is not recognized.
                 */
                static Result<SocketAddress> fromSockAddr(const struct sockaddr *addr, size_t len);

                /**
                 * @brief Fills a sockaddr_storage with this address and port.
                 * @param[out] storage The structure to fill.
                 * @return The size of the filled sockaddr structure, or 0 on failure.
                 */
                size_t toSockAddr(struct sockaddr_storage *storage) const;
#endif

                /** @brief Equality comparison. */
                bool operator==(const SocketAddress &other) const {
                        return _address == other._address && _port == other._port;
                }

                /** @brief Inequality comparison. */
                bool operator!=(const SocketAddress &other) const { return !(*this == other); }

        private:
                NetworkAddress _address;
                uint16_t       _port = 0;
};

/** @brief Writes the socket address to the stream. */
TextStream &operator<<(TextStream &stream, const SocketAddress &addr);

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::SocketAddress);
