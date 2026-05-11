/**
 * @file      rtmpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtmpsession.h>

#include <cstring>

#include <promeki/buildinfo.h>
#include <promeki/elapsedtimer.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtmpSession);

namespace {

// ---------------------------------------------------------------------------
// CS-id conventions per the RTMP ecosystem (matches ffmpeg / OBS / librtmp).
// ---------------------------------------------------------------------------
constexpr uint32_t kCsidControl    = 2;
constexpr uint32_t kCsidCommand    = 3;
constexpr uint32_t kCsidUserCtrl   = 2;  ///< Some servers expect UC on cs-id 2 (with the rest of the protocol-control traffic).

// ---------------------------------------------------------------------------
// User-control sub-types (RTMP §6.2).
// ---------------------------------------------------------------------------
constexpr uint16_t kUcStreamBegin       = 0;
constexpr uint16_t kUcStreamEof         = 1;
constexpr uint16_t kUcStreamDry         = 2;
constexpr uint16_t kUcSetBufferLength   = 3;
constexpr uint16_t kUcStreamIsRecorded  = 4;
constexpr uint16_t kUcPingRequest       = 6;
constexpr uint16_t kUcPingResponse      = 7;

void writeBE16(uint8_t *p, uint16_t v) {
        p[0] = static_cast<uint8_t>((v >> 8) & 0xff);
        p[1] = static_cast<uint8_t>(v & 0xff);
}

uint16_t readBE16(const uint8_t *p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t readBE32(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24)
             | (static_cast<uint32_t>(p[1]) << 16)
             | (static_cast<uint32_t>(p[2]) << 8)
             | static_cast<uint32_t>(p[3]);
}

/**
 * @brief Returns the library's default `flashVer` string.
 */
String defaultFlashVer() {
        const BuildInfo *bi = getBuildInfo();
        const char      *ver = (bi != nullptr && bi->version != nullptr) ? bi->version : "0";
        return String::sprintf("FMLE/3.0 (compatible; libpromeki/%s)", ver);
}

}  // namespace

// ---------------------------------------------------------------------------
// onStatus → Error mapping (RTMP §7.2.2 + Adobe NetStream / NetConnection table).
// ---------------------------------------------------------------------------

Error RtmpSession::onStatusToError(const String &code) {
        if (code == "NetConnection.Connect.Success")    return Error::Ok;
        if (code == "NetConnection.Connect.Rejected")   return Error::PermissionDenied;
        if (code == "NetConnection.Connect.InvalidApp") return Error::InvalidArgument;
        if (code == "NetConnection.Connect.Failed")     return Error::ConnectionRefused;
        if (code == "NetConnection.Connect.AppShutdown") return Error::Cancelled;
        if (code == "NetConnection.Connect.Closed")     return Error::Cancelled;
        if (code == "NetStream.Publish.Start")          return Error::Ok;
        if (code == "NetStream.Publish.BadName")        return Error::Exists;
        if (code == "NetStream.Publish.Denied")         return Error::PermissionDenied;
        if (code == "NetStream.Play.Start")             return Error::Ok;
        if (code == "NetStream.Play.Reset")             return Error::Ok;  // not an error — just a sync notice
        if (code == "NetStream.Play.StreamNotFound")    return Error::NotFound;
        if (code == "NetStream.Play.Failed")            return Error::IOError;
        if (code == "NetStream.Authenticate.UsherToken") return Error::AuthenticationRequired;
        // Unknown / unrecognized status code — surface as ProtocolError; the
        // raw code is preserved in the `onStatus` signal arg.
        return Error::ProtocolError;
}

// ---------------------------------------------------------------------------
// Construction / attachment
// ---------------------------------------------------------------------------

RtmpSession::RtmpSession(RtmpRole role, ObjectBase *parent)
    : ObjectBase(parent), _role(role), _handshake(role, this) {}

RtmpSession::~RtmpSession() = default;

Error RtmpSession::attach(IODevice *device) {
        _device = device;
        if (device != nullptr) {
                _chunk = UniquePtr<RtmpChunkStream>::create(device, this);
        } else {
                _chunk.reset();
        }
        return Error::Ok;
}

double RtmpSession::nextTransactionId() {
        double v = _nextTxnId.value();
        _nextTxnId.setValue(v + 1.0);
        return v;
}

// ---------------------------------------------------------------------------
// Handshake driver
// ---------------------------------------------------------------------------

Error RtmpSession::performHandshake(unsigned int timeoutMs) {
        if (_device == nullptr) return Error::Invalid;

        ElapsedTimer timer;
        timer.start();
        for (;;) {
                Buffer pending = _handshake.pendingOutput();
                while (pending.size() > 0) {
                        int64_t n = _device->write(pending.data(), static_cast<int64_t>(pending.size()));
                        if (n < 0) {
                                _handshake.markPeerClosed();
                                return Error::IOError;
                        }
                        if (n == 0) {
                                _handshake.markPeerClosed();
                                return Error::IOError;
                        }
                        if (static_cast<size_t>(n) < pending.size()) {
                                // Partial — re-slice the buffer for the next loop turn.
                                Buffer rest(pending.size() - static_cast<size_t>(n));
                                std::memcpy(rest.data(),
                                            static_cast<const uint8_t *>(pending.data()) + n,
                                            pending.size() - static_cast<size_t>(n));
                                rest.setSize(pending.size() - static_cast<size_t>(n));
                                pending = rest;
                                continue;
                        }
                        pending = _handshake.pendingOutput();
                }

                if (_handshake.state() == RtmpHandshake::Done) {
                        handshakeCompleteSignal.emit();
                        return Error::Ok;
                }
                if (_handshake.state() == RtmpHandshake::Failed) return _handshake.lastError();

                int64_t elapsed = timer.elapsedUs() / 1000;
                if (timeoutMs != 0 && elapsed >= static_cast<int64_t>(timeoutMs)) {
                        _handshake.markPeerClosed();
                        return Error::Timeout;
                }
                unsigned int rem = (timeoutMs == 0) ? 250 : static_cast<unsigned int>(timeoutMs - elapsed);
                if (_device->bytesAvailable() == 0) {
                        if (!_device->waitForReadyRead(rem)) continue;
                }
                uint8_t buf[4096];
                int64_t n = _device->read(buf, sizeof(buf));
                if (n < 0) {
                        _handshake.markPeerClosed();
                        return Error::IOError;
                }
                if (n == 0) {
                        // recv() returning zero on a connected TCP
                        // socket is EOF — the peer has half-closed.
                        // Continuing would spin (POLLHUP is sticky, so
                        // the next waitForReadyRead returns instantly).
                        _handshake.markPeerClosed();
                        return Error::IOError;
                }
                Buffer wrap = Buffer::wrapHost(buf, static_cast<size_t>(n));
                wrap.setSize(static_cast<size_t>(n));
                Error err = _handshake.feed(BufferView(wrap, 0, static_cast<size_t>(n)));
                if (err.isError()) return err;
        }
}

// ---------------------------------------------------------------------------
// Command serialization helper
// ---------------------------------------------------------------------------

Error RtmpSession::sendCommand(uint32_t csid, uint32_t msid,
                               const String &command, double txnId,
                               const Amf0Value::List &args) {
        if (_chunk.isValid() == false) return Error::Invalid;
        Buffer    payload;
        Amf0Writer w(payload);
        if (Error e = w.writeString(command); e.isError()) return e;
        if (Error e = w.writeNumber(txnId);   e.isError()) return e;
        for (const Amf0Value &v : args) {
                if (Error e = w.writeValue(v); e.isError()) return e;
        }
        RtmpMessage msg;
        msg.type = RtmpMessage::CommandMessageAmf0;
        msg.streamId = msid;
        msg.chunkStreamId = csid;
        msg.timestamp = 0;
        msg.payload = payload;
        return _chunk->writeMessage(msg);
}

// ---------------------------------------------------------------------------
// Transaction tracking
// ---------------------------------------------------------------------------

Error RtmpSession::pumpUntilTransaction(double txnId, unsigned int timeoutMs,
                                        PendingTransaction *outTxn) {
        if (_chunk.isValid() == false) return Error::Invalid;
        ElapsedTimer timer;
        timer.start();
        for (;;) {
                {
                        Mutex::Locker lk(_txnMutex);
                        auto it = _pending.find(txnId);
                        if (it != _pending.end() && it->second != nullptr && it->second->completed) {
                                if (outTxn != nullptr) *outTxn = *it->second;
                                Error result = it->second->result;
                                delete it->second;
                                _pending.remove(it);
                                return result;
                        }
                }
                int64_t elapsed = timer.elapsedUs() / 1000;
                if (timeoutMs != 0 && elapsed >= static_cast<int64_t>(timeoutMs)) {
                        return Error::Timeout;
                }
                unsigned int rem = (timeoutMs == 0) ? 250 : static_cast<unsigned int>(timeoutMs - elapsed);
                Result<RtmpMessage> got = _chunk->readMessage(rem);
                if (got.second().isError()) {
                        if (got.second() == Error::Timeout) continue;
                        return got.second();
                }
                Error dispatchErr = Error::Ok;
                switch (got.first().type) {
                        case RtmpMessage::CommandMessageAmf0:
                                dispatchErr = handleInboundCommand(got.first());
                                break;
                        case RtmpMessage::UserControl:
                                dispatchErr = handleInboundUserControl(got.first());
                                break;
                        case RtmpMessage::DataMessageAmf0:
                                dispatchErr = handleInboundData(got.first());
                                break;
                        case RtmpMessage::AudioMessage:
                                audioMessageReceivedSignal.emit(got.first());
                                break;
                        case RtmpMessage::VideoMessage:
                                videoMessageReceivedSignal.emit(got.first());
                                break;
                        default:
                                // SetChunkSize / WindowAckSize / SetPeerBandwidth / Acknowledgement /
                                // AbortMessage all auto-applied by the chunk layer.
                                break;
                }
                if (dispatchErr.isError()) return dispatchErr;
        }
}

// ---------------------------------------------------------------------------
// Inbound command (AMF0): _result / _error / onStatus / onBWDone / _checkbw
// ---------------------------------------------------------------------------

Error RtmpSession::handleInboundCommand(const RtmpMessage &msg) {
        Result<Amf0Value::List> parsed = Amf0Reader::read(
                static_cast<const uint8_t *>(msg.payload.data()), msg.payload.size());
        if (parsed.second().isError()) return parsed.second();

        const Amf0Value::List &vals = parsed.first();
        if (vals.size() < 2) return Error::CorruptData;
        if (!vals[0].isString() || !vals[1].isNumber()) return Error::CorruptData;

        String   command = vals[0].asString();
        double   txnId   = vals[1].asNumber();

        if (command == "_result" || command == "_error") {
                Mutex::Locker lk(_txnMutex);
                auto it = _pending.find(txnId);
                if (it == _pending.end() || it->second == nullptr) {
                        promekiDebug("RtmpSession: unmatched %s for txn=%g", command.cstr(), txnId);
                        return Error::Ok;
                }
                PendingTransaction *txn = it->second;
                // Find the info object — by convention the 4th value (after command, txnId, cmdObj).
                if (vals.size() >= 3) txn->commandObject = vals[2];
                if (vals.size() >= 4) txn->info = vals[3];
                if (command == "_error") {
                        txn->result = Error::ProtocolError;
                        if (vals.size() >= 4 && vals[3].isObject()) {
                                const Amf0Value *codeField = vals[3].find("code");
                                if (codeField != nullptr && codeField->isString()) {
                                        txn->result = onStatusToError(codeField->asString());
                                        if (txn->result.isOk()) txn->result = Error::ProtocolError;
                                }
                        }
                } else {
                        txn->result = Error::Ok;
                }
                txn->completed = true;
                return Error::Ok;
        }

        if (command == "onStatus") {
                Amf0Value info;
                if (vals.size() >= 4) info = vals[3];
                onStatusSignal.emit(info);
                // If a transaction is waiting for the same msid + this is a
                // play/publish-start, complete it.
                if (info.isObject()) {
                        const Amf0Value *codeField = info.find("code");
                        if (codeField != nullptr && codeField->isString()) {
                                String code = codeField->asString();
                                Error  mapped = onStatusToError(code);
                                // Complete the most recently pending status-bearing
                                // transaction (publish / play).  Caller's
                                // awaitTransaction provides the txnId mapping.
                                Mutex::Locker lk(_txnMutex);
                                auto it = _pending.find(txnId);
                                if (it != _pending.end() && it->second != nullptr) {
                                        it->second->info = info;
                                        it->second->result = mapped;
                                        it->second->completed = true;
                                }
                        }
                }
                return Error::Ok;
        }

        if (command == "onBWDone" || command == "_checkbw") {
                // No-op: bandwidth-check is a courtesy ping; we ignore it.
                return Error::Ok;
        }

        // Unhandled command — keep going.
        promekiDebug("RtmpSession: unhandled inbound command %s txn=%g", command.cstr(), txnId);
        return Error::Ok;
}

Error RtmpSession::handleInboundUserControl(const RtmpMessage &msg) {
        if (msg.payload.size() < 2) return Error::CorruptData;
        const auto *p = static_cast<const uint8_t *>(msg.payload.data());
        uint16_t evt = readBE16(p);
        switch (evt) {
                case kUcStreamBegin:
                        // Carries one 4-byte stream id — informational; just log.
                        if (msg.payload.size() >= 6) {
                                uint32_t sid = readBE32(p + 2);
                                promekiDebug("RtmpSession: StreamBegin sid=%u", sid);
                        }
                        break;
                case kUcStreamEof:
                case kUcStreamDry:
                case kUcStreamIsRecorded:
                case kUcSetBufferLength:
                        break;
                case kUcPingRequest: {
                        // Reply with PingResponse echoing the same 4-byte timestamp.
                        if (msg.payload.size() < 6) break;
                        uint8_t resp[6];
                        writeBE16(resp, kUcPingResponse);
                        std::memcpy(resp + 2, p + 2, 4);
                        RtmpMessage out;
                        out.type = RtmpMessage::UserControl;
                        out.streamId = 0;
                        out.chunkStreamId = kCsidUserCtrl;
                        out.timestamp = 0;
                        out.payload = Buffer(sizeof(resp));
                        out.payload.setSize(sizeof(resp));
                        std::memcpy(out.payload.data(), resp, sizeof(resp));
                        if (_chunk.isValid()) _chunk->writeMessage(out);
                        break;
                }
                case kUcPingResponse:
                        // Latency sample — surfaced as a counter when Phase 4 wires up
                        // RtmpClient::rttEstimate.
                        break;
                default:
                        break;
        }
        return Error::Ok;
}

Error RtmpSession::handleInboundData(const RtmpMessage &msg) {
        Result<Amf0Value::List> parsed = Amf0Reader::read(
                static_cast<const uint8_t *>(msg.payload.data()), msg.payload.size());
        if (parsed.second().isError()) return Error::Ok;  // best-effort
        const Amf0Value::List &vals = parsed.first();
        if (vals.size() < 1 || !vals[0].isString()) return Error::Ok;

        String name = vals[0].asString();
        if (name == "onMetaData" || name == "@setDataFrame") {
                // For @setDataFrame the actual command name is at index 1;
                // skip and parse from there.
                size_t start = (name == "@setDataFrame") ? 1 : 0;
                if (vals.size() <= start) return Error::Ok;
                // The metadata object follows immediately.
                const Amf0Value *meta = nullptr;
                if (vals.size() > start + 1) meta = &vals[start + 1];
                if (meta != nullptr && (meta->isObject() || meta->isEcmaArray())) {
                        Metadata md;
                        // Walk known keys we care about; everything else is dropped.
                        // Specific keys can be filled in as MediaIO consumers need
                        // them; we conservatively expose every string + number
                        // field as a Variant via setDescription would, but the
                        // first-cut keeps this minimal.
                        for (const auto &f : meta->fields()) {
                                (void)f;
                        }
                        onMetaDataSignal.emit(md);
                }
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// connect-flow helpers
// ---------------------------------------------------------------------------

void RtmpSession::applyConnectFlowDefaults() {
        if (!_chunk.isValid()) return;
        // Step 3 (client side): echo the server's window-ack-size and raise
        // our local chunk size.  The chunk layer's peerWindowAckSize was
        // populated by the inbound WindowAckSize message during the connect
        // reply pump.  We mirror that value back at the server and raise
        // our local chunk size to the default we use for steady-state.
        int peerWas = _chunk->peerWindowAckSize();
        int echo = (peerWas > 0) ? peerWas : RtmpChunkStream::DefaultWindowAckSize;
        _chunk->setLocalWindowAckSize(echo);
        _chunk->setLocalChunkSize(DefaultPostConnectChunkSize);
}

// ---------------------------------------------------------------------------
// High-level commands
// ---------------------------------------------------------------------------

Error RtmpSession::connect(const RtmpConnectOptions &opts, unsigned int timeoutMs) {
        if (!_chunk.isValid()) return Error::Invalid;
        double txnId = nextTransactionId();

        Amf0Value::FieldList fields;
        fields.pushToBack({"app", Amf0Value(opts.app)});
        fields.pushToBack({"flashVer",
                           Amf0Value(opts.flashVer.isEmpty() ? defaultFlashVer() : opts.flashVer)});
        if (!opts.swfUrl.isEmpty())   fields.pushToBack({"swfUrl",   Amf0Value(opts.swfUrl)});
        fields.pushToBack({"tcUrl", Amf0Value(opts.tcUrl)});
        fields.pushToBack({"fpad",  Amf0Value(false)});
        fields.pushToBack({"capabilities", Amf0Value(static_cast<double>(opts.capabilities))});
        fields.pushToBack({"audioCodecs",  Amf0Value(static_cast<double>(opts.audioCodecs))});
        fields.pushToBack({"videoCodecs",  Amf0Value(static_cast<double>(opts.videoCodecs))});
        fields.pushToBack({"videoFunction", Amf0Value(static_cast<double>(opts.videoFunction))});
        if (!opts.pageUrl.isEmpty())  fields.pushToBack({"pageUrl", Amf0Value(opts.pageUrl)});
        fields.pushToBack({"objectEncoding", Amf0Value(static_cast<double>(opts.objectEncoding))});
        if (!opts.fourCcList.isEmpty()) {
                Amf0Value::List items;
                for (const FourCC &fc : opts.fourCcList) {
                        char buf[5] = { static_cast<char>((fc.value() >> 24) & 0xff),
                                        static_cast<char>((fc.value() >> 16) & 0xff),
                                        static_cast<char>((fc.value() >> 8) & 0xff),
                                        static_cast<char>(fc.value() & 0xff), 0 };
                        items.pushToBack(Amf0Value(String(buf)));
                }
                Amf0Value fourCcArr = Amf0Value::strictArray();
                fourCcArr.items() = items;
                fields.pushToBack({"fourCcList", fourCcArr});
        }
        Amf0Value cmdObj = Amf0Value::object();
        cmdObj.fields() = fields;

        // Pre-register the pending transaction before sending — the reply
        // can land before sendCommand returns on a fast loopback.
        PendingTransaction *txn = new PendingTransaction();
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }

        Error sendErr = sendCommand(kCsidCommand, 0, "connect", txnId, { cmdObj });
        if (sendErr.isError()) {
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return sendErr;
        }

        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isOk()) {
                _connected = true;
                applyConnectFlowDefaults();
                connectedSignal.emit();
        } else if (result == Error::Timeout) {
                connectionFailedSignal.emit(result);
        }
        return result;
}

Result<uint32_t> RtmpSession::createStream(unsigned int timeoutMs) {
        if (!_chunk.isValid()) return makeError<uint32_t>(Error::Invalid);
        double txnId = nextTransactionId();
        PendingTransaction *txn = new PendingTransaction();
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }
        Error sendErr = sendCommand(kCsidCommand, 0, "createStream", txnId, { Amf0Value() });
        if (sendErr.isError()) {
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return makeError<uint32_t>(sendErr);
        }
        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isError()) return makeError<uint32_t>(result);
        // The reply's `info` slot (vals[3]) carries the new stream id as a number.
        uint32_t streamId = 0;
        if (completed.info.isNumber()) {
                streamId = static_cast<uint32_t>(completed.info.asNumber());
        }
        if (streamId != 0) streamCreatedSignal.emit(streamId);
        return makeResult(streamId);
}

Error RtmpSession::publish(uint32_t streamId, const String &streamKey,
                           const String &mode, unsigned int timeoutMs) {
        if (!_chunk.isValid()) return Error::Invalid;
        double txnId = nextTransactionId();
        PendingTransaction *txn = new PendingTransaction();
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());                 // null command-object
        args.pushToBack(Amf0Value(streamKey));        // stream name
        args.pushToBack(Amf0Value(mode));             // "live" / "record" / "append"
        Error sendErr = sendCommand(8, streamId, "publish", txnId, args);
        if (sendErr.isError()) {
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return sendErr;
        }
        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isOk()) publishStartedSignal.emit(streamId);
        return result;
}

Error RtmpSession::play(uint32_t streamId, const String &streamKey,
                        double start, double duration, unsigned int timeoutMs) {
        if (!_chunk.isValid()) return Error::Invalid;
        double txnId = nextTransactionId();
        PendingTransaction *txn = new PendingTransaction();
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());                 // null command-object
        args.pushToBack(Amf0Value(streamKey));
        args.pushToBack(Amf0Value(start));
        args.pushToBack(Amf0Value(duration));
        Error sendErr = sendCommand(8, streamId, "play", txnId, args);
        if (sendErr.isError()) {
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return sendErr;
        }
        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isOk()) playStartedSignal.emit(streamId);
        return result;
}

Error RtmpSession::deleteStream(uint32_t streamId) {
        // Fire-and-forget — no _result expected from most servers.
        if (!_chunk.isValid()) return Error::Invalid;
        Amf0Value::List args;
        args.pushToBack(Amf0Value());                 // null command-object
        args.pushToBack(Amf0Value(static_cast<double>(streamId)));
        return sendCommand(kCsidCommand, 0, "deleteStream", 0.0, args);
}

Error RtmpSession::releaseStream(const String &streamKey) {
        if (!_chunk.isValid()) return Error::Invalid;
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        return sendCommand(kCsidCommand, 0, "releaseStream", 0.0, args);
}

Error RtmpSession::fcPublish(const String &streamKey) {
        if (!_chunk.isValid()) return Error::Invalid;
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        return sendCommand(kCsidCommand, 0, "FCPublish", 0.0, args);
}

Error RtmpSession::fcUnpublish(const String &streamKey) {
        if (!_chunk.isValid()) return Error::Invalid;
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        return sendCommand(kCsidCommand, 0, "FCUnpublish", 0.0, args);
}

Error RtmpSession::fcSubscribe(const String &streamKey) {
        if (!_chunk.isValid()) return Error::Invalid;
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        return sendCommand(kCsidCommand, 0, "FCSubscribe", 0.0, args);
}

// ---------------------------------------------------------------------------
// Raw access
// ---------------------------------------------------------------------------

Error RtmpSession::sendMessage(const RtmpMessage &m) {
        if (!_chunk.isValid()) return Error::Invalid;
        return _chunk->writeMessage(m);
}

Result<RtmpMessage> RtmpSession::readMessage(unsigned int timeoutMs) {
        if (!_chunk.isValid()) return makeError<RtmpMessage>(Error::Invalid);
        Result<RtmpMessage> got = _chunk->readMessage(timeoutMs);
        if (got.second().isError()) return got;
        switch (got.first().type) {
                case RtmpMessage::CommandMessageAmf0:
                        handleInboundCommand(got.first());
                        break;
                case RtmpMessage::UserControl:
                        handleInboundUserControl(got.first());
                        break;
                case RtmpMessage::DataMessageAmf0:
                        handleInboundData(got.first());
                        break;
                case RtmpMessage::AudioMessage:
                        audioMessageReceivedSignal.emit(got.first());
                        break;
                case RtmpMessage::VideoMessage:
                        videoMessageReceivedSignal.emit(got.first());
                        break;
                default:
                        break;
        }
        return got;
}

PROMEKI_NAMESPACE_END
