/**
 * @file      tcpserver.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>

PROMEKI_NAMESPACE_BEGIN

class TcpSocket;

/**
 * @brief TCP server that listens for incoming connections.
 * @ingroup network
 *
 * TcpServer binds to a local address and listens for incoming TCP
 * connections. Accepted connections are returned as TcpSocket objects.
 *
 * This class must only be used from the thread that created it
 * (or moved to via moveToThread()).
 *
 * @par Example
 * @code
 * TcpServer server;
 * server.listen(SocketAddress::any(8080));
 * server.waitForNewConnection(5000);
 * TcpSocket *client = server.nextPendingConnection();
 * @endcode
 */
class TcpServer : public ObjectBase {
        PROMEKI_OBJECT(TcpServer, ObjectBase)
        public:
                /**
                 * @brief Constructs a TcpServer.
                 * @param parent The parent object, or nullptr.
                 */
                TcpServer(ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes the server if listening. */
                ~TcpServer() override;

                /**
                 * @brief Starts listening for connections.
                 * @param address The local address and port to listen on.
                 * @param backlog The maximum pending connection queue length.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error listen(const SocketAddress &address, int backlog = 50);

                /** @brief Stops listening and closes the server socket. */
                void close();

                /** @brief Returns true if the server is listening. */
                bool isListening() const { return _listening; }

                /** @brief Returns the address the server is listening on. */
                SocketAddress serverAddress() const { return _address; }

                /**
                 * @brief Returns the next accepted connection.
                 *
                 * The caller takes ownership of the returned TcpSocket.
                 * Returns nullptr if no pending connections.
                 *
                 * @return A new TcpSocket for the accepted connection, or nullptr.
                 */
                TcpSocket *nextPendingConnection();

                /**
                 * @brief Drains the next pending connection as a raw fd.
                 *
                 * Equivalent to @ref nextPendingConnection except the
                 * caller takes the bare descriptor and is responsible
                 * for closing it.  Useful when the caller wants to
                 * wrap the descriptor in something other than a plain
                 * @ref TcpSocket — for example an @ref SslSocket
                 * that needs to drive a TLS handshake on top.
                 *
                 * @return The accepted fd, or -1 when no connection
                 *         is pending or accept() failed.
                 */
                int nextPendingDescriptor();

                /**
                 * @brief Returns true if there are pending connections.
                 * @return True if at least one connection is waiting.
                 */
                bool hasPendingConnections() const;

                /**
                 * @brief Blocks until a new connection arrives or timeout.
                 * @param timeoutMs Timeout in milliseconds (0 = wait forever).
                 * @return Error::Ok if a connection arrived, Error::Timeout on timeout.
                 */
                Error waitForNewConnection(unsigned int timeoutMs = 0);

                /**
                 * @brief Sets the maximum number of pending connections.
                 * @param count The maximum count.
                 */
                void setMaxPendingConnections(int count) { _maxPending = count; }

                /**
                 * @brief Returns the raw socket file descriptor, or -1.
                 *
                 * Exposed so callers driving accept from an
                 * @ref EventLoop can register the listening fd with
                 * @ref EventLoop::addIoSource and call
                 * @ref nextPendingConnection on read-readiness.
                 *
                 * @return The fd, or -1 when not listening.
                 */
                int socketDescriptor() const { return _fd; }

                /**
                 * @brief Sets the listening socket to (non-)blocking mode.
                 *
                 * Required for callers that drain the accept queue
                 * from inside an @ref EventLoop::addIoSource callback
                 * — otherwise @ref nextPendingConnection blocks
                 * forever once the queue is drained.
                 *
                 * @param enable True for non-blocking, false for blocking.
                 * @return @ref Error::Ok on success, or a system error.
                 */
                Error setNonBlocking(bool enable);

                /** @brief Emitted when a new connection is available. @signal */
                PROMEKI_SIGNAL(newConnection);

        private:
                int             _fd = -1;
                bool            _listening = false;
                SocketAddress   _address;
                int             _maxPending = 50;
};

PROMEKI_NAMESPACE_END
