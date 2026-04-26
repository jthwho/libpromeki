/**
 * @file      pipelineevent.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pipelineevent.h>

#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

constexpr const char *kKindStateChanged = "StateChanged";
constexpr const char *kKindStageState   = "StageState";
constexpr const char *kKindStageError   = "StageError";
constexpr const char *kKindStatsUpdated = "StatsUpdated";
constexpr const char *kKindPlanResolved = "PlanResolved";
constexpr const char *kKindLog          = "Log";

bool isComplexKind(PipelineEvent::Kind k) {
        return k == PipelineEvent::Kind::StatsUpdated
            || k == PipelineEvent::Kind::PlanResolved;
}

} // namespace

String PipelineEvent::kindToString(Kind k) {
        switch(k) {
                case Kind::StateChanged: return String(kKindStateChanged);
                case Kind::StageState:   return String(kKindStageState);
                case Kind::StageError:   return String(kKindStageError);
                case Kind::StatsUpdated: return String(kKindStatsUpdated);
                case Kind::PlanResolved: return String(kKindPlanResolved);
                case Kind::Log:          return String(kKindLog);
        }
        return String(kKindStateChanged);
}

PipelineEvent::Kind PipelineEvent::kindFromString(const String &s, bool *ok) {
        if(ok != nullptr) *ok = true;
        if(s == kKindStateChanged) return Kind::StateChanged;
        if(s == kKindStageState)   return Kind::StageState;
        if(s == kKindStageError)   return Kind::StageError;
        if(s == kKindStatsUpdated) return Kind::StatsUpdated;
        if(s == kKindPlanResolved) return Kind::PlanResolved;
        if(s == kKindLog)          return Kind::Log;
        if(ok != nullptr) *ok = false;
        return Kind::StateChanged;
}

JsonObject PipelineEvent::toJson() const {
        JsonObject j;
        j.set("kind", kindToString(_kind));
        if(!_stageName.isEmpty()) j.set("stage", _stageName);
        j.set("ts", _ts.seconds());
        if(!_metadata.isEmpty()) j.set("metadata", _metadata.toJson());

        if(isComplexKind(_kind)) {
                j.set("payload", _jsonPayload);
        } else if(_payload.isValid()) {
                j.setFromVariant("payload", _payload);
        }
        return j;
}

PipelineEvent PipelineEvent::fromJson(const JsonObject &obj, Error *err) {
        PipelineEvent ev;

        if(!obj.contains("kind")) {
                promekiWarn("PipelineEvent::fromJson: missing 'kind' field.");
                if(err != nullptr) *err = Error::ParseFailed;
                return ev;
        }

        Error kindErr;
        const String kindStr = obj.getString("kind", &kindErr);
        if(kindErr.isError()) {
                promekiWarn("PipelineEvent::fromJson: 'kind' is not a string.");
                if(err != nullptr) *err = Error::Invalid;
                return ev;
        }
        bool kindOk = false;
        ev._kind = kindFromString(kindStr, &kindOk);
        if(!kindOk) {
                promekiWarn("PipelineEvent::fromJson: unknown kind '%s'.",
                            kindStr.cstr());
                if(err != nullptr) *err = Error::ParseFailed;
                return ev;
        }

        if(obj.contains("stage")) {
                ev._stageName = obj.getString("stage");
        }

        if(obj.contains("ts")) {
                Error tsErr;
                double secs = obj.getDouble("ts", &tsErr);
                if(tsErr.isOk()) {
                        TimeStamp ts;
                        ts.setValue(TimeStamp::Value(TimeStamp::secondsToDuration(secs)));
                        ev._ts = ts;
                }
        }

        if(obj.valueIsObject("metadata")) {
                Error mErr;
                ev._metadata = Metadata::fromJson(obj.getObject("metadata"), &mErr);
                if(mErr.isError()) {
                        promekiWarn("PipelineEvent::fromJson: metadata block invalid.");
                }
        }

        if(obj.contains("payload")) {
                if(isComplexKind(ev._kind)) {
                        if(obj.valueIsObject("payload")) {
                                ev._jsonPayload = obj.getObject("payload");
                        } else {
                                promekiWarn("PipelineEvent::fromJson: kind '%s' "
                                            "expects an object payload.",
                                            kindStr.cstr());
                        }
                } else if(obj.valueIsObject("payload") || obj.valueIsArray("payload")) {
                        promekiWarn("PipelineEvent::fromJson: kind '%s' "
                                    "expects a primitive payload.",
                                    kindStr.cstr());
                } else {
                        Variant tmp;
                        obj.forEach([&](const String &key, const Variant &val) {
                                if(key == "payload") tmp = val;
                        });
                        ev._payload = tmp;
                }
        }

        if(err != nullptr) *err = Error::Ok;
        return ev;
}

PROMEKI_NAMESPACE_END
