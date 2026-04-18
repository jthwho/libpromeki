/**
 * @file      localserver.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class LocalSocket;

/**
 * @brief Server that accepts local (same-host) stream connections.
 * @ingroup network
 *
 * LocalServer listens for incoming connections on a path-named
 * stream socket.  On POSIX it binds an @c AF_UNIX / @c SOCK_STREAM
 * socket at the requested filesystem path, listens, and returns
 * accepted connections as @ref LocalSocket instances.
 *
 * The server creates its socket file on @ref listen and unlinks it
 * on @ref close (or destruction).  If a stale socket file from a
 * prior crashed run exists at the requested path, @ref listen will
 * silently remove it before rebinding; this is safe because
 * filesystem sockets have no inode reuse issues of their own.
 *
 * This class must only be used from the thread that created it (or
 * moved to via @ref moveToThread).
 *
 * @par Example
 * @code
 * LocalServer server;
 * Error err = server.listen("/tmp/my-service.sock");
 * server.waitForNewConnection(5000);
 * LocalSocket *client = server.nextPendingConnection();
 * // caller owns the returned socket
 * @endcode
 */
class LocalServer : public ObjectBase {
        PROMEKI_OBJECT(LocalServer, ObjectBase)
        public:
                /** @brief Default backlog for @c listen(2). */
                static constexpr int DefaultBacklog = 50;

                /** @brief Default file mode applied to the socket file. */
                static constexpr uint32_t DefaultPermissions = 0600;

                /** @brief Returns true if local sockets are supported on this platform. */
                static bool isSupported();

                /**
                 * @brief Constructs a LocalServer.
                 * @param parent Optional parent @ref ObjectBase.
                 */
                LocalServer(ObjectBase *parent = nullptr);

                /** @brief Destructor — closes and unlinks the socket file. */
                ~LocalServer() override;

                /**
                 * @brief Binds and listens at the given filesystem path.
                 *
                 * Removes any stale socket file at @p path (if it is in
                 * fact a socket) before binding.  Applies @p mode via
                 * @c fchmod / umask workaround and, if @p groupName is
                 * non-empty, @c chown's the socket file to that group.
                 *
                 * @param path       Filesystem path for the socket.
                 * @param mode       POSIX file mode for the socket file.
                 * @param groupName  Optional group to @c chown to (empty = skip).
                 * @param backlog    Maximum pending-connection queue length.
                 * @return @c Error::Ok on success, or an error.
                 */
                Error listen(const String &path,
                             uint32_t mode = DefaultPermissions,
                             const String &groupName = String(),
                             int backlog = DefaultBacklog);

                /** @brief Stops listening and unlinks the socket file. */
                void close();

                /** @brief Returns true if the server is listening. */
                bool isListening() const { return _listening; }

                /** @brief Returns the filesystem path the server is bound to. */
                const String &serverPath() const { return _path; }

                /**
                 * @brief Returns the next accepted connection, or nullptr.
                 *
                 * The caller takes ownership of the returned
                 * @ref LocalSocket.  Returns nullptr if there is no
                 * pending connection right now.
                 *
                 * @return A new @ref LocalSocket, or nullptr.
                 */
                LocalSocket *nextPendingConnection();

                /** @brief Returns true if at least one connection is waiting. */
                bool hasPendingConnections() const;

                /**
                 * @brief Blocks until a new connection arrives or timeout.
                 * @param timeoutMs  Milliseconds (0 = wait forever).
                 * @return @c Error::Ok on arrival, @c Error::Timeout on timeout.
                 */
                Error waitForNewConnection(unsigned int timeoutMs = 0);

                /** @brief Returns the raw listening file descriptor. */
                int socketDescriptor() const { return _fd; }

                /** @brief Emitted when a new connection is available. @signal */
                PROMEKI_SIGNAL(newConnection);

        private:
                int     _fd            = -1;
                bool    _listening     = false;
                bool    _unlinkOnClose = false;
                String  _path;
};

PROMEKI_NAMESPACE_END
