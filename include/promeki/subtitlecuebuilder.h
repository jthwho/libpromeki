/**
 * @file      subtitlecuebuilder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/subtitle.h>
#include <promeki/transcript.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class Frame;
class MediaConfig;
struct SubtitleCueBuilderImpl; // Pimpl — defined in subtitlecuebuilder.cpp.

/**
 * @brief Builds @ref Subtitle cues from a stream of @ref Transcript
 *        utterances.
 * @ingroup proav
 *
 * The cue builder is the second half of the speech-to-subtitle
 * pipeline.  @ref TranscriptionEngine produces raw transcripts —
 * words with timing, confidence, speaker, language; this class
 * consumes that stream and emits @ref Subtitle cues per a
 * configurable policy (max chars per line, max lines, min / max
 * display duration, anchor, partial-gating).  Splitting the work
 * means the transcript stream is reusable for analytics / search /
 * alignment without subtitle-shaping decisions baked in, and the
 * cue policy is testable in isolation without standing up a real
 * speech engine.
 *
 * @par Usage shapes
 *
 *  - **Frame-shaped pipeline stage.**  @ref submitFrame extracts
 *    @c Metadata::Transcript from the input Frame; @ref receiveFrame
 *    emits a Frame whose @c Metadata::Subtitle is the shaped cue
 *    (and whose other payloads / metadata echo the input through
 *    unchanged).  Drop-in for live captioning graphs.
 *  - **Batch helper.**  @ref buildCues takes a @ref TranscriptList
 *    and a @ref MediaConfig and returns a @ref SubtitleList in one
 *    pass.  Use for offline transcription → SRT / WebVTT export.
 *
 * Both paths share the same policy implementation; the streaming
 * path simply tracks a small amount of state (whether the last
 * emitted cue is still on screen, whether a partial supersedes an
 * older partial).
 *
 * @par Policy keys
 *
 *  - @ref MediaConfig::SubtitleCueMaxCharsPerLine — wrap width.
 *  - @ref MediaConfig::SubtitleCueMaxLines — wrap line cap.
 *  - @ref MediaConfig::SubtitleCueMinDuration — minimum on-screen
 *    time.  The builder extends @c end to honour this.
 *  - @ref MediaConfig::SubtitleCueMaxDuration — maximum on-screen
 *    time.  The builder truncates @c end to honour this.
 *  - @ref MediaConfig::SubtitleCueEmitPartials — emit cues for
 *    interim transcripts (default off).  When enabled, partial
 *    transcripts produce cues with @ref Subtitle::partial @c true.
 *  - @ref MediaConfig::SubtitleCueAnchor — anchor stamped on
 *    emitted cues.
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct builder instances are
 * independent; concurrent access to a single instance must be
 * externally synchronised.  The static @ref buildCues batch helper
 * is reentrant.
 *
 * @see TranscriptionEngine, Transcript, Subtitle, SubtitleList
 */
class SubtitleCueBuilder {
        public:
                /** @brief Unique-ownership pointer alias. */
                using UPtr = UniquePtr<SubtitleCueBuilder>;

                /** @brief Constructs an unconfigured cue builder. */
                SubtitleCueBuilder();

                /** @brief Constructs a cue builder pre-configured with @p config. */
                explicit SubtitleCueBuilder(const MediaConfig &config);

                ~SubtitleCueBuilder();

                /**
                 * @brief Applies cue-shaping policy from a @ref MediaConfig.
                 *
                 * Reads the @c SubtitleCue* keys.  Unset keys fall
                 * back to their declared defaults.
                 */
                void configure(const MediaConfig &config);

                /**
                 * @brief Submits one input @ref Frame for shaping.
                 *
                 * Looks up @c Metadata::Transcript on @p frame and
                 * runs the policy against the contained @ref
                 * Transcript.  When the policy accepts the transcript
                 * (not partial, or @c SubtitleCueEmitPartials enabled),
                 * an output Frame is enqueued for @ref receiveFrame.
                 *
                 * Frames without a Transcript on metadata are passed
                 * through unchanged — useful when the builder sits in
                 * a pipeline that carries other Frames between
                 * transcription-bearing ones.
                 *
                 * @return @c Error::Ok on success; @c Error::Invalid
                 *         when @p frame is itself invalid.
                 */
                Error submitFrame(const Frame &frame);

                /**
                 * @brief Dequeues one shaped output @ref Frame, or an
                 *        invalid Frame when none is ready.
                 *
                 * Emitted Frames echo @p source's video / audio / ANC
                 * payloads and metadata through unchanged and stamp
                 * @c Metadata::Subtitle with the shaped cue.
                 * @ref Subtitle::partial mirrors the source
                 * @ref Transcript::partial flag.
                 */
                Frame receiveFrame();

                /** @brief Discards any in-flight state. */
                void reset();

                // ---- Batch helper ----

                /**
                 * @brief Builds a @ref SubtitleList from a
                 *        @ref TranscriptList in one pass.
                 *
                 * One-shot helper for offline conversion (transcribed
                 * recording → SRT file, batch translation pipelines,
                 * test fixtures).  Skips transcripts marked partial
                 * when @c SubtitleCueEmitPartials is @c false; honours
                 * every other @c SubtitleCue* policy key the same way
                 * the streaming @ref submitFrame path does.
                 *
                 * @param transcripts Input utterances.
                 * @param config      Policy configuration.  May omit
                 *                    any key; defaults are documented
                 *                    on @ref MediaConfig.
                 * @return The shaped cue list in input order.
                 */
                static SubtitleList buildCues(const TranscriptList &transcripts, const MediaConfig &config);

        private:
                SubtitleCueBuilder(const SubtitleCueBuilder &) = delete;
                SubtitleCueBuilder &operator=(const SubtitleCueBuilder &) = delete;

                UniquePtr<SubtitleCueBuilderImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
