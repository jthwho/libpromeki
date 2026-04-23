/**
 * @file      audiodesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/audiodesc.h>
#include <promeki/sdpsession.h>

PROMEKI_NAMESPACE_BEGIN

AudioDesc AudioDesc::fromSdp(const SdpMediaDescription &md) {
        if(md.mediaType() != "audio") return AudioDesc();

        SdpMediaDescription::RtpMap rm = md.rtpMap();
        if(!rm.valid) return AudioDesc();

        // RFC 3551 / RFC 3190 RTP audio encodings.  The wire format
        // is always big-endian for L16 / L24, and unsigned 8-bit
        // for L8, regardless of the local CPU's native byte order.
        AudioFormat::ID fmtId = AudioFormat::Invalid;
        if(rm.encoding == "L16") {
                fmtId = AudioFormat::PCMI_S16BE;
        } else if(rm.encoding == "L24") {
                fmtId = AudioFormat::PCMI_S24BE;
        } else if(rm.encoding == "L8") {
                fmtId = AudioFormat::PCMI_U8;
        } else {
                return AudioDesc();
        }
        return AudioDesc(AudioFormat(fmtId),
                         static_cast<float>(rm.clockRate),
                         rm.channels);
}

SdpMediaDescription AudioDesc::toSdp(uint8_t payloadType) const {
        if(!isValid()) return SdpMediaDescription();

        const char *encoding = nullptr;
        switch(_format.id()) {
                case AudioFormat::PCMI_S16LE:
                case AudioFormat::PCMI_S16BE:
                        encoding = "L16"; break;
                case AudioFormat::PCMI_S24LE:
                case AudioFormat::PCMI_S24BE:
                        encoding = "L24"; break;
                case AudioFormat::PCMI_U8:
                case AudioFormat::PCMI_S8:
                        encoding = "L8"; break;
                default:
                        return SdpMediaDescription();
        }

        SdpMediaDescription md;
        md.setMediaType("audio");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(payloadType);

        String rtpmap = String::number(payloadType) + String(" ") +
                        String(encoding) + String("/") +
                        String::number(static_cast<int>(_sampleRate));
        if(_channels > 1) {
                rtpmap += String("/") + String::number(_channels);
        }
        md.setAttribute("rtpmap", rtpmap);
        return md;
}

AudioDesc AudioDesc::fromJson(const JsonObject &json, Error *err) {
        AudioFormat fmt = value(AudioFormat::lookup(json.getString("Format")));
        float sampleRate = json.getDouble("SampleRate");
        unsigned int chans = json.getUInt("Channels");
        if(!fmt.isValid() || sampleRate <= 0.0f || chans < 1) {
                if(err) *err = Error::Invalid;
                return AudioDesc();
        }
        if(err) *err = Error::Ok;
        return AudioDesc(fmt, sampleRate, chans);
}

PROMEKI_NAMESPACE_END
