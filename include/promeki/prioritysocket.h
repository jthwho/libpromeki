/**
 * @file      prioritysocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief UDP socket with QoS priority convenience methods.
 * @ingroup network
 *
 * PrioritySocket extends UdpSocket with a Priority enum that maps
 * to standard DSCP values used in AV-over-IP traffic. This provides
 * a convenient, protocol-aware interface on top of the raw DSCP
 * support in UdpSocket.
 *
 * @par DSCP Mapping
 * | Priority       | DSCP Value | DSCP Name | Use Case              |
 * |----------------|------------|-----------|----------------------|
 * | BestEffort     | 0          | CS0       | Default traffic      |
 * | Background     | 8          | CS1       | Low-priority traffic |
 * | Video          | 34         | AF41      | Broadcast video      |
 * | Voice          | 46         | EF        | Real-time audio      |
 * | NetworkControl | 48         | CS6       | PTP traffic          |
 *
 * @par Example
 * @code
 * PrioritySocket sock;
 * sock.open(IODevice::ReadWrite);
 * sock.setPriority(PrioritySocket::Video);
 * sock.writeDatagram(data, size, dest);
 * @endcode
 *
 * @note Setting DSCP values may require elevated permissions
 *       (CAP_NET_ADMIN on Linux) depending on the platform.
 *
 * @par Thread Safety
 * Inherits @ref UdpSocket: thread-affine.  A single PrioritySocket
 * must only be used from the thread that created it.
 */
class PrioritySocket : public UdpSocket {
                PROMEKI_OBJECT(PrioritySocket, UdpSocket)
        public:
                /** @brief QoS priority levels for network traffic. */
                enum Priority {
                        BestEffort = 0,     ///< Default, no priority (DSCP 0, CS0).
                        Background = 8,     ///< Low-priority background traffic (DSCP 8, CS1).
                        Video = 34,         ///< Broadcast video (DSCP 34, AF41).
                        Voice = 46,         ///< Real-time audio (DSCP 46, EF).
                        NetworkControl = 48 ///< Network control / PTP (DSCP 48, CS6).
                };

                /**
                 * @brief Constructs a PrioritySocket.
                 * @param parent The parent object, or nullptr.
                 */
                PrioritySocket(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~PrioritySocket() override;

                /**
                 * @brief Sets the QoS priority level.
                 *
                 * Maps the Priority enum value to the corresponding DSCP
                 * value and applies it to the socket via setDscp().
                 *
                 * @param p The priority level.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setPriority(Priority p);

                /**
                 * @brief Returns the current priority level.
                 *
                 * Reverse-maps the current DSCP value to the Priority enum.
                 * If the DSCP value does not match any Priority enum value,
                 * returns BestEffort.
                 *
                 * @return The current priority level.
                 */
                Priority priority() const { return _priority; }

        private:
                Priority _priority = BestEffort;
};

PROMEKI_NAMESPACE_END
