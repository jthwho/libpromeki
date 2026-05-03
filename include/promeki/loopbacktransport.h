/**
 * @file      loopbacktransport.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/packettransport.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/pair.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief In-process @ref PacketTransport implementation for tests.
 * @ingroup network
 *
 * LoopbackTransport lets RtpSession and the RTP MediaIO backends run
 * end-to-end inside a unit test without touching the kernel network
 * stack.  A pair of LoopbackTransport instances is linked via
 * @ref pair(), after which any packet sent on one is immediately
 * enqueued on the other's receive queue.
 *
 * There is no kernel involvement and no delivery latency: the send
 * path copies the packet into an internal @ref Buffer and pushes it
 * onto the peer's FIFO.  @ref receivePacket() pops the next entry.
 *
 * Pacing and transmit-time controls are accepted but ignored (they
 * return @ref Error::Ok so test code can exercise the same code
 * paths as the UDP transport).
 *
 * @par Example
 * @code
 * LoopbackTransport tx, rx;
 * LoopbackTransport::pair(&tx, &rx);
 * tx.open();
 * rx.open();
 *
 * const char msg[] = "hello";
 * tx.sendPacket(msg, sizeof(msg), SocketAddress::localhost(0));
 *
 * char buf[64];
 * SocketAddress from;
 * ssize_t n = rx.receivePacket(buf, sizeof(buf), &from);
 * @endcode
 *
 * @par Thread Safety
 * Inherits @ref PacketTransport &mdash; the send and receive sides may
 * safely run on separate threads (the internal FIFO is mutex-
 * guarded), but multiple concurrent senders or multiple concurrent
 * receivers on the same instance must be externally synchronized.
 */
class LoopbackTransport : public PacketTransport {
        public:
                /** @brief Constructs an unopened loopback transport. */
                LoopbackTransport();

                /** @brief Destructor. */
                ~LoopbackTransport() override;

                /**
                 * @brief Links two transports as a bidirectional pair.
                 *
                 * After pairing, packets sent on @p a arrive on @p b
                 * and vice-versa.  Pairing must be done before either
                 * transport is opened.
                 *
                 * @param a First transport.
                 * @param b Second transport.
                 */
                static void pair(LoopbackTransport *a, LoopbackTransport *b);

                /** @copydoc PacketTransport::open() */
                Error open() override;

                /** @copydoc PacketTransport::close() */
                void close() override;

                /** @copydoc PacketTransport::isOpen() */
                bool isOpen() const override { return _open; }

                /** @copydoc PacketTransport::sendPacket() */
                ssize_t sendPacket(const void *data, size_t size, const SocketAddress &dest) override;

                /** @copydoc PacketTransport::sendPackets() */
                int sendPackets(const DatagramList &datagrams) override;

                /** @copydoc PacketTransport::receivePacket() */
                ssize_t receivePacket(void *data, size_t maxSize, SocketAddress *sender = nullptr) override;

                /** @copydoc PacketTransport::setPacingRate() */
                Error setPacingRate(uint64_t bytesPerSec) override;

                /** @copydoc PacketTransport::setTxTime() */
                Error setTxTime(bool enable) override;

                /** @brief Returns the number of packets waiting to be received. */
                size_t pendingPackets() const { return _recvQueue.size(); }

        private:
                /** @brief Entry in the internal receive queue. */
                struct QueueEntry {
                                Buffer        data;   ///< @brief Captured packet bytes.
                                SocketAddress sender; ///< @brief Peer's identity, echoed from the send call.
                };

                /** @brief Called by the sending peer to enqueue a packet. */
                void deliver(const void *data, size_t size, const SocketAddress &sender);

                LoopbackTransport *_peer = nullptr;
                List<QueueEntry>   _recvQueue;
                bool               _open = false;
};

PROMEKI_NAMESPACE_END
