/**
 * @file      localsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/iodevice.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Path-named local (same-host) stream socket.
 * @ingroup network
 *
 * LocalSocket is a stream-oriented, connection-based socket for
 * same-host inter-process communication.  On POSIX it is implemented
 * with @c AF_UNIX / @c SOCK_STREAM and identified by a filesystem
 * path.  On Windows it would be backed by a named pipe or
 * Windows-10-era @c AF_UNIX; only POSIX is implemented for the MVP.
 *
 * Instances derive from @ref IODevice, so once connected they are
 * usable with @ref DataStream, @ref BufferIODevice, and anything
 * else that takes an @c IODevice *.
 *
 * This class must only be used from the thread that created it (or
 * moved to via @ref moveToThread).
 *
 * @par Example
 * @code
 * LocalSocket sock;
 * Error err = sock.connectTo("/tmp/my-service.sock");
 * if(err.isOk()) {
 *     sock.write("ping", 4);
 *     char buf[16];
 *     int64_t n = sock.read(buf, sizeof(buf));
 * }
 * @endcode
 */
class LocalSocket : public IODevice {
        PROMEKI_OBJECT(LocalSocket, IODevice)
        public:
                /** @brief Unique-ownership pointer to a LocalSocket. */
                using UPtr = UniquePtr<LocalSocket>;

                /** @brief Default receive timeout applied to newly connected sockets. */
                static constexpr unsigned int DefaultReceiveTimeoutMs = 5000;

                /** @brief Default send timeout applied to newly connected sockets. */
                static constexpr unsigned int DefaultSendTimeoutMs = 5000;

                /** @brief Returns true if local sockets are supported on this platform. */
                static bool isSupported();

                /**
                 * @brief Constructs a LocalSocket.
                 * @param parent Optional parent @ref ObjectBase.
                 */
                LocalSocket(ObjectBase *parent = nullptr);

                /** @brief Destructor — closes the socket if open. */
                ~LocalSocket() override;

                /**
                 * @brief Creates an unconnected local socket.
                 *
                 * After @c open, the socket is ready to be connected via
                 * @ref connectTo or to be used by @ref LocalServer which
                 * will adopt an accepted descriptor into a fresh instance.
                 *
                 * @param mode Open mode (typically @c ReadWrite).
                 * @return @c Error::Ok on success, or an error.
                 */
                Error open(OpenMode mode) override;

                /** @brief Closes the socket. */
                Error close() override;

                /**
                 * @brief Connects to a listening local socket by path.
                 *
                 * Opens the socket first if it is not yet open.  After
                 * this call returns @c Error::Ok, @ref read and @ref write
                 * may be used.  Default receive and send timeouts are
                 * applied unless the caller has already set them.
                 *
                 * @param path     Filesystem path of the server socket.
                 * @param timeoutMs Optional connect timeout in ms (0 = default).
                 * @return @c Error::Ok on success, or an error.
                 */
                Error connectTo(const String &path, unsigned int timeoutMs = 0);

                /** @brief Returns true if the underlying descriptor is valid. */
                bool isOpen() const override { return _fd >= 0; }

                /** @brief Returns true if the socket is connected to a peer. */
                bool isConnected() const { return _fd >= 0 && _connected; }

                /**
                 * @brief Reads data from the connected socket.
                 * @param data    Destination buffer.
                 * @param maxSize Maximum bytes to read.
                 * @return Bytes read, 0 on peer disconnect, or -1 on error.
                 */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Writes data to the connected socket.
                 * @param data    Bytes to send.
                 * @param maxSize Number of bytes to send.
                 * @return Bytes actually sent, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /** @brief Returns the number of bytes available for reading. */
                int64_t bytesAvailable() const override;

                /** @brief Local sockets are sequential (non-seekable). */
                bool isSequential() const override { return true; }

                /** @brief Returns the raw file descriptor (-1 if not open). */
                int socketDescriptor() const { return _fd; }

                /**
                 * @brief Adopts an already-open file descriptor as the socket.
                 *
                 * Used by @ref LocalServer to hand an accepted descriptor
                 * to a fresh @ref LocalSocket.  The instance takes
                 * ownership; the caller must not close the descriptor.
                 *
                 * @param fd       The descriptor to adopt.
                 * @param peerPath Optional path of the peer that connected.
                 */
                void setSocketDescriptor(int fd, const String &peerPath = String());

                /** @brief Returns the path of the remote peer (empty if unknown). */
                const String &peerPath() const { return _peerPath; }

                /**
                 * @brief Sets a receive timeout for blocking reads.
                 * @param timeoutMs Milliseconds; 0 removes the timeout.
                 */
                Error setReceiveTimeout(unsigned int timeoutMs);

                /**
                 * @brief Sets a send timeout for blocking writes.
                 * @param timeoutMs Milliseconds; 0 removes the timeout.
                 */
                Error setSendTimeout(unsigned int timeoutMs);

                /** @brief Emitted when @ref connectTo completes successfully. @signal */
                PROMEKI_SIGNAL(connected);

                /** @brief Emitted when the peer disconnects (EOF on read). @signal */
                PROMEKI_SIGNAL(disconnected);

        private:
                int     _fd        = -1;
                bool    _connected = false;
                String  _peerPath;
};

PROMEKI_NAMESPACE_END
