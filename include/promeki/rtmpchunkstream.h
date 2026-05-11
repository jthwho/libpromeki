/**
 * @file      rtmpchunkstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>

#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/hashmap.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/result.h>
#include <promeki/rtmpmessage.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief Bidirectional RTMP chunk-stream multiplexer (RTMP 1.0 §5.3).
 * @ingroup network
 *
 * RtmpChunkStream sits between the byte-oriented @ref IODevice
 * transport (a connected @ref TcpSocket / @ref SslSocket / etc.) and
 * the message-oriented @ref RtmpSession.  It splits outbound
 * @ref RtmpMessage payloads into chunks no larger than the local
 * chunk size and reassembles inbound chunks into complete messages.
 *
 * @par Chunk format (RTMP 1.0 §5.3.1)
 *
 * Every chunk on the wire is built from up to four header pieces and
 * a payload byte run:
 *
 *   - **Basic header (1 / 2 / 3 bytes).**  Carries a 2-bit format
 *     code (which selects the message-header length) and a 6-bit /
 *     14-bit / 22-bit chunk-stream-id.  CS-id values 2-63 fit in
 *     one byte; 64-319 in two; 320-65599 in three.
 *   - **Message header (0 / 3 / 7 / 11 bytes).**  Format-dependent;
 *     carries the timestamp / timestamp-delta, message length, type
 *     id, and (format 0 only) the message-stream-id.
 *   - **Extended timestamp (0 / 4 bytes).**  Present whenever the
 *     24-bit timestamp / delta field equals @c 0xFFFFFF.  Carries
 *     the full 32-bit value.
 *   - **Payload (≤ chunkSize bytes).**  The next slice of the
 *     message body.
 *
 * The per-cs-id state needed to track the "previous" header values
 * for type-1 / 2 / 3 delta encoding is held inside this class; users
 * see only complete @ref RtmpMessage values.
 *
 * @par Window-ack-size flow control
 *
 * RTMP requires the receiver to emit an @c Acknowledgement message
 * every @ref localWindowAckSize bytes received.  RtmpChunkStream
 * tracks the cumulative receive byte count and emits the
 * Acknowledgement automatically.  Likewise the sender's window-ack
 * configuration is exchanged via the @c WindowAckSize message — the
 * peer's value (advertised in their @c WindowAckSize) is captured
 * inside @ref readMessage and exposed through @ref peerWindowAckSize.
 *
 * @par Thread Safety
 * RtmpChunkStream is designed for the "one writer + one reader
 * thread" topology described in the RTMP devplan: a single thread
 * calls @ref writeMessage; a different thread calls @ref readMessage.
 * Per-cs-id encode and decode state are isolated in their own maps,
 * protected by their own mutexes.  The cross-thread fields
 * (peer-chunk-size, peer-window-ack-size, byte counters) are
 * @ref Atomic.  Concurrent calls to @c writeMessage from multiple
 * threads on the same instance are not supported — the local
 * encode state would race; serialize via a single writer thread per
 * the devplan's topology.
 */
class RtmpChunkStream : public ObjectBase {
                PROMEKI_OBJECT(RtmpChunkStream, ObjectBase)
        public:
                /** @brief Default local + peer chunk size at connect time (RTMP §5.4.1). */
                static constexpr int DefaultChunkSize = 128;

                /** @brief Minimum chunk size the peer is allowed to negotiate (RTMP §5.4.1). */
                static constexpr int MinChunkSize = 128;

                /**
                 * @brief Maximum chunk size we accept / emit.
                 *
                 * RTMP §5.4.1 specifies the theoretical maximum as
                 * the 24-bit message-length field's tipping point,
                 * but most peers reject anything ≥ 65536, so we cap
                 * at 65535.
                 */
                static constexpr int MaxChunkSize = 65535;

                /** @brief Default window-ack-size we advertise on @c WindowAckSize. */
                static constexpr int DefaultWindowAckSize = 5'000'000;

                /**
                 * @brief Constructs a chunk stream bound to @p device.
                 *
                 * The device must be open and connected.  The chunk
                 * stream does not take ownership — the caller manages
                 * device lifetime.
                 */
                explicit RtmpChunkStream(IODevice *device, ObjectBase *parent = nullptr);

                /** @brief Destructor.  Does not close the device. */
                ~RtmpChunkStream() override;

                // ---- Write side ----

                /**
                 * @brief Splits @p msg across chunks and writes every
                 *        byte to the bound device.
                 *
                 * Picks the most compressive header type permissible
                 * against the per-CS-id encode state (type 0 for the
                 * first message on a CS-id; type 1 when only the
                 * timestamp delta and length change; type 2 when only
                 * the timestamp delta changes; type 3 for
                 * continuation chunks within the same message).
                 *
                 * @return @ref Error::Ok when every byte of the
                 *         message was successfully written;
                 *         @ref Error::IOError when the underlying
                 *         device returns a short write.
                 */
                Error writeMessage(const RtmpMessage &msg);

                // ---- Read side ----

                /**
                 * @brief Drains the device until exactly one complete
                 *        message has been reassembled and returns it.
                 *
                 * Internally consumes one or more chunks (including
                 * extended-timestamp chunks and chunkSize-reset
                 * frames), and emits an @c Acknowledgement message as
                 * the cumulative received byte count crosses
                 * @ref peerWindowAckSize.
                 *
                 * Returns Protocol Control Messages
                 * (SetChunkSize / WindowAckSize / SetPeerBandwidth /
                 * Acknowledgement / etc.) on the same path as
                 * application messages; the chunk-stream layer
                 * applies any value changes (peer chunk-size, peer
                 * window-ack-size) before returning, so the caller
                 * may inspect-and-ignore those types.
                 *
                 * @param timeoutMs Maximum wall time to wait for the
                 *                  next byte.  @c 0 = wait forever.
                 * @return The next message on success;
                 *         @ref Error::IOError on transport failure;
                 *         @ref Error::CorruptData on protocol
                 *         violation; @ref Error::Timeout when the
                 *         device produced no bytes within the
                 *         budget.
                 */
                Result<RtmpMessage> readMessage(unsigned int timeoutMs = 0);

                // ---- Configuration ----

                /** @brief Current local chunk size. */
                int  localChunkSize() const  { return _localChunkSize.value(); }

                /**
                 * @brief Raises the local chunk size and emits a
                 *        @c SetChunkSize Protocol Control Message.
                 *
                 * @return @ref Error::InvalidArgument when @p bytes
                 *         is outside [@ref MinChunkSize,
                 *         @ref MaxChunkSize]; the underlying device
                 *         error when the SetChunkSize fails to write.
                 */
                Error setLocalChunkSize(int bytes);

                /** @brief Last peer-advertised chunk size. */
                int  peerChunkSize() const { return _peerChunkSize.value(); }

                /** @brief Current local window-ack-size value. */
                int  localWindowAckSize() const { return _localWindowAckSize.value(); }

                /**
                 * @brief Updates the advertised window-ack-size and
                 *        emits a @c WindowAckSize message.
                 */
                Error setLocalWindowAckSize(int bytes);

                /** @brief Peer-advertised window-ack-size, or @c 0 until received. */
                int  peerWindowAckSize() const { return _peerWindowAckSize.value(); }

                // ---- Counters ----

                /** @brief Cumulative bytes written to the device. */
                int64_t bytesSent() const     { return _bytesSent.value(); }

                /** @brief Cumulative bytes read from the device. */
                int64_t bytesReceived() const { return _bytesReceived.value(); }

                /** @brief Total bytes-received as of the last @c Acknowledgement we emitted. */
                int64_t lastAckBytesAcked() const { return _lastAckBytesSent.value(); }

                // ---- Signals ----

                /**
                 * @brief Emitted for every Protocol Control Message
                 *        received (after the chunk layer applies any
                 *        side-effect like raising @ref peerChunkSize).
                 * @signal
                 */
                PROMEKI_SIGNAL(controlMessageReceived, RtmpMessage);

                /** @brief Emitted whenever the peer raises its chunk size. @signal */
                PROMEKI_SIGNAL(peerChunkSizeChanged, int);

                /**
                 * @brief Emitted on @c Acknowledgement messages from
                 *        the peer, carrying the cumulative byte count.
                 * @signal
                 */
                PROMEKI_SIGNAL(peerAck, uint32_t);

        private:
                /**
                 * @brief Per-cs-id sender state.  Used to decide the
                 *        most compressive header type and to drive
                 *        type-3 continuation.
                 */
                struct WriteState {
                        uint32_t timestamp = 0;     ///< Last absolute timestamp emitted.
                        uint32_t delta = 0;         ///< Last delta emitted.
                        uint32_t messageLength = 0;
                        uint8_t  messageTypeId = 0;
                        uint32_t messageStreamId = 0;
                        bool     established = false;  ///< False until the first type-0 header on this CS-id.
                };

                /**
                 * @brief Per-cs-id receiver state.  Carries reassembly
                 *        buffer and the prior-header values needed to
                 *        decode type-1 / 2 / 3 chunks.
                 */
                struct ReadState {
                        uint32_t timestamp = 0;
                        uint32_t delta = 0;
                        uint32_t messageLength = 0;
                        uint8_t  messageTypeId = 0;
                        uint32_t messageStreamId = 0;
                        bool     extendedTimestamp = false;  ///< Was the prior header extended?
                        bool     established = false;
                        Buffer   reassembly;        ///< In-flight message payload.
                        uint32_t reassemblyBytes = 0;  ///< Bytes accumulated so far.
                };

                // ---- Internal helpers ----

                Error writeBytes(const uint8_t *data, size_t len);
                Error readBytesExact(uint8_t *data, size_t len, unsigned int timeoutMs);
                Error sendSetChunkSize(int bytes);
                Error sendWindowAckSize(int bytes);
                Error sendAck(uint32_t cumulative);
                void  handleControl(const RtmpMessage &msg);

                int  defaultCsidForType(RtmpMessage::Type type) const;

                IODevice *_device;

                Atomic<int32_t> _localChunkSize{DefaultChunkSize};
                Atomic<int32_t> _peerChunkSize{DefaultChunkSize};
                Atomic<int32_t> _localWindowAckSize{DefaultWindowAckSize};
                Atomic<int32_t> _peerWindowAckSize{0};

                Atomic<int64_t> _bytesSent{0};
                Atomic<int64_t> _bytesReceived{0};
                Atomic<int64_t> _lastAckBytesSent{0};

                Mutex _writeMutex;      ///< Guards _writeStates + serializes writeMessage.
                Mutex _readMutex;       ///< Guards _readStates.
                HashMap<uint32_t, WriteState> _writeStates;
                HashMap<uint32_t, ReadState>  _readStates;
};

PROMEKI_NAMESPACE_END
