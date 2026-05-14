/**
 * @file      mediapipelinetrigger.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <memory>

#include <promeki/function.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/variantquery.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-frame predicate that arms a capture-mode @ref MediaPipeline.
 * @ingroup pipeline
 *
 * A capture pipeline armed via @ref MediaPipeline::armCapture stays in
 * @c CaptureState::Armed (sink gate closed) until its trigger reports
 * @c true on an incoming frame.  At that point the pipeline transitions
 * to @c CaptureState::Recording, opens the sink gate, and stamps
 * @c Metadata::ForceKeyframe on the matched frame so the downstream
 * encoder cuts a clean keyframe at the recording start.
 *
 * Two stock implementations cover the common cases:
 *  - @ref MediaPipelineFunctionTrigger wraps an arbitrary
 *    @c std::function — the most ergonomic form for inline lambdas
 *    that need to inspect non-metadata Frame fields.
 *  - @ref MediaPipelineQueryTrigger wraps a @c VariantQuery<Frame>
 *    expression — the idiomatic form when the predicate is purely a
 *    @c Meta.* lookup ("Meta.Timecode >= '01:00:00:00'",
 *    "Meta.FrameKeyframe == true", etc.) and you want a string
 *    representation that round-trips through config files.
 *
 * Triggers are evaluated on the @ref MediaIOPortConnection 's pump
 * thread for every frame that lands while the capture transport is
 * Armed.  Implementations should be thread-safe and cheap to call.
 */
class MediaPipelineTrigger {
        public:
                /** @brief Unique-ownership pointer alias. */
                using UPtr = ::promeki::UniquePtr<MediaPipelineTrigger>;

                virtual ~MediaPipelineTrigger() = default;

                /**
                 * @brief Evaluates the predicate against @p frame.
                 *
                 * @param frame The frame about to be admitted to the
                 *              capture sink.
                 * @return @c true to fire the trigger and switch
                 *         capture state to @c Recording.
                 */
                virtual bool match(const Frame &frame) = 0;
};

/**
 * @brief @ref MediaPipelineTrigger that wraps an arbitrary callable.
 * @ingroup pipeline
 *
 * The most flexible form — the lambda gets the full @ref Frame and
 * can inspect any combination of payload, metadata, or shape.  Used
 * by @ref MediaPipeline::setCaptureTrigger(Function<bool(const Frame &)>).
 */
class MediaPipelineFunctionTrigger : public MediaPipelineTrigger {
        public:
                using Predicate = Function<bool(const Frame &)>;

                /**
                 * @brief Constructs from a callable.
                 * @param fn The predicate.  An empty function never matches.
                 */
                explicit MediaPipelineFunctionTrigger(Predicate fn) : _fn(std::move(fn)) {}

                bool match(const Frame &frame) override {
                        return _fn ? _fn(frame) : false;
                }

        private:
                Predicate _fn;
};

/**
 * @brief @ref MediaPipelineTrigger backed by a @c VariantQuery<Frame>.
 * @ingroup pipeline
 *
 * The string-driven form — perfect for predicates that live in a
 * config file or are typed by an operator.  The expression is parsed
 * once via @ref parse and then matched cheaply per frame.
 */
class MediaPipelineQueryTrigger : public MediaPipelineTrigger {
        public:
                /**
                 * @brief Parses @p expr into a query trigger.
                 *
                 * On success returns the trigger and @ref Error::Ok.
                 * On failure returns a null pointer and the parse error;
                 * call sites that want the diagnostic detail can
                 * additionally @c VariantQuery<Frame>::parse the same
                 * expression to inspect @c errorDetail.
                 *
                 * @param expr A @c VariantQuery<Frame> expression
                 *             (e.g. @c "Meta.FrameKeyframe == true").
                 * @return The trigger on success; on failure a null
                 *         @c UPtr and the error.
                 */
                static Result<MediaPipelineTrigger::UPtr> parse(const String &expr);

                /**
                 * @brief Constructs from an already-parsed query.
                 *
                 * Used by @ref parse; callers with their own pre-parsed
                 * @c VariantQuery<Frame> may construct directly to
                 * skip the second parse pass.
                 */
                explicit MediaPipelineQueryTrigger(VariantQuery<Frame> query) : _query(std::move(query)) {}

                bool match(const Frame &frame) override {
                        return _query.isValid() && _query.match(frame);
                }

                /** @brief Returns the underlying expression source. */
                const String &source() const { return _query.source(); }

        private:
                VariantQuery<Frame> _query;
};

PROMEKI_NAMESPACE_END
