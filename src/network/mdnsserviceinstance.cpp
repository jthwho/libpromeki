/**
 * @file      mdnsserviceinstance.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnsserviceinstance.h>
#include <promeki/datastream.h>
#include <promeki/mdnsname.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Adds a trailing "." to a hostname / FQDN when one is not
        // already present.  Wire-form DNS names are always rooted; we
        // accept either form on input but emit the rooted form from
        // fqdn() so callers can concatenate it directly into wire-
        // building code without thinking about the marker.
        String withTrailingDot(const String &s) {
                if (s.isEmpty()) return s;
                if (s[s.size() - 1] == '.') return s;
                return s + String(".");
        }

} // anonymous namespace

String MdnsServiceInstance::fqdn() const {
        if (!isValid()) return String();
        // RFC 6763 §4.3: <Instance>.<Service>.<Domain>.
        // The instance label is published as one wire label — bytes
        // are arbitrary including @c '.' — but the text-form FQDN
        // joins labels with @c '.'.  We escape the instance label
        // so the encoder splits the FQDN back into the same number
        // of labels the publisher claimed.  The service-type half
        // is already restricted by MdnsServiceType validation so no
        // escaping is needed there.
        String out;
        out += mdnsEscapeLabel(_instanceName);
        out += '.';
        out += _type.toFqdn();
        return withTrailingDot(out);
}

bool MdnsServiceInstance::operator==(const MdnsServiceInstance &other) const {
        // Identity: type + instanceName.  Two announcements of the
        // same publisher seconds apart agree on these but diverge on
        // every snapshot field; tying equality to identity keeps
        // application-level dedupe (List::contains, Map keying) doing
        // the obvious thing.
        return _type == other._type
               && _instanceName.compareIgnoreCase(other._instanceName) == 0;
}

bool MdnsServiceInstance::hasSameContent(const MdnsServiceInstance &other) const {
        return _type == other._type
               && _instanceName.compareIgnoreCase(other._instanceName) == 0
               && _hostname.compareIgnoreCase(other._hostname) == 0
               && _port == other._port
               && _v4   == other._v4
               && _v6   == other._v6
               && _txt  == other._txt;
}

bool MdnsServiceInstance::hasSameSnapshot(const MdnsServiceInstance &other) const {
        return hasSameContent(other)
               && _interfaceIndex == other._interfaceIndex
               && _lastSeen       == other._lastSeen
               && _ttl            == other._ttl;
}

String MdnsServiceInstance::toString() const {
        // One-line summary aimed at log lines.  Address list is
        // truncated to the first entry of each family to keep the
        // line readable; full details live in the structured fields.
        String out;
        out += "MdnsServiceInstance{";
        out += _instanceName;
        out += " type=";
        out += _type.toString();
        if (!_hostname.isEmpty()) {
                out += " host=";
                out += _hostname;
        }
        if (_port != 0) {
                out += " port=";
                out += String::number(_port);
        }
        if (!_v4.isEmpty()) {
                out += " v4=";
                out += _v4[0].toString();
                if (_v4.size() > 1) out += "+";
        }
        if (!_v6.isEmpty()) {
                out += " v6=";
                out += _v6[0].toString();
                if (_v6.size() > 1) out += "+";
        }
        if (!_txt.isEmpty()) {
                out += " txt=";
                out += String::number(_txt.count());
        }
        if (_interfaceIndex != InvalidInterfaceIndex) {
                out += " ifindex=";
                out += String::number(_interfaceIndex);
        }
        out += '}';
        return out;
}

TextStream &operator<<(TextStream &stream, const MdnsServiceInstance &inst) {
        stream << inst.toString();
        return stream;
}

// ============================================================================
// DataStream wire form (v1: field-by-field tuple).
// ============================================================================

Error MdnsServiceInstance::writeToStream(DataStream &s) const {
        s << _instanceName;
        s << _type;
        s << _hostname;
        s << _port;
        s << _v4;
        s << _v6;
        s << _txt;
        s << static_cast<int32_t>(_interfaceIndex);
        s << _lastSeen;
        s << _ttl;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<MdnsServiceInstance> MdnsServiceInstance::readFromStream<1>(DataStream &s) {
        MdnsServiceInstance out;
        int32_t             ifindex = 0;
        s >> out._instanceName;
        s >> out._type;
        s >> out._hostname;
        s >> out._port;
        s >> out._v4;
        s >> out._v6;
        s >> out._txt;
        s >> ifindex;
        s >> out._lastSeen;
        s >> out._ttl;
        if (s.status() != DataStream::Ok) return makeError<MdnsServiceInstance>(s.toError());
        out._interfaceIndex = static_cast<int>(ifindex);
        return makeResult(std::move(out));
}

PROMEKI_NAMESPACE_END
