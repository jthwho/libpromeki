/**
 * @file      frame.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <optional>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/benchmark.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/metadata.h>
#include <promeki/list.h>
#include <promeki/stringlist.h>
#include <promeki/mediaconfig.h>
#include <promeki/videoformat.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

class MediaDesc;

/**
 * @brief A media frame containing payloads and metadata.
 * @ingroup proav
 *
 * Aggregates one or more @ref MediaPayload entries — uncompressed
 * or compressed video (@ref VideoPayload) and audio
 * (@ref AudioPayload) — together with a @ref Metadata container,
 * into a single unit that represents one frame of media content.
 *
 * All essence lives in the @ref payloadList.  Use
 * @ref videoPayloads / @ref audioPayloads to filter by kind.
 * Compressed bitstream access units travel as
 * @ref CompressedVideoPayload or @ref CompressedAudioPayload entries
 * in the same list.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently — Frame is a
 * value-with-COW container (PROMEKI_SHARED_FINAL) and copies
 * share underlying payloads via atomic refcount.  A single
 * instance is conditionally thread-safe: const accessors are
 * safe; mutators (@c addPayload, @c setMetadata, ...) require
 * external synchronization.
 */
class Frame {
                PROMEKI_SHARED_FINAL(Frame)
        public:
                /** @brief Shared pointer type for Frame. */
                using Ptr = SharedPtr<Frame>;
                /** @brief Plain value list of Frame objects. */
                using List = promeki::List<Frame>;
                /** @brief List of shared pointers to Frame objects. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty frame with no images, audio, or metadata. */
                Frame() = default;

                /**
                 * @brief Returns a const reference to the list of media payloads.
                 * @return The payload pointer list.
                 */
                const MediaPayload::PtrList &payloadList() const { return _payloads; }

                /**
                 * @brief Returns a mutable reference to the list of media payloads.
                 * @return The payload pointer list.
                 */
                MediaPayload::PtrList &payloadList() { return _payloads; }

                /**
                 * @brief Appends a payload to @ref payloadList.
                 */
                void addPayload(MediaPayload::Ptr p) { _payloads.pushToBack(std::move(p)); }

                /**
                 * @brief Returns the @ref Video-kind entries from
                 *        @ref payloadList as typed pointers.
                 *
                 * Walks @ref payloadList once and collects every
                 * @ref MediaPayload whose @c kind is
                 * @ref MediaPayloadKind::Video, returning them as a
                 * fresh @ref VideoPayload::PtrList with shared
                 * ownership (no clone).  Null payload entries are
                 * skipped.
                 *
                 * The convenience consumers want during migration:
                 * read once, get only the video side, still see
                 * @ref UncompressedVideoPayload and
                 * @ref CompressedVideoPayload polymorphically via
                 * @ref MediaPayload::as.
                 *
                 * @return A fresh @ref VideoPayload::PtrList of all
                 *         video-kind payloads in payload-list order.
                 */
                VideoPayload::PtrList videoPayloads() const;

                /**
                 * @brief Returns the @ref Audio-kind entries from
                 *        @ref payloadList as typed pointers.
                 *
                 * @return A fresh @ref AudioPayload::PtrList of all
                 *         audio-kind payloads in payload-list order.
                 * @sa videoPayloads
                 */
                AudioPayload::PtrList audioPayloads() const;

                /**
                 * @brief Returns a const reference to the frame metadata.
                 * @return The metadata container.
                 */
                const Metadata &metadata() const { return _metadata; }

                /**
                 * @brief Returns a mutable reference to the frame metadata.
                 * @return The metadata container.
                 */
                Metadata &metadata() { return _metadata; }

                /**
                 * @brief Returns the VideoFormat for the image at @p index.
                 *
                 * Composes the image's raster and scan mode with the
                 * frame rate carried in @ref metadata under
                 * @c Metadata::FrameRate.  Returns a default
                 * (invalid) @ref VideoFormat when @p index is out of
                 * range or when the frame rate metadata is missing
                 * or not convertible to a FrameRate.
                 *
                 * @param index Zero-based image layer index.
                 * @return The composed VideoFormat, or an invalid
                 *         VideoFormat on failure.
                 */
                VideoFormat videoFormat(size_t index) const;

                /**
                 * @brief Assembles a MediaDesc describing this Frame.
                 *
                 * Returns a MediaDesc populated from the frame's own
                 * state: the frame rate is taken from
                 * @c Metadata::FrameRate (if set), the video descriptors
                 * are built from each @ref VideoPayload in
                 * @ref payloadList, the audio descriptors from each
                 * @ref AudioPayload, and the frame's metadata is copied
                 * across.
                 *
                 * The returned MediaDesc is only
                 * @ref MediaDesc::isValid "valid" if both a frame
                 * rate is present in metadata and the frame carries
                 * at least one video or audio payload.
                 */
                MediaDesc mediaDesc() const;

                /**
                 * @brief Returns a const reference to the frame benchmark.
                 * @return The benchmark pointer (may be null if benchmarking is disabled).
                 */
                const Benchmark::Ptr &benchmark() const { return _benchmark; }

                /**
                 * @brief Returns a mutable reference to the frame benchmark.
                 * @return The benchmark pointer.
                 */
                Benchmark::Ptr &benchmark() { return _benchmark; }

                /**
                 * @brief Sets the frame benchmark.
                 * @param bm The benchmark to attach.
                 */
                void setBenchmark(Benchmark::Ptr bm) {
                        _benchmark = std::move(bm);
                        return;
                }

                /**
                 * @brief Returns a const reference to the config update delta.
                 *
                 * When non-empty, the config update is applied by the
                 * receiving @ref MediaIOTask before the frame is
                 * processed.  This gives frame-level synchronisation
                 * for dynamic parameter changes (bitrate, quality,
                 * etc.) — the update travels with the write command
                 * through the strand queue, so a flush can never
                 * separate a config change from its frame.
                 */
                const MediaConfig &configUpdate() const { return _configUpdate; }

                /** @brief Returns a mutable reference to the config update delta. */
                MediaConfig &configUpdate() { return _configUpdate; }

                /** @brief Replaces the config update delta. */
                void setConfigUpdate(MediaConfig cfg) { _configUpdate = std::move(cfg); }

                /**
                 * @brief Essence scope for @ref isSafeCutPoint.
                 *
                 * Selects which essence streams contribute to the cut-
                 * point decision.  Callers that know the downstream
                 * sink only consumes one kind of essence can restrict
                 * the check — an audio-only sink shouldn't block a cut
                 * on a mid-GOP video frame that will never be written.
                 */
                enum CutPointScope {
                        CutPointVideoOnly = 0, ///< @brief Consider only video payloads.
                        CutPointAudioOnly = 1, ///< @brief Consider only audio payloads.
                        CutPointAudioVideo = 2 ///< @brief Consider every essence stream in the frame (default).
                };

                /**
                 * @brief Returns true when stopping the stream before this
                 *        frame leaves the chosen essence streams intact.
                 *
                 * Defers to @ref MediaPayload::isSafeCutPoint for every video
                 * payload in @ref payloadList and every audio payload.
                 * A scope that excludes a
                 * side of the essence skips that side entirely — useful
                 * for sinks that only accept one kind of essence and
                 * should not be held hostage to a mid-GOP frame that
                 * will never be written to them.
                 *
                 * An empty essence list for the selected scope is treated
                 * as trivially safe (nothing to truncate).  Null entries
                 * are skipped rather than rejected — the pipeline builds
                 * lists that always carry valid pointers.
                 *
                 * @param scope Which essence streams to consider.
                 * @return @c true when the cut is safe for every payload
                 *         in scope.
                 */
                bool isSafeCutPoint(CutPointScope scope = CutPointAudioVideo) const;

                /**
                 * @brief Returns a human-readable multi-line dump of
                 *        this frame's full contents.
                 *
                 * Emits the scalar-key block registered with
                 * @c VariantLookup<Frame> (@c VideoCount,
                 * @c AudioCount, @c HasBenchmark, @c PayloadCount,
                 * @c VideoFormat), the frame's metadata via
                 * @ref Metadata::dump, the @ref configUpdate delta
                 * when non-empty, and then a subdump of each payload
                 * in @ref payloadList indented by two spaces.  Each
                 * payload section is headed with the kind name and
                 * per-kind index (@c "Video[0]:", @c "Audio[0]:",
                 * @c "Subtitle[0]:", …) and its body is the
                 * concrete leaf's full @ref VariantLookup dump.
                 *
                 * No leading @c "Frame:" header is emitted — callers
                 * that need one (e.g. @c pmdf-inspect printing a
                 * frame index) prepend their own title and an
                 * additional indent so the output is uniform
                 * regardless of whether one frame or many are being
                 * printed.
                 *
                 * @param indent Leading whitespace to prefix every line.
                 * @return A @ref StringList (one entry per line).
                 */
                StringList dump(const String &indent = String()) const;

        private:
                MediaPayload::PtrList _payloads;
                Metadata              _metadata;
                Benchmark::Ptr        _benchmark;
                MediaConfig           _configUpdate;
};

PROMEKI_NAMESPACE_END
