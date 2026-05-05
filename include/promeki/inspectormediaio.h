/**
 * @file      inspectormediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdio>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/audiodatadecoder.h>
#include <promeki/imagedataencoder.h>
#include <promeki/imagedatadecoder.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/audiobuffer.h>
#include <promeki/buffer.h>
#include <promeki/framerate.h>
#include <promeki/timecode.h>
#include <promeki/mutex.h>
#include <promeki/list.h>
#include <promeki/ltcdecoder.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One detected discontinuity in the inspector's frame stream.
 * @ingroup proav
 *
 * Inspector continuity tracking compares the previous frame's
 * picture-side metadata against the current frame's, and emits one
 * @ref InspectorDiscontinuity for every property that changed in an
 * unexpected way.  The triple @c (kind, previousValue, currentValue)
 * is intended to be self-describing — the @c description field is a
 * pre-rendered human-readable string suitable for logging.
 */
struct InspectorDiscontinuity {
                /** @brief Categorical type of the discontinuity. */
                enum Kind {
                        /// Frame number from the picture data band did not advance by 1.
                        FrameNumberJump,
                        /// Stream ID from the picture data band changed between frames.
                        StreamIdChange,
                        /// Picture timecode did not advance by 1 frame compared to the
                        /// previous frame.
                        PictureTcJump,
                        /// LTC timecode did not advance by 1 frame compared to the
                        /// previous frame.
                        LtcTcJump,
                        /// Picture data band failed to decode (CRC mismatch / sync
                        /// error / etc.).  After at least one previous successful
                        /// decode this counts as a continuity event because the
                        /// stream's picture-side identity disappeared mid-flight.
                        ImageDataDecodeFailure,
                        /// LTC failed to decode in this frame's audio after a
                        /// previously-successful decode.
                        LtcDecodeFailure,
                        /// The marker-based A/V sync offset (audio
                        /// codeword position vs the rational-rate-
                        /// predicted ideal, baseline-anchored on the
                        /// first match) moved by more than the
                        /// configured tolerance from the previous
                        /// frame.  Audio and video are locked in pro
                        /// workflows, so any change from a
                        /// previously-stable offset is a real fault
                        /// rather than a measurement artifact.
                        SyncOffsetChange,
                        /// Video essence arrived without a valid MediaTimeStamp
                        /// even though the Timestamp test is enabled (MediaIO
                        /// guarantees one on every essence).
                        MissingVideoTimestamp,
                        /// Audio essence arrived without a valid MediaTimeStamp
                        /// even though the Timestamp test is enabled.
                        MissingAudioTimestamp,
                        /// The audio @ref MediaTimeStamp on an incoming chunk
                        /// diverged from the prediction (audio stream anchor
                        /// + cumulative samples × sample period) by more than
                        /// the configured tolerance.  The inspector re-anchors
                        /// the audio stream timeline on the new PTS so that
                        /// subsequent wall-clock derivations remain accurate.
                        AudioTimestampReanchor,
                        /// The video @ref MediaTimeStamp on this frame diverged
                        /// from the prediction (video anchor + frame index ×
                        /// frame duration) by more than the configured
                        /// tolerance.  The inspector re-anchors the video
                        /// timeline on the new PTS.
                        VideoTimestampReanchor,
                        /// An audio channel's @ref AudioDataEncoder
                        /// marker decoded successfully but the channel
                        /// byte in the codeword (bits 48..55) did not
                        /// match the channel index the inspector pulled
                        /// it from.  Indicates the audio path swapped
                        /// or duplicated channels.
                        AudioChannelMismatch,
                        /// An @ref AudioDataEncoder codeword was
                        /// detected (sync nibble located, samples-
                        /// per-bit measured) but failed final
                        /// validation — either the recovered sync
                        /// byte didn't match @c 0xA or the recovered
                        /// CRC didn't match the recomputed CRC.
                        /// Indicates the audio path scrambled the
                        /// codeword bits (lossy codec, gain
                        /// distortion, sample drop).
                        AudioDataDecodeFailure,
                        /// An @ref AudioDataEncoder codeword was
                        /// detected but its measured sample length
                        /// (76 × samplesPerBit) deviated by more
                        /// than the inspector's tolerance from the
                        /// expected length.  Normal SRC round-trip
                        /// drift sits well inside the tolerance;
                        /// firing this discontinuity means the audio
                        /// path is rate-shifting the codeword
                        /// substantially.
                        AudioDataLengthAnomaly,
                };

                Kind   kind;          ///< Which kind of discontinuity this is.
                String previousValue; ///< Pre-rendered "previous" snapshot.
                String currentValue;  ///< Pre-rendered "current" snapshot.
                String description;   ///< Full human-readable description (kind + values).
};

/**
 * @brief Per-frame measurement record produced by @ref InspectorMediaIO.
 * @ingroup proav
 *
 * The inspector populates one of these for every frame it sees and
 * delivers it to the user-supplied callback (set via
 * @ref InspectorMediaIO::setEventCallback).  Fields are only
 * meaningful when the corresponding decoder was enabled in the
 * config — check @c pictureDecoded / @c ltcDecoded before reading.
 */
struct InspectorEvent {
                /// Inspector's monotonic per-frame counter (0-based, increments
                /// once per @c executeCmd(MediaIOCommandWrite&) call).
                FrameNumber frameIndex{0};

                // ---- Picture data band decode ----

                /// True if the picture data band was decoded for this frame.
                bool pictureDecoded = false;
                /// True if the *image-data decoder enable* flag was set in the
                /// config — distinguishes "decoder disabled, no result expected"
                /// from "decoder enabled but the data band could not be read".
                bool pictureDecoderEnabled = false;
                /// Frame number recovered from the first 64-bit data band
                /// (low 32 bits of the @c (streamID << 32) | frameNumber word).
                uint32_t pictureFrameNumber = 0;
                /// Stream ID recovered from the first 64-bit data band (high
                /// 32 bits of the same word).
                uint32_t pictureStreamId = 0;
                /// Timecode recovered from the second 64-bit data band, decoded
                /// via @ref Timecode::fromBcd64.
                Timecode pictureTimecode;

                // ---- Audio LTC decode ----

                /// True if at least one LTC frame was recovered from this
                /// frame's audio chunk.
                bool ltcDecoded = false;
                /// True if the *LTC decoder enable* flag was set in the config.
                bool ltcDecoderEnabled = false;
                /// Most recent timecode recovered from the audio LTC stream.
                Timecode ltcTimecode;
                /// Absolute audio sample position in the inspector's stream
                /// where the LTC sync word arrived.  Sample 0 is the very
                /// first audio sample the inspector ever ingested; the
                /// per-stream wall-clock time of any sample @c N is
                /// @c audioStreamStartNs + N / sampleRate (in seconds).
                int64_t ltcSampleStart = 0;

                // ---- A/V Sync (marker-based, baseline-anchored) ----

                /// True if the inspector was able to compute an A/V sync
                /// offset for this frame: an audio codeword decoded
                /// successfully, the picture data band on a frame
                /// matching its 48-bit frame number is in history, and
                /// the upstream frame rate is known.
                bool avSyncValid = false;
                /// Instantaneous A/V sync offset, in audio samples, of the
                /// @ref AudioDataEncoder codeword carrying this frame's
                /// 48-bit frame number, reported as a *deviation* from a
                /// baseline phase the inspector latches on the first
                /// successful marker match.  The raw phase is the
                /// difference between the codeword's actual
                /// stream-sample position and the rational-rate-
                /// predicted position
                /// (@ref FrameRate::cumulativeTicks at the inspector's
                /// audio sample rate); the reported value is that raw
                /// phase minus the baseline.
                ///
                /// The baseline absorbs any constant phase the producer
                /// happened to start with — a mid-stream join, a
                /// non-zero first frame number, an SRC's constant
                /// group delay, or any other one-time offset between
                /// the producer's audio cadence and the rational
                /// ideal.  None of those are real A/V sync errors, so
                /// a clean cadenced run reports 0 every frame
                /// regardless of rate, starting frame, or pipeline
                /// shape.  Real changes in the codeword position
                /// relative to its expected rational position
                /// (codeword moved within a chunk, audio sample
                /// dropped/inserted, audio path resampling drift)
                /// still show up because they move the raw phase
                /// away from the latched baseline.
                ///
                /// Sign convention: positive = audio codeword landed
                /// at a *later* stream sample than the baseline
                /// predicts, which means the audio is delayed
                /// relative to the video — i.e. video leads audio.
                /// Negative = audio leads video.
                ///
                /// @note Both encoders stamp the same
                /// @c [stream:8][channel:8][frame:48] codeword, so the
                /// frame-number lookup is sample-accurate end-to-end
                /// when the audio path doesn't resample.  Any
                /// frame-to-frame movement beyond
                /// @ref MediaConfig::InspectorSyncOffsetToleranceSamples
                /// is flagged as an
                /// @ref InspectorDiscontinuity::SyncOffsetChange — and
                /// because both the cadence and the baseline phase
                /// are already removed, the tolerance defaults to 0.
                int64_t avSyncOffsetSamples = 0;

                // ---- Timestamps (per-essence MediaTimeStamps) ----

                /// True if the *timestamp test enable* flag was set in the config.
                bool timestampTestEnabled = false;
                /// True if the frame's first image carried a valid MediaTimeStamp.
                bool videoTimestampValid = false;
                /// True if the frame's first audio chunk carried a valid MediaTimeStamp.
                bool audioTimestampValid = false;
                /// Video @ref MediaTimeStamp converted to nanoseconds
                /// (@c timeStamp.nanoseconds() + @c offset.nanoseconds()).
                int64_t videoTimestampNs = 0;
                /// Audio @ref MediaTimeStamp converted to nanoseconds.
                int64_t audioTimestampNs = 0;
                /// True if @c videoTimestampDeltaNs is meaningful (i.e. the
                /// previous frame also had a valid video timestamp).  False on
                /// the very first timestamped frame and across any gap.
                bool videoTimestampDeltaValid = false;
                /// True if @c audioTimestampDeltaNs is meaningful.
                bool audioTimestampDeltaValid = false;
                /// Nanoseconds between this frame's video timestamp and the
                /// previous frame's.  Only valid when
                /// @c videoTimestampDeltaValid is true.
                int64_t videoTimestampDeltaNs = 0;
                /// Nanoseconds between this frame's audio timestamp and the
                /// previous frame's.
                int64_t audioTimestampDeltaNs = 0;

                // ---- Audio sample count ----

                /// True if the *audio samples test enable* flag was set in the config.
                bool audioSamplesTestEnabled = false;
                /// True if the frame carried an audio chunk (from which the
                /// per-frame sample count was read).
                bool audioSamplesValid = false;
                /// Number of audio samples carried by this frame's first audio chunk.
                /// With bursty network audio this is a *delivery shape* statistic
                /// — it reports how the upstream chose to slice the audio across
                /// video frames, not how many samples logically belong to this
                /// frame's slice of time.  Per-stream cadence is reported via
                /// @ref InspectorSnapshot::measuredAudioSampleRate.
                int64_t audioSamplesThisFrame = 0;

                // ---- Per-essence PTS jitter (continuous-stream model) ----

                /// True when this frame received an audio chunk and a
                /// prediction was available (any frame after the first that
                /// carries an audio MediaTimeStamp).  @ref audioPtsJitterNs
                /// is meaningful only when this is @c true.
                bool audioPtsJitterValid = false;
                /// Difference (actual - predicted) between the audio chunk's
                /// MediaTimeStamp and the inspector's prediction based on the
                /// audio stream anchor and cumulative sample count.  Positive
                /// means the chunk arrived later in wall-clock time than the
                /// stream's sample cadence would predict; negative means
                /// earlier.
                int64_t audioPtsJitterNs = 0;
                /// True when the video PTS jitter measurement is meaningful
                /// (any frame after the first that carries a video PTS).
                bool videoPtsJitterValid = false;
                /// Difference (actual - predicted) between this frame's video
                /// MediaTimeStamp and the inspector's prediction based on the
                /// video anchor and the frame index.
                int64_t videoPtsJitterNs = 0;

                // ---- A/V cross-essence PTS drift ----

                /// True when both essences carried valid PTSs on this frame
                /// AND a baseline (videoPts - audioPts) has been established
                /// by an earlier frame.
                bool avPtsDriftValid = false;
                /// Drift of @c (videoPts - audioPts) from the baseline offset
                /// captured on the first frame where both essences carried
                /// valid PTSs.  A constant zero indicates the two essence
                /// clocks share a common reference and stay locked; a
                /// drifting value indicates the audio and video clocks are
                /// running at slightly different rates.
                int64_t avPtsDriftNs = 0;

                // ---- Per-channel audio data marker decode ----

                /// One per-channel decoded @ref AudioDataEncoder
                /// marker.  Populated on every frame where the audio
                /// data decode test is enabled and at least one audio
                /// chunk arrived; the list length equals the audio
                /// stream's channel count.
                struct AudioChannelMarker {
                                /// True if at least one codeword decoded
                                /// successfully on this channel during the
                                /// current frame (CRC + sync nibble both
                                /// valid).  When @c true the @c streamId,
                                /// @c encodedChannel, and @c frameNumber
                                /// fields reflect the most recent successful
                                /// decode.
                                bool decoded = false;
                                /// True when @c decoded is true and the
                                /// channel byte in the marker matches the
                                /// channel index the inspector read it from.
                                bool channelMatches = false;
                                /// Stream ID byte (bits 56..63 of the
                                /// codeword).
                                uint8_t streamId = 0;
                                /// Channel byte (bits 48..55 of the
                                /// codeword) — the value the encoder
                                /// stamped, which should equal the
                                /// channel index this entry came from.
                                uint8_t encodedChannel = 0;
                                /// Frame number (bits 0..47 of the
                                /// codeword).
                                uint64_t frameNumber = 0;
                                /// Total codewords completed on this
                                /// channel during the current frame
                                /// (counts both successes and failures —
                                /// any decoded packet that the streaming
                                /// decoder emitted).
                                uint32_t packetsDecoded = 0;
                                /// Codewords that decoded with a CRC or
                                /// sync-nibble error.  Subset of
                                /// @c packetsDecoded.
                                uint32_t packetsCorrupt = 0;
                                /// Stream-absolute sample position of the
                                /// most recently completed codeword's
                                /// leading edge.  Sample 0 is the very
                                /// first sample the inspector ever pushed
                                /// through the AudioData stream state.
                                int64_t streamSampleStart = -1;
                };

                /// True if the *audio data decoder enable* flag was
                /// set in the config.
                bool audioDataDecoderEnabled = false;
                /// True if at least one channel produced a successful
                /// audio data marker decode this frame.
                bool audioDataDecoded = false;
                /// One @ref AudioChannelMarker per channel, indexed
                /// by channel.  Empty when the test is disabled or
                /// no audio chunk arrived on this frame.
                List<AudioChannelMarker> audioChannelMarkers;

                // ---- Continuity ----

                /// Discontinuities detected on this specific frame, with both
                /// previous and current values pre-rendered for logging.
                List<InspectorDiscontinuity> discontinuities;
};

/**
 * @brief Aggregate snapshot of the inspector's accumulated state.
 * @ingroup proav
 *
 * @ref InspectorMediaIO::snapshot returns one of these on demand.
 * Counters are monotonic over the inspector's lifetime; the
 * @c lastEvent field is the most recently produced
 * @ref InspectorEvent (a copy, taken under the inspector's mutex).
 */
struct InspectorSnapshot {
                /// Total number of frames the inspector has processed.
                FrameCount framesProcessed{0};
                /// Total number of frames where the picture data band decoded.
                FrameCount framesWithPictureData{0};
                /// Total number of frames where at least one LTC frame decoded.
                FrameCount framesWithLtc{0};
                /// Total number of frames where the video MediaTimeStamp was valid.
                FrameCount framesWithVideoTimestamp{0};
                /// Total number of frames where the audio MediaTimeStamp was valid.
                FrameCount framesWithAudioTimestamp{0};
                /// Number of consecutive-pair video timestamp deltas measured.
                int64_t videoDeltaSamples = 0;
                /// Number of consecutive-pair audio timestamp deltas measured.
                int64_t audioDeltaSamples = 0;
                /// Minimum observed frame-to-frame video timestamp delta (ns).
                int64_t videoDeltaMinNs = 0;
                /// Maximum observed frame-to-frame video timestamp delta (ns).
                int64_t videoDeltaMaxNs = 0;
                /// Running mean of frame-to-frame video timestamp delta (ns).
                double videoDeltaAvgNs = 0.0;
                /// Minimum observed frame-to-frame audio timestamp delta (ns).
                int64_t audioDeltaMinNs = 0;
                /// Maximum observed frame-to-frame audio timestamp delta (ns).
                int64_t audioDeltaMaxNs = 0;
                /// Running mean of frame-to-frame audio timestamp delta (ns).
                double audioDeltaAvgNs = 0.0;
                /// Actual observed frames-per-second, derived from
                /// @c videoDeltaAvgNs (@c 0 if not yet measured).
                double actualFps = 0.0;
                /// Number of frames that contributed an audio sample count.
                int64_t audioSamplesFrames = 0;
                /// Minimum observed per-frame audio sample count.
                int64_t audioSamplesMin = 0;
                /// Maximum observed per-frame audio sample count.
                int64_t audioSamplesMax = 0;
                /// Running mean of per-frame audio sample count.
                double audioSamplesAvg = 0.0;
                /// Total audio samples counted across every frame that
                /// carried an audio chunk with a valid MediaTimeStamp anchor
                /// (excludes the very first such frame, which is used only
                /// as the anchor).
                int64_t audioSamplesTotal = 0;
                /// Elapsed audio MediaTimeStamp time (ns) from the first
                /// anchored frame to the most recent anchored frame.
                int64_t audioSamplesSpanNs = 0;
                /// Measured audio sample rate (Hz), derived from
                /// @c audioSamplesTotal divided by @c audioSamplesSpanNs.
                /// @c 0 until at least two anchored frames have been seen.
                double measuredAudioSampleRate = 0.0;
                /// Total number of discontinuities of any kind, summed over all
                /// frames.
                int64_t totalDiscontinuities = 0;

                // ---- Per-essence PTS jitter accumulators (jitter is
                // measured against the prediction; see
                // @ref InspectorEvent::audioPtsJitterNs and
                // @ref InspectorEvent::videoPtsJitterNs) ----

                /// Number of audio chunks that contributed a jitter measurement.
                int64_t audioPtsJitterSamples = 0;
                /// Minimum observed audio PTS jitter (ns).
                int64_t audioPtsJitterMinNs = 0;
                /// Maximum observed audio PTS jitter (ns).
                int64_t audioPtsJitterMaxNs = 0;
                /// Running mean of the audio PTS jitter (ns).
                double audioPtsJitterAvgNs = 0.0;
                /// Number of times the audio stream had to be re-anchored
                /// because the PTS diverged from the prediction beyond the
                /// configured tolerance.
                int64_t audioReanchorCount = 0;

                /// Number of frames that contributed a video PTS jitter measurement.
                int64_t videoPtsJitterSamples = 0;
                /// Minimum observed video PTS jitter (ns).
                int64_t videoPtsJitterMinNs = 0;
                /// Maximum observed video PTS jitter (ns).
                int64_t videoPtsJitterMaxNs = 0;
                /// Running mean of the video PTS jitter (ns).
                double videoPtsJitterAvgNs = 0.0;
                /// Number of times the video stream had to be re-anchored.
                int64_t videoReanchorCount = 0;

                // ---- A/V cross-stream PTS drift accumulators ----

                /// True once a baseline @c (videoPts - audioPts) has been
                /// captured (first frame where both essences carry a valid PTS).
                bool avBaselineSet = false;
                /// Baseline @c (videoPts - audioPts) in nanoseconds — captured
                /// on the first frame where both essences carry valid PTSs.
                /// Subsequent drift measurements report deviations from this
                /// value.
                int64_t avBaselineOffsetNs = 0;
                /// Number of frames that contributed an A/V drift measurement.
                int64_t avPtsDriftSamples = 0;
                /// Minimum observed deviation from the baseline (ns).
                int64_t avPtsDriftMinNs = 0;
                /// Maximum observed deviation from the baseline (ns).
                int64_t avPtsDriftMaxNs = 0;
                /// Running mean of the A/V drift (ns).
                double avPtsDriftAvgNs = 0.0;

                /// True if @c lastEvent is populated (at least one frame has
                /// been processed).
                bool hasLastEvent = false;
                /// Most recent per-frame event.
                InspectorEvent lastEvent;
};

/**
 * @brief MediaIO sink backend that decodes and validates a media stream.
 * @ingroup proav
 *
 * The inverse of @ref TpgMediaIO &mdash; where the TPG @em produces
 * test-pattern frames, the inspector @em consumes frames and runs a
 * configurable set of checks on each one.  The set of checks is
 * driven by @ref MediaConfig::InspectorTests — an EnumList of
 * @ref InspectorTest values.  The default carries every check that
 * the @ref TpgMediaIO default produces signal for; @c Ltc and
 * @c CaptureStats are off by default and have to be opted in via
 * the config key.  Currently supported tests:
 *
 *  - @c ImageData   — pulls the two 64-bit payloads written by
 *    @ref ImageDataEncoder out of the top of every frame,
 *    recovering the rolling frame number, stream ID, and BCD
 *    timecode.  *Default-on.*
 *  - @c AudioData   — runs @ref AudioDataDecoder over every audio
 *    channel and recovers the @c [stream:8][channel:8][frame:48]
 *    codeword stamped by the @c PcmMarker test pattern.  Flags
 *    any channel whose encoded channel byte differs from its
 *    actual index as an
 *    @ref InspectorDiscontinuity::AudioChannelMismatch.
 *    *Default-on.*
 *  - @c Continuity  — tracks frame number, stream ID, picture
 *    timecode, and LTC timecode from one frame to the next, and
 *    flags any unexpected jump as a @ref InspectorDiscontinuity
 *    record on the per-frame event.  *Default-on.*
 *  - @c Timestamp   — checks that the per-essence
 *    @ref MediaTimeStamp is valid on every frame, records the
 *    frame-to-frame delta (min / max / avg) for video and audio,
 *    and reports the actual observed FPS.  *Default-on.*
 *  - @c AudioSamples — tracks per-frame audio sample count (min /
 *    max / avg) and derives the measured audio sample rate by
 *    dividing cumulative samples by the elapsed audio
 *    @ref MediaTimeStamp span.  *Default-on.*
 *  - @c Ltc         — runs the selected audio channel through
 *    @ref LtcDecoder and reports any timecodes recovered.
 *    *Off by default* — opt in when the upstream is configured
 *    to put @c AudioPattern::LTC on a channel.
 *  - @c AvSync      — with @c ImageData and @c AudioData both
 *    running, computes the per-frame A/V sync offset directly from
 *    the shared frame-number marker the two encoders stamp.  The
 *    audio side reports the stream-absolute sample position of the
 *    codeword carrying frame N; the inspector compares it against
 *    the rational-rate-predicted ideal and reports the deviation
 *    from a baseline phase latched on the first match.  A constant
 *    offset (always 0) is healthy; any movement flags a
 *    discontinuity.  *Default-on* — implicitly turns on
 *    @c ImageData and @c AudioData when enabled.
 *
 * Dependency relationships are auto-resolved at open time: enabling
 * @c AvSync implicitly turns on @c ImageData and @c AudioData;
 * enabling @c Continuity implicitly turns on @c ImageData.  The user
 * does not have to specify the upstream decoders explicitly.
 *
 * @par Per-frame event delivery
 * The inspector exposes its results in three complementary ways:
 *
 *  1. **Per-frame callback** — set via @ref setEventCallback.  Invoked
 *     once per frame from the worker thread; the callback receives a
 *     fully-populated @ref InspectorEvent and must be thread-safe.
 *  2. **Accumulator snapshot** — @ref snapshot returns a thread-safe
 *     copy of the running totals plus the most recent event.  Useful
 *     for "polled" consumers and for tests.
 *  3. **Periodic log line** — at the cadence configured by
 *     @ref MediaConfig::InspectorLogIntervalSec (default 1.0 s of
 *     wall time), the inspector emits a one-line summary via
 *     @c promekiInfo.  Set the interval to @c 0 to disable.
 *
 * @par Construction pattern
 * Because @ref setEventCallback must be called before @c open(),
 * callers that want a callback construct @ref InspectorMediaIO
 * directly (rather than via @ref MediaIO::create) so they can wire
 * the callback up before opening:
 *
 * @code
 * auto *insp = new InspectorMediaIO();
 * insp->setEventCallback([](const InspectorEvent &e) {
 *     // ... runs on the worker thread
 * });
 *
 * MediaIO::Config cfg = MediaIOFactory::defaultConfig("Inspector");
 * // Default is every test; narrow the list to run a subset:
 * EnumList tests = EnumList::forType<InspectorTest>();
 * tests.append(InspectorTest::AvSync);
 * tests.append(InspectorTest::Continuity);
 * cfg.set(MediaConfig::InspectorTests, tests);
 * insp->setConfig(cfg);
 * insp->open().wait();
 *
 * // Pump frames in:
 * insp->sink(0)->writeFrame(frame);
 * @endcode
 *
 * Callers that don't need the callback can use the standard factory
 * path: @c MediaIO::create("Inspector", ...) and poll @c snapshot()
 * via the typed task pointer (or just rely on the periodic log).
 *
 * @par Frame disposition
 * The inspector is a sink: by default frames are dropped after the
 * checks run (@ref MediaConfig::InspectorDropFrames defaults to
 * @c true).  This is what makes it usable as the terminal stage of a
 * pipeline.  Disabling the drop is currently a no-op since there is
 * no downstream — the option exists for future tee-style wrappers.
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 */
class InspectorMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(InspectorMediaIO, SharedThreadMediaIO)
        public:
                /// Per-frame callback signature.
                using EventCallback = std::function<void(const InspectorEvent &)>;

                /** @brief Constructs an inspector. */
                InspectorMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~InspectorMediaIO() override;

                /**
                 * @brief Sets the per-frame event callback.
                 *
                 * Must be called before the inspector is opened — calling
                 * it on a running inspector is a data race.  Pass an empty
                 * @c std::function to clear.
                 *
                 * @param cb Callback invoked once per frame on the
                 *           worker thread with a fully-populated
                 *           @ref InspectorEvent.
                 */
                void setEventCallback(EventCallback cb);

                /**
                 * @brief Returns a thread-safe snapshot of the accumulator.
                 *
                 * Safe to call from any thread, including while the
                 * inspector is processing frames.  The returned snapshot
                 * is a value copy taken under the inspector's mutex.
                 */
                InspectorSnapshot snapshot() const;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

        private:
                void  decompressImages(Frame &frame);
                void  initDecoders(const Frame &frame);
                void  ingestAudio(const Frame &frame, InspectorEvent &event);
                void  runImageDataCheck(const Frame &frame, InspectorEvent &event);
                void  runAudioDataCheck(const Frame &frame, InspectorEvent &event);
                void  runLtcCheck(InspectorEvent &event);
                void  runAvSyncCheck(const Frame &frame, InspectorEvent &event);
                void  runContinuityCheck(InspectorEvent &event);
                void  runTimestampCheck(const Frame &frame, InspectorEvent &event);
                void  runAudioSamplesCheck(const Frame &frame, InspectorEvent &event);
                void  runCaptureStats(const Frame &frame, const InspectorEvent &event);
                Error openCaptureStatsFile(const String &configured);
                void  closeCaptureStatsFile();
                void  emitPeriodicLogIfDue();
                void  resetState();

                // ---- Configuration (resolved at open time) ----
                bool    _decodeImageData = false;
                bool    _decodeAudioData = false;
                bool    _decodeLtc = false;
                bool    _checkAvSync = false;
                bool    _checkContinuity = false;
                bool    _checkTimestamp = false;
                bool    _checkAudioSamples = false;
                bool    _checkCaptureStats = false;
                bool    _dropFrames = true;
                int     _imageDataRepeatLines = 16;
                int     _ltcChannel = 0;
                int     _syncOffsetToleranceSamples = 0;
                int64_t _audioPtsToleranceNs = 5'000'000;
                int64_t _videoPtsToleranceNs = 5'000'000;
                double  _logIntervalSec = 1.0;
                bool    _isOpen = false;
                /// Nominal video frame duration in nanoseconds (cached at
                /// open() from the upstream MediaDesc's frame rate).  Used
                /// by the video PTS prediction.
                double _videoFrameDurationNs = 0.0;
                /// Upstream frame rate (rational form — 30000/1001 for
                /// NTSC etc.).  Used by the marker-based A/V sync check
                /// to predict the rational audio sample cadence via
                /// @ref FrameRate::cumulativeTicks, so the reported
                /// offset is cadence-free even on fractional rates.
                /// Seeded from @c pendingMediaDesc at open time and
                /// re-latched from @ref Metadata::FrameRate on each
                /// frame so sources that learn the real rate after
                /// open (e.g. NDI receivers, which default to a 30/1
                /// placeholder until the first SDK frame arrives) end
                /// up driving the cadence math against the real rate.
                FrameRate _frameRate;
                /// Whether @ref _frameRate has been confirmed by a
                /// frame-level @ref Metadata::FrameRate value.  The
                /// open-time value from @c pendingMediaDesc is
                /// speculative (some backends only learn the real rate
                /// from the first incoming frame), so the first
                /// authoritative metadata-published rate that
                /// disagrees silently relatches.  Subsequent mid-run
                /// changes warn and reset the A/V sync baseline so
                /// the cadence math runs against the new rate.
                bool _frameRateConfirmed = false;

                // ---- Decoder state (constructed lazily on the first frame
                // because we need the actual ImageDesc and AudioDesc to
                // build them) ----
                ImageDataDecoder _imageDataDecoder;
                AudioDataDecoder _audioDataDecoder;
                LtcDecoder::UPtr _ltcDecoder;
                bool             _decodersInitialized = false;

                // ---- Video frame history (for marker-based A/V sync) ----
                //
                // Every successful @ref runImageDataCheck pushes a
                // record into this ring so the audio-side codeword
                // decode can look up the matching video frame's
                // wall-clock anchor by 48-bit frame number.  Bounded
                // at @ref kVideoFrameHistoryMax; older entries are
                // evicted from the front when the ring fills.
                struct VideoFrameRecord {
                                /// 48-bit frame field from the picture
                                /// data band (matches the audio codeword's
                                /// @c bits 0..47).
                                uint64_t frame48 = 0;
                                /// 8-bit stream ID byte (matches the
                                /// audio codeword's @c bits 56..63).
                                uint8_t streamId = 0;
                                /// Video MediaTimeStamp expressed in
                                /// nanoseconds (timestamp + offset).
                                int64_t videoWallNs = 0;
                };
                /// Bound on @ref _videoFrameHistory.  64 entries cover
                /// any reasonable network buffering depth at common
                /// frame rates while keeping the lookup O(N) cost
                /// trivially small.
                static constexpr size_t kVideoFrameHistoryMax = 64;
                List<VideoFrameRecord>  _videoFrameHistory;

                // ---- AudioData per-channel rolling accumulators ----
                //
                // Decoupled from the LTC-side @ref _audioStream so the
                // two decoders don't fight over the same FIFO and so
                // the data check can survive bursty audio that splits
                // a codeword across multiple chunks.  One @ref
                // AudioDataDecoder::StreamState per channel — owns the
                // per-channel rolling buffer and the absolute stream
                // sample anchor reported back through @ref
                // AudioDataDecoder::DecodedItem::streamSampleStart.
                List<AudioDataDecoder::StreamState> _audioDataStreamStates;
                // Per-channel "have I ever seen a clean decode on
                // this channel?" latch.  Anomaly discontinuities
                // (decode failure / length anomaly) only fire on
                // channels that have produced at least one valid
                // codeword — without this gate, channels that simply
                // don't carry PcmMarker (LTC, tones, silence) would
                // generate spurious anomalies whenever findSync's
                // 4-transition search happens to land on a samplesPerBit
                // inside the decoder's bandwidth gate.
                // Stored as @c uint8_t (0/1) rather than @c bool —
                // @c List<bool> wraps @c std::vector<bool> whose
                // proxy references break the lvalue interfaces our
                // @c List wrapper exposes.
                List<uint8_t> _audioDataChannelActive;

                // ---- Audio stream (continuous-stream view of incoming
                // audio that decouples per-frame chunk boundaries from
                // analyses that want a real stream) ----

                /// FIFO that accumulates incoming audio in native float
                /// interleaved at the stream's sample rate.  Format is
                /// learned from the first audio chunk; subsequent chunks
                /// in different formats convert on push via AudioBuffer's
                /// built-in conversion.  Sized for ~1 s of headroom.
                AudioBuffer _audioStream;
                /// True once the buffer has been initialized with a format.
                bool _audioStreamReady = false;
                /// Cached sample rate of the audio stream (Hz).
                double _audioSampleRate = 0.0;
                /// True once a wall-clock anchor is known for stream
                /// sample 0.  Set on the first audio chunk that carries
                /// a valid PTS.
                bool _audioStreamAnchored = false;
                /// Wall-clock nanoseconds corresponding to stream sample 0.
                /// Sample @c N's wall-clock is
                /// @c _audioStreamStartNs + N * 1e9 / _audioSampleRate.
                int64_t _audioStreamStartNs = 0;
                /// Total audio samples ever pushed into @ref _audioStream.
                int64_t _audioCumulativeIn = 0;
                /// Total audio samples ever drained from @ref _audioStream
                /// for analysis (i.e. fed to the LTC decoder).  Equal to
                /// the LTC decoder's internal sample counter, so its
                /// @c sampleStart results map directly to absolute stream
                /// sample positions.
                int64_t _audioCumulativeAnalyzed = 0;
                /// Scratch buffer (shared-pointer so a @ref BufferView can
                /// borrow it without copying) for pulling samples out of
                /// the audio stream during analysis; held as a member so
                /// steady-state drains avoid per-call reallocation.
                Buffer _audioDrainScratch;

                // ---- Continuity tracking ----
                bool     _hasPreviousPicture = false;
                uint32_t _previousFrameNumber = 0;
                uint32_t _previousStreamId = 0;
                Timecode _previousPictureTc;
                bool     _hasPreviousLtc = false;
                Timecode _previousLtcTc;
                /// Inferred frame-rate mode for the picture timecode.
                /// The picture data band only carries digits + the DF
                /// flag, so the picture TC has no native @ref Timecode::Mode.
                /// The inspector latches the first @c Mode it sees from
                /// any source that carries one (currently: the LTC
                /// decoder) and attaches it to picture TCs from then on
                /// so the continuity check can compute "expected next"
                /// via @c operator++.
                Timecode::Mode _inferredPictureMode;
                /// Most-recent valid sync offset measurement, used by
                /// the per-frame "did the offset change" check.
                bool    _hasPreviousSyncOffset = false;
                int64_t _previousSyncOffset = 0;
                /// Baseline phase between the audio codeword's
                /// stream-sample position and the rational-rate
                /// prediction (@c cumulativeTicks).  Latched on the
                /// first successful marker match: @c
                /// streamSampleStart - @c cumulativeTicks(sampleRate,
                /// frameNumber).  Subsequent samples report their
                /// phase as a delta against this baseline so a stream
                /// that joins mid-flight, runs through an SRC with
                /// constant group delay, or simply doesn't start at
                /// frame 0 still reports 0 sync offset for a clean
                /// cadenced run.  Real changes in the codeword
                /// position relative to its expected rational
                /// position still show up.
                bool    _avSyncBaselineSet = false;
                int64_t _avSyncBaselinePhase = 0;
                /// Cached average samples-per-frame for the picture
                /// stream, captured the first time the A/V sync check
                /// finds a working sample rate + frame rate pair.
                /// Stored as @c double because for NTSC fractional
                /// rates (29.97, 59.94, 23.98, ...) the per-frame
                /// sample count is non-integer — the LTC encoder
                /// alternates between adjacent counts to maintain rate
                /// sync, but the *average* is the rational
                /// @c sampleRate*den/num.  Zero means "not yet
                /// measurable".
                double _samplesPerFrame = 0.0;

                // ---- Timestamp test state (previous-frame anchors
                // needed to compute frame-to-frame deltas) ----
                bool    _hasPreviousVideoTimestamp = false;
                bool    _hasPreviousAudioTimestamp = false;
                int64_t _previousVideoTimestampNs = 0;
                int64_t _previousAudioTimestampNs = 0;

                // ---- Video PTS prediction anchor (for jitter / re-anchor) ----
                bool        _videoPtsAnchored = false;
                int64_t     _videoPtsAnchorNs = 0;
                FrameNumber _videoPtsAnchorFrame{0};

                // ---- A/V cross-PTS baseline for drift measurement.  Set
                // on the first frame that carries valid PTSs on both
                // essences. ----
                bool    _avBaselineSet = false;
                int64_t _avBaselineOffsetNs = 0;
                /// Most recent audio chunk PTS (used by the A/V drift check
                /// when the *current* frame doesn't itself carry an audio
                /// chunk — a normal occurrence under bursty delivery).
                bool    _hasLastAudioPtsForAv = false;
                int64_t _lastAudioPtsForAvNs = 0;

                // ---- Per-frame counter ----
                FrameNumber _frameIndex{0};

                // ---- Stats accumulator ----
                mutable Mutex     _stateMutex;
                InspectorSnapshot _stats;

                // ---- Periodic log timing (wall time) ----
                double     _lastLogWallSec = 0.0;
                FrameCount _framesSinceLastLog{0};

                // ---- Per-frame callback ----
                EventCallback _callback;

                // ---- CaptureStats test output ----
                //
                // TSV with one row per frame.  @c _statsFile is opened
                // at @c open() time when the test is enabled and closed
                // at @c close().  @c _statsFilePath holds the resolved
                // path (caller-supplied or auto-generated inside
                // @c Dir::temp()).  @c _statsWriteError latches on the
                // first write failure so the inspector stops retrying
                // rather than spamming the log.
                FILE  *_statsFile = nullptr;
                String _statsFilePath;
                bool   _statsWriteError = false;
};

/**
 * @brief @ref MediaIOFactory for the Inspector backend.
 * @ingroup proav
 */
class InspectorFactory : public MediaIOFactory {
        public:
                InspectorFactory() = default;

                String name() const override { return String("Inspector"); }
                String displayName() const override { return String("Frame Inspector"); }
                String description() const override {
                        return String("Sink that inspects each frame for continuity, sync, and decode "
                                      "tests, then drops it.");
                }
                bool canBeSink() const override { return true; }

                Config::SpecMap configSpecs() const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
