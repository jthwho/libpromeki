/**
 * @file      network/tcpsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/network/abstractsocket.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Stream-oriented TCP socket.
 * @ingroup network
 *
 * TcpSocket provides reliable, ordered, connection-oriented
 * communication over TCP. Uses the IODevice read()/write()
 * interface for data transfer after a connection is established.
 *
 * This class must only be used from the thread that created it
 * (or moved to via moveToThread()).
 *
 * @par Example
 * @code
 * TcpSocket sock;
 * sock.open(IODevice::ReadWrite);
 * sock.connectToHost(SocketAddress(Ipv4Address(127, 0, 0, 1), 8080));
 * sock.write("GET / HTTP/1.0\r\n\r\n", 18);
 * char buf[4096];
 * int64_t n = sock.read(buf, sizeof(buf));
 * @endcode
 */
class TcpSocket : public AbstractSocket {
        PROMEKI_OBJECT(TcpSocket, AbstractSocket)
        public:
                /**
                 * @brief Constructs a TcpSocket.
                 * @param parent The parent object, or nullptr.
                 */
                TcpSocket(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~TcpSocket() override;

                /**
                 * @brief Opens the socket.
                 *
                 * Creates an AF_INET SOCK_STREAM socket. Use openIpv6()
                 * if IPv6 is needed.
                 *
                 * @param mode The open mode (typically ReadWrite).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(OpenMode mode) override;

                /**
                 * @brief Opens the socket for IPv6 operation.
                 *
                 * Creates an AF_INET6 SOCK_STREAM socket. By default,
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
                 * @brief Reads data from the connected socket.
                 * @param data Buffer to read into.
                 * @param maxSize Maximum bytes to read.
                 * @return Bytes read, 0 on peer disconnect, or -1 on error.
                 */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Writes data to the connected socket.
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
                 * @brief Enables or disables TCP_NODELAY (Nagle algorithm).
                 * @param enable True to disable Nagle (low latency), false to enable.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setNoDelay(bool enable);

                /**
                 * @brief Enables or disables SO_KEEPALIVE.
                 * @param enable True to enable keepalive probes.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setKeepAlive(bool enable);
};

PROMEKI_NAMESPACE_END
