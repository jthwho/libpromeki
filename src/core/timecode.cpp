/**
 * @file      timecode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/timecode.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

VtcTimecode Timecode::toVtc() const {
        VtcTimecode vtc;
        vtc.hour = _hour;
        vtc.min = _min;
        vtc.sec = _sec;
        vtc.frame = _frame;
        vtc.userbits = 0;
        vtc.format = _mode.vtcFormat();
        vtc.flags = 0;
        if(_flags & FirstField) vtc.flags |= VTC_TC_FLAG_FIELD_1;
        return vtc;
}

void Timecode::fromVtc(const VtcTimecode &vtc) {
        _hour = vtc.hour;
        _min = vtc.min;
        _sec = vtc.sec;
        _frame = vtc.frame;
        if(vtc.format) {
                _mode = Mode(vtc.format);
        } else {
                // Parsed digits but no format determined — valid but format-less
                _mode = Mode(0u, 0u);
        }
        _flags = 0;
        if(vtc.flags & VTC_TC_FLAG_FIELD_1) _flags |= FirstField;
}

Timecode &Timecode::operator++() {
        VtcTimecode vtc = toVtc();
        vtc_timecode_increment(&vtc);
        fromVtc(vtc);
        return *this;
}

Timecode &Timecode::operator--() {
        VtcTimecode vtc = toVtc();
        vtc_timecode_decrement(&vtc);
        fromVtc(vtc);
        return *this;
}

Timecode Timecode::fromFrameNumber(const Mode &mode, const FrameNumber &frameNumber) {
        if(!mode.isValid() || mode.fps() == 0) return Timecode(mode);
        if(!frameNumber.isValid()) return Timecode(mode);
        VtcTimecode vtc;
        VtcError err = vtc_timecode_from_frames(&vtc, mode.vtcFormat(),
                                                static_cast<uint64_t>(frameNumber.value()));
        if(err != VTC_ERR_OK) return Timecode(mode);
        Timecode tc(mode);
        tc._hour = vtc.hour;
        tc._min = vtc.min;
        tc._sec = vtc.sec;
        tc._frame = vtc.frame;
        return tc;
}

// Canonical text form of an invalid / unknown timecode.  Used as both
// the @ref Timecode::toString output for default-constructed timecodes
// and the sentinel that @ref Timecode::fromString recognises as the
// "no information" round-trip partner of an invalid Timecode.
static const char *kInvalidTimecodeString = "--:--:--:--";

Result<Timecode> Timecode::fromString(const String &str) {
        // Empty input or the canonical invalid sentinel both round-trip
        // to a default-constructed Timecode.  This lets callers pass
        // toString() output back into fromString() without special-casing
        // the "no data" path.
        if(str.isEmpty() || str == kInvalidTimecodeString) {
                return makeResult(Timecode());
        }
        VtcTimecode vtc;
        VtcError err = vtc_timecode_from_string(&vtc, str.cstr());
        if(err != VTC_ERR_OK) {
                promekiErr("Failed to parse timecode from '%s': %s", str.cstr(), vtc_error_string(err));
                return makeError<Timecode>(Error::Invalid);
        }
        Timecode tc;
        tc.fromVtc(vtc);
        return makeResult(tc);
}

Result<String> Timecode::toString(const VtcStringFormat *fmt) const {
        // A default-constructed (or otherwise mode-invalid) Timecode has
        // no information to render.  Return the canonical "no data"
        // sentinel so callers always get a printable string and can
        // round-trip it back through @ref fromString.
        if(!isValid()) {
                return makeResult(String(kInvalidTimecodeString));
        }
        // Valid digits but no frame rate (e.g. parsed from a string
        // with no format hint, or recovered from an @ref ImageDataDecoder
        // BCD payload that only carries digits + the DF flag).  libvtc
        // can't render without a format pointer, so we lay out the
        // digits ourselves in standard SMPTE form.  The @p fmt argument
        // is intentionally ignored on this path because every
        // libvtc-specific style needs the rate to be meaningful.
        if(!_mode.hasFormat()) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u:%02u",
                              static_cast<unsigned>(_hour),
                              static_cast<unsigned>(_min),
                              static_cast<unsigned>(_sec),
                              static_cast<unsigned>(_frame));
                return makeResult(String(buf));
        }
        VtcTimecode vtc = toVtc();
        char buf[64];
        VtcError err = vtc_timecode_to_string(&vtc, fmt, buf, sizeof(buf));
        if(err != VTC_ERR_OK) {
                return makeError<String>(Error::Invalid);
        }
        return makeResult(String(buf));
}

// ============================================================================
// 64-bit BCD time-address packing
// ============================================================================
//
// Both LTC (SMPTE 12M-1) and VITC (SMPTE 12M-2) lay out the same set of
// time-address fields — eight BCD digits, the binary groups, the DF/CF
// flags, and BGF0/BGF1/BGF2 — at *almost* the same bit positions inside
// the lower 64 bits of their respective wire frames.  The only mismatch
// is bit 27: LTC uses it for the polarity correction bit (or BGF0 in
// 25fps mode), VITC uses it for the field marker.
//
// Ltc mode wraps libvtc's vtc_ltc_pack so the polarity correction bit
// is computed correctly per SMPTE 12M-1.  Vitc mode bypasses libvtc and
// writes the field marker bit directly from Timecode::isFirstField (which
// also doubles as the HFR frame-pair identifier per SMPTE 12-3).
//
// Bit numbering matches SMPTE 12M / libvtc convention: bit 0 is the LSB
// of the frame units nibble, bit 63 is the MSB of binary group 8.

namespace {

constexpr uint32_t BcdBitColorFrame  = 11;
constexpr uint32_t BcdBitDropFrame   = 10;
constexpr uint32_t BcdBitFieldMarker = 27;  // VITC only
constexpr uint32_t BcdBitBgf1        = 58;
constexpr uint32_t BcdBitBgf0_30fps  = 43;  // 30fps family BGF0 position
constexpr uint32_t BcdBitBgf2_30fps  = 59;  // 30fps family BGF2 position

inline uint8_t bcdTens(uint8_t v) { return (v / 10u) & 0x0fu; }
inline uint8_t bcdUnits(uint8_t v) { return v % 10u; }

// Set @c bits [start, start+count) of @p word to the @p count low-order
// bits of @p value.  start counts from the LSB.
inline void setBits(uint64_t &word, uint32_t start, uint32_t count, uint64_t value) {
        const uint64_t mask = ((uint64_t(1) << count) - 1u) << start;
        word = (word & ~mask) | ((value << start) & mask);
}

// Get @p count bits starting at @p start (LSB-counted) from @p word.
inline uint64_t getBits(uint64_t word, uint32_t start, uint32_t count) {
        const uint64_t mask = (uint64_t(1) << count) - 1u;
        return (word >> start) & mask;
}

// Pack the 64-bit VITC time-address word directly, without going through
// libvtc.  The layout matches SMPTE 12M-2 bit positions for the lower
// 64 data bits (i.e. excluding the inter-group sync nibbles and CRC).
uint64_t packVitc64(const Timecode &tc) {
        uint64_t word = 0;

        const uint8_t hh = tc.hour();
        const uint8_t mm = tc.min();
        const uint8_t ss = tc.sec();
        const uint8_t ff = tc.frame();

        setBits(word,  0, 4, bcdUnits(ff));            // frame units
        setBits(word,  8, 2, bcdTens(ff) & 0x03u);     // frame tens
        if(tc.isDropFrame())   word |= (uint64_t(1) << BcdBitDropFrame);
        // Color frame flag (bit 11) — left clear; not surfaced on Timecode.
        setBits(word, 16, 4, bcdUnits(ss));            // seconds units
        setBits(word, 24, 3, bcdTens(ss) & 0x07u);     // seconds tens
        if(tc.isFirstField()) word |= (uint64_t(1) << BcdBitFieldMarker);
        setBits(word, 32, 4, bcdUnits(mm));            // minute units
        setBits(word, 40, 3, bcdTens(mm) & 0x07u);     // minute tens
        // BGF0 (bit 43), BGF1 (bit 58), BGF2 (bit 59) — left clear; the
        // Timecode class does not currently surface them.  Userbits
        // similarly default to zero.
        setBits(word, 48, 4, bcdUnits(hh));            // hour units
        setBits(word, 56, 2, bcdTens(hh) & 0x03u);     // hour tens
        return word;
}

// Read libvtc's 80-bit LTC output back into a uint64_t containing the
// lower 64 data bits.  The 16-bit sync word at bits 64-79 is
// intentionally dropped; wire framing is the encoder's job.
uint64_t ltc80LowerWord(const VtcLTC &ltc) {
        uint64_t word = 0;
        for(int i = 0; i < 8; i++) {
                word |= static_cast<uint64_t>(ltc.data[i]) << (i * 8);
        }
        return word;
}

// Write a 64-bit data word back into the lower half of an LTC structure
// for use with vtc_ltc_unpack.  The upper 16 bits (sync word) are
// populated with the canonical forward sync pattern because libvtc's
// vtc_ltc_unpack rejects any frame whose sync word does not match —
// our 64-bit BCD payload deliberately omits the sync word over the
// wire (the encoder layer provides its own framing), so we re-attach
// it here purely so vtc_ltc_unpack accepts the synthesised LTC frame.
void writeLtc80LowerWord(VtcLTC &ltc, uint64_t word) {
        for(int i = 0; i < 8; i++) {
                ltc.data[i] = static_cast<uint8_t>((word >> (i * 8)) & 0xffu);
        }
        ltc.data[8] = static_cast<uint8_t>(VTC_LTC_SYNC_WORD_FORWARD & 0xffu);
        ltc.data[9] = static_cast<uint8_t>((VTC_LTC_SYNC_WORD_FORWARD >> 8) & 0xffu);
}

// Look up the drop-frame sister of a given format.  Returns nullptr if
// no DF variant exists for that rate.
const VtcFormat *findDropFrameSister(const VtcFormat *fmt) {
        if(fmt == nullptr) return nullptr;
        if(vtc_format_is_drop_frame(fmt)) return fmt;
        const uint32_t flags = (fmt->flags | VTC_FORMAT_FLAG_DROP_FRAME);
        return vtc_format_find(fmt->tc_fps, flags);
}

}  // namespace

uint64_t Timecode::toBcd64(TimecodePackFormat fmt) const {
        if(fmt == TimecodePackFormat::Ltc) {
                // Wrap libvtc — gives us correct polarity correction
                // and BGF handling per SMPTE 12M-1 for free.
                VtcTimecode vtc = toVtc();
                VtcLTC ltc;
                if(vtc_ltc_pack(&vtc, &ltc) != VTC_ERR_OK) {
                        // libvtc rejects only obviously bad inputs (mm/ss
                        // > 59).  Fall through to the direct packer in
                        // that case so the function remains total.
                        return packVitc64(*this);
                }
                return ltc80LowerWord(ltc);
        }
        return packVitc64(*this);
}

Result<Timecode> Timecode::fromBcd64(uint64_t bcd, TimecodePackFormat fmt, const Mode &mode) {
        // Resolve the effective mode against the DF flag.
        const bool dfFlag = ((bcd >> BcdBitDropFrame) & 1u) != 0u;
        const VtcFormat *vtcFmt = mode.vtcFormat();

        Mode effectiveMode = mode;
        if(dfFlag) {
                if(vtcFmt == nullptr) {
                        // Unknown mode + DF flag → infer 29.97 DF.
                        effectiveMode = Mode(&VTC_FORMAT_29_97_DF);
                } else if(vtc_format_is_drop_frame(vtcFmt)) {
                        // Already a DF format — keep as-is.
                } else {
                        // NDF mode — try to find its DF sister.
                        const VtcFormat *df = findDropFrameSister(vtcFmt);
                        if(df == nullptr) {
                                // Mode does not support DF and BCD says
                                // it should — flag the inconsistency.
                                return makeError<Timecode>(Error::ConversionFailed);
                        }
                        effectiveMode = Mode(df);
                }
        } else if(!mode.isValid()) {
                // No DF inference available and the caller passed an
                // invalid Mode — promote to a valid-but-format-less
                // mode so the resulting Timecode reports digits via
                // hour() / min() / etc. and renders cleanly through
                // toString().  toFrameNumber() will still fail until
                // a real frame rate is supplied via setMode().
                effectiveMode = Mode(0u, 0u);
        }

        // Extract digits.  In Ltc mode we route through libvtc's unpacker
        // so we get the same flag preservation that vtc_ltc_pack performs;
        // in Vitc mode we extract the nibbles directly.
        Timecode tc;
        if(fmt == TimecodePackFormat::Ltc) {
                VtcLTC ltc;
                writeLtc80LowerWord(ltc, bcd);
                VtcTimecode vtc;
                VtcError err = vtc_ltc_unpack(&ltc, &vtc, effectiveMode.vtcFormat());
                if(err != VTC_ERR_OK) {
                        return makeError<Timecode>(Error::ConversionFailed);
                }
                tc.fromVtc(vtc);
                // vtc_ltc_unpack sets _mode from the format pointer it
                // wrote back.  If the caller passed an unknown mode and
                // we inferred 29.97 DF above, libvtc adopts that
                // inferred format, so the result already carries the
                // right mode.
                if(effectiveMode.vtcFormat() != nullptr) {
                        tc.setMode(effectiveMode);
                }
        } else {
                const uint8_t ffUnits = static_cast<uint8_t>(getBits(bcd,  0, 4));
                const uint8_t ffTens  = static_cast<uint8_t>(getBits(bcd,  8, 2));
                const uint8_t ssUnits = static_cast<uint8_t>(getBits(bcd, 16, 4));
                const uint8_t ssTens  = static_cast<uint8_t>(getBits(bcd, 24, 3));
                const uint8_t mmUnits = static_cast<uint8_t>(getBits(bcd, 32, 4));
                const uint8_t mmTens  = static_cast<uint8_t>(getBits(bcd, 40, 3));
                const uint8_t hhUnits = static_cast<uint8_t>(getBits(bcd, 48, 4));
                const uint8_t hhTens  = static_cast<uint8_t>(getBits(bcd, 56, 2));

                tc.setMode(effectiveMode);
                tc._hour  = static_cast<DigitType>(hhTens * 10u + hhUnits);
                tc._min   = static_cast<DigitType>(mmTens * 10u + mmUnits);
                tc._sec   = static_cast<DigitType>(ssTens * 10u + ssUnits);
                tc._frame = static_cast<DigitType>(ffTens * 10u + ffUnits);

                // Field marker → FirstField flag (HFR frame-pair bit).
                if(((bcd >> BcdBitFieldMarker) & 1u) != 0u) {
                        tc._flags |= FirstField;
                }
        }
        return makeResult(tc);
}

FrameNumber Timecode::toFrameNumber() const {
        if(!isValid()) return FrameNumber::unknown();
        if(!_mode.hasFormat()) return FrameNumber::unknown();
        VtcTimecode vtc = toVtc();
        uint64_t frameNum;
        VtcError err = vtc_timecode_to_frames(&vtc, &frameNum);
        if(err != VTC_ERR_OK) return FrameNumber::unknown();
        return FrameNumber(static_cast<int64_t>(frameNum));
}

PROMEKI_NAMESPACE_END
