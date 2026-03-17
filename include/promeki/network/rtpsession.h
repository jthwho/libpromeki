/**
 * @file      network/rtpsession.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/objectbase.h>
#include <promeki/core/error.h>
#include <promeki/core/buffer.h>
#include <promeki/core/duration.h>
#include <promeki/network/socketaddress.h>
#include <promeki/network/rtppacket.h>

PROMEKI_NAMESPACE_BEGIN

class UdpSocket;

/**
 * @brief RTP session for sending and receiving packets (RFC 3550).
 * @ingroup network
 *
 * RtpSession manages the RTP protocol state for a single
 * synchronization source (SSRC). It handles RTP header construction,
 * sequence number management, and timestamp tracking.
 *
 * The session operates on pre-built RtpPacket lists from RtpPayload
 * handlers. It fills in the RTP header fields (version, payload type,
 * sequence number, timestamp, SSRC, marker bit) and transmits
 * via a UdpSocket.
 *
 * @par Example
 * @code
 * RtpSession session;
 * session.setPayloadType(96);
 * session.setClockRate(90000);
 * Error err = session.start(SocketAddress::any(0));
 *
 * // Send pre-packed RTP packets
 * RtpPayloadJpeg payload(1920, 1080);
 * auto packets = payload.pack(jpegData, jpegSize);
 * session.sendPackets(packets, timestamp, dest, true);
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
                 * @brief Starts the session, binding to a local address.
                 *
                 * Creates and opens the internal UDP socket, then binds
                 * it to the specified local address. Use port 0 to let
                 * the OS assign a port.
                 *
                 * @param localAddr The local address and port to bind to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error start(const SocketAddress &localAddr);

                /** @brief Stops the session and closes the socket. */
                void stop();

                /** @brief Returns true if the session is started. */
                bool isRunning() const { return _running; }

                /**
                 * @brief Sends a single RTP packet with raw payload data.
                 *
                 * Constructs a complete RTP packet with header and sends it.
                 *
                 * @param payload The payload data.
                 * @param timestamp The RTP timestamp for this packet.
                 * @param payloadType The RTP payload type.
                 * @param dest The destination address.
                 * @param marker If true, the marker bit is set.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error sendPacket(const Buffer &payload, uint32_t timestamp,
                                 uint8_t payloadType, const SocketAddress &dest,
                                 bool marker = false);

                /**
                 * @brief Sends pre-packed RTP packets from a payload handler.
                 *
                 * Fills in the RTP header fields on each packet and transmits.
                 * The marker bit is set on the last packet in the list (end of
                 * frame/access unit).
                 *
                 * @param packets The pre-packed packet list. Modified in-place:
                 *                RTP header fields are overwritten before transmission.
                 * @param timestamp The RTP timestamp for this frame.
                 * @param dest The destination address.
                 * @param markerOnLast If true, set marker bit on the last packet.
                 * @return Error::Ok on success, or an error on first failure.
                 */
                Error sendPackets(RtpPacket::List &packets, uint32_t timestamp,
                                  const SocketAddress &dest, bool markerOnLast = true);

                /**
                 * @brief Sends pre-packed RTP packets with even inter-packet pacing.
                 *
                 * Spreads packet transmission evenly across the given duration,
                 * sleeping between packets to maintain a steady send rate. This
                 * implements ST 2110-21 style sender pacing to avoid bursting
                 * all packets at once and overflowing receiver jitter buffers.
                 *
                 * @param packets The pre-packed packet list. Modified in-place.
                 * @param timestamp The RTP timestamp for this frame.
                 * @param dest The destination address.
                 * @param spreadInterval The total duration over which to spread
                 *        packet transmission. Packets are sent at evenly spaced
                 *        intervals within this window.
                 * @param markerOnLast If true, set marker bit on the last packet.
                 * @return Error::Ok on success, or an error on first failure.
                 */
                Error sendPacketsPaced(RtpPacket::List &packets, uint32_t timestamp,
                                       const SocketAddress &dest,
                                       const Duration &spreadInterval,
                                       bool markerOnLast = true);

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
                 * @brief Returns the internal UDP socket.
                 *
                 * Use this to configure socket options (DSCP, multicast, etc.)
                 * after calling start().
                 *
                 * @return Pointer to the socket, or nullptr if not started.
                 */
                UdpSocket *socket() const { return _socket; }

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
                void fillHeader(RtpPacket &pkt, uint8_t pt, bool marker,
                                uint32_t timestamp);
                void generateSsrc();

                UdpSocket      *_socket = nullptr;
                bool            _running = false;
                uint32_t        _ssrc = 0;
                uint16_t        _sequenceNumber = 0;
                uint8_t         _payloadType = 96;
                uint32_t        _clockRate = 90000;
};

PROMEKI_NAMESPACE_END
