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

        // Encode an MM/SS/HH/Frame BCD nibble pair into the SMPTE 12M form
        // used by the CDP time-code section: high nibble = tens, low
        // nibble = units.  Tens are masked to the small range each field
        // legally allows (0..2 for hours, 0..5 for min/sec, 0..3 for
        // frame).
        uint8_t bcd(uint8_t value) {
                return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
        }

        uint8_t bcdHigh(uint8_t value, uint8_t tensMask) {
                uint8_t tens = static_cast<uint8_t>((value / 10) & tensMask);
                uint8_t units = static_cast<uint8_t>(value % 10);
                return static_cast<uint8_t>((tens << 4) | units);
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
                // SMPTE 334-2 §5.1.6.1 (mirrors SMPTE 12M):
                //   byte 1: high nibble = frame tens (0..3) | (BGF flags),
                //           low nibble  = frame units
                //   byte 2: high nibble = seconds tens (0..5),
                //           low nibble  = seconds units
                //   byte 3: high nibble = minutes tens (0..5),
                //           low nibble  = minutes units
                //   byte 4: high nibble = hours tens (0..2) | (FF flag),
                //           low nibble  = hours units
                // The BGF / FF flags are emitted as zero by this codec.
                out[pos + 1] = bcdHigh(static_cast<uint8_t>(timeCode.frame()), 0x03);
                out[pos + 2] = bcdHigh(static_cast<uint8_t>(timeCode.sec()), 0x07);
                out[pos + 3] = bcdHigh(static_cast<uint8_t>(timeCode.min()), 0x07);
                out[pos + 4] = bcdHigh(static_cast<uint8_t>(timeCode.hour()), 0x03);
                pos += timecodeSize;
        }

        // Optional cc_data section.
        if (ccDataPresent) {
                out[pos + 0] = CcDataSectionId;
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
                uint8_t b1 = in[pos + 1];
                uint8_t b2 = in[pos + 2];
                uint8_t b3 = in[pos + 3];
                uint8_t b4 = in[pos + 4];
                uint8_t frm = static_cast<uint8_t>(((b1 >> 4) & 0x03) * 10 + (b1 & 0x0F));
                uint8_t sec = static_cast<uint8_t>(((b2 >> 4) & 0x07) * 10 + (b2 & 0x0F));
                uint8_t min = static_cast<uint8_t>(((b3 >> 4) & 0x07) * 10 + (b3 & 0x0F));
                uint8_t hrs = static_cast<uint8_t>(((b4 >> 4) & 0x03) * 10 + (b4 & 0x0F));
                cdp.timeCode = Timecode(Timecode::Mode(Timecode::NDF30), hrs, min, sec, frm);
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
                auto [tcStr, err] = timeCode.toString();
                if (err.isOk()) obj.set("timeCode", tcStr);
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
        if (timeCodePresent && timeCode != o.timeCode) return false;
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
                auto [tcStr, err] = timeCode.toString();
                if (err.isOk()) {
                        s += ", tc=";
                        s += tcStr;
                }
        }
        if (svcInfoPresent) s += ", svcInfo";
        if (captionServiceActive) s += ", active";
        s += ")";
        return s;
}

// ============================================================================
// DataStream operators (wire bytes via toBuffer / fromBuffer)
// ============================================================================

void writeCea708CdpData(DataStream &stream, const Cea708Cdp &cdp) {
        Buffer buf = cdp.toBuffer();
        stream << buf;
}

Cea708Cdp readCea708CdpData(DataStream &stream) {
        Buffer buf;
        stream >> buf;
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(buf);
        if (r.second().isError()) {
                promekiWarn("Cea708Cdp DataStream read failed: %s", r.second().name().cstr());
                return Cea708Cdp();
        }
        return r.first();
}

DataStream &operator<<(DataStream &stream, const Cea708Cdp &cdp) {
        stream.beginFrame(DataStream::TypeCea708Cdp, 1);
        writeCea708CdpData(stream, cdp);
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, Cea708Cdp &cdp) {
        if (!stream.readFrame(DataStream::TypeCea708Cdp)) {
                cdp = Cea708Cdp();
                return stream;
        }
        cdp = readCea708CdpData(stream);
        return stream;
}

PROMEKI_NAMESPACE_END
