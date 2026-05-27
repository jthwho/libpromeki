/**
 * @file      mediadesc.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediadesc.h>
#include <promeki/sdpsession.h>

PROMEKI_NAMESPACE_BEGIN

SdpSession MediaDesc::toSdp(uint8_t videoPayloadType) const {
        SdpSession session;
        uint8_t    pt = videoPayloadType;
        for (size_t i = 0; i < _imageList.size(); i++) {
                SdpMediaDescription md = _imageList[i].toSdp(pt);
                if (md.mediaType().isEmpty()) continue;
                // Stamp the session-level frame rate onto each video
                // m= section as @c a=framerate (RFC 4566 §6).  Without
                // this, an RFC 2435 / RFC 4175 SDP — neither of which
                // carries the rate in @c rtpmap or @c fmtp — leaves
                // the receiver guessing, which collapses any
                // per-frame audio aggregation that depends on
                // samples-per-frame math.
                if (_frameRate.isValid()) {
                        // Rational form (e.g. "60000/1001") roundtrips
                        // cleanly through @ref FrameRate::fromString;
                        // a decimal would lose precision on NTSC rates.
                        md.setAttribute("framerate", _frameRate.toString());
                }
                session.addMediaDescription(md);
                pt++;
        }
        for (size_t i = 0; i < _audioList.size(); i++) {
                SdpMediaDescription md = _audioList[i].toSdp(pt);
                if (md.mediaType().isEmpty()) continue;
                session.addMediaDescription(md);
                pt++;
        }
        return session;
}

MediaDesc MediaDesc::fromSdp(const SdpSession &session) {
        MediaDesc md;
        for (size_t i = 0; i < session.mediaDescriptions().size(); i++) {
                const SdpMediaDescription &sm = session.mediaDescriptions()[i];
                if (sm.mediaType() == "video") {
                        ImageDesc img = ImageDesc::fromSdp(sm);
                        if (img.isValid()) md.imageList().pushToBack(img);
                        // First valid framerate attribute on a video
                        // m= section sets the session-level rate.
                        if (!md.frameRate().isValid()) {
                                String fr = sm.attribute("framerate");
                                if (!fr.isEmpty()) {
                                        Result<FrameRate> parsed = FrameRate::fromString(fr);
                                        if (parsed.second().isOk() && parsed.first().isValid()) {
                                                md.setFrameRate(parsed.first());
                                        }
                                }
                        }
                } else if (sm.mediaType() == "audio") {
                        AudioDesc aud = AudioDesc::fromSdp(sm);
                        if (aud.isValid()) md.audioList().pushToBack(aud);
                }
                // "application" / data tracks are skipped —
                // MediaDesc has no data-stream list today.
        }
        return md;
}

PROMEKI_NAMESPACE_END
