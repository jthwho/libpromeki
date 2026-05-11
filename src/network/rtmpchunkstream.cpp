/**
 * @file      rtmpchunkstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtmpchunkstream.h>

#include <cstring>

#include <promeki/elapsedtimer.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtmpChunkStream);

namespace {

constexpr uint32_t kExtendedTimestampMarker = 0xFFFFFFu;
constexpr uint32_t kProtocolControlMsid     = 0;
constexpr uint32_t kProtocolControlCsid     = 2;

void writeBE24(uint8_t *p, uint32_t v) {
        p[0] = static_cast<uint8_t>((v >> 16) & 0xff);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
        p[2] = static_cast<uint8_t>(v & 0xff);
}

void writeBE32(uint8_t *p, uint32_t v) {
        p[0] = static_cast<uint8_t>((v >> 24) & 0xff);
        p[1] = static_cast<uint8_t>((v >> 16) & 0xff);
        p[2] = static_cast<uint8_t>((v >> 8) & 0xff);
        p[3] = static_cast<uint8_t>(v & 0xff);
}

void writeLE32(uint8_t *p, uint32_t v) {
        p[0] = static_cast<uint8_t>(v & 0xff);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
        p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

uint32_t readBE24(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 16)
             | (static_cast<uint32_t>(p[1]) << 8)
             | static_cast<uint32_t>(p[2]);
}

uint32_t readBE32(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24)
             | (static_cast<uint32_t>(p[1]) << 16)
             | (static_cast<uint32_t>(p[2]) << 8)
             | static_cast<uint32_t>(p[3]);
}

uint32_t readLE32(const uint8_t *p) {
        return static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
}

/**
 * @brief Encodes the chunk basic header into @p out.
 *
 * Returns the number of bytes written: 1 / 2 / 3 depending on CS-id
 * magnitude.  CS-ids in [2, 63] fit a 6-bit field; [64, 319] use the
 * 8-bit form with byte0=fmt<<6|0; [320, 65599] use the 16-bit form
 * with byte0=fmt<<6|1.
 */
size_t encodeBasicHeader(uint8_t *out, uint8_t fmt, uint32_t csid) {
        if (csid >= 64 && csid <= 319) {
                out[0] = static_cast<uint8_t>((fmt << 6) | 0);
                out[1] = static_cast<uint8_t>(csid - 64);
                return 2;
        } else if (csid >= 320 && csid <= 65599) {
                uint32_t v = csid - 64;
                out[0] = static_cast<uint8_t>((fmt << 6) | 1);
                out[1] = static_cast<uint8_t>(v & 0xff);
                out[2] = static_cast<uint8_t>((v >> 8) & 0xff);
                return 3;
        }
        // 2 ≤ csid ≤ 63 — single-byte form (csid 0/1 are escape markers, not usable directly).
        out[0] = static_cast<uint8_t>((fmt << 6) | (csid & 0x3f));
        return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RtmpChunkStream::RtmpChunkStream(IODevice *device, ObjectBase *parent)
    : ObjectBase(parent), _device(device) {}

RtmpChunkStream::~RtmpChunkStream() = default;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

Error RtmpChunkStream::setLocalChunkSize(int bytes) {
        if (bytes < MinChunkSize || bytes > MaxChunkSize) return Error::InvalidArgument;
        Error err = sendSetChunkSize(bytes);
        if (err.isError()) return err;
        _localChunkSize.setValue(bytes);
        promekiDebug("RtmpChunkStream: local chunk size set to %d", bytes);
        return Error::Ok;
}

Error RtmpChunkStream::setLocalWindowAckSize(int bytes) {
        if (bytes <= 0) return Error::InvalidArgument;
        Error err = sendWindowAckSize(bytes);
        if (err.isError()) return err;
        _localWindowAckSize.setValue(bytes);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Default CS-id mapping
// ---------------------------------------------------------------------------

int RtmpChunkStream::defaultCsidForType(RtmpMessage::Type type) const {
        switch (type) {
                case RtmpMessage::SetChunkSize:
                case RtmpMessage::AbortMessage:
                case RtmpMessage::Acknowledgement:
                case RtmpMessage::UserControl:
                case RtmpMessage::WindowAckSize:
                case RtmpMessage::SetPeerBandwidth:
                        return 2;
                case RtmpMessage::AudioMessage:
                        return 4;
                case RtmpMessage::VideoMessage:
                        return 6;
                case RtmpMessage::DataMessageAmf0:
                case RtmpMessage::DataMessageAmf3:
                case RtmpMessage::SharedObjectAmf0:
                case RtmpMessage::SharedObjectAmf3:
                case RtmpMessage::CommandMessageAmf0:
                case RtmpMessage::CommandMessageAmf3:
                case RtmpMessage::AggregateMessage:
                        return 3;
        }
        return 3;
}

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------

Error RtmpChunkStream::writeBytes(const uint8_t *data, size_t len) {
        if (_device == nullptr) {
                promekiWarn("RtmpChunkStream::writeBytes: no device attached (len=%zu)", len);
                return Error::Invalid;
        }
        size_t off = 0;
        while (off < len) {
                int64_t n = _device->write(data + off, static_cast<int64_t>(len - off));
                if (n < 0) {
                        promekiWarn("RtmpChunkStream::writeBytes: write failed "
                                    "(wanted=%zu, sent=%zu, returned=%lld)",
                                    len, off, static_cast<long long>(n));
                        return Error::IOError;
                }
                if (n == 0) {
                        promekiWarn("RtmpChunkStream::writeBytes: write returned 0 "
                                    "(peer half-closed; wanted=%zu, sent=%zu)",
                                    len, off);
                        return Error::IOError;
                }
                off += static_cast<size_t>(n);
        }
        _bytesSent.setValue(_bytesSent.value() + static_cast<int64_t>(len));
        return Error::Ok;
}

Error RtmpChunkStream::readBytesExact(uint8_t *data, size_t len, unsigned int timeoutMs) {
        if (_device == nullptr) {
                promekiWarn("RtmpChunkStream::readBytesExact: no device attached (len=%zu)", len);
                return Error::Invalid;
        }
        size_t off = 0;
        ElapsedTimer timer;
        timer.start();
        while (off < len) {
                unsigned int remaining = 0;
                if (timeoutMs != 0) {
                        int64_t elapsedMs = timer.elapsedUs() / 1000;
                        if (elapsedMs >= static_cast<int64_t>(timeoutMs)) return Error::Timeout;
                        remaining = timeoutMs - static_cast<unsigned int>(elapsedMs);
                }
                // Wait until the device reports bytes available (or
                // the timeout elapses).  Sockets return false
                // immediately when no bytes are present.
                if (_device->bytesAvailable() == 0) {
                        bool ready = _device->waitForReadyRead(remaining);
                        if (!ready) {
                                if (timeoutMs == 0) {
                                        promekiWarn("RtmpChunkStream::readBytesExact: "
                                                    "waitForReadyRead failed (no timeout, "
                                                    "wanted=%zu, got=%zu) — treating as IOError",
                                                    len, off);
                                        return Error::IOError;
                                }
                                return Error::Timeout;
                        }
                }
                int64_t n = _device->read(data + off, static_cast<int64_t>(len - off));
                if (n < 0) {
                        promekiWarn("RtmpChunkStream::readBytesExact: read failed "
                                    "(wanted=%zu, got=%zu, returned=%lld)",
                                    len, off, static_cast<long long>(n));
                        return Error::IOError;
                }
                if (n == 0) {
                        // recv() returning zero on a TCP socket is EOF
                        // — the peer half-closed before sending the
                        // bytes we needed.  Treat as IOError; looping
                        // would spin since POLLHUP makes the next
                        // waitForReadyRead return instantly.
                        promekiWarn("RtmpChunkStream::readBytesExact: peer EOF "
                                    "(wanted=%zu, got=%zu)", len, off);
                        return Error::IOError;
                }
                off += static_cast<size_t>(n);
                _bytesReceived.setValue(_bytesReceived.value() + n);
        }

        // Window-Ack: emit Acknowledgement once we've crossed the
        // peer's window-ack threshold since the last Ack.
        int peerWindow = _peerWindowAckSize.value();
        if (peerWindow > 0) {
                int64_t total = _bytesReceived.value();
                int64_t since = total - _lastAckBytesSent.value();
                if (since >= peerWindow) {
                        sendAck(static_cast<uint32_t>(total & 0xffffffffu));
                        _lastAckBytesSent.setValue(total);
                }
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Protocol-control senders (cs-id 2, msid 0)
// ---------------------------------------------------------------------------

Error RtmpChunkStream::sendSetChunkSize(int bytes) {
        uint8_t  payload[4];
        writeBE32(payload, static_cast<uint32_t>(bytes) & 0x7fffffffu);  // MSB must be 0 per spec.

        RtmpMessage msg;
        msg.type = RtmpMessage::SetChunkSize;
        msg.streamId = 0;
        msg.chunkStreamId = kProtocolControlCsid;
        msg.timestamp = 0;
        msg.payload = Buffer(sizeof(payload));
        std::memcpy(msg.payload.data(), payload, sizeof(payload));
        msg.payload.setSize(sizeof(payload));
        return writeMessage(msg);
}

Error RtmpChunkStream::sendWindowAckSize(int bytes) {
        uint8_t payload[4];
        writeBE32(payload, static_cast<uint32_t>(bytes));

        RtmpMessage msg;
        msg.type = RtmpMessage::WindowAckSize;
        msg.streamId = 0;
        msg.chunkStreamId = kProtocolControlCsid;
        msg.timestamp = 0;
        msg.payload = Buffer(sizeof(payload));
        std::memcpy(msg.payload.data(), payload, sizeof(payload));
        msg.payload.setSize(sizeof(payload));
        return writeMessage(msg);
}

Error RtmpChunkStream::sendAck(uint32_t cumulative) {
        uint8_t payload[4];
        writeBE32(payload, cumulative);

        RtmpMessage msg;
        msg.type = RtmpMessage::Acknowledgement;
        msg.streamId = 0;
        msg.chunkStreamId = kProtocolControlCsid;
        msg.timestamp = 0;
        msg.payload = Buffer(sizeof(payload));
        std::memcpy(msg.payload.data(), payload, sizeof(payload));
        msg.payload.setSize(sizeof(payload));
        return writeMessage(msg);
}

// ---------------------------------------------------------------------------
// Inbound Protocol Control Message handling
// ---------------------------------------------------------------------------

void RtmpChunkStream::handleControl(const RtmpMessage &msg) {
        switch (msg.type) {
                case RtmpMessage::SetChunkSize: {
                        if (msg.payload.size() < 4) break;
                        uint32_t cs = readBE32(static_cast<const uint8_t *>(msg.payload.data())) & 0x7fffffffu;
                        if (cs < static_cast<uint32_t>(MinChunkSize)) cs = MinChunkSize;
                        if (cs > static_cast<uint32_t>(MaxChunkSize)) cs = MaxChunkSize;
                        _peerChunkSize.setValue(static_cast<int32_t>(cs));
                        peerChunkSizeChangedSignal.emit(static_cast<int>(cs));
                        promekiDebug("RtmpChunkStream: peer chunk size now %u", cs);
                        break;
                }
                case RtmpMessage::WindowAckSize: {
                        if (msg.payload.size() < 4) break;
                        uint32_t v = readBE32(static_cast<const uint8_t *>(msg.payload.data()));
                        _peerWindowAckSize.setValue(static_cast<int32_t>(v));
                        break;
                }
                case RtmpMessage::Acknowledgement: {
                        if (msg.payload.size() < 4) break;
                        uint32_t v = readBE32(static_cast<const uint8_t *>(msg.payload.data()));
                        peerAckSignal.emit(v);
                        break;
                }
                case RtmpMessage::SetPeerBandwidth: {
                        // SetPeerBandwidth carries (window-ack-size, limit-type).
                        // RTMP §5.4.5: a receiver of SetPeerBandwidth that has
                        // not yet sent a WindowAckSize should send one in
                        // response.  We surface the message to the session via
                        // controlMessageReceivedSignal and let the session
                        // decide; the chunk layer doesn't update local
                        // window-ack-size automatically.
                        break;
                }
                case RtmpMessage::AbortMessage: {
                        if (msg.payload.size() < 4) break;
                        uint32_t csid = readBE32(static_cast<const uint8_t *>(msg.payload.data()));
                        Mutex::Locker lk(_readMutex);
                        auto it = _readStates.find(csid);
                        if (it != _readStates.end()) {
                                it->second.reassembly = Buffer();
                                it->second.reassemblyBytes = 0;
                        }
                        promekiDebug("RtmpChunkStream: peer aborted message on cs-id %u", csid);
                        break;
                }
                default:
                        break;
        }
        controlMessageReceivedSignal.emit(msg);
}

// ---------------------------------------------------------------------------
// writeMessage
// ---------------------------------------------------------------------------

Error RtmpChunkStream::writeMessage(const RtmpMessage &msg) {
        if (_device == nullptr) {
                promekiWarn("RtmpChunkStream::writeMessage: no device attached "
                            "(type=%d, msid=%u, size=%zu)",
                            static_cast<int>(msg.type), msg.streamId, msg.payload.size());
                return Error::Invalid;
        }

        uint32_t csid = msg.chunkStreamId;
        if (csid == 0) csid = static_cast<uint32_t>(defaultCsidForType(msg.type));
        if (csid < 2) {
                promekiWarn("RtmpChunkStream::writeMessage: invalid csid %u "
                            "(type=%d, msid=%u) — csid must be >= 2",
                            csid, static_cast<int>(msg.type), msg.streamId);
                return Error::InvalidArgument;
        }
        if (csid > 65599) {
                promekiWarn("RtmpChunkStream::writeMessage: invalid csid %u "
                            "(type=%d, msid=%u) — csid must be <= 65599",
                            csid, static_cast<int>(msg.type), msg.streamId);
                return Error::InvalidArgument;
        }

        const uint8_t *body = static_cast<const uint8_t *>(msg.payload.data());
        const uint32_t bodyLen = static_cast<uint32_t>(msg.payload.size());
        const int      chunkSize = _localChunkSize.value();
        if (chunkSize <= 0) {
                promekiWarn("RtmpChunkStream::writeMessage: invalid local chunk size %d",
                            chunkSize);
                return Error::Invalid;
        }

        Mutex::Locker lk(_writeMutex);

        // Pick the most compressive header for the leading chunk.
        WriteState &ws = _writeStates[csid];
        uint8_t fmt = 0;
        uint32_t deltaToEmit = msg.timestamp;
        if (ws.established
            && msg.streamId == ws.messageStreamId
            && msg.timestamp >= ws.timestamp) {
                uint32_t delta = msg.timestamp - ws.timestamp;
                if (bodyLen == ws.messageLength
                    && static_cast<uint8_t>(msg.type) == ws.messageTypeId) {
                        if (delta == ws.delta && delta != 0) fmt = 3;  // fully repeated
                        else                                  fmt = 2;
                } else {
                        fmt = 1;
                }
                deltaToEmit = delta;
        }
        // Compute timestamp + extended-timestamp encoding for the
        // leading chunk's header.
        bool     useExt = false;
        uint32_t headerTs;
        uint32_t extTs = 0;
        if (fmt == 0) {
                headerTs = msg.timestamp;
                if (headerTs >= kExtendedTimestampMarker) {
                        extTs = headerTs;
                        headerTs = kExtendedTimestampMarker;
                        useExt = true;
                }
        } else if (fmt == 1 || fmt == 2) {
                headerTs = deltaToEmit;
                if (headerTs >= kExtendedTimestampMarker) {
                        extTs = headerTs;
                        headerTs = kExtendedTimestampMarker;
                        useExt = true;
                }
        } else {
                // fmt == 3 leading chunk inherits everything.
                headerTs = 0;
                useExt = (ws.delta >= kExtendedTimestampMarker)
                         || (ws.timestamp >= kExtendedTimestampMarker && !ws.established);
        }

        // ---- Emit the leading chunk header ----
        uint8_t hdrBuf[16];
        size_t  hdrLen = encodeBasicHeader(hdrBuf, fmt, csid);
        switch (fmt) {
                case 0:
                        writeBE24(hdrBuf + hdrLen, headerTs);
                        writeBE24(hdrBuf + hdrLen + 3, bodyLen);
                        hdrBuf[hdrLen + 6] = static_cast<uint8_t>(msg.type);
                        writeLE32(hdrBuf + hdrLen + 7, msg.streamId);
                        hdrLen += 11;
                        break;
                case 1:
                        writeBE24(hdrBuf + hdrLen, headerTs);
                        writeBE24(hdrBuf + hdrLen + 3, bodyLen);
                        hdrBuf[hdrLen + 6] = static_cast<uint8_t>(msg.type);
                        hdrLen += 7;
                        break;
                case 2:
                        writeBE24(hdrBuf + hdrLen, headerTs);
                        hdrLen += 3;
                        break;
                default:
                        break;
        }
        if (useExt) {
                writeBE32(hdrBuf + hdrLen, extTs);
                hdrLen += 4;
        }
        Error err = writeBytes(hdrBuf, hdrLen);
        if (err.isError()) return err;

        // ---- Payload + continuation chunks ----
        uint32_t sent = 0;
        const uint32_t firstChunk = (bodyLen < static_cast<uint32_t>(chunkSize))
                                    ? bodyLen
                                    : static_cast<uint32_t>(chunkSize);
        if (firstChunk > 0) {
                err = writeBytes(body, firstChunk);
                if (err.isError()) return err;
                sent = firstChunk;
        }
        while (sent < bodyLen) {
                uint8_t contHdr[8];
                size_t  contLen = encodeBasicHeader(contHdr, 3, csid);
                if (useExt) {
                        // ffmpeg / librtmp tradition: re-emit the
                        // extended timestamp on every continuation
                        // chunk too.
                        writeBE32(contHdr + contLen, extTs);
                        contLen += 4;
                }
                err = writeBytes(contHdr, contLen);
                if (err.isError()) return err;

                uint32_t remaining = bodyLen - sent;
                uint32_t take = (remaining < static_cast<uint32_t>(chunkSize))
                                ? remaining
                                : static_cast<uint32_t>(chunkSize);
                err = writeBytes(body + sent, take);
                if (err.isError()) return err;
                sent += take;
        }

        // ---- Update encode state ----
        if (fmt == 0) {
                ws.timestamp = msg.timestamp;
                ws.delta = 0;
                ws.messageLength = bodyLen;
                ws.messageTypeId = static_cast<uint8_t>(msg.type);
                ws.messageStreamId = msg.streamId;
                ws.established = true;
        } else if (fmt == 1) {
                ws.timestamp = msg.timestamp;
                ws.delta = deltaToEmit;
                ws.messageLength = bodyLen;
                ws.messageTypeId = static_cast<uint8_t>(msg.type);
        } else if (fmt == 2) {
                ws.timestamp = msg.timestamp;
                ws.delta = deltaToEmit;
        } else {
                ws.timestamp = msg.timestamp;
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// readMessage
// ---------------------------------------------------------------------------

Result<RtmpMessage> RtmpChunkStream::readMessage(unsigned int timeoutMs) {
        if (_device == nullptr) return makeError<RtmpMessage>(Error::Invalid);

        ElapsedTimer timer;
        timer.start();

        for (;;) {
                bool        completedMessage = false;
                RtmpMessage out;
                unsigned int remaining = 0;
                if (timeoutMs != 0) {
                        int64_t elapsedMs = timer.elapsedUs() / 1000;
                        if (elapsedMs >= static_cast<int64_t>(timeoutMs)) {
                                return makeError<RtmpMessage>(Error::Timeout);
                        }
                        remaining = timeoutMs - static_cast<unsigned int>(elapsedMs);
                }

                uint8_t basic[3];
                Error err = readBytesExact(basic, 1, remaining);
                if (err.isError()) return makeError<RtmpMessage>(err);

                uint8_t  fmt = static_cast<uint8_t>((basic[0] >> 6) & 0x03);
                uint32_t csid = static_cast<uint32_t>(basic[0] & 0x3f);
                if (csid == 0) {
                        err = readBytesExact(basic + 1, 1, remaining);
                        if (err.isError()) return makeError<RtmpMessage>(err);
                        csid = 64u + basic[1];
                } else if (csid == 1) {
                        err = readBytesExact(basic + 1, 2, remaining);
                        if (err.isError()) return makeError<RtmpMessage>(err);
                        csid = 64u + basic[1] + (static_cast<uint32_t>(basic[2]) << 8);
                }

                // Decode message header by fmt.
                uint8_t  mh[11];
                size_t   mhLen = (fmt == 0) ? 11 : (fmt == 1) ? 7 : (fmt == 2) ? 3 : 0;
                if (mhLen > 0) {
                        err = readBytesExact(mh, mhLen, remaining);
                        if (err.isError()) return makeError<RtmpMessage>(err);
                }

                {
                Mutex::Locker lk(_readMutex);
                ReadState    &rs = _readStates[csid];

                uint32_t headerTs = 0;
                uint32_t msgLen = 0;
                uint8_t  typeId = 0;
                uint32_t msid = 0;
                bool     fieldExt = false;

                if (fmt == 0) {
                        headerTs = readBE24(mh);
                        msgLen   = readBE24(mh + 3);
                        typeId   = mh[6];
                        msid     = readLE32(mh + 7);
                } else if (fmt == 1) {
                        headerTs = readBE24(mh);
                        msgLen   = readBE24(mh + 3);
                        typeId   = mh[6];
                        msid     = rs.messageStreamId;
                } else if (fmt == 2) {
                        headerTs = readBE24(mh);
                        msgLen   = rs.messageLength;
                        typeId   = rs.messageTypeId;
                        msid     = rs.messageStreamId;
                } else {  // fmt == 3
                        msgLen   = rs.messageLength;
                        typeId   = rs.messageTypeId;
                        msid     = rs.messageStreamId;
                        headerTs = 0;  // see extended-timestamp block below
                }

                if (mhLen > 0 && headerTs == kExtendedTimestampMarker) {
                        uint8_t ext[4];
                        err = readBytesExact(ext, 4, remaining);
                        if (err.isError()) return makeError<RtmpMessage>(err);
                        headerTs = readBE32(ext);
                        fieldExt = true;
                }

                if (fmt == 3 && rs.extendedTimestamp) {
                        // ffmpeg/librtmp tradition: extended timestamp
                        // is repeated on continuation chunks.  We
                        // peek at the first byte; if it looks like a
                        // valid extended-timestamp field (top bit
                        // unset matches the prior ts more closely
                        // than a basic-header byte would), consume
                        // it.  Pragmatically: a peer that ever set
                        // extended timestamp on a CS-id always
                        // continues to do so for that message —
                        // matches what we emit on the write side.
                        uint8_t ext[4];
                        err = readBytesExact(ext, 4, remaining);
                        if (err.isError()) return makeError<RtmpMessage>(err);
                        headerTs = readBE32(ext);
                        fieldExt = true;
                }

                // Update reassembly state.
                if (fmt == 0) {
                        rs.timestamp = headerTs;
                        rs.delta = 0;
                } else if (fmt == 1 || fmt == 2) {
                        rs.delta = headerTs;
                        rs.timestamp += rs.delta;
                } else {
                        // fmt == 3 mid-message: timestamp is constant
                        // across continuation chunks for this message.
                        // For *new-message* fmt-3 leading chunk, the
                        // peer applies the prior delta — match librtmp.
                        if (rs.reassemblyBytes == 0) {
                                rs.timestamp += rs.delta;
                        }
                }
                rs.messageLength    = msgLen;
                rs.messageTypeId    = typeId;
                rs.messageStreamId  = msid;
                rs.extendedTimestamp = fieldExt;
                rs.established      = true;

                // Allocate reassembly buffer on first chunk.
                if (rs.reassemblyBytes == 0) {
                        if (msgLen > 0) {
                                rs.reassembly = Buffer(msgLen);
                                rs.reassembly.setSize(msgLen);
                        } else {
                                rs.reassembly = Buffer();
                        }
                }

                // Read payload bytes (chunkSize-bounded).
                int      peerChunkSize = _peerChunkSize.value();
                uint32_t remainingMsg  = msgLen - rs.reassemblyBytes;
                uint32_t take = (remainingMsg < static_cast<uint32_t>(peerChunkSize))
                                ? remainingMsg
                                : static_cast<uint32_t>(peerChunkSize);
                if (take > 0) {
                        err = readBytesExact(
                                static_cast<uint8_t *>(rs.reassembly.data()) + rs.reassemblyBytes,
                                take, remaining);
                        if (err.isError()) return makeError<RtmpMessage>(err);
                        rs.reassemblyBytes += take;
                }

                if (rs.reassemblyBytes >= msgLen) {
                        out.type = static_cast<RtmpMessage::Type>(typeId);
                        out.streamId = msid;
                        out.timestamp = rs.timestamp;
                        out.chunkStreamId = csid;
                        out.payload = rs.reassembly;
                        rs.reassembly = Buffer();
                        rs.reassemblyBytes = 0;
                        completedMessage = true;
                }
                }  // release _readMutex before any signal emission.

                if (completedMessage) {
                        bool isControl =
                                (out.type == RtmpMessage::SetChunkSize
                                 || out.type == RtmpMessage::AbortMessage
                                 || out.type == RtmpMessage::Acknowledgement
                                 || out.type == RtmpMessage::UserControl
                                 || out.type == RtmpMessage::WindowAckSize
                                 || out.type == RtmpMessage::SetPeerBandwidth);
                        if (isControl) handleControl(out);
                        return makeResult(out);
                }
                // Need more chunks for this message — loop.
        }
}

PROMEKI_NAMESPACE_END
