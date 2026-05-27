/**
 * @file      rtmpmessage.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One RTMP message ("PDU") carried across an @ref RtmpChunkStream.
 * @ingroup network
 *
 * RtmpMessage is a transport-layer value type — the unit of work the
 * RTMP chunk layer hands to / takes from the session layer.  It
 * carries a type-id, the message-stream id the peer expects, a
 * 32-bit millisecond timestamp, an opaque @ref Buffer payload, and an
 * optional chunk-stream-id hint that biases @ref RtmpChunkStream
 * placement for outbound messages.
 *
 * The payload @ref Buffer is shared by value (copies are cheap
 * refcount bumps); message-handling code that needs a unique copy
 * calls @c ensureExclusive on the buffer before mutating.
 *
 * @par Conventional chunk-stream-ids
 *
 * RTMP doesn't formally pin types to chunk-stream-ids, but every
 * mainstream implementation (FFmpeg, OBS, nginx-rtmp, librtmp)
 * follows the same convention so receivers can predict which CS-id
 * to expect different traffic on:
 *
 *   - **2**  — Protocol control messages (SetChunkSize / Ack /
 *              WindowAckSize / etc.).  Message-stream-id is also @c 0.
 *   - **3**  — AMF0 / AMF3 commands and `onMetaData` script data.
 *   - **4**  — Audio.
 *   - **5**  — User-control messages (some servers).
 *   - **6**  — Video.
 *
 * Outbound messages may set @ref chunkStreamId to override the
 * convention; @c 0 means "let @ref RtmpChunkStream pick".  Inbound
 * messages always report the CS-id they arrived on.
 *
 * @par Thread Safety
 * RtmpMessage is a plain value type — distinct instances may be used
 * concurrently.  Concurrent mutation of a single instance must be
 * externally synchronized.  Copying is cheap: the @ref Buffer is a
 * refcount-incremented handle.
 */
class RtmpMessage {
        public:
                /**
                 * @brief RTMP message type-ids (Adobe RTMP §5.4 + §6.3).
                 *
                 * Values 1-6 are Protocol Control Messages — they
                 * always travel on chunk-stream-id 2 with
                 * message-stream-id 0 and adjust the chunk-layer
                 * itself.  8, 9, and 18 are the workhorse media /
                 * data types.  AMF3 variants (15-17) exist for
                 * completeness; we send AMF0 (18 / 20) by default.
                 */
                enum Type : uint8_t {
                        SetChunkSize       = 1,
                        AbortMessage       = 2,
                        Acknowledgement    = 3,
                        UserControl        = 4,
                        WindowAckSize      = 5,
                        SetPeerBandwidth   = 6,
                        AudioMessage       = 8,
                        VideoMessage       = 9,
                        DataMessageAmf3    = 15,
                        SharedObjectAmf3   = 16,
                        CommandMessageAmf3 = 17,
                        DataMessageAmf0    = 18,
                        SharedObjectAmf0   = 19,
                        CommandMessageAmf0 = 20,
                        AggregateMessage   = 22
                };

                /** @brief Convenience list type. */
                using List = ::promeki::List<RtmpMessage>;

                /** @brief Message type-id. */
                Type     type = AudioMessage;

                /**
                 * @brief Message-stream-id (RTMP §5.4).
                 *
                 * Always @c 0 for Protocol Control Messages.  The
                 * server allocates a non-zero id in response to
                 * @c createStream and the client carries that id on
                 * audio / video / data messages for the stream.
                 *
                 * Note: this is the only field in the entire RTMP
                 * wire format encoded little-endian.
                 */
                uint32_t streamId = 0;

                /**
                 * @brief 32-bit millisecond timestamp.
                 *
                 * Monotonically increasing within a stream.  Wraps
                 * after ~49.7 days; the chunk layer's
                 * extended-timestamp escape carries the full 32-bit
                 * value once the 24-bit base field overflows.
                 */
                uint32_t timestamp = 0;

                /**
                 * @brief Message body.
                 *
                 * The chunk layer fragments the payload across one or
                 * more chunks on the writer side and reassembles the
                 * fragments back into a single contiguous @ref Buffer
                 * on the reader side.  Payload size is implicit in
                 * @c payload.size().
                 */
                Buffer payload;

                /**
                 * @brief Chunk-stream-id hint.
                 *
                 * On the writer side, @c 0 means "use the default for
                 * @ref type" (see the conventional mapping above);
                 * any non-zero value pins the message to a specific
                 * CS-id.  On the reader side, this field reports the
                 * actual CS-id the message arrived on.
                 */
                uint32_t chunkStreamId = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
