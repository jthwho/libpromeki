/**
 * @file      mediaiodescription.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiodescription.h>

#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Helpers
// ============================================================================

namespace {

        // Renders one MediaDesc into a single human-readable line.  Used by
        // summary() for CLI / UI display and by toJson() to emit format lists
        // as readable strings.  The JSON form is intentionally one-way (lossy
        // for round-trip) — round-trip lives on the DataStream operators.
        String shortMediaDescLine(const MediaDesc &desc) {
                String line;
                if (desc.frameRate().isValid()) {
                        line += desc.frameRate().toString();
                        line += "  ";
                }
                if (!desc.imageList().isEmpty()) {
                        for (size_t i = 0; i < desc.imageList().size(); ++i) {
                                if (i) line += " | ";
                                line += desc.imageList()[i].toString();
                        }
                }
                if (!desc.audioList().isEmpty()) {
                        if (!line.isEmpty()) line += "  +  ";
                        for (size_t i = 0; i < desc.audioList().size(); ++i) {
                                if (i) line += " | ";
                                line += desc.audioList()[i].toString();
                        }
                }
                if (line.isEmpty()) line = "<empty>";
                return line;
        }

        void appendRoles(StringList &out, const MediaIODescription &d) {
                String roles;
                bool   first = true;
                auto   add = [&](const char *label) {
                        if (!first) roles += ", ";
                        roles += label;
                        first = false;
                };
                if (d.canBeSource()) add("source");
                if (d.canBeSink()) add("sink");
                if (d.canBeTransform()) add("transform");
                if (roles.isEmpty()) roles = "(none)";
                out.pushToBack(String("  Roles:        ") + roles);
        }

} // namespace

// ============================================================================
// summary()
// ============================================================================

StringList MediaIODescription::summary() const {
        StringList out;

        // Identity line: "TPG  [name=tpg-1]"
        String hdr;
        if (!_backendName.isEmpty())
                hdr += _backendName;
        else
                hdr += "<unknown backend>";
        if (!_name.isEmpty()) {
                hdr += "  [name=";
                hdr += _name;
                hdr += "]";
        }
        out.pushToBack(hdr);

        if (!_backendDescription.isEmpty()) {
                out.pushToBack(String("  Description:  ") + _backendDescription);
        }

        appendRoles(out, *this);

        // Capabilities
        String caps;
        caps += _canSeek ? "seekable" : "non-seekable";
        if (_frameCount.isInfinite()) {
                caps += ", infinite";
        } else if (_frameCount.isUnknown()) {
                caps += ", frame-count=unknown";
        } else if (_frameCount.isFinite()) {
                caps += ", frames=";
                caps += String::number(_frameCount.value());
        }
        if (_frameRate.isValid()) {
                caps += ", rate=";
                caps += _frameRate.toString();
        }
        out.pushToBack(String("  Capabilities: ") + caps);

        if (_preferredFormat.isValid()) {
                out.pushToBack(String("  Preferred:    ") + shortMediaDescLine(_preferredFormat));
        }

        if (!_producibleFormats.isEmpty()) {
                out.pushToBack(String("  Producible (") + String::number(_producibleFormats.size()) + "):");
                for (size_t i = 0; i < _producibleFormats.size(); ++i) {
                        out.pushToBack(String("    - ") + shortMediaDescLine(_producibleFormats[i]));
                }
        }

        if (!_acceptableFormats.isEmpty()) {
                out.pushToBack(String("  Acceptable (") + String::number(_acceptableFormats.size()) + "):");
                for (size_t i = 0; i < _acceptableFormats.size(); ++i) {
                        out.pushToBack(String("    - ") + shortMediaDescLine(_acceptableFormats[i]));
                }
        }

        if (!_containerMetadata.isEmpty()) {
                out.pushToBack("  Metadata:");
                const StringList md = _containerMetadata.dump();
                for (size_t i = 0; i < md.size(); ++i) {
                        out.pushToBack(String("    ") + md[i]);
                }
        }

        if (_probeStatus.isError() || !_probeMessage.isEmpty()) {
                String line = "  Probe:        ";
                line += _probeStatus.name();
                if (!_probeMessage.isEmpty()) {
                        line += " - ";
                        line += _probeMessage;
                }
                out.pushToBack(line);
        }

        return out;
}

// ============================================================================
// JSON serialization
// ============================================================================
//
// JSON is for human / tooling consumption (CLI, REST, GUI dumps).  The
// MediaDesc lists are emitted as readable summary strings, so the JSON
// shape is one-way: faithful to humans but not a round-trip vehicle for
// the format lists.  When the planner needs lossless round-trip it uses
// the DataStream operators below.

JsonObject MediaIODescription::toJson() const {
        JsonObject j;
        if (!_backendName.isEmpty()) j.set("backendName", _backendName);
        if (!_backendDescription.isEmpty()) j.set("backendDescription", _backendDescription);
        if (!_name.isEmpty()) j.set("name", _name);

        // Role flags emitted as an array of role strings; absence
        // = false, presence = true.  Compact and self-describing.
        JsonArray roles;
        if (_canBeSource) roles.add(String("source"));
        if (_canBeSink) roles.add(String("sink"));
        if (_canBeTransform) roles.add(String("transform"));
        if (roles.size() > 0) j.set("roles", roles);

        if (!_producibleFormats.isEmpty()) {
                JsonArray arr;
                for (size_t i = 0; i < _producibleFormats.size(); ++i) {
                        arr.add(shortMediaDescLine(_producibleFormats[i]));
                }
                j.set("producibleFormats", arr);
        }
        if (!_acceptableFormats.isEmpty()) {
                JsonArray arr;
                for (size_t i = 0; i < _acceptableFormats.size(); ++i) {
                        arr.add(shortMediaDescLine(_acceptableFormats[i]));
                }
                j.set("acceptableFormats", arr);
        }
        if (_preferredFormat.isValid()) {
                j.set("preferredFormat", shortMediaDescLine(_preferredFormat));
        }

        if (_canSeek) j.set("canSeek", true);
        if (!_frameCount.isUnknown()) j.set("frameCount", _frameCount.toString());
        if (_frameRate.isValid()) j.set("frameRate", _frameRate.toString());
        if (!_containerMetadata.isEmpty()) j.set("containerMetadata", _containerMetadata.toJson());

        // Error code persisted as the underlying integer for a
        // round-trip-safe path (Error::name() has no inverse today).
        if (_probeStatus.isError()) {
                j.set("probeStatusCode", int64_t(_probeStatus.code()));
                j.set("probeStatusName", _probeStatus.name());
        }
        if (!_probeMessage.isEmpty()) j.set("probeMessage", _probeMessage);
        return j;
}

MediaIODescription MediaIODescription::fromJson(const JsonObject &obj, Error *err) {
        MediaIODescription d;
        bool               good = true;

        if (obj.contains("backendName")) d._backendName = obj.getString("backendName");
        if (obj.contains("backendDescription")) d._backendDescription = obj.getString("backendDescription");
        if (obj.contains("name")) d._name = obj.getString("name");

        if (obj.valueIsArray("roles")) {
                JsonArray roles = obj.getArray("roles");
                for (int i = 0; i < roles.size(); ++i) {
                        const String r = roles.getString(i);
                        if (r == "source")
                                d._canBeSource = true;
                        else if (r == "sink")
                                d._canBeSink = true;
                        else if (r == "transform")
                                d._canBeTransform = true;
                        else {
                                promekiWarn("MediaIODescription::fromJson: unknown role '%s'.", r.cstr());
                                good = false;
                        }
                }
        }

        // Format lists are intentionally one-way through JSON — the
        // DataStream form carries the lossless representation.  We
        // leave the parsed lists empty so callers know not to trust
        // them after a JSON round-trip.

        if (obj.contains("canSeek")) d._canSeek = obj.getBool("canSeek");
        if (obj.contains("frameCount")) {
                d._frameCount = FrameCount::fromString(obj.getString("frameCount"));
        }
        if (obj.contains("frameRate")) {
                Result<FrameRate> r = FrameRate::fromString(obj.getString("frameRate"));
                if (r.second().isOk()) {
                        d._frameRate = r.first();
                } else {
                        promekiWarn("MediaIODescription::fromJson: invalid frameRate.");
                        good = false;
                }
        }
        if (obj.valueIsObject("containerMetadata")) {
                Error merr;
                d._containerMetadata = Metadata::fromJson(obj.getObject("containerMetadata"), &merr);
                if (merr.isError()) good = false;
        }

        if (obj.contains("probeStatusCode")) {
                d._probeStatus = Error(static_cast<Error::Code>(obj.getInt("probeStatusCode")));
        }
        if (obj.contains("probeMessage")) d._probeMessage = obj.getString("probeMessage");

        if (err) *err = good ? Error::Ok : Error::Invalid;
        return d;
}

// ============================================================================
// Equality
// ============================================================================

bool MediaIODescription::operator==(const MediaIODescription &other) const {
        return _backendName == other._backendName && _backendDescription == other._backendDescription &&
               _name == other._name && _canBeSource == other._canBeSource && _canBeSink == other._canBeSink &&
               _canBeTransform == other._canBeTransform && _producibleFormats == other._producibleFormats &&
               _acceptableFormats == other._acceptableFormats && _preferredFormat == other._preferredFormat &&
               _canSeek == other._canSeek && _frameCount == other._frameCount && _frameRate == other._frameRate &&
               _containerMetadata == other._containerMetadata && _probeStatus == other._probeStatus &&
               _probeMessage == other._probeMessage;
}

// ============================================================================
// DataStream
// ============================================================================

DataStream &operator<<(DataStream &stream, const MediaIODescription &d) {
        stream.writeTag(DataStream::TypeMediaIODescription);
        stream << d.backendName();
        stream << d.backendDescription();
        stream << d.name();
        stream << d.canBeSource();
        stream << d.canBeSink();
        stream << d.canBeTransform();
        stream << d.producibleFormats();
        stream << d.acceptableFormats();
        stream << d.preferredFormat();
        stream << d.canSeek();
        stream << d.frameCount();
        stream << d.frameRate();
        stream << d.containerMetadata();
        stream << static_cast<uint32_t>(d.probeStatus().code());
        stream << d.probeMessage();
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaIODescription &d) {
        d = MediaIODescription();
        if (!stream.readTag(DataStream::TypeMediaIODescription)) return stream;

        String          backendName, backendDescription, name, probeMessage;
        bool            canBeSource = false, canBeSink = false, canBeTransform = false;
        MediaDesc::List producibleFormats, acceptableFormats;
        MediaDesc       preferredFormat;
        bool            canSeek = false;
        FrameCount      frameCount;
        FrameRate       frameRate;
        Metadata        containerMetadata;
        uint32_t        probeStatusValue = 0;

        stream >> backendName;
        stream >> backendDescription;
        stream >> name;
        stream >> canBeSource;
        stream >> canBeSink;
        stream >> canBeTransform;
        stream >> producibleFormats;
        stream >> acceptableFormats;
        stream >> preferredFormat;
        stream >> canSeek;
        stream >> frameCount;
        stream >> frameRate;
        stream >> containerMetadata;
        stream >> probeStatusValue;
        stream >> probeMessage;

        if (stream.status() != DataStream::Ok) return stream;

        d.setBackendName(backendName);
        d.setBackendDescription(backendDescription);
        d.setName(name);
        d.setCanBeSource(canBeSource);
        d.setCanBeSink(canBeSink);
        d.setCanBeTransform(canBeTransform);
        d.producibleFormats() = std::move(producibleFormats);
        d.acceptableFormats() = std::move(acceptableFormats);
        d.setPreferredFormat(preferredFormat);
        d.setCanSeek(canSeek);
        d.setFrameCount(frameCount);
        d.setFrameRate(frameRate);
        d.containerMetadata() = std::move(containerMetadata);
        d.setProbeStatus(Error(static_cast<Error::Code>(probeStatusValue)));
        d.setProbeMessage(probeMessage);

        return stream;
}

PROMEKI_NAMESPACE_END
