/**
 * @file      videoencodersei.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/videoencodersei.h>

#if PROMEKI_ENABLE_PROAV

#include <algorithm>
#include <cstring>

#include <promeki/frame.h>
#include <promeki/videoencoder.h>
#include <promeki/anctranslator.h>
#include <promeki/ancpacket.h>
#include <promeki/ancformat.h>
#include <promeki/enums_anc.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

List<VideoEncoderSei::SeiPayload> VideoEncoderSei::captions(const Frame &source, int videoStreamIndex,
                                                            AncTranslator &translator) {
        List<SeiPayload> out;

        static const AncFormat::IDList kCaptionFormats{AncFormat::Cea708};
        AncPacket::List ancPackets = VideoEncoder::selectAncForSei(source, videoStreamIndex, kCaptionFormats);
        for (const AncPacket &pkt : ancPackets) {
                AncTranslator::PacketsResult r = translator.translate(pkt, AncTransport::HlsSei);
                if (error(r).isError()) {
                        promekiWarn("VideoEncoderSei::captions: AncTranslator::translate(Cea708, %s → HlsSei) "
                                    "failed: %s",
                                    pkt.transport().toString().cstr(), error(r).name().cstr());
                        continue;
                }
                for (const AncPacket &produced : value(r)) {
                        const Buffer &b = produced.data();
                        if (b.size() == 0) continue;
                        SeiPayload sp;
                        sp.type = TypeUserDataRegistered;
                        sp.bytes = b;
                        out.pushToBack(std::move(sp));
                }
        }
        return out;
}

VideoEncoderSei::SeiPayload VideoEncoderSei::masteringDisplay(const MasteringDisplay &md) {
        SeiPayload sp;
        sp.type = TypeMasteringDisplay;
        if (!md.isValid()) return sp;

        // mastering_display_colour_volume payload body (24 bytes):
        //   display_primaries_x/y[c] for c = green, blue, red   (6 × u16 BE)
        //   white_point_x/y                                     (2 × u16 BE)
        //   max_display_mastering_luminance                     (u32 BE)
        //   min_display_mastering_luminance                     (u32 BE)
        // Chromaticity is in 0.00002 units (× 50000); luminance in
        // 0.0001 cd/m² units (× 10000) — same scaling the NVENC backend
        // applies before handing the values to the driver.
        uint8_t bytes[24];
        size_t  i = 0;
        auto    putU16 = [&](uint32_t v) {
                bytes[i++] = static_cast<uint8_t>((v >> 8) & 0xFF);
                bytes[i++] = static_cast<uint8_t>(v & 0xFF);
        };
        auto putU32 = [&](uint32_t v) {
                bytes[i++] = static_cast<uint8_t>((v >> 24) & 0xFF);
                bytes[i++] = static_cast<uint8_t>((v >> 16) & 0xFF);
                bytes[i++] = static_cast<uint8_t>((v >> 8) & 0xFF);
                bytes[i++] = static_cast<uint8_t>(v & 0xFF);
        };
        auto chroma = [](double c) -> uint32_t { return static_cast<uint32_t>(c * 50000.0 + 0.5); };

        putU16(chroma(md.green().x()));
        putU16(chroma(md.green().y()));
        putU16(chroma(md.blue().x()));
        putU16(chroma(md.blue().y()));
        putU16(chroma(md.red().x()));
        putU16(chroma(md.red().y()));
        putU16(chroma(md.whitePoint().x()));
        putU16(chroma(md.whitePoint().y()));
        putU32(static_cast<uint32_t>(md.maxLuminance() * 10000.0 + 0.5));
        putU32(static_cast<uint32_t>(md.minLuminance() * 10000.0 + 0.5));

        Buffer buf(sizeof(bytes));
        std::memcpy(buf.data(), bytes, sizeof(bytes));
        buf.setSize(sizeof(bytes));
        sp.bytes = buf;
        return sp;
}

VideoEncoderSei::SeiPayload VideoEncoderSei::contentLightLevel(const ContentLightLevel &cll) {
        SeiPayload sp;
        sp.type = TypeContentLightLevel;
        if (!cll.isValid()) return sp;

        // content_light_level_info payload body (4 bytes):
        //   max_content_light_level       (u16 BE)
        //   max_pic_average_light_level   (u16 BE)
        const uint32_t maxCLL = std::min(cll.maxCLL(), uint32_t(65535));
        const uint32_t maxFALL = std::min(cll.maxFALL(), uint32_t(65535));
        uint8_t        bytes[4] = {
                static_cast<uint8_t>((maxCLL >> 8) & 0xFF),
                static_cast<uint8_t>(maxCLL & 0xFF),
                static_cast<uint8_t>((maxFALL >> 8) & 0xFF),
                static_cast<uint8_t>(maxFALL & 0xFF),
        };

        Buffer buf(sizeof(bytes));
        std::memcpy(buf.data(), bytes, sizeof(bytes));
        buf.setSize(sizeof(bytes));
        sp.bytes = buf;
        return sp;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
