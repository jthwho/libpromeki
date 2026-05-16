/**
 * @file      anccodec_hdrstatic.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * CTA-861.3 / CTA-861-G static HDR metadata codec for
 * @c AncTransport::HdmiInfoFrame.  Parses / builds the DRM
 * (Dynamic Range and Mastering) InfoFrame (Type @c 0x87, Version
 * @c 1) body into / from an @ref HdrStaticMetadata Variant.
 *
 * The InfoFrame body layout and the EOTF mapping live in
 * @ref HdrStaticMetadata::toBuffer / @ref HdrStaticMetadata::fromBuffer;
 * this codec is a thin transport adapter around them.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/hdmiinfoframe.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/result.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        Result<Variant> parseHdrStaticHdmiInfoFrame(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<HdmiInfoFrame> rf = HdmiInfoFrame::from(pkt);
                if (rf.second().isError()) return makeError<Variant>(rf.second());
                const HdmiInfoFrame &frame = rf.first();
                if (frame.type() != HdrStaticMetadata::InfoFrameType) {
                        return makeError<Variant>(Error::CorruptData);
                }
                Buffer body = frame.body();
                Result<HdrStaticMetadata> rm = HdrStaticMetadata::fromBuffer(body);
                if (rm.second().isError()) return makeError<Variant>(rm.second());
                return makeResult<Variant>(Variant(rm.first()));
        }

        Result<List<AncPacket>> buildHdrStaticHdmiInfoFrame(const Variant &v,
                                                             const AncTranslateConfig & /*cfg*/) {
                HdrStaticMetadata md = v.get<HdrStaticMetadata>();
                Buffer            body = md.toBuffer();
                HdmiInfoFrame     frame = HdmiInfoFrame::build(AncFormat(AncFormat::HdrStatic2086),
                                                                HdrStaticMetadata::InfoFrameVersion, body);
                List<AncPacket>   out;
                out.pushToBack(frame.packet());
                return makeResult<List<AncPacket>>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(HdrStatic_HdmiInfoFrame, HdrStatic2086, ::promeki::AncTransport::HdmiInfoFrame,
                             ::promeki::parseHdrStaticHdmiInfoFrame)
PROMEKI_REGISTER_ANC_BUILDER(HdrStatic_HdmiInfoFrame, HdrStatic2086, ::promeki::AncTransport::HdmiInfoFrame,
                              ::promeki::buildHdrStaticHdmiInfoFrame)
