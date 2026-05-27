/**
 * @file      audiodesc.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/optional.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiodesc.h>
#include <promeki/sdpsession.h>
#include <promeki/variantlookup.h>
#include <promeki/variantdatabase.h>

PROMEKI_NAMESPACE_BEGIN

AudioDesc AudioDesc::fromSdp(const SdpMediaDescription &md) {
        if (md.mediaType() != "audio") return AudioDesc();

        SdpMediaDescription::RtpMap rm = md.rtpMap();
        if (!rm.valid) return AudioDesc();

        // RFC 3551 / RFC 3190 RTP audio encodings.  The wire format
        // is always big-endian for L16 / L24, and unsigned 8-bit
        // for L8, regardless of the local CPU's native byte order.
        AudioFormat::ID fmtId = AudioFormat::Invalid;
        if (rm.encoding == "L16") {
                fmtId = AudioFormat::PCMI_S16BE;
        } else if (rm.encoding == "L24") {
                fmtId = AudioFormat::PCMI_S24BE;
        } else if (rm.encoding == "L8") {
                fmtId = AudioFormat::PCMI_U8;
        } else {
                return AudioDesc();
        }
        AudioDesc out(AudioFormat(fmtId), static_cast<float>(rm.clockRate), rm.channels);

        // ST 2110-30:2025 §6.2.2 / RFC 3190 channel-order fmtp.
        // When present and parseable, replace the constructor's
        // default channel map with the explicit ordering from the
        // SDP — and only when the parsed map's channel count
        // matches the rtpmap so a malformed sender can't corrupt
        // the per-channel role assignment.
        const auto fmtp = md.fmtpParameters();
        const auto orderIt = fmtp.find(String("channel-order"));
        if (orderIt != fmtp.end()) {
                auto parsed = AudioChannelMap::fromSt2110ChannelOrder(orderIt->second);
                if (isOk(parsed) &&
                    value(parsed).channels() == static_cast<size_t>(rm.channels)) {
                        out.setChannelMap(value(parsed));
                }
        }

        return out;
}

SdpMediaDescription AudioDesc::toSdp(uint8_t payloadType) const {
        if (!isValid()) return SdpMediaDescription();

        const char *encoding = nullptr;
        switch (_format.id()) {
                case AudioFormat::PCMI_S16LE:
                case AudioFormat::PCMI_S16BE: encoding = "L16"; break;
                case AudioFormat::PCMI_S24LE:
                case AudioFormat::PCMI_S24BE: encoding = "L24"; break;
                case AudioFormat::PCMI_U8:
                case AudioFormat::PCMI_S8: encoding = "L8"; break;
                default: return SdpMediaDescription();
        }

        SdpMediaDescription md;
        md.setMediaType("audio");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(payloadType);

        String rtpmap = String::number(payloadType) + String(" ") + String(encoding) + String("/") +
                        String::number(static_cast<int>(_sampleRate));
        if (_channels > 1) {
                rtpmap += String("/") + String::number(_channels);
        }
        md.setAttribute("rtpmap", rtpmap);

        // ST 2110-30:2025 §6.2.2 channel-order fmtp.  RFC 3190 §3
        // requires that the attribute MUST NOT be included for 1,
        // 2 or 3-channel streams (AIFF-C ordering is implicit at
        // those channel counts).  At ≥4 channels and with a
        // non-empty channel map, format the SMPTE2110 convention
        // value through @ref AudioChannelMap::toSt2110ChannelOrder.
        if (_channels >= 4 && channelMap().isValid()) {
                const String body = channelMap().toSt2110ChannelOrder();
                if (!body.isEmpty()) {
                        md.setAttribute("fmtp",
                                        String::number(payloadType) +
                                                String(" channel-order=SMPTE2110.") + body);
                }
        }
        return md;
}

AudioDesc AudioDesc::fromJson(const JsonObject &json, Error *err) {
        AudioFormat  fmt = value(AudioFormat::lookup(json.getString("Format")));
        float        sampleRate = json.getDouble("SampleRate");
        unsigned int chans = json.getUInt("Channels");
        if (!fmt.isValid() || sampleRate <= 0.0f || chans < 1) {
                if (err) *err = Error::Invalid;
                return AudioDesc();
        }
        AudioDesc out(fmt, sampleRate, chans);
        // Restore an explicit channel map when present; otherwise leave the
        // default mapping that the constructor installed.
        if (json.contains("ChannelMap")) {
                auto mapResult = AudioChannelMap::fromString(json.getString("ChannelMap"));
                if (isOk(mapResult) && value(mapResult).channels() == chans) {
                        out.setChannelMap(value(mapResult));
                }
        }
        if (json.contains("Metadata")) {
                Error    metaErr;
                Metadata meta = Metadata::fromJson(json.getObject("Metadata"), &metaErr);
                if (metaErr.isOk()) out.metadata() = std::move(meta);
        }
        if (err) *err = Error::Ok;
        return out;
}

// ============================================================================
// VariantLookup registration
//
// Self-contained introspection for @ref AudioDesc so a bare
// descriptor (no payload context) is queryable / printable via
// @c VariantLookup<AudioDesc>.  The @ref AudioPayload registration
// re-surfaces every one of these fields directly (rather than going
// through a @c Desc composition) so pipeline queries like
// @c Audio[0].SampleRate stay flat and don't need to know that
// AudioDesc has its own lookup.
// ============================================================================

PROMEKI_LOOKUP_REGISTER(AudioDesc)
        .scalar("Format", [](const AudioDesc &d) -> Optional<Variant> { return Variant(d.format()); })
        .scalar("SampleRate", [](const AudioDesc &d) -> Optional<Variant> { return Variant(d.sampleRate()); })
        .scalar("Channels",
                [](const AudioDesc &d) -> Optional<Variant> {
                        return Variant(static_cast<uint32_t>(d.channels()));
                })
        .scalar("BytesPerSample",
                [](const AudioDesc &d) -> Optional<Variant> {
                        return Variant(static_cast<uint64_t>(d.bytesPerSample()));
                })
        .scalar("IsValid", [](const AudioDesc &d) -> Optional<Variant> { return Variant(d.isValid()); })
        .scalar("IsCompressed", [](const AudioDesc &d) -> Optional<Variant> { return Variant(d.isCompressed()); })
        .scalar("IsNative", [](const AudioDesc &d) -> Optional<Variant> { return Variant(d.isNative()); })
        .scalar("ChannelMap",
                [](const AudioDesc &d) -> Optional<Variant> { return Variant(d.channelMap().toString()); })
        .database<"Metadata">(
                "Meta", [](const AudioDesc &d) -> const VariantDatabase<"Metadata"> * { return &d.metadata(); },
                [](AudioDesc &d) -> VariantDatabase<"Metadata"> * { return &d.metadata(); });

PROMEKI_NAMESPACE_END
