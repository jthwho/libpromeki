/**
 * @file      cea608xds.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/cea708cdp.h>
#include <promeki/datetime.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/namespace.h>
#include <promeki/optional.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief CEA-608 eXtended Data Services (XDS) class identifiers,
 *        per ANSI/CTA-608-E §9.3 Table 14.
 * @ingroup proav
 *
 * XDS packets are interleaved into the field-2 byte stream and carry
 * non-caption metadata about the program currently transmitting, the
 * channel, and other ancillary information.  Each packet begins with
 * a 2-byte (Class control, Type) pair; the Class control code's value
 * identifies both the packet's class and whether the pair is a Start
 * or Continue marker.
 *
 * Six classes are defined by the spec plus a Private Data class.  An
 * additional sentinel value @c Unknown is used in this library for
 * received packets that don't match any defined class — receivers
 * are required to tolerate unknown values per §9 future-expansion
 * provisions.
 */
enum class Cea608XdsClass : uint8_t {
        Current     = 0, ///< 0x01 Start / 0x02 Continue — current program metadata.
        Future      = 1, ///< 0x03 Start / 0x04 Continue — future program metadata.
        Channel     = 2, ///< 0x05 Start / 0x06 Continue — channel (network / station / TSID).
        Misc        = 3, ///< 0x07 Start / 0x08 Continue — time of day, local time zone, etc.
        PublicSvc   = 4, ///< 0x09 Start / 0x0A Continue — NWS WRSAME warnings + messages.
        Reserved    = 5, ///< 0x0B Start / 0x0C Continue — reserved for future spec use.
        PrivateData = 6, ///< 0x0D Start / 0x0E Continue — vendor-private channel.
        Unknown     = 7  ///< Library-only sentinel for unmapped class codes.
};

/**
 * @brief Which content-advisory rating system a Content Advisory
 *        packet carries.
 * @ingroup proav
 *
 * Selected by the (a3, a2, a1, a0) bits of the Content Advisory
 * packet per CEA-608-E §9.5.1.5 Table 19, where
 * @c a0 = char1 b3, @c a1 = char1 b4, @c a2 = char1 b5, and
 * @c a3 = char2 b3:
 *
 *  - (-,  -,  0, 0) → System 0 — MPAA picture rating.
 *  - (L,  D,  0, 1) → System 1 — US TV Parental Guidelines
 *                                (L and D are content-descriptor
 *                                bits, not selector bits).
 *  - (-,  -,  1, 0) → System 2 — MPAA picture rating (backward
 *                                compatible form per Table 19
 *                                footnote 4).  Decoded as @c Mpaa.
 *  - (0,  0,  1, 1) → System 3 — Canadian English language rating.
 *  - (0,  1,  1, 1) → System 4 — Canadian French language rating.
 *  - (1,  *,  1, 1) → Reserved — surfaced as an empty Optional.
 */
enum class Cea608XdsRatingSystem : uint8_t {
        Mpaa = 0,            ///< US movie rating (G / PG / PG-13 / R / NC-17 / X / NR).
        UsTvParental = 1,    ///< US TV Parental Guidelines (TV-Y / TV-Y7 / TV-G / …).
        CanadianEnglish = 2, ///< Canadian English-language TV rating (Children / Family / PG / 14+ / 18+).
        CanadianFrench = 3,  ///< Canadian French-language TV rating.
};

/**
 * @brief MPAA rating identifiers used in Content Advisory packets.
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.1.5 Table 20.  Numeric values match the wire bits
 * carried in @c c2 bits 0..2 when the rating system is
 * @c Cea608XdsRatingSystem::Mpaa.
 */
enum class Cea608XdsMpaaRating : uint8_t {
        NotApplicable = 0, ///< No rating present (e.g. live event).
        G = 1,
        Pg = 2,
        Pg13 = 3,
        R = 4,
        Nc17 = 5,
        X = 6,
        NotRated = 7,
};

/**
 * @brief A decoded XDS content advisory packet.
 * @ingroup proav
 *
 * Surfaces a content-advisory rating decoded from an XDS Content
 * Advisory packet (Current / Future class, Type 0x05).  See
 * CEA-608-E §9.5.1.5.  The @ref system selector identifies which
 * rating system the packet carries; the remaining fields are
 * interpreted accordingly:
 *
 *  - For @c UsTvParental: @c level encodes the age rating per
 *    Table 17 (1=TV-Y, 2=TV-Y7, 3=TV-G, 4=TV-PG, 5=TV-14, 6=TV-MA,
 *    7=None).  @c violence / @c sexual / @c language / @c dialog
 *    are the individual content-descriptor bits; @c fantasyViolence
 *    is the TV-Y7-specific "FV" bit (set in place of @c violence
 *    when @c level == 2).
 *  - For @c Mpaa: @c mpaa carries the movie rating; the other bit
 *    fields are unused.
 *  - For @c CanadianEnglish / @c CanadianFrench: @c level encodes
 *    the Canadian rating index (system-specific); the bit fields
 *    are unused.
 */
struct Cea608XdsContentAdvisory {
                /// @brief Which rating system this packet carries.
                Cea608XdsRatingSystem system = Cea608XdsRatingSystem::UsTvParental;
                /// @brief Age-level index (system-specific encoding).  Unused for MPAA.
                uint8_t level = 0;
                /// @brief MPAA rating; only meaningful when @c system == @c Mpaa.
                Cea608XdsMpaaRating mpaa = Cea608XdsMpaaRating::NotApplicable;
                bool violence = false;
                bool sexual = false;
                bool language = false;
                bool dialog = false;
                bool fantasyViolence = false;

                bool operator==(const Cea608XdsContentAdvisory &o) const {
                        return system == o.system && level == o.level && mpaa == o.mpaa
                               && violence == o.violence && sexual == o.sexual
                               && language == o.language && dialog == o.dialog
                               && fantasyViolence == o.fantasyViolence;
                }
                bool operator!=(const Cea608XdsContentAdvisory &o) const { return !(*this == o); }

                /// @brief Returns the symbolic rating name for this
                ///        advisory record, per CEA-608-E Tables 20
                ///        (MPAA), 21 (US TV), 22 (Canadian English),
                ///        23 (Canadian French).  Empty for systems
                ///        not yet decoded into symbols.
                ///
                /// Examples:
                ///  - MPAA + level 3 → "PG-13"
                ///  - US TV + level 4 → "TV-PG"
                ///  - Canadian English + level 4 → "PG"
                ///  - Canadian French + level 5 → "18 ans +"
                String ratingName() const;
};

/**
 * @brief Decoded XDS "Program Identification Number" packet
 *        (Current / Future class, Type 0x01).
 * @ingroup proav
 *
 * Carries the program's scheduled start time as wall-clock UTC fields
 * per CEA-608-E §9.5.1.1.  Unlike the Misc/Time-of-Day packet, no
 * year is transmitted — receivers infer the year from the current
 * wall clock.  The @c tapeDelay flag indicates the program was
 * pre-recorded and the time is the original-broadcast time, not
 * wall-clock.
 */
struct Cea608XdsProgramId {
                uint8_t minute = 0; ///< 0..59
                uint8_t hour = 0;   ///< 0..23
                uint8_t date = 1;   ///< Day of month, 1..31
                uint8_t month = 1;  ///< 1..12
                bool tapeDelay = false;     ///< T bit — program is tape-delayed.
                // Per CEA-608-E §9.5.1.1: "The D, L, and Z bits are
                // ignored by the decoder when processing this packet."
                // We don't expose those bits — the @c D, @c L, @c Z
                // semantics live in the Time-of-Day packet
                // (§9.5.4.1), surfaced via @ref Cea608XdsPacket::
                // timeOfDayDstFlag / timeOfDayLeapYearFlag /
                // timeOfDayZeroSecondsFlag.

                bool operator==(const Cea608XdsProgramId &o) const {
                        return minute == o.minute && hour == o.hour && date == o.date
                               && month == o.month && tapeDelay == o.tapeDelay;
                }
                bool operator!=(const Cea608XdsProgramId &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Length / Time-in-Show" packet
 *        (Current / Future class, Type 0x02).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.1.2 Table 16, the wire payload is 2, 4, or 6
 * informational bytes:
 *
 *  - 2 bytes — total length only: [Length-(m), Length-(h)].
 *  - 4 bytes — adds elapsed time in minutes/hours:
 *              [Length-(m), Length-(h), Elapsed-(m), Elapsed-(h)].
 *  - 6 bytes — adds elapsed seconds and a trailing null:
 *              [Length-(m), Length-(h), Elapsed-(m), Elapsed-(h),
 *               Elapsed-(s), Null].
 *
 * Each non-null byte carries six value bits in bits b5..b0 with the
 * standard XDS marker (b6=1).  Useful for DVR progress bars and
 * "time remaining" displays.
 */
struct Cea608XdsProgramLength {
                uint8_t lengthHours = 0;   ///< Total program length, hours (0..23).
                uint8_t lengthMinutes = 0; ///< Total program length, minutes (0..59).
                uint8_t elapsedHours = 0;     ///< Elapsed hours into the program (0..23).  Zero when @ref hasElapsedTime is false.
                uint8_t elapsedMinutes = 0;   ///< Elapsed minutes (0..59).  Zero when @ref hasElapsedTime is false.
                uint8_t elapsedSeconds = 0;   ///< Elapsed seconds (0..59).  Zero when @ref hasElapsedSeconds is false.
                bool    hasElapsedTime = false;     ///< @c true when the wire payload included elapsed min/hour.
                bool    hasElapsedSeconds = false;  ///< @c true when the wire payload included elapsed seconds.

                bool operator==(const Cea608XdsProgramLength &o) const {
                        return lengthHours == o.lengthHours && lengthMinutes == o.lengthMinutes
                               && elapsedHours == o.elapsedHours && elapsedMinutes == o.elapsedMinutes
                               && elapsedSeconds == o.elapsedSeconds
                               && hasElapsedTime == o.hasElapsedTime
                               && hasElapsedSeconds == o.hasElapsedSeconds;
                }
                bool operator!=(const Cea608XdsProgramLength &o) const { return !(*this == o); }
};

/**
 * @brief CGMS-A copy generation management state, per CEA-608-E
 *        §9.5.1.8 Table 30.
 * @ingroup proav
 *
 * Carried in bits b4..b3 of Byte 1 of a Copy and Redistribution
 * Control packet (Current / Future class, Type 0x08).
 */
enum class Cea608XdsCgmsControl : uint8_t {
        CopyFree     = 0, ///< (0, 0) — Copying is permitted without restriction.
        NoMoreCopies = 1, ///< (0, 1) — No more copies (one generation has been made).
        CopyOnce     = 2, ///< (1, 0) — One generation of copies may be made.
        CopyNever    = 3, ///< (1, 1) — No copying is permitted.
};

/**
 * @brief Analog Protection System state, per CEA-608-E §9.5.1.8
 *        Table 31.
 * @ingroup proav
 *
 * Carried in bits b2..b1 of Byte 1 of a Copy and Redistribution
 * Control packet (Current / Future class, Type 0x08).
 */
enum class Cea608XdsApsControl : uint8_t {
        Off                 = 0, ///< (0, 0) — No APS.
        PspOnly             = 1, ///< (0, 1) — PSP On; Split Burst Off.
        Psp2LineSplitBurst  = 2, ///< (1, 0) — PSP On; 2-line Split Burst On.
        Psp4LineSplitBurst  = 3, ///< (1, 1) — PSP On; 4-line Split Burst On.
};

/**
 * @brief Decoded XDS "Copy and Redistribution Control" packet
 *        (Current / Future class, Type 0x08).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.1.8 Table 29.  Two informational bytes carry
 * the CGMS-A / APS / ASB / RCD fields.  Bits b5..b1 of Byte 2 are
 * Reserved (transmitted as zero) and are not surfaced.
 *
 * @par Field semantics
 *
 *  - @ref cgms — CGMS-A copy-generation management.  See Table 30.
 *  - @ref aps  — APS analog protection scheme.  Table 31; surfaced
 *                only when @ref cgms is @c NoMoreCopies or
 *                @c CopyNever (spec note p. 46 — for @c CopyFree and
 *                @c CopyOnce the wire APS bits carry no defined
 *                meaning and are reported as an empty Optional).
 *  - @ref analogSourceBit — ASB.  CEA-608-E does not define its
 *                semantics; receivers pass through unaltered.
 *  - @ref redistributionControl — RCD.  When set, signals that
 *                technological control of consumer redistribution
 *                has been signalled by the ATSC A/65C
 *                rc_descriptor.  CEA-608-E requires only pass-through.
 */
struct Cea608XdsCgmsA {
                Cea608XdsCgmsControl         cgms = Cea608XdsCgmsControl::CopyFree;
                Optional<Cea608XdsApsControl> aps; ///< Populated only when @ref cgms restricts copying.
                bool                          analogSourceBit = false;
                bool                          redistributionControl = false;

                bool operator==(const Cea608XdsCgmsA &o) const {
                        return cgms == o.cgms && aps == o.aps
                               && analogSourceBit == o.analogSourceBit
                               && redistributionControl == o.redistributionControl;
                }
                bool operator!=(const Cea608XdsCgmsA &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Aspect Ratio" packet (Current / Future class,
 *        Type 0x09).
 * @ingroup proav
 *
 * @par Spec status: Reserved in CEA-608-E
 *
 * CEA-608-E §9.5.1.9 marks Current Class Type 0x09 as Reserved —
 * the Aspect Ratio packet definition was carried over from EIA-608-B
 * (1999) but removed in 608-E (2008+).  Pre-E broadcasts still
 * transmit this packet using the old EIA-608-B semantics, so this
 * library retains the receive-side accessor for legacy compatibility.
 * Emitting it (via the encoder injector) would be technically out of
 * spec for CEA-608-E.
 *
 * EIA-608-B layout (preserved here): @ref squeezed = @c true
 * indicates a 16:9 anamorphic program in a 4:3 frame (receivers
 * should unsqueeze); @ref startLine / @ref endLine bound the
 * active-line range as offsets from line 22.
 */
struct Cea608XdsAspectRatio {
                uint8_t startLine = 0; ///< Active-region start line (0..21 offset from line 22).
                uint8_t endLine = 0;   ///< Active-region end line (0..21 offset from line 22).
                bool    squeezed = false; ///< @c true when 16:9 anamorphic in a 4:3 frame.

                bool operator==(const Cea608XdsAspectRatio &o) const {
                        return startLine == o.startLine && endLine == o.endLine && squeezed == o.squeezed;
                }
                bool operator!=(const Cea608XdsAspectRatio &o) const { return !(*this == o); }
};

/**
 * @brief Spoken-language code carried in the Audio Services and
 *        Caption Services packets per CEA-608-E §9.5.1.6 Table 25.
 * @ingroup proav
 */
enum class Cea608XdsLanguage : uint8_t {
        Unknown = 0,
        English = 1,
        Spanish = 2,
        French  = 3,
        German  = 4,
        Italian = 5,
        Other   = 6,
        None    = 7,
};

/**
 * @brief Main-audio program type per CEA-608-E §9.5.1.6 Table 26
 *        (Main column).
 * @ingroup proav
 */
enum class Cea608XdsMainAudioType : uint8_t {
        Unknown        = 0,
        Mono           = 1,
        SimulatedStereo = 2,
        TrueStereo     = 3,
        StereoSurround = 4,
        DataService    = 5,
        Other          = 6,
        None           = 7,
};

/**
 * @brief Second-audio program (SAP) type per CEA-608-E §9.5.1.6
 *        Table 26 (Second Audio Program column).
 * @ingroup proav
 */
enum class Cea608XdsSecondAudioType : uint8_t {
        Unknown          = 0,
        Mono             = 1,
        VideoDescriptions = 2,
        NonProgramAudio   = 3,
        SpecialEffects   = 4,
        DataService      = 5,
        Other            = 6,
        None             = 7,
};

/**
 * @brief Decoded XDS "Audio Services" packet (Current / Future class,
 *        Type 0x06).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.1.6 Table 24: two informational bytes carry the
 * Main audio program (byte 1) and the Second Audio Program (byte 2).
 * Each byte holds a 3-bit language code (L2..L0) and a 3-bit type
 * field (T2..T0) with the spec's standard b6=1 marker.
 */
struct Cea608XdsAudioServices {
                Cea608XdsLanguage       mainLanguage = Cea608XdsLanguage::Unknown;
                Cea608XdsMainAudioType  mainType = Cea608XdsMainAudioType::Unknown;
                Cea608XdsLanguage       sapLanguage = Cea608XdsLanguage::Unknown;
                Cea608XdsSecondAudioType sapType = Cea608XdsSecondAudioType::Unknown;

                bool operator==(const Cea608XdsAudioServices &o) const {
                        return mainLanguage == o.mainLanguage && mainType == o.mainType
                               && sapLanguage == o.sapLanguage && sapType == o.sapType;
                }
                bool operator!=(const Cea608XdsAudioServices &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Caption Services" entry per CEA-608-E §9.5.1.7
 *        Table 27 / Table 28.
 * @ingroup proav
 *
 * Each entry's @c language is a Table 25 code; @c field /
 * @c channel / @c text bits identify the caption service slot
 * (field 1 vs field 2 × channel 1 vs channel 2 × captioning vs
 * text mode).  The packet carries 2..8 such entries — one per
 * available caption service.
 */
struct Cea608XdsCaptionService {
                Cea608XdsLanguage language = Cea608XdsLanguage::Unknown;
                bool              fieldTwo = false; ///< F bit — false = field 1, true = field 2.
                bool              channelTwo = false; ///< C bit — false = C1, true = C2.
                bool              textMode = false; ///< T bit — false = captioning, true = Text.

                bool operator==(const Cea608XdsCaptionService &o) const {
                        return language == o.language && fieldTwo == o.fieldTwo
                               && channelTwo == o.channelTwo && textMode == o.textMode;
                }
                bool operator!=(const Cea608XdsCaptionService &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Tape Delay" packet (Channel class, Type 0x03).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.3.3 Table 34: two bytes carry the local
 * station's routine tape-delay duration in hours + minutes
 * (h0..h4, m0..m5).
 */
struct Cea608XdsTapeDelay {
                uint8_t hours = 0;
                uint8_t minutes = 0;

                bool operator==(const Cea608XdsTapeDelay &o) const {
                        return hours == o.hours && minutes == o.minutes;
                }
                bool operator!=(const Cea608XdsTapeDelay &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Impulse Capture ID" packet (Misc class,
 *        Type 0x02).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.4.2: 6 informational bytes — the first 4
 * mirror @ref Cea608XdsProgramId (minute / hour / date / month +
 * T / Z bits), the next 2 mirror the first two informational
 * bytes of the Length / Time-in-Show packet (length minutes,
 * length hours).
 */
struct Cea608XdsImpulseCaptureId {
                Cea608XdsProgramId     programId;
                uint8_t                lengthMinutes = 0;
                uint8_t                lengthHours = 0;

                bool operator==(const Cea608XdsImpulseCaptureId &o) const {
                        return programId == o.programId
                               && lengthMinutes == o.lengthMinutes
                               && lengthHours == o.lengthHours;
                }
                bool operator!=(const Cea608XdsImpulseCaptureId &o) const { return !(*this == o); }
};

/**
 * @brief A single Supplemental Data Location entry per CEA-608-E
 *        §9.5.4.3 Table 37.
 * @ingroup proav
 *
 * One byte carries the F bit (false = field 1, true = field 2)
 * and a 5-bit line number (7..31).  The packet may carry multiple
 * entries — each a different VBI line where supplemental data
 * appears.
 */
struct Cea608XdsSupplementalDataLocation {
                bool    fieldTwo = false;
                uint8_t lineNumber = 0;

                bool operator==(const Cea608XdsSupplementalDataLocation &o) const {
                        return fieldTwo == o.fieldTwo && lineNumber == o.lineNumber;
                }
                bool operator!=(const Cea608XdsSupplementalDataLocation &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Channel Map Header" packet (Misc class, Type
 *        0x42) per CEA-608-E §9.5.4.5.3 Table 41.
 * @ingroup proav
 */
struct Cea608XdsChannelMapHeader {
                uint16_t channelCount = 0; ///< c0..c9 — number of channels in the map.
                uint8_t  version = 0;      ///< v0..v5 — current map version.

                bool operator==(const Cea608XdsChannelMapHeader &o) const {
                        return channelCount == o.channelCount && version == o.version;
                }
                bool operator!=(const Cea608XdsChannelMapHeader &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Channel Map" entry (Misc class, Type 0x43)
 *        per CEA-608-E §9.5.4.6 Table 42.
 * @ingroup proav
 *
 * @c userChannel is the channel number the viewer enters; when
 * @c remapped is true, the receiver should tune to @c tuneChannel
 * instead.  @c channelId carries up to 6 displayable characters of
 * call letters / network ID (may be empty when omitted).
 */
struct Cea608XdsChannelMapPacket {
                uint16_t userChannel = 0; ///< u0..u9.
                bool     remapped = false; ///< rm bit.
                uint16_t tuneChannel = 0; ///< t0..t9 — valid only when @c remapped.
                String   channelId;       ///< 0..6 displayable chars (call letters or network ID).

                bool operator==(const Cea608XdsChannelMapPacket &o) const {
                        return userChannel == o.userChannel && remapped == o.remapped
                               && tuneChannel == o.tuneChannel && channelId == o.channelId;
                }
                bool operator!=(const Cea608XdsChannelMapPacket &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "WRSAME" (National Weather Service Code)
 *        packet per CEA-608-E §9.5.5.1 Table 43.
 * @ingroup proav
 *
 * Wire payload layout (after the Start/Type pair):
 *  - 3-char event code (e.g. "TOR" = Tornado Warning) + '-'
 *  - 1-digit county slice (P, 1..9) + 2-digit state code (SS) +
 *    3-digit county code (CCC) + '-' + NUL
 *  - '+' + 2-digit duration count (in 15-minute units) + '-' +
 *    optional trailing data
 *
 * See Table 44 for the full event-code dictionary.
 */
struct Cea608XdsWrsame {
                String   eventCode;       ///< Three-letter EAS event code (e.g. "TOR").
                uint8_t  countySlice = 0; ///< "P" digit 1..9 dividing a county into 9 sections.
                uint8_t  stateCode = 0;   ///< Two-digit FIPS state code (47 C.F.R. §11.31(f)).
                uint16_t countyCode = 0;  ///< Three-digit FIPS 6-4 county code.
                uint8_t  durationQuarters = 0; ///< Two-digit count of 15-minute units (0..99).

                bool operator==(const Cea608XdsWrsame &o) const {
                        return eventCode == o.eventCode && countySlice == o.countySlice
                               && stateCode == o.stateCode && countyCode == o.countyCode
                               && durationQuarters == o.durationQuarters;
                }
                bool operator!=(const Cea608XdsWrsame &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS Composite Packet-1 (Current / Future class,
 *        Type 0x0C).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.1.10 Table 32, Composite Packet-1 carries:
 *
 *  - 5 Program Type keyword bytes (§9.5.1.4 Table 17).
 *  - 1 byte from the Content Advisory packet (the first byte
 *    only, per §9.5.1.10 footnote 7 / §9.6.2.5 — carries the
 *    MPAA selector + rating bits).
 *  - 2 bytes Length (length-minutes + length-hours, §9.5.1.2).
 *  - 2 bytes Time-in-show (elapsed-minutes + elapsed-hours).
 *  - 0..22 Title bytes (displayable characters).
 *
 * Total payload is 10..32 bytes (even).  When information is not
 * available a field is filled with null characters.
 */
struct Cea608XdsCompositePacket1 {
                /// @brief Up to 5 Program Type keyword bytes.
                ///        Pass each to @ref Cea608XdsPacket::programTypeName
                ///        for the human-readable string.
                List<uint8_t>            programTypeKeywords;
                /// @brief Raw first byte of the Content Advisory
                ///        packet (§9.5.1.5 Table 18 character 1) —
                ///        encodes the system selector + MPAA rating
                ///        bits.  Zero when unavailable.
                uint8_t                  contentAdvisoryByte1 = 0;
                /// @brief Length minutes / hours (§9.5.1.2).
                uint8_t                  lengthMinutes = 0;
                uint8_t                  lengthHours = 0;
                /// @brief Elapsed minutes / hours (§9.5.1.2).
                uint8_t                  elapsedMinutes = 0;
                uint8_t                  elapsedHours = 0;
                /// @brief Program title (0..22 displayable chars).
                String                   title;

                bool operator==(const Cea608XdsCompositePacket1 &o) const {
                        return programTypeKeywords == o.programTypeKeywords
                               && contentAdvisoryByte1 == o.contentAdvisoryByte1
                               && lengthMinutes == o.lengthMinutes
                               && lengthHours == o.lengthHours
                               && elapsedMinutes == o.elapsedMinutes
                               && elapsedHours == o.elapsedHours
                               && title == o.title;
                }
                bool operator!=(const Cea608XdsCompositePacket1 &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS Composite Packet-2 (Current / Future class,
 *        Type 0x0D).
 * @ingroup proav
 *
 * Per CEA-608-E §9.5.1.11 Table 33, Composite Packet-2 carries:
 *
 *  - 4 bytes Program Start Time / Program ID (§9.5.1.1).
 *  - 2 bytes Audio Services (§9.5.1.6 Table 24).
 *  - 2 bytes Caption Services (§9.5.1.7 Table 27 — first 2 entries).
 *  - 4 bytes Call Letters (§9.5.3.2).
 *  - 2 bytes Native Channel digits (§9.5.3.2).
 *  - 0..18 Network Name bytes (§9.5.3.1).
 *
 * Total payload is 14..32 bytes.
 */
struct Cea608XdsCompositePacket2 {
                Cea608XdsProgramId            programId;
                Cea608XdsAudioServices        audioServices;
                List<Cea608XdsCaptionService> captionServices;
                String                        callLetters;
                Optional<int>                 nativeChannel; ///< Empty when both digits are null.
                String                        networkName;

                bool operator==(const Cea608XdsCompositePacket2 &o) const {
                        return programId == o.programId
                               && audioServices == o.audioServices
                               && captionServices == o.captionServices
                               && callLetters == o.callLetters
                               && nativeChannel == o.nativeChannel
                               && networkName == o.networkName;
                }
                bool operator!=(const Cea608XdsCompositePacket2 &o) const { return !(*this == o); }
};

/**
 * @brief Decoded XDS "Local Time Zone & DST Use" packet (Misc class,
 *        Type 0x04).
 * @ingroup proav
 *
 * Carries the broadcaster's local UTC offset and DST-observance flag
 * per CEA-608-E §9.5.4.4.  Receivers can use this to convert the
 * UTC time-of-day packet to local wall-clock time without consulting
 * an external timezone database.
 */
struct Cea608XdsTimeZone {
                int8_t utcOffsetHours = 0;  ///< Signed hours offset from UTC (-12..+14).
                bool   observesDst = false; ///< @c true when DST is in effect.

                bool operator==(const Cea608XdsTimeZone &o) const {
                        return utcOffsetHours == o.utcOffsetHours && observesDst == o.observesDst;
                }
                bool operator!=(const Cea608XdsTimeZone &o) const { return !(*this == o); }
};

/**
 * @brief One decoded XDS packet — class + type + raw informational
 *        bytes — plus typed accessors for the common packets.
 * @ingroup proav
 *
 * Per ANSI/CTA-608-E §9, a complete XDS packet is a sequence of
 * 7-bit informational bytes framed by a Start/Type pair and ended by
 * an End/Checksum pair.  This struct holds the validated packet's
 * payload — Start/Type and End/Checksum bytes are consumed by the
 * extractor and not surfaced.  @c class_ + @c type identify the
 * packet semantics; @c payload carries the raw 7-bit informational
 * bytes (0x00, 0x20..0x7F) in the order received.
 *
 * @par Storage and copy semantics
 *
 * Plain value type — copies deep-copy the payload bytes.  Typical
 * packets are < 32 bytes so the cost is negligible.
 */
struct Cea608XdsPacket {
                /// @brief Packet class.  See @ref Cea608XdsClass.
                Cea608XdsClass class_ = Cea608XdsClass::Unknown;
                /// @brief Packet type byte (sub-packet identifier within the class).
                ///        Per §9.5 type 0x00 is reserved; types 0x01..0x7F are
                ///        defined per-class.  Bit 6 of @c type is the In-Band
                ///        (0) / Out-of-Band (1) flag per §9.4.
                uint8_t type = 0;
                /// @brief Raw informational bytes received (Start/Type and
                ///        End/Checksum bytes excluded).  Always an even byte
                ///        count per §9.2 ("there always be an even number of
                ///        informational characters" — odd payloads are padded
                ///        with a 0x00 null character).
                List<uint8_t> payload;

                /// @brief @c true when the @c type byte has bit 6 set,
                ///        indicating the packet describes a *different*
                ///        channel than the one carrying the signal (§9.4
                ///        Out-of-Band).
                bool isOutOfBand() const { return (type & 0x40) != 0; }

                /// @brief Returns @p payload reinterpreted as ASCII text
                ///        (each byte is a 7-bit character).  Used by
                ///        text-shaped packets like Program Name and
                ///        Network Name.  Trailing 0x00 null bytes (added
                ///        for even-byte alignment) are stripped.
                String text() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x03 (Program Name /
                ///        Title), returns the title text.  Empty otherwise.
                String programName() const;

                /// @brief Convenience: when @c class_ is @c Channel and
                ///        @c type is 0x01 (Network Name / Affiliation),
                ///        returns the network name.  Empty otherwise.
                String networkName() const;

                /// @brief Convenience: when @c class_ is @c Channel and
                ///        @c type is 0x02 (Call Letters / Station ID),
                ///        returns the call letters.  Empty otherwise.
                String callLetters() const;

                /// @brief Convenience: when @c class_ is @c Channel and
                ///        @c type is 0x02 and the wire payload is the
                ///        optional 6-byte form, returns the broadcast
                ///        native channel number (2..69) per
                ///        CEA-608-E §9.5.3.2.  Empty when the packet is
                ///        the 4-byte (call-letters-only) form, when the
                ///        digit bytes are malformed, or when the
                ///        decoded number falls outside the spec range.
                ///
                /// Per spec: "Single digit numbers may either be
                /// preceded by a zero or a standard null."  Both
                /// @c '0' (0x30) and @c NUL (0x00) are accepted as
                /// the leading byte for a single-digit channel.
                Optional<int> nativeChannel() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x01 (Time of Day, UTC), returns
                ///        the decoded wall-clock @ref DateTime.  Empty
                ///        otherwise.
                ///
                /// Wire layout per §9.5.4.1 is six informational bytes:
                /// minute (0..59), hour (0..23), day-of-month (1..31),
                /// month (1..12 + DST flag in bit 5 + leap-year flag in
                /// bit 4), day-of-week (1=Sun..7=Sat), year-1990 (year
                /// minus 1990, 0..63).  Time is UTC.  Each payload byte
                /// carries the field value in bits 0..5 (per §9.5.4.1
                /// "characters in the range 0x40..0x7F"); bit 6 is set
                /// on all six payload bytes to keep them out of the
                /// 0x00..0x1F XDS control range.
                Optional<DateTime> timeOfDay() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x01 (Time of Day), returns the
                ///        DST flag (D) carried in bit 5 of the hour
                ///        byte.  False when the packet isn't time-of-
                ///        day or the field cannot be decoded.  Per
                ///        §9.5.4.1 the D bit indicates that the
                ///        transmitted UTC has been adjusted for
                ///        daylight saving; receivers may use it to
                ///        suppress a duplicate adjustment.
                bool timeOfDayDstFlag() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x01 (Time of Day), returns the
                ///        leap-year flag (L) carried in bit 5 of the
                ///        day-of-month byte.  False otherwise.
                bool timeOfDayLeapYearFlag() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x01 (Time of Day), returns the
                ///        tape-delay flag (T) carried in bit 4 of the
                ///        month byte.  Per §9.5.4.1 a tape-delayed
                ///        program reports the original-broadcast UTC
                ///        rather than wall-clock UTC.
                bool timeOfDayTapeDelayFlag() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x01 (Time of Day), returns the
                ///        zero-seconds flag (Z) carried in bit 5 of
                ///        the month byte.  Per §9.5.4.1 the Z bit
                ///        signals "received at zero-seconds boundary"
                ///        and is intended for tight-tolerance time
                ///        sync.
                bool timeOfDayZeroSecondsFlag() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x05 (Content
                ///        Advisory), returns the decoded
                ///        @ref Cea608XdsContentAdvisory.  Empty
                ///        otherwise.  Decodes US TV Parental
                ///        Guidelines and MPAA picture ratings; the
                ///        Canadian English / French rating systems
                ///        return an empty Optional today (the wire
                ///        is still valid — callers can read raw
                ///        bytes from @ref payload).
                Optional<Cea608XdsContentAdvisory> contentAdvisory() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x01 (Program
                ///        Identification Number), returns the decoded
                ///        @ref Cea608XdsProgramId.  Empty otherwise or
                ///        on malformed payload (also empty when the
                ///        payload is the all-ones end-of-program
                ///        sentinel — use @ref isProgramIdEndOfProgramSentinel
                ///        to detect that explicitly).  Per CEA-608-E §9.5.1.1.
                Optional<Cea608XdsProgramId> programId() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x01 (Program ID)
                ///        and every payload byte is at its all-ones
                ///        sentinel value, returns @c true.  Per
                ///        CEA-608-E §9.5.1.1 last sentence: "When all
                ///        characters of this packet contain all Ones,
                ///        it indicates the end of the current program."
                bool isProgramIdEndOfProgramSentinel() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x02 (Length /
                ///        Time-in-Show), returns the decoded
                ///        @ref Cea608XdsProgramLength.  Empty otherwise
                ///        or on malformed payload.  Per CEA-608-E §9.5.1.2.
                Optional<Cea608XdsProgramLength> programLength() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x04 (Program Type),
                ///        returns the list of keyword bytes carried in
                ///        the payload.  Each byte is one keyword index
                ///        per CEA-608-E §9.5.1.4 Table 18 — pass it to
                ///        @ref programTypeName for the human-readable
                ///        keyword string.  Empty list when the packet
                ///        isn't ProgramType.
                List<uint8_t> programTypeKeywords() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x09 (Aspect Ratio),
                ///        returns the decoded @ref Cea608XdsAspectRatio.
                ///        Empty otherwise.  Per CEA-608-E §9.5.1.9.
                Optional<Cea608XdsAspectRatio> aspectRatio() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x08 (Copy and
                ///        Redistribution Control), returns the decoded
                ///        CGMS-A / APS / ASB / RCD fields.  Empty
                ///        otherwise or on malformed payload.  Per
                ///        CEA-608-E §9.5.1.8 Table 29.
                Optional<Cea608XdsCgmsA> cgmsA() const;

                /// @brief Convenience: when @c class_ is @c Channel
                ///        and @c type is 0x04 (Transmission Signal ID,
                ///        a 16-bit TSID), returns the value.  Empty
                ///        otherwise.  Per CEA-608-E §9.5.3.4.
                Optional<uint16_t> transmissionSignalId() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x04 (Local Time Zone & DST Use),
                ///        returns the decoded @ref Cea608XdsTimeZone.
                ///        Empty otherwise.  Per CEA-608-E §9.5.4.4.
                Optional<Cea608XdsTimeZone> timeZone() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x06 (Audio
                ///        Services), returns the decoded
                ///        @ref Cea608XdsAudioServices.  Empty
                ///        otherwise.  Per CEA-608-E §9.5.1.6.
                Optional<Cea608XdsAudioServices> audioServices() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x07 (Caption
                ///        Services), returns the decoded list of
                ///        caption-service entries (one per available
                ///        service, 2..8 entries).  Empty list when
                ///        the packet isn't Caption Services or the
                ///        payload is malformed.  Per CEA-608-E §9.5.1.7.
                List<Cea608XdsCaptionService> captionServices() const;

                /// @brief Convenience: when @c class_ is @c Channel and
                ///        @c type is 0x03 (Tape Delay), returns the
                ///        decoded @ref Cea608XdsTapeDelay.  Empty
                ///        otherwise.  Per CEA-608-E §9.5.3.3.
                Optional<Cea608XdsTapeDelay> tapeDelay() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x02 (Impulse Capture ID),
                ///        returns the decoded
                ///        @ref Cea608XdsImpulseCaptureId.  Empty
                ///        otherwise.  Per CEA-608-E §9.5.4.2.
                Optional<Cea608XdsImpulseCaptureId> impulseCaptureId() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x03 (Supplemental Data
                ///        Location), returns the decoded list of
                ///        location entries.  Empty list when the
                ///        packet isn't Supplemental Data Location
                ///        or the payload is malformed.  Per
                ///        CEA-608-E §9.5.4.3.
                List<Cea608XdsSupplementalDataLocation> supplementalDataLocations() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x40 (Out-of-Band Channel
                ///        Number), returns the decoded 12-bit
                ///        channel number.  Empty otherwise.  Per
                ///        CEA-608-E §9.5.4.5.1.
                Optional<uint16_t> outOfBandChannel() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x41 (Channel Map Pointer),
                ///        returns the 10-bit channel number where
                ///        the Channel Map lives.  Empty otherwise.
                ///        Per CEA-608-E §9.5.4.5.2.
                Optional<uint16_t> channelMapPointer() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x42 (Channel Map Header),
                ///        returns the decoded header.  Empty
                ///        otherwise.  Per CEA-608-E §9.5.4.5.3.
                Optional<Cea608XdsChannelMapHeader> channelMapHeader() const;

                /// @brief Convenience: when @c class_ is @c Misc and
                ///        @c type is 0x43 (Channel Map Packet),
                ///        returns the decoded entry.  Empty otherwise.
                ///        Per CEA-608-E §9.5.4.6.
                Optional<Cea608XdsChannelMapPacket> channelMapPacket() const;

                /// @brief Convenience: when @c class_ is @c PublicSvc
                ///        and @c type is 0x01 (NWS WRSAME), returns
                ///        the decoded EAS code + duration.  Empty
                ///        otherwise or on malformed payload.  Per
                ///        CEA-608-E §9.5.5.1.
                Optional<Cea608XdsWrsame> wrsame() const;

                /// @brief Convenience: when @c class_ is @c PublicSvc
                ///        and @c type is 0x02 (NWS Message), returns
                ///        the free-text warning message.  Empty
                ///        otherwise.  Per CEA-608-E §9.5.5.2.
                String nwsMessage() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is in 0x10..0x17
                ///        (Program Description Row 1..Row 8),
                ///        returns the row's 0..32 displayable
                ///        characters.  Empty otherwise.  Per
                ///        CEA-608-E §9.5.1.12.
                String programDescriptionRow() const;

                /// @brief Convenience: returns the 1-based row index
                ///        (1..8) when @c class_ is @c Current /
                ///        @c Future and @c type is 0x10..0x17, or 0
                ///        otherwise.  Lets callers tag the value
                ///        returned by @ref programDescriptionRow().
                int programDescriptionRowIndex() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x0C (Composite
                ///        Packet-1), returns the decoded composite
                ///        record.  Empty otherwise or on malformed
                ///        payload.  Per CEA-608-E §9.5.1.10.
                Optional<Cea608XdsCompositePacket1> compositePacket1() const;

                /// @brief Convenience: when @c class_ is @c Current /
                ///        @c Future and @c type is 0x0D (Composite
                ///        Packet-2), returns the decoded composite
                ///        record.  Empty otherwise or on malformed
                ///        payload.  Per CEA-608-E §9.5.1.11.
                Optional<Cea608XdsCompositePacket2> compositePacket2() const;

                /// @brief Static helper — returns the human-readable
                ///        keyword name for a Program Type byte
                ///        (CEA-608-E §9.5.1.4 Table 18).  Returns an
                ///        empty string for codes that don't correspond
                ///        to a defined keyword.
                static String programTypeName(uint8_t keywordByte);

                /// @brief Encodes this packet as a complete sequence
                ///        of byte pairs ready for field-2 emission.
                ///
                /// Produces, in order:
                ///   - Start byte (per @c class_) + Type byte
                ///   - Informational byte pairs from @c payload
                ///     (padded to even length with @c 0x00 per §9.2)
                ///   - End byte (0x0F) + Checksum byte
                ///
                /// The returned list is always an even byte count.  Each
                /// byte is a 7-bit value (0x00..0x7F); callers wanting
                /// to inject into a parity-stamped field-2 stream must
                /// add odd parity via @ref Cea608::setOddParity before
                /// pushing through the wire.
                ///
                /// Returns an empty list when @c class_ is
                /// @c Cea608XdsClass::Unknown (no wire byte exists for
                /// the sentinel) or when @c type has bit 7 set (XDS
                /// types are 0..0x7F).
                ///
                /// This emits a single self-contained packet — no
                /// interleaving with other packets.  Callers that need
                /// to interleave packets across field-2 frames can use
                /// @ref xdsStartByte, @ref xdsContinueByte, and
                /// @ref xdsChecksum to assemble fragments by hand.
                List<uint8_t> encode() const;

                bool operator==(const Cea608XdsPacket &o) const {
                        return class_ == o.class_ && type == o.type && payload == o.payload;
                }
                bool operator!=(const Cea608XdsPacket &o) const { return !(*this == o); }
};

/// @brief Returns the field-2 "class start" control byte for @p cls,
///        per CEA-608-E §9.3 Table 14.  Start bytes are odd (0x01,
///        0x03, 0x05, …, 0x0D).  Returns 0 for @c Cea608XdsClass::Unknown.
/// @ingroup proav
uint8_t xdsStartByte(Cea608XdsClass cls);

/// @brief Returns the field-2 "class continue" control byte for @p cls.
///        Continue bytes are even (0x02, 0x04, 0x06, …, 0x0E).  Returns
///        0 for @c Cea608XdsClass::Unknown.
/// @ingroup proav
uint8_t xdsContinueByte(Cea608XdsClass cls);

/// @brief Computes the §8.6.3 XDS checksum byte for an in-progress
///        packet — the value that, when appended after the End byte,
///        makes the modular sum zero.
///
/// @param classAndType  Sum of the Start byte + Type byte (these are
///                      included in the checksum per §8.6.3).
/// @param informational Sum of every Informational payload byte
///                      (Continue/Type pairs of resumed packets are
///                      **not** included per §8.6.3).
/// @return Checksum byte in 0x00..0x7F such that
///         (classAndType + informational + 0x0F + checksum) mod 128 == 0.
/// @ingroup proav
uint8_t xdsChecksum(uint32_t classAndType, uint32_t informational);

struct Cea608XdsExtractorImpl; // Pimpl — defined in cea608xds.cpp.

/**
 * @brief Stateful CEA-608 XDS extractor.
 * @ingroup proav
 *
 * Consumes the parity-stripped @c (b1, b2) byte pairs of CEA-608
 * field 2, extracts complete and checksum-validated XDS packets, and
 * surfaces them through @ref drain.
 *
 * @par Wire format
 *
 *  - Each (b1, b2) pair is consumed via @ref processPair.
 *  - When @p b1 is in @c 0x01..0x0E (the Class Start / Continue
 *    control codes), it begins (or resumes) a sub-packet.  @p b2
 *    is the Type byte.
 *  - When @p b1 is in @c 0x20..0x7F (printable ASCII), the pair
 *    is an Informational byte pair — appended to the active
 *    sub-packet's payload.
 *  - When @p b1 is @c 0x0F (End), @p b2 is the Checksum byte.
 *    The accumulated sum (including the Start/Type byte pair,
 *    every informational byte, the End byte, and the Checksum
 *    byte) is verified to be zero modulo 128 (§8.6.3).  On
 *    success the packet is added to the drain queue.
 *
 * @par Interleaving
 *
 * Per §8.6.5 the spec allows packets to interleave (one packet
 * suspended by another).  This extractor tracks up to
 * @ref MaxInFlight packets simultaneously, keyed by
 * @c (class << 8 | type).  The "active" sub-packet is the one
 * whose Start/Continue most recently fired.  A Continue pair for
 * a (class, type) that has no in-flight buffer is silently
 * dropped — the extractor never started accumulating for it.
 *
 * @par Channel filter
 *
 * XDS lives in field 2 only.  Callers must filter
 * @ref Cea708Cdp::CcData triples by @c type == 1 (field 2) before
 * pushing the pairs through @ref processPair — see @ref pushFrame
 * for the canonical CDP-level entry point.  Field-2 pairs whose
 * @c b1 is in the caption / text control-code range
 * (@c 0x10..0x1F) are caption / text channel bytes, not XDS, and
 * should also be filtered out before the extractor sees them.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker, pimpl-backed via @ref SharedPtr.  Copies share
 * the same internal state (drain queue + in-flight buffers); a
 * copy is a second handle onto the same extractor, not a fresh
 * one.  Construct one instance per decoding session.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  @ref processPair / @ref pushFrame / @ref drain
 * must be called serially.
 *
 * @see Cea608, Cea608Decoder, Cea608XdsPacket
 */
class Cea608XdsExtractor {
        public:
                /// @brief Maximum number of simultaneously-tracked
                ///        in-flight sub-packets.  Spec §8.6.5 recommends
                ///        at most one level of interleaving — i.e. two
                ///        in-flight packets — but this library accepts
                ///        up to four to tolerate non-compliant encoders.
                static constexpr int MaxInFlight = 4;

                /// @brief CEA-608-E §8.6.6 normative cap: "Each
                ///        complete packet shall have no more than 32
                ///        Informational characters."  Sub-packet
                ///        payloads exceeding this are dropped from
                ///        the in-flight buffer and counted via
                ///        @ref oversizedPackets() for telemetry.
                static constexpr int MaxPayloadBytes = 32;

                Cea608XdsExtractor();

                /// @brief Resets all in-flight state.  Drained packets
                ///        that haven't been pulled via @ref drain are
                ///        discarded.
                void reset();

                /// @brief Pushes one (b1, b2) parity-stripped byte pair
                ///        into the extractor.
                ///
                /// @p b1 values are routed as:
                ///   - @c 0x01..0x0F (odd) — Class Start control: begins
                ///     or restarts a sub-packet for that class.
                ///   - @c 0x02..0x0E (even, plus @c 0x0E) — Class
                ///     Continue control: resumes a suspended sub-packet
                ///     for that class.
                ///   - @c 0x0F — End control: validates the active
                ///     sub-packet's checksum and surfaces it on
                ///     success.
                ///   - @c 0x20..0x7F (or @c 0x00 padding) — Informational
                ///     byte pair, appended to the active sub-packet's
                ///     payload.
                ///   - Anything else (notably @c 0x10..0x1F — the
                ///     CC3/CC4 caption / T3/T4 text control range) is
                ///     consumed silently without affecting any in-flight
                ///     sub-packet.  Callers should normally filter
                ///     caption / text bytes upstream via
                ///     @ref pushFrame; this is a safety net for
                ///     hand-rolled byte streams.
                void processPair(uint8_t b1, uint8_t b2);

                /// @brief Convenience wrapper that filters @p data for
                ///        field-2 triples (@c cc_type == 1) and pushes
                ///        each parity-valid byte pair through
                ///        @ref processPair.  Mirrors
                ///        @ref Cea608Decoder::pushFrame's input shape so
                ///        callers can route the same @ref Cea708Cdp ::CcDataList
                ///        through both decoders.
                ///
                ///        Pairs whose @c b1 (after parity strip) is in
                ///        @c 0x10..0x1F (caption / text control-code
                ///        space) are skipped — those are CC3 / CC4
                ///        captions or T3 / T4 text, not XDS.
                void pushFrame(const Cea708Cdp::CcDataList &data);

                /// @brief Returns and clears all packets that have
                ///        completed (Start..End/Checksum) and passed
                ///        checksum validation since the last call to
                ///        @ref drain or construction.
                List<Cea608XdsPacket> drain();

                /// @brief Returns the count of complete validated
                ///        packets currently buffered for drain.
                size_t pending() const;

                /// @brief Returns the count of checksum-failed packets
                ///        observed since construction or the most
                ///        recent @ref reset.  Useful for telemetry —
                ///        non-zero indicates wire-format errors or
                ///        bit-flip noise on the line-21 channel.
                uint32_t checksumFailures() const;

                /// @brief Returns the count of in-flight sub-packets
                ///        dropped for exceeding the §8.6.6 32-byte
                ///        payload cap, since construction or the
                ///        most recent @ref reset.  Non-zero indicates
                ///        a non-conformant upstream encoder (or a
                ///        catastrophic loss of End / Checksum frames).
                uint32_t oversizedPackets() const;

        private:
                SharedPtr<Cea608XdsExtractorImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
