/**
 * @file      videoreferenceconfig.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/videoreferenceconfig.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Resolves @p lower (already lowercased) against the registered
        // names of @p type, returning the matching integer value or
        // @c -1 when no name matches.
        int lookupEnumValue(const Enum::Type &type, const String &lower) {
                const Enum::ValueList values = Enum::values(type);
                for (size_t i = 0; i < values.size(); ++i) {
                        if (values.at(i).first().toLower() == lower) return values.at(i).second();
                }
                return -1;
        }

} // namespace

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

VideoReferenceConfig::VideoReferenceConfig() : _d(SharedPtr<Impl>::create()) {}

VideoReferenceConfig::VideoReferenceConfig(VideoReferenceSource source, VideoReferenceRateFamily family)
    : _d(SharedPtr<Impl>::create()) {
        Impl *impl = _d.modify();
        impl->source = source;
        impl->family = family;
}

// ---------------------------------------------------------------------------
// Accessors / mutators
// ---------------------------------------------------------------------------

bool VideoReferenceConfig::isValid() const {
        if (_d->source == VideoReferenceSource::FromSignal) return _d->signalPort.isValid();
        return true;
}

VideoReferenceSource VideoReferenceConfig::source() const { return _d->source; }

VideoReferenceRateFamily VideoReferenceConfig::family() const { return _d->family; }

VideoPortRef VideoReferenceConfig::signalPort() const { return _d->signalPort; }

void VideoReferenceConfig::setSource(VideoReferenceSource source) { _d.modify()->source = source; }

void VideoReferenceConfig::setFamily(VideoReferenceRateFamily family) { _d.modify()->family = family; }

void VideoReferenceConfig::setSignalPort(VideoPortRef signalPort) {
        _d.modify()->signalPort = std::move(signalPort);
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool VideoReferenceConfig::operator==(const VideoReferenceConfig &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        return _d->source == other._d->source && _d->family == other._d->family &&
               _d->signalPort == other._d->signalPort;
}

// ---------------------------------------------------------------------------
// String form
// ---------------------------------------------------------------------------

String VideoReferenceConfig::toString() const {
        const String src = _d->source.toString().toLower();
        const String fam = _d->family.toString().toLower();
        if (_d->source == VideoReferenceSource::FromSignal) {
                return src + String(":") + _d->signalPort.toString() + String(":") + fam;
        }
        return src + String(":") + fam;
}

Result<VideoReferenceConfig> VideoReferenceConfig::fromString(const String &s) {
        String trimmed = s.trim();
        if (trimmed.isEmpty()) return makeError<VideoReferenceConfig>(Error::InvalidArgument);

        const String lower = trimmed.toLower();

        // Split on every ':' so we can branch on segment count for the
        // FromSignal three-segment form.
        StringList segments;
        {
                size_t       start = 0;
                const size_t n = lower.length();
                for (size_t i = 0; i <= n; ++i) {
                        if (i == n || lower.charAt(i) == ':') {
                                segments.pushToBack(lower.substr(start, i - start).trim());
                                start = i + 1;
                        }
                }
        }
        if (segments.size() < 2 || segments.size() > 3) {
                return makeError<VideoReferenceConfig>(Error::InvalidArgument);
        }
        for (size_t i = 0; i < segments.size(); ++i) {
                if (segments.at(i).isEmpty()) return makeError<VideoReferenceConfig>(Error::InvalidArgument);
        }

        const int sourceValue = lookupEnumValue(VideoReferenceSource::Type, segments.at(0));
        if (sourceValue < 0) return makeError<VideoReferenceConfig>(Error::InvalidArgument);
        const VideoReferenceSource source(sourceValue);

        if (source == VideoReferenceSource::FromSignal) {
                if (segments.size() != 3) return makeError<VideoReferenceConfig>(Error::InvalidArgument);
                Result<VideoPortRef> portR = VideoPortRef::fromString(segments.at(1));
                if (portR.second().isError()) return makeError<VideoReferenceConfig>(portR.second());
                const int familyValue = lookupEnumValue(VideoReferenceRateFamily::Type, segments.at(2));
                if (familyValue < 0) return makeError<VideoReferenceConfig>(Error::InvalidArgument);
                VideoReferenceConfig out(source, VideoReferenceRateFamily(familyValue));
                out.setSignalPort(portR.first());
                return makeResult<VideoReferenceConfig>(std::move(out));
        }

        if (segments.size() != 2) return makeError<VideoReferenceConfig>(Error::InvalidArgument);
        const int familyValue = lookupEnumValue(VideoReferenceRateFamily::Type, segments.at(1));
        if (familyValue < 0) return makeError<VideoReferenceConfig>(Error::InvalidArgument);
        return makeResult<VideoReferenceConfig>(
                VideoReferenceConfig(source, VideoReferenceRateFamily(familyValue)));
}

// ---------------------------------------------------------------------------
// DataStream serialization (member-API path for PROMEKI_DATATYPE)
// ---------------------------------------------------------------------------

Error VideoReferenceConfig::writeToStream(DataStream &s) const {
        s << _d->source;
        s << _d->family;
        s << _d->signalPort;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<VideoReferenceConfig> VideoReferenceConfig::readFromStream<1>(DataStream &s) {
        VideoReferenceSource     source;
        VideoReferenceRateFamily family;
        VideoPortRef             signalPort;
        s >> source;
        s >> family;
        s >> signalPort;
        if (s.status() != DataStream::Ok) return makeError<VideoReferenceConfig>(s.toError());
        VideoReferenceConfig out(source, family);
        out.setSignalPort(std::move(signalPort));
        return makeResult<VideoReferenceConfig>(std::move(out));
}

PROMEKI_NAMESPACE_END
