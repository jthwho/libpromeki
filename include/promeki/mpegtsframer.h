/**
 * @file      mpegtsframer.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/function.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mpegts.h>
#include <promeki/mpegtsmuxer.h>
#include <promeki/mpegtsdemuxer.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Frame-level glue for MPEG-TS muxing and demuxing.
 * @ingroup proav
 *
 * @c MpegTsFramer is the transport-agnostic bridge between the
 * project's @ref Frame data model and the bytestream side of
 * @ref MpegTsMuxer / @ref MpegTsDemuxer.  It owns one muxer and one
 * demuxer (lazily instantiated based on direction) plus the logic for
 * mapping @ref CompressedVideoPayload / @ref CompressedAudioPayload
 * payloads onto MPEG-TS @c stream_type values and back.
 *
 * @par What @c MpegTsFramer does
 *
 * - Resolves codec → @c stream_type / @c PesStreamId for each payload.
 * - Allocates PIDs (one video, one audio in v1) and registers them
 *   with the muxer at first sight or from the configured pendingMediaDesc.
 * - Computes per-frame PTS from the configured frame rate when
 *   payloads don't already carry one.
 * - Probes the first H.264 / HEVC keyframe per video PID via
 *   @ref H264Bitstream::parseSpsResolution /
 *   @ref HevcDecoderConfig::parseSpsResolution so emitted Frames
 *   carry real @c ImageDesc dimensions instead of @c (0, 0).
 * - Stamps each emitted payload's pts via @ref MediaTimeStamp built
 *   from the PES PTS in 90 kHz.
 *
 * @par What @c MpegTsFramer does NOT do
 *
 * - Does not own a transport.  Callers wire @ref writeFrame's emit
 *   callback to a @c File, @c SrtSocketTransport, or anything else
 *   that accepts byte runs; on the read side they push bytes from
 *   whatever source via @ref pushBytes.
 * - Does not handle @ref Frame transport scheduling — the caller's
 *   read loop drives @ref pushBytes at whatever rate suits the
 *   transport.
 * - Does not synthesise NULL packets for CBR shaping — that knob
 *   lives directly on the underlying @ref MpegTsMuxer.
 *
 * @par Lifecycle
 *
 * 1. Construct.
 * 2. Configure via @ref setVideoPid, @ref setAudioPid,
 *    @ref setProgramNumber, @ref setPatPmtIntervalMs,
 *    @ref setPcrIntervalMs, @ref setMuxRateBps, @ref setAacFraming.
 *    All of these proxy through to the lazily-created muxer.
 * 3. Optional: pre-declare the stream shape via @ref configureStreams.
 *    This avoids needing a write-call to discover the codec.
 * 4. Call @ref writeFrame per Frame (writer mode) or
 *    @ref pushBytes / @ref flushReader / @ref drainFrames (reader
 *    mode).
 *
 * Both modes can coexist on one instance — the muxer and demuxer are
 * independent and only the one in use is materialised.
 *
 * @par Thread Safety
 * Single-threaded.  Owned by one strand at a time.
 */
class MpegTsFramer {
        public:
                /**
                 * @brief Selects how AAC is framed on the wire.
                 *
                 * - @c Adts — @c stream_type @c 0x0F, the payload's
                 *   bytes ride verbatim (ADTS sync words and all).
                 * - @c Latm — @c stream_type @c 0x11, LOAS/LATM
                 *   framing.  The library's encoders emit ADTS today
                 *   so the writer will (for now) still emit ADTS
                 *   bytes when @c Latm is selected — only the
                 *   PMT @c stream_type changes.  Full transcoding
                 *   between framings is a follow-up.
                 */
                enum class AacFraming {
                        Adts,
                        Latm
                };

                /** @brief Emit callback shape (same as @ref MpegTsMuxer::EmitCallback). */
                using EmitCallback = MpegTsMuxer::EmitCallback;

                /**
                 * @brief Callback invoked once per reassembled Frame
                 *        during @ref pushBytes / @ref flushReader.
                 */
                using FrameCallback = Function<Error(Frame &&)>;

                MpegTsFramer();
                ~MpegTsFramer();

                MpegTsFramer(const MpegTsFramer &) = delete;
                MpegTsFramer &operator=(const MpegTsFramer &) = delete;

                // ---- Common configuration --------------------------

                /** @brief Sets the video PID (default @c MpegTs::DefaultVideoPid). */
                void setVideoPid(uint16_t v) { _videoPid = v; }

                /** @brief Returns the configured video PID. */
                uint16_t videoPid() const { return _videoPid; }

                /** @brief Sets the audio PID (default @c MpegTs::DefaultAudioPid). */
                void setAudioPid(uint16_t v) { _audioPid = v; }

                /** @brief Returns the configured audio PID. */
                uint16_t audioPid() const { return _audioPid; }

                /** @brief Sets the program number (default @c MpegTs::DefaultProgramNumber). */
                void setProgramNumber(uint16_t v) { _programNumber = v; }

                /** @brief Returns the configured program number. */
                uint16_t programNumber() const { return _programNumber; }

                /** @brief Sets the PMT PID (default @c MpegTs::DefaultPmtPid). */
                void setPmtPid(uint16_t v) { _pmtPid = v; }

                /** @brief Returns the configured PMT PID. */
                uint16_t pmtPid() const { return _pmtPid; }

                /** @brief Sets the PAT/PMT interval in ms (default 100). */
                void setPatPmtIntervalMs(int v) { _patPmtIntervalMs = v; }

                /** @brief Returns the PAT/PMT interval in ms. */
                int patPmtIntervalMs() const { return _patPmtIntervalMs; }

                /** @brief Sets the PCR interval in ms (default 20). */
                void setPcrIntervalMs(int v) { _pcrIntervalMs = v; }

                /** @brief Returns the PCR interval in ms. */
                int pcrIntervalMs() const { return _pcrIntervalMs; }

                /** @brief Sets the CBR target in bits/sec (0 = disabled). */
                void setMuxRateBps(int64_t v) { _muxRateBps = v; }

                /** @brief Returns the configured CBR target. */
                int64_t muxRateBps() const { return _muxRateBps; }

                /** @brief Sets the AAC framing mode (default ADTS). */
                void setAacFraming(AacFraming v) { _aacFraming = v; }

                /** @brief Returns the AAC framing mode. */
                AacFraming aacFraming() const { return _aacFraming; }

                /**
                 * @brief Sets the writer-side default frame rate.
                 *
                 * Used to synthesise PES PTS values for video access
                 * units whose payload doesn't carry an explicit pts.
                 * Default: 30 fps.
                 */
                void setWriterFrameRate(const FrameRate &fps) { _writerFrameRate = fps; }

                /** @brief Returns the writer-side default frame rate. */
                const FrameRate &writerFrameRate() const { return _writerFrameRate; }

                /**
                 * @brief Optionally pre-declares the streams the writer
                 *        will emit, so the PAT / PMT are correct from
                 *        the very first packet.
                 *
                 * Walks @p desc's image and audio lists, mapping
                 * compressed @ref PixelFormat / @ref AudioFormat
                 * values onto MPEG-TS @c stream_type values and
                 * registering them with the muxer.  Non-compressed
                 * descriptors are ignored.  Idempotent — re-calling
                 * with the same shape is a no-op.
                 */
                Error configureStreams(const MediaDesc &desc);

                /** @brief Returns true once a video stream has been registered. */
                bool haveVideoStream() const { return _haveVideoStream; }

                /** @brief Returns true once an audio stream has been registered. */
                bool haveAudioStream() const { return _haveAudioStream; }

                // ---- Writer side -----------------------------------

                /**
                 * @brief Forces the next @ref writeFrame to re-emit
                 *        PAT and PMT.  Proxies @ref MpegTsMuxer::forcePatPmt.
                 */
                void forcePatPmt();

                /**
                 * @brief Marks the next access unit on @p pid as
                 *        discontinuous.  Proxies
                 *        @ref MpegTsMuxer::markNextAccessUnitDiscontinuous.
                 */
                Error markNextAccessUnitDiscontinuous(uint16_t pid);

                /**
                 * @brief Mux one @ref Frame and emit the resulting TS
                 *        bytes via @p emit.
                 *
                 * Iterates the frame's video payloads (first
                 * @ref CompressedVideoPayload only in v1) and audio
                 * payloads (every @ref CompressedAudioPayload), routes
                 * each through the muxer.
                 *
                 * @return @c Error::Ok on success or the first non-Ok
                 *         error the muxer / @p emit returned.
                 */
                Error writeFrame(const Frame &frame, const EmitCallback &emit);

                /**
                 * @brief Returns the number of access units (across
                 *        all streams) the framer has muxed so far.
                 */
                int64_t writerAccessUnitsEmitted() const { return _writerAccessUnitsEmitted; }

                // ---- Reader side -----------------------------------

                /**
                 * @brief Replaces the per-Frame callback fired during
                 *        @ref pushBytes / @ref flushReader.
                 *
                 * Each reassembled access unit produces a single
                 * Frame with one @ref CompressedVideoPayload or
                 * @ref CompressedAudioPayload.  Video / audio streams
                 * flow as independent Frames in the order the demuxer
                 * produces them.
                 */
                void setFrameCallback(FrameCallback cb) { _frameCallback = std::move(cb); }

                /**
                 * @brief Pushes raw TS bytes into the demuxer.
                 *
                 * Equivalent to @c demuxer().push(data).  Any complete
                 * access units that fall out are wrapped in
                 * @ref Frame and delivered via the frame callback set
                 * by @ref setFrameCallback (or enqueued for
                 * @ref drainFrames if no callback is installed).
                 */
                Error pushBytes(const BufferView &data);

                /**
                 * @brief Drains any in-flight unbounded video PES.
                 *
                 * Call once after the producer has signalled end of
                 * stream (file EOF, peer close).  Any final video AU
                 * that was waiting on the next-PUSI sentinel is
                 * flushed through the frame pipeline.
                 */
                Error flushReader();

                /**
                 * @brief Drains queued Frames into @p out and returns
                 *        the number moved.
                 *
                 * Only meaningful when @ref setFrameCallback was not
                 * installed — the framer keeps an internal queue of
                 * Frames when no callback is present.  When a
                 * callback is installed the queue stays empty and
                 * this returns 0.
                 *
                 * @param out  Receives the drained frames in
                 *             arrival order.  Appended to existing
                 *             contents.
                 * @return The number of frames moved.
                 */
                size_t drainFrames(Frame::List &out);

                /** @brief Direct access to the underlying muxer (lazy-allocated). */
                MpegTsMuxer *muxer();

                /** @brief Direct access to the underlying demuxer (lazy-allocated). */
                MpegTsDemuxer *demuxer();

        private:
                MpegTsMuxer   *ensureMuxer();
                MpegTsDemuxer *ensureDemuxer();
                Error          registerVideoStream(MpegTs::StreamType st, uint32_t registrationFormat,
                                                   const ImageDesc &imageDesc);
                Error          registerAudioStream(MpegTs::StreamType st, uint32_t registrationFormat,
                                                   const AudioDesc &audioDesc);
                Error          handleAccessUnit(const MpegTsDemuxer::AccessUnit &au);
                void           probeVideoDimensions(uint16_t pid, MpegTs::StreamType st, const BufferView &au);

                // Configuration.
                uint16_t   _videoPid = MpegTs::DefaultVideoPid;
                uint16_t   _audioPid = MpegTs::DefaultAudioPid;
                uint16_t   _pmtPid = MpegTs::DefaultPmtPid;
                uint16_t   _programNumber = MpegTs::DefaultProgramNumber;
                int        _patPmtIntervalMs = 100;
                int        _pcrIntervalMs = 20;
                int64_t    _muxRateBps = 0;
                AacFraming _aacFraming = AacFraming::Adts;
                FrameRate  _writerFrameRate;

                // Lazy components.
                UniquePtr<MpegTsMuxer>   _muxer;
                UniquePtr<MpegTsDemuxer> _demuxer;

                // Writer state.
                bool     _haveVideoStream = false;
                bool     _haveAudioStream = false;
                uint64_t _writerFrameIndex = 0;
                uint64_t _writerAudioPts90k = 0;
                int64_t  _writerAccessUnitsEmitted = 0;
                // SMPTE 302M block phase counter — wraps 0..191
                // (one AES3 block = 192 frames).  Carries the F-bit
                // phase across @ref writeFrame calls so we stamp the
                // first AES3 subframe of every block correctly.
                uint32_t _writer302MBlockPhase = 0;

                // Reader state.
                FrameCallback        _frameCallback;
                Frame::List          _readQueue;
                Map<uint16_t, ImageDesc> _readerImageDesc; // per video PID, learned from SPS.
                Map<uint16_t, bool>      _readerDimsProbed;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
