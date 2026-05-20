/**
 * @file      sdisignalconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdisignalconfig.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

SdiSignalConfig::SdiSignalConfig() : _d(SharedPtr<Impl>::create()) {}

SdiSignalConfig::SdiSignalConfig(SdiLinkStandard standard, PortList ports) : _d(SharedPtr<Impl>::create()) {
        Impl *impl = _d.modify();
        impl->standard = standard;
        impl->ports    = std::move(ports);
}

// ---------------------------------------------------------------------------
// Accessors / mutators
// ---------------------------------------------------------------------------

bool SdiSignalConfig::isValid() const { return !validate().isError(); }

SdiLinkStandard SdiSignalConfig::standard() const { return _d->standard; }

const SdiSignalConfig::PortList &SdiSignalConfig::ports() const { return _d->ports; }

int SdiSignalConfig::cableCount() const { return static_cast<int>(_d->ports.size()); }

void SdiSignalConfig::setStandard(SdiLinkStandard standard) { _d.modify()->standard = standard; }

void SdiSignalConfig::setPorts(PortList ports) { _d.modify()->ports = std::move(ports); }

void SdiSignalConfig::appendPort(VideoPortRef port) { _d.modify()->ports.pushToBack(std::move(port)); }

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

SdiSignalConfig SdiSignalConfig::singleLink(SdiLinkStandard standard, VideoPortRef port) {
        PortList ports;
        ports.pushToBack(std::move(port));
        return SdiSignalConfig(standard, std::move(ports));
}

SdiSignalConfig SdiSignalConfig::dualLink(SdiLinkStandard standard, VideoPortRef a, VideoPortRef b) {
        PortList ports;
        ports.pushToBack(std::move(a));
        ports.pushToBack(std::move(b));
        return SdiSignalConfig(standard, std::move(ports));
}

SdiSignalConfig SdiSignalConfig::quadLink(SdiLinkStandard standard, VideoPortRef a, VideoPortRef b,
                                          VideoPortRef c, VideoPortRef d) {
        PortList ports;
        ports.pushToBack(std::move(a));
        ports.pushToBack(std::move(b));
        ports.pushToBack(std::move(c));
        ports.pushToBack(std::move(d));
        return SdiSignalConfig(standard, std::move(ports));
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

Error SdiSignalConfig::validate() const {
        // Auto is treated as "unspecified" — any cable count, including
        // zero, is accepted so sources can describe "whatever arrives".
        if (_d->standard == SdiLinkStandard::Auto) return Error::Ok;
        const int expected = sdiCableCount(_d->standard);
        const int actual   = cableCount();
        if (expected != actual) return Error::InvalidArgument;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool SdiSignalConfig::operator==(const SdiSignalConfig &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        if (!(_d->standard == other._d->standard)) return false;
        if (_d->ports.size() != other._d->ports.size()) return false;
        for (size_t i = 0; i < _d->ports.size(); ++i) {
                if (!(_d->ports.at(i) == other._d->ports.at(i))) return false;
        }
        return true;
}

// ---------------------------------------------------------------------------
// String form
// ---------------------------------------------------------------------------

String SdiSignalConfig::toString() const {
        String out = _d->standard.toString().toLower();
        if (_d->ports.isEmpty()) return out;
        out += String(":");
        for (size_t i = 0; i < _d->ports.size(); ++i) {
                if (i > 0) out += String("+");
                out += _d->ports.at(i).toString();
        }
        return out;
}

Result<SdiSignalConfig> SdiSignalConfig::fromString(const String &s) {
        String trimmed = s.trim();
        if (trimmed.isEmpty()) return makeError<SdiSignalConfig>(Error::InvalidArgument);

        // Split on the first ':'.  The left side is the standard
        // portion (canonical lowercase / underscore-stripped); the
        // right side is the @c + separated port list (may be absent
        // when the standard alone is given, e.g. @c "auto").
        const size_t colon = trimmed.find(':');
        String       stdPart;
        String       portsPart;
        if (colon == String::npos) {
                stdPart = trimmed;
        } else {
                stdPart   = trimmed.substr(0, colon);
                portsPart = trimmed.substr(colon + 1);
        }

        const String stdLower = stdPart.toLower();
        if (stdLower.isEmpty()) return makeError<SdiSignalConfig>(Error::InvalidArgument);

        // Match the standard portion against the registered names
        // (lower-cased).  Names are unique under lower-casing, so
        // exact-match is sufficient and unambiguous.
        const Enum::ValueList values = Enum::values(SdiLinkStandard::Type);
        int                   matchedValue = -1;
        for (size_t i = 0; i < values.size(); ++i) {
                if (values.at(i).first().toLower() == stdLower) {
                        matchedValue = values.at(i).second();
                        break;
                }
        }
        if (matchedValue < 0) return makeError<SdiSignalConfig>(Error::InvalidArgument);

        const SdiLinkStandard standard(matchedValue);
        PortList              ports;
        if (!portsPart.isEmpty()) {
                // Split on '+' to recover the individual port specs.
                size_t start = 0;
                const size_t n = portsPart.length();
                for (size_t i = 0; i <= n; ++i) {
                        if (i == n || portsPart.charAt(i) == '+') {
                                String tok = portsPart.substr(start, i - start).trim();
                                if (tok.isEmpty()) return makeError<SdiSignalConfig>(Error::InvalidArgument);
                                Result<VideoPortRef> r = VideoPortRef::fromString(tok);
                                if (r.second().isError()) return makeError<SdiSignalConfig>(r.second());
                                ports.pushToBack(r.first());
                                start = i + 1;
                        }
                }
        }

        return makeResult<SdiSignalConfig>(SdiSignalConfig(standard, std::move(ports)));
}

// ---------------------------------------------------------------------------
// DataStream serialization (member-API path for PROMEKI_DATATYPE)
// ---------------------------------------------------------------------------

Error SdiSignalConfig::writeToStream(DataStream &s) const {
        s << _d->standard;
        s << static_cast<uint32_t>(_d->ports.size());
        for (const auto &port : _d->ports) s << port;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<SdiSignalConfig> SdiSignalConfig::readFromStream<1>(DataStream &s) {
        SdiLinkStandard standard;
        uint32_t        count = 0;
        s >> standard;
        s >> count;
        if (s.status() != DataStream::Ok) return makeError<SdiSignalConfig>(s.toError());
        PortList ports;
        ports.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                VideoPortRef port;
                s >> port;
                if (s.status() != DataStream::Ok) return makeError<SdiSignalConfig>(s.toError());
                ports.pushToBack(std::move(port));
        }
        return makeResult<SdiSignalConfig>(SdiSignalConfig(standard, std::move(ports)));
}

PROMEKI_NAMESPACE_END
