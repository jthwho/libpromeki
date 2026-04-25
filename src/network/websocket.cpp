/**
 * @file      websocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/websocket.h>
#include <promeki/tcpsocket.h>
#include <promeki/eventloop.h>
#include <promeki/application.h>
#include <promeki/socketaddress.h>
#include <promeki/ipv4address.h>
#include <promeki/sha1.h>
#include <promeki/base64.h>
#include <promeki/random.h>
#include <promeki/logger.h>
#include <promeki/stringlist.h>
#if PROMEKI_ENABLE_TLS
#include <promeki/sslsocket.h>
#endif
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(WebSocket);

// ============================================================
// Magic GUID per RFC 6455 §1.3.  Concatenated with the client's
// Sec-WebSocket-Key, SHA-1'd, and base64'd to produce the value
// the server returns in Sec-WebSocket-Accept.
// ============================================================
static const char *kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ============================================================
// WebSocket frame opcodes (RFC 6455 §5.2)
// ============================================================
enum WsOpcode : uint8_t {
        OpContinuation  = 0x0,
        OpText          = 0x1,
        OpBinary        = 0x2,
        OpClose         = 0x8,
        OpPing          = 0x9,
        OpPong          = 0xA
};

// ============================================================
// Pimpl: framing scratch lives here so the public header doesn't
// need to know how messages are reassembled.
// ============================================================
struct WebSocket::Impl {
        // Outbound: pending bytes to write to the socket.  Frames
        // are serialized into this buffer; pumpWrite drains it.
        Buffer          writeQueue;
        size_t          writeOffset = 0;

        // Inbound parser state.  Bytes from the socket are
        // accumulated into `inbound` until at least one full frame
        // (header + payload) is available.  A multi-frame data
        // message accumulates payloads in `messageBuf` with
        // `messageOpcode` recording the original (first-frame)
        // opcode.
        Buffer          inbound;
        size_t          inboundSize = 0;        ///< Logical bytes used inside `inbound`

        Buffer          messageBuf;
        size_t          messageSize = 0;
        uint8_t         messageOpcode = 0;
        bool            messageInProgress = false;

        // Client-side handshake scratch.
        String          handshakeBuf;           ///< Accumulated HTTP response bytes
        bool            handshakeComplete = false;
        Buffer          requestBytes;           ///< Outbound HTTP upgrade request
        size_t          requestOffset = 0;

        // Per-instance random key for the client handshake (16 raw
        // bytes, base64-encoded for the wire).
        String          clientKey;

        // Extra client-side request headers.
        promeki::List<String> extraHeaderNames;
        promeki::List<String> extraHeaderValues;

        // The client-driven close handshake fires `disconnected`
        // only after the peer's close frame arrives or the socket
        // EOFs.  Track so we don't double-emit.
        bool            sawPeerClose = false;
        bool            sentClose = false;
        bool            disconnectedEmitted = false;
};

// ============================================================
// Helpers
// ============================================================

namespace {

// Append-friendly write into a Buffer (mirrors HttpConnection's helper).
static void bufAppend(Buffer &buf, size_t &usedSize, const void *src, size_t n) {
        if(n == 0) return;
        size_t haveCap = buf.isValid() ? buf.availSize() : 0;
        if(usedSize + n > haveCap) {
                size_t newCap = std::max<size_t>(haveCap * 2, usedSize + n);
                if(newCap < 256) newCap = 256;
                Buffer grown(newCap);
                if(usedSize > 0 && buf.isValid()) {
                        std::memcpy(grown.data(), buf.data(), usedSize);
                }
                grown.setSize(usedSize);
                buf = std::move(grown);
        }
        std::memcpy(static_cast<uint8_t *>(buf.data()) + usedSize, src, n);
        usedSize += n;
        buf.setSize(usedSize);
}

static void bufAppendByte(Buffer &buf, size_t &usedSize, uint8_t b) {
        bufAppend(buf, usedSize, &b, 1);
}

// Synchronous IPv4 hostname resolution (matches HttpClient).  Async
// DNS is out of scope for v1.
static Error resolveHost(const String &host, uint32_t &outIPv4) {
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res = nullptr;
        const int rc = ::getaddrinfo(host.cstr(), nullptr, &hints, &res);
        if(rc != 0 || res == nullptr) {
                if(res != nullptr) ::freeaddrinfo(res);
                return Error::HostNotFound;
        }
        const struct sockaddr_in *sin =
                reinterpret_cast<const sockaddr_in *>(res->ai_addr);
        outIPv4 = ntohl(sin->sin_addr.s_addr);
        ::freeaddrinfo(res);
        return Error::Ok;
}

} // anonymous namespace

// ============================================================
// Construction / destruction
// ============================================================

WebSocket::WebSocket(ObjectBase *parent) :
        ObjectBase(parent),
        _impl(UniquePtr<Impl>::create())
{
        _loop = EventLoop::current();
        if(_loop == nullptr) _loop = Application::mainEventLoop();
}

WebSocket::~WebSocket() {
        if(_state != Disconnected) {
                abort();
        }
}

// ============================================================
// Static helpers
// ============================================================

String WebSocket::computeAcceptValue(const String &clientKey) {
        // RFC 6455: base64(sha1(clientKey + GUID))
        const String concat = clientKey + String(kWsGuid);
        const SHA1Digest digest = sha1(concat.cstr(), concat.byteCount());
        return Base64::encode(digest.data(), digest.size());
}

// ============================================================
// Client-side connect
// ============================================================

void WebSocket::setRequestHeader(const String &name, const String &value) {
        _impl->extraHeaderNames.pushToBack(name);
        _impl->extraHeaderValues.pushToBack(value);
}

Error WebSocket::connectToUrl(const String &urlStr) {
        if(_state != Disconnected) return Error::AlreadyOpen;
        Error parseErr;
        Url u = Url::fromString(urlStr, &parseErr);
        if(parseErr.isError() || !u.isValid()) return Error::Invalid;
        if(u.host().isEmpty())                 return Error::Invalid;

        _useTls = false;
        if(u.scheme() == "wss") {
#if PROMEKI_ENABLE_TLS
                _useTls = true;
#else
                return Error::NotImplemented;
#endif
        } else if(u.scheme() != "ws") {
                return Error::Invalid;
        }

        _url = u;
        _isClient = true;
        _state = Connecting;

        uint32_t ipv4 = 0;
        Error err = resolveHost(u.host(), ipv4);
        if(err.isError()) { _state = Disconnected; return err; }

        int port = u.port();
        if(port == Url::PortUnset) port = _useTls ? 443 : 80;

#if PROMEKI_ENABLE_TLS
        if(_useTls) {
                SslSocket *ssl = new SslSocket(this);
                if(!_sslContext.isValid()) {
                        _sslContext = SslContext::Ptr::takeOwnership(new SslContext());
                }
                ssl->setSslContext(_sslContext);
                _socket = ssl;
        } else {
                _socket = new TcpSocket(this);
        }
#else
        _socket = new TcpSocket(this);
#endif

        err = _socket->open(IODevice::ReadWrite);
        if(err.isError()) { finalizeClose(err); return err; }
        _socket->setNonBlocking(true);

        err = _socket->connectToHost(SocketAddress(Ipv4Address(ipv4),
                static_cast<uint16_t>(port)));
        if(err.isError()) { finalizeClose(err); return err; }

#if PROMEKI_ENABLE_TLS
        if(_useTls) {
                SslSocket *ssl = static_cast<SslSocket *>(_socket);
                Error sslErr = ssl->startEncryption(u.host());
                if(sslErr.isError() && sslErr != Error::TryAgain) {
                        finalizeClose(sslErr); return sslErr;
                }
                _handshakeDone = ssl->isEncrypted();
        } else {
                _handshakeDone = true;
        }
#else
        _handshakeDone = true;
#endif

        // Generate a random 16-byte key, base64-encode.  Save the
        // expected accept value so we can verify the response.
        Buffer keyBytes = Random::global().randomBytes(16);
        _impl->clientKey = Base64::encode(keyBytes.data(), keyBytes.size());
        _expectedAccept  = computeAcceptValue(_impl->clientKey);

        // Build the upgrade request.  Held in Impl::requestBytes
        // because the underlying socket may not be writable yet —
        // pumpWrite drains it once the loop signals readiness.
        String head;
        head += "GET ";
        head += u.path().isEmpty() ? "/" : u.path();
        if(!u.query().isEmpty()) {
                head += "?";
                bool first = true;
                for(auto it = u.query().cbegin(); it != u.query().cend(); ++it) {
                        if(!first) head += "&";
                        first = false;
                        head += Url::percentEncode(it->first);
                        head += "=";
                        head += Url::percentEncode(it->second);
                }
        }
        head += " HTTP/1.1\r\n";
        head += "Host: " + u.host();
        if(u.port() != Url::PortUnset) {
                head += ":" + String::number(u.port());
        }
        head += "\r\n";
        head += "Upgrade: websocket\r\n";
        head += "Connection: Upgrade\r\n";
        head += "Sec-WebSocket-Key: " + _impl->clientKey + "\r\n";
        head += "Sec-WebSocket-Version: 13\r\n";
        if(!_requestedSubprotocols.isEmpty()) {
                head += "Sec-WebSocket-Protocol: " + _requestedSubprotocols + "\r\n";
        }
        for(size_t i = 0; i < _impl->extraHeaderNames.size(); ++i) {
                head += _impl->extraHeaderNames[i] + ": "
                      + _impl->extraHeaderValues[i] + "\r\n";
        }
        head += "\r\n";

        _impl->requestBytes = Buffer(head.byteCount() == 0 ? 1 : head.byteCount());
        std::memcpy(_impl->requestBytes.data(), head.cstr(), head.byteCount());
        _impl->requestBytes.setSize(head.byteCount());
        _impl->requestOffset = 0;

        registerIo(EventLoop::IoRead | EventLoop::IoWrite);
        return Error::Ok;
}

// ============================================================
// Server-side adoption
// ============================================================

void WebSocket::adoptUpgradedSocket(TcpSocket *socket) {
        if(_state != Disconnected) return;
        _socket = socket;
        if(_socket != nullptr) _socket->setParent(this);
        _isClient = false;
        _handshakeDone = true;
        _state = Connected;
        registerIo(EventLoop::IoRead);
        // Defer the connectedSignal one tick so the application has
        // a chance to wire up its receivers between adoption and the
        // first data event.
        if(_loop != nullptr) {
                _loop->postCallable([this]() {
                        if(_state == Connected) connectedSignal.emit();
                });
        }
}

// ============================================================
// I/O
// ============================================================

void WebSocket::registerIo(uint32_t mask) {
        if(_loop == nullptr || _socket == nullptr) return;
        if(_ioHandle >= 0) {
                _loop->removeIoSource(_ioHandle);
                _ioHandle = -1;
        }
        const int fd = _socket->socketDescriptor();
        if(fd < 0) return;
        _ioHandle = _loop->addIoSource(fd, mask,
                [this](int f, uint32_t e) { onIoReady(f, e); });
}

void WebSocket::onIoReady(int fd, uint32_t events) {
        (void)fd;
        if(_state == Disconnected) return;

        if(events & EventLoop::IoError) {
                finalizeClose(Error::ConnectionReset);
                return;
        }

#if PROMEKI_ENABLE_TLS
        if(_isClient && _useTls && !_handshakeDone) {
                SslSocket *ssl = static_cast<SslSocket *>(_socket);
                Error err = ssl->continueHandshake();
                if(err == Error::TryAgain) return;
                if(err.isError()) { finalizeClose(err); return; }
                _handshakeDone = true;
                // Fall through to drain any queued upgrade request.
        }
#endif

        if(events & EventLoop::IoWrite) pumpWrite();
        if(events & EventLoop::IoRead)  readSome();
}

void WebSocket::pumpWrite() {
        if(_socket == nullptr) return;

        // Client-side handshake: drain the upgrade request first.
        if(_isClient && !_impl->handshakeComplete && _impl->requestBytes.isValid()) {
                const size_t total = _impl->requestBytes.size();
                while(_impl->requestOffset < total) {
                        const char *src = static_cast<const char *>(
                                _impl->requestBytes.data()) + _impl->requestOffset;
                        const int64_t want = static_cast<int64_t>(total - _impl->requestOffset);
                        const int64_t n = _socket->write(src, want);
                        if(n < 0) return;     // EAGAIN, wait for next IoWrite
                        if(n == 0) { finalizeClose(Error::ConnectionReset); return; }
                        _impl->requestOffset += static_cast<size_t>(n);
                }
                if(_impl->requestOffset >= total) {
                        // Done sending — drop the write subscription.
                        registerIo(EventLoop::IoRead);
                }
                return;
        }

        // Drain frames queued by sendFrame.
        if(_impl->writeQueue.isValid()) {
                while(_impl->writeOffset < _impl->writeQueue.size()) {
                        const char *src = static_cast<const char *>(
                                _impl->writeQueue.data()) + _impl->writeOffset;
                        const int64_t want = static_cast<int64_t>(
                                _impl->writeQueue.size() - _impl->writeOffset);
                        const int64_t n = _socket->write(src, want);
                        if(n < 0) return;     // EAGAIN
                        if(n == 0) { finalizeClose(Error::ConnectionReset); return; }
                        _impl->writeOffset += static_cast<size_t>(n);
                }
                // All queued frames sent.  Reset the queue and drop
                // write subscription so we don't busy-spin.
                _impl->writeQueue = Buffer();
                _impl->writeOffset = 0;
                registerIo(EventLoop::IoRead);
        }
}

void WebSocket::readSome() {
        if(_socket == nullptr) return;
        char buf[8192];
        const int64_t n = _socket->read(buf, sizeof(buf));
        if(n == 0) {
                // Peer half-closed.  If we never got a close frame,
                // treat as abnormal close.
                finalizeClose(_impl->sawPeerClose ? Error::Ok : Error::ConnectionReset);
                return;
        }
        if(n < 0) return;     // EAGAIN
        bufAppend(_impl->inbound, _impl->inboundSize, buf, static_cast<size_t>(n));

        if(_isClient && !_impl->handshakeComplete) {
                parseClientHandshakeResponse();
                if(!_impl->handshakeComplete) return;
        }
        processIncomingBytes();
}

// ============================================================
// Client handshake response parsing
// ============================================================

void WebSocket::parseClientHandshakeResponse() {
        // Find the end of the header block.
        const char *data = static_cast<const char *>(_impl->inbound.data());
        const size_t avail = _impl->inboundSize;
        const String view(data, avail);
        const size_t sep = view.find("\r\n\r\n");
        if(sep == String::npos) return;       // need more bytes

        const String head = view.left(sep);
        const StringList lines = head.split("\r\n");
        if(lines.size() == 0) {
                finalizeClose(Error::Invalid);
                return;
        }

        // Status line: HTTP/1.1 101 Switching Protocols
        const String &status = lines[0];
        const size_t sp1 = status.find(' ');
        if(sp1 == String::npos) { finalizeClose(Error::Invalid); return; }
        const size_t sp2 = status.find(' ', sp1 + 1);
        const String code = (sp2 == String::npos)
                ? status.mid(sp1 + 1)
                : status.mid(sp1 + 1, sp2 - sp1 - 1);
        if(std::atoi(code.cstr()) != 101) {
                finalizeClose(Error::Invalid);
                return;
        }

        // Verify Sec-WebSocket-Accept and capture the negotiated
        // subprotocol if any.
        bool acceptOk = false;
        for(size_t i = 1; i < lines.size(); ++i) {
                const String &ln = lines[i];
                const size_t colon = ln.find(':');
                if(colon == String::npos) continue;
                String name  = ln.left(colon).trim().toLower();
                String value = ln.mid(colon + 1).trim();
                if(name == "sec-websocket-accept") {
                        if(value == _expectedAccept) acceptOk = true;
                } else if(name == "sec-websocket-protocol") {
                        _negotiatedSubprotocol = value;
                }
        }
        if(!acceptOk) { finalizeClose(Error::Invalid); return; }

        // Trim consumed bytes from inbound buffer (header + 4 sep bytes).
        const size_t consumed = sep + 4;
        const size_t leftover = avail - consumed;
        if(leftover > 0) {
                Buffer kept(leftover);
                std::memcpy(kept.data(),
                            static_cast<const uint8_t *>(_impl->inbound.data()) + consumed,
                            leftover);
                kept.setSize(leftover);
                _impl->inbound = std::move(kept);
                _impl->inboundSize = leftover;
        } else {
                _impl->inbound = Buffer();
                _impl->inboundSize = 0;
        }
        _impl->handshakeComplete = true;
        _state = Connected;
        connectedSignal.emit();
}

// ============================================================
// Frame parsing (RFC 6455 §5.2)
// ============================================================

void WebSocket::processIncomingBytes() {
        // Loop: parse as many full frames as available.  A frame is
        // discarded from the inbound buffer once its payload has been
        // delivered to the message accumulator (or as a control
        // frame).
        while(_state != Disconnected) {
                const uint8_t *p = static_cast<const uint8_t *>(_impl->inbound.data());
                const size_t avail = _impl->inboundSize;
                if(avail < 2) return;     // need at least the 2-byte header

                const uint8_t b0 = p[0];
                const uint8_t b1 = p[1];
                const bool fin     = (b0 & 0x80) != 0;
                const uint8_t op   =  b0 & 0x0F;
                const bool masked  = (b1 & 0x80) != 0;
                uint64_t plen      =  b1 & 0x7F;

                size_t hdrLen = 2;
                if(plen == 126) {
                        if(avail < 4) return;
                        plen = (uint64_t(p[2]) << 8) | uint64_t(p[3]);
                        hdrLen = 4;
                } else if(plen == 127) {
                        if(avail < 10) return;
                        plen = 0;
                        for(int i = 0; i < 8; ++i) {
                                plen = (plen << 8) | uint64_t(p[2 + i]);
                        }
                        hdrLen = 10;
                }
                uint8_t mask[4] = {0, 0, 0, 0};
                if(masked) {
                        if(avail < hdrLen + 4) return;
                        std::memcpy(mask, p + hdrLen, 4);
                        hdrLen += 4;
                }

                // Server-side: client MUST mask (RFC 6455 §5.1).
                // Client-side: server MUST NOT mask.
                if(_isClient && masked) {
                        finalizeClose(Error::Invalid);
                        return;
                }
                if(!_isClient && !masked) {
                        finalizeClose(Error::Invalid);
                        return;
                }

                if(_maxMessageBytes >= 0 &&
                   plen > static_cast<uint64_t>(_maxMessageBytes)) {
                        enqueueClose(CloseMessageTooBig, "frame too large");
                        return;
                }
                if(avail < hdrLen + plen) return;     // need more bytes

                Buffer payload;
                if(plen > 0) {
                        payload = Buffer(static_cast<size_t>(plen));
                        payload.setSize(static_cast<size_t>(plen));
                        const uint8_t *pp = p + hdrLen;
                        uint8_t *dst = static_cast<uint8_t *>(payload.data());
                        if(masked) {
                                for(uint64_t i = 0; i < plen; ++i) {
                                        dst[i] = pp[i] ^ mask[i & 3];
                                }
                        } else {
                                std::memcpy(dst, pp, static_cast<size_t>(plen));
                        }
                }

                // Consume the frame from the inbound buffer.
                const size_t total = hdrLen + static_cast<size_t>(plen);
                const size_t leftover = avail - total;
                if(leftover > 0) {
                        Buffer kept(leftover);
                        std::memcpy(kept.data(),
                                    static_cast<const uint8_t *>(_impl->inbound.data()) + total,
                                    leftover);
                        kept.setSize(leftover);
                        _impl->inbound = std::move(kept);
                        _impl->inboundSize = leftover;
                } else {
                        _impl->inbound = Buffer();
                        _impl->inboundSize = 0;
                }

                // Control frames (op >= 0x8) MUST NOT be fragmented
                // and MUST NOT exceed 125 bytes (RFC 6455 §5.5).
                if(op >= 0x8) {
                        if(!fin || plen > 125) {
                                enqueueClose(CloseProtocolError, "bad control frame");
                                return;
                        }
                        handleControlFrame(op, std::move(payload));
                        continue;
                }

                // Data frame.  Reassemble into messageBuf.
                if(op == OpContinuation) {
                        if(!_impl->messageInProgress) {
                                enqueueClose(CloseProtocolError, "unexpected continuation");
                                return;
                        }
                } else {
                        if(_impl->messageInProgress) {
                                enqueueClose(CloseProtocolError, "expected continuation");
                                return;
                        }
                        _impl->messageOpcode = op;
                        _impl->messageInProgress = true;
                        _impl->messageBuf = Buffer();
                        _impl->messageSize = 0;
                }
                if(payload.isValid() && payload.size() > 0) {
                        bufAppend(_impl->messageBuf, _impl->messageSize,
                                  payload.data(), payload.size());
                }
                if(_maxMessageBytes >= 0 &&
                   _impl->messageSize > static_cast<size_t>(_maxMessageBytes)) {
                        enqueueClose(CloseMessageTooBig, "message too large");
                        return;
                }
                if(fin) {
                        Buffer msg = std::move(_impl->messageBuf);
                        msg.setSize(_impl->messageSize);
                        _impl->messageBuf = Buffer();
                        _impl->messageSize = 0;
                        _impl->messageInProgress = false;
                        handleDataMessage(_impl->messageOpcode, std::move(msg));
                }
        }
}

void WebSocket::handleControlFrame(uint8_t opcode, Buffer payload) {
        switch(opcode) {
                case OpClose: {
                        _impl->sawPeerClose = true;
                        // Echo a close frame if we haven't sent one
                        // yet (RFC 6455 §5.5.1).
                        if(!_impl->sentClose) {
                                // Echo the same code if present.
                                Buffer echo;
                                if(payload.isValid() && payload.size() >= 2) {
                                        echo = Buffer(2);
                                        std::memcpy(echo.data(), payload.data(), 2);
                                        echo.setSize(2);
                                }
                                sendFrame(OpClose,
                                          echo.isValid() ? echo.data() : nullptr,
                                          echo.isValid() ? echo.size() : 0);
                                _impl->sentClose = true;
                        }
                        // Wait for write drain, then tear down.  The
                        // socket close will be triggered when the
                        // peer closes its side.
                        finalizeClose(Error::Ok);
                        break;
                }
                case OpPing: {
                        // Reply with a pong carrying the same payload.
                        sendFrame(OpPong,
                                  payload.isValid() ? payload.data() : nullptr,
                                  payload.isValid() ? payload.size() : 0);
                        break;
                }
                case OpPong: {
                        pongReceivedSignal.emit(payload);
                        break;
                }
                default:
                        enqueueClose(CloseProtocolError, "unknown control opcode");
                        break;
        }
}

void WebSocket::handleDataMessage(uint8_t opcode, Buffer payload) {
        if(opcode == OpText) {
                String text;
                if(payload.isValid() && payload.size() > 0) {
                        text = String(static_cast<const char *>(payload.data()),
                                      payload.size());
                }
                textMessageReceivedSignal.emit(text);
        } else if(opcode == OpBinary) {
                binaryMessageReceivedSignal.emit(payload);
        } else {
                enqueueClose(CloseProtocolError, "unknown data opcode");
        }
}

// ============================================================
// Frame send
// ============================================================

Error WebSocket::sendFrame(uint8_t opcode, const void *data, size_t len, bool fin) {
        if(_state == Disconnected) return Error::NotOpen;

        // Header: 2 bytes minimum, +2 / +8 for extended length, +4
        // for the mask key on client-side frames.
        const bool mask = _isClient;
        uint8_t header[14];
        size_t hLen = 0;
        header[hLen++] = static_cast<uint8_t>((fin ? 0x80 : 0) | (opcode & 0x0F));

        uint8_t lenByte = mask ? 0x80 : 0x00;
        if(len < 126) {
                header[hLen++] = lenByte | static_cast<uint8_t>(len);
        } else if(len <= 0xFFFF) {
                header[hLen++] = lenByte | 126;
                header[hLen++] = static_cast<uint8_t>((len >> 8) & 0xFF);
                header[hLen++] = static_cast<uint8_t>(len & 0xFF);
        } else {
                header[hLen++] = lenByte | 127;
                for(int i = 7; i >= 0; --i) {
                        header[hLen++] = static_cast<uint8_t>(
                                (static_cast<uint64_t>(len) >> (i * 8)) & 0xFF);
                }
        }

        uint8_t maskKey[4] = {0, 0, 0, 0};
        if(mask) {
                Buffer keyBuf = Random::global().randomBytes(4);
                std::memcpy(maskKey, keyBuf.data(), 4);
                std::memcpy(header + hLen, maskKey, 4);
                hLen += 4;
        }

        // Append header + (masked) payload at the logical end of the
        // write queue.  pumpWrite consumes from writeOffset up to the
        // queue's logical size; new frames always go after the last
        // pending byte regardless of how much has already drained.
        size_t logical = _impl->writeQueue.isValid() ? _impl->writeQueue.size() : 0;
        bufAppend(_impl->writeQueue, logical, header, hLen);
        if(len > 0) {
                const size_t before = logical;
                bufAppend(_impl->writeQueue, logical, data, len);
                if(mask) {
                        uint8_t *p = static_cast<uint8_t *>(_impl->writeQueue.data()) + before;
                        for(size_t i = 0; i < len; ++i) {
                                p[i] ^= maskKey[i & 3];
                        }
                }
        }

        // Subscribe to write readiness if not already.
        registerIo(EventLoop::IoRead | EventLoop::IoWrite);
        // Drive an immediate write so small frames leave the socket
        // without an extra loop tick.
        pumpWrite();
        return Error::Ok;
}

Error WebSocket::sendTextMessage(const String &message) {
        if(_state != Connected) return Error::NotOpen;
        return sendFrame(OpText, message.cstr(), message.byteCount(), true);
}

Error WebSocket::sendBinaryMessage(const Buffer &message) {
        if(_state != Connected) return Error::NotOpen;
        const void *data = message.isValid() ? message.data() : nullptr;
        const size_t len = message.isValid() ? message.size() : 0;
        return sendFrame(OpBinary, data, len, true);
}

Error WebSocket::ping(const Buffer &payload) {
        if(_state != Connected) return Error::NotOpen;
        const void *data = payload.isValid() ? payload.data() : nullptr;
        const size_t len = payload.isValid() ? payload.size() : 0;
        if(len > 125) return Error::Invalid;
        return sendFrame(OpPing, data, len, true);
}

void WebSocket::enqueueClose(uint16_t code, const String &reason) {
        if(_impl->sentClose) return;
        Buffer payload(2 + reason.byteCount());
        uint8_t *p = static_cast<uint8_t *>(payload.data());
        p[0] = static_cast<uint8_t>((code >> 8) & 0xFF);
        p[1] = static_cast<uint8_t>(code & 0xFF);
        if(reason.byteCount() > 0) {
                std::memcpy(p + 2, reason.cstr(), reason.byteCount());
        }
        payload.setSize(2 + reason.byteCount());
        sendFrame(OpClose, payload.data(), payload.size(), true);
        _impl->sentClose = true;
        _state = Closing;
}

void WebSocket::disconnect(uint16_t code, const String &reason) {
        if(_state == Disconnected || _state == Closing) return;
        enqueueClose(code, reason);
}

void WebSocket::abort() {
        finalizeClose(Error::Ok);
}

void WebSocket::finalizeClose(Error err) {
        if(_state == Disconnected) return;
        _state = Disconnected;

        if(_loop != nullptr && _ioHandle >= 0) {
                _loop->removeIoSource(_ioHandle);
                _ioHandle = -1;
        }
        if(_socket != nullptr && _socket->isOpen()) {
                _socket->close();
        }
        if(_socket != nullptr) {
                delete _socket;
                _socket = nullptr;
        }

        if(err.isError()) errorOccurredSignal.emit(err);
        if(!_impl->disconnectedEmitted) {
                _impl->disconnectedEmitted = true;
                disconnectedSignal.emit();
        }
}

PROMEKI_NAMESPACE_END
