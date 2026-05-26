/**
 * @file      rtmpsession.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>

#include <promeki/amf0.h>
#include <promeki/atomic.h>
#include <promeki/enums_rtmp.h>
#include <promeki/error.h>
#include <promeki/fourcc.h>
#include <promeki/hashmap.h>
#include <promeki/list.h>
#include <promeki/metadata.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/result.h>
#include <promeki/rtmpchunkstream.h>
#include <promeki/rtmphandshake.h>
#include <promeki/rtmpmessage.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief Configurable parameters for @c NetConnection.connect.
 * @ingroup network
 *
 * Defaults are tuned to match the FMLE / OBS / FFmpeg-compatibility
 * profile that every mainstream RTMP destination accepts.  Override
 * individual fields when targeting a destination with non-default
 * requirements (e.g. set @c flashVer when emulating a specific
 * encoder fingerprint, or add fourccs to @c fourCcList when extending
 * Enhanced RTMP coverage).
 */
struct RtmpConnectOptions {
                /** @brief Application name (URL path's leading segment).  Required. */
                String       app;
                /** @brief Full RTMP URL the connect message advertises (`tcUrl` field). */
                String       tcUrl;
                /** @brief Optional `pageUrl` field — useful for analytics on the server side. */
                String       pageUrl;
                /** @brief Optional `swfUrl` field. */
                String       swfUrl;
                /** @brief `flashVer` field.  Empty = use the library's default user-agent. */
                String       flashVer;
                /** @brief Connection type — `"nonprivate"` matches FMLE / OBS. */
                String       type = "nonprivate";
                /** @brief Object encoding — `0` = AMF0 (default), `3` = AMF3 (not supported). */
                int          objectEncoding = 0;
                /** @brief Capabilities bitmask — `239` matches FMLE. */
                int          capabilities = 239;
                /** @brief Bitmask of accepted audio codec IDs.  `0x0FFF` matches FMLE. */
                int          audioCodecs = 0x0FFF;
                /** @brief Bitmask of accepted video codec IDs.  `0x00FF` matches FMLE. */
                int          videoCodecs = 0x00FF;
                /** @brief Bitmask of supported video functions.  `1` matches FMLE. */
                int          videoFunction = 1;
                /**
                 * @brief enhanced-rtmp.org `fourCcList` — supported FourCC video codecs.
                 *
                 * Sent as a strict-array of strings in the connect
                 * AMF0 object.  Defaults to `"hvc1"` (HEVC); add
                 * `"vp09"` / `"av01"` once those encoder backends
                 * ship.  Pass an empty list to suppress the field
                 * (some legacy servers reject unknown connect-object
                 * fields).
                 */
                List<FourCC> fourCcList = { FourCC("hvc1") };
};

/**
 * @brief User-facing RTMP protocol session: handshake + chunk-stream + AMF0 commands.
 * @ingroup network
 *
 * @c RtmpSession is the top of the standalone-protocol stack — it
 * owns an @ref RtmpHandshake and an @ref RtmpChunkStream and drives
 * them through the RTMP 1.0 connect / publish / play flows.  The
 * session is transport-agnostic: callers attach any @ref IODevice
 * (TCP, TLS, in-process pipe) once it's connected and the session
 * handles every protocol exchange above that.
 *
 * @par Mirroring @c RtpSession
 *
 * Mirrors @ref RtpSession in spirit: no MediaIO indirection, no
 * thread of its own (the caller drives the read loop), and a flat
 * mix of synchronous high-level helpers (@ref connect, @ref publish,
 * @ref play) and raw access (@ref sendMessage, @ref readMessage).
 * The high-level helpers block on the transaction's reply; the raw
 * access methods are non-blocking on the protocol layer (they still
 * block on the underlying @c IODevice the chunk stream reads from).
 *
 * @par Connect-flow ordering
 *
 * The session enforces the message order every FMS-clone server
 * (Wowza, nginx-rtmp, YouTube, Twitch) demands:
 *
 *   1. **Client → Server:** @c connect on chunk-stream-id 3,
 *      message-stream-id 0.
 *   2. **Server → Client:** @c WindowAckSize → @c SetPeerBandwidth
 *      → @c UserControl(StreamBegin) → @c _result for connect.
 *   3. **Client → Server:** @c WindowAckSize echoing the server's
 *      value, then @c SetChunkSize raising local chunk size.
 *
 * @par Status-code → Error mapping
 *
 * The session maps every well-known @c onStatus code to a specific
 * @ref Error per the project's "specific errors" preference.  See
 * @ref onStatusToError for the table.  The raw @ref Amf0Value
 * status object is still surfaced via the @ref onStatus signal so
 * callers wanting the original code string (logging, telemetry) can
 * inspect it; the typed @c Error is just the routed-back value of
 * the synchronous @c connect / @c publish / @c play call.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  The session is
 * designed for the standard "writer + reader" thread topology of
 * @ref RtmpClient: one thread calls @ref sendMessage / outbound
 * helpers; another calls @ref readMessage.  The handshake and the
 * inline pieces of @c connect / @c createStream / @c publish /
 * @c play are synchronous from the calling thread and must not run
 * concurrently with a separate reader thread.  Once the session is
 * in the steady-state media-pumping phase, dedicated reader +
 * writer threads can drive @ref readMessage / @ref sendMessage in
 * parallel.
 */
class RtmpSession : public ObjectBase {
                PROMEKI_OBJECT(RtmpSession, ObjectBase)
        public:
                /** @brief Default protocol-control chunk size we raise to after connect. */
                static constexpr int DefaultPostConnectChunkSize = 60000;

                /**
                 * @brief Constructs a session for @p role.
                 *
                 * No device is attached; call @ref attach with an
                 * already-connected @ref IODevice (or an in-process
                 * pipe for testing) before driving the handshake.
                 */
                explicit RtmpSession(RtmpRole role, ObjectBase *parent = nullptr);

                /** @brief Destructor.  Does not close the attached device. */
                ~RtmpSession() override;

                /**
                 * @brief Binds @p device as the transport.
                 *
                 * @c device must already be open and connected.  The
                 * session does not own the device.  Calling
                 * @c attach a second time replaces the binding.
                 */
                Error attach(IODevice *device);

                /** @brief Returns the attached @ref IODevice, or @c nullptr. */
                IODevice *device() const { return _device; }

                /** @brief Returns the underlying chunk-stream layer. */
                RtmpChunkStream *chunkStream() { return _chunk.get(); }

                /**
                 * @brief Drives @ref RtmpHandshake to completion.
                 *
                 * Reads from / writes to the attached device,
                 * blocking up to @p timeoutMs.
                 */
                Error performHandshake(unsigned int timeoutMs = 10000);

                // ----- High-level commands (client side) -----

                /**
                 * @brief Issues @c NetConnection.connect and waits
                 *        for the reply.
                 *
                 * Sends the connect command on chunk-stream-id 3
                 * with message-stream-id 0 and the AMF0 command
                 * object built from @p opts.  Blocks until the
                 * server replies @c _result or @c _error within
                 * @p timeoutMs.  Returns the @ref Error mapped from
                 * the server's status code (or @ref Error::Timeout
                 * if no reply arrives).
                 */
                Error connect(const RtmpConnectOptions &opts, unsigned int timeoutMs = 10000);

                /**
                 * @brief Issues @c createStream and returns the
                 *        message-stream-id the server allocated.
                 */
                Result<uint32_t> createStream(unsigned int timeoutMs = 5000);

                /**
                 * @brief Issues @c publish and waits for
                 *        @c NetStream.Publish.Start.
                 *
                 * @param streamId  Message-stream-id from @ref createStream.
                 * @param streamKey Last path segment of the destination URL.
                 * @param mode      @c "live" / @c "record" / @c "append".
                 */
                Error publish(uint32_t streamId, const String &streamKey,
                              const String &mode = "live", unsigned int timeoutMs = 5000);

                /**
                 * @brief Issues @c play and waits for @c NetStream.Play.Start.
                 *
                 * @param streamId Message-stream-id from @ref createStream.
                 * @param streamKey Last path segment of the destination URL.
                 * @param start     Per Adobe RTMP §7.2.2.7: @c -2 = live,
                 *                  @c -1 = recorded-then-live fallback,
                 *                  @c 0 = recorded from beginning,
                 *                  positive values = seek offset (seconds).
                 * @param duration  @c -1 = play until end / unpublish;
                 *                  positive = stop after this many seconds.
                 */
                Error play(uint32_t streamId, const String &streamKey,
                           double start = -2.0, double duration = -1.0,
                           unsigned int timeoutMs = 5000);

                /**
                 * @brief Issues @c deleteStream on @p streamId.
                 *
                 * Fire-and-forget: does not wait for a reply.
                 */
                Error deleteStream(uint32_t streamId);

                /**
                 * @brief Issues the FMS-flavoured @c releaseStream
                 *        command — required by some FMS-clone
                 *        servers before @c publish.
                 *
                 * Fire-and-forget: many servers reply with an
                 * @c _error which we silently discard.
                 */
                Error releaseStream(const String &streamKey);

                /**
                 * @brief Issues @c FCPublish — required by some
                 *        FMS-clone servers as a precondition to
                 *        @c publish.  Fire-and-forget.
                 */
                Error fcPublish(const String &streamKey);

                /**
                 * @brief Issues @c FCUnpublish — paired teardown for
                 *        @c FCPublish.  Fire-and-forget.
                 */
                Error fcUnpublish(const String &streamKey);

                /**
                 * @brief Issues @c FCSubscribe — required by some
                 *        Wowza configurations before @c play.
                 *        Fire-and-forget.
                 */
                Error fcSubscribe(const String &streamKey);

                // ----- Raw access -----

                /**
                 * @brief Sends a fully-formed message via the chunk
                 *        layer.  Used by the writer thread once the
                 *        session is in the media-pump phase.
                 */
                Error sendMessage(const RtmpMessage &m);

                /**
                 * @brief Drains one inbound message from the chunk
                 *        layer.
                 *
                 * Protocol-control messages are auto-applied by the
                 * chunk layer before the message is returned;
                 * user-control messages are inspected by the session
                 * (PingRequest → PingResponse echo, StreamBegin
                 * dispatch) before the call returns.  Audio / video
                 * / data / command messages flow through unchanged
                 * so the caller's reader thread can dispatch them.
                 */
                Result<RtmpMessage> readMessage(unsigned int timeoutMs = 0);

                /**
                 * @brief Maps an @c onStatus AMF0 @c code field to an
                 *        @ref Error.
                 *
                 * Used internally by @c connect / @c publish /
                 * @c play; exposed so callers writing custom
                 * command-flow code (e.g. server-side handlers) can
                 * reuse the same table.
                 */
                static Error onStatusToError(const String &code);

                // ----- Signals -----

                /** @brief Fires once @ref performHandshake completes. @signal */
                PROMEKI_SIGNAL(handshakeComplete);

                /** @brief Fires once @ref connect succeeds. @signal */
                PROMEKI_SIGNAL(connected);

                /**
                 * @brief Fires when the connection terminates
                 *        unexpectedly during the connect-flow phase.
                 * @signal
                 */
                PROMEKI_SIGNAL(connectionFailed, Error);

                /**
                 * @brief Fires on every successful @ref createStream
                 *        reply, carrying the allocated message-stream-id.
                 * @signal
                 */
                PROMEKI_SIGNAL(streamCreated, uint32_t);

                /**
                 * @brief Fires on @c NetStream.Publish.Start, carrying
                 *        the message-stream-id.
                 * @signal
                 */
                PROMEKI_SIGNAL(publishStarted, uint32_t);

                /**
                 * @brief Fires on @c NetStream.Play.Start, carrying
                 *        the message-stream-id.
                 * @signal
                 */
                PROMEKI_SIGNAL(playStarted, uint32_t);

                /**
                 * @brief Fires for every @c onStatus AMF0 message
                 *        received.  The full status object is the
                 *        argument; callers wanting the raw `code`
                 *        / `description` strings inspect it
                 *        directly.
                 * @signal
                 */
                PROMEKI_SIGNAL(onStatus, Amf0Value);

                /**
                 * @brief Fires for every @c onMetaData AMF0 message
                 *        received.  The argument is the parsed
                 *        @ref Metadata.
                 * @signal
                 */
                PROMEKI_SIGNAL(onMetaData, Metadata);

                /**
                 * @brief Fires for every audio @ref RtmpMessage drained
                 *        from the chunk layer.
                 * @signal
                 */
                PROMEKI_SIGNAL(audioMessageReceived, RtmpMessage);

                /**
                 * @brief Fires for every video @ref RtmpMessage drained
                 *        from the chunk layer.
                 * @signal
                 */
                PROMEKI_SIGNAL(videoMessageReceived, RtmpMessage);

                /** @brief Fires when the transport drops. @signal */
                PROMEKI_SIGNAL(disconnected);

        private:
                /** @brief Pending AMF0 transaction awaiting a `_result` / `_error`. */
                struct PendingTransaction {
                        Error    result = Error::Timeout;
                        bool     completed = false;
                        Amf0Value commandObject;   ///< Snapshot of the reply body for inspection.
                        Amf0Value info;             ///< The 4th AMF0 value in the reply (info object).
                        /**
                         * @brief Message-stream-id we expect the reply
                         *        to arrive on, or 0 if the reply is
                         *        correlated purely by transaction id.
                         *
                         * Required for @c publish / @c play because
                         * the server's @c onStatus reply carries
                         * @c txnId = 0 per RTMP §7.2.2 — correlation
                         * has to fall back to the msid the command
                         * was issued on.  @c connect /
                         * @c createStream leave this at 0 because
                         * their replies (@c _result on the
                         * NetConnection csid) correlate by txnId.
                         */
                        uint32_t expectedMsid = 0;
                        /**
                         * @brief Last command name we registered this
                         *        transaction for ("publish", "play",
                         *        "connect", "createStream").  Used for
                         *        diagnostic logging when a reply is
                         *        unmatched or fails.
                         */
                        String   commandName;
                };

                Error    sendCommand(uint32_t csid, uint32_t msid,
                                     const String &command, double txnId,
                                     const Amf0Value::List &args);
                Error    awaitTransaction(double txnId, unsigned int timeoutMs,
                                          PendingTransaction *outTxn = nullptr);
                Error    handleInboundCommand(const RtmpMessage &msg);
                Error    handleInboundUserControl(const RtmpMessage &msg);
                Error    handleInboundData(const RtmpMessage &msg);
                void     applyConnectFlowDefaults();

                /**
                 * @brief Pumps the chunk layer once and dispatches any
                 *        protocol or command messages until we either
                 *        match @p txnId or @p timeoutMs elapses.
                 */
                Error    pumpUntilTransaction(double txnId, unsigned int timeoutMs,
                                              PendingTransaction *outTxn);

                /** @brief Returns the next AMF0 transaction id to use. */
                double   nextTransactionId();

                IODevice              *_device = nullptr;
                RtmpRole               _role;
                RtmpHandshake          _handshake;
                UniquePtr<RtmpChunkStream> _chunk;

                Atomic<double>         _nextTxnId{1.0};

                Mutex                       _txnMutex;
                HashMap<double, PendingTransaction *> _pending;

                bool _connected = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
