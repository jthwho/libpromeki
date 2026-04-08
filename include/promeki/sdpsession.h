/**
 * @file      sdpsession.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/pair.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief SDP media description (m= section).
 * @ingroup network
 *
 * Represents a single media description within an SDP session.
 * Each media description has a media type, port, transport protocol,
 * payload types, and key-value attributes.
 *
 * @par Example
 * @code
 * SdpMediaDescription md;
 * md.setMediaType("video");
 * md.setPort(5004);
 * md.setProtocol("RTP/AVP");
 * md.addPayloadType(96);
 * md.setAttribute("rtpmap", "96 raw/90000");
 * @endcode
 */
class SdpMediaDescription {
        public:
                /** @brief Default constructor. */
                SdpMediaDescription() = default;

                /** @brief Returns the media type (e.g. "audio", "video"). */
                const String &mediaType() const { return _mediaType; }

                /** @brief Sets the media type. */
                void setMediaType(const String &type) { _mediaType = type; }

                /** @brief Returns the port number. */
                uint16_t port() const { return _port; }

                /** @brief Sets the port number. */
                void setPort(uint16_t port) { _port = port; }

                /** @brief Returns the transport protocol (e.g. "RTP/AVP"). */
                const String &protocol() const { return _protocol; }

                /** @brief Sets the transport protocol. */
                void setProtocol(const String &proto) { _protocol = proto; }

                /** @brief Returns the list of payload type numbers. */
                const List<uint8_t> &payloadTypes() const { return _payloadTypes; }

                /** @brief Adds a payload type number. */
                void addPayloadType(uint8_t pt) { _payloadTypes.pushToBack(pt); }

                /** @brief Attribute key-value pair. */
                using Attribute = Pair<String, String>;

                /** @brief Ordered list of attributes, preserving insertion order. */
                using AttributeList = List<Attribute>;

                /** @brief Returns the value of a named attribute, or empty string. */
                String attribute(const String &name) const {
                        for(size_t i = 0; i < _attributes.size(); i++) {
                                if(_attributes[i].first() == name) return _attributes[i].second();
                        }
                        return String();
                }

                /**
                 * @brief Sets a named attribute.
                 *
                 * If an attribute with the same name already exists, its value
                 * is updated in place. Otherwise a new entry is appended,
                 * preserving insertion order.
                 */
                void setAttribute(const String &name, const String &value) {
                        for(size_t i = 0; i < _attributes.size(); i++) {
                                if(_attributes[i].first() == name) {
                                        _attributes[i].setSecond(value);
                                        return;
                                }
                        }
                        _attributes.pushToBack(Attribute(name, value));
                }

                /** @brief Returns all attributes in insertion order. */
                const AttributeList &attributes() const { return _attributes; }

                /**
                 * @brief Returns an optional connection address for this media.
                 *
                 * If set, overrides the session-level connection address.
                 */
                const String &connectionAddress() const { return _connectionAddress; }

                /** @brief Sets the connection address for this media. */
                void setConnectionAddress(const String &addr) { _connectionAddress = addr; }

        private:
                String          _mediaType;
                uint16_t        _port = 0;
                String          _protocol;
                List<uint8_t>   _payloadTypes;
                AttributeList   _attributes;
                String          _connectionAddress;
};

/**
 * @brief SDP session description (RFC 4566).
 * @ingroup network
 *
 * SdpSession represents a complete SDP document. It can parse
 * SDP text into structured data and generate SDP text from
 * structured data. Used by ST 2110 and AES67 for stream
 * advertisement.
 *
 * @par Example
 * @code
 * SdpSession sdp;
 * sdp.setSessionName("vidgen test stream");
 * sdp.setConnectionAddress("239.0.0.1");
 *
 * SdpMediaDescription video;
 * video.setMediaType("video");
 * video.setPort(5004);
 * video.setProtocol("RTP/AVP");
 * video.addPayloadType(96);
 * video.setAttribute("rtpmap", "96 raw/90000");
 * sdp.addMediaDescription(video);
 *
 * String sdpText = sdp.toString();
 * @endcode
 */
class SdpSession {
        PROMEKI_SHARED_FINAL(SdpSession)
        public:
                /** @brief Shared pointer type. */
                using Ptr = SharedPtr<SdpSession>;

                /** @brief List of SdpSession values. */
                using List = promeki::List<SdpSession>;

                /** @brief List of shared SdpSession pointers. */
                using PtrList = promeki::List<SdpSession::Ptr>;

                /**
                 * @brief Parses an SDP text string into an SdpSession.
                 * @param sdp The SDP text to parse.
                 * @return A Result containing the parsed session and Error::Ok,
                 *         or Error::Invalid on parse failure.
                 */
                static Result<SdpSession> fromString(const String &sdp);

                /** @brief Default constructor. */
                SdpSession() = default;

                /** @brief Returns the session name (s= line). */
                const String &sessionName() const { return _sessionName; }

                /** @brief Sets the session name. */
                void setSessionName(const String &name) { _sessionName = name; }

                /** @brief Returns the originator username. */
                const String &originUsername() const { return _originUsername; }

                /** @brief Returns the session ID. */
                uint64_t sessionId() const { return _sessionId; }

                /** @brief Returns the session version. */
                uint64_t sessionVersion() const { return _sessionVersion; }

                /**
                 * @brief Sets the origin (o= line) fields.
                 * @param username The originator username.
                 * @param sessionId The session ID.
                 * @param sessionVersion The session version.
                 * @param netType Network type (default "IN").
                 * @param addrType Address type (default "IP4").
                 * @param address The originator's address.
                 */
                void setOrigin(const String &username, uint64_t sessionId,
                               uint64_t sessionVersion, const String &netType = "IN",
                               const String &addrType = "IP4",
                               const String &address = "0.0.0.0");

                /** @brief Returns the origin network type (e.g. "IN"). */
                const String &originNetType() const { return _originNetType; }

                /** @brief Returns the origin address type (e.g. "IP4"). */
                const String &originAddrType() const { return _originAddrType; }

                /** @brief Returns the origin address. */
                const String &originAddress() const { return _originAddress; }

                /** @brief Returns the session-level connection address (c= line). */
                const String &connectionAddress() const { return _connectionAddress; }

                /** @brief Sets the session-level connection address. */
                void setConnectionAddress(const String &address) { _connectionAddress = address; }

                /** @brief Returns the list of media descriptions. */
                const promeki::List<SdpMediaDescription> &mediaDescriptions() const { return _mediaDescriptions; }

                /** @brief Adds a media description. */
                void addMediaDescription(const SdpMediaDescription &md) { _mediaDescriptions.pushToBack(md); }

                /**
                 * @brief Generates the SDP text representation.
                 * @return The complete SDP document as a string.
                 */
                String toString() const;

        private:
                String          _sessionName;
                String          _originUsername = "-";
                uint64_t        _sessionId = 0;
                uint64_t        _sessionVersion = 0;
                String          _originNetType = "IN";
                String          _originAddrType = "IP4";
                String          _originAddress = "0.0.0.0";
                String          _connectionAddress;
                promeki::List<SdpMediaDescription> _mediaDescriptions;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::SdpSession);
