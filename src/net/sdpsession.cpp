/**
 * @file      net/sdpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/sdpsession.h>
#include <promeki/core/stringlist.h>
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

PROMEKI_NAMESPACE_END
