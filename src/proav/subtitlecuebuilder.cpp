/**
 * @file      subtitlecuebuilder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Stateful cue-shaping policy that turns a stream of @ref Transcript
 * utterances into @ref Subtitle cues per the @c SubtitleCue*
 * @ref MediaConfig keys.  The streaming @c submitFrame /
 * @c receiveFrame surface and the static @c buildCues batch helper
 * share the same policy implementation; only state lifetime differs.
 */

#include <algorithm>
#include <promeki/audiopayload.h>
#include <promeki/ancpayload.h>
#include <promeki/deque.h>
#include <promeki/duration.h>
#include <promeki/enums_subtitle.h>
#include <promeki/frame.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/subtitle.h>
#include <promeki/subtitlecuebuilder.h>
#include <promeki/transcript.h>
#include <promeki/variant.h>
#include <promeki/videopayload.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Static-resolved policy snapshot read from a
        ///        MediaConfig.  Lets the batch helper avoid stamping
        ///        an instance, and the streaming impl re-snapshot
        ///        cheaply on every @c configure.
        struct CuePolicy {
                        int            maxCharsPerLine = 42;
                        int            maxLines        = 2;
                        Duration       minDuration     = Duration::fromMilliseconds(1000);
                        Duration       maxDuration     = Duration::fromMilliseconds(7000);
                        bool           emitPartials    = false;
                        SubtitleAnchor anchor          = SubtitleAnchor::BottomCenter;
        };

        CuePolicy readPolicy(const MediaConfig &cfg) {
                CuePolicy p;
                p.maxCharsPerLine = cfg.getAs<int32_t>(MediaConfig::SubtitleCueMaxCharsPerLine, p.maxCharsPerLine);
                p.maxLines        = cfg.getAs<int32_t>(MediaConfig::SubtitleCueMaxLines, p.maxLines);
                p.minDuration     = cfg.getAs<Duration>(MediaConfig::SubtitleCueMinDuration, p.minDuration);
                p.maxDuration     = cfg.getAs<Duration>(MediaConfig::SubtitleCueMaxDuration, p.maxDuration);
                p.emitPartials    = cfg.getAs<bool>(MediaConfig::SubtitleCueEmitPartials, p.emitPartials);
                // Only override the C++ default when the caller
                // explicitly set the anchor — asEnum on an unset key
                // falls back to the enum's registered default
                // (SubtitleAnchor::Default), which is meaningful but
                // not the same as "the user chose Default."
                if (cfg.contains(MediaConfig::SubtitleCueAnchor)) {
                        Enum anchorEnum = cfg.get(MediaConfig::SubtitleCueAnchor).asEnum(SubtitleAnchor::Type);
                        if (anchorEnum.isValid()) p.anchor = SubtitleAnchor(anchorEnum.value());
                }
                return p;
        }

        /// @brief Shapes one Transcript into one Subtitle per the policy.
        ///
        /// Returns a default-constructed (invalid) Subtitle when the
        /// transcript is empty or should be filtered out by the
        /// partial-gating policy.  Honours min / max display
        /// duration, wraps text to maxCharsPerLine × maxLines, and
        /// stamps anchor + speaker + partial onto the cue.
        Subtitle shapeOne(const Transcript &tr, const CuePolicy &p) {
                if (tr.isEmpty()) return Subtitle();
                if (tr.partial() && !p.emitPartials) return Subtitle();

                TimeStamp start = tr.start();
                TimeStamp end   = tr.end();
                if (!start.isValid() || !end.isValid()) return Subtitle();

                // Clamp display duration into [minDuration, maxDuration].
                const int64_t durNs   = end.nanoseconds() - start.nanoseconds();
                const int64_t minNs   = p.minDuration.nanoseconds();
                const int64_t maxNs   = p.maxDuration.nanoseconds();
                if (durNs < minNs) {
                        end = TimeStamp(start.nanoseconds() + minNs);
                } else if (durNs > maxNs) {
                        end = TimeStamp(start.nanoseconds() + maxNs);
                }

                Subtitle cue(start, end, tr.text(), p.anchor);
                cue.setSpeaker(tr.speaker());
                cue.setPartial(tr.partial());
                if (p.maxCharsPerLine > 0) {
                        cue = cue.wrapped(p.maxCharsPerLine, p.maxLines);
                }
                return cue;
        }

        /// @brief Builds an output Frame echoing @p source's payloads
        ///        through and stamping @p cue on the metadata.
        ///
        /// Mirrors @ref TranscriptionEngine::buildOutputFrame: every
        /// payload echoed unchanged, then @c Metadata::Subtitle set
        /// to the shaped cue.  Source's @c Metadata::Transcript is
        /// preserved so downstream consumers can still see the raw
        /// data alongside the shaped cue.
        Frame buildShapedFrame(const Frame &source, const Subtitle &cue) {
                Frame out;
                if (source.isValid()) {
                        out.metadata() = source.metadata();
                        out.setCaptureTime(source.captureTime());
                        for (const VideoPayload::Ptr &vp : source.videoPayloads()) {
                                if (vp.isValid()) out.addPayload(vp);
                        }
                        for (const AudioPayload::Ptr &ap : source.audioPayloads()) {
                                if (ap.isValid()) out.addPayload(ap);
                        }
                        for (const AncPayload::Ptr &anc : source.ancPayloads()) {
                                if (anc.isValid()) out.addPayload(anc);
                        }
                }
                if (cue.isEmpty()) {
                        // Filtered transcript — echo the frame through
                        // without stamping a cue.  Lets non-transcription
                        // frames flow past the builder unchanged.
                        return out;
                }
                out.metadata().set(Metadata::Subtitle, Variant(cue));
                return out;
        }

} // namespace

// ============================================================================
// Pimpl
// ============================================================================

struct SubtitleCueBuilderImpl {
                CuePolicy    policy;
                Deque<Frame> queue;
};

// ============================================================================
// Construction / destruction
// ============================================================================

SubtitleCueBuilder::SubtitleCueBuilder() : _d(UniquePtr<SubtitleCueBuilderImpl>::create()) {}

SubtitleCueBuilder::SubtitleCueBuilder(const MediaConfig &config)
    : _d(UniquePtr<SubtitleCueBuilderImpl>::create()) {
        _d->policy = readPolicy(config);
}

SubtitleCueBuilder::~SubtitleCueBuilder() = default;

void SubtitleCueBuilder::configure(const MediaConfig &config) {
        _d->policy = readPolicy(config);
}

// ============================================================================
// Streaming submit / receive
// ============================================================================

Error SubtitleCueBuilder::submitFrame(const Frame &frame) {
        if (!frame.isValid()) return Error::Invalid;

        // Pass-through case — no Transcript on this Frame.  Forward
        // it unchanged so the builder is safe to wire into a pipeline
        // graph that carries non-transcription Frames too.
        if (!frame.metadata().contains(Metadata::Transcript)) {
                _d->queue.pushToBack(frame);
                return Error::Ok;
        }

        Transcript tr  = frame.metadata().get(Metadata::Transcript).get<Transcript>();
        Subtitle   cue = shapeOne(tr, _d->policy);
        if (cue.isEmpty()) {
                // Transcript was present but the policy filtered it
                // (most commonly: partial transcript with
                // SubtitleCueEmitPartials off).  Suppress the Frame
                // entirely — partials are deliberate noise we don't
                // want to surface downstream.
                return Error::Ok;
        }
        _d->queue.pushToBack(buildShapedFrame(frame, cue));
        return Error::Ok;
}

Frame SubtitleCueBuilder::receiveFrame() {
        if (_d->queue.isEmpty()) return Frame();
        return _d->queue.popFromFront();
}

void SubtitleCueBuilder::reset() {
        _d->queue.clear();
}

// ============================================================================
// Batch helper
// ============================================================================

SubtitleList SubtitleCueBuilder::buildCues(const TranscriptList &transcripts, const MediaConfig &config) {
        const CuePolicy policy = readPolicy(config);
        SubtitleList    out;
        out.reserve(transcripts.size());
        for (size_t i = 0; i < transcripts.size(); ++i) {
                Subtitle cue = shapeOne(transcripts[i], policy);
                if (!cue.isEmpty()) out.append(cue);
        }
        return out;
}

PROMEKI_NAMESPACE_END
