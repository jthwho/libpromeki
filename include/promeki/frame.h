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
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/ancpayload.h>
#include <promeki/mediatimestamp.h>
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
 * @par Copy semantics
 * Frame is a value-type handle wrapping a @c SharedPtr<Data> so
 * copies are O(1) (refcount increment, no deep copy) and a single
 * Frame can flow through pipeline strands by value.  Mutation
 * triggers copy-on-write — the @c Data block is detached the
 * first time a mutator runs against a shared instance.  This is
 * the same pattern @ref String and @ref VariantDatabase use.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently — the underlying
 * refcount on the shared @c Data is atomic, so copying a Frame
 * across threads is safe even when the source is in use elsewhere.
 * Concurrent mutation of a single instance (@c addPayload,
 * @c metadata(), @c setConfigUpdate, ...) must be externally
 * synchronized.
 */
class Frame {
        public:
                /** @brief Plain value list of Frame objects. */
                using List = ::promeki::List<Frame>;

                /** @brief Constructs an empty frame with no images, audio, or metadata. */
                Frame() = default;

                /**
                 * @brief Returns true when this Frame carries any content.
                 *
                 * The receive side of every codec / MediaIO drain loop
                 * uses this as the "got a real Frame?" gate, so an
                 * empty Frame &mdash; no payloads, empty metadata, no
                 * captureTime, no configUpdate &mdash; reports
                 * @c false.  This matches the @c return Frame();
                 * sentinel that backends emit from @c receiveFrame()
                 * and @c readFrame() when no frame is ready, and it
                 * matches input-gate checks like
                 * @c if(!cmd.frame.isValid()) that test "did the
                 * producer hand me anything?" before doing work.
                 *
                 * A Frame with a moved-from / null internal storage
                 * handle also reports @c false.  Any non-trivial
                 * mutation (@ref addPayload, @ref metadata "metadata()&",
                 * @ref setCaptureTime, @ref setConfigUpdate) makes the
                 * Frame valid.
                 */
                bool isValid() const {
                        if (!_d.isValid()) return false;
                        return !_d->_payloads.isEmpty() || !_d->_metadata.isEmpty()
                                || _d->_captureTime.isValid() || !_d->_configUpdate.isEmpty();
                }

                /**
                 * @brief Returns a const reference to the list of media payloads.
                 * @return The payload pointer list.
                 */
                const MediaPayload::PtrList &payloadList() const { return _d->_payloads; }

                /**
                 * @brief Returns a mutable reference to the list of media payloads.
                 *
                 * Triggers copy-on-write — the underlying @c Data
                 * is detached if it is currently shared with other
                 * Frame handles.
                 *
                 * @return The payload pointer list.
                 */
                MediaPayload::PtrList &payloadList() { return _d.modify()->_payloads; }

                /**
                 * @brief Appends a payload to @ref payloadList.
                 */
                void addPayload(MediaPayload::Ptr p) { _d.modify()->_payloads.pushToBack(std::move(p)); }

                /**
                 * @brief Returns the Video-kind entries from
                 *        @ref payloadList as typed pointers.
                 *
                 * Walks @ref payloadList once and collects every
                 * @ref MediaPayload whose @c kind is
                 * @c MediaPayloadKind::Video, returning them as a
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
                 * @brief Returns the Audio-kind entries from
                 *        @ref payloadList as typed pointers.
                 *
                 * @return A fresh @ref AudioPayload::PtrList of all
                 *         audio-kind payloads in payload-list order.
                 * @sa videoPayloads
                 */
                AudioPayload::PtrList audioPayloads() const;

                /**
                 * @brief Returns the AncillaryData-kind entries from
                 *        @ref payloadList as typed @ref AncPayload pointers.
                 *
                 * Mirrors @ref videoPayloads / @ref audioPayloads:
                 * walks the payload list, filters by @c kind() ==
                 * @c MediaPayloadKind::AncillaryData, and downcasts
                 * each match to an @ref AncPayload::Ptr.  Null
                 * payload entries are skipped.
                 *
                 * @return A fresh @ref AncPayload::PtrList of every
                 *         ANC payload on this frame, in payload-list
                 *         order.
                 */
                AncPayload::PtrList ancPayloads() const;

                /**
                 * @brief Returns a const reference to the frame metadata.
                 * @return The metadata container.
                 */
                const Metadata &metadata() const { return _d->_metadata; }

                /**
                 * @brief Returns a mutable reference to the frame metadata.
                 *
                 * Triggers copy-on-write — the underlying @c Data
                 * is detached if it is currently shared.
                 *
                 * @return The metadata container.
                 */
                Metadata &metadata() { return _d.modify()->_metadata; }

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
                 * @brief Returns the frame-level capture timestamp.
                 *
                 * Distinct from per-payload @c pts() — the
                 * captureTime represents the instant the source
                 * captured this Frame as a whole (the analog of
                 * "shutter open" for a camera, "first sample of
                 * buffer" for an audio capture, "render-completion
                 * time" for a synthetic source).  Per-essence pts
                 * carries the timing for that specific essence,
                 * which can differ from the Frame's wallclock
                 * capture instant when the backend re-derives the
                 * frame (e.g. CSC or framesync producing output at
                 * a moment that has nothing to do with capture).
                 *
                 * Backends that own a hardware capture clock
                 * (V4L2, NDI, ST 2110 RX, PTP-locked sources)
                 * fill in their authoritative capture instant via
                 * @ref setCaptureTime before the Frame leaves the
                 * backend.  Backends without one rely on the
                 * MediaIO write-path default-stamper, which fills
                 * in @c (TimeStamp::now(), ClockDomain::SystemMonotonic)
                 * if the inbound Frame still has none — mirroring
                 * the MediaIO default-stamping behaviour for
                 * per-payload @c pts.
                 *
                 * Used by RTP TX to derive the SR's NTP timestamp
                 * from a single observed instant per opening, so
                 * receivers can correlate cross-stream capture
                 * timing instead of having to assume the wire
                 * emission instant maps cleanly to capture.
                 *
                 * @return The capture timestamp, or an invalid
                 *         @ref MediaTimeStamp if none was set.
                 */
                const MediaTimeStamp &captureTime() const { return _d->_captureTime; }

                /**
                 * @brief Sets the frame-level capture timestamp.
                 *
                 * Overwrites whatever timestamp was previously
                 * present.  Triggers copy-on-write — the
                 * underlying @c Data is detached if it is
                 * currently shared.  CoW Frame copies preserve
                 * the timestamp without restamping, so a Frame
                 * that arrived with an authoritative timestamp
                 * keeps it across pipeline copies even if a
                 * downstream stage clones the Frame.
                 *
                 * @param ts The capture timestamp to record.
                 */
                void setCaptureTime(const MediaTimeStamp &ts) { _d.modify()->_captureTime = ts; }

                /**
                 * @brief Returns a const reference to the config update delta.
                 *
                 * When non-empty, the config update is applied by the
                 * receiving @ref MediaIO before the frame is
                 * processed.  This gives frame-level synchronisation
                 * for dynamic parameter changes (bitrate, quality,
                 * etc.) — the update travels with the write command
                 * through the strand queue, so a flush can never
                 * separate a config change from its frame.
                 */
                const MediaConfig &configUpdate() const { return _d->_configUpdate; }

                /** @brief Returns a mutable reference to the config update delta. */
                MediaConfig &configUpdate() { return _d.modify()->_configUpdate; }

                /** @brief Replaces the config update delta. */
                void setConfigUpdate(MediaConfig cfg) { _d.modify()->_configUpdate = std::move(cfg); }

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
                 * @c AudioCount, @c PayloadCount,
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
                /**
                 * @brief Internal CoW storage for the frame's payloads,
                 *        metadata, and config-update delta.
                 *
                 * Held via @c SharedPtr<Data> so copying a Frame is
                 * O(1) and the contents are only deep-copied when one
                 * of the aliased handles is mutated.  The fields keep
                 * their leading-underscore names from the pre-refactor
                 * @c Frame layout so the diff against history stays
                 * easy to read.
                 */
                struct Data {
                                PROMEKI_SHARED_FINAL(Data)
                                MediaPayload::PtrList _payloads;
                                Metadata              _metadata;
                                MediaConfig           _configUpdate;
                                MediaTimeStamp        _captureTime;
                };
                SharedPtr<Data> _d = SharedPtr<Data>::create();
};

PROMEKI_NAMESPACE_END
