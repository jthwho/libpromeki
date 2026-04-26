/**
 * @file      pipelinesettings.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "pipelinesettings.h"

#include <promeki/json.h>

using promeki::Duration;
using promeki::Error;
using promeki::JsonArray;
using promeki::JsonObject;
using promeki::MediaPipelinePlanner;
using promeki::String;
using promeki::StringList;

namespace promekipipeline {

        const String PipelineSettings::DefaultName = String("untitled");

        const Duration PipelineSettings::DefaultStatsInterval = Duration::fromSeconds(1);

        PipelineSettings::PipelineSettings()
            : _name(DefaultName), _statsInterval(DefaultStatsInterval), _quality(DefaultQuality),
              _maxBridgeDepth(DefaultMaxBridgeDepth), _autoplan(DefaultAutoplan) {}

        MediaPipelinePlanner::Policy PipelineSettings::plannerPolicy() const {
                MediaPipelinePlanner::Policy p;
                p.quality = _quality;
                p.maxBridgeDepth = _maxBridgeDepth;
                p.excludedBridges = _excludedBridges;
                return p;
        }

        JsonObject PipelineSettings::toJson() const {
                JsonObject obj;
                obj.set("name", _name);
                obj.set("statsIntervalMs", static_cast<int64_t>(_statsInterval.milliseconds()));
                obj.set("quality", qualityToString(_quality));
                obj.set("maxBridgeDepth", _maxBridgeDepth);
                JsonArray excluded;
                for (const auto &name : _excludedBridges) excluded.add(name);
                obj.set("excludedBridges", excluded);
                obj.set("autoplan", _autoplan);
                return obj;
        }

        PipelineSettings PipelineSettings::fromJson(const JsonObject &obj, Error *err) {
                PipelineSettings out;
                if (err) *err = Error::Ok;
                if (obj.contains("name")) {
                        out._name = obj.getString("name");
                }
                if (obj.contains("statsIntervalMs")) {
                        const int64_t ms = obj.getInt("statsIntervalMs");
                        out._statsInterval = ms > 0 ? Duration::fromMilliseconds(ms) : Duration();
                }
                if (obj.contains("quality")) {
                        bool         ok = false;
                        const String q = obj.getString("quality");
                        out._quality = qualityFromString(q, &ok);
                        if (!ok && err) *err = Error::Invalid;
                }
                if (obj.contains("maxBridgeDepth")) {
                        out._maxBridgeDepth = static_cast<int>(obj.getInt("maxBridgeDepth"));
                }
                if (obj.contains("excludedBridges")) {
                        Error           aerr;
                        const JsonArray list = obj.getArray("excludedBridges", &aerr);
                        if (aerr.isOk()) {
                                StringList copy;
                                for (int i = 0; i < list.size(); ++i) {
                                        copy.pushToBack(list.getString(i));
                                }
                                out._excludedBridges = copy;
                        }
                }
                if (obj.contains("autoplan")) {
                        out._autoplan = obj.getBool("autoplan");
                }
                return out;
        }

        String PipelineSettings::qualityToString(MediaPipelinePlanner::Quality q) {
                switch (q) {
                        case MediaPipelinePlanner::Quality::Highest: return "Highest";
                        case MediaPipelinePlanner::Quality::Balanced: return "Balanced";
                        case MediaPipelinePlanner::Quality::Fastest: return "Fastest";
                        case MediaPipelinePlanner::Quality::ZeroCopyOnly: return "ZeroCopyOnly";
                }
                return "Highest";
        }

        MediaPipelinePlanner::Quality PipelineSettings::qualityFromString(const String &s, bool *ok) {
                if (ok) *ok = true;
                if (s == "Highest") return MediaPipelinePlanner::Quality::Highest;
                if (s == "Balanced") return MediaPipelinePlanner::Quality::Balanced;
                if (s == "Fastest") return MediaPipelinePlanner::Quality::Fastest;
                if (s == "ZeroCopyOnly") return MediaPipelinePlanner::Quality::ZeroCopyOnly;
                if (ok) *ok = false;
                return MediaPipelinePlanner::Quality::Highest;
        }

} // namespace promekipipeline
