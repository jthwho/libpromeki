/**
 * @file      multicastmanager.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/socketaddress.h>

PROMEKI_NAMESPACE_BEGIN

class UdpSocket;

/**
 * @brief Manages multicast group membership for multi-stream scenarios.
 * @ingroup network
 *
 * MulticastManager tracks which sockets have joined which multicast
 * groups, providing centralized management and cleanup. It supports
 * both Any-Source Multicast (ASM) and Source-Specific Multicast (SSM).
 *
 * @note All managed sockets must remain valid for the lifetime of
 * their memberships. MulticastManager does not take ownership
 * of socket pointers.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  A single
 * MulticastManager must only be used from the thread that
 * created it.
 *
 * @par Example
 * @code
 * MulticastManager mgr;
 * UdpSocket sock;
 * sock.open(IODevice::ReadWrite);
 * sock.setReuseAddress(true);
 * sock.bind(SocketAddress::any(5004));
 *
 * SocketAddress group(Ipv4Address(239, 0, 0, 1), 5004);
 * mgr.joinGroup(group, &sock);
 *
 * // Later...
 * mgr.leaveAllGroups();
 * @endcode
 */
class MulticastManager : public ObjectBase {
                PROMEKI_OBJECT(MulticastManager, ObjectBase)
        public:
                /**
                 * @brief Constructs a MulticastManager.
                 * @param parent The parent object, or nullptr.
                 */
                MulticastManager(ObjectBase *parent = nullptr);

                /** @brief Destructor. Leaves all managed groups. */
                ~MulticastManager() override;

                /**
                 * @brief Joins a multicast group on a socket.
                 * @param group The multicast group address.
                 * @param socket The UDP socket to join with.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error joinGroup(const SocketAddress &group, UdpSocket *socket);

                /**
                 * @brief Joins a multicast group on a specific interface.
                 * @param group The multicast group address.
                 * @param socket The UDP socket to join with.
                 * @param iface The network interface name (e.g. "eth0").
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error joinGroup(const SocketAddress &group, UdpSocket *socket, const String &iface);

                /**
                 * @brief Leaves a multicast group on a socket.
                 * @param group The multicast group address.
                 * @param socket The UDP socket to leave from.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error leaveGroup(const SocketAddress &group, UdpSocket *socket);

                /** @brief Leaves all managed multicast groups. */
                void leaveAllGroups();

                /** @brief Returns a list of all active multicast group addresses. */
                List<SocketAddress> activeGroups() const;

                /**
                 * @brief Returns true if any socket is a member of the group.
                 * @param group The multicast group to check.
                 * @return True if the group is active.
                 */
                bool isMemberOf(const SocketAddress &group) const;

                /**
                 * @brief Joins a Source-Specific Multicast group.
                 *
                 * Subscribes to multicast traffic from a specific source only
                 * (IP_ADD_SOURCE_MEMBERSHIP).
                 *
                 * @param group The multicast group address.
                 * @param source The source address to accept traffic from.
                 * @param socket The UDP socket.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error joinSourceGroup(const SocketAddress &group, const SocketAddress &source, UdpSocket *socket);

                /**
                 * @brief Leaves a Source-Specific Multicast group.
                 * @param group The multicast group address.
                 * @param source The source address.
                 * @param socket The UDP socket.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error leaveSourceGroup(const SocketAddress &group, const SocketAddress &source, UdpSocket *socket);

                /**
                 * @brief Sets the default network interface for joins.
                 *
                 * Used when joinGroup() is called without an explicit interface.
                 * Only affects future joins, not existing memberships.
                 *
                 * @param iface The interface name (e.g. "eth0").
                 */
                void setDefaultInterface(const String &iface) { _defaultInterface = iface; }

                /** @brief Returns the default interface name. */
                const String &defaultInterface() const { return _defaultInterface; }

                /** @brief Emitted when a multicast group is joined. @signal */
                PROMEKI_SIGNAL(groupJoined, SocketAddress);

                /** @brief Emitted when a multicast group is left. @signal */
                PROMEKI_SIGNAL(groupLeft, SocketAddress);

        private:
                struct Membership {
                                SocketAddress group;
                                UdpSocket    *socket;
                                SocketAddress source; // For SSM, null otherwise
                                bool          isSSM = false;
                };

                List<Membership> _memberships;
                String           _defaultInterface;
};

PROMEKI_NAMESPACE_END
