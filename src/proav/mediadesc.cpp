/**
 * @file      mediadesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediadesc.h>
#include <promeki/sdpsession.h>

PROMEKI_NAMESPACE_BEGIN

SdpSession MediaDesc::toSdp(uint8_t videoPayloadType) const {
        SdpSession session;
        uint8_t pt = videoPayloadType;
        for(size_t i = 0; i < _imageList.size(); i++) {
                SdpMediaDescription md = _imageList[i].toSdp(pt);
                if(md.mediaType().isEmpty()) continue;
                session.addMediaDescription(md);
                pt++;
        }
        for(size_t i = 0; i < _audioList.size(); i++) {
                SdpMediaDescription md = _audioList[i].toSdp(pt);
                if(md.mediaType().isEmpty()) continue;
                session.addMediaDescription(md);
                pt++;
        }
        return session;
}

MediaDesc MediaDesc::fromSdp(const SdpSession &session) {
        MediaDesc md;
        for(size_t i = 0; i < session.mediaDescriptions().size(); i++) {
                const SdpMediaDescription &sm = session.mediaDescriptions()[i];
                if(sm.mediaType() == "video") {
                        ImageDesc img = ImageDesc::fromSdp(sm);
                        if(img.isValid()) md.imageList().pushToBack(img);
                } else if(sm.mediaType() == "audio") {
                        AudioDesc aud = AudioDesc::fromSdp(sm);
                        if(aud.isValid()) md.audioList().pushToBack(aud);
                }
                // "application" / data tracks are skipped —
                // MediaDesc has no data-stream list today.
        }
        return md;
}

PROMEKI_NAMESPACE_END
