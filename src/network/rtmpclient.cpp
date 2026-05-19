/**
 * @file      rtmpclient.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtmpclient.h>

#include <cstring>
#include <netdb.h>
#include <sys/socket.h>

#include <promeki/buffer.h>
#include <promeki/elapsedtimer.h>
#include <promeki/iodevice.h>
#include <promeki/ipv4address.h>
#include <promeki/logger.h>
#include <promeki/socketaddress.h>
#include <promeki/tcpsocket.h>
#include <promeki/thread.h>

#if PROMEKI_ENABLE_TLS
#include <promeki/sslsocket.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtmpClient);

namespace {

constexpr uint16_t kDefaultRtmpPort  = 1935;
constexpr uint16_t kDefaultRtmpsPort = 443;

/**
 * @brief Resolves @p host to an IPv4 address.
 *
 * Synchronous; for the common localhost / LAN / cached cases the
 * lookup is effectively instantaneous.  Async DNS is out of scope
 * for v1.
 */
Error resolveHost(const String &host, uint32_t &outIPv4) {
        struct addrinfo  hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res = nullptr;
        const int        rc = ::getaddrinfo(host.cstr(), nullptr, &hints, &res);
        if (rc != 0 || res == nullptr) {
                if (res != nullptr) ::freeaddrinfo(res);
                return Error::HostNotFound;
        }
        const struct sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(res->ai_addr);
        outIPv4 = ntohl(sin->sin_addr.s_addr);
        ::freeaddrinfo(res);
        return Error::Ok;
}

}  // namespace

// ---------------------------------------------------------------------------
// Writer / reader thread declarations
// ---------------------------------------------------------------------------

/**
 * @brief Drains @c _writeQueue onto the session.
 *
 * Lives for the duration the client is open.  Exits when the queue's
 * @c cancelWaiters wakes us with @c Error::Cancelled or when a write
 * fails terminally.
 */
class RtmpClient::WriterThread : public Thread {
        public:
                explicit WriterThread(RtmpClient *owner) : _owner(owner) {
                        setName("RtmpClientWriter");
                }
                ~WriterThread() override {
                        // Thread base joins via wait() in its dtor.
                }

        protected:
                void run() override {
                        for (;;) {
                                if (_owner->_stopping.value()) return;
                                Result<RtmpMessage> got = _owner->_writeQueue.pop(250);
                                if (got.second().isError()) {
                                        if (got.second() == Error::Cancelled) return;
                                        if (got.second() == Error::Timeout) continue;
                                        promekiWarn("RtmpClient::WriterThread: write queue pop "
                                                    "failed: %s — exiting writer",
                                                    got.second().desc().cstr());
                                        return;
                                }
                                RtmpSession *s = _owner->_session.get();
                                if (s == nullptr) {
                                        promekiWarn("RtmpClient::WriterThread: session vanished "
                                                    "while draining write queue — exiting writer");
                                        return;
                                }
                                Error err = s->sendMessage(got.first());
                                if (err.isError()) {
                                        promekiWarn("RtmpClient::WriterThread: sendMessage failed "
                                                    "(type=%d, msid=%u, ts=%u, size=%zu): %s",
                                                    static_cast<int>(got.first().type),
                                                    got.first().streamId,
                                                    got.first().timestamp,
                                                    got.first().payload.size(),
                                                    err.desc().cstr());
                                        _owner->teardown(err);
                                        return;
                                }
                                if (got.first().type == RtmpMessage::VideoMessage) {
                                        _owner->_videoMessagesSent.setValue(
                                                _owner->_videoMessagesSent.value() + 1);
                                } else if (got.first().type == RtmpMessage::AudioMessage) {
                                        _owner->_audioMessagesSent.setValue(
                                                _owner->_audioMessagesSent.value() + 1);
                                }
                                _owner->_bytesSent.setValue(
                                        _owner->_bytesSent.value()
                                        + static_cast<int64_t>(got.first().payload.size()));
                        }
                }

        private:
                RtmpClient *_owner;
};

/**
 * @brief Pumps @c RtmpSession::readMessage and demuxes audio / video /
 *        metadata into per-kind queues.
 */
class RtmpClient::ReaderThread : public Thread {
        public:
                explicit ReaderThread(RtmpClient *owner) : _owner(owner) {
                        setName("RtmpClientReader");
                }
                ~ReaderThread() override = default;

        protected:
                void run() override {
                        for (;;) {
                                if (_owner->_stopping.value()) return;
                                RtmpSession *s = _owner->_session.get();
                                if (s == nullptr) {
                                        promekiWarn("RtmpClient::ReaderThread: session vanished "
                                                    "— exiting reader");
                                        return;
                                }
                                Result<RtmpMessage> got = s->readMessage(250);
                                if (got.second() == Error::Timeout) continue;
                                if (got.second().isError()) {
                                        promekiWarn("RtmpClient::ReaderThread: readMessage failed: "
                                                    "%s — tearing down client",
                                                    got.second().desc().cstr());
                                        _owner->teardown(got.second());
                                        return;
                                }
                                _owner->_bytesReceived.setValue(
                                        _owner->_bytesReceived.value()
                                        + static_cast<int64_t>(got.first().payload.size()));
                                switch (got.first().type) {
                                        case RtmpMessage::VideoMessage: {
                                                FlvVideoTag tag;
                                                Error perr = FlvVideoTag::unpack(
                                                        BufferView(got.first().payload, 0,
                                                                   got.first().payload.size()),
                                                        tag);
                                                if (perr.isError()) {
                                                        promekiDebug(
                                                                "RtmpClient: FlvVideoTag::unpack "
                                                                "failed (%s) — dropping message",
                                                                perr.name().cstr());
                                                        break;
                                                }
                                                _owner->_videoRxQueue.pushBlocking(tag, 1000);
                                                _owner->_videoMessagesReceived.setValue(
                                                        _owner->_videoMessagesReceived.value() + 1);
                                                break;
                                        }
                                        case RtmpMessage::AudioMessage: {
                                                FlvAudioTag tag;
                                                Error perr = FlvAudioTag::unpack(
                                                        BufferView(got.first().payload, 0,
                                                                   got.first().payload.size()),
                                                        tag);
                                                if (perr.isError()) {
                                                        promekiDebug(
                                                                "RtmpClient: FlvAudioTag::unpack "
                                                                "failed (%s) — dropping message",
                                                                perr.name().cstr());
                                                        break;
                                                }
                                                _owner->_audioRxQueue.pushBlocking(tag, 1000);
                                                _owner->_audioMessagesReceived.setValue(
                                                        _owner->_audioMessagesReceived.value() + 1);
                                                break;
                                        }
                                        default:
                                                // CommandMessageAmf0 / DataMessageAmf0 /
                                                // UserControl / chunk-layer control msgs are
                                                // handled inside RtmpSession::readMessage.
                                                break;
                                }
                        }
                }

        private:
                RtmpClient *_owner;
};

// ---------------------------------------------------------------------------
// Construction / teardown
// ---------------------------------------------------------------------------

RtmpClient::RtmpClient(ObjectBase *parent) : ObjectBase(parent) {
        _writeQueue.setMaxSize(DefaultSendQueueDepth);
        _videoRxQueue.setMaxSize(DefaultReadQueueDepth);
        _audioRxQueue.setMaxSize(DefaultReadQueueDepth);
        _metadataRxQueue.setMaxSize(DefaultReadQueueDepth);
}

RtmpClient::~RtmpClient() {
        close();
}

void RtmpClient::splitPath(const Url &url, String &app, String &streamKey) {
        const String &path = url.path();
        if (path.isEmpty()) {
                app.clear();
                streamKey.clear();
                return;
        }
        // Strip the leading '/' for the AMF0 `app` field.
        size_t start = (path.charAt(0) == '/') ? 1 : 0;
        // Find the last '/' separator (after the leading one).
        int lastSlash = -1;
        for (size_t i = start; i < path.length(); i++) {
                if (path.charAt(i) == '/') lastSlash = static_cast<int>(i);
        }
        if (lastSlash < 0) {
                // Single segment after the host: the AMF0 `connect`
                // call needs a non-empty `app`, so treat the segment
                // as the app and leave the stream key empty.  Callers
                // can supply the stream key via
                // @c MediaConfig::RtmpStreamKey or the publish() /
                // play() arguments — e.g. YouTube's
                // `rtmp://a.rtmp.youtube.com/live2` with the key
                // pinned out-of-band.
                app = path.substr(start);
                streamKey.clear();
        } else {
                app = path.substr(start, lastSlash - static_cast<int>(start));
                streamKey = path.substr(lastSlash + 1);
        }
}

void RtmpClient::teardown(Error reason) {
        bool wasOpen = _open.value();
        if (!_stopping.value()) {
                _stopping.setValue(true);
                _writeQueue.cancelWaiters();
                _videoRxQueue.cancelWaiters();
                _audioRxQueue.cancelWaiters();
                _metadataRxQueue.cancelWaiters();
        }
        if (wasOpen) {
                _open.setValue(false);
                disconnectedSignal.emit(reason);
        }
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

Error RtmpClient::open(const Url &url, const RtmpConnectOptions &userOpts,
                       unsigned int timeoutMs) {
        if (_open.value()) {
                promekiWarn("RtmpClient::open: already open (url='%s')",
                            url.toString().cstr());
                return Error::Exists;
        }

        _url = url;
        const String &scheme = url.scheme();
        bool isTls = false;
        if (scheme == "rtmps") {
                isTls = true;
        } else if (scheme == "rtmp") {
                isTls = false;
        } else {
                promekiWarn("RtmpClient::open: unsupported URL scheme '%s' (url='%s')",
                            scheme.cstr(), url.toString().cstr());
                return Error::InvalidArgument;
        }

#if !PROMEKI_ENABLE_TLS
        if (isTls) {
                promekiWarn("RtmpClient::open: rtmps:// requested but TLS is not "
                            "enabled in this build (url='%s')", url.toString().cstr());
                return Error::NotSupported;
        }
#endif

        if (url.host().isEmpty()) {
                promekiWarn("RtmpClient::open: empty host in URL '%s'",
                            url.toString().cstr());
                return Error::InvalidArgument;
        }

        splitPath(url, _app, _streamKey);
        promekiDebug("RtmpClient::open: url='%s' app='%s' urlKey='%s'",
                     url.toString().cstr(), _app.cstr(), _streamKey.cstr());

        // Open the right socket.
#if PROMEKI_ENABLE_TLS
        _isTls = isTls;
        if (isTls) {
                SslSocket *ssl = new SslSocket();
                // SslContext's default constructor already auto-loads
                // the system CA bundle.  SslSocket fails-closed if no
                // anchors are available.
                ssl->setSslContext(_sslContext);
                _socket = UniquePtr<TcpSocket>::takeOwnership(ssl);
        } else {
                _socket = UniquePtr<TcpSocket>::takeOwnership(new TcpSocket());
        }
#else
        _socket = UniquePtr<TcpSocket>::takeOwnership(new TcpSocket());
#endif
        if (Error err = _socket->open(IODevice::ReadWrite); err.isError()) {
                promekiWarn("RtmpClient::open: socket open failed (url='%s'): %s",
                            url.toString().cstr(), err.desc().cstr());
                _socket.reset();
                return err;
        }

        // Resolve + connect.
        uint32_t ipv4 = 0;
        if (Error err = resolveHost(url.host(), ipv4); err.isError()) {
                promekiWarn("RtmpClient::open: DNS resolution failed for host '%s': %s",
                            url.host().cstr(), err.desc().cstr());
                _socket.reset();
                return err;
        }
        uint16_t port = static_cast<uint16_t>(url.port() > 0 ? url.port()
                                              : (isTls ? kDefaultRtmpsPort : kDefaultRtmpPort));
        if (Error err = _socket->connectToHost(SocketAddress(Ipv4Address(ipv4), port));
            err.isError()) {
                promekiWarn("RtmpClient::open: TCP connect to %s:%u failed: %s",
                            Ipv4Address(ipv4).toString().cstr(), port, err.desc().cstr());
                _socket.reset();
                return err;
        }
        _socket->setNoDelay(true);
        _socket->setKeepAlive(true);

#if PROMEKI_ENABLE_TLS
        if (isTls) {
                SslSocket *ssl = static_cast<SslSocket *>(_socket.get());
                if (Error err = ssl->startEncryption(url.host()); err.isError() && err != Error::TryAgain) {
                        promekiWarn("RtmpClient::open: TLS startEncryption(%s) failed: %s",
                                    url.host().cstr(), err.desc().cstr());
                        _socket.reset();
                        return err;
                }
                ElapsedTimer timer;
                timer.start();
                while (!ssl->isEncrypted()) {
                        int64_t elapsedMs = timer.elapsedUs() / 1000;
                        if (elapsedMs >= static_cast<int64_t>(timeoutMs)) {
                                promekiWarn("RtmpClient::open: TLS handshake timed out after %u ms",
                                            timeoutMs);
                                _socket.reset();
                                return Error::Timeout;
                        }
                        Error step = ssl->continueHandshake();
                        if (step.isError() && step != Error::TryAgain) {
                                promekiWarn("RtmpClient::open: TLS continueHandshake failed: %s",
                                            step.desc().cstr());
                                _socket.reset();
                                return step;
                        }
                        if (ssl->isEncrypted()) break;
                        unsigned int rem = static_cast<unsigned int>(timeoutMs - elapsedMs);
                        if (!ssl->waitForReadyRead(rem)) {
                                promekiWarn("RtmpClient::open: TLS waitForReadyRead timed out "
                                            "after %u ms remaining", rem);
                                _socket.reset();
                                return Error::Timeout;
                        }
                }
        }
#endif

        // Build the session and run handshake + connect.
        _session = UniquePtr<RtmpSession>::create(RtmpRole::Client, this);
        if (Error err = _session->attach(_socket.get()); err.isError()) {
                promekiWarn("RtmpClient::open: session attach failed: %s",
                            err.desc().cstr());
                _session.reset();
                _socket.reset();
                return err;
        }
        if (Error err = _session->performHandshake(timeoutMs); err.isError()) {
                promekiWarn("RtmpClient::open: RTMP handshake failed: %s",
                            err.desc().cstr());
                _session.reset();
                _socket.reset();
                return err;
        }

        RtmpConnectOptions opts = userOpts;
        if (opts.app.isEmpty()) opts.app = _app;
        if (opts.tcUrl.isEmpty()) {
                // Reconstruct a minimal tcUrl from scheme + host + port + app
                // (callers can pin a different value via @p opts).
                String tcUrl = scheme;
                tcUrl += "://";
                tcUrl += url.host();
                if (url.port() > 0 && url.port() != kDefaultRtmpPort && url.port() != kDefaultRtmpsPort) {
                        tcUrl += ":";
                        tcUrl += String::number(url.port());
                }
                tcUrl += "/";
                tcUrl += _app;
                opts.tcUrl = tcUrl;
        }
        if (opts.app.isEmpty()) {
                promekiWarn("RtmpClient::open: empty AMF0 `app` field — most RTMP servers "
                            "(YouTube, Twitch, nginx-rtmp) will reject the connect call. "
                            "URL '%s' may need to be of the form rtmp://host/<app>[/<streamKey>].",
                            url.toString().cstr());
        }
        if (Error err = _session->connect(opts, timeoutMs); err.isError()) {
                promekiWarn("RtmpClient::open: RTMP NetConnection.connect failed "
                            "(app='%s', tcUrl='%s'): %s",
                            opts.app.cstr(), opts.tcUrl.cstr(), err.desc().cstr());
                _session.reset();
                _socket.reset();
                return err;
        }

        _open.setValue(true);
        _stopping.setValue(false);
        // Writer + reader threads start after publish() / play()
        // succeeds — running them during the synchronous
        // createStream / publish / play AMF0 round-trips would race
        // the foreground caller for inbound messages on the same
        // session.
        connectedSignal.emit();
        return Error::Ok;
}

void RtmpClient::startMediaThreads() {
        if (_writer.get() != nullptr || _reader.get() != nullptr) return;
        _stopping.setValue(false);
        _writer = UniquePtr<WriterThread>::create(this);
        _reader = UniquePtr<ReaderThread>::create(this);
        _writer->start();
        _reader->start();
}

// ---------------------------------------------------------------------------
// publish / play
// ---------------------------------------------------------------------------

Error RtmpClient::publish(const String &streamKey, const String &mode, unsigned int timeoutMs) {
        if (!_open.value()) {
                promekiWarn("RtmpClient::publish: client is not open");
                return Error::Invalid;
        }
        if (_session.get() == nullptr) {
                promekiWarn("RtmpClient::publish: no session attached");
                return Error::Invalid;
        }

        String key = streamKey.isEmpty() ? _streamKey : streamKey;
        if (key.isEmpty()) {
                promekiWarn("RtmpClient::publish: no stream key (neither argument nor "
                            "MediaConfig::RtmpStreamKey supplied one) — URL was '%s'",
                            _url.toString().cstr());
                return Error::InvalidArgument;
        }

        // Most FMS-clone destinations want releaseStream + FCPublish ahead of createStream.
        _session->releaseStream(key);
        _session->fcPublish(key);

        Result<uint32_t> sidR = _session->createStream(timeoutMs);
        if (sidR.second().isError()) {
                promekiWarn("RtmpClient::publish: createStream failed (key='%s'): %s",
                            key.cstr(), sidR.second().desc().cstr());
                return sidR.second();
        }
        _streamId.setValue(sidR.first());

        Error err = _session->publish(sidR.first(), key, mode, timeoutMs);
        if (err.isError()) {
                promekiWarn("RtmpClient::publish: publish failed "
                            "(streamId=%u, key='%s', mode='%s'): %s",
                            sidR.first(), key.cstr(), mode.cstr(), err.desc().cstr());
        } else {
                startMediaThreads();
        }
        return err;
}

Error RtmpClient::play(const String &streamKey, unsigned int timeoutMs, bool useFcSubscribe) {
        if (!_open.value()) {
                promekiWarn("RtmpClient::play: client is not open");
                return Error::Invalid;
        }
        if (_session.get() == nullptr) {
                promekiWarn("RtmpClient::play: no session attached");
                return Error::Invalid;
        }

        String key = streamKey.isEmpty() ? _streamKey : streamKey;
        if (key.isEmpty()) {
                promekiWarn("RtmpClient::play: no stream key (neither argument nor "
                            "MediaConfig::RtmpStreamKey supplied one) — URL was '%s'",
                            _url.toString().cstr());
                return Error::InvalidArgument;
        }

        Result<uint32_t> sidR = _session->createStream(timeoutMs);
        if (sidR.second().isError()) {
                promekiWarn("RtmpClient::play: createStream failed (key='%s'): %s",
                            key.cstr(), sidR.second().desc().cstr());
                return sidR.second();
        }
        _streamId.setValue(sidR.first());
        if (useFcSubscribe) _session->fcSubscribe(key);
        Error err = _session->play(sidR.first(), key, -2.0, -1.0, timeoutMs);
        if (err.isError()) {
                promekiWarn("RtmpClient::play: play failed "
                            "(streamId=%u, key='%s'): %s",
                            sidR.first(), key.cstr(), err.desc().cstr());
        } else {
                startMediaThreads();
        }
        return err;
}

// ---------------------------------------------------------------------------
// send / take
// ---------------------------------------------------------------------------

Error RtmpClient::sendVideo(const FlvVideoTag &tag, uint32_t timestampMs) {
        if (!_open.value()) return Error::Invalid;
        Buffer payload;
        if (Error err = tag.pack(payload); err.isError()) return err;
        RtmpMessage msg;
        msg.type = RtmpMessage::VideoMessage;
        msg.streamId = _streamId.value();
        msg.timestamp = timestampMs;
        msg.chunkStreamId = 6;
        msg.payload = payload;
        Error pushErr = _writeQueue.pushBlocking(msg, 50);
        if (pushErr == Error::Timeout) return Error::TryAgain;
        return pushErr;
}

Error RtmpClient::sendAudio(const FlvAudioTag &tag, uint32_t timestampMs) {
        if (!_open.value()) return Error::Invalid;
        Buffer payload;
        if (Error err = tag.pack(payload); err.isError()) return err;
        RtmpMessage msg;
        msg.type = RtmpMessage::AudioMessage;
        msg.streamId = _streamId.value();
        msg.timestamp = timestampMs;
        msg.chunkStreamId = 4;
        msg.payload = payload;
        Error pushErr = _writeQueue.pushBlocking(msg, 50);
        if (pushErr == Error::Timeout) return Error::TryAgain;
        return pushErr;
}

Error RtmpClient::sendMetadata(const Metadata & /*meta*/, uint32_t timestampMs) {
        if (!_open.value()) return Error::Invalid;
        // v1: emit @c @setDataFrame + onMetaData with an empty ecma-array.
        // The downstream RtmpMediaIO (Phase 5) populates the metadata
        // payload from a concrete `MediaDesc`; for now we ship the
        // wire-frame so the transport mechanics are exercised.
        Buffer body;
        Amf0Writer w(body);
        w.writeString("@setDataFrame");
        w.writeString("onMetaData");
        w.writeEcmaArray(Amf0Value::FieldList{}, 0);
        RtmpMessage msg;
        msg.type = RtmpMessage::DataMessageAmf0;
        msg.streamId = _streamId.value();
        msg.timestamp = timestampMs;
        msg.chunkStreamId = 5;
        msg.payload = body;
        Error pushErr = _writeQueue.pushBlocking(msg, 50);
        if (pushErr == Error::Timeout) return Error::TryAgain;
        return pushErr;
}

Result<FlvVideoTag> RtmpClient::takeVideo(unsigned int timeoutMs) {
        if (!_open.value() && _videoRxQueue.size() == 0) return makeError<FlvVideoTag>(Error::Invalid);
        Result<FlvVideoTag> got = _videoRxQueue.pop(timeoutMs);
        if (got.second() == Error::Timeout) return makeError<FlvVideoTag>(Error::TryAgain);
        return got;
}

Result<FlvAudioTag> RtmpClient::takeAudio(unsigned int timeoutMs) {
        if (!_open.value() && _audioRxQueue.size() == 0) return makeError<FlvAudioTag>(Error::Invalid);
        Result<FlvAudioTag> got = _audioRxQueue.pop(timeoutMs);
        if (got.second() == Error::Timeout) return makeError<FlvAudioTag>(Error::TryAgain);
        return got;
}

Result<Metadata> RtmpClient::takeMetadata(unsigned int timeoutMs) {
        if (!_open.value() && _metadataRxQueue.size() == 0) return makeError<Metadata>(Error::Invalid);
        Result<Metadata> got = _metadataRxQueue.pop(timeoutMs);
        if (got.second() == Error::Timeout) return makeError<Metadata>(Error::TryAgain);
        return got;
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------

Error RtmpClient::close() {
        if (!_open.value() && _writer.get() == nullptr && _reader.get() == nullptr) {
                _socket.reset();
                _session.reset();
                return Error::Ok;
        }
        _stopping.setValue(true);
        _writeQueue.cancelWaiters();
        _videoRxQueue.cancelWaiters();
        _audioRxQueue.cancelWaiters();
        _metadataRxQueue.cancelWaiters();

        if (_writer.get() != nullptr) {
                _writer->wait();
                _writer.reset();
        }
        if (_reader.get() != nullptr) {
                _reader->wait();
                _reader.reset();
        }
        _session.reset();
        if (_socket.get() != nullptr) {
                _socket->close();
                _socket.reset();
        }
        bool wasOpen = _open.value();
        _open.setValue(false);
        if (wasOpen) disconnectedSignal.emit(Error::Ok);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
