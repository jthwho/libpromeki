/**
 * @file      sdpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdpsession.h>
#include <promeki/file.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>
#include <promeki/stringlist.h>
#include <sstream>

PROMEKI_NAMESPACE_BEGIN

void SdpSession::setOrigin(const String &username, uint64_t sessionId,
                            uint64_t sessionVersion, const String &netType,
                            const String &addrType, const String &address) {
        _originUsername = username;
        _sessionId = sessionId;
        _sessionVersion = sessionVersion;
        _originNetType = netType;
        _originAddrType = addrType;
        _originAddress = address;
}

String SdpSession::toString() const {
        std::ostringstream out;

        // v= (protocol version, always 0)
        out << "v=0\r\n";

        // o= (origin)
        out << "o=" << _originUsername.str()
            << " " << _sessionId
            << " " << _sessionVersion
            << " " << _originNetType.str()
            << " " << _originAddrType.str()
            << " " << _originAddress.str() << "\r\n";

        // s= (session name)
        out << "s=" << (_sessionName.isEmpty() ? " " : _sessionName.str()) << "\r\n";

        // c= (connection, session-level)
        if(!_connectionAddress.isEmpty()) {
                out << "c=IN IP4 " << _connectionAddress.str() << "\r\n";
        }

        // t= (timing, always 0 0 for permanent sessions)
        out << "t=0 0\r\n";

        // Media descriptions
        for(const auto &md : _mediaDescriptions) {
                // m= line
                out << "m=" << md.mediaType().str()
                    << " " << md.port()
                    << " " << md.protocol().str();
                for(auto pt : md.payloadTypes()) {
                        out << " " << static_cast<int>(pt);
                }
                out << "\r\n";

                // c= (media-level connection, if different from session)
                if(!md.connectionAddress().isEmpty()) {
                        out << "c=IN IP4 " << md.connectionAddress().str() << "\r\n";
                }

                // a= (attributes, in insertion order)
                for(size_t ai = 0; ai < md.attributes().size(); ai++) {
                        const auto &attr = md.attributes()[ai];
                        if(attr.second().isEmpty()) {
                                out << "a=" << attr.first().str() << "\r\n";
                        } else {
                                out << "a=" << attr.first().str() << ":" << attr.second().str() << "\r\n";
                        }
                }
        }
        return String(out.str());
}

SdpMediaDescription::RtpMap SdpMediaDescription::rtpMap() const {
        RtpMap result;
        String value = attribute("rtpmap");
        if(value.isEmpty()) return result;

        // Expected form: "<pt> <encoding>/<rate>[/<channels>]".
        size_t sp = value.find(String(" "));
        if(sp == String::npos || sp == 0) return result;
        String ptStr = value.substr(0, sp);
        String tail  = value.substr(sp + 1).trim();
        size_t slash = tail.find(String("/"));
        if(slash == String::npos || slash == 0) return result;

        Error ptErr;
        int pt = ptStr.toInt(&ptErr);
        if(ptErr.isError() || pt < 0 || pt > 127) return result;

        result.encoding = tail.substr(0, slash).trim();
        String rest = tail.substr(slash + 1);
        size_t slash2 = rest.find(String("/"));
        Error rateErr;
        uint32_t rate = 0;
        if(slash2 == String::npos) {
                rate = static_cast<uint32_t>(rest.trim().toInt(&rateErr));
                result.channels = 1;
        } else {
                rate = static_cast<uint32_t>(
                        rest.substr(0, slash2).trim().toInt(&rateErr));
                int ch = rest.substr(slash2 + 1).trim().toInt();
                result.channels = (ch > 0) ? static_cast<unsigned int>(ch) : 1;
        }
        if(rateErr.isError() || rate == 0) return result;

        result.payloadType = static_cast<uint8_t>(pt);
        result.clockRate   = rate;
        result.valid       = true;
        return result;
}

SdpMediaDescription::FmtpParameters SdpMediaDescription::fmtpParameters() const {
        FmtpParameters params;
        String value = attribute("fmtp");
        if(value.isEmpty()) return params;

        // Form: "<pt> key1=value1;key2=value2;...".  Skip the
        // leading PT token, then split the remainder on ';'.
        size_t sp = value.find(String(" "));
        String tail = (sp == String::npos) ? value : value.substr(sp + 1);
        size_t pos = 0;
        while(pos < tail.size()) {
                size_t semi = tail.find(String(";"), pos);
                size_t end = (semi == String::npos) ? tail.size() : semi;
                String kv = tail.substr(pos, end - pos).trim();
                if(!kv.isEmpty()) {
                        size_t eq = kv.find(String("="));
                        if(eq == String::npos) {
                                params.insert(kv, String());
                        } else {
                                String key = kv.substr(0, eq).trim();
                                String val = kv.substr(eq + 1).trim();
                                if(!key.isEmpty()) params.insert(key, val);
                        }
                }
                if(semi == String::npos) break;
                pos = semi + 1;
        }
        return params;
}

Result<SdpSession> SdpSession::fromString(const String &sdp) {
        SdpSession session;
        SdpMediaDescription *currentMedia = nullptr;

        StringList lines = sdp.split("\n");
        for(const auto &rawLine : lines) {
                // Trim trailing \r
                String line = rawLine;
                while(!line.isEmpty() && (line.str().back() == '\r' || line.str().back() == '\n')) {
                        line = String(line.str().substr(0, line.size() - 1));
                }
                if(line.size() < 2) continue;
                if(line.str()[1] != '=') continue;

                char type = line.str()[0];
                String value(line.str().substr(2));

                switch(type) {
                        case 'v':
                                // Protocol version, must be 0
                                break;

                        case 'o': {
                                // o=<username> <sess-id> <sess-version> <nettype> <addrtype> <addr>
                                StringList parts = value.split(" ");
                                if(parts.size() >= 6) {
                                        uint64_t sid = 0;
                                        uint64_t sver = 0;
                                        try {
                                                sid = std::stoull(parts[1].str());
                                                sver = std::stoull(parts[2].str());
                                        } catch(...) { }
                                        session.setOrigin(parts[0], sid, sver,
                                                          parts[3], parts[4], parts[5]);
                                }
                                break;
                        }

                        case 's':
                                session.setSessionName(value);
                                break;

                        case 'c': {
                                // c=IN IP4 <address>[/<ttl>]
                                StringList parts = value.split(" ");
                                if(parts.size() >= 3) {
                                        // Strip TTL suffix if present
                                        String addr = parts[2];
                                        auto slashPos = addr.str().find('/');
                                        if(slashPos != std::string::npos) {
                                                addr = String(addr.str().substr(0, slashPos));
                                        }
                                        if(currentMedia) {
                                                currentMedia->setConnectionAddress(addr);
                                        } else {
                                                session.setConnectionAddress(addr);
                                        }
                                }
                                break;
                        }

                        case 't':
                                // Timing — ignored for now
                                break;

                        case 'm': {
                                // m=<media> <port> <proto> <fmt> ...
                                StringList parts = value.split(" ");
                                if(parts.size() < 3) break;

                                SdpMediaDescription md;
                                md.setMediaType(parts[0]);
                                md.setPort(static_cast<uint16_t>(parts[1].toInt()));
                                md.setProtocol(parts[2]);
                                for(size_t i = 3; i < parts.size(); i++) {
                                        int pt = parts[i].toInt();
                                        md.addPayloadType(static_cast<uint8_t>(pt));
                                }
                                session._mediaDescriptions.pushToBack(md);
                                // Point currentMedia to the just-added entry
                                currentMedia = &session._mediaDescriptions.back();
                                break;
                        }

                        case 'a': {
                                // a=<attribute> or a=<attribute>:<value>
                                if(!currentMedia) break;
                                auto colonPos = value.str().find(':');
                                if(colonPos == std::string::npos) {
                                        currentMedia->setAttribute(value, String());
                                } else {
                                        String attrName(value.str().substr(0, colonPos));
                                        String attrValue(value.str().substr(colonPos + 1));
                                        currentMedia->setAttribute(attrName, attrValue);
                                }
                                break;
                        }

                        default:
                                break;
                }
        }
        return { session, Error::Ok };
}

Result<SdpSession> SdpSession::fromFile(const String &path) {
        File f(path);
        Error err = f.open(IODevice::ReadOnly);
        if(err.isError()) {
                promekiErr("SdpSession::fromFile: open '%s' failed: %s",
                           path.cstr(), err.desc().cstr());
                return { SdpSession(), err };
        }

        // Slurp the file into a char buffer.  Note: unqualified
        // `List` inside a SdpSession member function resolves to
        // `SdpSession::List` (injected by PROMEKI_SHARED_FINAL), so
        // we spell the namespace explicitly when we want the plain
        // container template.
        Result<int64_t> sz = f.size();
        int64_t fileSize = sz.second().isOk() ? sz.first() : 0;
        if(fileSize <= 0) fileSize = 16384;
        ::promeki::List<char> scratch;
        scratch.resize(static_cast<size_t>(fileSize));
        int64_t nread = f.read(scratch.data(),
                               static_cast<int64_t>(scratch.size()));
        f.close();
        if(nread <= 0) {
                promekiErr("SdpSession::fromFile: '%s' is empty or unreadable",
                           path.cstr());
                return { SdpSession(), Error::IOError };
        }
        String text(scratch.data(), static_cast<size_t>(nread));
        return fromString(text);
}

Error SdpSession::toFile(const String &path) const {
        File f(path);
        Error err = f.open(IODevice::WriteOnly,
                           File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("SdpSession::toFile: open '%s' failed: %s",
                           path.cstr(), err.desc().cstr());
                return err;
        }
        String text = toString();
        int64_t n = f.write(text.cstr(),
                            static_cast<int64_t>(text.size()));
        f.close();
        if(n != static_cast<int64_t>(text.size())) {
                promekiErr("SdpSession::toFile: short write to '%s' "
                           "(%lld of %zu bytes)",
                           path.cstr(),
                           static_cast<long long>(n), text.size());
                return Error::IOError;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
