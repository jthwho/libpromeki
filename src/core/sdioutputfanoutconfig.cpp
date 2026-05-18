/**
 * @file      sdioutputfanoutconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdioutputfanoutconfig.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

SdiOutputFanoutConfig::SdiOutputFanoutConfig() : _d(SharedPtr<Impl>::create()) {}

SdiOutputFanoutConfig::SdiOutputFanoutConfig(SdiLinkStandard standard, GroupList groups)
    : _d(SharedPtr<Impl>::create()) {
        Impl *impl     = _d.modify();
        impl->standard = standard;
        impl->groups   = std::move(groups);
}

// ---------------------------------------------------------------------------
// Accessors / mutators
// ---------------------------------------------------------------------------

SdiLinkStandard SdiOutputFanoutConfig::standard() const { return _d->standard; }

const SdiOutputFanoutConfig::GroupList &SdiOutputFanoutConfig::groups() const { return _d->groups; }

int SdiOutputFanoutConfig::groupCount() const { return static_cast<int>(_d->groups.size()); }

void SdiOutputFanoutConfig::setStandard(SdiLinkStandard standard) { _d.modify()->standard = standard; }

void SdiOutputFanoutConfig::setGroups(GroupList groups) { _d.modify()->groups = std::move(groups); }

void SdiOutputFanoutConfig::appendGroup(PortList group) {
        _d.modify()->groups.pushToBack(std::move(group));
}

SdiSignalConfig SdiOutputFanoutConfig::primary() const {
        if (_d->groups.isEmpty()) return SdiSignalConfig();
        return SdiSignalConfig(_d->standard, _d->groups.at(0));
}

List<SdiSignalConfig> SdiOutputFanoutConfig::asSignalConfigs() const {
        List<SdiSignalConfig> out;
        out.reserve(_d->groups.size());
        for (size_t i = 0; i < _d->groups.size(); ++i) {
                out.pushToBack(SdiSignalConfig(_d->standard, _d->groups.at(i)));
        }
        return out;
}

bool SdiOutputFanoutConfig::isValid() const {
        if (_d->groups.isEmpty()) return false;
        if (_d->standard == SdiLinkStandard::Auto) return true;
        const int expected = cablesFor(_d->standard);
        for (size_t i = 0; i < _d->groups.size(); ++i) {
                if (static_cast<int>(_d->groups.at(i).size()) != expected) return false;
        }
        return true;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

bool SdiOutputFanoutConfig::operator==(const SdiOutputFanoutConfig &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        if (!(_d->standard == other._d->standard)) return false;
        if (_d->groups.size() != other._d->groups.size()) return false;
        for (size_t i = 0; i < _d->groups.size(); ++i) {
                const PortList &a = _d->groups.at(i);
                const PortList &b = other._d->groups.at(i);
                if (a.size() != b.size()) return false;
                for (size_t j = 0; j < a.size(); ++j) {
                        if (!(a.at(j) == b.at(j))) return false;
                }
        }
        return true;
}

// ---------------------------------------------------------------------------
// String form
// ---------------------------------------------------------------------------

String SdiOutputFanoutConfig::toString() const {
        String out = _d->standard.toString().toLower();
        if (_d->groups.isEmpty()) return out;
        out += String(":");
        for (size_t g = 0; g < _d->groups.size(); ++g) {
                if (g > 0) out += String(",");
                const PortList &grp = _d->groups.at(g);
                for (size_t i = 0; i < grp.size(); ++i) {
                        if (i > 0) out += String("+");
                        out += grp.at(i).toString();
                }
        }
        return out;
}

Result<SdiOutputFanoutConfig> SdiOutputFanoutConfig::fromString(const String &s) {
        String trimmed = s.trim();
        if (trimmed.isEmpty()) return makeError<SdiOutputFanoutConfig>(Error::InvalidArgument);

        // Split on the first ':'.  The left side is the standard prefix;
        // the right side carries one or more comma-separated groups.
        const size_t colon = trimmed.find(':');
        String       stdPart;
        String       groupsPart;
        if (colon == String::npos) {
                stdPart = trimmed;
        } else {
                stdPart    = trimmed.substr(0, colon);
                groupsPart = trimmed.substr(colon + 1);
        }

        const String stdLower = stdPart.toLower();
        if (stdLower.isEmpty()) return makeError<SdiOutputFanoutConfig>(Error::InvalidArgument);

        const Enum::ValueList values        = Enum::values(SdiLinkStandard::Type);
        int                   matchedValue = -1;
        for (size_t i = 0; i < values.size(); ++i) {
                if (values.at(i).first().toLower() == stdLower) {
                        matchedValue = values.at(i).second();
                        break;
                }
        }
        if (matchedValue < 0) return makeError<SdiOutputFanoutConfig>(Error::InvalidArgument);

        const SdiLinkStandard standard(matchedValue);
        GroupList             groups;
        if (!groupsPart.isEmpty()) {
                // Split on ',' to recover each group, then on '+' for
                // the per-group port list.  Empty tokens at either
                // level reject the whole string — the format is
                // unambiguous so trailing or doubled separators are
                // user errors, not flexibility.
                size_t       gStart = 0;
                const size_t gn     = groupsPart.length();
                for (size_t gi = 0; gi <= gn; ++gi) {
                        if (gi == gn || groupsPart.charAt(gi) == ',') {
                                String grpStr = groupsPart.substr(gStart, gi - gStart).trim();
                                if (grpStr.isEmpty()) {
                                        return makeError<SdiOutputFanoutConfig>(Error::InvalidArgument);
                                }
                                PortList ports;
                                size_t   pStart = 0;
                                const size_t pn = grpStr.length();
                                for (size_t pi = 0; pi <= pn; ++pi) {
                                        if (pi == pn || grpStr.charAt(pi) == '+') {
                                                String tok = grpStr.substr(pStart, pi - pStart).trim();
                                                if (tok.isEmpty()) {
                                                        return makeError<SdiOutputFanoutConfig>(
                                                                Error::InvalidArgument);
                                                }
                                                Result<VideoPortRef> r = VideoPortRef::fromString(tok);
                                                if (r.second().isError()) {
                                                        return makeError<SdiOutputFanoutConfig>(
                                                                r.second());
                                                }
                                                ports.pushToBack(r.first());
                                                pStart = pi + 1;
                                        }
                                }
                                groups.pushToBack(std::move(ports));
                                gStart = gi + 1;
                        }
                }
        }

        return makeResult<SdiOutputFanoutConfig>(
                SdiOutputFanoutConfig(standard, std::move(groups)));
}

// ---------------------------------------------------------------------------
// DataStream serialization (member-API path for PROMEKI_DATATYPE)
// ---------------------------------------------------------------------------

Error SdiOutputFanoutConfig::writeToStream(DataStream &s) const {
        s << _d->standard;
        s << static_cast<uint32_t>(_d->groups.size());
        for (size_t g = 0; g < _d->groups.size(); ++g) {
                const PortList &grp = _d->groups.at(g);
                s << static_cast<uint32_t>(grp.size());
                for (const auto &port : grp) s << port;
        }
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<SdiOutputFanoutConfig> SdiOutputFanoutConfig::readFromStream<1>(DataStream &s) {
        SdiLinkStandard standard;
        uint32_t        groupCount = 0;
        s >> standard;
        s >> groupCount;
        if (s.status() != DataStream::Ok) return makeError<SdiOutputFanoutConfig>(s.toError());
        GroupList groups;
        groups.reserve(groupCount);
        for (uint32_t g = 0; g < groupCount; ++g) {
                uint32_t portCount = 0;
                s >> portCount;
                if (s.status() != DataStream::Ok) return makeError<SdiOutputFanoutConfig>(s.toError());
                PortList ports;
                ports.reserve(portCount);
                for (uint32_t p = 0; p < portCount; ++p) {
                        VideoPortRef port;
                        s >> port;
                        if (s.status() != DataStream::Ok) {
                                return makeError<SdiOutputFanoutConfig>(s.toError());
                        }
                        ports.pushToBack(std::move(port));
                }
                groups.pushToBack(std::move(ports));
        }
        return makeResult<SdiOutputFanoutConfig>(
                SdiOutputFanoutConfig(standard, std::move(groups)));
}

PROMEKI_NAMESPACE_END
