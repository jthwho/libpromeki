/**
 * @file      rtmpsession.cpp
 * @copyright Jason Howard. All rights reserved.
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
        if (_device == nullptr) {
                promekiWarn("RtmpSession::performHandshake: no device attached");
                return Error::Invalid;
        }

        ElapsedTimer timer;
        timer.start();
        for (;;) {
                Buffer pending = _handshake.pendingOutput();
                while (pending.size() > 0) {
                        int64_t n = _device->write(pending.data(), static_cast<int64_t>(pending.size()));
                        if (n < 0) {
                                promekiWarn("RtmpSession::performHandshake: write failed "
                                            "(state=%d, wanted=%zu, returned=%lld)",
                                            static_cast<int>(_handshake.state()),
                                            pending.size(),
                                            static_cast<long long>(n));
                                _handshake.markPeerClosed();
                                return Error::IOError;
                        }
                        if (n == 0) {
                                promekiWarn("RtmpSession::performHandshake: write returned 0 "
                                            "(peer half-closed during handshake, state=%d)",
                                            static_cast<int>(_handshake.state()));
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
                if (_handshake.state() == RtmpHandshake::Failed) {
                        Error e = _handshake.lastError();
                        promekiWarn("RtmpSession::performHandshake: handshake failed: %s",
                                    e.desc().cstr());
                        return e;
                }

                int64_t elapsed = timer.elapsedUs() / 1000;
                if (timeoutMs != 0 && elapsed >= static_cast<int64_t>(timeoutMs)) {
                        promekiWarn("RtmpSession::performHandshake: timed out after %u ms "
                                    "(state=%d, handshake never reached Done)",
                                    timeoutMs, static_cast<int>(_handshake.state()));
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
                        promekiWarn("RtmpSession::performHandshake: read failed "
                                    "(state=%d, returned=%lld)",
                                    static_cast<int>(_handshake.state()),
                                    static_cast<long long>(n));
                        _handshake.markPeerClosed();
                        return Error::IOError;
                }
                if (n == 0) {
                        // recv() returning zero on a connected TCP
                        // socket is EOF — the peer has half-closed.
                        // Continuing would spin (POLLHUP is sticky, so
                        // the next waitForReadyRead returns instantly).
                        promekiWarn("RtmpSession::performHandshake: peer EOF mid-handshake "
                                    "(state=%d)", static_cast<int>(_handshake.state()));
                        _handshake.markPeerClosed();
                        return Error::IOError;
                }
                Buffer wrap = Buffer::wrapHost(buf, static_cast<size_t>(n));
                wrap.setSize(static_cast<size_t>(n));
                Error err = _handshake.feed(BufferView(wrap, 0, static_cast<size_t>(n)));
                if (err.isError()) {
                        promekiWarn("RtmpSession::performHandshake: feed(%lld bytes) failed: %s "
                                    "(state=%d)", static_cast<long long>(n),
                                    err.desc().cstr(), static_cast<int>(_handshake.state()));
                        return err;
                }
        }
}

// ---------------------------------------------------------------------------
// Command serialization helper
// ---------------------------------------------------------------------------

Error RtmpSession::sendCommand(uint32_t csid, uint32_t msid,
                               const String &command, double txnId,
                               const Amf0Value::List &args) {
        if (_chunk.isValid() == false) {
                promekiWarn("RtmpSession::sendCommand(%s, txn=%g): chunk stream not attached",
                            command.cstr(), txnId);
                return Error::Invalid;
        }
        Buffer    payload;
        Amf0Writer w(payload);
        if (Error e = w.writeString(command); e.isError()) {
                promekiWarn("RtmpSession::sendCommand(%s): AMF0 encode of name failed: %s",
                            command.cstr(), e.desc().cstr());
                return e;
        }
        if (Error e = w.writeNumber(txnId);   e.isError()) {
                promekiWarn("RtmpSession::sendCommand(%s, txn=%g): AMF0 encode of txnId failed: %s",
                            command.cstr(), txnId, e.desc().cstr());
                return e;
        }
        size_t argIdx = 0;
        for (const Amf0Value &v : args) {
                if (Error e = w.writeValue(v); e.isError()) {
                        promekiWarn("RtmpSession::sendCommand(%s, txn=%g): AMF0 encode of "
                                    "arg[%zu] failed: %s",
                                    command.cstr(), txnId, argIdx, e.desc().cstr());
                        return e;
                }
                ++argIdx;
        }
        RtmpMessage msg;
        msg.type = RtmpMessage::CommandMessageAmf0;
        msg.streamId = msid;
        msg.chunkStreamId = csid;
        msg.timestamp = 0;
        msg.payload = payload;
        promekiDebug("RtmpSession::sendCommand: %s txn=%g msid=%u csid=%u payload=%zu",
                     command.cstr(), txnId, msid, csid, payload.size());
        Error err = _chunk->writeMessage(msg);
        if (err.isError()) {
                promekiWarn("RtmpSession::sendCommand(%s, txn=%g, msid=%u): "
                            "chunk write failed: %s",
                            command.cstr(), txnId, msid, err.desc().cstr());
        }
        return err;
}

// ---------------------------------------------------------------------------
// Transaction tracking
// ---------------------------------------------------------------------------

Error RtmpSession::pumpUntilTransaction(double txnId, unsigned int timeoutMs,
                                        PendingTransaction *outTxn) {
        if (_chunk.isValid() == false) {
                promekiWarn("RtmpSession::pumpUntilTransaction(txn=%g): chunk stream not attached",
                            txnId);
                return Error::Invalid;
        }
        ElapsedTimer timer;
        timer.start();
        String txnCommandName;
        uint32_t txnExpectedMsid = 0;
        {
                Mutex::Locker lk(_txnMutex);
                auto it = _pending.find(txnId);
                if (it != _pending.end() && it->second != nullptr) {
                        txnCommandName = it->second->commandName;
                        txnExpectedMsid = it->second->expectedMsid;
                }
        }
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
                        promekiWarn("RtmpSession::pumpUntilTransaction: timed out after %u ms "
                                    "waiting for reply to %s (txn=%g, expectedMsid=%u)",
                                    timeoutMs,
                                    txnCommandName.isEmpty() ? "(unknown)" : txnCommandName.cstr(),
                                    txnId, txnExpectedMsid);
                        return Error::Timeout;
                }
                unsigned int rem = (timeoutMs == 0) ? 250 : static_cast<unsigned int>(timeoutMs - elapsed);
                Result<RtmpMessage> got = _chunk->readMessage(rem);
                if (got.second().isError()) {
                        if (got.second() == Error::Timeout) continue;
                        promekiWarn("RtmpSession::pumpUntilTransaction: read failed while waiting "
                                    "for %s (txn=%g): %s",
                                    txnCommandName.isEmpty() ? "(unknown)" : txnCommandName.cstr(),
                                    txnId, got.second().desc().cstr());
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
                if (dispatchErr.isError()) {
                        promekiWarn("RtmpSession::pumpUntilTransaction: dispatch failed while "
                                    "waiting for %s (txn=%g, inbound type=%d): %s",
                                    txnCommandName.isEmpty() ? "(unknown)" : txnCommandName.cstr(),
                                    txnId, static_cast<int>(got.first().type),
                                    dispatchErr.desc().cstr());
                        return dispatchErr;
                }
        }
}

// ---------------------------------------------------------------------------
// Inbound command (AMF0): _result / _error / onStatus / onBWDone / _checkbw
// ---------------------------------------------------------------------------

Error RtmpSession::handleInboundCommand(const RtmpMessage &msg) {
        Result<Amf0Value::List> parsed = Amf0Reader::read(
                static_cast<const uint8_t *>(msg.payload.data()), msg.payload.size());
        if (parsed.second().isError()) {
                promekiWarn("RtmpSession::handleInboundCommand: AMF0 parse failed "
                            "(payload=%zu bytes, msid=%u): %s",
                            msg.payload.size(), msg.streamId,
                            parsed.second().desc().cstr());
                return parsed.second();
        }

        const Amf0Value::List &vals = parsed.first();
        if (vals.size() < 2) {
                promekiWarn("RtmpSession::handleInboundCommand: short AMF0 command "
                            "(values=%zu, msid=%u)", vals.size(), msg.streamId);
                return Error::CorruptData;
        }
        if (!vals[0].isString() || !vals[1].isNumber()) {
                promekiWarn("RtmpSession::handleInboundCommand: malformed AMF0 command "
                            "(vals[0].type=%d, vals[1].type=%d, msid=%u)",
                            static_cast<int>(vals[0].type()),
                            static_cast<int>(vals[1].type()),
                            msg.streamId);
                return Error::CorruptData;
        }

        String   command = vals[0].asString();
        double   txnId   = vals[1].asNumber();
        promekiDebug("RtmpSession: inbound command=%s txn=%g msid=%u vals=%zu",
                     command.cstr(), txnId, msg.streamId, vals.size());

        if (command == "_result" || command == "_error") {
                Mutex::Locker lk(_txnMutex);
                auto it = _pending.find(txnId);
                if (it == _pending.end() || it->second == nullptr) {
                        promekiWarn("RtmpSession: unmatched %s for txn=%g msid=%u "
                                    "(no pending transaction registered)",
                                    command.cstr(), txnId, msg.streamId);
                        return Error::Ok;
                }
                PendingTransaction *txn = it->second;
                // Find the info object — by convention the 4th value (after command, txnId, cmdObj).
                if (vals.size() >= 3) txn->commandObject = vals[2];
                if (vals.size() >= 4) txn->info = vals[3];
                if (command == "_error") {
                        txn->result = Error::ProtocolError;
                        String code;
                        String level;
                        String description;
                        if (vals.size() >= 4 && vals[3].isObject()) {
                                const Amf0Value *codeField = vals[3].find("code");
                                const Amf0Value *levelField = vals[3].find("level");
                                const Amf0Value *descField  = vals[3].find("description");
                                if (codeField  != nullptr && codeField->isString())  code = codeField->asString();
                                if (levelField != nullptr && levelField->isString()) level = levelField->asString();
                                if (descField  != nullptr && descField->isString())  description = descField->asString();
                                if (!code.isEmpty()) {
                                        txn->result = onStatusToError(code);
                                        if (txn->result.isOk()) txn->result = Error::ProtocolError;
                                }
                        }
                        promekiWarn("RtmpSession: _error for %s (txn=%g, msid=%u) "
                                    "code='%s' level='%s' description='%s' -> %s",
                                    txn->commandName.isEmpty() ? "(unknown)" : txn->commandName.cstr(),
                                    txnId, msg.streamId,
                                    code.cstr(), level.cstr(), description.cstr(),
                                    txn->result.desc().cstr());
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

                String code;
                String level;
                String description;
                if (info.isObject()) {
                        const Amf0Value *codeField  = info.find("code");
                        const Amf0Value *levelField = info.find("level");
                        const Amf0Value *descField  = info.find("description");
                        if (codeField  != nullptr && codeField->isString())  code = codeField->asString();
                        if (levelField != nullptr && levelField->isString()) level = levelField->asString();
                        if (descField  != nullptr && descField->isString())  description = descField->asString();
                }
                if (code.isEmpty()) {
                        promekiWarn("RtmpSession: onStatus without `code` field "
                                    "(txn=%g, msid=%u) — cannot correlate",
                                    txnId, msg.streamId);
                        return Error::Ok;
                }

                Error mapped = onStatusToError(code);

                // Per RTMP §7.2.2 onStatus carries txnId=0 — most
                // FMS-clone servers (YouTube, Twitch, nginx-rtmp) emit
                // it that way.  Our publish() / play() register the
                // pending transaction under a *non-zero* txnId, so we
                // can't lookup by txnId alone.  Correlation strategy:
                //   1. If the server echoed a non-zero txnId, try a
                //      direct lookup (covers servers that echo for
                //      compatibility).
                //   2. Otherwise scan _pending for an outstanding
                //      transaction whose `expectedMsid` matches the
                //      inbound message-stream-id — that's the publish
                //      / play we issued on that stream.
                Mutex::Locker lk(_txnMutex);
                auto matched = _pending.end();
                if (txnId != 0.0) {
                        matched = _pending.find(txnId);
                        if (matched != _pending.end()
                            && (matched->second == nullptr || matched->second->completed)) {
                                matched = _pending.end();
                        }
                }
                if (matched == _pending.end() && msg.streamId != 0) {
                        for (auto it = _pending.begin(); it != _pending.end(); ++it) {
                                if (it->second == nullptr)            continue;
                                if (it->second->completed)            continue;
                                if (it->second->expectedMsid != msg.streamId) continue;
                                matched = it;
                                break;
                        }
                }
                if (matched != _pending.end() && matched->second != nullptr) {
                        matched->second->info = info;
                        matched->second->result = mapped;
                        matched->second->completed = true;
                        if (mapped.isError()) {
                                promekiWarn("RtmpSession: onStatus for %s (txn=%g, msid=%u) "
                                            "code='%s' level='%s' description='%s' -> %s",
                                            matched->second->commandName.isEmpty()
                                                ? "(unknown)"
                                                : matched->second->commandName.cstr(),
                                            matched->first, msg.streamId,
                                            code.cstr(), level.cstr(), description.cstr(),
                                            mapped.desc().cstr());
                        } else {
                                promekiDebug("RtmpSession: onStatus matched %s (txn=%g, msid=%u) "
                                             "code=%s",
                                             matched->second->commandName.isEmpty()
                                                 ? "(unknown)"
                                                 : matched->second->commandName.cstr(),
                                             matched->first, msg.streamId, code.cstr());
                        }
                } else {
                        // No pending publish/play matches.  This is
                        // either a server-initiated status (e.g.
                        // NetStream.Play.PublishNotify) or, more often,
                        // the bug we just guarded against — a missing
                        // expectedMsid registration.
                        promekiWarn("RtmpSession: unmatched onStatus "
                                    "code='%s' level='%s' description='%s' "
                                    "(inbound txn=%g, msid=%u)",
                                    code.cstr(), level.cstr(), description.cstr(),
                                    txnId, msg.streamId);
                }
                return Error::Ok;
        }

        if (command == "onBWDone" || command == "_checkbw") {
                // No-op: bandwidth-check is a courtesy ping; we ignore it.
                return Error::Ok;
        }

        // Unhandled command — keep going.
        promekiWarn("RtmpSession: unhandled inbound command '%s' txn=%g msid=%u "
                    "(no handler — ignoring)",
                    command.cstr(), txnId, msg.streamId);
        return Error::Ok;
}

Error RtmpSession::handleInboundUserControl(const RtmpMessage &msg) {
        if (msg.payload.size() < 2) {
                promekiWarn("RtmpSession::handleInboundUserControl: short payload "
                            "(%zu bytes; need >= 2)", msg.payload.size());
                return Error::CorruptData;
        }
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
        if (parsed.second().isError()) {
                promekiWarn("RtmpSession::handleInboundData: AMF0 parse failed "
                            "(payload=%zu bytes, msid=%u): %s",
                            msg.payload.size(), msg.streamId,
                            parsed.second().desc().cstr());
                return Error::Ok;  // best-effort
        }
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
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::connect: chunk stream not attached");
                return Error::Invalid;
        }
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
        txn->commandName = "connect";
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }

        promekiDebug("RtmpSession::connect: txn=%g app='%s' tcUrl='%s'",
                     txnId, opts.app.cstr(), opts.tcUrl.cstr());
        Error sendErr = sendCommand(kCsidCommand, 0, "connect", txnId, { cmdObj });
        if (sendErr.isError()) {
                promekiWarn("RtmpSession::connect: send failed (txn=%g, app='%s'): %s",
                            txnId, opts.app.cstr(), sendErr.desc().cstr());
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
        } else {
                promekiWarn("RtmpSession::connect: failed (app='%s', tcUrl='%s'): %s",
                            opts.app.cstr(), opts.tcUrl.cstr(), result.desc().cstr());
                if (result == Error::Timeout) {
                        connectionFailedSignal.emit(result);
                }
        }
        return result;
}

Result<uint32_t> RtmpSession::createStream(unsigned int timeoutMs) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::createStream: chunk stream not attached");
                return makeError<uint32_t>(Error::Invalid);
        }
        double txnId = nextTransactionId();
        PendingTransaction *txn = new PendingTransaction();
        txn->commandName = "createStream";
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }
        Error sendErr = sendCommand(kCsidCommand, 0, "createStream", txnId, { Amf0Value() });
        if (sendErr.isError()) {
                promekiWarn("RtmpSession::createStream: send failed (txn=%g): %s",
                            txnId, sendErr.desc().cstr());
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return makeError<uint32_t>(sendErr);
        }
        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isError()) {
                promekiWarn("RtmpSession::createStream: failed: %s", result.desc().cstr());
                return makeError<uint32_t>(result);
        }
        // The reply's `info` slot (vals[3]) carries the new stream id as a number.
        uint32_t streamId = 0;
        if (completed.info.isNumber()) {
                streamId = static_cast<uint32_t>(completed.info.asNumber());
        } else {
                promekiWarn("RtmpSession::createStream: reply info is not a number "
                            "(type=%d) — server may have returned an unexpected shape",
                            static_cast<int>(completed.info.type()));
        }
        if (streamId == 0) {
                promekiWarn("RtmpSession::createStream: server returned streamId=0 — "
                            "publish/play will fail to correlate replies");
        } else {
                streamCreatedSignal.emit(streamId);
        }
        return makeResult(streamId);
}

Error RtmpSession::publish(uint32_t streamId, const String &streamKey,
                           const String &mode, unsigned int timeoutMs) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::publish: chunk stream not attached "
                            "(streamId=%u, key='%s')", streamId, streamKey.cstr());
                return Error::Invalid;
        }
        double txnId = nextTransactionId();
        PendingTransaction *txn = new PendingTransaction();
        txn->commandName = "publish";
        txn->expectedMsid = streamId;
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());                 // null command-object
        args.pushToBack(Amf0Value(streamKey));        // stream name
        args.pushToBack(Amf0Value(mode));             // "live" / "record" / "append"
        promekiDebug("RtmpSession::publish: txn=%g msid=%u key='%s' mode='%s'",
                     txnId, streamId, streamKey.cstr(), mode.cstr());
        Error sendErr = sendCommand(8, streamId, "publish", txnId, args);
        if (sendErr.isError()) {
                promekiWarn("RtmpSession::publish: send failed (txn=%g, msid=%u, key='%s'): %s",
                            txnId, streamId, streamKey.cstr(), sendErr.desc().cstr());
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return sendErr;
        }
        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isOk()) {
                publishStartedSignal.emit(streamId);
        } else {
                promekiWarn("RtmpSession::publish: failed (msid=%u, key='%s', mode='%s'): %s",
                            streamId, streamKey.cstr(), mode.cstr(), result.desc().cstr());
        }
        return result;
}

Error RtmpSession::play(uint32_t streamId, const String &streamKey,
                        double start, double duration, unsigned int timeoutMs) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::play: chunk stream not attached "
                            "(streamId=%u, key='%s')", streamId, streamKey.cstr());
                return Error::Invalid;
        }
        double txnId = nextTransactionId();
        PendingTransaction *txn = new PendingTransaction();
        txn->commandName = "play";
        txn->expectedMsid = streamId;
        {
                Mutex::Locker lk(_txnMutex);
                _pending.insert(txnId, txn);
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());                 // null command-object
        args.pushToBack(Amf0Value(streamKey));
        args.pushToBack(Amf0Value(start));
        args.pushToBack(Amf0Value(duration));
        promekiDebug("RtmpSession::play: txn=%g msid=%u key='%s' start=%g duration=%g",
                     txnId, streamId, streamKey.cstr(), start, duration);
        Error sendErr = sendCommand(8, streamId, "play", txnId, args);
        if (sendErr.isError()) {
                promekiWarn("RtmpSession::play: send failed (txn=%g, msid=%u, key='%s'): %s",
                            txnId, streamId, streamKey.cstr(), sendErr.desc().cstr());
                Mutex::Locker lk(_txnMutex);
                delete txn;
                _pending.remove(txnId);
                return sendErr;
        }
        PendingTransaction completed;
        Error result = pumpUntilTransaction(txnId, timeoutMs, &completed);
        if (result.isOk()) {
                playStartedSignal.emit(streamId);
        } else {
                promekiWarn("RtmpSession::play: failed (msid=%u, key='%s'): %s",
                            streamId, streamKey.cstr(), result.desc().cstr());
        }
        return result;
}

Error RtmpSession::deleteStream(uint32_t streamId) {
        // Fire-and-forget — no _result expected from most servers.
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::deleteStream: chunk stream not attached "
                            "(streamId=%u)", streamId);
                return Error::Invalid;
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());                 // null command-object
        args.pushToBack(Amf0Value(static_cast<double>(streamId)));
        Error err = sendCommand(kCsidCommand, 0, "deleteStream", 0.0, args);
        if (err.isError()) {
                promekiWarn("RtmpSession::deleteStream: send failed (streamId=%u): %s",
                            streamId, err.desc().cstr());
        }
        return err;
}

Error RtmpSession::releaseStream(const String &streamKey) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::releaseStream: chunk stream not attached "
                            "(key='%s')", streamKey.cstr());
                return Error::Invalid;
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        Error err = sendCommand(kCsidCommand, 0, "releaseStream", 0.0, args);
        if (err.isError()) {
                promekiWarn("RtmpSession::releaseStream: send failed (key='%s'): %s",
                            streamKey.cstr(), err.desc().cstr());
        }
        return err;
}

Error RtmpSession::fcPublish(const String &streamKey) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::fcPublish: chunk stream not attached "
                            "(key='%s')", streamKey.cstr());
                return Error::Invalid;
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        Error err = sendCommand(kCsidCommand, 0, "FCPublish", 0.0, args);
        if (err.isError()) {
                promekiWarn("RtmpSession::fcPublish: send failed (key='%s'): %s",
                            streamKey.cstr(), err.desc().cstr());
        }
        return err;
}

Error RtmpSession::fcUnpublish(const String &streamKey) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::fcUnpublish: chunk stream not attached "
                            "(key='%s')", streamKey.cstr());
                return Error::Invalid;
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        Error err = sendCommand(kCsidCommand, 0, "FCUnpublish", 0.0, args);
        if (err.isError()) {
                promekiWarn("RtmpSession::fcUnpublish: send failed (key='%s'): %s",
                            streamKey.cstr(), err.desc().cstr());
        }
        return err;
}

Error RtmpSession::fcSubscribe(const String &streamKey) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::fcSubscribe: chunk stream not attached "
                            "(key='%s')", streamKey.cstr());
                return Error::Invalid;
        }
        Amf0Value::List args;
        args.pushToBack(Amf0Value());
        args.pushToBack(Amf0Value(streamKey));
        Error err = sendCommand(kCsidCommand, 0, "FCSubscribe", 0.0, args);
        if (err.isError()) {
                promekiWarn("RtmpSession::fcSubscribe: send failed (key='%s'): %s",
                            streamKey.cstr(), err.desc().cstr());
        }
        return err;
}

// ---------------------------------------------------------------------------
// Raw access
// ---------------------------------------------------------------------------

Error RtmpSession::sendMessage(const RtmpMessage &m) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::sendMessage: chunk stream not attached "
                            "(type=%d, msid=%u, size=%zu)",
                            static_cast<int>(m.type), m.streamId, m.payload.size());
                return Error::Invalid;
        }
        Error err = _chunk->writeMessage(m);
        if (err.isError()) {
                promekiWarn("RtmpSession::sendMessage: writeMessage failed "
                            "(type=%d, msid=%u, size=%zu): %s",
                            static_cast<int>(m.type), m.streamId, m.payload.size(),
                            err.desc().cstr());
        }
        return err;
}

Result<RtmpMessage> RtmpSession::readMessage(unsigned int timeoutMs) {
        if (!_chunk.isValid()) {
                promekiWarn("RtmpSession::readMessage: chunk stream not attached");
                return makeError<RtmpMessage>(Error::Invalid);
        }
        Result<RtmpMessage> got = _chunk->readMessage(timeoutMs);
        if (got.second().isError()) {
                if (got.second() != Error::Timeout) {
                        promekiWarn("RtmpSession::readMessage: chunk read failed: %s",
                                    got.second().desc().cstr());
                }
                return got;
        }
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
