/**
 * @file      packettransport.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/socketaddress.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract packet-I/O transport for datagram-oriented protocols.
 * @ingroup network
 *
 * PacketTransport decouples higher-level packet protocols (RtpSession,
 * future RTCP, SRT, etc.) from the concrete kernel or userspace path
 * that actually moves bytes.  It is the layer where kernel socket vs.
 * DPDK vs. AF_XDP vs. in-process loopback is chosen, so that the RTP
 * and MediaIO layers never talk to sockets directly and can be reused
 * unchanged across transport backends.
 *
 * The interface is deliberately narrow:
 *
 *  - @ref open() / @ref close() acquire and release whatever the
 *    transport needs (a kernel socket, a DPDK port, a loopback queue).
 *  - @ref sendPacket() and @ref sendPackets() move outbound datagrams
 *    to one or many destinations.  The batch form is the primary API
 *    because DPDK and @c sendmmsg() both prefer to see many packets at
 *    once; the single-packet form exists for convenience.
 *  - @ref receivePacket() reads one inbound datagram.
 *  - @ref setPacingRate() and @ref setTxTime() expose optional
 *    transmit-rate and per-packet deadline controls.  Backends that
 *    cannot implement them return @ref Error::NotSupported.
 *
 * @par Lifetime
 *
 * Transports are owned by the caller.  They are *not* reference
 * counted — an RtpSession or MediaIO backend that takes a transport
 * pointer does not own it; the caller is responsible for keeping it
 * alive until the session is stopped.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct transport instances may be
 * used concurrently.  A single instance must only be used from one
 * thread at a time — like the underlying socket, no internal
 * locking is performed on send / receive paths.
 *
 * @par Buffer lifetime
 *
 * When @ref sendPackets() accepts a @ref Datagram list, the backing
 * data pointers must remain valid until the call returns.  Socket
 * backends copy into kernel buffers synchronously, so this contract
 * is free for the caller; DPDK backends must copy or refcount before
 * returning, not defer the copy.
 */
class PacketTransport {
        public:
                /** @brief Unique-ownership pointer to a PacketTransport. */
                using UPtr = UniquePtr<PacketTransport>;

                /**
                 * @brief Describes one outbound datagram.
                 *
                 * Identical in shape to @ref UdpSocket::Datagram so
                 * that the UDP-backed transport can pass the list
                 * through unchanged.  Other backends build their own
                 * mbuf / ring-buffer entries from these fields.
                 */
                struct Datagram {
                                /** @brief Pointer to the packet bytes (caller-owned, contiguous). */
                                const void *data = nullptr;
                                /** @brief Number of bytes to send. */
                                size_t size = 0;
                                /** @brief Destination address and port. */
                                SocketAddress dest;
                                /**
                         * @brief Optional SCM_TXTIME nanoseconds-since-epoch deadline.
                         *
                         * When zero the datagram is sent immediately.
                         * When non-zero and the transport supports it
                         * (see @ref setTxTime()), the kernel / NIC
                         * holds the packet until the target time.
                         */
                                uint64_t txTimeNs = 0;
                };

                /** @brief List of datagrams for batch send. */
                using DatagramList = List<Datagram>;

                /** @brief Virtual destructor. */
                virtual ~PacketTransport() = default;

                /**
                 * @brief Opens the transport and acquires its resources.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error open() = 0;

                /** @brief Closes the transport and releases its resources. */
                virtual void close() = 0;

                /** @brief Returns true if the transport is open. */
                virtual bool isOpen() const = 0;

                /**
                 * @brief Sends a single packet to the given destination.
                 * @param data The packet bytes.
                 * @param size The packet size in bytes.
                 * @param dest The destination address and port.
                 * @return The number of bytes sent, or -1 on failure.
                 */
                virtual ssize_t sendPacket(const void *data, size_t size, const SocketAddress &dest) = 0;

                /**
                 * @brief Sends a batch of packets.
                 *
                 * Backends implement this as @c sendmmsg() on Linux
                 * sockets, as a ring-burst on DPDK, or as a queue
                 * enqueue on loopback.  The call returns the number
                 * of datagrams the transport accepted before
                 * erroring; a fully successful call returns
                 * @c datagrams.size().
                 *
                 * @param datagrams The list of datagrams to send.
                 * @return The count of accepted datagrams, or -1 if
                 *         none were sent.
                 */
                virtual int sendPackets(const DatagramList &datagrams) = 0;

                /**
                 * @brief Receives one inbound packet.
                 * @param data The destination buffer.
                 * @param maxSize The size of @p data in bytes.
                 * @param[out] sender If non-null, receives the
                 *                    sender's address.
                 * @return The number of bytes received, or -1 on
                 *         failure.
                 */
                virtual ssize_t receivePacket(void *data, size_t maxSize, SocketAddress *sender = nullptr) = 0;

                /**
                 * @brief Sets a transmit-rate limit on this transport.
                 *
                 * Backends that support rate limiting (e.g. kernel
                 * @c SO_MAX_PACING_RATE + @c fq qdisc) will pace
                 * outbound packets to the requested rate.  The
                 * default implementation returns
                 * @ref Error::NotSupported.
                 *
                 * @param bytesPerSec Maximum transmit rate in bytes/sec.
                 * @return Error::Ok on success, Error::NotSupported
                 *         if the backend does not implement it.
                 */
                virtual Error setPacingRate(uint64_t bytesPerSec);

                /**
                 * @brief Enables per-packet transmit-time scheduling.
                 *
                 * When enabled, the @ref Datagram::txTimeNs field is
                 * honored and packets are released according to their
                 * target time.  The default implementation returns
                 * @ref Error::NotSupported.
                 *
                 * @param enable True to enable, false to disable.
                 * @return Error::Ok on success, Error::NotSupported
                 *         if the backend does not implement it.
                 */
                virtual Error setTxTime(bool enable);

        protected:
                /** @brief Protected constructor — instantiate a concrete subclass. */
                PacketTransport() = default;

                PacketTransport(const PacketTransport &) = delete;
                PacketTransport &operator=(const PacketTransport &) = delete;
                PacketTransport(PacketTransport &&) = delete;
                PacketTransport &operator=(PacketTransport &&) = delete;
};

PROMEKI_NAMESPACE_END
