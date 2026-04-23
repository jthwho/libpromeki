/**
 * @file      udpsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/abstractsocket.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Datagram-oriented UDP socket with multicast support.
 * @ingroup network
 *
 * UdpSocket provides connectionless datagram communication over UDP.
 * It supports unicast and multicast send/receive, including joining
 * and leaving multicast groups with optional interface binding.
 *
 * For connected-mode UDP (via connectToHost()), the standard
 * IODevice read()/write() methods can be used. For connectionless
 * operation, use writeDatagram()/readDatagram() with explicit
 * destination/source addresses.
 *
 * This class must only be used from the thread that created it
 * (or moved to via moveToThread()).
 *
 * @par Example
 * @code
 * UdpSocket sock;
 * sock.open(IODevice::ReadWrite);
 * sock.bind(SocketAddress::any(5004));
 *
 * // Send a datagram
 * SocketAddress dest(Ipv4Address(239, 0, 0, 1), 5004);
 * sock.writeDatagram("hello", 5, dest);
 *
 * // Receive a datagram
 * char buf[1500];
 * SocketAddress sender;
 * ssize_t n = sock.readDatagram(buf, sizeof(buf), &sender);
 * @endcode
 */
class UdpSocket : public AbstractSocket {
        PROMEKI_OBJECT(UdpSocket, AbstractSocket)
        public:
                /** @brief Unique-ownership pointer to a UdpSocket. */
                using UPtr = UniquePtr<UdpSocket>;

                /**
                 * @brief Describes a single datagram for batch-send APIs.
                 *
                 * Used by @ref writeDatagrams() to submit many datagrams
                 * in one syscall.  The @c data pointer must remain valid
                 * until the call returns.  When @c txTimeNs is non-zero
                 * and the socket has transmit-time enabled, the kernel
                 * (via the ETF qdisc) will hold the packet until the
                 * requested send time.
                 */
                struct Datagram {
                        const void     *data = nullptr; ///< @brief Pointer to the packet bytes (caller-owned).
                        size_t          size = 0;       ///< @brief Number of bytes to send.
                        SocketAddress   dest;           ///< @brief Destination address and port.
                        uint64_t        txTimeNs = 0;   ///< @brief Optional SCM_TXTIME nanoseconds since epoch (0 = immediate).
                };

                /** @brief List of datagrams for batch send. */
                using DatagramList = List<Datagram>;

                /** @brief Value returned by @ref setPacingRate() to disable pacing. */
                static constexpr uint64_t PacingRateUnlimited = ~static_cast<uint64_t>(0);

                /**
                 * @brief Constructs a UdpSocket.
                 * @param parent The parent object, or nullptr.
                 */
                UdpSocket(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~UdpSocket() override;

                /**
                 * @brief Opens the socket.
                 *
                 * Creates an AF_INET SOCK_DGRAM socket. Use openIpv6()
                 * if IPv6 is needed.
                 *
                 * @param mode The open mode (typically ReadWrite).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(OpenMode mode) override;

                /**
                 * @brief Opens the socket for IPv6 operation.
                 *
                 * Creates an AF_INET6 SOCK_DGRAM socket. By default,
                 * IPV6_V6ONLY is disabled so IPv4-mapped addresses work.
                 *
                 * @param mode The open mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error openIpv6(OpenMode mode);

                /**
                 * @brief Closes the socket.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error close() override;

                /** @brief Returns true if the socket is open. */
                bool isOpen() const override { return _fd >= 0; }

                /**
                 * @brief Reads data from a connected UDP socket.
                 *
                 * Requires a prior connectToHost() call.
                 *
                 * @param data Buffer to read into.
                 * @param maxSize Maximum bytes to read.
                 * @return Bytes read, or -1 on error.
                 */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Writes data to a connected UDP socket.
                 *
                 * Requires a prior connectToHost() call.
                 *
                 * @param data Data to send.
                 * @param maxSize Bytes to send.
                 * @return Bytes sent, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /**
                 * @brief Returns the number of bytes available for reading.
                 * @return Bytes available, or 0 if unknown.
                 */
                int64_t bytesAvailable() const override;

                /**
                 * @brief Sends a datagram to a specific destination.
                 * @param data Pointer to the data.
                 * @param size Number of bytes to send.
                 * @param dest The destination address and port.
                 * @return Bytes sent, or -1 on error.
                 */
                ssize_t writeDatagram(const void *data, size_t size, const SocketAddress &dest);

                /**
                 * @brief Sends a Buffer as a datagram.
                 * @param data The buffer to send.
                 * @param dest The destination address and port.
                 * @return Bytes sent, or -1 on error.
                 */
                ssize_t writeDatagram(const Buffer &data, const SocketAddress &dest);

                /**
                 * @brief Sends a batch of datagrams in one syscall.
                 *
                 * On Linux this uses @c sendmmsg(), which hands the
                 * kernel the entire batch in a single system call and
                 * avoids the per-packet context-switch cost of looping
                 * on @c sendto().  On platforms without @c sendmmsg()
                 * the implementation falls back to a @c sendto() loop
                 * with equivalent semantics.
                 *
                 * Each datagram's @c data pointer must remain valid
                 * until this call returns.  The kernel copies into its
                 * own buffers before this function returns; callers do
                 * not need to keep the data alive afterwards.
                 *
                 * If @c txTimeNs is non-zero and transmit-time is
                 * enabled (see @ref setTxTime()), the datagram carries
                 * a @c SCM_TXTIME cmsg and is held by the ETF qdisc
                 * until the target time.
                 *
                 * On partial failure the kernel reports how many
                 * datagrams it accepted before erroring; this call
                 * returns that count.  Callers that care about
                 * completeness should compare against
                 * @c datagrams.size().
                 *
                 * @param datagrams The list of datagrams to send.
                 * @return The number of datagrams accepted by the
                 *         kernel, or -1 if none were sent.
                 */
                int writeDatagrams(const DatagramList &datagrams);

                /**
                 * @brief Sets a per-socket transmit rate limit.
                 *
                 * Uses @c SO_MAX_PACING_RATE, which is enforced by the
                 * @c fq qdisc on Linux (default on modern kernels).
                 * Packets submitted via @c writeDatagram() or
                 * @c writeDatagrams() beyond the configured rate are
                 * held by the qdisc and released on schedule, with
                 * zero per-packet CPU cost.
                 *
                 * Pass @ref PacingRateUnlimited to disable pacing.
                 *
                 * @param bytesPerSec Maximum transmit rate in bytes/sec.
                 * @return Error::Ok on success, Error::NotSupported if
                 *         the platform does not implement it, or
                 *         another error on failure.
                 */
                Error setPacingRate(uint64_t bytesPerSec);

                /**
                 * @brief Clears the transmit rate limit set by @ref setPacingRate().
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error clearPacingRate();

                /**
                 * @brief Enables per-packet transmit-time scheduling.
                 *
                 * Sets @c SO_TXTIME on Linux, which lets individual
                 * datagrams carry a @c SCM_TXTIME nanosecond deadline
                 * via @ref Datagram::txTimeNs.  Requires the ETF qdisc;
                 * works best with NICs that support hardware TX
                 * scheduling (Intel i210, i225).  Used by ST 2110-21
                 * sender pacing and by precise AES67 pacing.
                 *
                 * Disabling is a no-op — once the caller stops passing
                 * a non-zero @ref Datagram::txTimeNs, subsequent sends
                 * behave like plain UDP without reconfiguring the
                 * socket.
                 *
                 * @param enable True to enable, false to disable.
                 * @param clockId Clock ID for the deadlines (defaults
                 *                to @c CLOCK_TAI to match PTP).
                 * @return Error::Ok on success, Error::NotSupported if
                 *         the platform does not implement it, or
                 *         another error on failure.
                 */
                Error setTxTime(bool enable, int clockId = 11 /* CLOCK_TAI */);

                /**
                 * @brief Receives a datagram.
                 * @param data Buffer to receive into.
                 * @param maxSize Maximum bytes to receive.
                 * @param[out] sender If not null, receives the sender's address.
                 * @return Bytes received, or -1 on error.
                 */
                ssize_t readDatagram(void *data, size_t maxSize, SocketAddress *sender = nullptr);

                /**
                 * @brief Returns true if there are pending datagrams to read.
                 * @return True if a datagram is available.
                 */
                bool hasPendingDatagrams() const;

                /**
                 * @brief Returns the size of the next pending datagram.
                 * @return The datagram size in bytes, or -1 if no datagram.
                 */
                ssize_t pendingDatagramSize() const;

                /**
                 * @brief Joins a multicast group.
                 * @param group The multicast group address and port.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error joinMulticastGroup(const SocketAddress &group);

                /**
                 * @brief Joins a multicast group on a specific interface.
                 * @param group The multicast group address.
                 * @param iface The network interface name (e.g. "eth0").
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error joinMulticastGroup(const SocketAddress &group, const String &iface);

                /**
                 * @brief Leaves a multicast group.
                 * @param group The multicast group address.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error leaveMulticastGroup(const SocketAddress &group);

                /**
                 * @brief Sets the multicast TTL (time to live).
                 * @param ttl The TTL value (1-255).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setMulticastTTL(int ttl);

                /**
                 * @brief Enables or disables multicast loopback.
                 *
                 * When enabled, multicast packets sent on this socket are
                 * looped back to local receivers on the same host.
                 *
                 * @param enable True to enable loopback, false to disable.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setMulticastLoopback(bool enable);

                /**
                 * @brief Sets the outgoing multicast interface.
                 * @param iface The network interface name (e.g. "eth0").
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setMulticastInterface(const String &iface);

                /**
                 * @brief Enables or disables SO_REUSEADDR.
                 *
                 * Must be called before bind(). Required for multiple
                 * processes to receive from the same multicast group+port.
                 *
                 * @param enable True to enable, false to disable.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setReuseAddress(bool enable);

                /**
                 * @brief Sets the DSCP (Differentiated Services) value.
                 *
                 * Used for QoS marking on outgoing packets. The value is
                 * the 6-bit DSCP field shifted left by 2 to form the full
                 * TOS byte (e.g. AF41 = 0x22 << 2 = 0x88, but pass 0x22).
                 *
                 * @param dscp The DSCP value (0-63).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setDscp(uint8_t dscp);

        private:
                int _domain = 0;       ///< Address family (AF_INET or AF_INET6).
};

PROMEKI_NAMESPACE_END
