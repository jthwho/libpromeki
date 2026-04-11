/**
 * @file      mediaiotask_inspector.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <memory>
#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/imagedataencoder.h>
#include <promeki/imagedatadecoder.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/timecode.h>
#include <promeki/mutex.h>
#include <promeki/list.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class LtcDecoder;

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
                /// The picture-vs-LTC sync offset moved by more than
                /// the configured tolerance from the previous frame.
                /// Audio and video are locked in pro workflows, so any
                /// change from a previously-stable offset is a real
                /// fault rather than a measurement artifact.
                SyncOffsetChange,
        };

        Kind     kind;             ///< Which kind of discontinuity this is.
        String   previousValue;    ///< Pre-rendered "previous" snapshot.
        String   currentValue;     ///< Pre-rendered "current" snapshot.
        String   description;      ///< Full human-readable description (kind + values).
};

/**
 * @brief Per-frame measurement record produced by @ref MediaIOTask_Inspector.
 * @ingroup proav
 *
 * The inspector populates one of these for every frame it sees and
 * delivers it to the user-supplied callback (set via
 * @ref MediaIOTask_Inspector::setEventCallback).  Fields are only
 * meaningful when the corresponding decoder was enabled in the
 * config — check @c pictureDecoded / @c ltcDecoded before reading.
 */
struct InspectorEvent {
        /// Inspector's monotonic per-frame counter (0-based, increments
        /// once per @c executeCmd(MediaIOCommandWrite&) call).
        int64_t  frameIndex = 0;

        // ---- Picture data band decode ----

        /// True if the picture data band was decoded for this frame.
        bool     pictureDecoded   = false;
        /// True if the *image-data decoder enable* flag was set in the
        /// config — distinguishes "decoder disabled, no result expected"
        /// from "decoder enabled but the data band could not be read".
        bool     pictureDecoderEnabled = false;
        /// Frame number recovered from the first 64-bit data band
        /// (low 32 bits of the @c (streamID << 32) | frameNumber word).
        uint32_t pictureFrameNumber = 0;
        /// Stream ID recovered from the first 64-bit data band (high
        /// 32 bits of the same word).
        uint32_t pictureStreamId    = 0;
        /// Timecode recovered from the second 64-bit data band, decoded
        /// via @ref Timecode::fromBcd64.
        Timecode pictureTimecode;

        // ---- Audio LTC decode ----

        /// True if at least one LTC frame was recovered from this
        /// frame's audio chunk.
        bool     ltcDecoded         = false;
        /// True if the *LTC decoder enable* flag was set in the config.
        bool     ltcDecoderEnabled  = false;
        /// Most recent timecode recovered from the audio LTC stream.
        Timecode ltcTimecode;
        /// Audio sample offset @em within this frame's audio chunk
        /// where the LTC sync word arrived.  Zero means the sync word
        /// landed exactly at the start of the chunk; positive means
        /// later in the chunk; negative is rare but possible when the
        /// sync word was buffered from a previous chunk.  This is the
        /// canonical "LTC time anchor" used by the TC sync drift
        /// check.
        int64_t  ltcSampleStart     = 0;

        // ---- A/V Sync (picture TC vs audio LTC) ----

        /// True if the inspector was able to compute an A/V sync
        /// offset for this frame (both picture and LTC decoded
        /// successfully, and the LTC carried a known frame rate).
        bool     avSyncValid        = false;
        /// Instantaneous A/V sync offset in audio samples between
        /// the picture timecode anchor and the audio LTC sync
        /// anchor.  This is the *current* misalignment between the
        /// two sources on this specific frame, not a delta against
        /// a previous frame.
        ///
        /// Sign convention: positive = video leads audio (the
        /// picture's TC anchor lands at an earlier wall-clock audio
        /// sample than the LTC's TC anchor).  Negative = audio
        /// leads video.
        ///
        /// @note A small constant offset (a few samples — typically
        /// 1-3 at common sample rates) is normal when the LTC is
        /// produced by libvtc's encoder and decoded by libvtc's
        /// decoder.  The encoder's raised-cosine transition ramp and
        /// the decoder's hysteresis threshold combine to give the
        /// decoder a fixed edge-detection latency relative to the
        /// encoder's bit boundary.  The value is *constant* across
        /// frames, so any movement does represent real drift — the
        /// inspector flags such movement as a discontinuity (see
        /// @ref MediaConfig::InspectorSyncOffsetToleranceSamples).
        int64_t  avSyncOffsetSamples = 0;

        // ---- Continuity ----

        /// Discontinuities detected on this specific frame, with both
        /// previous and current values pre-rendered for logging.
        List<InspectorDiscontinuity> discontinuities;
};

/**
 * @brief Aggregate snapshot of the inspector's accumulated state.
 * @ingroup proav
 *
 * @ref MediaIOTask_Inspector::snapshot returns one of these on demand.
 * Counters are monotonic over the inspector's lifetime; the
 * @c lastEvent field is the most recently produced
 * @ref InspectorEvent (a copy, taken under the inspector's mutex).
 */
struct InspectorSnapshot {
        /// Total number of frames the inspector has processed.
        int64_t framesProcessed       = 0;
        /// Total number of frames where the picture data band decoded.
        int64_t framesWithPictureData = 0;
        /// Total number of frames where at least one LTC frame decoded.
        int64_t framesWithLtc         = 0;
        /// Total number of discontinuities of any kind, summed over all
        /// frames.
        int64_t totalDiscontinuities  = 0;
        /// True if @c lastEvent is populated (at least one frame has
        /// been processed).
        bool    hasLastEvent          = false;
        /// Most recent per-frame event.
        InspectorEvent lastEvent;
};

/**
 * @brief MediaIO sink backend that decodes and validates a media stream.
 * @ingroup proav
 *
 * The inverse of @ref MediaIOTask_TPG: where the TPG @em produces
 * test-pattern frames, the inspector @em consumes frames and runs a
 * configurable set of checks on each one.  Currently supported checks:
 *
 *  - **Image data band decode** (@ref MediaConfig::InspectorDecodeImageData):
 *    pulls the two 64-bit payloads written by @ref ImageDataEncoder out
 *    of the top of every frame, recovering the rolling frame number,
 *    stream ID, and BCD timecode.
 *  - **LTC decode** (@ref MediaConfig::InspectorDecodeLtc): runs the
 *    selected audio channel through @ref LtcDecoder and reports any
 *    timecodes recovered.
 *  - **A/V Sync** (@ref MediaConfig::InspectorCheckTcSync): with
 *    both decoders enabled, computes the per-frame offset between
 *    the picture timecode and the audio LTC, in audio samples and
 *    in fractional frames.  This is an *instantaneous* measurement
 *    on each frame; a constant offset across frames is healthy
 *    (audio and video are locked), while any movement is flagged
 *    as a discontinuity.
 *  - **Continuity** (@ref MediaConfig::InspectorCheckContinuity):
 *    tracks frame number, stream ID, picture timecode, and LTC
 *    timecode from one frame to the next, and flags any unexpected
 *    jump as a @ref InspectorDiscontinuity record on the per-frame
 *    event.
 *
 * The dependency relationships between these are auto-resolved at
 * open time: enabling @c InspectorCheckTcSync implicitly turns on
 * @c InspectorDecodeImageData and @c InspectorDecodeLtc; enabling
 * @c InspectorCheckContinuity implicitly turns on
 * @c InspectorDecodeImageData.  The user does not have to specify
 * the upstream decoders explicitly.
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
 * Because @ref setEventCallback must be called before @c open(), and
 * the standard @ref MediaIO::create() factory hides the underlying
 * task, callers that want a callback should construct the task
 * directly and adopt it via @ref MediaIO::adoptTask:
 *
 * @code
 * auto *insp = new MediaIOTask_Inspector();
 * insp->setEventCallback([](const InspectorEvent &e) {
 *     // ... runs on the worker thread
 * });
 *
 * MediaIO *io = new MediaIO();
 * MediaIO::Config cfg = MediaIO::defaultConfig("Inspector");
 * cfg.set(MediaConfig::InspectorCheckTcSync, true);
 * cfg.set(MediaConfig::InspectorCheckContinuity, true);
 * io->setConfig(cfg);
 * io->adoptTask(insp);
 * io->open(MediaIO::Writer);
 *
 * // Pump frames in:
 * io->writeFrame(frame);
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
 */
class MediaIOTask_Inspector : public MediaIOTask {
        public:
                /// Per-frame callback signature.
                using EventCallback = std::function<void(const InspectorEvent &)>;

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc registered as a sink (write-only).
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs an inspector task. */
                MediaIOTask_Inspector();

                /** @brief Destructor. */
                ~MediaIOTask_Inspector() override;

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

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

                void initDecoders(const Frame &frame);
                void runImageDataCheck(const Frame &frame, InspectorEvent &event);
                void runLtcCheck(const Frame &frame, InspectorEvent &event);
                void runAvSyncCheck(const Frame &frame, InspectorEvent &event);
                void runContinuityCheck(InspectorEvent &event);
                void emitPeriodicLogIfDue();
                void resetState();

                // ---- Configuration (resolved at open time) ----
                bool     _decodeImageData      = false;
                bool     _decodeLtc            = false;
                bool     _checkTcSync          = false;
                bool     _checkContinuity      = false;
                bool     _dropFrames           = true;
                int      _imageDataRepeatLines = 16;
                int      _ltcChannel           = 0;
                int      _syncOffsetToleranceSamples = 0;
                double   _logIntervalSec       = 1.0;
                bool     _isOpen               = false;

                // ---- Decoder state (constructed lazily on the first frame
                // because we need the actual ImageDesc and AudioDesc to
                // build them) ----
                ImageDataDecoder            _imageDataDecoder;
                std::unique_ptr<LtcDecoder> _ltcDecoder;
                bool                        _decodersInitialized = false;
                /// Total audio samples fed to the LTC decoder so far.
                /// Lets us turn the decoder's cumulative @c sampleStart
                /// values into per-chunk offsets.
                int64_t                     _ltcCumulativeSamples = 0;

                // ---- Continuity tracking ----
                bool          _hasPreviousPicture = false;
                uint32_t      _previousFrameNumber = 0;
                uint32_t      _previousStreamId = 0;
                Timecode      _previousPictureTc;
                bool          _hasPreviousLtc = false;
                Timecode      _previousLtcTc;
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
                bool          _hasPreviousSyncOffset = false;
                int64_t       _previousSyncOffset    = 0;
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
                double        _samplesPerFrame       = 0.0;

                // ---- Per-frame counter ----
                int64_t  _frameIndex = 0;

                // ---- Stats accumulator ----
                mutable Mutex     _stateMutex;
                InspectorSnapshot _stats;

                // ---- Periodic log timing (wall time) ----
                double   _lastLogWallSec = 0.0;
                int64_t  _framesSinceLastLog = 0;

                // ---- Per-frame callback ----
                EventCallback _callback;
};

PROMEKI_NAMESPACE_END
