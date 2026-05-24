/**
 * @file      cea608xds.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <ctime>
#include <promeki/cea608.h>
#include <promeki/cea608xds.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(Cea608Xds)

namespace {

        /// @brief Maps a class-start control byte (0x01, 0x03, 0x05,
        ///        0x07, 0x09, 0x0B, 0x0D) to its @ref Cea608XdsClass.
        ///        Returns @c Cea608XdsClass::Unknown for any other
        ///        value.  Per CEA-608-E §9.3 Table 14.
        Cea608XdsClass classFromStartCode(uint8_t code) {
                switch (code) {
                        case 0x01: return Cea608XdsClass::Current;
                        case 0x03: return Cea608XdsClass::Future;
                        case 0x05: return Cea608XdsClass::Channel;
                        case 0x07: return Cea608XdsClass::Misc;
                        case 0x09: return Cea608XdsClass::PublicSvc;
                        case 0x0B: return Cea608XdsClass::Reserved;
                        case 0x0D: return Cea608XdsClass::PrivateData;
                        default:   return Cea608XdsClass::Unknown;
                }
        }

        /// @brief Maps a class-continue control byte (0x02, 0x04, ...,
        ///        0x0E) to its @ref Cea608XdsClass.  Returns
        ///        @c Cea608XdsClass::Unknown for non-continue bytes.
        Cea608XdsClass classFromContinueCode(uint8_t code) {
                switch (code) {
                        case 0x02: return Cea608XdsClass::Current;
                        case 0x04: return Cea608XdsClass::Future;
                        case 0x06: return Cea608XdsClass::Channel;
                        case 0x08: return Cea608XdsClass::Misc;
                        case 0x0A: return Cea608XdsClass::PublicSvc;
                        case 0x0C: return Cea608XdsClass::Reserved;
                        case 0x0E: return Cea608XdsClass::PrivateData;
                        default:   return Cea608XdsClass::Unknown;
                }
        }

        /// @brief Composes the @c (class, type) key used to index in-flight
        ///        sub-packet buffers in the extractor's @c Impl::inFlight
        ///        map.
        uint16_t makeKey(Cea608XdsClass c, uint8_t type) {
                return static_cast<uint16_t>(
                        (static_cast<uint16_t>(c) << 8) | static_cast<uint16_t>(type));
        }

} // namespace

// ============================================================================
// Cea608XdsPacket — typed accessors
// ============================================================================

String Cea608XdsPacket::text() const {
        String out;
        // The spec pads odd-length text packets with a trailing 0x00
        // null to maintain the even-byte-count rule (§9.2).  Drop those
        // nulls; everything else (0x20..0x7F) is a printable 7-bit ASCII
        // character.  Per §8.6.1 informational bytes are 0x00 or
        // 0x20..0x7F — anything else is a wire-format anomaly.
        for (size_t i = 0; i < payload.size(); ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if (b < 0x20 || b > 0x7F) {
                        promekiDebug(
                                "Cea608XdsPacket::text: dropping non-printable byte "
                                "0x%02X (class=%u, type=0x%02X) — §8.6.1 informational "
                                "characters are 0x00 or 0x20..0x7F.",
                                static_cast<unsigned>(b),
                                static_cast<unsigned>(class_),
                                static_cast<unsigned>(type));
                        continue;
                }
                const char ch = static_cast<char>(b);
                out += String(&ch, 1);
        }
        return out;
}

String Cea608XdsPacket::programName() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x03) {
                return String();
        }
        return text();
}

String Cea608XdsPacket::networkName() const {
        if (class_ != Cea608XdsClass::Channel || type != 0x01) return String();
        return text();
}

Optional<int> Cea608XdsPacket::nativeChannel() const {
        if (class_ != Cea608XdsClass::Channel || type != 0x02) return Optional<int>();
        // Per §9.5.3.2: the optional 6-byte payload form's last two
        // bytes carry the native FCC channel number as two
        // displayable digit characters.  Range 2..69.  Single-digit
        // numbers use either a leading '0' (0x30) or a leading NUL
        // (0x00).
        if (payload.size() < 6) return Optional<int>();
        const uint8_t hi = payload[4];
        const uint8_t lo = payload[5];
        // Low digit must be a real ASCII digit.
        if (lo < 0x30 || lo > 0x39) return Optional<int>();
        // High digit either an ASCII digit or NUL.
        int hiDigit = 0;
        if (hi == 0x00) {
                hiDigit = 0;
        } else if (hi >= 0x30 && hi <= 0x39) {
                hiDigit = hi - 0x30;
        } else {
                return Optional<int>();
        }
        const int loDigit = lo - 0x30;
        const int channel = hiDigit * 10 + loDigit;
        if (channel < 2 || channel > 69) return Optional<int>();
        return Optional<int>(channel);
}

String Cea608XdsPacket::callLetters() const {
        if (class_ != Cea608XdsClass::Channel || type != 0x02) return String();
        // First four bytes are call letters (0x20..0x7F), padded with
        // 0x20 if the call sign is only three letters.  The optional
        // last two bytes are the native channel digits.  Strip trailing
        // spaces; never surface the digits as part of the letters.
        if (payload.size() < 4) return String();
        String out;
        for (size_t i = 0; i < 4; ++i) {
                const uint8_t b = payload[i];
                if (b < 0x20 || b > 0x7F) return String();
                const char ch = static_cast<char>(b);
                out += String(&ch, 1);
        }
        // Trim trailing 0x20 (space) padding.
        while (out.length() > 0 && out.charAt(out.length() - 1).codepoint() == 0x20) {
                out = out.substr(0, out.length() - 1);
        }
        return out;
}

Optional<DateTime> Cea608XdsPacket::timeOfDay() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x01) return Optional<DateTime>();
        // Wire layout per §9.5.4.1: six informational bytes — minute,
        // hour, date, month, day-of-week, year — each with bit 6 set
        // to keep the byte outside the XDS control-code range.
        if (payload.size() < 6) return Optional<DateTime>();
        const uint8_t mByte = payload[0];
        const uint8_t hByte = payload[1];
        const uint8_t dByte = payload[2];
        const uint8_t moByte = payload[3];
        // payload[4] is day-of-week (1..7) and payload[5] is year-1990
        // (0..63); we read year but not day-of-week (std::tm derives it
        // from the calendar date during normalisation).
        const uint8_t yByte = payload[5];
        // Bit 6 of every payload byte must be 1 per spec — a missing
        // bit means the packet is corrupt or we're decoding the wrong
        // type.  Bail out rather than yielding garbage.
        const uint8_t kBit6Mask = 0x40;
        if (((mByte & kBit6Mask) == 0) || ((hByte & kBit6Mask) == 0)
            || ((dByte & kBit6Mask) == 0) || ((moByte & kBit6Mask) == 0)
            || ((yByte & kBit6Mask) == 0)) {
                return Optional<DateTime>();
        }
        const int minute = mByte & 0x3F;
        const int hour = hByte & 0x1F;        // bits 0..4; bit 5 = D (DST flag, ignored on receive per spec)
        const int date = dByte & 0x1F;        // bits 0..4; bit 5 = L (leap-year flag, advisory only)
        const int month = moByte & 0x0F;      // bits 0..3; bit 4 = T (tape delay flag), bit 5 = Z (zero seconds flag)
        const int year = (yByte & 0x3F) + 1990;
        // Reject out-of-range values per §9.5.1.1: minute 0..59, hour
        // 0..23, date 1..31, month 1..12.  An XDS "all-ones" packet
        // (minute=63, hour=31, etc.) is the spec's "end of current
        // program" sentinel; surface that as an empty Optional.
        if (minute < 0 || minute > 59) return Optional<DateTime>();
        if (hour < 0 || hour > 23) return Optional<DateTime>();
        if (date < 1 || date > 31) return Optional<DateTime>();
        if (month < 1 || month > 12) return Optional<DateTime>();
        // §9.5.4.1 wire year field is 6 bits → max value (year-1990)
        // is 63, so the absolute spec ceiling is 2053.  The field
        // cannot mathematically exceed that range, but we keep the
        // explicit guard + a debug warning so a future spec revision
        // (or a non-conformant encoder that stuffs bits 6/7 into the
        // year byte) gets surfaced rather than silently mis-decoded.
        if (year > 2053) {
                promekiWarn(
                        "Cea608XdsPacket::timeOfDay: decoded year %d exceeds the "
                        "§9.5.4.1 6-bit ceiling (year-1990 ≤ 63 ⇒ year ≤ 2053).",
                        year);
                return Optional<DateTime>();
        }
        if (year < 1990) return Optional<DateTime>();
        // Assemble a std::tm in UTC.  std::mktime treats the tm as
        // local time, so we use timegm where available; if not, we
        // adjust manually.  glibc, musl, and macOS libc all provide
        // timegm.
        std::tm tm = {};
        tm.tm_sec = 0;
        tm.tm_min = minute;
        tm.tm_hour = hour;
        tm.tm_mday = date;
        tm.tm_mon = month - 1; // std::tm uses 0..11
        tm.tm_year = year - 1900;
        tm.tm_isdst = 0;
        // timegm interprets the tm as UTC.  POSIX doesn't standardise
        // it but it's universal across Linux / BSD / macOS / Windows
        // (as _mkgmtime there).
        const time_t utc = timegm(&tm);
        if (utc == static_cast<time_t>(-1)) return Optional<DateTime>();
        return Optional<DateTime>(DateTime(utc));
}

bool Cea608XdsPacket::timeOfDayDstFlag() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x01) return false;
        if (payload.size() < 6) return false;
        // Hour byte: bit 5 = D (DST).  Bit 6 is the spec's "char in
        // 0x40..0x7F" guard; verify it before trusting the flag.
        const uint8_t hByte = payload[1];
        if ((hByte & 0x40) == 0) return false;
        return (hByte & 0x20) != 0;
}

bool Cea608XdsPacket::timeOfDayLeapYearFlag() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x01) return false;
        if (payload.size() < 6) return false;
        // Date byte: bit 5 = L (leap year).
        const uint8_t dByte = payload[2];
        if ((dByte & 0x40) == 0) return false;
        return (dByte & 0x20) != 0;
}

bool Cea608XdsPacket::timeOfDayTapeDelayFlag() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x01) return false;
        if (payload.size() < 6) return false;
        // Month byte: bit 4 = T (tape delay).
        const uint8_t moByte = payload[3];
        if ((moByte & 0x40) == 0) return false;
        return (moByte & 0x10) != 0;
}

bool Cea608XdsPacket::timeOfDayZeroSecondsFlag() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x01) return false;
        if (payload.size() < 6) return false;
        // Month byte: bit 5 = Z (zero seconds).
        const uint8_t moByte = payload[3];
        if ((moByte & 0x40) == 0) return false;
        return (moByte & 0x20) != 0;
}

String Cea608XdsContentAdvisory::ratingName() const {
        // Per CEA-608-E Tables 20-23.  Level is the integer index
        // surfaced into the struct from g2..g0 (or r2..r0 for MPAA).
        switch (system) {
                case Cea608XdsRatingSystem::Mpaa: {
                        static const char *kMpaa[8] = {
                                "N/A", "G", "PG", "PG-13", "R", "NC-17", "X", "Not Rated",
                        };
                        if (level < 8) return String(kMpaa[level]);
                        return String();
                }
                case Cea608XdsRatingSystem::UsTvParental: {
                        // Table 21: age rating from g2..g0.
                        static const char *kUsTv[8] = {
                                "None", "TV-Y", "TV-Y7", "TV-G", "TV-PG", "TV-14", "TV-MA", "None",
                        };
                        if (level < 8) return String(kUsTv[level]);
                        return String();
                }
                case Cea608XdsRatingSystem::CanadianEnglish: {
                        // Table 22: E / C / C8+ / G / PG / 14+ / 18+.
                        // Level 7 is invalid per spec; @ref
                        // Cea608XdsPacket::contentAdvisory rejects it
                        // upstream so we don't surface "" here.
                        static const char *kCanEn[8] = {
                                "E", "C", "C8+", "G", "PG", "14+", "18+", "",
                        };
                        if (level < 8) return String(kCanEn[level]);
                        return String();
                }
                case Cea608XdsRatingSystem::CanadianFrench: {
                        // Table 23: E / G / 8 ans + / 13 ans + / 16 ans + / 18 ans +.
                        // Levels 6 and 7 are invalid per spec.
                        static const char *kCanFr[8] = {
                                "E", "G", "8 ans +", "13 ans +", "16 ans +", "18 ans +", "", "",
                        };
                        if (level < 8) return String(kCanFr[level]);
                        return String();
                }
        }
        return String();
}

Optional<Cea608XdsContentAdvisory> Cea608XdsPacket::contentAdvisory() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x05) {
                return Optional<Cea608XdsContentAdvisory>();
        }
        if (payload.size() < 2) return Optional<Cea608XdsContentAdvisory>();
        const uint8_t c1 = payload[0];
        const uint8_t c2 = payload[1];
        // Both characters require bit 6 high per §9.5.1.5.
        if ((c1 & 0x40) == 0 || (c2 & 0x40) == 0) {
                return Optional<Cea608XdsContentAdvisory>();
        }
        // Decode rating-system selector (a3, a2, a1, a0) per Table 18:
        //   a0 = char1 bit 3
        //   a1 = char1 bit 4
        //   a2 = char1 bit 5    (also D bit for US TV)
        //   a3 = char2 bit 3    (also L bit for US TV)
        // Selector per Table 19:
        //   (-,-,0,0)  → System 0 — MPAA picture rating.
        //   (L,D,0,1)  → System 1 — US TV Parental Guidelines.
        //   (-,-,1,0)  → System 2 — MPAA picture rating (backward compat).
        //   (0,0,1,1)  → System 3 — Canadian English language rating.
        //   (0,1,1,1)  → System 4 — Canadian French language rating.
        //   (1,*,1,1)  → Reserved — packet is invalid.
        const int a0 = (c1 >> 3) & 0x01;
        const int a1 = (c1 >> 4) & 0x01;
        const int a2 = (c1 >> 5) & 0x01;
        const int a3 = (c2 >> 3) & 0x01;
        Cea608XdsContentAdvisory adv;
        if (a1 == 0 && a0 == 1) {
                // -- US TV Parental Guidelines (Tables 17, 18, 21).
                adv.system = Cea608XdsRatingSystem::UsTvParental;
                // Character 1 carries D (Dialog) in bit 5 (= the a2 slot
                // when not used as a selector).
                adv.dialog = (c1 & 0x20) != 0;
                // Character 2 wire layout per Table 18:
                //   b6=1 b5=(F)V b4=S b3=L b2=g2 b1=g1 b0=g0
                const bool vBit = (c2 & 0x20) != 0;
                adv.sexual = (c2 & 0x10) != 0;
                adv.language = (c2 & 0x08) != 0;
                adv.level = static_cast<uint8_t>(c2 & 0x07);
                // FV is the V bit when the age rating is TV-Y7 (level 2 per
                // Table 21); otherwise V is regular Violence.
                if (adv.level == 2) {
                        adv.fantasyViolence = vBit;
                        adv.violence = false;
                } else {
                        adv.violence = vBit;
                        adv.fantasyViolence = false;
                }
                return Optional<Cea608XdsContentAdvisory>(adv);
        }
        if (a1 == 0 && a0 == 0) {
                // -- System 0 — MPAA picture ratings (Table 20).
                adv.system = Cea608XdsRatingSystem::Mpaa;
                const uint8_t r = static_cast<uint8_t>(c1 & 0x07);
                adv.mpaa = static_cast<Cea608XdsMpaaRating>(r);
                adv.level = r;
                return Optional<Cea608XdsContentAdvisory>(adv);
        }
        if (a1 == 1 && a0 == 0) {
                // -- System 2 — MPAA picture ratings, backward-compat
                // form (Table 19 footnote 4).  Same payload as System 0.
                adv.system = Cea608XdsRatingSystem::Mpaa;
                const uint8_t r = static_cast<uint8_t>(c1 & 0x07);
                adv.mpaa = static_cast<Cea608XdsMpaaRating>(r);
                adv.level = r;
                return Optional<Cea608XdsContentAdvisory>(adv);
        }
        // (a1, a0) == (1, 1) — Canadian, with (a3, a2) picking language
        // or signalling Reserved.
        if (a3 == 1) {
                // (1, *, 1, 1) — Reserved for non-U.S. & non-Canadian
                // systems; treat as invalid per §9.5.1.5.
                return Optional<Cea608XdsContentAdvisory>();
        }
        adv.system = (a2 == 0) ? Cea608XdsRatingSystem::CanadianEnglish
                               : Cea608XdsRatingSystem::CanadianFrench;
        // Both Canadian systems carry the level in g2..g0 of char 2.
        adv.level = static_cast<uint8_t>(c2 & 0x07);
        // Reject invalid level values per Tables 22, 23.
        //   English: (1,1,1) is invalid.
        //   French:  (1,1,0) and (1,1,1) are invalid.
        if (adv.system == Cea608XdsRatingSystem::CanadianEnglish) {
                if (adv.level == 0x07) return Optional<Cea608XdsContentAdvisory>();
        } else {
                if (adv.level == 0x06 || adv.level == 0x07) {
                        return Optional<Cea608XdsContentAdvisory>();
                }
        }
        return Optional<Cea608XdsContentAdvisory>(adv);
}

Optional<Cea608XdsProgramId> Cea608XdsPacket::programId() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x01) {
                return Optional<Cea608XdsProgramId>();
        }
        // Per §9.5.1.1 the packet is exactly 4 informational bytes:
        // minute, hour, date, month (each with bit 6 set per §8.6.1).
        if (payload.size() < 4) return Optional<Cea608XdsProgramId>();
        const uint8_t mByte = payload[0];
        const uint8_t hByte = payload[1];
        const uint8_t dByte = payload[2];
        const uint8_t moByte = payload[3];
        const uint8_t kBit6 = 0x40;
        if (((mByte & kBit6) == 0) || ((hByte & kBit6) == 0)
            || ((dByte & kBit6) == 0) || ((moByte & kBit6) == 0)) {
                return Optional<Cea608XdsProgramId>();
        }
        Cea608XdsProgramId pid;
        pid.minute = static_cast<uint8_t>(mByte & 0x3F);
        pid.hour = static_cast<uint8_t>(hByte & 0x1F);
        pid.date = static_cast<uint8_t>(dByte & 0x1F);
        pid.month = static_cast<uint8_t>(moByte & 0x0F);
        // Month byte: bit 4 = T (tape delay).  Per §9.5.1.1 the D,
        // L, and Z bits are explicitly "ignored by the decoder when
        // processing this packet" — only the T bit is meaningful
        // here.
        pid.tapeDelay = (moByte & 0x10) != 0;
        if (pid.minute > 59 || pid.hour > 23 || pid.date == 0 || pid.date > 31
            || pid.month == 0 || pid.month > 12) {
                return Optional<Cea608XdsProgramId>();
        }
        return Optional<Cea608XdsProgramId>(pid);
}

bool Cea608XdsPacket::isProgramIdEndOfProgramSentinel() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x01) {
                return false;
        }
        // §9.5.1.1: "When all characters of this packet contain all
        // Ones, it indicates the end of the current program."  Each
        // payload byte has bit 6 forced to 1 (XDS marker) plus six
        // value bits — "all ones" therefore means 0x7F on every byte.
        if (payload.size() < 4) return false;
        for (size_t i = 0; i < 4; ++i) {
                if (payload[i] != 0x7F) return false;
        }
        return true;
}

Optional<Cea608XdsProgramLength> Cea608XdsPacket::programLength() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x02) {
                return Optional<Cea608XdsProgramLength>();
        }
        // Per §9.5.1.2 Table 16 the wire payload is exactly 2, 4, or 6
        // informational bytes, in order:
        //   [0] Length minutes   [1] Length hours
        //   [2] Elapsed minutes  [3] Elapsed hours
        //   [4] Elapsed seconds  [5] Null
        // Each non-null byte has bit 6 set as the standard XDS marker
        // and carries six value bits in bits 0..5.
        if (payload.size() < 2) return Optional<Cea608XdsProgramLength>();
        const uint8_t kBit6 = 0x40;
        Cea608XdsProgramLength pl;
        const uint8_t lmByte = payload[0];
        const uint8_t lhByte = payload[1];
        if (((lmByte & kBit6) == 0) || ((lhByte & kBit6) == 0)) {
                return Optional<Cea608XdsProgramLength>();
        }
        pl.lengthMinutes = static_cast<uint8_t>(lmByte & 0x3F);
        pl.lengthHours = static_cast<uint8_t>(lhByte & 0x3F);
        if (pl.lengthMinutes > 59 || pl.lengthHours > 23) {
                return Optional<Cea608XdsProgramLength>();
        }
        // Optional elapsed-(m)/(h) pair.
        if (payload.size() >= 4) {
                const uint8_t emByte = payload[2];
                const uint8_t ehByte = payload[3];
                if (((emByte & kBit6) == 0) || ((ehByte & kBit6) == 0)) {
                        return Optional<Cea608XdsProgramLength>();
                }
                pl.elapsedMinutes = static_cast<uint8_t>(emByte & 0x3F);
                pl.elapsedHours = static_cast<uint8_t>(ehByte & 0x3F);
                if (pl.elapsedMinutes > 59 || pl.elapsedHours > 23) {
                        return Optional<Cea608XdsProgramLength>();
                }
                pl.hasElapsedTime = true;
        }
        // Optional elapsed-(s) + null pair.
        if (payload.size() >= 6) {
                const uint8_t esByte = payload[4];
                // payload[5] is the §9.5.1.2 trailing null (0x00) — its
                // bit 6 is intentionally clear; we don't validate the
                // null byte's value beyond expecting it to be zero by
                // spec.
                if ((esByte & kBit6) == 0) {
                        return Optional<Cea608XdsProgramLength>();
                }
                pl.elapsedSeconds = static_cast<uint8_t>(esByte & 0x3F);
                if (pl.elapsedSeconds > 59) {
                        return Optional<Cea608XdsProgramLength>();
                }
                pl.hasElapsedSeconds = true;
        }
        return Optional<Cea608XdsProgramLength>(pl);
}

List<uint8_t> Cea608XdsPacket::programTypeKeywords() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x04) {
                return List<uint8_t>();
        }
        // Per §9.5.1.4: payload is two..thirty-two keyword bytes,
        // each in the range 0x20..0x7F.  Basic-group keywords
        // (0x20..0x26) must precede Detail-group keywords
        // (0x27..0x7F).  Trailing 0x00 even-byte padding is ignored
        // (matches text() semantics).
        List<uint8_t> out;
        bool sawDetail = false;
        bool order_ok = true;
        for (size_t i = 0; i < payload.size(); ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if (b < 0x20 || b > 0x7F) continue;
                const bool isBasic = (b >= 0x20 && b <= 0x26);
                if (sawDetail && isBasic) order_ok = false;
                if (!isBasic) sawDetail = true;
                out.pushToBack(b);
        }
        if (out.size() < 2 || out.size() > 32 || !order_ok) {
                return List<uint8_t>();
        }
        return out;
}

Optional<Cea608XdsAspectRatio> Cea608XdsPacket::aspectRatio() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x09) {
                return Optional<Cea608XdsAspectRatio>();
        }
        // Per §9.5.1.9: 2-byte payload.  Byte 1 bits 0..4 = start line
        // (0..21 offset from line 22); bit 5 = squeezed flag; bit 6
        // is the standard 0x40 marker.  Byte 2 bits 0..4 = end line.
        if (payload.size() < 2) return Optional<Cea608XdsAspectRatio>();
        const uint8_t b1 = payload[0];
        const uint8_t b2 = payload[1];
        if ((b1 & 0x40) == 0 || (b2 & 0x40) == 0) return Optional<Cea608XdsAspectRatio>();
        // Byte 2 bit 5 is reserved (zero) per §9.5.1.9.  Reject the
        // packet when an out-of-spec encoder sets it.
        if ((b2 & 0x20) != 0) return Optional<Cea608XdsAspectRatio>();
        Cea608XdsAspectRatio ar;
        ar.startLine = static_cast<uint8_t>(b1 & 0x1F);
        ar.squeezed = (b1 & 0x20) != 0;
        ar.endLine = static_cast<uint8_t>(b2 & 0x1F);
        return Optional<Cea608XdsAspectRatio>(ar);
}

Optional<Cea608XdsCgmsA> Cea608XdsPacket::cgmsA() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x08) {
                return Optional<Cea608XdsCgmsA>();
        }
        // Per §9.5.1.8 Table 29: 2 informational bytes carry the CGMS-A
        // packet.
        //   Byte 1: b6=1, b5=-, b4=CGMS-A high, b3=CGMS-A low,
        //           b2=APS high, b1=APS low, b0=ASB.
        //   Byte 2: b6=1, b5..b1=Reserved (zero), b0=RCD.
        // §9.1 normative: receivers must reject the packet when a
        // "-" (reserved) bit is non-zero.
        if (payload.size() < 2) return Optional<Cea608XdsCgmsA>();
        const uint8_t b1 = payload[0];
        const uint8_t b2 = payload[1];
        if ((b1 & 0x40) == 0 || (b2 & 0x40) == 0) return Optional<Cea608XdsCgmsA>();
        // Byte 1 bit 5 is the spec's lone reserved "-" bit; bit 7
        // (the parity slot) is consumed before the byte reaches the
        // payload, so we only police bit 5 here.
        if ((b1 & 0x20) != 0) return Optional<Cea608XdsCgmsA>();
        // Byte 2 bits 5..1 are all "-".  Bit 7 (parity slot) is
        // already stripped.
        if ((b2 & 0x3E) != 0) return Optional<Cea608XdsCgmsA>();
        Cea608XdsCgmsA out;
        out.cgms = static_cast<Cea608XdsCgmsControl>((b1 >> 3) & 0x03);
        const Cea608XdsApsControl apsRaw =
                static_cast<Cea608XdsApsControl>((b1 >> 1) & 0x03);
        out.analogSourceBit = (b1 & 0x01) != 0;
        out.redistributionControl = (b2 & 0x01) != 0;
        // §9.5.1.8 note: APS is meaningful only when CGMS-A signals
        // NoMoreCopies or CopyNever — otherwise the receiver has no
        // need for the analog protection scheme.  Surface APS only
        // in that case so downstream callers can't misinterpret a
        // CopyFree packet as carrying real APS state.
        if (out.cgms == Cea608XdsCgmsControl::NoMoreCopies
            || out.cgms == Cea608XdsCgmsControl::CopyNever) {
                out.aps = Optional<Cea608XdsApsControl>(apsRaw);
        }
        return Optional<Cea608XdsCgmsA>(out);
}

Optional<uint16_t> Cea608XdsPacket::transmissionSignalId() const {
        if (class_ != Cea608XdsClass::Channel || type != 0x04) return Optional<uint16_t>();
        // Per §9.5.3.4 Table 35: 4 informational bytes carry the 16-bit
        // TSID as four 4-bit binary nibbles in bits b3..b0 of each byte,
        // low-order nibble first.  Bits b5/b4 are reserved (zero) and
        // bit b6 is the standard XDS marker (always 1).  Wire bytes are
        // 0x40..0x4F — NOT ASCII hex.
        if (payload.size() < 4) return Optional<uint16_t>();
        const uint8_t kBit6 = 0x40;
        if (((payload[0] & kBit6) == 0) || ((payload[1] & kBit6) == 0)
            || ((payload[2] & kBit6) == 0) || ((payload[3] & kBit6) == 0)) {
                return Optional<uint16_t>();
        }
        const uint16_t v = static_cast<uint16_t>(
                (payload[0] & 0x0F) | ((payload[1] & 0x0F) << 4)
                | ((payload[2] & 0x0F) << 8) | ((payload[3] & 0x0F) << 12));
        return Optional<uint16_t>(v);
}

Optional<Cea608XdsAudioServices> Cea608XdsPacket::audioServices() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x06) {
                return Optional<Cea608XdsAudioServices>();
        }
        // §9.5.1.6 Table 24: 2 informational bytes carry the Main +
        // SAP audio program descriptors.  Each byte has bit 6 set
        // and packs language in b5..b3, type in b2..b0.
        if (payload.size() < 2) return Optional<Cea608XdsAudioServices>();
        const uint8_t mb = payload[0];
        const uint8_t sb = payload[1];
        if ((mb & 0x40) == 0 || (sb & 0x40) == 0) return Optional<Cea608XdsAudioServices>();
        Cea608XdsAudioServices out;
        out.mainLanguage = static_cast<Cea608XdsLanguage>((mb >> 3) & 0x07);
        out.mainType = static_cast<Cea608XdsMainAudioType>(mb & 0x07);
        out.sapLanguage = static_cast<Cea608XdsLanguage>((sb >> 3) & 0x07);
        out.sapType = static_cast<Cea608XdsSecondAudioType>(sb & 0x07);
        return Optional<Cea608XdsAudioServices>(out);
}

List<Cea608XdsCaptionService> Cea608XdsPacket::captionServices() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x07) {
                return List<Cea608XdsCaptionService>();
        }
        // §9.5.1.7 Table 27: each character is one service entry —
        // b6=1 marker, b5..b3 language, b2 F, b1 C, b0 T.  Spec
        // mandates 2..8 such entries; reject malformed packets that
        // fall outside that range.
        List<Cea608XdsCaptionService> out;
        for (size_t i = 0; i < payload.size(); ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue; // §9.2 padding
                if ((b & 0x40) == 0) continue; // malformed entry
                Cea608XdsCaptionService cs;
                cs.language = static_cast<Cea608XdsLanguage>((b >> 3) & 0x07);
                cs.fieldTwo = (b & 0x04) != 0;
                cs.channelTwo = (b & 0x02) != 0;
                cs.textMode = (b & 0x01) != 0;
                out.pushToBack(cs);
        }
        if (out.size() < 2 || out.size() > 8) {
                return List<Cea608XdsCaptionService>();
        }
        return out;
}

Optional<Cea608XdsTapeDelay> Cea608XdsPacket::tapeDelay() const {
        if (class_ != Cea608XdsClass::Channel || type != 0x03) return Optional<Cea608XdsTapeDelay>();
        // §9.5.3.3 Table 34: 2 bytes — minute (m5..m0) + hour
        // (h4..h0, bit 5 unused).  Standard b6=1 marker.
        if (payload.size() < 2) return Optional<Cea608XdsTapeDelay>();
        const uint8_t mb = payload[0];
        const uint8_t hb = payload[1];
        if ((mb & 0x40) == 0 || (hb & 0x40) == 0) return Optional<Cea608XdsTapeDelay>();
        Cea608XdsTapeDelay out;
        out.minutes = static_cast<uint8_t>(mb & 0x3F);
        out.hours = static_cast<uint8_t>(hb & 0x1F);
        if (out.minutes > 59 || out.hours > 23) return Optional<Cea608XdsTapeDelay>();
        return Optional<Cea608XdsTapeDelay>(out);
}

Optional<Cea608XdsImpulseCaptureId> Cea608XdsPacket::impulseCaptureId() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x02) {
                return Optional<Cea608XdsImpulseCaptureId>();
        }
        // §9.5.4.2: 6 bytes — first 4 are PID format, next 2 are
        // length minutes + length hours (Length/Time-in-Show prefix).
        if (payload.size() < 6) return Optional<Cea608XdsImpulseCaptureId>();
        const uint8_t kBit6 = 0x40;
        for (size_t i = 0; i < 6; ++i) {
                if ((payload[i] & kBit6) == 0) return Optional<Cea608XdsImpulseCaptureId>();
        }
        Cea608XdsImpulseCaptureId out;
        out.programId.minute = static_cast<uint8_t>(payload[0] & 0x3F);
        out.programId.hour = static_cast<uint8_t>(payload[1] & 0x1F);
        out.programId.date = static_cast<uint8_t>(payload[2] & 0x1F);
        out.programId.month = static_cast<uint8_t>(payload[3] & 0x0F);
        out.programId.tapeDelay = (payload[3] & 0x10) != 0;
        if (out.programId.minute > 59 || out.programId.hour > 23
            || out.programId.date == 0 || out.programId.date > 31
            || out.programId.month == 0 || out.programId.month > 12) {
                return Optional<Cea608XdsImpulseCaptureId>();
        }
        out.lengthMinutes = static_cast<uint8_t>(payload[4] & 0x3F);
        out.lengthHours = static_cast<uint8_t>(payload[5] & 0x3F);
        if (out.lengthMinutes > 59 || out.lengthHours > 23) {
                return Optional<Cea608XdsImpulseCaptureId>();
        }
        return Optional<Cea608XdsImpulseCaptureId>(out);
}

List<Cea608XdsSupplementalDataLocation> Cea608XdsPacket::supplementalDataLocations() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x03) {
                return List<Cea608XdsSupplementalDataLocation>();
        }
        // §9.5.4.3 Table 37: each character holds F (b5) + 5-bit
        // line number (b4..b0).  Standard b6=1 marker; b5 is F.
        List<Cea608XdsSupplementalDataLocation> out;
        for (size_t i = 0; i < payload.size(); ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if ((b & 0x40) == 0) continue;
                Cea608XdsSupplementalDataLocation loc;
                loc.fieldTwo = (b & 0x20) != 0;
                loc.lineNumber = static_cast<uint8_t>(b & 0x1F);
                if (loc.lineNumber < 7 || loc.lineNumber > 31) continue; // spec range
                out.pushToBack(loc);
        }
        return out;
}

Optional<uint16_t> Cea608XdsPacket::outOfBandChannel() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x40) return Optional<uint16_t>();
        // §9.5.4.5.1 Table 39: 2 bytes — low 6 bits of channel
        // (c5..c0) in byte 1; high 6 bits (c11..c6) in byte 2.
        if (payload.size() < 2) return Optional<uint16_t>();
        const uint8_t lo = payload[0];
        const uint8_t hi = payload[1];
        if ((lo & 0x40) == 0 || (hi & 0x40) == 0) return Optional<uint16_t>();
        const uint16_t v = static_cast<uint16_t>(
                (lo & 0x3F) | ((hi & 0x3F) << 6));
        return Optional<uint16_t>(v);
}

Optional<uint16_t> Cea608XdsPacket::channelMapPointer() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x41) return Optional<uint16_t>();
        // §9.5.4.5.2 Table 40: 2 bytes — low 6 bits (c5..c0) in
        // byte 1; high 4 bits (c9..c6) in low 4 bits of byte 2.
        // Bits 5 and 4 of the high byte are Reserved (zero per §9.1);
        // reject the packet when an out-of-spec encoder sets them.
        if (payload.size() < 2) return Optional<uint16_t>();
        const uint8_t lo = payload[0];
        const uint8_t hi = payload[1];
        if ((lo & 0x40) == 0 || (hi & 0x40) == 0) return Optional<uint16_t>();
        if ((hi & 0x30) != 0) return Optional<uint16_t>();
        const uint16_t v = static_cast<uint16_t>(
                (lo & 0x3F) | ((hi & 0x0F) << 6));
        return Optional<uint16_t>(v);
}

Optional<Cea608XdsChannelMapHeader> Cea608XdsPacket::channelMapHeader() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x42) {
                return Optional<Cea608XdsChannelMapHeader>();
        }
        // §9.5.4.5.3 Table 41: 4 bytes — #Chan Lo + #Chan Hi +
        // Version + null.
        if (payload.size() < 4) return Optional<Cea608XdsChannelMapHeader>();
        const uint8_t kBit6 = 0x40;
        if ((payload[0] & kBit6) == 0 || (payload[1] & kBit6) == 0
            || (payload[2] & kBit6) == 0) {
                return Optional<Cea608XdsChannelMapHeader>();
        }
        Cea608XdsChannelMapHeader out;
        out.channelCount = static_cast<uint16_t>(
                (payload[0] & 0x3F) | ((payload[1] & 0x0F) << 6));
        out.version = static_cast<uint8_t>(payload[2] & 0x3F);
        return Optional<Cea608XdsChannelMapHeader>(out);
}

Optional<Cea608XdsChannelMapPacket> Cea608XdsPacket::channelMapPacket() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x43) {
                return Optional<Cea608XdsChannelMapPacket>();
        }
        // §9.5.4.6 Table 42: 2 or 4 bytes for User/Tune channel,
        // followed by 0..6 displayable channel-ID characters.
        if (payload.size() < 2) return Optional<Cea608XdsChannelMapPacket>();
        const uint8_t kBit6 = 0x40;
        if ((payload[0] & kBit6) == 0 || (payload[1] & kBit6) == 0) {
                return Optional<Cea608XdsChannelMapPacket>();
        }
        Cea608XdsChannelMapPacket out;
        out.userChannel = static_cast<uint16_t>(
                (payload[0] & 0x3F) | ((payload[1] & 0x0F) << 6));
        out.remapped = (payload[1] & 0x20) != 0;
        size_t idx = 2;
        if (out.remapped) {
                if (payload.size() < idx + 2) return Optional<Cea608XdsChannelMapPacket>();
                if ((payload[idx] & kBit6) == 0 || (payload[idx + 1] & kBit6) == 0) {
                        return Optional<Cea608XdsChannelMapPacket>();
                }
                out.tuneChannel = static_cast<uint16_t>(
                        (payload[idx] & 0x3F) | ((payload[idx + 1] & 0x0F) << 6));
                idx += 2;
        }
        // Remaining bytes are displayable Channel-ID characters
        // (0x20..0x7F).  Up to 6 letters per spec.
        for (size_t i = idx; i < payload.size() && out.channelId.length() < 6; ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if (b < 0x20 || b > 0x7F) continue;
                const char ch = static_cast<char>(b);
                out.channelId += String(&ch, 1);
        }
        return Optional<Cea608XdsChannelMapPacket>(out);
}

Optional<Cea608XdsWrsame> Cea608XdsPacket::wrsame() const {
        if (class_ != Cea608XdsClass::PublicSvc || type != 0x01) {
                return Optional<Cea608XdsWrsame>();
        }
        // §9.5.5.1 Table 43 wire layout (per byte pair, after the
        // Start/Type pair, before End/Checksum):
        //   [0..2] event-code letters (e.g. "TOR")
        //   [3]    '-' (0x2D)
        //   [4]    'P' digit '1'..'9'
        //   [5..6] state code SS
        //   [7..9] county code CCC
        //   [10]   '-' (0x2D)
        //   [11]   NUL (0x00)
        //   [12]   '+' (0x2A)
        //   [13..14] duration count nn
        //   [15]   '-' (0x2D)
        // Full 16-byte WRSAME payload: byte 15 is the trailing '-'
        // after the duration count.  Receivers must verify the full
        // framing, not just the first 15 bytes — §9.5.5.1 Table 43.
        if (payload.size() < 16) return Optional<Cea608XdsWrsame>();
        auto isAlpha = [](uint8_t b) { return b >= 'A' && b <= 'Z'; };
        auto isDigit = [](uint8_t b) { return b >= '0' && b <= '9'; };
        for (size_t i = 0; i < 3; ++i) {
                if (!isAlpha(payload[i])) return Optional<Cea608XdsWrsame>();
        }
        if (payload[3] != '-') return Optional<Cea608XdsWrsame>();
        if (!isDigit(payload[4]) || payload[4] == '0') return Optional<Cea608XdsWrsame>();
        for (size_t i = 5; i < 10; ++i) {
                if (!isDigit(payload[i])) return Optional<Cea608XdsWrsame>();
        }
        if (payload[10] != '-') return Optional<Cea608XdsWrsame>();
        if (payload[12] != '+') return Optional<Cea608XdsWrsame>();
        if (!isDigit(payload[13]) || !isDigit(payload[14])) return Optional<Cea608XdsWrsame>();
        if (payload[15] != '-') return Optional<Cea608XdsWrsame>();
        Cea608XdsWrsame out;
        const char ev[3] = {
                static_cast<char>(payload[0]),
                static_cast<char>(payload[1]),
                static_cast<char>(payload[2]),
        };
        out.eventCode = String(ev, 3);
        out.countySlice = static_cast<uint8_t>(payload[4] - '0');
        out.stateCode = static_cast<uint8_t>((payload[5] - '0') * 10 + (payload[6] - '0'));
        out.countyCode = static_cast<uint16_t>(
                (payload[7] - '0') * 100 + (payload[8] - '0') * 10 + (payload[9] - '0'));
        out.durationQuarters = static_cast<uint8_t>(
                (payload[13] - '0') * 10 + (payload[14] - '0'));
        return Optional<Cea608XdsWrsame>(out);
}

String Cea608XdsPacket::nwsMessage() const {
        if (class_ != Cea608XdsClass::PublicSvc || type != 0x02) return String();
        // §9.5.5.2: free-text NWS warning message — pass through
        // @ref text() which strips 0x00 padding and rejects
        // non-printable bytes.
        return text();
}

String Cea608XdsPacket::programDescriptionRow() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future)
            || type < 0x10 || type > 0x17) {
                return String();
        }
        // §9.5.1.12 caps each row at 32 displayable characters.
        // text() does not enforce that cap (callers might want raw
        // bytes for other text-shaped packets); truncate here.
        String row = text();
        if (row.length() > 32) row = row.substr(0, 32);
        return row;
}

int Cea608XdsPacket::programDescriptionRowIndex() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future)
            || type < 0x10 || type > 0x17) {
                return 0;
        }
        return static_cast<int>(type - 0x10 + 1);
}

Optional<Cea608XdsCompositePacket1> Cea608XdsPacket::compositePacket1() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x0C) {
                return Optional<Cea608XdsCompositePacket1>();
        }
        // §9.5.1.10 Table 32: fixed prefix of 10 bytes
        //   [0..4]  Program Type (5 keyword bytes)
        //   [5]     Content Advisory char 1 only (per footnote 7)
        //   [6..7]  Length-(m), Length-(h)
        //   [8..9]  Time-in-show: elapsed-(m), elapsed-(h)
        //   [10..]  Title (0..22 displayable chars)
        // Total payload 10..32 bytes (even).
        if (payload.size() < 10) return Optional<Cea608XdsCompositePacket1>();
        Cea608XdsCompositePacket1 out;
        // Program type keywords — skip nulls per §9.5.1.10 "fields
        // filled with nulls when not available".
        for (size_t i = 0; i < 5; ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if (b < 0x20 || b > 0x7F) continue;
                out.programTypeKeywords.pushToBack(b);
        }
        out.contentAdvisoryByte1 = payload[5];
        const uint8_t kBit6 = 0x40;
        if ((payload[6] & kBit6) != 0 && (payload[7] & kBit6) != 0) {
                out.lengthMinutes = static_cast<uint8_t>(payload[6] & 0x3F);
                out.lengthHours = static_cast<uint8_t>(payload[7] & 0x3F);
        }
        if ((payload[8] & kBit6) != 0 && (payload[9] & kBit6) != 0) {
                out.elapsedMinutes = static_cast<uint8_t>(payload[8] & 0x3F);
                out.elapsedHours = static_cast<uint8_t>(payload[9] & 0x3F);
        }
        // Title — 0..22 displayable chars; stop at the first null
        // padding byte (the encoder pads unused title slots with
        // 0x00 to fill the even-byte cadence).  Per §9.5.1.10 the
        // packet is capped at 32 informational bytes total (10-byte
        // prefix + ≤22 title bytes); don't read past byte 32 even
        // if a non-compliant encoder somehow stuffed more in.
        const size_t titleEnd = payload.size() < 32 ? payload.size() : 32;
        for (size_t i = 10; i < titleEnd && out.title.length() < 22; ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if (b < 0x20 || b > 0x7F) continue;
                const char ch = static_cast<char>(b);
                out.title += String(&ch, 1);
        }
        return Optional<Cea608XdsCompositePacket1>(out);
}

Optional<Cea608XdsCompositePacket2> Cea608XdsPacket::compositePacket2() const {
        if ((class_ != Cea608XdsClass::Current && class_ != Cea608XdsClass::Future) || type != 0x0D) {
                return Optional<Cea608XdsCompositePacket2>();
        }
        // §9.5.1.11 Table 33: fixed prefix of 14 bytes
        //   [0..3]  Program Start Time (PID) — minute / hour / date / month
        //   [4..5]  Audio Services — Main + SAP descriptor
        //   [6..7]  Caption Services — first 2 service entries
        //   [8..11] Call Letters — 4 ASCII characters
        //   [12..13] Native Channel digits
        //   [14..] Network Name (0..18 ASCII chars)
        if (payload.size() < 14) return Optional<Cea608XdsCompositePacket2>();
        const uint8_t kBit6 = 0x40;
        // PID — every byte has bit 6 set per §9.5.1.1.
        for (size_t i = 0; i < 4; ++i) {
                if ((payload[i] & kBit6) == 0) return Optional<Cea608XdsCompositePacket2>();
        }
        Cea608XdsCompositePacket2 out;
        out.programId.minute = static_cast<uint8_t>(payload[0] & 0x3F);
        out.programId.hour = static_cast<uint8_t>(payload[1] & 0x1F);
        out.programId.date = static_cast<uint8_t>(payload[2] & 0x1F);
        out.programId.month = static_cast<uint8_t>(payload[3] & 0x0F);
        out.programId.tapeDelay = (payload[3] & 0x10) != 0;
        if (out.programId.minute > 59 || out.programId.hour > 23
            || out.programId.date == 0 || out.programId.date > 31
            || out.programId.month == 0 || out.programId.month > 12) {
                return Optional<Cea608XdsCompositePacket2>();
        }
        // Audio Services (Main + SAP).
        if ((payload[4] & kBit6) == 0 || (payload[5] & kBit6) == 0) {
                return Optional<Cea608XdsCompositePacket2>();
        }
        out.audioServices.mainLanguage = static_cast<Cea608XdsLanguage>((payload[4] >> 3) & 0x07);
        out.audioServices.mainType = static_cast<Cea608XdsMainAudioType>(payload[4] & 0x07);
        out.audioServices.sapLanguage = static_cast<Cea608XdsLanguage>((payload[5] >> 3) & 0x07);
        out.audioServices.sapType = static_cast<Cea608XdsSecondAudioType>(payload[5] & 0x07);
        // Caption Services — first 2 entries (variable list in the
        // standalone packet; fixed at 2 here).  Skip nulls per spec.
        for (size_t i = 6; i < 8; ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if ((b & kBit6) == 0) continue;
                Cea608XdsCaptionService cs;
                cs.language = static_cast<Cea608XdsLanguage>((b >> 3) & 0x07);
                cs.fieldTwo = (b & 0x04) != 0;
                cs.channelTwo = (b & 0x02) != 0;
                cs.textMode = (b & 0x01) != 0;
                out.captionServices.pushToBack(cs);
        }
        // Call Letters — 4 ASCII chars, possibly space-padded.
        for (size_t i = 8; i < 12; ++i) {
                const uint8_t b = payload[i];
                if (b < 0x20 || b > 0x7F) return Optional<Cea608XdsCompositePacket2>();
                const char ch = static_cast<char>(b);
                out.callLetters += String(&ch, 1);
        }
        while (out.callLetters.length() > 0
               && out.callLetters.charAt(out.callLetters.length() - 1).codepoint() == 0x20) {
                out.callLetters = out.callLetters.substr(0, out.callLetters.length() - 1);
        }
        // Native Channel (same rules as §9.5.3.2 6-byte form).
        const uint8_t hi = payload[12];
        const uint8_t lo = payload[13];
        if (lo >= 0x30 && lo <= 0x39) {
                int hiDigit = 0;
                bool hiValid = false;
                if (hi == 0x00) {
                        hiDigit = 0;
                        hiValid = true;
                } else if (hi >= 0x30 && hi <= 0x39) {
                        hiDigit = hi - 0x30;
                        hiValid = true;
                }
                if (hiValid) {
                        const int channel = hiDigit * 10 + (lo - 0x30);
                        if (channel >= 2 && channel <= 69) {
                                out.nativeChannel = Optional<int>(channel);
                        }
                }
        }
        // Network Name — 0..18 displayable chars.
        for (size_t i = 14; i < payload.size(); ++i) {
                const uint8_t b = payload[i];
                if (b == 0x00) continue;
                if (b < 0x20 || b > 0x7F) continue;
                const char ch = static_cast<char>(b);
                out.networkName += String(&ch, 1);
        }
        return Optional<Cea608XdsCompositePacket2>(out);
}

Optional<Cea608XdsTimeZone> Cea608XdsPacket::timeZone() const {
        if (class_ != Cea608XdsClass::Misc || type != 0x04) return Optional<Cea608XdsTimeZone>();
        // Per §9.5.4.4 Table 38: this packet contains TWO
        // informational characters — the first byte carries the
        // hour offset (h0..h4) + D bit (b5) with the standard b6=1
        // marker, and the second byte is a standard NUL (every bit
        // zero).  Require both bytes; reject when the trailing NUL
        // is missing or has unexpected bits set.
        if (payload.size() < 2) return Optional<Cea608XdsTimeZone>();
        const uint8_t b = payload[0];
        const uint8_t nul = payload[1];
        if ((b & 0x40) == 0) return Optional<Cea608XdsTimeZone>();
        if (nul != 0x00) return Optional<Cea608XdsTimeZone>();
        // Wire field is 5 bits (h4..h0, 0..31).  Per §9.5.4.4 the
        // valid range is 0..12 hours west of UTC; values 13..31 are
        // out-of-spec and we reject them.
        const uint8_t hourMag = static_cast<uint8_t>(b & 0x1F);
        if (hourMag > 12) return Optional<Cea608XdsTimeZone>();
        Cea608XdsTimeZone tz;
        tz.utcOffsetHours = static_cast<int8_t>(-hourMag); // hours WEST of UTC → negative offset
        tz.observesDst = (b & 0x20) != 0;
        return Optional<Cea608XdsTimeZone>(tz);
}

String Cea608XdsPacket::programTypeName(uint8_t keywordByte) {
        // Per CEA-608-E §9.5.1.4 Table 18.  Codes 0x20..0x7F map to
        // human-readable keywords; the table below covers the well-known
        // Basic and Optional groups.  Unknown codes return empty string.
        struct Entry {
                        uint8_t     code;
                        const char *name;
        };
        static const Entry kTable[] = {
                {0x20, "Education"},      {0x21, "Entertainment"},   {0x22, "Movie"},
                {0x23, "News"},           {0x24, "Religious"},       {0x25, "Sports"},
                {0x26, "Other"},          {0x27, "Action"},          {0x28, "Advertisement"},
                {0x29, "Animated"},       {0x2A, "Anthology"},       {0x2B, "Automobile"},
                {0x2C, "Awards"},         {0x2D, "Baseball"},        {0x2E, "Basketball"},
                {0x2F, "Bulletin"},       {0x30, "Business"},        {0x31, "Classical"},
                {0x32, "College"},        {0x33, "Combat"},          {0x34, "Comedy"},
                {0x35, "Commentary"},     {0x36, "Concert"},         {0x37, "Consumer"},
                {0x38, "Contemporary"},   {0x39, "Crime"},           {0x3A, "Dance"},
                {0x3B, "Documentary"},    {0x3C, "Drama"},           {0x3D, "Elementary"},
                {0x3E, "Erotica"},        {0x3F, "Exercise"},        {0x40, "Fantasy"},
                {0x41, "Farm"},           {0x42, "Fashion"},         {0x43, "Fiction"},
                {0x44, "Food"},           {0x45, "Football"},        {0x46, "Foreign"},
                {0x47, "Fund Raiser"},    {0x48, "Game/Quiz"},       {0x49, "Garden"},
                {0x4A, "Golf"},           {0x4B, "Government"},      {0x4C, "Health"},
                {0x4D, "High School"},    {0x4E, "History"},         {0x4F, "Hobby"},
                {0x50, "Hockey"},         {0x51, "Home"},            {0x52, "Horror"},
                {0x53, "Information"},    {0x54, "Instruction"},     {0x55, "International"},
                {0x56, "Interview"},      {0x57, "Language"},        {0x58, "Legal"},
                {0x59, "Live"},           {0x5A, "Local"},           {0x5B, "Math"},
                {0x5C, "Medical"},        {0x5D, "Meeting"},         {0x5E, "Military"},
                {0x5F, "Miniseries"},     {0x60, "Music"},           {0x61, "Mystery"},
                {0x62, "National"},       {0x63, "Nature"},          {0x64, "Police"},
                {0x65, "Politics"},       {0x66, "Premier"},         {0x67, "Prerecorded"},
                {0x68, "Product"},        {0x69, "Professional"},    {0x6A, "Public"},
                {0x6B, "Racing"},         {0x6C, "Reading"},         {0x6D, "Repair"},
                {0x6E, "Repeat"},         {0x6F, "Review"},          {0x70, "Romance"},
                {0x71, "Science"},        {0x72, "Series"},          {0x73, "Service"},
                {0x74, "Shopping"},       {0x75, "Soap Opera"},      {0x76, "Special"},
                {0x77, "Suspense"},       {0x78, "Talk"},            {0x79, "Technical"},
                {0x7A, "Tennis"},         {0x7B, "Travel"},          {0x7C, "Variety"},
                {0x7D, "Video"},          {0x7E, "Weather"},         {0x7F, "Western"},
        };
        for (const Entry &e : kTable) {
                if (e.code == keywordByte) return String(e.name);
        }
        return String();
}

// ============================================================================
// Public encoder helpers
// ============================================================================

uint8_t xdsStartByte(Cea608XdsClass cls) {
        switch (cls) {
                case Cea608XdsClass::Current:     return 0x01;
                case Cea608XdsClass::Future:      return 0x03;
                case Cea608XdsClass::Channel:     return 0x05;
                case Cea608XdsClass::Misc:        return 0x07;
                case Cea608XdsClass::PublicSvc:   return 0x09;
                case Cea608XdsClass::Reserved:    return 0x0B;
                case Cea608XdsClass::PrivateData: return 0x0D;
                case Cea608XdsClass::Unknown:     return 0x00;
        }
        return 0x00;
}

uint8_t xdsContinueByte(Cea608XdsClass cls) {
        const uint8_t start = xdsStartByte(cls);
        return start == 0 ? 0 : static_cast<uint8_t>(start + 1);
}

uint8_t xdsChecksum(uint32_t classAndType, uint32_t informational) {
        // Per §8.6.3 the End byte (0x0F) is included in the sum.  The
        // checksum is the value that makes the bottom 7 bits zero.
        const uint32_t partial = classAndType + informational + 0x0F;
        return static_cast<uint8_t>((0x80 - (partial & 0x7F)) & 0x7F);
}

List<uint8_t> Cea608XdsPacket::encode() const {
        if (class_ == Cea608XdsClass::Unknown) return List<uint8_t>();
        // Mask the type to its valid 7-bit range (XDS types are
        // 0..0x7F per §9.5).  If the caller handed us a type byte
        // with bit 7 set, log a warning and refuse — silently
        // masking the high bit would emit a *different* type on the
        // wire than the caller asked for and confuse any sanity
        // checks downstream.
        if ((type & 0x80) != 0) {
                promekiWarn(
                        "Cea608XdsPacket::encode: type 0x%02X has bit 7 set; "
                        "XDS types are 0x00..0x7F per §9.5 — refusing to encode.",
                        static_cast<unsigned>(type));
                return List<uint8_t>();
        }
        const uint8_t effectiveType = static_cast<uint8_t>(type & 0x7F);
        // §9.5.1.9: Type 0x09 (Aspect Ratio) was deprecated in 608-E.
        // We retain receive-side leniency for pre-E broadcasts but
        // refuse to emit a Reserved type for Current / Future.
        if ((class_ == Cea608XdsClass::Current || class_ == Cea608XdsClass::Future)
            && effectiveType == 0x09) {
                promekiWarn(
                        "Cea608XdsPacket::encode: Current/Future class type 0x09 "
                        "(Aspect Ratio) is Reserved in CEA-608-E §9.5.1.9 — refusing to emit.");
                return List<uint8_t>();
        }
        const uint8_t startByte = xdsStartByte(class_);
        if (startByte == 0) return List<uint8_t>();
        List<uint8_t> out;
        // Start + Type pair.
        out.pushToBack(startByte);
        out.pushToBack(effectiveType);
        // Informational payload, padded to an even count with 0x00.
        uint32_t infoSum = 0;
        for (size_t i = 0; i < payload.size(); ++i) {
                const uint8_t b = payload[i];
                out.pushToBack(b);
                infoSum += b;
        }
        if ((payload.size() % 2) != 0) {
                out.pushToBack(0x00);
                // 0x00 contributes nothing to the sum.
        }
        // End + Checksum.
        const uint32_t classAndType =
                static_cast<uint32_t>(startByte) + static_cast<uint32_t>(effectiveType);
        out.pushToBack(0x0F);
        out.pushToBack(xdsChecksum(classAndType, infoSum));
        return out;
}

// ============================================================================
// Cea608XdsExtractorImpl
// ============================================================================

struct Cea608XdsExtractorImpl {
                PROMEKI_SHARED_FINAL(Cea608XdsExtractorImpl)

                /// @brief Per-(class, type) in-flight sub-packet
                ///        accumulator.
                struct InFlight {
                                Cea608XdsClass cls = Cea608XdsClass::Unknown;
                                uint8_t        type = 0;
                                /// @brief Running 7-bit sum of every byte added to the
                                ///        packet (Start + Type + every informational
                                ///        byte).  When End / Checksum arrives the End
                                ///        byte (0x0F) + Checksum byte are added to
                                ///        complete the sum; a valid packet has the
                                ///        bottom 7 bits zero.
                                uint32_t       sum = 0;
                                /// @brief Accumulated informational payload bytes.
                                List<uint8_t>  payload;
                };

                /// @brief Up to @c MaxInFlight concurrently-accumulating
                ///        sub-packets, keyed by @ref makeKey.  Spec §8.6.5
                ///        recommends at most one level of interleaving (i.e.
                ///        2 in-flight packets); 4 here is permissive.
                Map<uint16_t, InFlight> inFlight;

                /// @brief Key of the currently-active sub-packet (the one
                ///        Informational byte pairs append to).  Set to 0
                ///        when no packet is active.
                uint16_t active = 0;
                bool     hasActive = false;

                /// @brief Validated packets pending @ref drain.
                List<Cea608XdsPacket> drained;

                /// @brief Telemetry — count of checksum-failed packets.
                uint32_t checksumFails = 0;

                /// @brief Telemetry — count of in-flight sub-packets
                ///        dropped because their payload exceeded
                ///        §8.6.6's 32-byte cap.
                uint32_t oversized = 0;
};

// ============================================================================
// Cea608XdsExtractor
// ============================================================================

Cea608XdsExtractor::Cea608XdsExtractor() : _d(SharedPtr<Cea608XdsExtractorImpl>::create()) {}

void Cea608XdsExtractor::reset() {
        auto *d = _d.modify();
        d->inFlight.clear();
        d->active = 0;
        d->hasActive = false;
        d->drained = List<Cea608XdsPacket>();
        d->checksumFails = 0;
        d->oversized = 0;
}

void Cea608XdsExtractor::processPair(uint8_t b1, uint8_t b2) {
        auto *d = _d.modify();
        // -- Caption / Text control range (b1 in 0x10..0x1F).
        // Per §8.6.7 a caption control pair *interrupts* any
        // in-flight XDS packet — the next informational pair must
        // NOT append to that packet.  Mark the slot suspended (we
        // clear hasActive but keep the in-flight buffer) so the
        // packet is only resumed by an explicit Continue for its
        // (class, type).  pushFrame normally filters this range out
        // upstream, but processPair is also a public API — preserve
        // the spec semantics for hand-rolled byte streams.
        if (b1 >= 0x10 && b1 <= 0x1F) {
                d->hasActive = false;
                return;
        }
        // -- Class Start (b1 in {0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D})
        if (Cea608::isXdsControl(b1) && (b1 & 0x01) != 0) {
                // Start codes are odd; continue codes are even.
                const Cea608XdsClass cls = classFromStartCode(b1);
                const uint16_t key = makeKey(cls, b2);
                // Spec §8.6.7: a Start pair may *interrupt* a different
                // in-flight packet (suspension) or *replace* the same
                // (class, type) packet's accumulated bytes (restart).
                // We track each (class, type) independently — a Start
                // for an already-in-flight key restarts that packet's
                // accumulation.
                Cea608XdsExtractorImpl::InFlight &inflight = d->inFlight[key];
                inflight.cls = cls;
                inflight.type = b2;
                inflight.sum = static_cast<uint32_t>(b1) + static_cast<uint32_t>(b2);
                inflight.payload = List<uint8_t>();
                d->active = key;
                d->hasActive = true;
                // Enforce the in-flight count limit by dropping the
                // least-recently-touched entry when we exceed it.  In
                // practice spec-compliant broadcasters interleave at
                // most 2 packets so the cap is rarely hit.
                while (d->inFlight.size() > static_cast<size_t>(MaxInFlight)) {
                        // Drop an entry that isn't currently active.
                        for (auto it = d->inFlight.begin(); it != d->inFlight.end(); ++it) {
                                if (it->first != d->active) {
                                        d->inFlight.remove(it->first);
                                        break;
                                }
                        }
                }
                return;
        }
        // -- Class Continue (b1 in {0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E})
        if (Cea608::isXdsControl(b1) && (b1 & 0x01) == 0) {
                const Cea608XdsClass cls = classFromContinueCode(b1);
                const uint16_t key = makeKey(cls, b2);
                // Per spec §8.6.3: "No Continue/Type control character
                // pairs are ever part of the checksum calculation."  So
                // we do NOT add b1 or b2 to the running sum here.
                // §9.2: the Type byte on a Continue pair must equal
                // the Type byte that opened the in-flight packet.
                // Three cases to handle:
                //   1. (cls, b2) names an existing in-flight slot —
                //      resume it.
                //   2. The active slot belongs to the same class but
                //      a different type — §9.2 Type-byte mismatch.
                //      Log + drop the stale slot, then drop the
                //      mismatched Continue (without a Start byte we
                //      can't validate any subsequent End/Checksum).
                //   3. No slot exists for (cls, b2) and the active
                //      slot (if any) is unrelated — silently drop
                //      the orphan Continue; this is a wire-format
                //      anomaly but not a Type-mismatch.
                auto it = d->inFlight.find(key);
                if (it != d->inFlight.end()) {
                        d->active = key;
                        d->hasActive = true;
                        return;
                }
                if (d->hasActive) {
                        auto prev = d->inFlight.find(d->active);
                        if (prev != d->inFlight.end() && prev->second.cls == cls
                            && prev->second.type != b2) {
                                promekiWarn(
                                        "Cea608XdsExtractor: Continue (class=%u, "
                                        "type=0x%02X) does not match in-flight "
                                        "type 0x%02X — abandoning the in-flight "
                                        "packet.",
                                        static_cast<unsigned>(cls),
                                        static_cast<unsigned>(b2),
                                        static_cast<unsigned>(prev->second.type));
                                d->inFlight.remove(d->active);
                        }
                }
                d->hasActive = false;
                return;
        }
        // -- End / Checksum (b1 == 0x0F)
        if (Cea608::isXdsTerminator(b1, b2)) {
                if (!d->hasActive) return;
                auto it = d->inFlight.find(d->active);
                if (it == d->inFlight.end()) {
                        d->hasActive = false;
                        return;
                }
                Cea608XdsExtractorImpl::InFlight &inflight = it->second;
                // Per §8.6.3: sum of Start + Type + all Informational +
                // End + Checksum modulo 128 must equal zero.  We've
                // been accumulating Start + Type + Informational; now
                // add End (0x0F) + Checksum (b2).
                const uint32_t finalSum =
                        inflight.sum + static_cast<uint32_t>(b1) + static_cast<uint32_t>(b2);
                if ((finalSum & 0x7F) == 0) {
                        Cea608XdsPacket pkt;
                        pkt.class_ = inflight.cls;
                        pkt.type = inflight.type;
                        pkt.payload = inflight.payload;
                        d->drained.pushToBack(pkt);
                } else {
                        ++d->checksumFails;
                        promekiDebug(
                                "Cea608XdsExtractor: checksum failure on class=%u type=0x%02X "
                                "(sum=%u, expected=0 mod 128)",
                                static_cast<unsigned>(inflight.cls),
                                static_cast<unsigned>(inflight.type),
                                static_cast<unsigned>(finalSum & 0x7F));
                }
                // Drop the in-flight slot — the next Start/Continue for
                // this (class, type) starts a fresh accumulator.
                d->inFlight.remove(d->active);
                d->hasActive = false;
                return;
        }
        // -- Informational pair (both bytes in {0x00, 0x20..0x7F})
        if (!d->hasActive) return;
        // Per §8.6.1 informational characters are 0x00 or 0x20..0x7F.
        // 0x00 is a placeholder; everything in 0x01..0x1F is forbidden
        // for informational characters (those are control codes).
        // Reject malformed pairs to avoid corrupting the sum.
        auto isInfoByte = [](uint8_t b) -> bool { return b == 0x00 || (b >= 0x20 && b <= 0x7F); };
        if (!isInfoByte(b1) || !isInfoByte(b2)) return;
        auto it = d->inFlight.find(d->active);
        if (it == d->inFlight.end()) {
                d->hasActive = false;
                return;
        }
        Cea608XdsExtractorImpl::InFlight &inflight = it->second;
        if (inflight.payload.size() + 2 > static_cast<size_t>(MaxPayloadBytes)) {
                // §8.6.6 normative cap exceeded — drop the in-flight
                // packet and bump the oversized counter so telemetry
                // surfaces the wire-format error.
                ++d->oversized;
                promekiDebug(
                        "Cea608XdsExtractor: dropping over-long sub-packet "
                        "(class=%u, type=0x%02X) — payload would exceed "
                        "§8.6.6's 32-byte cap.",
                        static_cast<unsigned>(inflight.cls),
                        static_cast<unsigned>(inflight.type));
                d->inFlight.remove(d->active);
                d->hasActive = false;
                return;
        }
        inflight.payload.pushToBack(b1);
        inflight.payload.pushToBack(b2);
        inflight.sum += static_cast<uint32_t>(b1) + static_cast<uint32_t>(b2);
}

void Cea608XdsExtractor::pushFrame(const Cea708Cdp::CcDataList &data) {
        // XDS lives in field 2 — cc_type == 1 per CEA-708-D §4.3.
        for (size_t i = 0; i < data.size(); ++i) {
                const Cea708Cdp::CcData &t = data[i];
                if (!t.valid) continue;
                if (t.type != 1) continue;
                if (!Cea608::checkOddParity(t.b1)) continue;
                if (!Cea608::checkOddParity(t.b2)) continue;
                const uint8_t b1 = Cea608::stripParity(t.b1);
                const uint8_t b2 = Cea608::stripParity(t.b2);
                // We DO forward caption / text control codes (b1 in
                // 0x10..0x1F) to processPair: per §8.6.7 those
                // interrupt an in-flight XDS packet, and the suspend
                // logic lives in processPair.  The accumulator
                // ignores anything that isn't a Class Start /
                // Continue / End / Informational pair.
                processPair(b1, b2);
        }
}

List<Cea608XdsPacket> Cea608XdsExtractor::drain() {
        auto *d = _d.modify();
        List<Cea608XdsPacket> out = d->drained;
        d->drained = List<Cea608XdsPacket>();
        return out;
}

size_t Cea608XdsExtractor::pending() const { return _d->drained.size(); }

uint32_t Cea608XdsExtractor::checksumFailures() const { return _d->checksumFails; }

uint32_t Cea608XdsExtractor::oversizedPackets() const { return _d->oversized; }

PROMEKI_NAMESPACE_END
