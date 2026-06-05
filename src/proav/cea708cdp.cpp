/**
 * @file      cea708cdp.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cea708cdp.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/string.h>
#include <vtc/format.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // SMPTE 334-2 §5 fixed bytes / shorthands.

        constexpr uint8_t kFrameRateLowNibble = 0x0F; // Bits 0–3 of the
                                                       // frame-rate byte
                                                       // are reserved and
                                                       // emitted as 1.

        // CDP minimum size = 7-byte header + 4-byte footer.
        constexpr size_t kMinCdpSize = 11;

        uint8_t flagsByte(const Cea708Cdp &c) {
                uint8_t f = 0x01; // Bit 0 reserved (per SMPTE 334-2 §5.1.5,
                                  // shall be set to 1).
                if (c.timeCodePresent) f |= 0x80;
                if (c.ccDataPresent) f |= 0x40;
                if (c.svcInfoPresent) f |= 0x20;
                if (c.svcInfoStart) f |= 0x10;
                if (c.svcInfoChange) f |= 0x08;
                if (c.svcInfoComplete) f |= 0x04;
                if (c.captionServiceActive) f |= 0x02;
                return f;
        }

        // Encode a digit value into the 8-bit BCD field used by the CDP
        // time-code section: high nibble = tens (masked to the legal range
        // each field allows — 0..2 for hours, 0..5 for min/sec, 0..3 for
        // frame), low nibble = units.  The mask only constrains the tens
        // nibble; the upper reserved / flag bits are stamped by the caller.
        uint8_t bcdHigh(uint8_t value, uint8_t tensMask) {
                uint8_t tens = static_cast<uint8_t>((value / 10) & tensMask);
                uint8_t units = static_cast<uint8_t>(value % 10);
                return static_cast<uint8_t>((tens << 4) | units);
        }

        // ST 334-2:2015 §5 Table 3: max cc_count value per frame rate.
        // CEA-708 bandwidth at 9600 bps yields different per-frame
        // service-block quotas at each rate; the cdp_frame_rate code
        // implies the cap.  Returns 0 on unknown codes (caller falls
        // back to the 5-bit field width of 0x1F).
        constexpr uint8_t kCcCountMaxByFrameRate[16] = {
                /* 0  reserved   */ 0,
                /* 1  23.976     */ 25,
                /* 2  24         */ 25,
                /* 3  25         */ 24,
                /* 4  29.97      */ 20,
                /* 5  30         */ 20,
                /* 6  50         */ 12,
                /* 7  59.94      */ 10,
                /* 8  60         */ 10,
                /* 9-15 reserved */ 0, 0, 0, 0, 0, 0, 0
        };

        // Resolve a ST 334-2 §5.3 Table 3 cdp_frame_rate code (plus the
        // wire drop_frame_flag bit) into a Timecode::Mode.  Returns an
        // invalid Mode for unrecognised codes; drop_frame is silently
        // ignored on rates that have no drop-frame sister format (24, 25,
        // 50, 60), matching the libvtc model where those families do not
        // carry a DF bit.
        Timecode::Mode modeForFrameRateCode(uint8_t code, bool dropFrame) {
                switch (code) {
                        case 1: return Timecode::Mode(&VTC_FORMAT_23_98);
                        case 2: return Timecode::Mode(&VTC_FORMAT_24);
                        case 3: return Timecode::Mode(&VTC_FORMAT_25);
                        case 4:
                                return Timecode::Mode(dropFrame ? &VTC_FORMAT_29_97_DF
                                                                : &VTC_FORMAT_29_97_NDF);
                        case 5:
                                return Timecode::Mode(dropFrame ? &VTC_FORMAT_30_DF
                                                                : &VTC_FORMAT_30_NDF);
                        case 6: return Timecode::Mode(&VTC_FORMAT_50);
                        case 7:
                                return Timecode::Mode(dropFrame ? &VTC_FORMAT_59_94_DF
                                                                : &VTC_FORMAT_59_94_NDF);
                        case 8: return Timecode::Mode(&VTC_FORMAT_60);
                        default: return Timecode::Mode();
                }
        }

} // namespace

// ============================================================================
// Wire encode
// ============================================================================

Buffer Cea708Cdp::toBuffer() const {
        // Pre-compute the payload section sizes.
        const size_t timecodeSize = timeCodePresent ? 5 : 0;
        const size_t ccDataSize =
                ccDataPresent ? (2 + 3 * ccData.size()) : 0;
        // SMPTE ST 334-2 §5.5: ccsvcinfo_section is 2 bytes of header
        // (section_id + flag byte) plus 7 bytes per service.
        const size_t svcInfoSize = svcInfoPresent ? (2 + 7 * ccSvcInfo.size()) : 0;
        const size_t extraSize = extraBytes.size();
        const size_t footerSize = 4;
        const size_t headerSize = 7;
        const size_t total = headerSize + timecodeSize + ccDataSize + svcInfoSize + extraSize
                             + footerSize;

        Buffer buf(total);
        buf.setSize(total);
        uint8_t *out = static_cast<uint8_t *>(buf.data());

        // Header.
        out[0] = static_cast<uint8_t>((Identifier >> 8) & 0xFF);
        out[1] = static_cast<uint8_t>(Identifier & 0xFF);
        // cdp_length is the total byte count including header and footer.
        out[2] = static_cast<uint8_t>(total & 0xFF);
        out[3] = static_cast<uint8_t>(((frameRateCode & 0x0F) << 4) | kFrameRateLowNibble);
        out[4] = flagsByte(*this);
        out[5] = static_cast<uint8_t>((sequenceCounter >> 8) & 0xFF);
        out[6] = static_cast<uint8_t>(sequenceCounter & 0xFF);

        size_t pos = headerSize;

        // Optional time-code section.
        if (timeCodePresent) {
                out[pos + 0] = TimeCodeSectionId;
                // SMPTE 334-2:2015 §5.3 Table 4 — H/M/S/F order, with
                // the spec's reserved '1' bits stamped in the upper bits
                // of the hours and minutes bytes:
                //   byte 1: '11' (2 bits) | tc_10hrs (2 bits) | tc_1hrs (4 bits)
                //   byte 2: '1'  (1 bit)  | tc_10min (3 bits) | tc_1min (4 bits)
                //   byte 3: tc_field_flag (1 bit) | tc_10sec (3 bits) | tc_1sec (4 bits)
                //   byte 4: drop_frame_flag (1 bit) | '0' (1 bit) | tc_10fr (2 bits) | tc_1fr (4 bits)
                out[pos + 1] = static_cast<uint8_t>(
                        0xC0 | bcdHigh(static_cast<uint8_t>(timeCode.hour()), 0x03));
                out[pos + 2] = static_cast<uint8_t>(
                        0x80 | bcdHigh(static_cast<uint8_t>(timeCode.min()), 0x07));
                out[pos + 3] = static_cast<uint8_t>(
                        (tcFieldFlag ? 0x80 : 0x00) |
                        bcdHigh(static_cast<uint8_t>(timeCode.sec()), 0x07));
                out[pos + 4] = static_cast<uint8_t>(
                        (timeCode.isDropFrame() ? 0x80 : 0x00) |
                        bcdHigh(static_cast<uint8_t>(timeCode.frame()), 0x03));
                pos += timecodeSize;
        }

        // Optional cc_data section.
        if (ccDataPresent) {
                out[pos + 0] = CcDataSectionId;
                // ST 334-2:2015 §5 Table 3 caps cc_count per frame
                // rate (e.g. 20 at 29.97/30, 10 at 59.94/60).  The
                // 5-bit cc_count field can encode up to 31 triples,
                // but a sender exceeding the per-rate cap produces a
                // non-conformant CDP.  Warn so producer bugs surface.
                const uint8_t cap = (frameRateCode < 16)
                                            ? kCcCountMaxByFrameRate[frameRateCode]
                                            : uint8_t(0);
                if (cap != 0 && ccData.size() > cap) {
                        promekiWarn("Cea708Cdp: cc_count=%zu exceeds ST 334-2 Table 3 cap of %u for "
                                    "frameRateCode=%u — non-conformant CDP on the wire",
                                    ccData.size(), static_cast<unsigned>(cap),
                                    static_cast<unsigned>(frameRateCode));
                }
                // High 3 bits are "marker bits" (all 1s per SMPTE 334-2);
                // low 5 bits are the cc_count.
                uint8_t cnt = static_cast<uint8_t>(ccData.size() & 0x1F);
                out[pos + 1] = static_cast<uint8_t>(0xE0 | cnt);
                size_t triplePos = pos + 2;
                for (size_t i = 0; i < ccData.size(); ++i) {
                        const CcData &c = ccData[i];
                        // Per CEA-708 §4.4.1: high 5 bits 11111, then
                        // cc_valid (1 bit), cc_type (2 bits).
                        uint8_t marker = 0xF8;
                        uint8_t valid = c.valid ? 0x04 : 0x00;
                        uint8_t type = static_cast<uint8_t>(c.type & 0x03);
                        out[triplePos + 0] = static_cast<uint8_t>(marker | valid | type);
                        out[triplePos + 1] = c.b1;
                        out[triplePos + 2] = c.b2;
                        triplePos += 3;
                }
                pos += ccDataSize;
        }

        // Optional ccsvcinfo_section (SMPTE 334-2 §5.5 Table 6).
        if (svcInfoPresent) {
                // Section header: id byte + flag byte carrying the
                // svc_info_start / change / complete bits + 4-bit
                // svc_count.  Reserved bit 7 of the flag byte is '1'.
                const uint8_t svcCountWire = static_cast<uint8_t>(ccSvcInfo.size() & 0x0F);
                out[pos + 0] = CcSvcInfoSectionId;
                out[pos + 1] = static_cast<uint8_t>(0x80                                    // reserved '1'
                                                    | (svcInfoStart ? 0x40 : 0x00)
                                                    | (svcInfoChange ? 0x20 : 0x00)
                                                    | (svcInfoComplete ? 0x10 : 0x00)
                                                    | svcCountWire);
                size_t svcPos = pos + 2;
                for (size_t i = 0; i < ccSvcInfo.size(); ++i) {
                        const CcSvcInfoEntry &e = ccSvcInfo[i];
                        // Service-entry flag byte:
                        //   bit 7 reserved '1'
                        //   bit 6 csn_size (1 = 5-bit csn, 0 = 6-bit csn)
                        //   if csn_size == 1:
                        //     bit 5 reserved '1'
                        //     bits 4..0 caption_service_number (5 bits)
                        //   else:
                        //     bits 5..0 caption_service_number (6 bits)
                        uint8_t flag = static_cast<uint8_t>(0x80);
                        if (e.csnSize5Bit) {
                                flag = static_cast<uint8_t>(flag | 0x40 | 0x20
                                                            | (e.captionServiceNumber & 0x1F));
                        } else {
                                flag = static_cast<uint8_t>(flag | (e.captionServiceNumber & 0x3F));
                        }
                        out[svcPos + 0] = flag;
                        // ATSC A/65 §6.9.2 caption_service_descriptor loop entry:
                        //   bytes 1..3: ISO-639.2 language code
                        //   byte 4: digital_cc (bit 7) | reserved (bit 6) |
                        //           (line21_field at bit 0 OR caption_service_number
                        //            in bits 5..0)
                        //   byte 5: easy_reader (bit 7) | wide_aspect_ratio
                        //           (bit 6) | reserved (bits 5..0)
                        //   byte 6: reserved
                        out[svcPos + 1] = e.languageCode[0];
                        out[svcPos + 2] = e.languageCode[1];
                        out[svcPos + 3] = e.languageCode[2];
                        if (e.digitalCc) {
                                out[svcPos + 4] = static_cast<uint8_t>(
                                        0x80 | 0x40 | (e.captionServiceNumber & 0x3F));
                        } else {
                                // CEA-608 line-21: reserved bits + line21_field.
                                out[svcPos + 4] = static_cast<uint8_t>(
                                        0x40 | (e.line21Field ? 0x01 : 0x00));
                        }
                        out[svcPos + 5] = static_cast<uint8_t>(
                                (e.easyReader ? 0x80 : 0x00) | (e.wideAspect ? 0x40 : 0x00)
                                | 0x3F /* reserved bits 5..0 = '111111' per ATSC A/65 */);
                        out[svcPos + 6] = 0xFF; // reserved byte
                        svcPos += 7;
                }
                pos += svcInfoSize;
        }

        // Extra opaque sections, copied verbatim.
        if (extraSize > 0) {
                const uint8_t *src = static_cast<const uint8_t *>(extraBytes.data());
                for (size_t i = 0; i < extraSize; ++i) out[pos + i] = src[i];
                pos += extraSize;
        }

        // Footer.
        out[pos + 0] = FooterId;
        out[pos + 1] = static_cast<uint8_t>((sequenceCounter >> 8) & 0xFF);
        out[pos + 2] = static_cast<uint8_t>(sequenceCounter & 0xFF);

        // packet_checksum: the byte that makes the mod-256 sum of every
        // CDP byte zero.  Compute the partial sum first, then derive the
        // final byte and stamp it.
        uint32_t sum = 0;
        for (size_t i = 0; i < total - 1; ++i) sum += out[i];
        out[total - 1] = static_cast<uint8_t>((0x100 - (sum & 0xFF)) & 0xFF);

        return buf;
}

// ============================================================================
// Wire decode
// ============================================================================

Result<Cea708Cdp> Cea708Cdp::fromBuffer(const void *data, size_t size) {
        if (data == nullptr || size < kMinCdpSize) {
                return makeError<Cea708Cdp>(Error::CorruptData);
        }
        const uint8_t *in = static_cast<const uint8_t *>(data);

        // Validate cdp_identifier.
        uint16_t ident = static_cast<uint16_t>((in[0] << 8) | in[1]);
        if (ident != Identifier) {
                return makeError<Cea708Cdp>(Error::CorruptData);
        }
        // cdp_length must equal the actual byte count we were handed.
        size_t declaredLen = in[2];
        if (declaredLen != size) {
                return makeError<Cea708Cdp>(Error::CorruptData);
        }

        // packet_checksum: every byte should sum to zero mod 256.
        uint32_t sum = 0;
        for (size_t i = 0; i < size; ++i) sum += in[i];
        if ((sum & 0xFF) != 0) {
                return makeError<Cea708Cdp>(Error::CorruptData);
        }

        Cea708Cdp cdp;
        cdp.frameRateCode = static_cast<uint8_t>((in[3] >> 4) & 0x0F);
        uint8_t flags = in[4];
        cdp.timeCodePresent = (flags & 0x80) != 0;
        cdp.ccDataPresent = (flags & 0x40) != 0;
        cdp.svcInfoPresent = (flags & 0x20) != 0;
        cdp.svcInfoStart = (flags & 0x10) != 0;
        cdp.svcInfoChange = (flags & 0x08) != 0;
        cdp.svcInfoComplete = (flags & 0x04) != 0;
        cdp.captionServiceActive = (flags & 0x02) != 0;
        cdp.sequenceCounter = static_cast<uint16_t>((in[5] << 8) | in[6]);

        size_t pos = 7;
        const size_t footerStart = size - 4;

        if (cdp.timeCodePresent) {
                if (pos + 5 > footerStart || in[pos] != TimeCodeSectionId) {
                        return makeError<Cea708Cdp>(Error::CorruptData);
                }
                // Per ST 334-2:2015 §5.3 Table 4 the time-code section
                // byte order is H/M/S/F.  The flag bits live in the
                // upper bits of the seconds (tc_field_flag) and frames
                // (drop_frame_flag) bytes; the hours / minutes upper
                // bits are reserved and not consumed here.
                uint8_t b1 = in[pos + 1]; // hours + reserved '11'
                uint8_t b2 = in[pos + 2]; // minutes + reserved '1'
                uint8_t b3 = in[pos + 3]; // seconds + tc_field_flag
                uint8_t b4 = in[pos + 4]; // frames + drop_frame_flag
                uint8_t hrs = static_cast<uint8_t>(((b1 >> 4) & 0x03) * 10 + (b1 & 0x0F));
                uint8_t min = static_cast<uint8_t>(((b2 >> 4) & 0x07) * 10 + (b2 & 0x0F));
                uint8_t sec = static_cast<uint8_t>(((b3 >> 4) & 0x07) * 10 + (b3 & 0x0F));
                uint8_t frm = static_cast<uint8_t>(((b4 >> 4) & 0x03) * 10 + (b4 & 0x0F));
                cdp.tcFieldFlag = (b3 & 0x80) != 0;
                const bool dropFrame = (b4 & 0x80) != 0;
                Timecode::Mode mode = modeForFrameRateCode(cdp.frameRateCode, dropFrame);
                cdp.timeCode = Timecode(mode, hrs, min, sec, frm);
                pos += 5;
        }

        if (cdp.ccDataPresent) {
                if (pos + 2 > footerStart || in[pos] != CcDataSectionId) {
                        return makeError<Cea708Cdp>(Error::CorruptData);
                }
                uint8_t cnt = static_cast<uint8_t>(in[pos + 1] & 0x1F);
                if (pos + 2 + 3 * static_cast<size_t>(cnt) > footerStart) {
                        return makeError<Cea708Cdp>(Error::CorruptData);
                }
                pos += 2;
                cdp.ccData.resize(cnt);
                for (uint8_t i = 0; i < cnt; ++i) {
                        uint8_t flagsByteIn = in[pos + 3 * i + 0];
                        CcData c;
                        c.valid = (flagsByteIn & 0x04) != 0;
                        c.type = static_cast<uint8_t>(flagsByteIn & 0x03);
                        c.b1 = in[pos + 3 * i + 1];
                        c.b2 = in[pos + 3 * i + 2];
                        cdp.ccData[i] = c;
                }
                pos += 3 * static_cast<size_t>(cnt);
        }

        // Optional ccsvcinfo_section (SMPTE 334-2 §5.5 Table 6).
        // Recognised by section_id 0x73 at the current position; not
        // gated on @c svcInfoPresent (parsers must skip any section
        // they don't understand per §5.7, and trust the flag bits
        // less than the wire structure).
        if (pos + 2 <= footerStart && in[pos] == CcSvcInfoSectionId) {
                const uint8_t flagByte = in[pos + 1];
                // Mirror the section's start/change/complete bits onto
                // the CDP struct — spec §5.5 mandates these match the
                // CDP header's matching flags, but a malformed encoder
                // could disagree.  The section is authoritative.
                cdp.svcInfoStart = (flagByte & 0x40) != 0;
                cdp.svcInfoChange = (flagByte & 0x20) != 0;
                cdp.svcInfoComplete = (flagByte & 0x10) != 0;
                const uint8_t svcCount = static_cast<uint8_t>(flagByte & 0x0F);
                const size_t  svcSize = 2 + 7 * static_cast<size_t>(svcCount);
                if (pos + svcSize > footerStart) {
                        return makeError<Cea708Cdp>(Error::CorruptData);
                }
                cdp.ccSvcInfo.resize(svcCount);
                size_t svcPos = pos + 2;
                for (uint8_t i = 0; i < svcCount; ++i) {
                        const uint8_t entryFlag = in[svcPos + 0];
                        CcSvcInfoEntry e;
                        // csn_size: bit 6.  '1' → 5-bit csn (bits 4..0
                        // after a reserved bit 5); '0' → 6-bit csn.
                        e.csnSize5Bit = (entryFlag & 0x40) != 0;
                        if (e.csnSize5Bit) {
                                e.captionServiceNumber = static_cast<uint8_t>(entryFlag & 0x1F);
                        } else {
                                e.captionServiceNumber = static_cast<uint8_t>(entryFlag & 0x3F);
                        }
                        e.languageCode[0] = in[svcPos + 1];
                        e.languageCode[1] = in[svcPos + 2];
                        e.languageCode[2] = in[svcPos + 3];
                        const uint8_t b4 = in[svcPos + 4];
                        e.digitalCc = (b4 & 0x80) != 0;
                        if (e.digitalCc) {
                                // svc_data_byte_4 carries the 6-bit
                                // caption_service_number in its low 6
                                // bits when digital_cc==1.  Per
                                // SMPTE 334-2 §5.5 / ATSC A/65 §6.9.2
                                // it must match the entry-flag service
                                // number.  We treat the entry flag as
                                // authoritative on the wire but log
                                // when they disagree — non-zero counts
                                // suggest a non-compliant encoder.
                                const uint8_t b4Svc = static_cast<uint8_t>(b4 & 0x3F);
                                if (b4Svc != e.captionServiceNumber) {
                                        ++cdp.svcInfoMismatches;
                                }
                                e.line21Field = false;
                        } else {
                                e.line21Field = (b4 & 0x01) != 0;
                        }
                        const uint8_t b5 = in[svcPos + 5];
                        e.easyReader = (b5 & 0x80) != 0;
                        e.wideAspect = (b5 & 0x40) != 0;
                        cdp.ccSvcInfo[i] = e;
                        svcPos += 7;
                }
                pos += svcSize;
        }

        // Any remaining bytes between here and the footer belong to a
        // @c future_section (SMPTE 334-2 §5.7) — section IDs in
        // 0x75..0xEF that this library doesn't model.  Preserve them
        // verbatim so the built form round-trips byte-exact.
        if (pos < footerStart) {
                size_t extraLen = footerStart - pos;
                Buffer extra(extraLen);
                extra.setSize(extraLen);
                Error err = extra.copyFrom(in + pos, extraLen, 0);
                if (err.isError()) return makeError<Cea708Cdp>(err);
                cdp.extraBytes = extra;
                pos = footerStart;
        }

        // Footer.
        if (in[pos] != FooterId) {
                return makeError<Cea708Cdp>(Error::CorruptData);
        }
        uint16_t footerSeq = static_cast<uint16_t>((in[pos + 1] << 8) | in[pos + 2]);
        if (footerSeq != cdp.sequenceCounter) {
                return makeError<Cea708Cdp>(Error::CorruptData);
        }

        return makeResult<Cea708Cdp>(std::move(cdp));
}

Result<Cea708Cdp> Cea708Cdp::fromBuffer(const Buffer &buf) {
        return fromBuffer(buf.data(), buf.size());
}

uint8_t Cea708Cdp::frameRateCodeFor(const FrameRate &frameRate) {
        // SMPTE 334-2 §5.1.4 cdp_frame_rate codes.
        const unsigned int num = frameRate.numerator();
        const unsigned int den = frameRate.denominator();
        if (num == 24000 && den == 1001) return 1; // 23.976
        if (num == 24 && den == 1) return 2;        // 24
        if (num == 25 && den == 1) return 3;        // 25
        if (num == 30000 && den == 1001) return 4;  // 29.97
        if (num == 30 && den == 1) return 5;        // 30
        if (num == 50 && den == 1) return 6;        // 50
        if (num == 60000 && den == 1001) return 7;  // 59.94
        if (num == 60 && den == 1) return 8;        // 60
        return 0;
}

// ============================================================================
// JSON
// ============================================================================

JsonObject Cea708Cdp::toJson() const {
        JsonObject obj;
        obj.set("identifier", static_cast<int64_t>(Identifier));
        obj.set("frameRateCode", static_cast<int64_t>(frameRateCode));
        obj.set("sequenceCounter", static_cast<int64_t>(sequenceCounter));
        obj.set("timeCodePresent", timeCodePresent);
        obj.set("ccDataPresent", ccDataPresent);
        obj.set("svcInfoPresent", svcInfoPresent);
        obj.set("svcInfoStart", svcInfoStart);
        obj.set("svcInfoChange", svcInfoChange);
        obj.set("svcInfoComplete", svcInfoComplete);
        obj.set("captionServiceActive", captionServiceActive);

        if (timeCodePresent) {
                obj.set("timeCode", timeCode.toString());
                obj.set("tcFieldFlag", tcFieldFlag);
                obj.set("dropFrame", timeCode.isDropFrame());
        }

        if (ccDataPresent) {
                JsonArray arr;
                for (size_t i = 0; i < ccData.size(); ++i) {
                        const CcData &c = ccData[i];
                        JsonObject t;
                        t.set("valid", c.valid);
                        t.set("type", static_cast<int64_t>(c.type));
                        t.set("b1", static_cast<int64_t>(c.b1));
                        t.set("b2", static_cast<int64_t>(c.b2));
                        arr.add(t);
                }
                obj.set("ccData", arr);
        }

        if (!ccSvcInfo.isEmpty()) {
                JsonArray arr;
                for (size_t i = 0; i < ccSvcInfo.size(); ++i) {
                        const CcSvcInfoEntry &e = ccSvcInfo[i];
                        JsonObject s;
                        s.set("csnSize5Bit", e.csnSize5Bit);
                        s.set("captionServiceNumber", static_cast<int64_t>(e.captionServiceNumber));
                        char lang[4] = {static_cast<char>(e.languageCode[0]),
                                         static_cast<char>(e.languageCode[1]),
                                         static_cast<char>(e.languageCode[2]), 0};
                        s.set("languageCode", String(lang));
                        s.set("digitalCc", e.digitalCc);
                        if (!e.digitalCc) s.set("line21Field", e.line21Field);
                        s.set("easyReader", e.easyReader);
                        s.set("wideAspect", e.wideAspect);
                        arr.add(s);
                }
                obj.set("ccSvcInfo", arr);
        }
        if (extraBytes.size() > 0) {
                obj.set("extraBytes", static_cast<int64_t>(extraBytes.size()));
        }
        return obj;
}

// ============================================================================
// Equality + diagnostics
// ============================================================================

bool Cea708Cdp::operator==(const Cea708Cdp &o) const {
        if (frameRateCode != o.frameRateCode) return false;
        if (timeCodePresent != o.timeCodePresent) return false;
        if (ccDataPresent != o.ccDataPresent) return false;
        if (svcInfoPresent != o.svcInfoPresent) return false;
        if (svcInfoStart != o.svcInfoStart) return false;
        if (svcInfoChange != o.svcInfoChange) return false;
        if (svcInfoComplete != o.svcInfoComplete) return false;
        if (captionServiceActive != o.captionServiceActive) return false;
        if (sequenceCounter != o.sequenceCounter) return false;
        if (timeCodePresent) {
                if (timeCode != o.timeCode) return false;
                if (tcFieldFlag != o.tcFieldFlag) return false;
        }
        if (ccData != o.ccData) return false;
        if (ccSvcInfo != o.ccSvcInfo) return false;
        if (!(extraBytes == o.extraBytes)) return false;
        return true;
}

String Cea708Cdp::toString() const {
        String s = "Cea708Cdp(seq=";
        s += String::number(static_cast<int>(sequenceCounter));
        s += ", rateCode=";
        s += String::number(static_cast<int>(frameRateCode));
        s += ", cc=";
        s += String::number(static_cast<int>(ccData.size()));
        if (timeCodePresent) {
                s += ", tc=";
                s += timeCode.toString();
        }
        if (svcInfoPresent) s += ", svcInfo";
        if (captionServiceActive) s += ", active";
        s += ")";
        return s;
}

// ============================================================================
// DataStream serialization (wire bytes via toBuffer / fromBuffer)
// ============================================================================

Error Cea708Cdp::writeToStream(DataStream &s) const {
        Buffer buf = toBuffer();
        s << buf;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<Cea708Cdp> Cea708Cdp::readFromStream<1>(DataStream &s) {
        Buffer buf;
        s >> buf;
        if (s.status() != DataStream::Ok) return makeError<Cea708Cdp>(s.toError());
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(buf);
        if (r.second().isError()) {
                s.setError(DataStream::ReadCorruptData,
                           String("Cea708Cdp::fromBuffer failed: ") + r.second().name());
                return makeError<Cea708Cdp>(r.second());
        }
        return r;
}

PROMEKI_NAMESPACE_END
