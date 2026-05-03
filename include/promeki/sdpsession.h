/**
 * @file      sdpsession.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/map.h>
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
                using AttributeList = ::promeki::List<Attribute>;

                /** @brief Returns the value of a named attribute, or empty string. */
                String attribute(const String &name) const {
                        for (size_t i = 0; i < _attributes.size(); i++) {
                                if (_attributes[i].first() == name) return _attributes[i].second();
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
                        for (size_t i = 0; i < _attributes.size(); i++) {
                                if (_attributes[i].first() == name) {
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

                /**
                 * @brief Structured form of an @c a=rtpmap attribute.
                 *
                 * @c rtpmap is written as @c "<pt> <encoding>/<rate>[/<ch>]"
                 * in the SDP attribute value.  RtpMap is the parsed
                 * representation so callers don't have to re-implement
                 * the split logic every time.
                 */
                struct RtpMap {
                                uint8_t      payloadType = 0; ///< @brief RTP payload type (0-127).
                                String       encoding; ///< @brief Encoding name (e.g. @c "JPEG", @c "jxsv", @c "L16").
                                uint32_t     clockRate = 0; ///< @brief RTP timestamp clock rate in Hz.
                                unsigned int channels = 1;  ///< @brief Audio channels (defaults to 1 for video).
                                bool         valid = false; ///< @brief Set by @ref rtpMap() when parsing succeeded.
                };

                /**
                 * @brief Parses the @c a=rtpmap attribute if present.
                 *
                 * Returns an @ref RtpMap with @c valid = false if the
                 * attribute is missing or malformed, and @c valid = true
                 * with populated fields on success.
                 */
                RtpMap rtpMap() const;

                /** @brief Parameter map extracted from an @c a=fmtp attribute. */
                using FmtpParameters = ::promeki::Map<String, String>;

                /**
                 * @brief Parses the @c a=fmtp attribute into a key=value map.
                 *
                 * @c fmtp lines are written as
                 * @c "<pt> key1=value1;key2=value2;...".  Returns an empty
                 * map when the attribute is absent or has no parameters.
                 * Values that don't contain an @c = are stored with an
                 * empty value so callers can still check for their presence.
                 */
                FmtpParameters fmtpParameters() const;

                /** @brief Equality comparison for Variant / container use. */
                bool operator==(const SdpMediaDescription &other) const {
                        return _mediaType == other._mediaType && _port == other._port && _protocol == other._protocol &&
                               _payloadTypes == other._payloadTypes && _attributes == other._attributes &&
                               _connectionAddress == other._connectionAddress;
                }

                /** @brief Inequality comparison. */
                bool operator!=(const SdpMediaDescription &other) const { return !(*this == other); }

        private:
                String        _mediaType;
                uint16_t      _port = 0;
                String        _protocol;
                List<uint8_t> _payloadTypes;
                AttributeList _attributes;
                String        _connectionAddress;
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
                using List = ::promeki::List<SdpSession>;

                /** @brief List of shared SdpSession pointers. */
                using PtrList = ::promeki::List<SdpSession::Ptr>;

                /** @brief List of @ref SdpMediaDescription values, used by @ref mediaDescriptions. */
                using MediaDescriptionList = ::promeki::List<SdpMediaDescription>;

                /**
                 * @brief Parses an SDP text string into an SdpSession.
                 * @param sdp The SDP text to parse.
                 * @return A Result containing the parsed session and Error::Ok,
                 *         or Error::Invalid on parse failure.
                 */
                static Result<SdpSession> fromString(const String &sdp);

                /**
                 * @brief Reads and parses an SDP file from disk.
                 *
                 * Opens @p path via @ref File, slurps the contents, and
                 * hands them to @ref fromString.  Accepts any resource
                 * path supported by @ref File, including the @c :/
                 * resource namespace.
                 *
                 * @param path The filesystem or resource path.
                 * @return Result with the parsed session on success, or
                 *         an error Result when the file cannot be read
                 *         or parsed.
                 */
                static Result<SdpSession> fromFile(const String &path);

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
                void setOrigin(const String &username, uint64_t sessionId, uint64_t sessionVersion,
                               const String &netType = "IN", const String &addrType = "IP4",
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
                const MediaDescriptionList &mediaDescriptions() const { return _mediaDescriptions; }

                /** @brief Adds a media description. */
                void addMediaDescription(const SdpMediaDescription &md) { _mediaDescriptions.pushToBack(md); }

                /**
                 * @brief Generates the SDP text representation.
                 * @return The complete SDP document as a string.
                 */
                String toString() const;

                /**
                 * @brief Writes the SDP text to the given file.
                 *
                 * Truncates any existing file.  The caller is responsible
                 * for directory creation — @ref File will not create
                 * missing parent directories.
                 *
                 * @param path The filesystem path to write.
                 * @return Error::Ok on success, or an error if the file
                 *         cannot be opened or the write is short.
                 */
                Error toFile(const String &path) const;

                /** @brief Equality comparison for Variant / container use. */
                bool operator==(const SdpSession &other) const {
                        return _sessionName == other._sessionName && _originUsername == other._originUsername &&
                               _sessionId == other._sessionId && _sessionVersion == other._sessionVersion &&
                               _originNetType == other._originNetType && _originAddrType == other._originAddrType &&
                               _originAddress == other._originAddress &&
                               _connectionAddress == other._connectionAddress &&
                               _mediaDescriptions == other._mediaDescriptions;
                }

                /** @brief Inequality comparison. */
                bool operator!=(const SdpSession &other) const { return !(*this == other); }

        private:
                String                             _sessionName;
                String                             _originUsername = "-";
                uint64_t                           _sessionId = 0;
                uint64_t                           _sessionVersion = 0;
                String                             _originNetType = "IN";
                String                             _originAddrType = "IP4";
                String                             _originAddress = "0.0.0.0";
                String                             _connectionAddress;
                MediaDescriptionList               _mediaDescriptions;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::SdpSession);
