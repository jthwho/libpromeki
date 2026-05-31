/**
 * @file      mpegtsmuxer.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mpegts.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Single-program MPEG-2 Transport Stream muxer.
 * @ingroup proav
 *
 * Wraps the bottom-layer @ref MpegTs framing helpers in a stateful
 * encoder that:
 *
 *  - Maintains per-PID @c continuity_counter values across calls.
 *  - Periodically emits PAT and PMT sections at a configurable
 *    interval (default @c 100 ms, per ETSI TR 101 290 §5.1.2 / §5.2.1
 *    recommended minimums).
 *  - Periodically inserts the Program Clock Reference (PCR) into the
 *    adaptation field of the configured PCR-carrier PID at the
 *    @ref setPcrIntervalMs cadence (default @c 40 ms).
 *  - Packetises each access unit into one PES + N transport packets,
 *    stuffing the final packet's adaptation field to fill 188 bytes.
 *  - Sets the @c random_access_indicator in the adaptation field on
 *    keyframe packets so downstream tools that re-MUX or seek can
 *    locate IDRs without parsing the elementary stream.
 *
 * The muxer emits a sequence of 188-byte transport packets to a
 * caller-supplied @ref EmitCallback.  A single call to
 * @ref writeAccessUnit may invoke @c emit up to three times — once
 * for the PAT if it is due, once for the PMT if it is due, and once
 * for the packetised access unit itself.  Every invocation passes a
 * contiguous buffer whose size is a multiple of 188.
 *
 * @par Lifecycle
 *
 * 1. Construct.
 * 2. Configure identifiers / intervals via @ref setProgramNumber,
 *    @ref setPmtPid, @ref setPcrPid, etc.
 * 3. Register elementary streams via @ref addStream (one call per PID).
 * 4. Repeat @ref writeAccessUnit per encoded frame.
 * 5. Optionally call @ref flush to emit a NULL packet (rarely useful
 *    for file writers — required only for callers that need to keep
 *    a CBR wire rate alive between bursts).
 *
 * @par Thread Safety
 * Single-threaded — owned by one producer at a time.  Concurrent
 * access requires external synchronisation.
 *
 * @par Example
 * @code
 * MpegTsMuxer mux;
 * mux.setProgramNumber(1);
 * mux.setPmtPid(0x1000);
 * mux.setPcrPid(0x100);
 * mux.addStream(0x100, MpegTs::StreamTypeH264);
 * mux.addStream(0x101, MpegTs::StreamTypeAacAdts);
 *
 * File out("out.ts");
 * out.open(IODevice::WriteOnly, File::Create | File::Truncate);
 *
 * auto emit = [&out](const BufferView &v) -> Error {
 *         int64_t n = out.write(v.data(), v.size());
 *         return (n < 0 || static_cast<size_t>(n) != v.size())
 *                 ? Error::IOError : Error::Ok;
 * };
 * mux.writeAccessUnit(0x100, videoAnnexB, ptsForVideo, ptsForVideo,
 *                     isIdr, emit);
 * mux.writeAccessUnit(0x101, aacAdts, ptsForAudio, ptsForAudio,
 *                     true, emit);
 * @endcode
 */
class MpegTsMuxer {
        public:
                /**
                 * @brief Callback invoked with one or more packed
                 *        188-byte transport packets.
                 *
                 * @c view is always single-slice and its @c size() is
                 * a positive multiple of @c MpegTs::PacketSize.  The
                 * callback may return any non-Ok @ref Error to abort
                 * the muxer's current operation; the error propagates
                 * back through @ref writeAccessUnit unchanged.
                 */
                using EmitCallback = Function<Error(const BufferView &)>;

                /** @brief Constructs a muxer with default identifiers and intervals. */
                MpegTsMuxer();

                ~MpegTsMuxer();

                /**
                 * @brief Sets the 16-bit @c transport_stream_id placed
                 *        in every PAT.
                 *
                 * Default: @c 1.
                 */
                void setTransportStreamId(uint16_t v);

                /** @brief Returns the configured @c transport_stream_id. */
                uint16_t transportStreamId() const { return _transportStreamId; }

                /**
                 * @brief Sets the 16-bit @c program_number placed in
                 *        the PAT and the PMT.
                 *
                 * Default: @c MpegTs::DefaultProgramNumber (= 1).
                 */
                void setProgramNumber(uint16_t v);

                /** @brief Returns the configured @c program_number. */
                uint16_t programNumber() const { return _programNumber; }

                /**
                 * @brief Sets the PID carrying the PMT.
                 *
                 * Default: @c MpegTs::DefaultPmtPid (= 0x1000).
                 */
                void setPmtPid(uint16_t v);

                /** @brief Returns the configured PMT PID. */
                uint16_t pmtPid() const { return _pmtPid; }

                /**
                 * @brief Sets the PID whose adaptation field carries
                 *        the PCR.
                 *
                 * The PCR is placed in the adaptation field of packets
                 * already being emitted for that PID — no dedicated
                 * PCR-only packets are produced.  Must match a PID
                 * registered via @ref addStream; @ref writeAccessUnit
                 * returns @c Error::IdNotFound when the chosen PCR PID
                 * is not a registered stream.
                 *
                 * Default: @c MpegTs::DefaultVideoPid.  Set explicitly
                 * after the first @ref addStream call when the video
                 * PID differs from the default.
                 */
                void setPcrPid(uint16_t v);

                /** @brief Returns the configured PCR-carrier PID. */
                uint16_t pcrPid() const { return _pcrPid; }

                /**
                 * @brief Sets the minimum interval, in milliseconds,
                 *        between consecutive PAT / PMT emissions.
                 *
                 * The muxer always emits the PAT and PMT before the
                 * first elementary-stream packet, and re-emits both
                 * whenever this interval has elapsed since the last
                 * emission.  Negative values are clamped to zero (=
                 * re-emit before every access unit).
                 *
                 * Default: @c 100 ms (ETSI TR 101 290 §5.1.2 / §5.2.1
                 * recommended minimum).
                 */
                void setPatPmtIntervalMs(int v);

                /** @brief Returns the PAT / PMT interval in ms. */
                int patPmtIntervalMs() const { return _patPmtIntervalMs; }

                /**
                 * @brief Sets the minimum interval, in milliseconds,
                 *        between consecutive PCR insertions on the
                 *        PCR-carrier PID.
                 *
                 * Default: @c 40 ms (ETSI TR 101 290 §5.4.1 allows up
                 * to 100 ms; 40 ms matches what most muxers default
                 * to and corresponds to one 25 fps frame).
                 */
                void setPcrIntervalMs(int v);

                /** @brief Returns the PCR interval in ms. */
                int pcrIntervalMs() const { return _pcrIntervalMs; }

                /**
                 * @brief Registers an elementary stream.
                 *
                 * Must be called before the first @ref writeAccessUnit
                 * for the PID.  Re-registering an existing PID
                 * returns @c Error::AlreadyExists.  At least one
                 * stream must be registered before any access unit
                 * may be muxed.
                 *
                 * @param pid          The 13-bit PID for this stream
                 *                     (1..@c MpegTs::MaxPid, excluding
                 *                     the reserved range @c 0..0x000F).
                 * @param streamType   The MPEG-TS @c stream_type value
                 *                     (@ref MpegTs::StreamType).
                 * @param descriptors  Optional raw @c ES_info descriptor
                 *                     bytes to embed in the PMT entry.
                 *                     May be empty / invalid.
                 * @param kind         How to classify this stream when
                 *                     choosing a PES @c stream_id range
                 *                     and PCR/PES timing.  Defaults to
                 *                     @ref MpegTs::StreamKind::Auto,
                 *                     which derives from @p streamType
                 *                     for the unambiguous codecs.  Must
                 *                     be set explicitly for
                 *                     @ref MpegTs::StreamTypePrivatePes
                 *                     (e.g. SMPTE 302M / Opus → Audio,
                 *                     AV1 → Video) since the PES
                 *                     @c stream_id range differs by
                 *                     kind.
                 * @return @c Error::Ok on success.
                 */
                Error addStream(uint16_t pid, MpegTs::StreamType streamType,
                                const BufferView &descriptors = BufferView(),
                                MpegTs::StreamKind kind = MpegTs::StreamKind::Auto);

                /**
                 * @brief Returns the list of currently registered
                 *        elementary-stream PIDs in registration order.
                 */
                List<uint16_t> registeredPids() const;

                /**
                 * @brief Returns @c true when @p pid has been
                 *        registered via @ref addStream.
                 */
                bool hasStream(uint16_t pid) const;

                /**
                 * @brief Mux one access unit and emit the resulting
                 *        TS packets via @p emit.
                 *
                 * Emission ordering: PAT (when due) → PMT (when due)
                 * → the access unit's PES + payload packets.
                 *
                 * @c PES @c PTS_DTS_flags is set to @c '11' when
                 * @p dts90k differs from @p pts90k, and to @c '10'
                 * otherwise — i.e. callers that have no separate DTS
                 * pass the same value for both.
                 *
                 * The @c random_access_indicator in the adaptation
                 * field of the first transport packet for this access
                 * unit is set when @p isKeyframe is true; the muxer
                 * has no opinion on what "keyframe" means per codec —
                 * that judgement belongs to the producer.
                 *
                 * @param pid         The PID this access unit belongs to.
                 *                    Must have been previously registered.
                 * @param payload     Access-unit bytes (e.g. H.264
                 *                    Annex-B AU, AAC ADTS frame).  Must
                 *                    be valid; empty payloads are not
                 *                    permitted.
                 * @param pts90k      33-bit Presentation Timestamp in
                 *                    90 kHz ticks.
                 * @param dts90k      33-bit Decode Timestamp in 90 kHz
                 *                    ticks.  Pass the same value as
                 *                    @p pts90k when there is no
                 *                    separate DTS.
                 * @param isKeyframe  @c true sets the random-access
                 *                    indicator and (on the PCR PID)
                 *                    forces a PCR emission regardless
                 *                    of interval.
                 * @param emit        Receives the resulting transport
                 *                    packets.
                 * @return @c Error::Ok on success, the callback's
                 *         error if @p emit returns non-Ok, or
                 *         @c Error::IdNotFound / @c Error::InvalidArgument
                 *         for misconfigured arguments.
                 */
                Error writeAccessUnit(uint16_t pid, const BufferView &payload, uint64_t pts90k, uint64_t dts90k,
                                      bool isKeyframe, const EmitCallback &emit);

                /**
                 * @brief Forces the next @ref writeAccessUnit call to
                 *        re-emit PAT and PMT, regardless of interval.
                 *
                 * Useful when the caller has just changed a PMT entry
                 * (added a stream, etc.) and wants the receiver to
                 * see the update on the next packet.
                 */
                void forcePatPmt();

                /**
                 * @brief Sets the adaptation-field
                 *        @c discontinuity_indicator on the next TS
                 *        packet emitted for @p pid.
                 *
                 * Signals that the continuity counter for @p pid is
                 * about to make an unexpected jump, or that the
                 * stream's PCR is about to be discontinuous (e.g.
                 * after an encoder restart or a downstream seek).
                 * ISO/IEC 13818-1 §2.4.3.4 — when the indicator is
                 * set, downstream receivers must not flag the CC
                 * jump or PCR jump as an error.
                 *
                 * The flag is one-shot per PID: it is cleared after
                 * the next packet for that PID carries it.
                 *
                 * @param pid The PID whose next packet will carry
                 *            the indicator.  Must have been
                 *            registered via @ref addStream.
                 * @return @c Error::Ok on success or
                 *         @c Error::IdNotFound if @p pid is unknown.
                 */
                Error markNextAccessUnitDiscontinuous(uint16_t pid);

                /**
                 * @brief Emits a single NULL packet (PID = 0x1FFF) via
                 *        @p emit.
                 *
                 * Convenience helper for callers that need to keep
                 * the wire rate alive between bursts.  No-op-safe to
                 * call repeatedly.
                 */
                Error emitNullPacket(const EmitCallback &emit);

                /**
                 * @brief Sets the constant-bit-rate target in bits
                 *        per second.
                 *
                 * When non-zero, the muxer counts the bytes emitted
                 * by each @ref writeAccessUnit and tops up the wire
                 * with NULL packets (PID @c 0x1FFF) so the running
                 * average matches the configured rate.  This is
                 * useful for downstream stages that expect a CBR
                 * stream (some hardware decoders, transport-rate-
                 * limited links) and as a deterministic floor for
                 * SRT live mode.
                 *
                 * The accounting is anchored to the per-call DTS so
                 * the rate is computed against elementary-stream
                 * time, not wall-clock time — there is no separate
                 * clock to drift against.  A negative argument is
                 * clamped to @c 0 (= disabled).  Default: @c 0
                 * (disabled).
                 *
                 * @param bps Target rate in bits per second.
                 */
                void setMuxRateBps(int64_t bps);

                /** @brief Returns the configured CBR target. */
                int64_t muxRateBps() const { return _muxRateBps; }

        private:
                struct StreamRec {
                                uint16_t           pid = 0;
                                MpegTs::StreamType streamType = MpegTs::StreamTypeReserved;
                                MpegTs::StreamKind kind = MpegTs::StreamKind::Auto;
                                uint8_t            pesStreamId = 0;
                                Buffer             descriptors;
                                uint8_t            continuityCounter = 0;
                                bool               pendingDiscontinuity = false;
                };

                StreamRec       *findStream(uint16_t pid);
                const StreamRec *findStream(uint16_t pid) const;
                uint8_t          pickPesStreamId(MpegTs::StreamType streamType, MpegTs::StreamKind kind,
                                                 size_t indexOfKind) const;

                Error emitPatPmtIfDue(uint64_t now27mhz, bool force, const EmitCallback &emit);
                Error writePsiSectionPacket(uint16_t pid, const Buffer &section, uint8_t &cc, const EmitCallback &emit);

                uint16_t _transportStreamId = 1;
                uint16_t _programNumber = MpegTs::DefaultProgramNumber;
                uint16_t _pmtPid = MpegTs::DefaultPmtPid;
                uint16_t _pcrPid = MpegTs::DefaultVideoPid;
                int      _patPmtIntervalMs = 100;
                int      _pcrIntervalMs = 20;
                uint8_t  _patVersion = 0;
                uint8_t  _pmtVersion = 0;
                uint8_t  _patCc = 0;
                uint8_t  _pmtCc = 0;

                List<StreamRec> _streams;
                size_t          _audioStreamCount = 0; // counts ADTS/LATM/AC3/MP2 streams for stream_id allocation.
                size_t          _videoStreamCount = 0;

                bool     _forcePatPmt = true; // emit on the first writeAccessUnit call.
                bool     _havePsiTime = false;
                bool     _havePcrTime = false;
                uint64_t _lastPsiTime27mhz = 0;
                uint64_t _lastPcrTime27mhz = 0;

                // CBR padding state.
                int64_t  _muxRateBps = 0;
                bool     _haveCbrAnchor = false;
                uint64_t _cbrAnchor27mhz = 0;
                int64_t  _bytesSinceAnchor = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
