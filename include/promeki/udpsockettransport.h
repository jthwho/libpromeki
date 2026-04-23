/**
 * @file      udpsockettransport.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/packettransport.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief UDP socket implementation of @ref PacketTransport.
 * @ingroup network
 *
 * UdpSocketTransport owns a kernel @ref UdpSocket and delegates every
 * @ref PacketTransport operation to it.  This is the default
 * transport used by @ref RtpSession and the RTP @ref MediaIO
 * backends; it supports kernel pacing via @c SO_MAX_PACING_RATE,
 * batch send via @c sendmmsg(), and transmit-time scheduling via
 * @c SO_TXTIME on platforms that implement those options.
 *
 * @par Configuration
 *
 * Configuration is applied at @ref open() time based on members set
 * before the call.  The bind address defaults to @c 0.0.0.0:0.
 * Optional knobs include multicast TTL, multicast outgoing
 * interface, DSCP marking, and IPv6 mode.
 *
 * @par Example
 * @code
 * UdpSocketTransport transport;
 * transport.setLocalAddress(SocketAddress::any(0));
 * transport.setDscp(46);
 * Error err = transport.open();
 *
 * PacketTransport::Datagram d;
 * d.data = payload.data();
 * d.size = payload.size();
 * d.dest = SocketAddress(Ipv4Address(239, 0, 0, 1), 5004);
 * PacketTransport::DatagramList batch;
 * batch.pushToBack(d);
 * transport.sendPackets(batch);
 * @endcode
 */
class UdpSocketTransport : public PacketTransport {
        public:
                /** @brief Unique-ownership pointer to a UdpSocketTransport. */
                using UPtr = UniquePtr<UdpSocketTransport>;

                /** @brief Constructs an unopened UDP transport. */
                UdpSocketTransport();

                /** @brief Destructor. Closes the transport if open. */
                ~UdpSocketTransport() override;

                /**
                 * @brief Sets the local bind address.
                 *
                 * Must be set before @ref open().  Defaults to
                 * @c SocketAddress::any(0), i.e. bind to any
                 * interface on an OS-assigned port.
                 *
                 * @param addr The local address.
                 */
                void setLocalAddress(const SocketAddress &addr) { _localAddress = addr; }

                /** @brief Returns the configured local address. */
                const SocketAddress &localAddress() const { return _localAddress; }

                /**
                 * @brief Enables IPv6 mode (default IPv4).
                 * @param enable True for IPv6, false for IPv4.
                 */
                void setIpv6(bool enable) { _ipv6 = enable; }

                /** @brief Returns true if IPv6 mode is enabled. */
                bool isIpv6() const { return _ipv6; }

                /**
                 * @brief Sets the DSCP (Differentiated Services) value.
                 * @param dscp The 6-bit DSCP value (0-63). 0 disables.
                 */
                void setDscp(uint8_t dscp) { _dscp = dscp; }

                /** @brief Returns the DSCP value. */
                uint8_t dscp() const { return _dscp; }

                /**
                 * @brief Sets the multicast TTL.
                 * @param ttl The TTL value (1-255). 0 = use default.
                 */
                void setMulticastTTL(int ttl) { _multicastTTL = ttl; }

                /** @brief Returns the multicast TTL. */
                int multicastTTL() const { return _multicastTTL; }

                /**
                 * @brief Sets the multicast outgoing interface.
                 * @param iface The interface name. Empty = default.
                 */
                void setMulticastInterface(const String &iface) { _multicastInterface = iface; }

                /** @brief Returns the multicast interface name. */
                const String &multicastInterface() const { return _multicastInterface; }

                /**
                 * @brief Enables multicast loopback.
                 *
                 * When enabled, multicast packets sent on this
                 * transport loop back to local receivers on the same
                 * host.  Useful for in-box testing.
                 *
                 * @param enable True to enable loopback.
                 */
                void setMulticastLoopback(bool enable) { _multicastLoopback = enable; }

                /** @brief Returns true if multicast loopback is enabled. */
                bool multicastLoopback() const { return _multicastLoopback; }

                /**
                 * @brief Enables @c SO_REUSEADDR on the underlying socket.
                 * @param enable True to enable.
                 */
                void setReuseAddress(bool enable) { _reuseAddress = enable; }

                /** @brief Returns true if @c SO_REUSEADDR is enabled. */
                bool reuseAddress() const { return _reuseAddress; }

                /**
                 * @brief Returns the underlying UdpSocket (valid only after @ref open()).
                 *
                 * Exposed for callers that need to configure a socket
                 * option not already surfaced through this class
                 * (e.g. joining a multicast group for the receive
                 * path).  Do not use it to send packets directly —
                 * that belongs in @ref sendPacket() / @ref sendPackets().
                 *
                 * @return The socket, or nullptr if not open.
                 */
                UdpSocket *socket() const { return _socket.get(); }

                /** @copydoc PacketTransport::open() */
                Error open() override;

                /** @copydoc PacketTransport::close() */
                void close() override;

                /** @copydoc PacketTransport::isOpen() */
                bool isOpen() const override;

                /** @copydoc PacketTransport::sendPacket() */
                ssize_t sendPacket(const void *data, size_t size,
                                   const SocketAddress &dest) override;

                /** @copydoc PacketTransport::sendPackets() */
                int sendPackets(const DatagramList &datagrams) override;

                /** @copydoc PacketTransport::receivePacket() */
                ssize_t receivePacket(void *data, size_t maxSize,
                                      SocketAddress *sender = nullptr) override;

                /** @copydoc PacketTransport::setPacingRate() */
                Error setPacingRate(uint64_t bytesPerSec) override;

                /** @copydoc PacketTransport::setTxTime() */
                Error setTxTime(bool enable) override;

        private:
                UdpSocket::UPtr _socket;
                SocketAddress   _localAddress;
                String          _multicastInterface;
                uint8_t         _dscp = 0;
                int             _multicastTTL = 0;
                bool            _ipv6 = false;
                bool            _reuseAddress = false;
                bool            _multicastLoopback = false;
};

PROMEKI_NAMESPACE_END
