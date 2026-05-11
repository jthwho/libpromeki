/**
 * @file      rtmphandshake.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RTMP 1.0 handshake state machine (simple + Adobe FMS3 complex).
 * @ingroup network
 *
 * RtmpHandshake is a transport-agnostic, byte-oriented state machine
 * that implements both the simple RTMP 1.0 §5.2.1 handshake and the
 * Adobe FMS3 "digest+key" complex handshake (HMAC-SHA256, schemes 0
 * and 1).  It owns no socket — the caller feeds it received bytes via
 * @ref feed and drains pending output via @ref pendingOutput, then
 * delivers those bytes over whatever transport (TCP, TLS, in-process
 * pipe) is in use.  The same class is used on both sides of the
 * connection; pick a side at construction with the @ref RtmpRole
 * argument.
 *
 * @par Two modes plus Auto
 *
 * - @c Simple — RTMP 1.0 §5.2.1: a 1-byte version (0x03) followed by a
 *   1536-byte block (4-byte timestamp + 4-byte zero + 1528 random
 *   bytes).  The peer echoes the block back verbatim.
 * - @c Complex — Adobe FMS3: the 1528 random bytes contain a 32-byte
 *   HMAC-SHA256 digest at an offset derived from four "scheme" bytes
 *   inside the block, computed over the rest of the block using one
 *   of two well-known seed keys (the @c GenuineFPKey / @c GenuineFMSKey
 *   constants lifted from rtmpdump).  The peer's S2 / C2 carries a
 *   second HMAC-SHA256 over 1504 random bytes, keyed on the peer's
 *   digest, which lets each side authenticate the exchange.
 * - @c Auto (default) — emit a Complex C1 first; if S1 fails to
 *   validate against either scheme, fall back to Simple by echoing
 *   the server's block back verbatim.  Most live destinations
 *   (YouTube, Facebook, Twitch) require Complex; some legacy ingests
 *   only speak Simple.  The Auto path covers both without an
 *   application-level retry.
 *
 * @par Random bytes
 *
 * The 1528-byte random region in C1 / S1 is a nonce against replay,
 * not a deterministic test value.  The implementation fills it with
 * @ref Random::trueRandom (OS entropy) rather than the PRNG, so the
 * handshake stays cryptographically meaningful even when the rest of
 * the process has reseeded the thread-local Mersenne Twister.
 *
 * @par Usage pattern
 *
 * The state machine drains output from @ref pendingOutput and
 * consumes input via @ref feed.  Typical client-side loop on a TCP
 * socket:
 *
 * @code
 * RtmpHandshake hs(RtmpRole::Client);
 * for (;;) {
 *         Buffer tx = hs.pendingOutput();
 *         if (tx.size() > 0) socket->write(tx.data(), tx.size());
 *         if (hs.state() == RtmpHandshake::Done)   break;
 *         if (hs.state() == RtmpHandshake::Failed) return hs.lastError();
 *         char tmp[4096];
 *         int64_t n = socket->read(tmp, sizeof(tmp));
 *         if (n <= 0) { hs.markPeerClosed(); continue; }
 *         Buffer buf = Buffer::wrapHost(tmp, n);
 *         buf.setSize(n);
 *         hs.feed(BufferView(buf, 0, n));
 * }
 * @endcode
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  A single
 * RtmpHandshake instance must only be used from the thread that
 * created it (or moved to via @c moveToThread).  Concurrent calls to
 * @ref feed and @ref pendingOutput on the same instance are
 * undefined.
 */
class RtmpHandshake : public ObjectBase {
                PROMEKI_OBJECT(RtmpHandshake, ObjectBase)
        public:
                /**
                 * @brief Position in the handshake exchange.
                 *
                 * Only the inter-state transitions are observable
                 * externally; the per-state byte counts and partial
                 * digest computations live in the implementation.
                 */
                enum State {
                        NotStarted,      ///< No bytes exchanged yet.
                        ExchangingC0C1,  ///< C0/C1 (or S0/S1) emitted; awaiting the peer's C0/C1 (or S0/S1).
                        ExchangingC2S2,  ///< Server signature received; C2 (or S2) emitted; awaiting peer's final.
                        Done,            ///< Both sides have exchanged C0/C1/C2 and S0/S1/S2 successfully.
                        Failed           ///< Handshake aborted — see @ref lastError.
                };

                /**
                 * @brief Wire-protocol version emitted as the first byte of C0 / S0.
                 *
                 * The spec defines values 3 (current), 6, 8, and 9
                 * (RTMPE variants).  We send and accept only 3.
                 */
                static constexpr uint8_t Version = 0x03;

                /**
                 * @brief Size of the C1 / S1 / C2 / S2 chunk (RTMP §5.2.2).
                 *
                 * Fixed at 1536 bytes: 4-byte timestamp + 4-byte zero
                 * (simple) or version (complex) + 1528-byte random
                 * payload (which holds the digest in complex mode).
                 */
                static constexpr int ChunkSize = 1536;

                /**
                 * @brief Constructs a handshake state machine for @p role.
                 * @param role   @c Client or @c Server — see @ref RtmpRole.
                 * @param parent Optional parent for the ObjectBase tree.
                 *
                 * The new handshake is in the @c NotStarted state with
                 * @ref mode set to @c RtmpHandshakeMode::Auto.  No
                 * bytes are buffered until the caller drives the state
                 * machine.
                 */
                explicit RtmpHandshake(RtmpRole role, ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~RtmpHandshake() override;

                /**
                 * @brief Selects the handshake variant to attempt.
                 *
                 * Must be called before the first @ref feed or
                 * @ref pendingOutput call.  Setting the mode after the
                 * machine has emitted bytes returns @ref Error::Invalid.
                 */
                Error setMode(RtmpHandshakeMode mode);

                /** @brief Returns the configured handshake mode. */
                RtmpHandshakeMode mode() const { return _mode; }

                /**
                 * @brief Returns the handshake variant actually
                 *        negotiated with the peer.
                 *
                 * Meaningful once @ref state returns @c Done.  Before
                 * that the value reflects the current best-guess: for
                 * @c Auto, that's @c Complex until the peer's S1 fails
                 * to validate, at which point it flips to @c Simple.
                 */
                RtmpHandshakeMode negotiatedMode() const { return _negotiatedMode; }

                /**
                 * @brief Returns bytes the caller should send to the peer.
                 *
                 * Drains all currently-pending output.  Returns an
                 * empty (invalid) @ref Buffer when nothing is pending.
                 * Safe to call repeatedly; each call returns a fresh
                 * allocation owned by the caller.
                 */
                Buffer pendingOutput();

                /**
                 * @brief Feeds bytes received from the peer into the state machine.
                 * @param data Bytes received in the order they arrived.
                 * @return @ref Error::Ok when the bytes were consumed
                 *         (state may have advanced); @ref Error::CorruptData
                 *         when the peer sent something unparseable
                 *         (e.g. version mismatch, complex-mode digest
                 *         validation failure with no fall-back
                 *         available); @ref Error::Cancelled when the
                 *         machine has already entered @c Failed for
                 *         another reason.
                 *
                 * The state machine consumes bytes from @p data as
                 * needed; surplus bytes are buffered internally for
                 * the next call.  "Need more bytes" is implicit in
                 * @c state() != Done after @c Error::Ok.
                 */
                Error feed(const BufferView &data);

                /**
                 * @brief Reports that the peer has closed the connection.
                 *
                 * Drives the state machine to @c Failed with
                 * @ref Error::Cancelled if the handshake hasn't
                 * completed yet.  No-op once the machine is in
                 * @c Done or @c Failed.
                 */
                void markPeerClosed();

                /** @brief Returns the current state. */
                State state() const { return _state; }

                /** @brief Returns the last terminal error (only valid once @ref state is @c Failed). */
                Error lastError() const { return _lastError; }

                /**
                 * @brief Returns the role this state machine was constructed for.
                 */
                RtmpRole role() const { return _role; }

                /**
                 * @brief Returns the peer's epoch timestamp (the value
                 *        the peer sent as the first 4 bytes of their
                 *        C1 / S1).
                 *
                 * Useful only for logging / interop diagnostics.  Zero
                 * before the peer's C1 / S1 has been received.
                 */
                uint32_t peerEpoch() const { return _peerEpoch; }

                /**
                 * @brief Returns the local epoch timestamp written
                 *        into our C1 / S1.
                 *
                 * Initialized to @c 0 — matches the common-case OBS /
                 * ffmpeg behavior of stamping a zero timestamp.  Tests
                 * may override via @ref setLocalEpoch before the first
                 * @ref pendingOutput call.
                 */
                uint32_t localEpoch() const { return _localEpoch; }

                /**
                 * @brief Overrides the local epoch timestamp.  Test-only.
                 *
                 * Must be called before the first @ref pendingOutput
                 * call; later calls have no effect.
                 */
                void setLocalEpoch(uint32_t epoch);

                /**
                 * @brief Overrides the random nonce written into the
                 *        local C1 / S1 payload.  Test-only.
                 *
                 * The buffer must be exactly @c 1528 bytes long.  Used
                 * to make doctest cases deterministic by feeding a
                 * known nonce instead of the OS entropy pool.
                 *
                 * @return @ref Error::InvalidArgument if @p nonce is not
                 *         the expected size; @ref Error::Invalid if
                 *         the machine has already emitted its first
                 *         chunk.
                 */
                Error setLocalNonce(const BufferView &nonce);

                /**
                 * @brief Emitted once the handshake reaches @c Done. @signal
                 */
                PROMEKI_SIGNAL(complete);

                /**
                 * @brief Emitted once the handshake transitions to
                 *        @c Failed.  Carries the terminal error. @signal
                 */
                PROMEKI_SIGNAL(failed, Error);

        private:
                /** @brief Internal sub-step inside ExchangingC0C1 / ExchangingC2S2. */
                enum InternalStep {
                        StepSendC0C1,    ///< Client: build + emit C0+C1.
                        StepRecvS0S1,    ///< Client: collect 1+1536 from peer.
                        StepSendC2,      ///< Client: build + emit C2.
                        StepRecvS2,      ///< Client: collect final 1536 from peer.
                        StepRecvC0C1,    ///< Server: collect 1+1536 from peer.
                        StepSendS0S1S2,  ///< Server: build + emit S0+S1+S2.
                        StepRecvC2,      ///< Server: collect final 1536 from peer.
                        StepDone
                };

                void   advance();
                void   fail(Error err);
                Error  produceClientC1();
                Error  produceClientC2();
                Error  produceServerS0S1S2();
                Error  consumePeerC0C1();          ///< Server: parse C0+C1.
                Error  consumePeerS0S1();          ///< Client: parse S0+S1.
                Error  consumePeerC2();            ///< Server: parse C2.
                Error  consumePeerS2();            ///< Client: parse S2.

                RtmpRole          _role;
                RtmpHandshakeMode _mode = RtmpHandshakeMode::Auto;
                RtmpHandshakeMode _negotiatedMode = RtmpHandshakeMode::Auto;
                State             _state = NotStarted;
                InternalStep      _step = StepSendC0C1;
                Error             _lastError = Error::Ok;

                uint32_t _localEpoch = 0;
                uint32_t _peerEpoch = 0;

                bool _outEmittedFirst = false;     ///< True once the first chunk has been handed to pendingOutput().
                bool _hasOverrideNonce = false;
                List<uint8_t> _overrideNonce;      ///< 1528-byte test-injected nonce, when non-empty.

                List<uint8_t> _outBuffer;          ///< Bytes pending egress.
                List<uint8_t> _inBuffer;           ///< Bytes from peer not yet consumed.

                // ---- Complex-handshake bookkeeping ----
                int    _localScheme = 1;           ///< 0 or 1 — used when emitting Complex C1 / S1.
                int    _peerScheme = -1;           ///< 0 / 1 once validated; -1 = unknown / not Complex.
                List<uint8_t> _localC1S1;          ///< 1536-byte copy of the C1/S1 we sent.
                List<uint8_t> _peerC1S1;           ///< 1536-byte copy of the peer's C1/S1.
                uint8_t _localDigest[32] = {};     ///< Digest copied out of our local C1/S1.
                uint8_t _peerDigest[32] = {};      ///< Digest pulled from the peer's C1/S1.
};

PROMEKI_NAMESPACE_END
