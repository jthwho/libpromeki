/**
 * @file      mediapipelinetrigger.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipelinetrigger.h>

#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

Result<MediaPipelineTrigger::UPtr> MediaPipelineQueryTrigger::parse(const String &expr) {
        auto parsed = VariantQuery<Frame>::parse(expr);
        if (parsed.second().isError()) {
                return makeError<MediaPipelineTrigger::UPtr>(parsed.second());
        }
        MediaPipelineTrigger::UPtr trig =
                MediaPipelineTrigger::UPtr::takeOwnership(new MediaPipelineQueryTrigger(std::move(parsed.first())));
        return makeResult<MediaPipelineTrigger::UPtr>(std::move(trig));
}

PROMEKI_NAMESPACE_END
