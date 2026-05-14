/**
 * @file      srtgroup.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/list.h>
#include <promeki/srtgroup.h>
#include <promeki/logger.h>

#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>

#include <srt/srt.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SrtGroup);

namespace {

        // Map our Type enum to libsrt's SRT_GTYPE_*.
        static SRT_GROUP_TYPE toSrtGroupType(SrtGroup::Type t) {
                switch (t) {
                        case SrtGroup::Backup: return SRT_GTYPE_BACKUP;
                        case SrtGroup::Broadcast:
                        default:               return SRT_GTYPE_BROADCAST;
                }
        }

} // anonymous namespace

SrtGroup::SrtGroup(Type type, ObjectBase *parent) : IODevice(parent), _type(type) {}

SrtGroup::SrtGroup(int adoptHandle, Type type, ObjectBase *parent)
    : IODevice(parent), _sock(adoptHandle), _type(type) {
        if (_sock != InvalidHandle) {
                setOpenMode(ReadWrite);
        }
}

SrtGroup::~SrtGroup() {
        if (isOpen()) close();
}

Error SrtGroup::open(OpenMode mode) {
        if (isOpen()) return Error::AlreadyOpen;
        _sock = srt_create_group(toSrtGroupType(_type));
        if (_sock == SRT_INVALID_SOCK) {
                _sock = InvalidHandle;
                captureLastError();
                return Error::LibraryFailure;
        }
        setOpenMode(mode);
        return Error::Ok;
}

Error SrtGroup::close() {
        if (_sock == InvalidHandle) return Error::Ok;
        srt_close(_sock);
        _sock = InvalidHandle;
        setOpenMode(NotOpen);
        return Error::Ok;
}

bool SrtGroup::isOpen() const {
        return _sock != InvalidHandle;
}

Error SrtGroup::applyPreConnectOptions() {
        if (_sock == InvalidHandle) return Error::NotOpen;

        if (_latencyMs > 0) {
                const int v = _latencyMs;
                if (srt_setsockflag(_sock, SRTO_LATENCY, &v, sizeof(v)) == SRT_ERROR) {
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
        if (!_passphrase.isEmpty()) {
                if (srt_setsockflag(_sock, SRTO_PASSPHRASE, _passphrase.cstr(),
                                    static_cast<int>(_passphrase.byteCount())) == SRT_ERROR) {
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
                if (srt_setsockflag(_sock, SRTO_STREAMID, _streamId.cstr(),
                                    static_cast<int>(_streamId.byteCount())) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        if (_maxBw != 0) {
                if (srt_setsockflag(_sock, SRTO_MAXBW, &_maxBw, sizeof(_maxBw)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        if (_recvTimeoutMs != 0) {
                if (srt_setsockflag(_sock, SRTO_RCVTIMEO, &_recvTimeoutMs, sizeof(_recvTimeoutMs)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        if (_sendTimeoutMs != 0) {
                if (srt_setsockflag(_sock, SRTO_SNDTIMEO, &_sendTimeoutMs, sizeof(_sendTimeoutMs)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        if (_connectTimeoutMs != 0) {
                if (srt_setsockflag(_sock, SRTO_CONNTIMEO, &_connectTimeoutMs, sizeof(_connectTimeoutMs)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }
        return Error::Ok;
}

Error SrtGroup::connect(const MemberList &members) {
        if (_sock == InvalidHandle) return Error::NotOpen;
        if (members.isEmpty()) return Error::Invalid;

        Error e = applyPreConnectOptions();
        if (e.isError()) return e;

        // Convert MemberList to libsrt's SRT_SOCKGROUPCONFIG[].  Each
        // entry is built via srt_prepare_endpoint(src, dst, len).
        List<SRT_SOCKGROUPCONFIG> configs;
        configs.reserve(members.size());
        List<sockaddr_storage> srcStorage(members.size());
        List<sockaddr_storage> dstStorage(members.size());

        for (size_t i = 0; i < members.size(); ++i) {
                const Member &m = members[i];
                if (m.peerAddress.isNull()) return Error::Invalid;
                const size_t  dstLen = m.peerAddress.toSockAddr(&dstStorage[i]);
                if (dstLen == 0) return Error::Invalid;
                struct sockaddr *srcSa = nullptr;
                if (!m.sourceAddress.isNull()) {
                        const size_t srcLen = m.sourceAddress.toSockAddr(&srcStorage[i]);
                        if (srcLen == 0) return Error::Invalid;
                        srcSa = reinterpret_cast<struct sockaddr *>(&srcStorage[i]);
                }
                SRT_SOCKGROUPCONFIG cfg = srt_prepare_endpoint(
                        srcSa,
                        reinterpret_cast<struct sockaddr *>(&dstStorage[i]),
                        static_cast<int>(dstLen));
                configs.pushToBack(cfg);
        }

        const int rc = srt_connect_group(_sock, configs.data(), static_cast<int>(configs.size()));
        if (rc == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

int64_t SrtGroup::read(void *data, int64_t maxSize) {
        if (!isOpen()) return -1;
        const int n = srt_recv(_sock, static_cast<char *>(data), static_cast<int>(maxSize));
        if (n == SRT_ERROR) {
                captureLastError();
                setError(Error::IOError);
                return -1;
        }
        return static_cast<int64_t>(n);
}

int64_t SrtGroup::write(const void *data, int64_t maxSize) {
        if (!isOpen()) return -1;
        const int n = srt_send(_sock, static_cast<const char *>(data), static_cast<int>(maxSize));
        if (n == SRT_ERROR) {
                captureLastError();
                setError(Error::IOError);
                return -1;
        }
        return static_cast<int64_t>(n);
}

SrtGroup::MemberStatusList SrtGroup::memberStatus() const {
        MemberStatusList out;
        if (_sock == InvalidHandle) return out;
        size_t n = 0;
        // First call with nullptr returns the count via inoutlen.
        if (srt_group_data(_sock, nullptr, &n) == SRT_ERROR && n == 0) return out;
        List<SRT_SOCKGROUPDATA> data(n);
        if (n > 0) {
                if (srt_group_data(_sock, data.data(), &n) == SRT_ERROR) return out;
        }
        for (size_t i = 0; i < n; ++i) {
                MemberStatus s;
                s.handle = data[i].id;
                auto r = SocketAddress::fromSockAddr(reinterpret_cast<const struct sockaddr *>(&data[i].peeraddr),
                                                    sizeof(data[i].peeraddr));
                if (r.second().isOk()) s.peerAddress = r.first();
                s.state      = static_cast<int>(data[i].sockstate);
                s.membership = static_cast<int>(data[i].memberstate);
                s.weight     = data[i].weight;
                out.pushToBack(s);
        }
        return out;
}

void SrtGroup::captureLastError() {
        const char *s = srt_getlasterror_str();
        _lastError = (s ? String(s) : String());
}

// ============================================================
// Pre-connect option setters — buffered until applyPreConnectOptions().
// ============================================================

Error SrtGroup::setLatency(int ms) {
        if (ms < 0 || ms > 60000) return Error::Invalid;
        _latencyMs = ms;
        return Error::Ok;
}

Error SrtGroup::setPassphrase(const String &passphrase) {
        if (!passphrase.isEmpty() && (passphrase.byteCount() < 10 || passphrase.byteCount() > 79)) return Error::Invalid;
        _passphrase = passphrase;
        return Error::Ok;
}

Error SrtGroup::setEncryptionKeyLength(int bytes) {
        if (bytes != 0 && bytes != 16 && bytes != 24 && bytes != 32) return Error::Invalid;
        _pbKeyLen = bytes;
        return Error::Ok;
}

Error SrtGroup::setStreamId(const String &id) {
        if (id.byteCount() > 512) return Error::Invalid;
        _streamId = id;
        return Error::Ok;
}

Error SrtGroup::setPayloadSize(int bytes) {
        if (bytes < 0 || bytes > 1456) return Error::Invalid;
        _payloadSize = bytes;
        return Error::Ok;
}

Error SrtGroup::setMaxBandwidth(int64_t bytesPerSec) {
        _maxBw = bytesPerSec;
        return Error::Ok;
}

Error SrtGroup::setReceiveTimeout(int timeoutMs) {
        _recvTimeoutMs = timeoutMs;
        return Error::Ok;
}

Error SrtGroup::setSendTimeout(int timeoutMs) {
        _sendTimeoutMs = timeoutMs;
        return Error::Ok;
}

Error SrtGroup::setConnectTimeout(int timeoutMs) {
        _connectTimeoutMs = timeoutMs;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
