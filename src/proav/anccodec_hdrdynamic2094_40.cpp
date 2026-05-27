/**
 * @file      anccodec_hdrdynamic2094_40.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 2094-40 dynamic HDR (HDR10+) codec for
 * @c AncTransport::HdmiInfoFrame.  Wraps the canonical ST 2094-40
 * metadata bitstream in a CEA-861 Vendor-Specific InfoFrame whose
 * body is:
 *
 * @code
 *  byte 0 : OUI[0]   (LSB of the 24-bit IEEE OUI)
 *  byte 1 : OUI[1]
 *  byte 2 : OUI[2]   (MSB of the OUI)
 *  byte 3.. : canonical ST 2094-40 bitstream (see HdrDynamic2094_40)
 * @endcode
 *
 * The default OUI is @c 0x90-84-8B (HDR10+ LLC,
 * @ref HdrDynamic2094_40::HdrPlusOui).  Callers can override the
 * stamped OUI by setting @c AncTranslateConfig::HdmiInfoFrameOui to
 * a non-zero value — the parser accepts any OUI (it leaves OUI
 * checking to the calling pipeline since vendor-specific InfoFrames
 * may share type @c 0x81 with non-HDR10+ flavours).
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/hdmiinfoframe.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/result.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr size_t kOuiSize = 3;

        AncTranslator::ParseResult parseHdrDynamicHdmiInfoFrame(const AncPacket &pkt, const AncTranslateConfig & /*cfg*/) {
                Result<HdmiInfoFrame> rf = HdmiInfoFrame::from(pkt);
                if (rf.second().isError()) return makeError<Variant>(rf.second());
                const HdmiInfoFrame &frame = rf.first();
                if (frame.type() != HdrDynamic2094_40::InfoFrameType) {
                        return makeError<Variant>(Error::CorruptData);
                }
                Buffer body = frame.body();
                if (body.size() < kOuiSize) {
                        return makeError<Variant>(Error::CorruptData);
                }
                const uint8_t *p = static_cast<const uint8_t *>(body.data());
                const size_t   payloadSize = body.size() - kOuiSize;
                Result<HdrDynamic2094_40> rm = HdrDynamic2094_40::fromBuffer(p + kOuiSize, payloadSize);
                if (rm.second().isError()) return makeError<Variant>(rm.second());
                return makeResult<Variant>(Variant(rm.first()));
        }

        AncTranslator::PacketsResult buildHdrDynamicHdmiInfoFrame(const Variant &v, const AncTranslateConfig &cfg) {
                HdrDynamic2094_40 md = v.get<HdrDynamic2094_40>();
                Buffer            stream = md.toBuffer();

                uint32_t oui = cfg.getAs<uint32_t>(AncTranslateConfig::HdmiInfoFrameOui, uint32_t(0));
                if (oui == 0) oui = HdrDynamic2094_40::HdrPlusOui;

                const size_t total = kOuiSize + stream.size();
                Buffer       body(total);
                body.setSize(total);
                uint8_t *bp = static_cast<uint8_t *>(body.data());
                // OUI in LSB-first wire order per CEA-861 §6.10.
                bp[0] = static_cast<uint8_t>(oui & 0xFFu);
                bp[1] = static_cast<uint8_t>((oui >> 8) & 0xFFu);
                bp[2] = static_cast<uint8_t>((oui >> 16) & 0xFFu);
                if (stream.size() > 0) {
                        Error err = body.copyFrom(stream.data(), stream.size(), kOuiSize);
                        if (err.isError()) return makeError<AncPacket::List>(err);
                }

                // HdmiInfoFrame::build() resolves the format via
                // AncFormat::fromHdmiInfoFrameType(0x81) which returns the
                // OUI-agnostic VendorInfoFrame catch-all.  Stamp the
                // specific HdrDynamic2094_40 format so AncTranslator::parse
                // dispatches to this codec on round-trip.
                HdmiInfoFrame frame = HdmiInfoFrame::build(AncFormat(AncFormat::HdrDynamic2094_40),
                                                            HdrDynamic2094_40::InfoFrameVersion, std::move(body));
                AncPacket     pkt = frame.packet();
                pkt.setFormat(AncFormat(AncFormat::HdrDynamic2094_40));
                AncPacket::List out;
                out.pushToBack(std::move(pkt));
                return makeResult<AncPacket::List>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(HdrDynamic2094_40_HdmiInfoFrame, HdrDynamic2094_40,
                             ::promeki::AncTransport::HdmiInfoFrame,
                             ::promeki::parseHdrDynamicHdmiInfoFrame)
PROMEKI_REGISTER_ANC_BUILDER(HdrDynamic2094_40_HdmiInfoFrame, HdrDynamic2094_40,
                              ::promeki::AncTransport::HdmiInfoFrame,
                              ::promeki::buildHdrDynamicHdmiInfoFrame)
