/**
 * @file      transcript.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class JsonObject;
class Metadata;
struct TranscriptImpl;     // Pimpl — defined in transcript.cpp.
struct TranscriptListImpl; // Pimpl — defined in transcript.cpp.

/**
 * @brief One word emitted by a @ref TranscriptionEngine, with timing.
 * @ingroup proav
 *
 * Plain value type carrying the text of a single recognised word
 * (or token — punctuation, filler, language tag) plus the time
 * window the engine attributes to it and a confidence score.
 *
 * @par Timestamp domain
 *
 * @ref start and @ref end are absolute @ref TimeStamp values in the
 * same media-time domain as the source @ref PcmAudioPayload's
 * @ref MediaTimeStamp::timeStamp.  Engines derive them via
 * @ref TranscriptionEngine::wordTimestamp from the input payload's
 * @c pts plus the in-payload sample offset, so words emitted across
 * multiple submit chunks ride on a single coherent timeline without
 * the engine bookkeeping its own session epoch.  The
 * @ref ClockDomain is implicit (every word in a single
 * @ref Transcript shares the source payload's domain).
 *
 * @par Confidence
 *
 * @ref confidence is a normalised score in @c [0, 1].  Engines that
 * don't expose per-word confidence report @c 1.0 (effectively "no
 * signal").  Downstream consumers (subtitle builders, search
 * indexers) gate on this — e.g. only show a partial cue when every
 * word is above some threshold.
 *
 * @par Thread Safety
 *
 * Distinct instances are independent; concurrent mutation of one
 * instance must be externally synchronised.
 *
 * @see Transcript, TranscriptList, TranscriptionEngine
 */
class TranscriptWord {
        public:
                PROMEKI_DATATYPE(TranscriptWord, DataTypeTranscriptWord, 1)

                /** @brief List of TranscriptWord values. */
                using List = ::promeki::List<TranscriptWord>;

                /** @brief Default-constructs an empty word. */
                TranscriptWord() = default;

                /**
                 * @brief Constructs a word with explicit timing.
                 * @param text       Word text (UTF-8).
                 * @param start      Absolute media-time start.
                 * @param end        Absolute media-time end.
                 * @param confidence Engine-reported confidence in
                 *                   @c [0, 1].  Defaults to @c 1.0
                 *                   for engines that don't surface a
                 *                   per-word score.
                 */
                TranscriptWord(String text, TimeStamp start, TimeStamp end, float confidence = 1.0f)
                    : _text(std::move(text)), _start(start), _end(end), _confidence(confidence) {}

                /** @brief Word text (UTF-8). */
                const String &text() const { return _text; }

                /** @brief Absolute media-time start of the word. */
                const TimeStamp &start() const { return _start; }

                /** @brief Absolute media-time end of the word. */
                const TimeStamp &end() const { return _end; }

                /** @brief Engine-reported confidence in @c [0, 1]. */
                float confidence() const { return _confidence; }

                /** @brief @c end - @c start as a Duration. */
                TimeStamp::Duration duration() const { return _end.value() - _start.value(); }

                void setText(String v) { _text = std::move(v); }
                void setStart(const TimeStamp &v) { _start = v; }
                void setEnd(const TimeStamp &v) { _end = v; }
                void setConfidence(float v) { _confidence = v; }

                bool operator==(const TranscriptWord &o) const {
                        return _text == o._text && _start == o._start && _end == o._end
                                && _confidence == o._confidence;
                }
                bool operator!=(const TranscriptWord &o) const { return !(*this == o); }

                /// @brief Structured JSON dump for inspection / tooling.
                JsonObject toJson() const;

                /// @brief Short human-readable summary.
                String toString() const;

                /// @brief DataStream body writer (PROMEKI_DATATYPE path).
                Error writeToStream(DataStream &s) const;

                /// @brief DataStream body reader (PROMEKI_DATATYPE path).
                template <uint32_t V> static Result<TranscriptWord> readFromStream(DataStream &s);

        private:
                String    _text;
                TimeStamp _start;
                TimeStamp _end;
                float     _confidence = 1.0f;
};

/** @brief Writes a @ref TranscriptWord to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const TranscriptWord &word);

/** @brief Reads a @ref TranscriptWord from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, TranscriptWord &word);

/**
 * @brief One utterance emitted by a @ref TranscriptionEngine.
 * @ingroup proav
 *
 * Format-agnostic value type modelling the raw output of a
 * speech-to-text session: the recognised words with timing, an
 * optional speaker label, an optional detected language tag, an
 * utterance-level confidence, and a partial / final lifecycle bit.
 *
 * Transcripts are intentionally distinct from @ref Subtitle —
 * transcription is "speech → text + timing"; subtitling is "text →
 * displayable cues with layout / wrap / gap rules."  The latter is
 * the job of @ref SubtitleCueBuilder, which consumes @c Transcript
 * values and emits @ref Subtitle cues per a configurable policy.
 * Decoupling the two means the raw transcript stream is reusable
 * for analytics, search, and alignment without subtitle-shaping
 * decisions baked in, and cue policy is testable in isolation
 * without standing up a real engine.
 *
 * @par Field set
 *
 *  - @ref words — ordered list of @ref TranscriptWord with absolute
 *    media-time @c start / @c end and a per-word confidence.
 *  - @ref speaker — accessibility / diarization label, empty when
 *    the engine doesn't surface speaker identity.
 *  - @ref language — BCP 47 language tag, populated when the engine
 *    performed language detection; empty otherwise.
 *  - @ref confidence — utterance-level confidence in @c [0, 1].
 *  - @ref partial — @c true for interim hypotheses (streaming
 *    engines emit partials that may be revised); @c false for
 *    finalised utterances.  A subsequent transcript with overlapping
 *    timing and @ref partial @c false supersedes a partial.
 *  - @ref metadata — escape hatch for engine-specific keys.
 *
 * @par Derived fields
 *
 *  - @ref text — space-joined concatenation of word texts.
 *  - @ref start / @ref end — first word's @c start / last word's
 *    @c end; @c TimeStamp::Invalid for empty transcripts.
 *
 * @par Storage and copy semantics
 *
 * Value-type handle backed by an internal
 * @c SharedPtr<TranscriptImpl> (pimpl).  Copying is O(1); the
 * shared state is detached on mutation.  No @c ::Ptr alias —
 * matches the post-2026-05-07 convention shared with
 * @ref Subtitle / @ref Frame / @ref Metadata.
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct handles may be used
 * concurrently; concurrent access to a single handle must be
 * externally synchronised.
 *
 * @see TranscriptionEngine, SubtitleCueBuilder, TranscriptList
 */
class Transcript {
        public:
                PROMEKI_DATATYPE(Transcript, DataTypeTranscript, 1)

                /** @brief Default-constructs an empty transcript. */
                Transcript();

                /** @brief Constructs a transcript from a word list. */
                explicit Transcript(TranscriptWord::List words);

                /**
                 * @brief Constructs a fully-populated transcript.
                 * @param words      Recognised words with timing.
                 * @param speaker    Speaker / voice label; empty when absent.
                 * @param language   Detected BCP 47 tag; empty when absent.
                 * @param confidence Utterance-level confidence in @c [0, 1].
                 * @param partial    @c true for interim hypotheses.
                 */
                Transcript(TranscriptWord::List words, String speaker, String language, float confidence, bool partial);

                Transcript(const Transcript &);
                Transcript(Transcript &&) noexcept;
                ~Transcript();
                Transcript &operator=(const Transcript &);
                Transcript &operator=(Transcript &&) noexcept;

                // -- Read-only accessors ----------------------------------

                /** @brief Recognised words in emission order. */
                const TranscriptWord::List &words() const;

                /** @brief Speaker / voice identifier; empty when absent. */
                const String &speaker() const;

                /** @brief Detected BCP 47 language tag; empty when absent. */
                const String &language() const;

                /** @brief Utterance-level confidence in @c [0, 1]. */
                float confidence() const;

                /**
                 * @brief @c true when this transcript is an interim
                 *        hypothesis that may still be revised.
                 *
                 * Streaming engines emit partials as audio
                 * accumulates; finalised transcripts (with
                 * @ref partial @c false) supersede earlier partials
                 * covering the same audio span.  Batch engines emit
                 * only finalised transcripts.
                 */
                bool partial() const;

                /** @brief Engine-specific extension metadata. */
                const Metadata &metadata() const;

                // -- Derived fields ---------------------------------------

                /**
                 * @brief Space-joined concatenation of word texts.
                 *
                 * Cached inside the impl; rebuilt whenever @ref words
                 * is replaced via @ref setWords or @ref appendWord.
                 */
                const String &text() const;

                /**
                 * @brief Absolute media-time start (first word's @c start),
                 *        or @c TimeStamp::Invalid when empty.
                 */
                TimeStamp start() const;

                /**
                 * @brief Absolute media-time end (last word's @c end),
                 *        or @c TimeStamp::Invalid when empty.
                 */
                TimeStamp end() const;

                /** @brief @c end - @c start, or zero when empty. */
                TimeStamp::Duration duration() const;

                /** @brief @c true when @ref words is empty. */
                bool isEmpty() const;

                // -- CoW mutators -----------------------------------------

                void setWords(TranscriptWord::List v);
                void appendWord(TranscriptWord w);
                void setSpeaker(String v);
                void setLanguage(String v);
                void setConfidence(float v);
                void setPartial(bool v);
                void setMetadata(Metadata v);

                // -- Comparison + diagnostics -----------------------------

                bool operator==(const Transcript &o) const;
                bool operator!=(const Transcript &o) const { return !(*this == o); }

                /// @brief Structured JSON dump for inspection / tooling.
                JsonObject toJson() const;

                /// @brief Short human-readable summary.
                String toString() const;

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 *
                 * Wire body: words, speaker, language, confidence,
                 * partial, metadata — each field carrying its own
                 * DataType tag.  Cached @ref text is reconstructed
                 * from words on read.
                 */
                Error writeToStream(DataStream &s) const;

                /** @brief DataStream body reader (PROMEKI_DATATYPE path). */
                template <uint32_t V> static Result<Transcript> readFromStream(DataStream &s);

        private:
                SharedPtr<TranscriptImpl> _d;
};

// ============================================================================
// TranscriptList
// ============================================================================

/**
 * @brief An ordered collection of @ref Transcript entries.
 * @ingroup proav
 *
 * Drop-in container for the timeline output of a transcription
 * session and the input to @ref SubtitleCueBuilder::buildCues batch
 * conversion.  Same value-type / CoW semantics as @ref Transcript —
 * copy is O(1), shared storage until a mutator detaches.
 *
 * @par Lookup helpers
 *
 * Point-in-time queries — "what transcript (if any) is active at
 * @c t?", "what's the next transcript after @c t?" — are first-class.
 * When the list is in sorted-by-start order, the lookup helpers use
 * binary search; otherwise they fall back to linear scan.
 * @ref sortByStart sets the cache flag; mutators that could break
 * order reset it.
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe (same contract as @ref Transcript).
 *
 * @see Transcript, TranscriptionEngine, SubtitleCueBuilder
 */
class TranscriptList {
        public:
                TranscriptList();
                explicit TranscriptList(List<Transcript> entries);
                TranscriptList(const TranscriptList &);
                TranscriptList(TranscriptList &&) noexcept;
                ~TranscriptList();
                TranscriptList &operator=(const TranscriptList &);
                TranscriptList &operator=(TranscriptList &&) noexcept;

                // -- Size / access ----------------------------------------

                size_t size() const;
                bool   isEmpty() const;

                /** @brief Read-only indexed access. */
                const Transcript &at(size_t i) const;

                /** @copydoc at */
                const Transcript &operator[](size_t i) const;

                // -- Mutators (CoW) ---------------------------------------

                /** @brief Appends one entry; resets the sorted-cache
                 *         flag when order would no longer be ascending. */
                void append(const Transcript &t);

                /** @brief Removes every entry. */
                void clear();

                /** @brief Sorts entries ascending by @c start (stable). */
                void sortByStart();

                /** @brief Reserves storage for at least @p n entries. */
                void reserve(size_t n);

                // -- Search helpers ---------------------------------------

                /**
                 * @brief Index of the first transcript whose
                 *        [start, end) window contains @p t, or -1
                 *        when none.
                 */
                int64_t findActiveAt(const TimeStamp &t) const;

                /**
                 * @brief Index of the first transcript whose @c start
                 *        is @c >= @p t, or -1 when every transcript
                 *        starts before @p t.
                 */
                int64_t findNextAfter(const TimeStamp &t) const;

                // -- Raw access -------------------------------------------

                /** @brief Read-only reference to the underlying list. */
                const List<Transcript> &entries() const;

                // -- Comparison + diagnostics -----------------------------

                bool operator==(const TranscriptList &o) const;
                bool operator!=(const TranscriptList &o) const { return !(*this == o); }

                /** @brief Structured JSON dump. */
                JsonObject toJson() const;

                /** @brief Short human-readable summary. */
                String toString() const;

        private:
                SharedPtr<TranscriptListImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
