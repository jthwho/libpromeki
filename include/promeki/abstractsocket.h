/**
 * @file      abstractsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/iodevice.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base class for network sockets.
 * @ingroup network
 *
 * AbstractSocket derives from IODevice and provides the common
 * interface for TCP, UDP, and raw sockets. It manages socket state,
 * local and peer addresses, and raw socket descriptor lifecycle.
 *
 * Subclasses implement open(), close(), read(), and write() for
 * their specific protocol. The socket descriptor is created by
 * open() and closed by close().
 *
 * @par Thread Safety
 * Inherits @ref IODevice &mdash; thread-affine.  A single AbstractSocket
 * instance must only be used from the thread that created it (or
 * moved to via @c moveToThread()).
 *
 * @par Example
 * @code
 * UdpSocket sock;
 * Error err = sock.open(IODevice::ReadWrite);
 * err = sock.bind(SocketAddress::any(5004));
 * @endcode
 */
class AbstractSocket : public IODevice {
                PROMEKI_OBJECT(AbstractSocket, IODevice)
        public:
                /** @brief Default receive timeout in milliseconds applied to
                 *  newly created sockets.  Prevents blocking reads from
                 *  waiting indefinitely. */
                static constexpr unsigned int DefaultReceiveTimeoutMs = 5000;

                /** @brief Default send timeout in milliseconds applied to
                 *  newly created sockets.  Prevents blocking writes from
                 *  waiting indefinitely. */
                static constexpr unsigned int DefaultSendTimeoutMs = 5000;

                /** @brief The type of socket. */
                enum SocketType {
                        TcpSocketType, ///< Stream-oriented TCP socket.
                        UdpSocketType, ///< Datagram-oriented UDP socket.
                        RawSocketType  ///< Raw Ethernet frame socket.
                };

                /** @brief The current state of the socket. */
                enum SocketState {
                        Unconnected, ///< Socket is not connected or bound.
                        Connecting,  ///< A non-blocking connect is in progress.
                        Connected,   ///< Socket is connected to a peer.
                        Bound,       ///< Socket is bound to a local address.
                        Closing,     ///< Socket is being closed.
                        Listening    ///< Socket is listening for incoming connections (TCP server).
                };

                /**
                 * @brief Constructs an AbstractSocket.
                 * @param type The socket type.
                 * @param parent The parent object, or nullptr.
                 */
                AbstractSocket(SocketType type, ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes the socket if open. */
                virtual ~AbstractSocket();

                /** @brief Returns the socket type. */
                SocketType socketType() const { return _type; }

                /** @brief Returns the current socket state. */
                SocketState state() const { return _state; }

                /**
                 * @brief Binds the socket to a local address.
                 *
                 * The socket must be open before calling bind().
                 *
                 * @param address The local address and port to bind to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error bind(const SocketAddress &address);

                /**
                 * @brief Initiates a connection to a remote host.
                 *
                 * For TCP sockets this performs the three-way handshake.
                 * For UDP sockets this sets the default destination for
                 * write() calls.
                 *
                 * @param address The remote address and port.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error connectToHost(const SocketAddress &address);

                /**
                 * @brief Disconnects from the remote host.
                 *
                 * For TCP sockets this performs a graceful close. For UDP
                 * sockets this removes the default destination.
                 */
                void disconnectFromHost();

                /** @brief Returns the local address the socket is bound to. */
                SocketAddress localAddress() const { return _localAddress; }

                /** @brief Returns the address of the connected peer. */
                SocketAddress peerAddress() const { return _peerAddress; }

                /**
                 * @brief Blocks until the socket is connected or timeout.
                 * @param timeoutMs Timeout in milliseconds (0 = wait forever).
                 * @return Error::Ok if connected, Error::Timeout on timeout.
                 */
                Error waitForConnected(unsigned int timeoutMs = 0);

                /**
                 * @brief Returns the raw socket file descriptor.
                 * @return The file descriptor, or -1 if no socket is open.
                 */
                int socketDescriptor() const { return _fd; }

                /**
                 * @brief Adopts an existing file descriptor as the socket.
                 *
                 * The socket takes ownership of the descriptor. The caller
                 * must not close it after this call.
                 *
                 * @param fd The file descriptor to adopt.
                 */
                void setSocketDescriptor(int fd);

                /**
                 * @brief Sets the receive timeout for blocking read operations.
                 *
                 * When a receive timeout is set, blocking calls such as read()
                 * and readDatagram() will return with an error after the
                 * specified duration if no data has arrived.
                 *
                 * @param timeoutMs Timeout in milliseconds.  A value of zero
                 *        removes the timeout (waits indefinitely).
                 * @return Error::Ok on success, or an error on failure.
                 *
                 * @par Example
                 * @code
                 * UdpSocket sock;
                 * sock.open(IODevice::ReadWrite);
                 * sock.setReceiveTimeout(1000);    // reads time out after 1 s
                 * @endcode
                 */
                Error setReceiveTimeout(unsigned int timeoutMs);

                /**
                 * @brief Sets the send timeout for blocking write operations.
                 *
                 * When a send timeout is set, blocking calls such as write()
                 * and writeDatagram() will return with an error after the
                 * specified duration if the data cannot be sent.
                 *
                 * @param timeoutMs Timeout in milliseconds.  A value of zero
                 *        removes the timeout (waits indefinitely).
                 * @return Error::Ok on success, or an error on failure.
                 *
                 * @par Example
                 * @code
                 * TcpSocket sock;
                 * sock.open(IODevice::ReadWrite);
                 * sock.setSendTimeout(5000);    // writes time out after 5 s
                 * @endcode
                 */
                Error setSendTimeout(unsigned int timeoutMs);

                /**
                 * @brief Sets a raw socket option via setsockopt().
                 * @param level The protocol level (e.g. SOL_SOCKET, IPPROTO_IP).
                 * @param option The option name.
                 * @param value The option value.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setSocketOption(int level, int option, int value);

                /**
                 * @brief Gets a raw socket option via getsockopt().
                 * @param level The protocol level.
                 * @param option The option name.
                 * @return A Result containing the option value, or an error.
                 */
                Result<int> socketOption(int level, int option) const;

                /**
                 * @brief Returns true — sockets are sequential (non-seekable).
                 * @return Always true.
                 */
                bool isSequential() const override { return true; }

                /** @brief Emitted when a connection is established. @signal */
                PROMEKI_SIGNAL(connected);

                /** @brief Emitted when the socket is disconnected. @signal */
                PROMEKI_SIGNAL(disconnected);

                /** @brief Emitted when the socket state changes. @signal */
                PROMEKI_SIGNAL(stateChanged, SocketState);

                /**
                 * @brief Sets the socket to non-blocking mode.
                 *
                 * Required for sockets driven by an
                 * @ref EventLoop::addIoSource — blocking reads/writes
                 * would stall the loop.
                 *
                 * @param enable True for non-blocking, false for blocking.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setNonBlocking(bool enable);

        protected:
                /**
                 * @brief Creates the underlying socket descriptor.
                 * @param domain The address family (AF_INET, AF_INET6, AF_PACKET).
                 * @param type The socket type (SOCK_STREAM, SOCK_DGRAM, SOCK_RAW).
                 * @param protocol The protocol (0 for default).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error createSocket(int domain, int type, int protocol = 0);

                /**
                 * @brief Closes the socket descriptor.
                 *
                 * Subclasses should call this from their close() override.
                 */
                void closeSocket();

                /**
                 * @brief Updates the local address from the socket descriptor.
                 *
                 * Calls getsockname() and stores the result. Called after
                 * bind() to capture the assigned port when binding to port 0.
                 */
                void updateLocalAddress();

                /**
                 * @brief Sets the socket state and emits stateChanged.
                 * @param state The new state.
                 */
                void setState(SocketState state);

                int           _fd = -1;             ///< Socket file descriptor.
                SocketState   _state = Unconnected; ///< Current socket state.
                SocketType    _type;                ///< Socket type.
                SocketAddress _localAddress;        ///< Local bound address.
                SocketAddress _peerAddress;         ///< Connected peer address.
};

PROMEKI_NAMESPACE_END
