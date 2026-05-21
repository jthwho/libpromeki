/**
 * @file      anccodec_atc_hfrtc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 12-3:2016 ATC_HFRTC parser and builder.  The HFRTC carriage
 * is the only standards-compliant way to recover per-physical-frame
 * timecode at the ≥72 fps rates ST 12-3 covers (72/96/100/119.88/120),
 * where the ST 12-2 §9.2 / ST 12-1 §12 pair-rate carriage can no
 * longer fit a single field-mark bit (those rates have N>2 sub-frames
 * per super-frame).
 *
 * Wire framing differs from the ST 12-2 ATC trio in two places:
 *
 *   - SDID: 0x61 (vs 0x60 for ATC_LTC/VITC1/VITC2).
 *   - DBB1: 0x80..0x8F, where the low nibble is the bitstream number
 *     (ST 12-3 §10.1 — multiple bitstreams can coexist per video
 *     frame so independent timecode sources can ride alongside each
 *     other).
 *
 * Within the codeword (DBB2 + 64-bit time-address word) the same
 * BCD digit layout as ST 12-2 is reused, plus the rate-dependent
 * sub-frame identifier bits introduced in ST 12-3 §6.2 + Table 3.
 * libvtc's @c vtc_atc_pack_hfr / @c vtc_atc_unpack handle the
 * sub-frame bit positions; this file translates between the
 * @ref AncAtc / @ref Timecode value types and libvtc's VtcATC
 * struct, then plumbs the resulting 16 nibble+DBB pairs through the
 * @ref St291Packet framing layer.
 *
 * @par Rate disambiguation on parse
 *
 * DBB2 alone uniquely identifies every standard ST 12-3 format in
 * this version of the spec (see @c vtc_atc_decode_hfr_dbb2 — the
 * (super-frame count, N) tuple maps to a single VtcFormat, with the
 * single 119.88 DF vs 120 NDF ambiguity broken by the codeword's
 * drop-frame bit).  The @c AtcHfrtcParseFormatHint cfg key is a
 * last-resort fallback for the rare case where the captured DBB2 is
 * vendor-extension or has been corrupted.
 *
 * @par Frame-sync policy
 *
 * Mirrors @c syncPolicyAtc in @c anccodec_atc.cpp: Drop returns
 * empty, Play / Repeat[idx=0] passes the packet through verbatim,
 * Repeat[idx>0] re-encodes after advancing the timecode by @c
 * repeatIndex physical frames via @c ++tc (libvtc handles HFR
 * drop-frame rollover internally — at 119.88 DF the increment skips
 * 8 frames per dropped minute).
 */

#include <cstring>
#include <promeki/ancatc.h>
#include <promeki/ancformat.h>
#include <promeki/ancmeta.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslator.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>
#include <vtc/atc.h>
#include <vtc/format.h>
#include <vtc/timecode.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -- Wire-layout constants per ST 12-3:2016 ---------------------------

        // ST 12-3 inherits ST 12-2 §5: DC = 0x10 → 16 UDWs.
        constexpr size_t AtcHfrtcUdwCount = 16;

        // -- Bit helpers (shared shape with anccodec_atc.cpp) -----------------

        inline uint16_t makeUdw(uint8_t nibble, bool dbb) {
                uint16_t v = static_cast<uint16_t>((nibble & 0x0F) << 4);
                if (dbb) v = static_cast<uint16_t>(v | 0x08);
                return v;
        }

        inline uint8_t udwNibble(uint16_t udw) {
                return static_cast<uint8_t>((udw >> 4) & 0x0F);
        }

        // -- VtcTimecode bridging ---------------------------------------------

        // Build a VtcTimecode from a libpromeki Timecode + an explicit
        // libvtc format pointer.  Mirrors LtcEncoder's toVtcTimecode (same
        // userbits + colorFrame + BGF surface routing) and is duplicated
        // here rather than pulled into a header — the helper is six
        // lines of memberwise copy and pulling it into a public header
        // would force every consumer to drag libvtc into their include
        // graph.
        //
        // The colorFrame and BGF flag bits surfaced here are stripped by
        // libvtc's HFR pack path: ST 12-3 §6.2 + Table 3 reassigns bit
        // positions 11, 27, 43, 58, 59 (ST 12-1's color-frame + BGF0/1/2
        // + polarity slots) to the sub-frame identifier bits.  The flag
        // surfacing is kept anyway for API uniformity with the LTC /
        // ST 12-2 ATC paths; libvtc just writes the sub-frame bits on
        // top of the flag bits and the flags never make it to the wire.
        VtcTimecode toVtcTimecode(const Timecode &tc, const VtcFormat *fmt) {
                VtcTimecode vtc;
                vtc.format   = fmt;
                vtc.hour     = tc.hour();
                vtc.min      = tc.min();
                vtc.sec      = tc.sec();
                vtc.frame    = tc.frame();
                vtc.userbits = tc.userbits().toUint32();
                vtc.flags    = 0;
                if (tc.colorFrame()) vtc.flags |= VTC_TC_FLAG_LTC_COLOR_FRAME;
                const uint8_t mode = static_cast<uint8_t>(tc.userbits().mode());
                if (mode & 0x01u) vtc.flags |= VTC_TC_FLAG_LTC_BGF0;
                if (mode & 0x02u) vtc.flags |= VTC_TC_FLAG_LTC_BGF1;
                if (mode & 0x04u) vtc.flags |= VTC_TC_FLAG_LTC_BGF2;
                return vtc;
        }

        // Inverse: copy a VtcTimecode (as populated by vtc_atc_unpack)
        // onto a libpromeki Timecode.  Recovers BGF mode triple +
        // colorFrame from libvtc flags so a parse → build cycle is
        // information-preserving.
        Timecode fromVtcTimecode(const VtcTimecode &vtc) {
                Timecode::Mode mode = vtc.format ? Timecode::Mode(vtc.format)
                                                 : Timecode::Mode(0u, 0u);
                Timecode out(mode,
                             static_cast<Timecode::DigitType>(vtc.hour),
                             static_cast<Timecode::DigitType>(vtc.min),
                             static_cast<Timecode::DigitType>(vtc.sec),
                             static_cast<Timecode::DigitType>(vtc.frame));
                out.setColorFrame((vtc.flags & VTC_TC_FLAG_LTC_COLOR_FRAME) != 0u);
                uint8_t modeBits = 0;
                if (vtc.flags & VTC_TC_FLAG_LTC_BGF0) modeBits |= 0x01u;
                if (vtc.flags & VTC_TC_FLAG_LTC_BGF1) modeBits |= 0x02u;
                if (vtc.flags & VTC_TC_FLAG_LTC_BGF2) modeBits |= 0x04u;
                out.setUserbits(TimecodeUserbits::fromRawBits(
                        vtc.userbits, static_cast<TimecodeUserbits::Mode>(modeBits)));
                return out;
        }

        // -- VtcATC ↔ libpromeki UDW conversion -------------------------------
        //
        // We do the nibble-by-nibble walk in C++ rather than calling
        // libvtc's vtc_atc_to_udw / vtc_atc_from_udw to keep the parity
        // bits owned by St291Packet (which validates them on parse and
        // recomputes them on build).  The bit-level mapping is identical
        // to vtc_atc_to_udw: UDW i carries nibble (tc_data[i/2] >> (i%2)*4)
        // & 0x0F in bits 4-7, with DBB1 bit i in bit 3 for i<8 and DBB2
        // bit (i-8) in bit 3 for i>=8.

        List<uint16_t> vtcAtcToUdw(const VtcATC &vatc) {
                List<uint16_t> udw;
                udw.resize(AtcHfrtcUdwCount);
                for (size_t i = 0; i < AtcHfrtcUdwCount; ++i) {
                        uint8_t nibble = static_cast<uint8_t>(
                                (vatc.tc_data[i / 2] >> ((i % 2) * 4)) & 0x0F);
                        bool dbb = (i < 8)
                                           ? ((vatc.dbb1 >> i) & 1u) != 0u
                                           : ((vatc.dbb2 >> (i - 8)) & 1u) != 0u;
                        udw[i] = makeUdw(nibble, dbb);
                }
                return udw;
        }

        void udwToVtcAtc(const List<uint16_t> &udw, VtcATC &vatc) {
                std::memset(&vatc, 0, sizeof(vatc));
                vatc.sdid = VTC_ATC_SDID_HFR;
                uint8_t dbb1 = 0;
                uint8_t dbb2 = 0;
                for (size_t i = 0; i < AtcHfrtcUdwCount; ++i) {
                        uint8_t nibble = udwNibble(udw[i]);
                        if ((i % 2) == 0) {
                                vatc.tc_data[i / 2] = nibble;
                        } else {
                                vatc.tc_data[i / 2] |= static_cast<uint8_t>(nibble << 4);
                        }
                        bool dbb = (udw[i] & 0x08u) != 0u;
                        if (i < 8) {
                                if (dbb) dbb1 = static_cast<uint8_t>(dbb1 | (1u << i));
                        } else {
                                if (dbb) dbb2 = static_cast<uint8_t>(dbb2 | (1u << (i - 8)));
                        }
                }
                vatc.dbb1 = dbb1;
                vatc.dbb2 = dbb2;
        }

        // -- Format hint fallback ---------------------------------------------

        // Map an integer fps hint (72/96/100/120) back to a libvtc HFR
        // format pointer for the rare case where DBB2 is corrupt /
        // vendor-extension and vtc_atc_decode_hfr_dbb2 returns NULL.
        // Returns nullptr when the hint can't be resolved; the caller
        // then surfaces an InvalidFormat error.
        const VtcFormat *hfrtcFormatFromHint(uint32_t fpsHint, bool dropFrame) {
                if (fpsHint == 0) return nullptr;
                Timecode::Mode m(fpsHint, dropFrame ? uint32_t(Timecode::DropFrame) : 0u);
                return m.vtcFormat();
        }

        // -- Parsing ----------------------------------------------------------

        AncTranslator::ParseResult parseAtcHfrtcImpl(const AncPacket &pkt, const AncTranslateConfig &cfg) {
                Result<St291Packet> rp = St291Packet::from(pkt, cfg.checksumPolicy());
                if (rp.second().isError()) return makeError<Variant>(rp.second());
                const St291Packet &p   = rp.first();
                List<uint16_t>     udw = p.udw();
                if (udw.size() < AtcHfrtcUdwCount) {
                        return makeError<Variant>(Error::CorruptData);
                }

                VtcATC vatc;
                udwToVtcAtc(udw, vatc);

                // libvtc's vtc_atc_unpack routes to the HFR unpacker
                // when dbb1 is in the HFRTC range; that path reads
                // sub-frame identifier bits per ST 12-3 Table 3 and
                // recovers physical_frame = super_frame × N +
                // sub_frame_index automatically.  Format resolves from
                // DBB2 + the codeword's drop-frame bit.  When DBB2 is
                // corrupt and resolution fails, fall back to the cfg
                // format hint so a packet with a recoverable codeword
                // doesn't get dropped.
                VtcTimecode vtc;
                std::memset(&vtc, 0, sizeof(vtc));
                VtcError err = vtc_atc_unpack(&vatc, &vtc, nullptr);
                if (err != VTC_ERR_OK || vtc.format == nullptr) {
                        // DBB2 didn't resolve — try the hint.
                        uint32_t fpsHint = cfg.getAs<uint32_t>(
                                AncTranslateConfig::AtcHfrtcParseFormatHint, uint32_t(0));
                        // Drop-frame bit lives at codeword bit 10 (= tc_data byte 1 bit 2).
                        bool dropFrame = (vatc.tc_data[1] & 0x04u) != 0u;
                        const VtcFormat *fmt = hfrtcFormatFromHint(fpsHint, dropFrame);
                        if (fmt == nullptr) return makeError<Variant>(Error::FormatMismatch);
                        std::memset(&vtc, 0, sizeof(vtc));
                        // Retry the unpack with an explicit format pointer.
                        err = vtc_atc_unpack(&vatc, &vtc, fmt);
                        if (err != VTC_ERR_OK) return makeError<Variant>(Error::FormatMismatch);
                        vtc.format = fmt;
                }

                Timecode tc = fromVtcTimecode(vtc);
                AncAtc   atc(tc);
                atc.setPayloadType(vatc.dbb1);
                atc.setDbb2(vatc.dbb2);
                (void)pkt; // AncMeta is informational on this carriage; no rate hint needed.
                return makeResult<Variant>(Variant(atc));
        }

        // -- Building ---------------------------------------------------------

        // Pack an AncAtc whose Timecode is at an ST 12-3 HFR rate into
        // the 16-UDW ATC_HFRTC layout.  The caller-supplied DBB1
        // (from atc.payloadType()) is honored when it's in the
        // 0x80..0x8F range; otherwise the bitstream number is taken
        // from the cfg key.  DBB2 is taken from atc.dbb2() when non-
        // zero (caller override) and otherwise computed via libvtc
        // from the format's (sf-count, N) tuple.
        List<uint16_t> packAtcHfrtcUdw(const AncAtc &atc, uint8_t bitstreamNumber, Error *err) {
                const Timecode  &tc   = atc.timecode();
                const VtcFormat *fmt  = tc.vtcFormat();
                if (fmt == nullptr || !vtc_format_is_hfr(fmt)) {
                        if (err) *err = Error::FormatMismatch;
                        return List<uint16_t>();
                }

                VtcATC      vatc;
                VtcTimecode vtc      = toVtcTimecode(tc, fmt);
                VtcError    packErr  = vtc_atc_pack_hfr(&vtc, &vatc, bitstreamNumber);
                if (packErr != VTC_ERR_OK) {
                        if (err) *err = Error::FormatMismatch;
                        return List<uint16_t>();
                }

                // Honor caller overrides on DBB1 / DBB2 when set.  DBB1
                // override is range-restricted to the ATC_HFRTC slot;
                // anything outside 0x80..0x8F falls back to the
                // libvtc-computed value so a stale ATC_LTC payloadType
                // riding on an AncAtc doesn't sneak through into an
                // HFRTC build.
                if (atc.payloadType() >= 0x80u && atc.payloadType() <= 0x8Fu) {
                        vatc.dbb1 = atc.payloadType();
                }
                if (atc.dbb2() != 0u) {
                        vatc.dbb2 = atc.dbb2();
                }
                if (err) *err = Error::Ok;
                return vtcAtcToUdw(vatc);
        }

        AncAtc variantToAncAtc(const Variant &v) {
                if (v.type() == DataTypeAncAtc) return v.get<AncAtc>();
                return AncAtc(v.get<Timecode>());
        }

        AncTranslator::PacketsResult buildAtcHfrtcSt291(const Variant &v, const AncTranslateConfig &cfg) {
                AncAtc atc = variantToAncAtc(v);
                uint8_t bitstreamNumber = cfg.getAs<uint8_t>(
                        AncTranslateConfig::AtcHfrtcBitstreamNumber, uint8_t(0));
                if (bitstreamNumber > 0x0Fu) {
                        return makeError<AncPacket::List>(Error::OutOfRange);
                }

                Error          packErr = Error::Ok;
                List<uint16_t> udw     = packAtcHfrtcUdw(atc, bitstreamNumber, &packErr);
                if (packErr.isError()) {
                        return makeError<AncPacket::List>(packErr);
                }

                uint16_t line   = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                      St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit   = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                St291Packet      p = St291Packet::build(AncFormat(AncFormat::AtcHfrtc), udw, line,
                                                        St291Packet::UnspecifiedHOffset, fieldB, cBit);
                AncPacket::List  out;
                out.pushToBack(p.packet());
                return makeResult<AncPacket::List>(std::move(out));
        }

        // -- Sync policy ------------------------------------------------------
        //
        // Same shape as @c syncPolicyAtc in @c anccodec_atc.cpp.  Repeat[idx>0]
        // advances the parsed Timecode by repeatIndex physical frames via
        // @c ++tc — libvtc's increment handles HFR drop-frame rollover
        // (at 119.88 DF the per-minute drop is 8 frames per ST 12-3 §6.4).

        AncTranslator::PacketsResult syncPolicyAtcHfrtc(const AncPacket &pkt, FrameSyncDisposition d,
                                                        uint8_t repeatIndex, const AncTranslateConfig &cfg) {
                AncPacket::List out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        return makeResult<AncPacket::List>(std::move(out));
                }
                if (d.kind() == FrameSyncDisposition::Play || repeatIndex == 0) {
                        out.pushToBack(pkt);
                        return makeResult<AncPacket::List>(std::move(out));
                }

                AncTranslator::ParseResult parsed = parseAtcHfrtcImpl(pkt, cfg);
                if (parsed.second().isError()) return makeError<AncPacket::List>(parsed.second());
                AncAtc   atc = parsed.first().get<AncAtc>();
                Timecode tc  = atc.timecode();
                for (uint8_t i = 0; i < repeatIndex; ++i) ++tc;
                atc.setTimecode(tc);

                Result<St291Packet> rp = St291Packet::from(pkt);
                if (rp.second().isError()) return makeError<AncPacket::List>(rp.second());
                AncTranslateConfig overrideCfg = cfg;
                overrideCfg.set(AncTranslateConfig::St291BuildLine, rp.first().line());
                overrideCfg.set(AncTranslateConfig::St291FieldB, rp.first().fieldB());

                return buildAtcHfrtcSt291(Variant(atc), overrideCfg);
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_PARSER(AtcHfrtc_St291, AtcHfrtc, ::promeki::AncTransport::St291,
                             ::promeki::parseAtcHfrtcImpl)

PROMEKI_REGISTER_ANC_BUILDER(AtcHfrtc_St291, AtcHfrtc, ::promeki::AncTransport::St291,
                              ::promeki::buildAtcHfrtcSt291)

PROMEKI_REGISTER_ANC_SYNC_POLICY(AtcHfrtc, AtcHfrtc, ::promeki::syncPolicyAtcHfrtc)
