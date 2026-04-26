/**
 * @file      rawsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/abstractsocket.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Raw Ethernet frame socket.
 * @ingroup network
 *
 * RawSocket provides send/receive access to raw Ethernet frames.
 * On Linux this uses AF_PACKET/SOCK_RAW. On macOS, BPF would be
 * used (not yet implemented).
 *
 * Opening a raw socket requires root privileges or the CAP_NET_RAW
 * capability. The open() method returns Error::PermissionDenied if
 * insufficient permissions are available.
 *
 * @par Thread Safety
 * Inherits @ref IODevice: thread-affine.  A single RawSocket
 * must only be used from the thread that created it.
 *
 * @par Example
 * @code
 * RawSocket sock;
 * sock.setInterface("eth0");
 * sock.setProtocol(0x0800);  // IPv4
 * Error err = sock.open(IODevice::ReadWrite);
 * @endcode
 */
class RawSocket : public AbstractSocket {
                PROMEKI_OBJECT(RawSocket, AbstractSocket)
        public:
                /**
                 * @brief Constructs a RawSocket.
                 * @param parent The parent object, or nullptr.
                 */
                RawSocket(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~RawSocket() override;

                /**
                 * @brief Sets the network interface to bind to.
                 *
                 * Must be called before open().
                 *
                 * @param interfaceName The interface name (e.g. "eth0").
                 */
                void setInterface(const String &interfaceName) { _interface = interfaceName; }

                /** @brief Returns the configured interface name. */
                const String &interface() const { return _interface; }

                /**
                 * @brief Sets the Ethernet protocol filter.
                 *
                 * Only frames matching this EtherType are received.
                 * Must be called before open(). Common values:
                 * - 0x0800 — IPv4
                 * - 0x0806 — ARP
                 * - 0x86DD — IPv6
                 * - ETH_P_ALL (0x0003) — all protocols
                 *
                 * @param ethertype The EtherType value.
                 */
                void setProtocol(uint16_t ethertype) { _protocol = ethertype; }

                /** @brief Returns the configured protocol filter. */
                uint16_t protocol() const { return _protocol; }

                /**
                 * @brief Opens the raw socket.
                 *
                 * Creates an AF_PACKET/SOCK_RAW socket on Linux. If an
                 * interface was set, the socket is bound to that interface.
                 *
                 * @param mode The open mode (typically ReadWrite).
                 * @return Error::Ok on success, Error::PermissionDenied if
                 *         insufficient privileges, or another error.
                 */
                Error open(OpenMode mode) override;

                /**
                 * @brief Closes the socket.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error close() override;

                /** @brief Returns true if the socket is open. */
                bool isOpen() const override { return _fd >= 0; }

                /**
                 * @brief Reads a raw Ethernet frame.
                 * @param data Buffer to read into.
                 * @param maxSize Maximum bytes to read.
                 * @return Bytes read, or -1 on error.
                 */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Writes a raw Ethernet frame.
                 * @param data The complete Ethernet frame to send.
                 * @param maxSize Frame size in bytes.
                 * @return Bytes sent, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /**
                 * @brief Enables or disables promiscuous mode.
                 * @param enable True to enable promiscuous mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setPromiscuous(bool enable);

        private:
                String   _interface;
                uint16_t _protocol = 0;
};

PROMEKI_NAMESPACE_END
