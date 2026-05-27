/**
 * @file      rtmphandshake.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtmphandshake.h>

#include <cstring>

#include <promeki/hmac.h>
#include <promeki/logger.h>
#include <promeki/random.h>
#include <promeki/sha2.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(RtmpHandshake);

namespace {

// ---------------------------------------------------------------------------
// Adobe "Genuine FP" / "Genuine FMS" seed keys.
//
// The first 30 / 36 bytes are the textual seed; the trailing 32 bytes are a
// fixed magic suffix shared by both keys.  Lifted verbatim from rtmpdump
// (handshake.h) and confirmed against the FFmpeg librtmp implementation.
// They are well-known Adobe constants — not secret keys — and exist in
// every public RTMP complex-handshake implementation.
// ---------------------------------------------------------------------------

constexpr uint8_t kGenuineFPKey[62] = {
        'G',  'e',  'n',  'u',  'i',  'n',  'e',  ' ',  'A',  'd',  'o',  'b',
        'e',  ' ',  'F',  'l',  'a',  's',  'h',  ' ',  'P',  'l',  'a',  'y',
        'e',  'r',  ' ',  '0',  '0',  '1',
        0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1,
        0x02, 0x9E, 0x7E, 0x57, 0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
        0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
};

constexpr uint8_t kGenuineFMSKey[68] = {
        'G',  'e',  'n',  'u',  'i',  'n',  'e',  ' ',  'A',  'd',  'o',  'b',
        'e',  ' ',  'F',  'l',  'a',  's',  'h',  ' ',  'M',  'e',  'd',  'i',
        'a',  ' ',  'S',  'e',  'r',  'v',  'e',  'r',  ' ',  '0',  '0',  '1',
        0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1,
        0x02, 0x9E, 0x7E, 0x57, 0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
        0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
};

constexpr size_t kFpSeedLen  = 30;  ///< "Genuine Adobe Flash Player 001"
constexpr size_t kFmsSeedLen = 36;  ///< "Genuine Adobe Flash Media Server 001"

constexpr int    kChunkSize = 1536;
constexpr int    kDigestLen = 32;
constexpr int    kDigestOffsetField = 4;     ///< 4 unsigned bytes summed for the digest offset.
constexpr int    kDigestRangeSize   = 728;   ///< 1536 - 8 (offset+filler) - 32 (digest) - 768 (key, scheme 1) = 728 mod range.

constexpr int    kScheme0Base = 12;    ///< Bytes 8..11 carry offset bytes; digest lives at 12 + (sum%728).
constexpr int    kScheme1Base = 776;   ///< Bytes 772..775 carry offset bytes; digest lives at 776 + (sum%728).
constexpr int    kScheme0Off  = 8;
constexpr int    kScheme1Off  = 772;

/**
 * @brief Computes the digest byte offset for scheme @p schemeIdx in
 *        a 1536-byte C1/S1 block.
 *
 * Scheme 0: digest offset = (sum of bytes 8..11) mod 728 + 12.
 * Scheme 1: digest offset = (sum of bytes 772..775) mod 728 + 776.
 */
int digestOffset(const uint8_t *block, int schemeIdx) {
        int field = (schemeIdx == 0) ? kScheme0Off : kScheme1Off;
        int base  = (schemeIdx == 0) ? kScheme0Base : kScheme1Base;
        int sum   = block[field] + block[field + 1] + block[field + 2] + block[field + 3];
        return base + (sum % kDigestRangeSize);
}

/**
 * @brief Computes HMAC-SHA-256 over a 1536-byte block, skipping the
 *        32-byte region at @p digestPos.
 *
 * The complex-handshake digest covers the 1504 bytes outside the
 * 32-byte digest field; streaming HMAC lets us feed both regions
 * without first materializing the concatenation.
 */
void hmacOverBlockExcludingDigest(const uint8_t *block, int digestPos,
                                  const uint8_t *key, size_t keyLen,
                                  uint8_t outDigest[kDigestLen]) {
        HmacSha256 h(key, keyLen);
        h.update(block, digestPos);
        h.update(block + digestPos + kDigestLen,
                 kChunkSize - digestPos - kDigestLen);
        SHA256Digest d = h.finalize();
        std::memcpy(outDigest, d.data(), kDigestLen);
}

/**
 * @brief Constant-time byte comparison (RFC 2104 — must be timing-safe).
 */
bool constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t len) {
        uint8_t accum = 0;
        for (size_t i = 0; i < len; i++) accum |= a[i] ^ b[i];
        return accum == 0;
}

/**
 * @brief Tries scheme 0 then scheme 1; returns the scheme index whose
 *        digest validates, or -1 if neither does.
 */
int validateBlockDigest(const uint8_t *block, const uint8_t *key, size_t keyLen,
                        uint8_t outDigest[kDigestLen]) {
        for (int s = 1; s >= 0; s--) {  // try scheme 1 first; YouTube/FB use it.
                int pos = digestOffset(block, s);
                uint8_t expected[kDigestLen];
                hmacOverBlockExcludingDigest(block, pos, key, keyLen, expected);
                if (constantTimeEqual(expected, block + pos, kDigestLen)) {
                        if (outDigest != nullptr) std::memcpy(outDigest, block + pos, kDigestLen);
                        return s;
                }
        }
        return -1;
}

/**
 * @brief Writes a 1536-byte Complex C1/S1 block: timestamp, version,
 *        random nonce, then the computed digest at the scheme-derived
 *        offset.
 */
void buildComplexBlock(uint8_t *block, uint32_t timestamp, uint32_t version,
                       int schemeIdx, const uint8_t *nonce1528,
                       const uint8_t *key, size_t keyLen,
                       uint8_t outDigest[kDigestLen]) {
        block[0] = static_cast<uint8_t>((timestamp >> 24) & 0xff);
        block[1] = static_cast<uint8_t>((timestamp >> 16) & 0xff);
        block[2] = static_cast<uint8_t>((timestamp >> 8) & 0xff);
        block[3] = static_cast<uint8_t>(timestamp & 0xff);
        block[4] = static_cast<uint8_t>((version >> 24) & 0xff);
        block[5] = static_cast<uint8_t>((version >> 16) & 0xff);
        block[6] = static_cast<uint8_t>((version >> 8) & 0xff);
        block[7] = static_cast<uint8_t>(version & 0xff);
        std::memcpy(block + 8, nonce1528, kChunkSize - 8);

        int pos = digestOffset(block, schemeIdx);
        // Zero the digest region so it isn't part of the HMAC input.
        std::memset(block + pos, 0, kDigestLen);
        hmacOverBlockExcludingDigest(block, pos, key, keyLen, block + pos);
        if (outDigest != nullptr) std::memcpy(outDigest, block + pos, kDigestLen);
}

/**
 * @brief Computes the trailing 32-byte signature in a Complex C2/S2.
 *
 * Per Adobe FMS3: key = HMAC(our full seed key, peer's digest);
 * sig = HMAC(key, first 1504 bytes of our 1536-byte echo block).
 */
void computeC2S2Signature(const uint8_t *peerDigest,
                          const uint8_t *ownKey, size_t ownKeyLen,
                          const uint8_t *body1504,
                          uint8_t outSig[kDigestLen]) {
        SHA256Digest derivedKey = hmacSha256(ownKey, ownKeyLen, peerDigest, kDigestLen);
        SHA256Digest sig        = hmacSha256(derivedKey.data(), derivedKey.size(),
                                             body1504, kChunkSize - kDigestLen);
        std::memcpy(outSig, sig.data(), kDigestLen);
}

/**
 * @brief Returns 1528 OS-entropy random bytes, or the test-injected
 *        override.
 */
void fillNonce(uint8_t *out, size_t len, const List<uint8_t> &override) {
        if (override.size() == len) {
                std::memcpy(out, override.data(), len);
                return;
        }
        Random::trueRandom(out, len);
}

void appendBytes(List<uint8_t> &dst, const uint8_t *src, size_t len) {
        size_t before = dst.size();
        dst.resize(before + len);
        std::memcpy(dst.data() + before, src, len);
}

void dropFront(List<uint8_t> &buf, size_t n) {
        if (n == 0) return;
        if (n >= buf.size()) {
                buf.clear();
                return;
        }
        std::memmove(buf.data(), buf.data() + n, buf.size() - n);
        buf.resize(buf.size() - n);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / mode
// ---------------------------------------------------------------------------

RtmpHandshake::RtmpHandshake(RtmpRole role, ObjectBase *parent)
    : ObjectBase(parent), _role(role) {
        _step = (role == RtmpRole::Server) ? StepRecvC0C1 : StepSendC0C1;
}

RtmpHandshake::~RtmpHandshake() = default;

Error RtmpHandshake::setMode(RtmpHandshakeMode mode) {
        if (_outEmittedFirst) {
                promekiDebug("RtmpHandshake: setMode after first emission is not allowed");
                return Error::Invalid;
        }
        _mode = mode;
        if (mode == RtmpHandshakeMode::Simple) {
                _negotiatedMode = RtmpHandshakeMode::Simple;
        } else if (mode == RtmpHandshakeMode::Complex) {
                _negotiatedMode = RtmpHandshakeMode::Complex;
        } else {
                _negotiatedMode = RtmpHandshakeMode::Auto;  // resolved at validation time.
        }
        return Error::Ok;
}

void RtmpHandshake::setLocalEpoch(uint32_t epoch) {
        if (_outEmittedFirst) return;
        _localEpoch = epoch;
}

Error RtmpHandshake::setLocalNonce(const BufferView &nonce) {
        if (_outEmittedFirst) return Error::Invalid;
        if (nonce.size() != static_cast<size_t>(ChunkSize - 8)) return Error::InvalidArgument;
        _overrideNonce.resize(nonce.size());
        // Concatenate every slice in the BufferView into the override.
        size_t offset = 0;
        for (auto entry : nonce) {
                std::memcpy(_overrideNonce.data() + offset, entry.data(), entry.size());
                offset += entry.size();
        }
        _hasOverrideNonce = true;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// State machine drivers
// ---------------------------------------------------------------------------

void RtmpHandshake::fail(Error err) {
        if (_state == Failed) return;
        _state = Failed;
        _step  = StepDone;
        _lastError = err;
        promekiWarn("RtmpHandshake: failed: %s", err.name().cstr());
        failedSignal.emit(err);
}

void RtmpHandshake::markPeerClosed() {
        if (_state == Done || _state == Failed) return;
        fail(Error::Cancelled);
}

Buffer RtmpHandshake::pendingOutput() {
        // Advance the state machine to (potentially) produce output before draining.
        advance();

        if (_outBuffer.isEmpty()) return Buffer();

        Buffer out(_outBuffer.size());
        out.setSize(_outBuffer.size());
        std::memcpy(out.data(), _outBuffer.data(), _outBuffer.size());
        _outBuffer.clear();
        _outEmittedFirst = true;
        return out;
}

Error RtmpHandshake::feed(const BufferView &data) {
        if (_state == Failed)  return Error::Cancelled;
        if (_state == Done)    return Error::Ok;
        if (_state == NotStarted) _state = ExchangingC0C1;

        for (auto entry : data) {
                if (entry.size() == 0) continue;
                appendBytes(_inBuffer, entry.data(), entry.size());
        }
        advance();
        if (_state == Failed) return _lastError;
        return Error::Ok;
}

void RtmpHandshake::advance() {
        // Drive the state machine until it either finishes a step or
        // needs more input / has filled the output buffer.  Each
        // produce*/consume* helper updates _step itself; we loop until
        // a step makes no further progress.
        for (;;) {
                InternalStep before = _step;

                switch (_step) {
                        case StepSendC0C1:
                                if (produceClientC1() != Error::Ok) return;
                                _state = ExchangingC0C1;
                                _step = StepRecvS0S1;
                                break;
                        case StepRecvS0S1:
                                if (consumePeerS0S1() != Error::Ok) return;
                                if (_step == StepRecvS0S1) return;  // need more bytes
                                break;
                        case StepSendC2:
                                if (produceClientC2() != Error::Ok) return;
                                _state = ExchangingC2S2;
                                _step = StepRecvS2;
                                break;
                        case StepRecvS2:
                                if (consumePeerS2() != Error::Ok) return;
                                if (_step == StepRecvS2) return;  // need more bytes
                                break;
                        case StepRecvC0C1:
                                if (consumePeerC0C1() != Error::Ok) return;
                                if (_step == StepRecvC0C1) return;  // need more bytes
                                break;
                        case StepSendS0S1S2:
                                if (produceServerS0S1S2() != Error::Ok) return;
                                _state = ExchangingC2S2;
                                _step = StepRecvC2;
                                break;
                        case StepRecvC2:
                                if (consumePeerC2() != Error::Ok) return;
                                if (_step == StepRecvC2) return;  // need more bytes
                                break;
                        case StepDone:
                                return;
                }
                if (_state == Failed || _step == before) return;
                if (_step == StepDone) {
                        _state = Done;
                        promekiDebug("RtmpHandshake: complete (mode=%s)",
                                     _negotiatedMode.toString().cstr());
                        completeSignal.emit();
                        return;
                }
        }
}

// ---------------------------------------------------------------------------
// Producers
// ---------------------------------------------------------------------------

Error RtmpHandshake::produceClientC1() {
        // C0 + C1 = 1 + 1536 = 1537 bytes.
        const size_t before = _outBuffer.size();
        _outBuffer.resize(before + 1 + ChunkSize);
        uint8_t *p = _outBuffer.data() + before;
        p[0] = Version;
        uint8_t *block = p + 1;

        uint8_t nonce[ChunkSize - 8];
        fillNonce(nonce, sizeof(nonce), _overrideNonce);

        if (_mode == RtmpHandshakeMode::Simple) {
                // Simple: 4-byte timestamp + 4 zero + 1528 random.
                block[0] = static_cast<uint8_t>((_localEpoch >> 24) & 0xff);
                block[1] = static_cast<uint8_t>((_localEpoch >> 16) & 0xff);
                block[2] = static_cast<uint8_t>((_localEpoch >> 8) & 0xff);
                block[3] = static_cast<uint8_t>(_localEpoch & 0xff);
                std::memset(block + 4, 0, 4);
                std::memcpy(block + 8, nonce, sizeof(nonce));
                _negotiatedMode = RtmpHandshakeMode::Simple;
        } else {
                // Complex / Auto: version field is non-zero; we emit
                // the FMLE-style version 0x80000702 that ffmpeg /
                // librtmp / OBS all send.
                const uint32_t kFlashVersion = 0x80000702u;
                buildComplexBlock(block, _localEpoch, kFlashVersion,
                                  _localScheme, nonce,
                                  kGenuineFPKey, kFpSeedLen, _localDigest);
                if (_mode == RtmpHandshakeMode::Complex) _negotiatedMode = RtmpHandshakeMode::Complex;
        }

        // Cache our C1 for later C2 signature derivation (Auto / Complex).
        _localC1S1.resize(ChunkSize);
        std::memcpy(_localC1S1.data(), block, ChunkSize);
        return Error::Ok;
}

Error RtmpHandshake::produceServerS0S1S2() {
        // S0 + S1 + S2 = 1 + 1536 + 1536 = 3073 bytes.
        // S2 is the peer's C1 echoed back (Simple) or carries a
        // Complex-derived signature (Complex).
        const size_t before = _outBuffer.size();
        _outBuffer.resize(before + 1 + ChunkSize + ChunkSize);
        uint8_t *p = _outBuffer.data() + before;
        p[0] = Version;
        uint8_t *s1 = p + 1;
        uint8_t *s2 = s1 + ChunkSize;

        uint8_t nonceS1[ChunkSize - 8];
        fillNonce(nonceS1, sizeof(nonceS1), _overrideNonce);

        const bool complex = (_peerScheme >= 0)
                             && (_mode != RtmpHandshakeMode::Simple);

        if (complex) {
                const uint32_t kFmsVersion = 0x0d0e0a0du;  // librtmp constant.
                _localScheme = _peerScheme;
                buildComplexBlock(s1, _localEpoch, kFmsVersion,
                                  _localScheme, nonceS1,
                                  kGenuineFMSKey, kFmsSeedLen, _localDigest);

                // S2: 1504 random + 32-byte signature derived from peer's C1 digest.
                uint8_t nonceS2[ChunkSize - kDigestLen];
                Random::trueRandom(nonceS2, sizeof(nonceS2));
                std::memcpy(s2, nonceS2, sizeof(nonceS2));
                computeC2S2Signature(_peerDigest, kGenuineFMSKey, sizeof(kGenuineFMSKey),
                                     s2, s2 + sizeof(nonceS2));
                _negotiatedMode = RtmpHandshakeMode::Complex;
        } else {
                // Simple S1: 4-byte timestamp + 4 zero + 1528 random.
                s1[0] = static_cast<uint8_t>((_localEpoch >> 24) & 0xff);
                s1[1] = static_cast<uint8_t>((_localEpoch >> 16) & 0xff);
                s1[2] = static_cast<uint8_t>((_localEpoch >> 8) & 0xff);
                s1[3] = static_cast<uint8_t>(_localEpoch & 0xff);
                std::memset(s1 + 4, 0, 4);
                std::memcpy(s1 + 8, nonceS1, sizeof(nonceS1));
                // Simple S2 echoes the peer's C1 verbatim.
                if (_peerC1S1.size() == static_cast<size_t>(ChunkSize)) {
                        std::memcpy(s2, _peerC1S1.data(), ChunkSize);
                } else {
                        std::memset(s2, 0, ChunkSize);
                }
                _negotiatedMode = RtmpHandshakeMode::Simple;
        }

        _localC1S1.resize(ChunkSize);
        std::memcpy(_localC1S1.data(), s1, ChunkSize);
        return Error::Ok;
}

Error RtmpHandshake::produceClientC2() {
        const size_t before = _outBuffer.size();
        _outBuffer.resize(before + ChunkSize);
        uint8_t *c2 = _outBuffer.data() + before;

        if (_negotiatedMode == RtmpHandshakeMode::Complex) {
                // 1504 random bytes + 32-byte signature derived from
                // server's S1 digest under GenuineFPKey.
                uint8_t nonce[ChunkSize - kDigestLen];
                Random::trueRandom(nonce, sizeof(nonce));
                std::memcpy(c2, nonce, sizeof(nonce));
                computeC2S2Signature(_peerDigest, kGenuineFPKey, sizeof(kGenuineFPKey),
                                     c2, c2 + sizeof(nonce));
        } else {
                // Simple C2 echoes the server's S1.
                if (_peerC1S1.size() == static_cast<size_t>(ChunkSize)) {
                        std::memcpy(c2, _peerC1S1.data(), ChunkSize);
                } else {
                        std::memset(c2, 0, ChunkSize);
                }
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Consumers
// ---------------------------------------------------------------------------

Error RtmpHandshake::consumePeerC0C1() {
        if (_inBuffer.size() < static_cast<size_t>(1 + ChunkSize)) return Error::Ok;  // need more
        uint8_t version = _inBuffer.data()[0];
        if (version != Version) {
                promekiWarn("RtmpHandshake (server): peer C0 version=0x%02x, expected 0x%02x", version,
                            static_cast<unsigned>(Version));
                fail(Error::CorruptData);
                return Error::CorruptData;
        }
        _peerC1S1.resize(ChunkSize);
        std::memcpy(_peerC1S1.data(), _inBuffer.data() + 1, ChunkSize);

        // Capture the peer's epoch for diagnostics.
        const uint8_t *block = _peerC1S1.data();
        _peerEpoch = (static_cast<uint32_t>(block[0]) << 24)
                   | (static_cast<uint32_t>(block[1]) << 16)
                   | (static_cast<uint32_t>(block[2]) << 8)
                   | static_cast<uint32_t>(block[3]);

        // Decide whether the peer used Complex by looking at C1's
        // version field — Simple sends zeros there.  We still
        // validate the digest against GenuineFPKey if we're a server
        // willing to speak Complex.
        bool peerVersionNonzero =
                (block[4] | block[5] | block[6] | block[7]) != 0;
        if (peerVersionNonzero && _mode != RtmpHandshakeMode::Simple) {
                int scheme = validateBlockDigest(block, kGenuineFPKey, kFpSeedLen, _peerDigest);
                if (scheme >= 0) {
                        _peerScheme = scheme;
                        _negotiatedMode = RtmpHandshakeMode::Complex;
                } else if (_mode == RtmpHandshakeMode::Complex) {
                        promekiWarn("RtmpHandshake (server): peer C1 digest invalid in strict Complex mode");
                        fail(Error::CorruptData);
                        return Error::CorruptData;
                } else {
                        _peerScheme = -1;
                        _negotiatedMode = RtmpHandshakeMode::Simple;
                }
        } else {
                if (_mode == RtmpHandshakeMode::Complex) {
                        // Peer is plainly Simple but we demanded Complex.
                        promekiWarn("RtmpHandshake (server): peer C1 is Simple but Complex was required");
                        fail(Error::CorruptData);
                        return Error::CorruptData;
                }
                _peerScheme = -1;
                _negotiatedMode = RtmpHandshakeMode::Simple;
        }

        dropFront(_inBuffer, 1 + ChunkSize);
        _step = StepSendS0S1S2;
        return Error::Ok;
}

Error RtmpHandshake::consumePeerS0S1() {
        if (_inBuffer.size() < static_cast<size_t>(1 + ChunkSize)) return Error::Ok;  // need more
        uint8_t version = _inBuffer.data()[0];
        if (version != Version) {
                promekiWarn("RtmpHandshake (client): peer S0 version=0x%02x, expected 0x%02x", version,
                            static_cast<unsigned>(Version));
                fail(Error::CorruptData);
                return Error::CorruptData;
        }
        _peerC1S1.resize(ChunkSize);
        std::memcpy(_peerC1S1.data(), _inBuffer.data() + 1, ChunkSize);

        const uint8_t *block = _peerC1S1.data();
        _peerEpoch = (static_cast<uint32_t>(block[0]) << 24)
                   | (static_cast<uint32_t>(block[1]) << 16)
                   | (static_cast<uint32_t>(block[2]) << 8)
                   | static_cast<uint32_t>(block[3]);

        // If we sent a Complex C1, validate S1 against GenuineFMSKey.
        // - Strict Complex → digest must match.
        // - Auto           → fall back to Simple on mismatch.
        // - Simple         → don't validate; just echo.
        if (_negotiatedMode == RtmpHandshakeMode::Simple) {
                _peerScheme = -1;
        } else {
                int scheme = validateBlockDigest(block, kGenuineFMSKey, kFmsSeedLen, _peerDigest);
                if (scheme >= 0) {
                        _peerScheme = scheme;
                        _negotiatedMode = RtmpHandshakeMode::Complex;
                } else if (_mode == RtmpHandshakeMode::Complex) {
                        promekiWarn("RtmpHandshake (client): peer S1 digest invalid in strict Complex mode");
                        fail(Error::CorruptData);
                        return Error::CorruptData;
                } else {
                        // Auto fall-back: treat as Simple.
                        _peerScheme = -1;
                        _negotiatedMode = RtmpHandshakeMode::Simple;
                        promekiDebug("RtmpHandshake: peer S1 lacks valid Complex digest; "
                                     "falling back to Simple handshake");
                }
        }

        dropFront(_inBuffer, 1 + ChunkSize);
        _step = StepSendC2;
        return Error::Ok;
}

Error RtmpHandshake::consumePeerC2() {
        if (_inBuffer.size() < static_cast<size_t>(ChunkSize)) return Error::Ok;  // need more

        // Validate C2: Complex carries a derived signature; Simple is
        // just an echo of our S1.  Both checks are best-effort —
        // failing here aborts the handshake.
        const uint8_t *c2 = _inBuffer.data();
        if (_negotiatedMode == RtmpHandshakeMode::Complex) {
                uint8_t expected[kDigestLen];
                computeC2S2Signature(_localDigest,
                                     kGenuineFPKey, sizeof(kGenuineFPKey),
                                     c2, expected);
                if (!constantTimeEqual(expected, c2 + (ChunkSize - kDigestLen), kDigestLen)) {
                        promekiWarn("RtmpHandshake (server): peer C2 signature mismatch in Complex mode");
                        fail(Error::CorruptData);
                        return Error::CorruptData;
                }
        }
        // Simple-mode echo: we don't strictly require byte-for-byte
        // equality — some clients re-randomize the trailing bytes
        // even in Simple mode.  Accept the frame as-is.

        dropFront(_inBuffer, ChunkSize);
        _step = StepDone;
        return Error::Ok;
}

Error RtmpHandshake::consumePeerS2() {
        if (_inBuffer.size() < static_cast<size_t>(ChunkSize)) return Error::Ok;  // need more

        const uint8_t *s2 = _inBuffer.data();
        if (_negotiatedMode == RtmpHandshakeMode::Complex) {
                uint8_t expected[kDigestLen];
                computeC2S2Signature(_localDigest,
                                     kGenuineFMSKey, sizeof(kGenuineFMSKey),
                                     s2, expected);
                if (!constantTimeEqual(expected, s2 + (ChunkSize - kDigestLen), kDigestLen)) {
                        promekiWarn("RtmpHandshake (client): peer S2 signature mismatch in Complex mode");
                        fail(Error::CorruptData);
                        return Error::CorruptData;
                }
        }

        dropFront(_inBuffer, ChunkSize);
        _step = StepDone;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
