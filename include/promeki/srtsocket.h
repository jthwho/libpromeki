/**
 * @file      srtsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/iodevice.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Connection-oriented SRT socket (Secure Reliable Transport).
 * @ingroup network
 *
 * SrtSocket provides reliable, low-latency packet delivery over an
 * unreliable datagram path (typically UDP across the public internet).
 * It is built on the vendored libsrt 1.5.5 stack — every send and
 * receive on this class is a thin wrapper around @c srt_send /
 * @c srt_recv on an opaque @c SRTSOCKET handle.  The crypto stack is
 * a private mbedTLS-3.6 build that lives entirely inside the SRT
 * bundle (see @c cmake/promeki_srt_bundle.cmake) — none of its
 * symbols leak into the rest of @c libpromeki.so.
 *
 * @par Connection model
 *
 * SRT borrows TCP's caller / listener split.  This class represents
 * one *established* connection; connections are created either by
 * @ref connectToHost on a fresh @ref SrtSocket (caller mode) or by
 * @ref SrtServer::accept on a listening @ref SrtServer (listener
 * mode).  Rendezvous mode is not exposed in this phase.
 *
 * @par Live vs. file mode
 *
 * SRT supports two transport types: @b live (default — message-mode
 * sends, fixed payload size, late packets are dropped after the
 * configured @ref setLatency), and @b file (TCP-like reliable byte
 * stream, no drops, message boundaries hidden).  The default here is
 * live, matching the dominant use case (low-latency video).  Switch
 * via @ref setTransportType.
 *
 * @par Encryption
 *
 * Set a passphrase via @ref setPassphrase before connecting / before
 * @ref SrtServer::listen and SRT will negotiate AES on the handshake
 * keying material.  The key length is configured via
 * @ref setEncryptionKeyLength (0 = auto-pick, 16 / 24 / 32 = AES-128
 * / 192 / 256).  An unset passphrase means no encryption.
 *
 * @par Thread safety
 *
 * Inherits @ref IODevice — thread-affine.  A single instance must
 * only be used from the thread that created it (or moved to via
 * @c moveToThread()).  Different SrtSocket instances may be used
 * concurrently from different threads.
 *
 * @par Example (caller)
 * @code
 * SrtSocket sock;
 * sock.setLatency(120);   // 120 ms receive buffer
 * sock.setStreamId("publish/cam1");
 * Error err = sock.open(IODevice::ReadWrite);
 * err = sock.connectToHost(SocketAddress(Ipv4Address(192, 168, 1, 10), 4200));
 * sock.write(payload, payloadSize);
 * @endcode
 */
class SrtSocket : public IODevice {
                PROMEKI_OBJECT(SrtSocket, IODevice)
        public:
                /** @brief Unique-ownership pointer to a SrtSocket. */
                using UPtr = UniquePtr<SrtSocket>;

                /** @brief Sentinel for an uninitialized SRTSOCKET handle. */
                static constexpr int InvalidHandle = -1;

                /**
                 * @brief Snapshot of the SRT performance counters.
                 *
                 * Wraps the production-relevant subset of libsrt's
                 * @c SRT_TRACEBSTATS struct.  All fields are
                 * cumulative since the connection was established
                 * unless prefixed @c link / @c send / @c recvBuffer.
                 * Filled by @ref SrtSocket::stats and the matching
                 * accessor on @ref SrtSocketTransport.
                 */
                struct Stats {
                                /** @brief Milliseconds since the connection started. */
                                int64_t  msSinceStart = 0;
                                /** @brief Total data packets sent (including retransmissions). */
                                int64_t  pktSent = 0;
                                /** @brief Total data packets received. */
                                int64_t  pktRecv = 0;
                                /** @brief Total payload bytes sent (including retransmissions). */
                                uint64_t byteSent = 0;
                                /** @brief Total payload bytes received. */
                                uint64_t byteRecv = 0;
                                /** @brief Sender-side packet losses observed by NAKs. */
                                int      pktSndLost = 0;
                                /** @brief Receiver-side packet losses observed at TSBPD. */
                                int      pktRcvLost = 0;
                                /** @brief Packets retransmitted. */
                                int      pktRetransmitted = 0;
                                /** @brief Packets dropped on the sender (too-late-to-send). */
                                int      pktSndDrop = 0;
                                /** @brief Packets dropped on the receiver (too-late-to-play). */
                                int      pktRcvDrop = 0;
                                /** @brief Packets received that failed AES decryption. */
                                int      pktRcvUndecrypt = 0;
                                /** @brief Current round-trip time, in milliseconds. */
                                double   rttMs = 0.0;
                                /** @brief Estimated end-to-end link bandwidth, in Mb/s. */
                                double   linkBandwidthMbps = 0.0;
                                /** @brief Configured @c SRTO_MAXBW ceiling, in Mb/s. */
                                double   maxBandwidthMbps = 0.0;
                                /** @brief Currently un-ACK'd bytes in the sender buffer. */
                                int      sendBufferBytes = 0;
                                /** @brief Time-span of un-ACK'd packets in the sender buffer (ms). */
                                int      sendBufferMs = 0;
                                /** @brief Bytes currently buffered in the receiver. */
                                int      recvBufferBytes = 0;
                                /** @brief Time-span of pending packets in the receiver buffer (ms). */
                                int      recvBufferMs = 0;
                };

                /** @brief SRT transport type. */
                enum TransportType {
                        Live, ///< Message-oriented live streaming (default).
                        File  ///< Reliable byte-stream file transfer.
                };

                /** @brief Connection state of an SRT socket. */
                enum SocketState {
                        Init,       ///< Created but not bound or connected.
                        Opened,     ///< Bound; not yet connected (caller pre-connect).
                        Listening,  ///< Listening for incoming connections.
                        Connecting, ///< Caller-side handshake in progress.
                        Connected,  ///< Handshake complete; data flow possible.
                        Broken,     ///< Connection lost (peer gone, timeout).
                        Closing,    ///< Local side requested shutdown.
                        Closed,     ///< Socket is closed.
                        NonExist    ///< SRT handle was never valid / already cleaned.
                };

                /**
                 * @brief Constructs an SrtSocket without opening it.
                 * @param parent Optional parent ObjectBase.
                 */
                SrtSocket(ObjectBase *parent = nullptr);

                /**
                 * @brief Adopts an existing SRTSOCKET handle.
                 *
                 * Used by @ref SrtServer::accept to wrap the socket that
                 * @c srt_accept returns.  The new SrtSocket takes
                 * ownership of @p handle and will call @c srt_close on
                 * destruction.
                 *
                 * @param handle An open @c SRTSOCKET, typically from
                 *               @c srt_accept.
                 * @param parent Optional parent ObjectBase.
                 */
                SrtSocket(int handle, ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes the socket if open. */
                ~SrtSocket() override;

                /**
                 * @brief Opens (creates) an SRT socket handle.
                 *
                 * Creates a fresh @c SRTSOCKET via @c srt_create_socket.
                 * The socket is not yet bound or connected — use
                 * @ref bind and @ref connectToHost (caller mode), or
                 * pass it to an @ref SrtServer for listener mode.
                 *
                 * @param mode The open mode (typically @c ReadWrite).
                 * @return @ref Error::Ok on success, or
                 *         @ref Error::LibraryFailure if SRT refused.
                 */
                Error open(OpenMode mode) override;

                /**
                 * @brief Closes the SRT socket.
                 * @return @ref Error::Ok on success.
                 */
                Error close() override;

                /** @brief Returns true if the socket is open. */
                bool isOpen() const override;

                /**
                 * @brief Reads up to @p maxSize bytes from the connection.
                 *
                 * In @ref Live mode this returns one full SRT message.
                 * In @ref File mode it returns whatever bytes are
                 * currently available, like TCP @c recv.
                 *
                 * @param data    Destination buffer.
                 * @param maxSize Buffer capacity in bytes.
                 * @return Bytes read, 0 on graceful peer shutdown, or -1
                 *         on error (use @ref lastSrtError for detail).
                 */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Sends @p maxSize bytes to the peer.
                 *
                 * In @ref Live mode the buffer is treated as a single
                 * message and must not exceed the configured payload
                 * size (@ref setPayloadSize, default 1316).
                 *
                 * @param data    Source buffer.
                 * @param maxSize Number of bytes to send.
                 * @return Bytes accepted, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /** @brief Returns true — SRT is sequential (non-seekable). */
                bool isSequential() const override { return true; }

                /**
                 * @brief Binds the socket to a local address.
                 *
                 * Optional for callers; required for listeners.  Must
                 * be called between @ref open and @ref connectToHost /
                 * @ref SrtServer::listen.
                 *
                 * @param address Local address and port (use port 0 for
                 *                an OS-assigned ephemeral port).
                 * @return @ref Error::Ok or a system error.
                 */
                Error bind(const SocketAddress &address);

                /**
                 * @brief Initiates an SRT caller-mode connection.
                 *
                 * Performs the SRT handshake synchronously by default;
                 * the call returns once the connection is @ref Connected
                 * or it has failed.  If @ref setNonBlocking is enabled,
                 * the call returns immediately with @ref Error::Ok and
                 * the caller must drive @ref waitForConnected manually.
                 *
                 * @param address Remote SRT listener address.
                 * @return @ref Error::Ok on success, or
                 *         @ref Error::ConnectionRefused / Timeout / etc.
                 */
                Error connectToHost(const SocketAddress &address);

                /**
                 * @brief Blocks until the SRT handshake finishes.
                 *
                 * Only useful when the socket is non-blocking.  In
                 * blocking mode @ref connectToHost itself waits, so
                 * this returns immediately.
                 *
                 * @param timeoutMs Maximum wait, in milliseconds (0 =
                 *                  wait forever, bounded by SRT's own
                 *                  @c SRTO_CONNTIMEO).
                 * @return @ref Error::Ok if @ref Connected,
                 *         @ref Error::Timeout otherwise.
                 */
                Error waitForConnected(unsigned int timeoutMs = 0);

                /** @brief Returns the local address (after @ref bind). */
                SocketAddress localAddress() const { return _localAddress; }

                /** @brief Returns the connected peer's address. */
                SocketAddress peerAddress() const { return _peerAddress; }

                /** @brief Returns the current connection state. */
                SocketState state() const;

                /** @brief Emitted after the handshake finishes. @signal */
                PROMEKI_SIGNAL(connected);

                /** @brief Emitted when the connection is torn down. @signal */
                PROMEKI_SIGNAL(disconnected);

                /**
                 * @brief Returns the SRT handle (an @c SRTSOCKET).
                 *
                 * Exposed so callers driving an @c srt_epoll loop or
                 * stats poller can hand the same descriptor to libsrt
                 * directly.  Don't use it for I/O — that belongs in
                 * @ref read / @ref write.  Returns @ref InvalidHandle
                 * when the socket is not open.
                 */
                int handle() const { return _sock; }

                /**
                 * @brief Returns the bonded-group handle for this socket.
                 *
                 * When the socket is part of an SRT bonded group
                 * (either because it was created from an
                 * @ref SrtGroup, or because @ref SrtServer::accept
                 * received it as part of an incoming bonded
                 * handshake), this returns the group's @c SRTSOCKET.
                 * Otherwise returns @ref InvalidHandle.  Wraps
                 * @c srt_groupof.
                 *
                 * Useful on listener-side code that needs to detect
                 * bonded clients without owning an explicit
                 * @ref SrtGroup — additional bonded members will
                 * arrive on the same group handle.
                 */
                int groupHandle() const;

                // ---- SRT-specific configuration knobs ----------------

                /**
                 * @brief Sets symmetric latency in milliseconds.
                 *
                 * Equivalent to setting both @c SRTO_RCVLATENCY (the
                 * receive-side reorder / loss-recovery buffer) and
                 * @c SRTO_PEERLATENCY (the floor advertised to the
                 * peer).  Higher latency tolerates more packet loss /
                 * jitter at the cost of end-to-end delay.  Must be set
                 * before @ref connectToHost / @ref SrtServer::listen.
                 *
                 * @param ms Latency budget in milliseconds (1..10000).
                 *           Library default is 120.
                 * @return @ref Error::Ok or @ref Error::Invalid.
                 */
                Error setLatency(int ms);

                /** @brief Returns the configured latency in milliseconds. */
                int latency() const { return _latencyMs; }

                /**
                 * @brief Sets the live-mode payload size in bytes.
                 *
                 * SRT in live mode treats every @ref write as one
                 * datagram-shaped message; this option caps the size.
                 * Default 1316 bytes (7 × 188 MPEG-TS packets, fits in
                 * one Ethernet MTU minus SRT/UDP/IP headers).
                 *
                 * @param bytes Payload size, 32..1456.  0 leaves the
                 *              SRT default in place.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setPayloadSize(int bytes);

                /** @brief Returns the configured payload size, or 0 for default. */
                int payloadSize() const { return _payloadSize; }

                /**
                 * @brief Sets the transport type (live or file).
                 * @param type Live (message-oriented, drop-late) or File.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setTransportType(TransportType type);

                /** @brief Returns the configured transport type. */
                TransportType transportType() const { return _transportType; }

                /**
                 * @brief Sets the AES passphrase for encryption.
                 *
                 * SRT uses the passphrase as keying material for an
                 * authenticated AES exchange during the handshake.
                 * Empty string disables encryption (the default).
                 * Length must be 10..79 bytes when non-empty.  Set
                 * before @ref connectToHost or @ref SrtServer::listen.
                 *
                 * @param passphrase 10–79 byte passphrase, or empty.
                 * @return @ref Error::Ok or @ref Error::Invalid.
                 */
                Error setPassphrase(const String &passphrase);

                /** @brief Returns true if a passphrase has been set. */
                bool hasPassphrase() const { return !_passphrase.isEmpty(); }

                /**
                 * @brief Sets the AES key length used by SRT encryption.
                 * @param bytes 0 = auto, 16 = AES-128, 24 = AES-192,
                 *              32 = AES-256.
                 * @return @ref Error::Ok or @ref Error::Invalid.
                 */
                Error setEncryptionKeyLength(int bytes);

                /** @brief Returns the configured AES key length. */
                int encryptionKeyLength() const { return _pbKeyLen; }

                /**
                 * @brief Sets the SRT stream identifier.
                 *
                 * On caller side, the stream ID is sent during the
                 * handshake.  On listener side, it is read off the
                 * accepted socket and inspected by the application
                 * (e.g. to route by stream name).  Up to 512 bytes.
                 *
                 * @param id Stream ID, or empty to clear.
                 * @return @ref Error::Ok or @ref Error::Invalid.
                 */
                Error setStreamId(const String &id);

                /** @brief Returns the stream ID seen on this socket. */
                String streamId() const;

                /**
                 * @brief Sets the maximum bandwidth, in bytes per second.
                 * @param bytesPerSec 0 = unlimited, -1 = relative-to-input.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setMaxBandwidth(int64_t bytesPerSec);

                /**
                 * @brief Sets a kernel receive buffer size hint.
                 * @param bytes Bytes (passed to SRTO_UDP_RCVBUF).  0 = default.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setReceiveBufferSize(int bytes);

                /**
                 * @brief Sets a kernel send buffer size hint.
                 * @param bytes Bytes (passed to SRTO_UDP_SNDBUF).  0 = default.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setSendBufferSize(int bytes);

                /**
                 * @brief Sets the receive timeout for blocking @ref read.
                 * @param timeoutMs 0 = wait forever, otherwise milliseconds.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setReceiveTimeout(int timeoutMs);

                /**
                 * @brief Sets the send timeout for blocking @ref write.
                 * @param timeoutMs 0 = wait forever, otherwise milliseconds.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setSendTimeout(int timeoutMs);

                /**
                 * @brief Sets the connect timeout (caller-mode handshake).
                 * @param timeoutMs Milliseconds.  Default 3000.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setConnectTimeout(int timeoutMs);

                /**
                 * @brief Enables rendezvous-mode handshakes.
                 *
                 * When set, the socket performs a peer-to-peer
                 * handshake on @ref connectToHost: both endpoints
                 * call @ref bind on a known local address and
                 * @ref connectToHost on the same prearranged peer.
                 * The exchange completes when each side proves it
                 * has reached the same UDP socket.  Must be set
                 * before @ref bind / @ref connectToHost.
                 *
                 * @param enable True to enable rendezvous mode.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setRendezvous(bool enable);

                /** @brief Returns true if rendezvous mode is enabled. */
                bool isRendezvous() const { return _rendezvous; }

                /**
                 * @brief Sets the socket to non-blocking mode.
                 *
                 * Required for callers driving SRT from inside an
                 * @ref EventLoop via @c srt_epoll.
                 *
                 * @param enable True for non-blocking, false for blocking.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setNonBlocking(bool enable);

                /**
                 * @brief Returns the human-readable text of the last
                 *        SRT error captured by this socket.
                 */
                String lastSrtError() const { return _lastError; }

                /**
                 * @brief Samples the current performance counters.
                 *
                 * Wraps @c srt_bstats internally.  The returned
                 * @ref Stats is a value-type snapshot — call repeatedly
                 * to track deltas.
                 *
                 * @param resetWindowed If true, the windowed (non-Total)
                 *        counters inside libsrt are zeroed after the
                 *        sample.  Use this when polling at a fixed
                 *        cadence and you want per-window numbers
                 *        rather than cumulative ones.
                 * @return A populated @ref Stats on success, or a
                 *         zeroed struct when the socket is not open
                 *         or libsrt rejected the call.
                 */
                Stats stats(bool resetWindowed = false) const;

        protected:
                /**
                 * @brief Refreshes @c _localAddress from @c srt_getsockname.
                 *
                 * Called by @ref bind (once a port is assigned) and by
                 * @ref SrtServer::accept (the accepted socket inherits
                 * the listener's local address).
                 */
                void updateLocalAddress();

                /**
                 * @brief Refreshes @c _peerAddress from @c srt_getpeername.
                 *
                 * Called after a successful caller-mode connect, and by
                 * @ref SrtServer::accept which has the peer address from
                 * @c srt_accept itself.
                 */
                void updatePeerAddress();

                /**
                 * @brief Captures the last error from libsrt for diagnostics.
                 *
                 * Stores @c srt_getlasterror_str() into @ref _lastError
                 * so subsequent @ref lastSrtError calls can report it.
                 * Called from any wrapper that gets a -1 return.
                 */
                void captureLastError();

        private:
                friend class SrtServer;

                int           _sock = InvalidHandle; ///< SRTSOCKET handle.
                SocketAddress _localAddress;
                SocketAddress _peerAddress;
                String        _passphrase;
                String        _streamId;
                String        _lastError;
                int           _latencyMs = 120;
                int           _payloadSize = 0;
                int           _pbKeyLen = 0;
                bool          _rendezvous = false;
                TransportType _transportType = Live;

                /**
                 * @brief Re-applies cached options to a freshly opened SRT handle.
                 *
                 * Called from @ref open and from the adopting constructor
                 * so that knobs the caller set before opening (latency,
                 * passphrase, …) actually reach the libsrt socket.
                 */
                Error applyPreConnectOptions();
};

PROMEKI_NAMESPACE_END
