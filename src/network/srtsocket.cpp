/**
 * @file      srtsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/srtsocket.h>
#include <promeki/logger.h>
#include <promeki/thread.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <chrono>

#include <srt/srt.h>
#include <srt/logging_api.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SrtSocket);

// ============================================================
// SRT runtime: a single, idempotent srt_startup() guard plus an
// atexit-registered srt_cleanup().  Modeled on the PSA-Crypto
// initializer in sslcontext.cpp.
//
// srt_startup() is internally reference-counted, but every paired
// shutdown means an extra epoll-thread teardown — cheaper to call
// it exactly once for the lifetime of the process and let
// libpromeki shut down naturally on exit.
//
// We also install an SRT log handler that routes libsrt's internal
// chatter through promeki::Logger.  Without this, libsrt writes
// directly to stderr (bypassing every libpromeki facility for
// silencing output), producing thousands of "srt_accept: no
// pending connection available" lines per second from non-blocking
// accept loops.  Routing through Logger makes the standard
// setConsoleLoggingEnabled(false) gate in doctest_main.cpp work.
// ============================================================
namespace {

        static std::atomic<bool> sSrtInitDone{false};

        // Map syslog-style SRT severity (LOG_EMERG=0 ... LOG_DEBUG=7) onto
        // promeki LogLevel.  LOG_NOTICE is informational by syslog convention
        // but libsrt uses it for things like "connection rejected"; treat it
        // as Info so it can still be silenced when the console is quiet.
        static Logger::LogLevel srtLevelToPromeki(int level) {
                if (level <= LOG_ERR) return Logger::LogLevel::Err;
                if (level == LOG_WARNING) return Logger::LogLevel::Warn;
                if (level <= LOG_INFO) return Logger::LogLevel::Info;
                return Logger::LogLevel::Debug;
        }

        extern "C" void srtPromekiLogHandler(void *opaque, int level, const char *file, int line,
                                             const char *area, const char *message) {
                (void)opaque;
                if (!message) return;
                String msg(message);
                while (!msg.isEmpty()) {
                        char c = msg[msg.size() - 1];
                        if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
                        msg.resize(msg.size() - 1);
                }
                if (msg.isEmpty()) return;
                String prefixed = String::sprintf("[SRT%s%s] %s", (area && *area) ? "." : "",
                                                  (area && *area) ? area : "", msg.cstr());
                Logger::defaultLogger().log(srtLevelToPromeki(level), file ? file : "srt",
                                            line, prefixed);
        }

        static Error ensureSrtRuntime() {
                if (sSrtInitDone.load(std::memory_order_acquire)) return Error::Ok;
                static std::atomic<bool> sStarting{false};
                bool                     expected = false;
                if (!sStarting.compare_exchange_strong(expected, true)) {
                        // Another thread is mid-init; spin briefly.
                        while (!sSrtInitDone.load(std::memory_order_acquire)) {
                                BasicThread::yield();
                        }
                        return Error::Ok;
                }
                if (srt_startup() < 0) {
                        promekiWarn("SrtSocket: srt_startup failed: %s", srt_getlasterror_str());
                        sStarting.store(false, std::memory_order_release);
                        return Error::LibraryFailure;
                }
                // Strip libsrt's own time/thread/severity decoration — Logger
                // adds its own consistent prefix.  Then forward every record.
                srt_setlogflags(SRT_LOGF_DISABLE_TIME | SRT_LOGF_DISABLE_THREADNAME
                                | SRT_LOGF_DISABLE_SEVERITY | SRT_LOGF_DISABLE_EOL);
                srt_setloghandler(nullptr, &srtPromekiLogHandler);
                std::atexit([]() { srt_cleanup(); });
                sSrtInitDone.store(true, std::memory_order_release);
                return Error::Ok;
        }

        // Translate libsrt's SRT_SOCKSTATUS to our SrtSocket::SocketState.
        // libsrt covers more states than the typical TCP-style enum so the
        // mapping is one-way; SrtSocket::state() is just a status query.
        static SrtSocket::SocketState translateSrtState(int s) {
                switch (s) {
                        case SRTS_INIT:       return SrtSocket::Init;
                        case SRTS_OPENED:     return SrtSocket::Opened;
                        case SRTS_LISTENING:  return SrtSocket::Listening;
                        case SRTS_CONNECTING: return SrtSocket::Connecting;
                        case SRTS_CONNECTED:  return SrtSocket::Connected;
                        case SRTS_BROKEN:     return SrtSocket::Broken;
                        case SRTS_CLOSING:    return SrtSocket::Closing;
                        case SRTS_CLOSED:     return SrtSocket::Closed;
                        case SRTS_NONEXIST:
                        default:              return SrtSocket::NonExist;
                }
        }

} // anonymous namespace

// ============================================================
// Construction / destruction
// ============================================================

SrtSocket::SrtSocket(ObjectBase *parent) : IODevice(parent) {}

SrtSocket::SrtSocket(int handle, ObjectBase *parent) : IODevice(parent), _sock(handle) {
        if (_sock != InvalidHandle) {
                setOpenMode(ReadWrite);
                updateLocalAddress();
                updatePeerAddress();
        }
}

SrtSocket::~SrtSocket() {
        if (isOpen()) close();
}

// ============================================================
// Open / close / I/O
// ============================================================

Error SrtSocket::open(OpenMode mode) {
        if (isOpen()) return Error::AlreadyOpen;
        Error e = ensureSrtRuntime();
        if (e.isError()) return e;
        _sock = srt_create_socket();
        if (_sock == SRT_INVALID_SOCK) {
                _sock = InvalidHandle;
                captureLastError();
                return Error::LibraryFailure;
        }
        setOpenMode(mode);
        Error oe = applyPreConnectOptions();
        if (oe.isError()) {
                close();
                return oe;
        }
        return Error::Ok;
}

Error SrtSocket::close() {
        if (_sock == InvalidHandle) return Error::Ok;
        srt_close(_sock);
        _sock = InvalidHandle;
        setOpenMode(NotOpen);
        return Error::Ok;
}

bool SrtSocket::isOpen() const {
        return _sock != InvalidHandle;
}

int64_t SrtSocket::read(void *data, int64_t maxSize) {
        if (!isOpen()) return -1;
        const int n = srt_recv(_sock, static_cast<char *>(data), static_cast<int>(maxSize));
        if (n == SRT_ERROR) {
                captureLastError();
                setError(Error::IOError);
                return -1;
        }
        if (n == 0) {
                // Peer closed cleanly.
                disconnectedSignal.emit();
        }
        return static_cast<int64_t>(n);
}

int64_t SrtSocket::write(const void *data, int64_t maxSize) {
        if (!isOpen()) return -1;
        const int n = srt_send(_sock, static_cast<const char *>(data), static_cast<int>(maxSize));
        if (n == SRT_ERROR) {
                captureLastError();
                setError(Error::IOError);
                return -1;
        }
        return static_cast<int64_t>(n);
}

// ============================================================
// Connection management
// ============================================================

Error SrtSocket::bind(const SocketAddress &address) {
        if (!isOpen()) return Error::NotOpen;
        struct sockaddr_storage storage;
        const size_t            len = address.toSockAddr(&storage);
        if (len == 0) return Error::Invalid;
        if (srt_bind(_sock, reinterpret_cast<struct sockaddr *>(&storage), static_cast<int>(len)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        updateLocalAddress();
        return Error::Ok;
}

Error SrtSocket::connectToHost(const SocketAddress &address) {
        if (!isOpen()) return Error::NotOpen;
        struct sockaddr_storage storage;
        const size_t            len = address.toSockAddr(&storage);
        if (len == 0) return Error::Invalid;
        const int rc = srt_connect(_sock, reinterpret_cast<struct sockaddr *>(&storage), static_cast<int>(len));
        if (rc == SRT_ERROR) {
                captureLastError();
                // Map a few well-known SRT error codes to our enum.  Anything
                // else falls back to a generic ConnectionFailed-equivalent.
                const int err = srt_getlasterror(nullptr);
                switch (err) {
                        case SRT_ECONNREJ: return Error::ConnectionRefused;
                        case SRT_ECONNLOST: return Error::ConnectionReset;
                        case SRT_ENOCONN:  return Error::IOError;
                        case SRT_ETIMEOUT: return Error::Timeout;
                        default:           return Error::LibraryFailure;
                }
        }
        _peerAddress = address;
        if (state() == Connected) {
                connectedSignal.emit();
        }
        return Error::Ok;
}

Error SrtSocket::waitForConnected(unsigned int timeoutMs) {
        if (!isOpen()) return Error::NotOpen;
        const auto deadline = (timeoutMs == 0)
                                      ? std::chrono::steady_clock::time_point::max()
                                      : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
                const SocketState s = state();
                if (s == Connected) {
                        connectedSignal.emit();
                        return Error::Ok;
                }
                if (s == Broken || s == Closed || s == NonExist) {
                        return Error::ConnectionReset;
                }
                if (std::chrono::steady_clock::now() >= deadline) return Error::Timeout;
                BasicThread::sleepMs(10);
        }
}

int SrtSocket::groupHandle() const {
        if (_sock == InvalidHandle) return InvalidHandle;
        const SRTSOCKET g = srt_groupof(_sock);
        if (g == SRT_INVALID_SOCK) return InvalidHandle;
        return g;
}

SrtSocket::SocketState SrtSocket::state() const {
        if (_sock == InvalidHandle) return NonExist;
        return translateSrtState(srt_getsockstate(_sock));
}

// ============================================================
// Address / error helpers
// ============================================================

void SrtSocket::updateLocalAddress() {
        if (_sock == InvalidHandle) return;
        struct sockaddr_storage storage;
        int                     len = sizeof(storage);
        if (srt_getsockname(_sock, reinterpret_cast<struct sockaddr *>(&storage), &len) == SRT_ERROR) return;
        auto r = SocketAddress::fromSockAddr(reinterpret_cast<struct sockaddr *>(&storage), static_cast<size_t>(len));
        if (r.second().isOk()) _localAddress = r.first();
}

void SrtSocket::updatePeerAddress() {
        if (_sock == InvalidHandle) return;
        struct sockaddr_storage storage;
        int                     len = sizeof(storage);
        if (srt_getpeername(_sock, reinterpret_cast<struct sockaddr *>(&storage), &len) == SRT_ERROR) return;
        auto r = SocketAddress::fromSockAddr(reinterpret_cast<struct sockaddr *>(&storage), static_cast<size_t>(len));
        if (r.second().isOk()) _peerAddress = r.first();
}

void SrtSocket::captureLastError() {
        const char *s = srt_getlasterror_str();
        _lastError = (s ? String(s) : String());
}

// ============================================================
// SRT options
// ============================================================

Error SrtSocket::applyPreConnectOptions() {
        if (_sock == InvalidHandle) return Error::NotOpen;

        // Transport type — must come before any other live/file-mode
        // option, since SRT recomputes defaults when this changes.
        const SRT_TRANSTYPE tt = (_transportType == File) ? SRTT_FILE : SRTT_LIVE;
        if (srt_setsockflag(_sock, SRTO_TRANSTYPE, &tt, sizeof(tt)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }

        if (_rendezvous) {
                const int v = 1;
                if (srt_setsockflag(_sock, SRTO_RENDEZVOUS, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        if (_payloadSize > 0) {
                const int v = _payloadSize;
                if (srt_setsockflag(_sock, SRTO_PAYLOADSIZE, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        // Latency — symmetric setter is SRTO_LATENCY (sets both
        // SRTO_RCVLATENCY and SRTO_PEERLATENCY).
        if (_latencyMs > 0) {
                const int v = _latencyMs;
                if (srt_setsockflag(_sock, SRTO_LATENCY, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        if (!_passphrase.isEmpty()) {
                if (srt_setsockflag(_sock, SRTO_PASSPHRASE, _passphrase.cstr(), _passphrase.byteCount()) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
                if (_pbKeyLen != 0) {
                        const int v = _pbKeyLen;
                        if (srt_setsockflag(_sock, SRTO_PBKEYLEN, &v, sizeof(v)) == SRT_ERROR) {
                                captureLastError();
                                return Error::LibraryFailure;
                        }
                }
        }

        if (!_streamId.isEmpty()) {
                if (srt_setsockflag(_sock, SRTO_STREAMID, _streamId.cstr(), _streamId.byteCount()) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setLatency(int ms) {
        if (ms < 0 || ms > 60000) return Error::Invalid;
        _latencyMs = ms;
        if (_sock != InvalidHandle && ms > 0) {
                if (srt_setsockflag(_sock, SRTO_LATENCY, &ms, sizeof(ms)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setPayloadSize(int bytes) {
        if (bytes < 0 || bytes > 1456) return Error::Invalid;
        _payloadSize = bytes;
        if (_sock != InvalidHandle && bytes > 0) {
                if (srt_setsockflag(_sock, SRTO_PAYLOADSIZE, &bytes, sizeof(bytes)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setTransportType(TransportType type) {
        _transportType = type;
        if (_sock != InvalidHandle) {
                const SRT_TRANSTYPE tt = (type == File) ? SRTT_FILE : SRTT_LIVE;
                if (srt_setsockflag(_sock, SRTO_TRANSTYPE, &tt, sizeof(tt)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setPassphrase(const String &passphrase) {
        if (!passphrase.isEmpty() && (passphrase.byteCount() < 10 || passphrase.byteCount() > 79)) return Error::Invalid;
        _passphrase = passphrase;
        if (_sock != InvalidHandle) {
                const int n = static_cast<int>(passphrase.byteCount());
                if (srt_setsockflag(_sock, SRTO_PASSPHRASE, passphrase.cstr(), n) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setEncryptionKeyLength(int bytes) {
        if (bytes != 0 && bytes != 16 && bytes != 24 && bytes != 32) return Error::Invalid;
        _pbKeyLen = bytes;
        if (_sock != InvalidHandle && bytes != 0) {
                if (srt_setsockflag(_sock, SRTO_PBKEYLEN, &bytes, sizeof(bytes)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setStreamId(const String &id) {
        if (id.byteCount() > 512) return Error::Invalid;
        _streamId = id;
        if (_sock != InvalidHandle) {
                if (srt_setsockflag(_sock, SRTO_STREAMID, id.cstr(), static_cast<int>(id.byteCount())) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

String SrtSocket::streamId() const {
        if (_sock == InvalidHandle) return _streamId;
        char buf[513] = {0};
        int  len = sizeof(buf);
        if (srt_getsockflag(_sock, SRTO_STREAMID, buf, &len) == SRT_ERROR) return String();
        return String(buf, static_cast<size_t>(len));
}

Error SrtSocket::setMaxBandwidth(int64_t bytesPerSec) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (srt_setsockflag(_sock, SRTO_MAXBW, &bytesPerSec, sizeof(bytesPerSec)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtSocket::setReceiveBufferSize(int bytes) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (srt_setsockflag(_sock, SRTO_UDP_RCVBUF, &bytes, sizeof(bytes)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtSocket::setSendBufferSize(int bytes) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (srt_setsockflag(_sock, SRTO_UDP_SNDBUF, &bytes, sizeof(bytes)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtSocket::setReceiveTimeout(int timeoutMs) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (srt_setsockflag(_sock, SRTO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtSocket::setSendTimeout(int timeoutMs) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (srt_setsockflag(_sock, SRTO_SNDTIMEO, &timeoutMs, sizeof(timeoutMs)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtSocket::setConnectTimeout(int timeoutMs) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (srt_setsockflag(_sock, SRTO_CONNTIMEO, &timeoutMs, sizeof(timeoutMs)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

SrtSocket::Stats SrtSocket::stats(bool resetWindowed) const {
        Stats out;
        if (_sock == InvalidHandle) return out;
        SRT_TRACEBSTATS perf{};
        if (srt_bstats(_sock, &perf, resetWindowed ? 1 : 0) == SRT_ERROR) return out;
        out.msSinceStart      = perf.msTimeStamp;
        out.pktSent           = perf.pktSentTotal;
        out.pktRecv           = perf.pktRecvTotal;
        out.byteSent          = perf.byteSentTotal;
        out.byteRecv          = perf.byteRecvTotal;
        out.pktSndLost        = perf.pktSndLossTotal;
        out.pktRcvLost        = perf.pktRcvLossTotal;
        out.pktRetransmitted  = perf.pktRetransTotal;
        out.pktSndDrop        = perf.pktSndDropTotal;
        out.pktRcvDrop        = perf.pktRcvDropTotal;
        out.pktRcvUndecrypt   = perf.pktRcvUndecryptTotal;
        out.rttMs             = perf.msRTT;
        out.linkBandwidthMbps = perf.mbpsBandwidth;
        out.maxBandwidthMbps  = perf.mbpsMaxBW;
        out.sendBufferBytes   = perf.byteSndBuf;
        out.sendBufferMs      = perf.msSndBuf;
        out.recvBufferBytes   = perf.byteRcvBuf;
        out.recvBufferMs      = perf.msRcvBuf;
        return out;
}

Error SrtSocket::setRendezvous(bool enable) {
        _rendezvous = enable;
        if (_sock != InvalidHandle) {
                const int v = enable ? 1 : 0;
                if (srt_setsockflag(_sock, SRTO_RENDEZVOUS, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtSocket::setNonBlocking(bool enable) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        const int sync = enable ? 0 : 1;
        if (srt_setsockflag(_sock, SRTO_RCVSYN, &sync, sizeof(sync)) == SRT_ERROR ||
            srt_setsockflag(_sock, SRTO_SNDSYN, &sync, sizeof(sync)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
