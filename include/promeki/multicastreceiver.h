/**
 * @file      multicastreceiver.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/multicastmanager.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Standalone multicast datagram receiver with an owned worker thread.
 * @ingroup network
 *
 * MulticastReceiver is a reusable primitive for any subsystem that
 * needs to consume UDP datagrams from one or more multicast groups
 * without having to build its own socket + thread plumbing.  It owns
 * a @ref UdpSocket bound to a caller-chosen port, joins any number
 * of Any-Source (ASM) or Source-Specific (SSM) groups on a single
 * interface, spawns a dedicated receive @ref Thread, and delivers
 * every arriving datagram to a user callback.
 *
 * Downstream consumers currently targeted:
 *  - @ref MediaIO_Rtp reader mode (one receiver per video / audio
 *    / data RTP stream whose destination happens to be a multicast
 *    group).
 *  - SAP / SDP announcement listener (joins @c 224.2.127.254:9875
 *    and hands each SAP PDU to the caller).
 *  - mDNS responder / resolver (joins @c 224.0.0.251:5353).
 *  - Any future AV-over-IP discovery / control protocol.
 *
 * @par Threading model
 *
 * The receive loop runs on a promeki @ref Thread subclass named by
 * the caller (via @ref setThreadName, default @c "multicast-rx").
 * The thread does not use its built-in @ref EventLoop — it blocks
 * in @ref UdpSocket::readDatagram with a short receive timeout so
 * the stop flag can be polled between datagrams.  The thread is
 * joined automatically in @ref stop() and in the destructor.
 *
 * @par Delivery
 *
 * The datagram callback is invoked from the receive thread.  It
 * receives:
 *  - a @ref Buffer "Buffer::Ptr" whose backing allocation is owned
 *    by the receiver and valid only for the duration of the call —
 *    if the consumer needs to keep the bytes longer it must copy
 *    them or take a fresh @ref Buffer::Ptr::create copy of the data.
 *  - the sender's @ref SocketAddress.
 *
 * Callbacks should be fast and non-blocking.  Work that cannot
 * complete inline (decoding, reassembly, upstream signalling) must
 * be pushed onto a queue for another thread to consume.
 *
 * @par Example — SAP listener skeleton
 * @code
 * MulticastReceiver rx;
 * rx.setLocalAddress(SocketAddress::any(9875));
 * rx.setThreadName("sap-rx");
 * rx.setDatagramCallback([](Buffer::Ptr data, const SocketAddress &sender) {
 *     // Parse the SAP PDU; hand the embedded SDP to the app.
 * });
 * Error err = rx.addGroup(SocketAddress(Ipv4Address(224, 2, 127, 254), 9875));
 * if(err.isError()) return err;
 * err = rx.start();
 * if(err.isError()) return err;
 * // ... later ...
 * rx.stop();
 * @endcode
 *
 * @par Lifetime
 *
 * Configuration (local address, interface, groups, callback) must be
 * set before @ref start().  Once running, @ref stop() is the only
 * legal mutator; reconfiguration requires a stop / reconfigure /
 * start cycle.  The destructor calls @ref stop automatically.
 *
 * @par Thread Safety
 * Configuration setters and @ref start / @ref stop are intended to
 * be called from the receiver's owning thread.  The user callback
 * is invoked on the dedicated receive thread spawned internally —
 * the callback's body must be thread-safe with respect to whatever
 * state it touches.
 */
class MulticastReceiver : public Thread {
                PROMEKI_OBJECT(MulticastReceiver, Thread)
        public:
                /**
                 * @brief Signature of the user callback invoked for each datagram.
                 *
                 * @param data   The received datagram bytes.  The @ref Buffer::Ptr
                 *               owns its allocation; the backing memory is valid
                 *               for the duration of the call.  Consumers that need
                 *               to retain the bytes should either keep the shared
                 *               pointer or copy the data out.
                 * @param sender The sender's address and port.
                 */
                using DatagramCallback = std::function<void(Buffer::Ptr data, const SocketAddress &sender)>;

                /**
                 * @brief Describes one group membership request.
                 *
                 * ASM entries set @c source to a null @ref SocketAddress.
                 * SSM entries populate @c source with the permitted
                 * source address.  Added via @ref addGroup or
                 * @ref addSourceGroup and applied when @ref start runs.
                 */
                struct GroupEntry {
                                /** @brief The multicast group address. */
                                SocketAddress group;
                                /** @brief SSM source; null for ASM. */
                                SocketAddress source;
                                /** @brief True if this entry is Source-Specific Multicast. */
                                bool isSSM = false;
                };

                /** @brief List of configured group memberships. */
                using GroupList = List<GroupEntry>;

                /** @brief Default receive buffer size in bytes. */
                static constexpr size_t DefaultMaxPacketSize = 2048;

                /** @brief Default poll interval used to check the stop flag, in ms. */
                static constexpr unsigned int DefaultReceiveTimeoutMs = 200;

                /**
                 * @brief Constructs an unstarted MulticastReceiver.
                 * @param parent Optional parent ObjectBase.
                 */
                MulticastReceiver(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Stops the receive thread if running. */
                ~MulticastReceiver() override;

                /**
                 * @brief Sets the local bind address (and port).
                 *
                 * Must be called before @ref start.  Defaults to
                 * @c SocketAddress::any(0) — callers should set a
                 * specific port matching the multicast service they
                 * want to receive on.
                 *
                 * @param address The local address.
                 */
                void setLocalAddress(const SocketAddress &address);

                /** @brief Returns the configured local address. */
                const SocketAddress &localAddress() const { return _localAddress; }

                /**
                 * @brief Sets the interface name used for group joins.
                 *
                 * When empty (the default) the kernel picks the
                 * routing-table default.  Must be set before
                 * @ref start.
                 *
                 * @param iface The interface name (e.g. @c "eth0").
                 */
                void setInterface(const String &iface);

                /** @brief Returns the configured multicast interface name. */
                const String &interfaceName() const { return _interfaceName; }

                /**
                 * @brief Sets the maximum datagram size the receiver will accept.
                 *
                 * Datagrams larger than this are truncated.  Default is
                 * @ref DefaultMaxPacketSize.  Must be set before
                 * @ref start.
                 *
                 * @param bytes Maximum datagram size in bytes.
                 */
                void setMaxPacketSize(size_t bytes);

                /** @brief Returns the maximum datagram size. */
                size_t maxPacketSize() const { return _maxPacketSize; }

                /**
                 * @brief Sets the poll interval used to check the stop flag.
                 *
                 * The underlying socket uses @c SO_RCVTIMEO set to this
                 * value so the receive loop wakes up periodically even
                 * when no packets are arriving.  Lower values reduce
                 * stop-response latency at the cost of more syscalls
                 * on idle streams.  Default is
                 * @ref DefaultReceiveTimeoutMs.
                 *
                 * @param timeoutMs Timeout in milliseconds.
                 */
                void setReceiveTimeout(unsigned int timeoutMs);

                /** @brief Returns the receive-timeout poll interval. */
                unsigned int receiveTimeout() const { return _receiveTimeoutMs; }

                /**
                 * @brief Sets the name applied to the receive @ref Thread.
                 *
                 * Helps debugging: the name shows up in @c top @c -H,
                 * @c htop, @c perf, and the Linux kernel scheduler
                 * tracepoints.  Default is @c "multicast-rx".
                 *
                 * @param name Thread name (truncated by the OS to ~15 chars).
                 */
                void setThreadName(const String &name);

                /** @brief Returns the configured thread name. */
                const String &threadName() const { return _threadName; }

                /**
                 * @brief Installs the datagram delivery callback.
                 *
                 * Must be set before @ref start.  Callbacks run on the
                 * receive thread and must not block.
                 *
                 * @param callback The per-datagram callback.
                 */
                void setDatagramCallback(DatagramCallback callback);

                /**
                 * @brief Adds an Any-Source Multicast group to join at start.
                 *
                 * @param group The multicast group address and port.
                 * @return Error::Ok on success, Error::Invalid if @p group
                 *         is not a valid multicast address.
                 */
                Error addGroup(const SocketAddress &group);

                /**
                 * @brief Adds a Source-Specific Multicast group to join at start.
                 *
                 * @param group  The multicast group address and port.
                 * @param source The permitted source address.
                 * @return Error::Ok on success, Error::Invalid if either
                 *         address is malformed.
                 */
                Error addSourceGroup(const SocketAddress &group, const SocketAddress &source);

                /** @brief Returns the list of configured group memberships. */
                const GroupList &groups() const { return _groups; }

                /**
                 * @brief Opens the socket, joins groups, and starts the receive thread.
                 *
                 * Must be called on a configured but not-yet-running
                 * receiver.  On failure, any partial setup is rolled
                 * back and the receiver returns to the stopped state.
                 *
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error start();

                /**
                 * @brief Stops the receive thread, leaves groups, and closes the socket.
                 *
                 * Safe to call from any thread, including the receive
                 * thread itself.  Also called automatically from the
                 * destructor.  A stopped receiver can be reconfigured
                 * and restarted.
                 */
                void stop();

                /** @brief Returns true if the receive thread is running. */
                bool isActive() const { return _active.value(); }

                /**
                 * @brief Returns the socket, or nullptr if not running.
                 *
                 * Exposed for callers that need to set advanced socket
                 * options (DSCP marking, receive buffer size, etc.)
                 * between @ref start and the first datagram.  Do not
                 * use it to receive directly — the receive thread
                 * owns the socket.
                 *
                 * @return The @ref UdpSocket, or nullptr.
                 */
                UdpSocket *socket() const { return _socket.get(); }

                /**
                 * @brief Returns the total number of datagrams delivered.
                 *
                 * Atomic counter incremented by the receive thread
                 * after each successful datagram.  Reset to zero on
                 * each @ref start call.
                 *
                 * @return Received datagram count.
                 */
                uint64_t datagramCount() const { return _datagramCount.value(); }

                /**
                 * @brief Returns the total number of bytes delivered.
                 * @return Received byte count.
                 */
                uint64_t byteCount() const { return _byteCount.value(); }

                /**
                 * @brief Emitted from the receive thread for each datagram.
                 *
                 * The signal runs in addition to the callback for
                 * consumers that prefer the signal/slot wiring.
                 * Note the cross-thread dispatch overhead — for hot
                 * paths prefer @ref setDatagramCallback.
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(datagramReceived, Buffer::Ptr, SocketAddress);

                /** @brief Emitted when a non-timeout receive error occurs. @signal */
                PROMEKI_SIGNAL(receiveError, Error);

        protected:
                /** @brief Thread entry — runs the receive loop. */
                void run() override;

        private:
                Error openAndJoin();
                void  closeAndLeave();

                UdpSocket::UPtr  _socket;
                MulticastManager _multicastManager;
                SocketAddress    _localAddress;
                String           _interfaceName;
                String           _threadName;
                size_t           _maxPacketSize = DefaultMaxPacketSize;
                unsigned int     _receiveTimeoutMs = DefaultReceiveTimeoutMs;
                DatagramCallback _callback;
                GroupList        _groups;

                Atomic<bool>     _active;
                Atomic<bool>     _stopRequested;
                Atomic<uint64_t> _datagramCount;
                Atomic<uint64_t> _byteCount;
};

PROMEKI_NAMESPACE_END
