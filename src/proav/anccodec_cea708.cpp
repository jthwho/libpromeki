/**
 * @file      anccodec_cea708.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE 334-2 CDP parser and builder for @c AncTransport::St291.
 * Encodes / decodes @ref Cea708Cdp values to / from the ST 291 wire
 * form (DID 0x61 / SDID 0x01).  Each CDP byte becomes one user-data
 * word; @ref Cea708Cdp::toBuffer / @ref Cea708Cdp::fromBuffer own the
 * structural layout, the checksum, and the footer-sequence mirror.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/stringlist.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        AncTranslator::ParseResult parseCea708St291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());
                const St291Packet &p = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < 11) {
                        // Minimum CDP is 7-byte header + 4-byte footer.
                        return makeError<Variant>(Error::CorruptData);
                }
                // Each UDW carries one data byte in its low 8 bits.
                List<uint8_t> bytes;
                bytes.resize(udw.size());
                for (size_t i = 0; i < udw.size(); ++i) {
                        bytes[i] = static_cast<uint8_t>(udw[i] & 0xFF);
                }
                Result<Cea708Cdp> rc = Cea708Cdp::fromBuffer(bytes.data(), bytes.size());
                if (rc.second().isError()) return makeError<Variant>(rc.second());
                return makeResult<Variant>(Variant(rc.first()));
        }

        AncTranslator::PacketsResult buildCea708St291(const Variant &v, const AncTranslateConfig &cfg) {
                Cea708Cdp cdp = v.get<Cea708Cdp>();
                Buffer    wire = cdp.toBuffer();
                const size_t sz = wire.size();
                if (sz == 0) return makeError<AncPacket::List>(Error::CorruptData);

                // ST 291 DataCount is one byte — cap CDP size at 255.  The
                // standard allows much larger CDPs split across multiple
                // ANC packets; that lands as a follow-up when a real
                // source produces over-255-byte CDPs.  Until then, signal
                // the error explicitly rather than silently truncating.
                if (sz > 255) return makeError<AncPacket::List>(Error::OutOfRange);

                List<uint16_t> udw;
                udw.resize(sz);
                const uint8_t *src = static_cast<const uint8_t *>(wire.data());
                for (size_t i = 0; i < sz; ++i) udw[i] = src[i];

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet     p = St291Packet::build(AncFormat(AncFormat::Cea708), udw, line,
                                                        St291Packet::UnspecifiedHOffset, fieldB, cBit);
                AncPacket::List out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // Sync policy: CEA-708 caption data is per-frame and stateful — a
        // CDP carries DTVCC service blocks whose decoder commands
        // (DefineWindow, SetCurrentWindow, SetPenAttributes, …) must not
        // re-fire when the same picture is held over multiple output
        // slots.  But the receiver also expects a CDP every frame at the
        // session's cdp_frame_rate, so we can't just stop emitting CDPs
        // either.
        //
        // The policy: on Repeat[idx>0] emit a *framing-only* CDP that
        // preserves the CDP envelope (same cc_count, same header / footer
        // shape, sequence_counter advanced by repeatIndex per SMPTE 334-2
        // §5.1.5) but invalidates every cc_data triple
        // (cc_valid = false).  Receivers see a normal CDP cadence but
        // ingest no caption commands from the held frames.  On Drop the
        // CDP is lost — the caption decoder may glitch one frame; the
        // next surviving CDP re-establishes state.  On Play and
        // Repeat[idx=0] the original packet copies through verbatim.
        AncTranslator::PacketsResult syncPolicyCea708(const AncPacket &pkt, FrameSyncDisposition d,
                                                  uint8_t repeatIndex, const AncTranslateConfig &cfg) {
                AncPacket::List out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        return makeResult<AncPacket::List>(std::move(out));
                }
                if (d.kind() == FrameSyncDisposition::Play || repeatIndex == 0) {
                        out.pushToBack(pkt);
                        return makeResult<AncPacket::List>(std::move(out));
                }

                // Repeat[idx>0]: decode, advance sequence_counter, blank
                // cc_data triples, re-encode.
                AncTranslator::ParseResult parsed = parseCea708St291(pkt, cfg);
                if (parsed.second().isError()) return makeError<AncPacket::List>(parsed.second());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();

                // Wrap is fine — sequence_counter is u16 in the wire and
                // the spec expects it to roll over.
                cdp.sequenceCounter = static_cast<uint16_t>(cdp.sequenceCounter + repeatIndex);
                for (Cea708Cdp::CcData &cc : cdp.ccData) {
                        cc.valid = false;
                        cc.type  = 0;
                        cc.b1    = 0;
                        cc.b2    = 0;
                }

                // Preserve the source packet's line / fieldB (same rationale
                // as the ATC sync policy — wire framing should match the
                // source, not whatever defaults are in the held cfg).
                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<AncPacket::List>(rp.second());
                AncTranslateConfig overrideCfg = cfg;
                overrideCfg.set(AncTranslateConfig::St291BuildLine, rp.first().line());
                overrideCfg.set(AncTranslateConfig::St291FieldB, rp.first().fieldB());

                return buildCea708St291(Variant(cdp), overrideCfg);
        }

        // -- Detailer ---------------------------------------------------------

        // Maps the 4-bit cdp_frame_rate code (SMPTE 334-2 §5.1.4 Table) to
        // its frame rate in fps.  Reserved / unknown codes surface as raw
        // hex so a non-conformant encoder still shows up in diagnostics.
        String cdpFrameRateName(uint8_t code) {
                switch (code & 0x0F) {
                        case 1: return String("23.976 fps");
                        case 2: return String("24 fps");
                        case 3: return String("25 fps");
                        case 4: return String("29.97 fps");
                        case 5: return String("30 fps");
                        case 6: return String("50 fps");
                        case 7: return String("59.94 fps");
                        case 8: return String("60 fps");
                        default: return String::sprintf("Unknown (0x%X)", code & 0x0F);
                }
        }

        // Full human-readable analysis of a SMPTE 334-2 CDP packet.  Always
        // returns a populated AncDetails — a packet that cannot be decoded
        // still surfaces its framing fields plus an Error issue.  The
        // cc_data triples are summarised by carriage type (608 F1/F2, 708)
        // rather than dumped byte-for-byte; the per-byte caption decode is
        // the job of the CEA-608 / CEA-708 caption decoders downstream.
        AncDetails detailCea708St291(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                AncDetails d;

                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) {
                        d.addError(String("ST 291 framing decode failed: ") + rp.second().desc());
                        return d;
                }
                const St291Packet &p = rp.first();
                d.addField("DID", String::sprintf("0x%02X", p.did()));
                d.addField("SDID", String::sprintf("0x%02X", p.sdid()));
                d.addField("DataCount", String::number(p.dataCount()));
                d.addField("Line", String::number(p.line()));
                if (!p.checksumValid()) {
                        d.addWarning(String::sprintf(
                                "Stored checksum 0x%03X does not match computed 0x%03X",
                                p.checksum(), p.computedChecksum()));
                }

                AncTranslator::ParseResult parsed = parseCea708St291(pkt, cfg);
                if (parsed.second().isError()) {
                        d.addError(String("CDP decode failed: ") + parsed.second().desc());
                        return d;
                }
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();

                d.addField("CdpFrameRate", cdpFrameRateName(cdp.frameRateCode));
                d.addField("SequenceCounter", String::number(cdp.sequenceCounter));

                StringList flags;
                if (cdp.timeCodePresent)      flags.pushToBack(String("TimeCode"));
                if (cdp.ccDataPresent)        flags.pushToBack(String("CcData"));
                if (cdp.svcInfoPresent)       flags.pushToBack(String("SvcInfo"));
                if (cdp.captionServiceActive) flags.pushToBack(String("ServiceActive"));
                d.addField("Flags", flags.isEmpty() ? String("(none)") : flags.join(", "));

                if (cdp.timeCodePresent) {
                        d.addField("Timecode", cdp.timeCode.toString());
                }

                if (cdp.ccDataPresent) {
                        d.addField("CcDataTriples", String::number(cdp.ccData.size()));
                        size_t n608f1 = 0, n608f2 = 0, n708 = 0, npad = 0;
                        for (const Cea708Cdp::CcData &cc : cdp.ccData) {
                                if (!cc.valid) {
                                        ++npad;
                                        continue;
                                }
                                switch (cc.type & 0x03) {
                                        case 0:  ++n608f1; break;
                                        case 1:  ++n608f2; break;
                                        default: ++n708;   break;
                                }
                        }
                        StringList breakdown;
                        if (n608f1) breakdown.pushToBack(String::sprintf("608-F1=%zu", n608f1));
                        if (n608f2) breakdown.pushToBack(String::sprintf("608-F2=%zu", n608f2));
                        if (n708)   breakdown.pushToBack(String::sprintf("708=%zu", n708));
                        if (npad)   breakdown.pushToBack(String::sprintf("padding=%zu", npad));
                        if (!breakdown.isEmpty()) {
                                d.addField("CcDataBreakdown", breakdown.join(", "));
                        }
                }

                if (cdp.svcInfoPresent && !cdp.ccSvcInfo.isEmpty()) {
                        d.addField("ServiceCount", String::number(cdp.ccSvcInfo.size()));
                        for (size_t i = 0; i < cdp.ccSvcInfo.size(); ++i) {
                                const Cea708Cdp::CcSvcInfoEntry &e = cdp.ccSvcInfo[i];
                                String lang = String::sprintf("%c%c%c",
                                                              e.languageCode[0] ? e.languageCode[0] : '?',
                                                              e.languageCode[1] ? e.languageCode[1] : '?',
                                                              e.languageCode[2] ? e.languageCode[2] : '?');
                                String svc = e.digitalCc
                                                     ? String::sprintf("DTVCC svc %u",
                                                                       e.captionServiceNumber)
                                                     : String::sprintf("Line-21 %s",
                                                                       e.line21Field ? "F2" : "F1");
                                d.addField(String::sprintf("Service[%zu]", i),
                                           String::sprintf("%s, lang=%s%s%s", svc.cstr(), lang.cstr(),
                                                           e.easyReader ? ", easy-reader" : "",
                                                           e.wideAspect ? ", 16:9" : ""));
                        }
                }

                if (cdp.svcInfoMismatches > 0) {
                        d.addWarning(String::sprintf(
                                "%u svcinfo entr%s had a service-number mismatch between the entry "
                                "flag and svc_data_byte_4 (ATSC A/65 §6.9.2)",
                                cdp.svcInfoMismatches, cdp.svcInfoMismatches == 1 ? "y" : "ies"));
                }

                return d;
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                             ::promeki::parseCea708St291)
PROMEKI_REGISTER_ANC_BUILDER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                              ::promeki::buildCea708St291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(Cea708, Cea708, ::promeki::syncPolicyCea708)
PROMEKI_REGISTER_ANC_DETAILER(Cea708_St291, Cea708, ::promeki::AncTransport::St291,
                              ::promeki::detailCea708St291)
