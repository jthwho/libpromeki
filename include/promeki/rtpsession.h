/**
 * @file      rtpsession.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/atomic.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/socketaddress.h>
#include <promeki/rtppacket.h>
#include <promeki/packettransport.h>

PROMEKI_NAMESPACE_BEGIN

class Thread;

/**
 * @brief RTP session for sending and receiving packets (RFC 3550).
 * @ingroup network
 *
 * RtpSession manages the RTP protocol state for a single
 * synchronization source (SSRC).  It handles RTP header
 * construction, sequence number management, timestamp tracking,
 * and packet transmission via a @ref PacketTransport.
 *
 * The session operates on pre-built @ref RtpPacket lists from
 * @ref RtpPayload handlers: it fills in the RTP header fields
 * (version, payload type, sequence number, timestamp, SSRC, marker
 * bit) and hands the packets to the transport for delivery.
 *
 * @par Transport ownership
 *
 * RtpSession can either own its transport (when started via
 * @ref start(const SocketAddress&), which creates an internal
 * @ref UdpSocketTransport) or borrow a caller-owned transport
 * (when started via @ref start(PacketTransport*)).  Borrowing is
 * the right pattern when the caller needs to configure the
 * transport with options RtpSession doesn't expose, or when the
 * same transport is being shared across multiple RTP streams (not
 * currently supported, but forward-compatible with future DPDK
 * multiplexing).
 *
 * @par Destination
 *
 * The destination address is set once via @ref setRemote() before
 * sending any packets.  RTP streams have a single peer (unicast
 * address or multicast group), so per-send destination arguments
 * would only invite inconsistency.  To send to a multicast group,
 * pass the group address to @c setRemote() and configure the
 * transport (TTL, outgoing interface) before @c start().
 *
 * @par Example
 * @code
 * RtpSession session;
 * session.setPayloadType(96);
 * session.setClockRate(90000);
 * session.setRemote(SocketAddress(Ipv4Address(239, 0, 0, 1), 5004));
 * session.start(SocketAddress::any(0));
 *
 * RtpPayloadJpeg payload(1920, 1080);
 * auto packets = payload.pack(jpegData, jpegSize);
 * session.sendPackets(packets, timestamp, true);
 * @endcode
 */
class RtpSession : public ObjectBase {
                PROMEKI_OBJECT(RtpSession, ObjectBase)
        public:
                /**
                 * @brief Constructs an RtpSession.
                 * @param parent The parent object, or nullptr.
                 */
                RtpSession(ObjectBase *parent = nullptr);

                /** @brief Destructor. Stops the session if running. */
                ~RtpSession() override;

                /**
                 * @brief Starts the session with an internally-owned UDP transport.
                 *
                 * Creates a @ref UdpSocketTransport bound to the given
                 * local address, opens it, and takes ownership.  The
                 * transport is automatically closed and destroyed by
                 * @ref stop() or the destructor.
                 *
                 * @param localAddr The local address and port to bind to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error start(const SocketAddress &localAddr);

                /**
                 * @brief Starts the session with a caller-owned transport.
                 *
                 * The transport must be @ref PacketTransport::open "open"
                 * before this call.  RtpSession does not take ownership
                 * of the transport; the caller is responsible for
                 * closing and destroying it after @ref stop().
                 *
                 * @param transport The caller-owned, already-open transport.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error start(PacketTransport *transport);

                /** @brief Stops the session and closes any owned transport. */
                void stop();

                /** @brief Returns true if the session is started. */
                bool isRunning() const { return _running; }

                /**
                 * @brief Sets the destination address for all subsequent sends.
                 *
                 * Can be called before or after @ref start().  Unicast
                 * and multicast addresses are both accepted; multicast
                 * TTL and outgoing interface are configured on the
                 * transport, not here.
                 *
                 * @param dest The destination address and port.
                 */
                void setRemote(const SocketAddress &dest) { _remote = dest; }

                /** @brief Returns the current destination address. */
                const SocketAddress &remote() const { return _remote; }

                /**
                 * @brief Sends a single RTP packet with raw payload data.
                 *
                 * @param payload The payload data.
                 * @param timestamp The RTP timestamp for this packet.
                 * @param payloadType The RTP payload type.
                 * @param marker If true, the marker bit is set.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error sendPacket(const Buffer &payload, uint32_t timestamp, uint8_t payloadType, bool marker = false);

                /**
                 * @brief Sends pre-packed RTP packets from a payload handler.
                 *
                 * Fills in the RTP header fields on each packet and
                 * transmits them in one batch through the transport.
                 * The marker bit is set on the last packet in the
                 * list when @p markerOnLast is true (end of
                 * frame/access unit).
                 *
                 * @param packets The pre-packed packet list.  Modified
                 *                in-place: RTP header fields are
                 *                overwritten before transmission.
                 * @param timestamp The RTP timestamp for this frame.
                 * @param markerOnLast If true, set marker bit on the
                 *                     last packet.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error sendPackets(RtpPacket::List &packets, uint32_t timestamp, bool markerOnLast = true);

                /**
                 * @brief Sends a list of packets with per-packet timestamps.
                 *
                 * Each packet is stamped with @c startTimestamp @c +
                 * @c i @c * @c timestampStride so consecutive packets
                 * carry monotonically advancing RTP timestamps.  This
                 * is the AES67 / ST 2110-30 audio path, where every
                 * packet-time interval gets its own timestamp
                 * reflecting the exact sample index at the start of
                 * that packet.
                 *
                 * Sequence numbers still come from the session's
                 * monotonic counter and the marker bit is applied
                 * uniformly (audio streams should pass @p marker =
                 * @c false).  Packets are handed to the transport as
                 * a single batch via @ref PacketTransport::sendPackets().
                 *
                 * @param packets The pre-packed packet list, modified
                 *                in-place.
                 * @param startTimestamp The RTP timestamp for the
                 *                       first packet's first sample.
                 * @param timestampStride Increment applied per
                 *                        successive packet.
                 * @param marker Marker bit applied to every packet.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error sendPackets(RtpPacket::List &packets, uint32_t startTimestamp, uint32_t timestampStride,
                                  bool marker = false);

                /**
                 * @brief Sends pre-packed RTP packets with userspace pacing.
                 *
                 * Spreads packet transmission evenly across the given
                 * duration by sleeping between sends.  Implements
                 * ST 2110-21-style userspace sender pacing and is the
                 * fallback when kernel pacing is unavailable.  For
                 * kernel pacing, prefer @ref setPacingRate().
                 *
                 * @param packets The pre-packed packet list.  Modified
                 *                in-place.
                 * @param timestamp The RTP timestamp for this frame.
                 * @param spreadInterval The total duration over which
                 *        to spread packet transmission.
                 * @param markerOnLast If true, set marker bit on the
                 *                     last packet.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error sendPacketsPaced(RtpPacket::List &packets, uint32_t timestamp, const Duration &spreadInterval,
                                       bool markerOnLast = true);

                /**
                 * @brief Signature of the per-packet receive callback.
                 *
                 * Invoked from the receive thread for every RTP
                 * packet arriving on the transport.  The @c packet
                 * view references a Buffer owned by the session; its
                 * backing memory is valid only for the duration of
                 * the call.  Consumers that need to retain the bytes
                 * should copy them or take a fresh @ref Buffer::Ptr
                 * copy.
                 *
                 * @param packet The parsed RTP packet.
                 * @param sender The datagram sender's address.
                 */
                using PacketCallback = std::function<void(const RtpPacket &packet, const SocketAddress &sender)>;

                /**
                 * @brief Starts the per-session RTP receive loop.
                 *
                 * Spawns an internal @ref Thread named
                 * @c "rtp-rx" that calls
                 * @ref PacketTransport::receivePacket in a loop,
                 * wraps each datagram in an @ref RtpPacket view of
                 * an owned @ref Buffer, and delivers the packet to
                 * @p callback.  The existing @c packetReceived
                 * signal is also emitted for consumers that prefer
                 * event-loop dispatch.
                 *
                 * The session must already be running
                 * (@ref isRunning() == true) — call one of the
                 * @ref start overloads first.
                 *
                 * The receive thread polls the stop flag every few
                 * hundred milliseconds using @c SO_RCVTIMEO on the
                 * underlying socket, so @ref stopReceiving() returns
                 * within one poll interval.  If the transport is not
                 * a UDP socket the poll timeout is a best-effort —
                 * transports that cannot expose a non-blocking
                 * receive may stall @ref stopReceiving until a
                 * packet arrives.
                 *
                 * @param callback The per-packet callback (required).
                 * @param threadName Thread name surfaced to
                 *        debuggers and OS monitors.  Defaults to
                 *        @c "rtp-rx".
                 * @return Error::Ok on success, Error::Busy if
                 *         already receiving, Error::NotOpen if the
                 *         session has not been started, or
                 *         Error::InvalidArgument on a null callback.
                 */
                Error startReceiving(PacketCallback callback, const String &threadName = "rtp-rx");

                /**
                 * @brief Stops the receive loop and joins the receive thread.
                 *
                 * Safe to call from any thread, including the
                 * receive thread itself (in which case the join is
                 * skipped).  Safe to call when no receive loop is
                 * running.
                 */
                void stopReceiving();

                /** @brief Returns true if the receive loop is running. */
                bool isReceiving() const { return _receiving.value(); }

                /**
                 * @brief Sets the receive-loop poll interval in milliseconds.
                 *
                 * Shorter intervals make @ref stopReceiving more
                 * responsive at the cost of extra idle wakeups.
                 * Default is 200 ms.  Must be set before
                 * @ref startReceiving.
                 *
                 * @param timeoutMs Poll interval in milliseconds.
                 */
                void setReceivePollIntervalMs(unsigned int timeoutMs) {
                        _receivePollMs = timeoutMs == 0 ? 200 : timeoutMs;
                }

                /** @brief Returns the receive-loop poll interval. */
                unsigned int receivePollIntervalMs() const { return _receivePollMs; }

                /**
                 * @brief Applies a transmit-rate cap to the transport.
                 *
                 * Convenience wrapper around
                 * @ref PacketTransport::setPacingRate() that works
                 * whether the transport is internally or externally
                 * owned.  For UDP sockets this maps to the kernel
                 * @c SO_MAX_PACING_RATE option, i.e. true
                 * kernel pacing with no per-packet CPU cost.
                 *
                 * @param bytesPerSec Maximum transmit rate in bytes/sec.
                 * @return Error::Ok on success, an error otherwise.
                 */
                Error setPacingRate(uint64_t bytesPerSec);

                /** @brief Returns the locally generated SSRC. */
                uint32_t ssrc() const { return _ssrc; }

                /** @brief Overrides the auto-generated SSRC. */
                void setSsrc(uint32_t ssrc) { _ssrc = ssrc; }

                /** @brief Returns the current sequence number. */
                uint16_t sequenceNumber() const { return _sequenceNumber; }

                /** @brief Sets the default payload type. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Returns the default payload type. */
                uint8_t payloadType() const { return _payloadType; }

                /** @brief Sets the RTP timestamp clock rate in Hz. */
                void setClockRate(uint32_t hz) { _clockRate = hz; }

                /** @brief Returns the RTP timestamp clock rate. */
                uint32_t clockRate() const { return _clockRate; }

                /**
                 * @brief Returns the active packet transport.
                 *
                 * This is the transport the session is currently
                 * using — either the one created internally by
                 * @ref start(const SocketAddress&), or the one passed
                 * to @ref start(PacketTransport*).
                 *
                 * @return Pointer to the transport, or nullptr if the
                 *         session is not running.
                 */
                PacketTransport *transport() const { return _transport; }

                /**
                 * @brief Emitted when a packet is received.
                 *
                 * Parameters: payload data (Buffer), RTP timestamp (uint32_t),
                 * payload type (uint8_t), marker bit (bool).
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(packetReceived, Buffer, uint32_t, uint8_t, bool);

                /** @brief Emitted when an SSRC collision is detected. @signal */
                PROMEKI_SIGNAL(ssrcCollision, uint32_t);

        private:
                class ReceiveThread;
                friend class ReceiveThread;

                void fillHeader(RtpPacket &pkt, uint8_t pt, bool marker, uint32_t timestamp);
                void generateSsrc();

                PacketTransport      *_transport = nullptr;
                PacketTransport::UPtr _ownedTransport;
                bool                  _running = false;
                SocketAddress         _remote;
                uint32_t              _ssrc = 0;
                uint16_t              _sequenceNumber = 0;
                uint8_t               _payloadType = 96;
                uint32_t              _clockRate = 90000;

                // Receive path
                using ReceiveThreadUPtr = UniquePtr<ReceiveThread>;
                ReceiveThreadUPtr _receiveThread;
                PacketCallback    _receiveCallback;
                Atomic<bool>      _receiving;
                unsigned int      _receivePollMs = 200;
};

PROMEKI_NAMESPACE_END
