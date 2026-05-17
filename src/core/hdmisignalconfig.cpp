/**
 * @file      hdmisignalconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/hdmisignalconfig.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

HdmiSignalConfig::HdmiSignalConfig() : _d(SharedPtr<Impl>::create()) {}

HdmiSignalConfig::HdmiSignalConfig(VideoPortRef port, HdmiSpecVersion versionHint)
    : _d(SharedPtr<Impl>::create()) {
        Impl *impl = _d.modify();
        impl->port        = std::move(port);
        impl->versionHint = versionHint;
}

// ---------------------------------------------------------------------------
// Accessors / mutators
// ---------------------------------------------------------------------------

bool HdmiSignalConfig::isValid() const { return _d->port.isValid(); }

VideoPortRef HdmiSignalConfig::port() const { return _d->port; }

HdmiSpecVersion HdmiSignalConfig::versionHint() const { return _d->versionHint; }

void HdmiSignalConfig::setPort(VideoPortRef port) { _d.modify()->port = std::move(port); }

void HdmiSignalConfig::setVersionHint(HdmiSpecVersion versionHint) { _d.modify()->versionHint = versionHint; }

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool HdmiSignalConfig::operator==(const HdmiSignalConfig &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        return _d->port == other._d->port && _d->versionHint == other._d->versionHint;
}

// ---------------------------------------------------------------------------
// String form
// ---------------------------------------------------------------------------

String HdmiSignalConfig::toString() const {
        return _d->versionHint.toString().toLower() + String(":") + _d->port.toString();
}

Result<HdmiSignalConfig> HdmiSignalConfig::fromString(const String &s) {
        String trimmed = s.trim();
        if (trimmed.isEmpty()) return makeError<HdmiSignalConfig>(Error::InvalidArgument);

        const size_t colon = trimmed.find(':');
        if (colon == String::npos) return makeError<HdmiSignalConfig>(Error::InvalidArgument);

        const String verPart  = trimmed.substr(0, colon).trim();
        const String portPart = trimmed.substr(colon + 1).trim();
        if (portPart.isEmpty() || verPart.isEmpty()) {
                return makeError<HdmiSignalConfig>(Error::InvalidArgument);
        }

        Result<VideoPortRef> portR = VideoPortRef::fromString(portPart);
        if (portR.second().isError()) return makeError<HdmiSignalConfig>(portR.second());

        // Resolve the version segment against the registered HdmiSpecVersion
        // names, case-insensitively.
        const String          verLower = verPart.toLower();
        const Enum::ValueList values   = Enum::values(HdmiSpecVersion::Type);
        int                   matched  = -1;
        for (size_t i = 0; i < values.size(); ++i) {
                if (values.at(i).first().toLower() == verLower) {
                        matched = values.at(i).second();
                        break;
                }
        }
        if (matched < 0) return makeError<HdmiSignalConfig>(Error::InvalidArgument);

        return makeResult<HdmiSignalConfig>(HdmiSignalConfig(portR.first(), HdmiSpecVersion(matched)));
}

// ---------------------------------------------------------------------------
// DataStream serialization (member-API path for PROMEKI_DATATYPE)
// ---------------------------------------------------------------------------

Error HdmiSignalConfig::writeToStream(DataStream &s) const {
        s << _d->port;
        s << _d->versionHint;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<HdmiSignalConfig> HdmiSignalConfig::readFromStream<1>(DataStream &s) {
        VideoPortRef    port;
        HdmiSpecVersion versionHint;
        s >> port;
        s >> versionHint;
        if (s.status() != DataStream::Ok) return makeError<HdmiSignalConfig>(s.toError());
        return makeResult<HdmiSignalConfig>(HdmiSignalConfig(std::move(port), versionHint));
}

PROMEKI_NAMESPACE_END
