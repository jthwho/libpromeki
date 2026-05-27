/**
 * @file      rfc7273refclk.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rfc7273refclk.h>

PROMEKI_NAMESPACE_BEGIN

Rfc7273RefClk Rfc7273RefClk::ptp(const String &profile, const EUI64 &gmid, uint8_t domain) {
        Rfc7273RefClk r;
        r._kind = Kind::Ptp;
        r._profile = profile.isEmpty() ? String(DefaultPtpProfile) : profile;
        r._gmid = gmid;
        r._domain = domain;
        return r;
}

Rfc7273RefClk Rfc7273RefClk::ptpTraceable(const String &profile) {
        Rfc7273RefClk r;
        r._kind = Kind::Ptp;
        r._traceable = true;
        r._profile = profile.isEmpty() ? String(DefaultPtpProfile) : profile;
        return r;
}

Rfc7273RefClk Rfc7273RefClk::localMac(const MacAddress &mac) {
        Rfc7273RefClk r;
        r._kind = Kind::LocalMac;
        r._mac = mac;
        return r;
}

Rfc7273RefClk Rfc7273RefClk::local() {
        Rfc7273RefClk r;
        r._kind = Kind::LocalMac;
        return r;
}

String Rfc7273RefClk::toSdpValue() const {
        switch (_kind) {
                case Kind::None: return String();
                case Kind::Ptp: {
                        String out = String("ptp=") + _profile;
                        if (_traceable) {
                                // RFC 7273 §4.7: the literal token
                                // "traceable" replaces the gmid/domain
                                // pair when the slave reports a
                                // traceable grandmaster but the
                                // specific GMID is elided.
                                out += String(":traceable");
                        } else if (!_gmid.isNull()) {
                                // RFC 7273 §4.5 — domain is meaningful
                                // only when emitted alongside the gmid.
                                out += String(":") + _gmid.toString();
                                out += String(":") + String::number(static_cast<int>(_domain));
                        }
                        return out;
                }
                case Kind::LocalMac:
                        // RFC 7273 §4.4.  Format with hyphens to match
                        // the RFC examples (and the ST 2110 reference
                        // captures); receivers parse either separator.
                        if (_mac.isNull()) return String("local");
                        return String("localmac=") + _mac.toString('-');
        }
        return String();
}

Result<Rfc7273RefClk> Rfc7273RefClk::fromSdpValue(const String &value) {
        if (value.isEmpty()) return makeError<Rfc7273RefClk>(Error::Invalid);
        if (value.startsWith("ptp=")) {
                String     payload = value.mid(4);
                StringList parts = payload.split(':');
                if (parts.isEmpty() || parts[0].isEmpty()) {
                        return makeError<Rfc7273RefClk>(Error::Invalid);
                }
                Rfc7273RefClk r;
                r._kind = Kind::Ptp;
                r._profile = parts[0];
                if (parts.size() == 2 && parts[1] == String("traceable")) {
                        // RFC 7273 §4.7 traceable form.
                        r._traceable = true;
                        return makeResult(r);
                }
                if (parts.size() >= 2) {
                        auto [gm, gmErr] = EUI64::fromString(parts[1]);
                        if (gmErr.isError()) return makeError<Rfc7273RefClk>(Error::Invalid);
                        r._gmid = gm;
                }
                if (parts.size() >= 3) {
                        Error dErr;
                        int   d = parts[2].toInt(&dErr);
                        if (dErr.isError() || d < 0 || d > 255) {
                                return makeError<Rfc7273RefClk>(Error::Invalid);
                        }
                        r._domain = static_cast<uint8_t>(d);
                }
                return makeResult(r);
        }
        if (value.startsWith("localmac=")) {
                String macStr = value.mid(9);
                auto [mac, mErr] = MacAddress::fromString(macStr);
                if (mErr.isError()) return makeError<Rfc7273RefClk>(Error::Invalid);
                return makeResult(localMac(mac));
        }
        if (value == String("local")) return makeResult(local());
        return makeError<Rfc7273RefClk>(Error::Invalid);
}

bool Rfc7273RefClk::operator==(const Rfc7273RefClk &o) const {
        return _kind == o._kind && _traceable == o._traceable && _domain == o._domain &&
               _profile == o._profile && _gmid == o._gmid && _mac == o._mac;
}

PROMEKI_NAMESPACE_END
