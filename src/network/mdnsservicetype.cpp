/**
 * @file      mdnsservicetype.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnsservicetype.h>
#include <promeki/datastream.h>
#include <promeki/textstream.h>
#include <cctype>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Strips a single optional trailing '.' (FQDN root marker) on input.
        // Wire-form names always carry the empty root label as a trailing
        // dot in string form; we strip it on storage so equality and
        // round-trip comparisons against textually-typed types agree.
        String stripTrailingDot(const String &s) {
                if (s.size() > 0 && s[s.size() - 1] == '.') {
                        return s.substr(0, s.size() - 1);
                }
                return s;
        }

        // RFC 6335 §5.1 service-name rule (relaxed):
        //   * 1..MaxAppLabelLen bytes
        //   * letters, digits, hyphens
        //   * first and last byte must be alnum (no leading/trailing hyphen)
        //   * no consecutive hyphens
        bool isValidAppLabel(const String &label) {
                const int n = static_cast<int>(label.size());
                if (n < 1 || n > MdnsServiceType::MaxAppLabelLen) return false;
                bool prevHyphen = false;
                for (int i = 0; i < n; ++i) {
                        char c = label[i];
                        bool isAlnum = (c >= '0' && c <= '9')
                                       || (c >= 'a' && c <= 'z')
                                       || (c >= 'A' && c <= 'Z');
                        if (isAlnum) {
                                prevHyphen = false;
                                continue;
                        }
                        if (c == '-') {
                                // Leading hyphen, trailing hyphen, or double
                                // hyphen all rejected here.
                                if (i == 0 || i == n - 1 || prevHyphen) return false;
                                prevHyphen = true;
                                continue;
                        }
                        return false;
                }
                return true;
        }

} // anonymous namespace

MdnsServiceType::MdnsServiceType(const String &app, Protocol proto, const String &domain)
    : _app(app), _proto(proto), _domain(stripTrailingDot(domain)) {
        if (_domain.isEmpty()) _domain = String(DefaultDomain);
}

Result<MdnsServiceType> MdnsServiceType::fromString(const String &s) {
        // Canonical form: _<app>._<proto>[.<domain>][.]
        // Strip the optional trailing root-marker dot, then split on '.'.
        String       work   = stripTrailingDot(s);
        StringList   labels = work.split(".");
        const int    nl     = labels.size();
        if (nl < 2 || nl > 3) return makeError<MdnsServiceType>(Error::Invalid);

        // App label must start with '_' and be a valid service name.
        const String &appLabel = labels[0];
        if (appLabel.size() < 2 || appLabel[0] != '_') {
                return makeError<MdnsServiceType>(Error::Invalid);
        }
        String app = appLabel.substr(1);
        if (!isValidAppLabel(app)) return makeError<MdnsServiceType>(Error::Invalid);

        // Protocol label must be exactly "_tcp" or "_udp" (case-insensitive
        // on input; canonical lower-case on output).
        const String &protoLabel = labels[1];
        if (protoLabel.size() != 4 || protoLabel[0] != '_') {
                return makeError<MdnsServiceType>(Error::Invalid);
        }
        Protocol proto = protocolFromString(protoLabel.substr(1));
        if (proto == Protocol::Invalid) return makeError<MdnsServiceType>(Error::Invalid);

        String domain = (nl == 3) ? labels[2] : String(DefaultDomain);
        if (domain.isEmpty()) domain = String(DefaultDomain);

        return makeResult(MdnsServiceType(app, proto, domain));
}

String MdnsServiceType::toString() const {
        if (!isValid()) return String();
        String out;
        out += '_';
        out += _app;
        out += "._";
        out += protocolToString(_proto);
        out += '.';
        out += _domain;
        return out;
}

String MdnsServiceType::toFqdn() const {
        if (!isValid()) return String();
        String out = toString();
        out += '.';
        return out;
}

String MdnsServiceType::toSubtypeBrowseFqdn(const String &subtype) const {
        if (!isValid()) return String();
        if (subtype.isEmpty()) return toFqdn();
        // RFC 6763 §7.1: _<subtype>._sub._<app>._<proto>.<domain>.
        String out;
        out += '_';
        out += subtype;
        out += "._sub.";
        out += toFqdn();
        return out;
}

String MdnsServiceType::protocolToString(Protocol p) {
        switch (p) {
                case Protocol::Tcp: return String("tcp");
                case Protocol::Udp: return String("udp");
                case Protocol::Invalid: break;
        }
        return String();
}

MdnsServiceType::Protocol MdnsServiceType::protocolFromString(const String &s) {
        if (s.size() != 3) return Protocol::Invalid;
        char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
        char c2 = static_cast<char>(std::tolower(static_cast<unsigned char>(s[2])));
        if (c0 == 't' && c1 == 'c' && c2 == 'p') return Protocol::Tcp;
        if (c0 == 'u' && c1 == 'd' && c2 == 'p') return Protocol::Udp;
        return Protocol::Invalid;
}

bool MdnsServiceType::operator==(const MdnsServiceType &other) const {
        // App label and domain are compared case-insensitively (DNS
        // names are case-insensitive per RFC 1035 §3.1).  The protocol
        // is a typed enum so case has no meaning there.
        return _proto == other._proto
               && _app.compareIgnoreCase(other._app) == 0
               && _domain.compareIgnoreCase(other._domain) == 0;
}

bool MdnsServiceType::operator<(const MdnsServiceType &other) const {
        // Ordering follows the same rules as equality — case folded for
        // the textual labels so the order is stable across input casing.
        int c = _app.compareIgnoreCase(other._app);
        if (c != 0) return c < 0;
        if (_proto != other._proto) {
                return static_cast<uint8_t>(_proto) < static_cast<uint8_t>(other._proto);
        }
        return _domain.compareIgnoreCase(other._domain) < 0;
}

TextStream &operator<<(TextStream &stream, const MdnsServiceType &t) {
        stream << t.toString();
        return stream;
}

// ============================================================================
// DataStream wire format (v1: canonical String round-trip).
// ============================================================================

Error MdnsServiceType::writeToStream(DataStream &s) const {
        s << toString();
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<MdnsServiceType> MdnsServiceType::readFromStream<1>(DataStream &s) {
        String str;
        s >> str;
        if (s.status() != DataStream::Ok) return makeError<MdnsServiceType>(s.toError());
        if (str.isEmpty()) return makeResult(MdnsServiceType());
        return MdnsServiceType::fromString(str);
}

PROMEKI_NAMESPACE_END
