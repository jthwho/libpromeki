/**
 * @file      cea708cdp.cpp
 * @copyright Howard Logic. All rights reserved.
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
        const size_t extraSize = extraBytes.size();
        const size_t footerSize = 4;
        const size_t headerSize = 7;
        const size_t total =
                headerSize + timecodeSize + ccDataSize + extraSize + footerSize;

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

        // Any bytes between here and the footer are opaque sections
        // (ccsvcinfo / future_section).  Preserve them verbatim so the
        // built form round-trips byte-exact.
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
