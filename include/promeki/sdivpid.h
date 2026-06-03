/**
 * @file      sdivpid.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/array.h>
#include <promeki/datatype.h>
#include <promeki/enums_color.h>
#include <promeki/enums_video.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class FrameRate;
class St291Packet;
class VideoFormat;

/**
 * @brief SMPTE ST 352 Video Payload Identifier (VPID) — 4-byte payload
 *        identification code carried on every SDI signal.
 * @ingroup util
 *
 * VPID is the in-band identifier every modern SDI signal carries as
 * a SMPTE ST 291-1 V-ANC packet (DID @c 41h, SDID @c 01h) on the
 * recommended line for the interface family.  Receivers inspect the
 * four payload bytes to learn what the wire actually carries: which
 * SDI link standard, picture rate, scan type, sampling structure,
 * bit depth, image aspect ratio, and channel assignment.  @c SdiVpid
 * is the library's typed wrapper around those four bytes, with
 * field-level decode / encode helpers that map to and from
 * @ref SdiLinkStandard, @ref SdiWireFormat, @ref FrameRate, and
 * @ref VideoScanMode.
 *
 * @par Byte layout (ST 352:2013, Table 1b — default field definitions)
 *
 * Byte indices follow the spec's 1-indexed convention (byte 1 is the
 * payload identification code, byte 4 carries bit depth + channel).
 *
 * | Byte | Bits | Field                                                |
 * |------|------|------------------------------------------------------|
 * | 1    | 7    | Version identifier (1 = ST 352:2013, 0 = pre-2008)   |
 * | 1    | 6-0  | Payload format + digital interface identification    |
 * | 2    | 7    | I/P transport (0=interlaced, 1=progressive)          |
 * | 2    | 6    | I/P picture (0=interlaced, 1=progressive)            |
 * | 2    | 5-4  | Reserved                                             |
 * | 2    | 3-0  | Picture rate (see Table 2)                           |
 * | 3    | 7    | Image aspect ratio (0=4:3, 1=16:9)                   |
 * | 3    | 6-4  | Reserved                                             |
 * | 3    | 3-0  | Sampling structure identification (see Table 3)      |
 * | 4    | 7-5  | Channel assignment (0=single/ch1, 1=ch2 ... 7=ch8)   |
 * | 4    | 4-2  | Reserved                                             |
 * | 4    | 1-0  | Bit depth (0=8-bit, 1=10-bit, 2=12-bit, 3=reserved)  |
 *
 * The class exposes both the raw bytes (@ref byte1, @ref byte2,
 * @ref byte3, @ref byte4, @ref bytes) and the decoded fields — the
 * raw bytes are what flows over the wire, the decoded fields are
 * what application code typically wants.  Encoder / decoder paths
 * only know the well-defined subset of ST 352 codes the library
 * currently supports; unsupported codes (private extensions, future
 * revisions) round-trip as raw bytes but report
 * @c SdiLinkStandard::Auto / @c SdiWireFormat::Auto / an invalid
 * @ref FrameRate from the decoders.  This keeps unknown wire data
 * from silently being misinterpreted while still preserving it on
 * pass-through.
 *
 * @par Supported byte-1 payload identifiers
 *
 * Quad-link variants don't carry a distinct byte-1 code because each
 * sub-image link carries its own VPID for the @em single-link
 * payload it transports.  The encoder maps quad-link 3G standards
 * to the matching 3G Level A single-link code so the per-link VPID
 * is correct.
 *
 * | Byte 1 | Standard family                    | Reference          |
 * |--------|------------------------------------|--------------------|
 * | 81h    | SL_SD (483/576 interlaced 270Mb/s) | Annex B.1          |
 * | 84h    | SL_HD, 720-line on 1.5 Gb/s        | ST 292-1, ST 296   |
 * | 85h    | SL_HD, 1080-line on 1.5 Gb/s       | ST 292-1, ST 274   |
 * | 87h    | DL_HD                              | ST 372             |
 * | 88h    | SL_3GA, 720-line on 3 Gb/s         | ST 425-1 Level A   |
 * | 89h    | SL_3GA, 1080-line on 3 Gb/s        | ST 425-1 Level A   |
 * | 8Ah    | SL_3GB (ST 372 dual-link on 3 Gb/s)| ST 425-1 Level B   |
 * | C0h    | SL_6G, 2160-line                   | ST 2081-10 Mode 1  |
 * | C1h    | SL_6G, 1080-line (SDR or HFR)      | ST 2081-10 Modes 2/3|
 * | CEh    | SL_12G, 2160-line                  | ST 2082-10 Mode 1  |
 * | CFh    | SL_12G, 1080-line HFR              | ST 2082-10 Mode 2  |
 *
 * @par Extended byte 2 / 3 / 4 layout (ST 2081-10, ST 2082-10)
 *
 * The 6G/12G specs re-use the Reserved bits in bytes 2, 3, and 4 of
 * the default ST 352:2013 layout to carry HDR / WCG signalling:
 *
 *  - Byte 2 @c [5:4] = Transfer characteristic (SDR / HLG / PQ /
 *    Unspecified)
 *  - Byte 3 @c [6] = Sub-image horizontal sampling (1920 / 2048)
 *  - Byte 3 @c [5:4] = Colorimetry (Rec.709 / Color VANC / UHDTV /
 *    Unknown)
 *  - Byte 4 @c [4] = Luminance/colour-difference signal type
 *    (Y'CbCr=0, ICtCp=1)
 *  - Byte 4 @c [1:0] = Bit depth, extended encoding (10-bit Full
 *    Range / 10-bit / 12-bit / 12-bit Full Range)
 *  - Byte 2 @c [3:0] picture rate codes are extended to cover the
 *    HFR rates 96 / 96/1.001 / 100 / 120 / 120/1.001 (codes
 *    @c 1h and @c Ch-Fh, which ST 352:2013 marked Reserved).
 *
 * Accessors that consult these extended fields are explicitly
 * documented as such; @ref isExtendedSchema returns @c true when
 * the byte 1 code names a 6G or 12G payload so callers can decide
 * whether the extended accessors are meaningful.
 *
 * @par Storage and copy semantics
 *
 * @c SdiVpid is a Simple data object — four bytes by value.  No
 * @c PROMEKI_SHARED_FINAL, no @c ::Ptr alias, no internal CoW.
 * Copy is a memcpy; pass by value.
 *
 * @par String form
 *
 * @ref toString emits the 4 bytes in lower-case 2-digit hex
 * separated by colons: @c "85:c1:00:00".  @ref fromString accepts
 * the same shape (case-insensitive), with optional whitespace
 * between separators.  A default-constructed VPID is @c "00:00:00:00".
 */
class SdiVpid {
        public:
                PROMEKI_DATATYPE(SdiVpid, DataTypeSdiVpid, 1)

                /** @brief 4-byte fixed-size byte array. */
                using ByteArray = ::promeki::Array<uint8_t, 4>;

                // ------------------------------------------------------------
                // Well-known byte 1 payload identification codes
                // ------------------------------------------------------------

                /// @brief Default / unknown byte 1 (invalid VPID).
                static constexpr uint8_t Byte1_Unknown      = 0x00;

                // Annex C — Historical pre-2008 payload identifier codes
                // (byte 1 bit 7 = 0).  These codes are recognised by
                // @ref isValid / @ref linkStandard for decoder
                // round-trip but their byte 2 / 3 / 4 layouts differ
                // from the modern Table 1b schema — Annex C carries
                // the picture rate in byte 2 @c [3:0] (same as
                // Table 1b) but moves the scan flag to byte 3 @c [4]
                // and uses byte 4 differently.  The library's field
                // decoders apply the Table 1b layout regardless, so
                // for a legacy payload the field accessors may report
                // misleading values.  Callers seeing @ref isAnnexC
                // return @c true should consult the raw bytes and
                // decode per Annex C tables themselves.

                /// @brief Annex C.1 — ITU-R BT.601 on SMPTE ST 259 SDI
                ///        (legacy 525/625-line interlaced).
                static constexpr uint8_t Byte1_AnnexC_BT601     = 0x01;
                /// @brief Annex C.2 — ITU-R BT.1358-1 on BT.1362 SDI
                ///        (legacy 525P/625P 270 Mb/s dual-link).
                static constexpr uint8_t Byte1_AnnexC_BT1358    = 0x02;
                /// @brief Annex C.3 — SMPTE ST 347 (legacy 525/625 on
                ///        540 Mb/s SDI).
                static constexpr uint8_t Byte1_AnnexC_ST347     = 0x03;
                /// @brief Annex C.4 — SMPTE ST 274 (legacy 1125-line
                ///        on ST 292-1 HD-SDI).
                static constexpr uint8_t Byte1_AnnexC_ST274     = 0x04;
                /// @brief Annex C.5 — SMPTE ST 296 (legacy 750-line
                ///        on ST 292-1 HD-SDI).
                static constexpr uint8_t Byte1_AnnexC_ST296     = 0x05;
                /// @brief Annex C.6 — SMPTE ST 349 (legacy SD payload
                ///        mapped into ST 292-1 HD-SDI).
                static constexpr uint8_t Byte1_AnnexC_ST349     = 0x06;

                /// @brief Annex B.1 — SD-SDI 483/576 interlaced on 270 Mb/s.
                static constexpr uint8_t Byte1_SL_SD        = 0x81;
                /// @brief SMPTE ST 292-1 / ST 296 — HD-SDI 1.5 Gb/s, 720-line.
                static constexpr uint8_t Byte1_SL_HD_720    = 0x84;
                /// @brief SMPTE ST 292-1 / ST 274 — HD-SDI 1.5 Gb/s, 1080-line.
                static constexpr uint8_t Byte1_SL_HD_1080   = 0x85;
                /// @brief SMPTE ST 372 — dual-link HD-SDI on 2× 1.5 Gb/s.
                static constexpr uint8_t Byte1_DL_HD        = 0x87;
                /// @brief SMPTE ST 425-1 Level A — 3G-SDI 720-line.
                static constexpr uint8_t Byte1_SL_3GA_720   = 0x88;
                /// @brief SMPTE ST 425-1 Level A — 3G-SDI 1080-line.
                static constexpr uint8_t Byte1_SL_3GA_1080  = 0x89;
                /// @brief SMPTE ST 425-1 Level B — 3G-SDI carrying ST 372
                ///        dual-link HD on a single 3 Gb/s link.
                static constexpr uint8_t Byte1_SL_3GB       = 0x8A;
                /// @brief SMPTE ST 2081-10 Mode 1 — 6G-SDI carrying
                ///        2160-line video (UHD).
                static constexpr uint8_t Byte1_SL_6G_2160   = 0xC0;
                /// @brief SMPTE ST 2081-10 Mode 2 / Mode 3 — 6G-SDI
                ///        carrying 1080-line video (SDR or HFR).
                static constexpr uint8_t Byte1_SL_6G_1080   = 0xC1;
                /// @brief SMPTE ST 2082-10 Mode 1 — 12G-SDI carrying
                ///        2160-line video (UHD).
                static constexpr uint8_t Byte1_SL_12G_2160  = 0xCE;
                /// @brief SMPTE ST 2082-10 Mode 2 — 12G-SDI carrying
                ///        1080-line HFR video.
                static constexpr uint8_t Byte1_SL_12G_1080  = 0xCF;

                // ------------------------------------------------------------
                // Well-known byte 2 picture-rate codes ([3:0] field, Table 2)
                // ------------------------------------------------------------

                /// @brief No defined picture rate value (byte 2 = @c 0h).
                static constexpr uint8_t Rate_Unknown       = 0x0;
                /// @brief 96/1.001 = 95.90p (ST 2081-10 / 2082-10
                ///        extension; reserved in ST 352:2013).
                static constexpr uint8_t Rate_95_90         = 0x1;
                /// @brief 24/1.001 = 23.98p.
                static constexpr uint8_t Rate_23_98         = 0x2;
                /// @brief 24p.
                static constexpr uint8_t Rate_24            = 0x3;
                /// @brief 48/1.001 = 47.95p.
                static constexpr uint8_t Rate_47_95         = 0x4;
                /// @brief 25p (also field rate 50i).
                static constexpr uint8_t Rate_25            = 0x5;
                /// @brief 30/1.001 = 29.97p (also field rate 59.94i).
                static constexpr uint8_t Rate_29_97         = 0x6;
                /// @brief 30p (also field rate 60i).
                static constexpr uint8_t Rate_30            = 0x7;
                /// @brief 48p.
                static constexpr uint8_t Rate_48            = 0x8;
                /// @brief 50p.
                static constexpr uint8_t Rate_50            = 0x9;
                /// @brief 60/1.001 = 59.94p.
                static constexpr uint8_t Rate_59_94         = 0xA;
                /// @brief 60p.
                static constexpr uint8_t Rate_60            = 0xB;
                /// @brief 96p (ST 2081-10 / 2082-10 extension).
                static constexpr uint8_t Rate_96            = 0xC;
                /// @brief 100p (ST 2081-10 / 2082-10 extension).
                static constexpr uint8_t Rate_100           = 0xD;
                /// @brief 120/1.001 = 119.88p (ST 2081-10 / 2082-10
                ///        extension).
                static constexpr uint8_t Rate_119_88        = 0xE;
                /// @brief 120p (ST 2081-10 / 2082-10 extension).
                static constexpr uint8_t Rate_120           = 0xF;

                // ------------------------------------------------------------
                // Byte 3 sampling-structure codes ([3:0] field, Table 3)
                // ------------------------------------------------------------
                //
                // The well-known sampling-structure codes are modelled by
                // the @ref VpidSampling enum (its integer values are the
                // on-wire ST 352:2013 Table 3 nibbles).  Use @ref sampling
                // / @ref setSampling for the decoded structure and
                // @ref samplingCode for the raw nibble.

                // ------------------------------------------------------------
                // Well-known byte 4 bit-depth codes ([1:0] field)
                // ------------------------------------------------------------
                //
                // The meaning of the bit-depth field depends on the
                // schema named by byte 1:
                //
                //  - ST 352:2013 (SD/HD/3G payloads): 0=8-bit, 1=10-bit,
                //    2=12-bit, 3=Reserved.
                //  - ST 2081-10 / ST 2082-10 (6G/12G payloads):
                //    0=10-bit Full Range, 1=10-bit, 2=12-bit, 3=12-bit
                //    Full Range.
                //
                // The @c BitDepth_* constants below name the raw codes;
                // @ref bitDepth and @ref isFullRange interpret them
                // against the appropriate schema based on byte 1.

                /// @brief Bit-depth code @c 0h — 8-bit per sample
                ///        (ST 352:2013) or 10-bit Full Range
                ///        (ST 2081-10 / 2082-10).
                static constexpr uint8_t BitDepth_8        = 0x0;
                /// @brief Same raw code as @ref BitDepth_8 — exists
                ///        because the 6G/12G specs redefine @c 0h as
                ///        "10-bit Full Range" rather than "8-bit".
                ///        Use @ref isExtendedSchema to disambiguate.
                static constexpr uint8_t BitDepth_10_Full  = 0x0;
                /// @brief 10-bit components (both schemas).
                static constexpr uint8_t BitDepth_10       = 0x1;
                /// @brief 12-bit components (both schemas).
                static constexpr uint8_t BitDepth_12       = 0x2;
                /// @brief 12-bit Full Range (ST 2081-10 / 2082-10
                ///        1080-line modes only); Reserved in
                ///        ST 352:2013 and in 6G/12G 2160-line modes.
                static constexpr uint8_t BitDepth_12_Full  = 0x3;

                // ------------------------------------------------------------
                // ST 2081-10 / ST 2082-10 extended byte 2 transfer codes
                // (byte 2 [5:4])
                // ------------------------------------------------------------

                /// @brief Transfer characteristic SDR-TV (BT.709 /
                ///        ST 2036-1).
                static constexpr uint8_t Transfer_SDR         = 0x0;
                /// @brief Transfer characteristic HLG HDR-TV (BT.2100).
                static constexpr uint8_t Transfer_HLG         = 0x1;
                /// @brief Transfer characteristic PQ HDR-TV (BT.2100 /
                ///        ST 2084).
                static constexpr uint8_t Transfer_PQ          = 0x2;
                /// @brief Transfer characteristic unspecified —
                ///        receiver should consult @c Color VANC
                ///        packet or fall back to display default.
                static constexpr uint8_t Transfer_Unspecified = 0x3;

                // ------------------------------------------------------------
                // ST 2081-10 / ST 2082-10 extended byte 3 colorimetry
                // codes (byte 3 [5:4])
                // ------------------------------------------------------------

                /// @brief Colorimetry Rec.709 (BT.709 / ST 2036-1
                ///        conventional system colorimetry).
                static constexpr uint8_t Colorimetry_Rec709  = 0x0;
                /// @brief Colorimetry defined externally by a
                ///        Color VANC packet (ST 2048-1).
                static constexpr uint8_t Colorimetry_VANC    = 0x1;
                /// @brief Colorimetry UHDTV (BT.2020 / BT.2100).
                static constexpr uint8_t Colorimetry_UHDTV   = 0x2;
                /// @brief Colorimetry unknown.
                static constexpr uint8_t Colorimetry_Unknown = 0x3;

                // ============================================================
                // Construction
                // ============================================================

                /**
                 * @brief Default-constructs an all-zero (invalid) VPID.
                 *
                 * @ref isValid returns @c false until at least byte 1
                 * is set to a non-zero payload identifier code.
                 */
                SdiVpid() = default;

                /**
                 * @brief Constructs a VPID from four raw bytes.
                 *
                 * @param b1  Payload identification code (byte 1).
                 * @param b2  Picture rate + scan (byte 2).
                 * @param b3  Sampling structure + aspect ratio (byte 3).
                 * @param b4  Channel assignment + bit depth (byte 4).
                 */
                SdiVpid(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4);

                /**
                 * @brief Constructs a VPID from a 4-byte @ref ByteArray.
                 */
                explicit SdiVpid(const ByteArray &bytes);

                // ============================================================
                // Validity and raw accessors
                // ============================================================

                /**
                 * @brief Returns @c true when byte 1 names a known SDI
                 *        payload identification code.
                 *
                 * A default-constructed VPID (all zeros) returns
                 * @c false; the validity check only verifies that the
                 * byte 1 code is one of the @c Byte1_* constants the
                 * library recognises, not that the remaining bytes
                 * form a self-consistent payload description.  Use
                 * @ref validate to additionally check that every
                 * field's value is in the defined (non-reserved)
                 * range.
                 */
                bool isValid() const;

                /**
                 * @brief Returns @c true when byte 1 bit 7 marks the
                 *        VPID as the current (ST 352:2013) version.
                 *
                 * Per spec §5.2, byte 1 bit 7 is the version identifier:
                 * @c 1 = ST 352:2013 / current, @c 0 = pre-2008
                 * historical (Annex C codes — different bit layouts
                 * for the remaining bytes).  Pre-2008 legacy packets
                 * round-trip through @c SdiVpid as raw bytes but the
                 * library's field decoders apply the modern Table 1b
                 * layout regardless; callers seeing
                 * @ref isCurrentVersion return @c false (i.e.
                 * @ref isAnnexC return @c true) should consult the
                 * raw bytes and decode per Annex C tables themselves.
                 */
                bool isCurrentVersion() const { return (_bytes[0] & 0x80) != 0; }

                /**
                 * @brief Returns @c true when byte 1 names a known
                 *        Annex C historical (pre-2008) code.
                 *
                 * Specifically, when byte 1 matches one of the
                 * @c Byte1_AnnexC_* constants (0x01..0x06).  This is
                 * a stricter check than @c !isCurrentVersion (which
                 * accepts any byte 1 with bit 7 = 0) — it confirms
                 * the byte 1 code is a registered legacy identifier
                 * the library knows about.  See the @c Byte1_AnnexC_*
                 * doc comments for the per-code limitations of the
                 * field-level decoders.
                 */
                bool isAnnexC() const;

                /**
                 * @brief Returns @c Error::Ok when every byte field is
                 *        in the spec-defined (non-reserved) range.
                 *
                 * Checks per ST 352:2013:
                 *  - byte 1 names a known payload identifier
                 *    (@ref isValid is @c true)
                 *  - byte 2 picture-rate code is not reserved
                 *    (codes @c 1h and @c Ch-Fh are reserved per
                 *    Table 2)
                 *  - byte 3 sampling-structure code is not reserved
                 *    (codes @c Bh, @c Ch, @c Dh, @c Fh are reserved
                 *    per Table 3; @c 7h-@c Ah and @c Eh are defined)
                 *  - byte 4 bit-depth code is not reserved
                 *    (code @c 3h is reserved per §5.5.2)
                 *
                 * Reserved bits within each byte (byte 2 [5:4], byte
                 * 3 [6:4], byte 4 [4:2]) are not enforced as zero —
                 * the spec allows application-specific documents to
                 * repurpose them.  Returns
                 * @c Error::InvalidArgument otherwise.
                 */
                Error validate() const;

                /** @brief Returns byte 1 (payload identification). */
                uint8_t byte1() const { return _bytes[0]; }
                /** @brief Returns byte 2 (picture rate + scan). */
                uint8_t byte2() const { return _bytes[1]; }
                /** @brief Returns byte 3 (sampling + aspect). */
                uint8_t byte3() const { return _bytes[2]; }
                /** @brief Returns byte 4 (channel + bit depth). */
                uint8_t byte4() const { return _bytes[3]; }

                /** @brief Returns the raw 4-byte payload. */
                const ByteArray &bytes() const { return _bytes; }

                /** @brief Sets byte 1 (payload identification). */
                void setByte1(uint8_t b) { _bytes[0] = b; }
                /** @brief Sets byte 2 (picture rate + scan). */
                void setByte2(uint8_t b) { _bytes[1] = b; }
                /** @brief Sets byte 3 (sampling + aspect). */
                void setByte3(uint8_t b) { _bytes[2] = b; }
                /** @brief Sets byte 4 (channel + bit depth). */
                void setByte4(uint8_t b) { _bytes[3] = b; }

                // ============================================================
                // Decoded field accessors
                // ============================================================

                /**
                 * @brief Returns the SDI link-standard family the byte 1
                 *        payload identifier names.
                 *
                 * Returns @c SdiLinkStandard::Auto when the byte 1 code
                 * is not in the supported mapping table.  Quad-link
                 * variants (@c QL_3G_SQD, @c QL_3G_2SI) are not
                 * separately encodable — each sub-image carries its
                 * own VPID for the underlying single-link payload, so
                 * those decode as the matching 3G Level A single-link
                 * standard.
                 */
                SdiLinkStandard linkStandard() const;

                /**
                 * @brief Returns a human-readable description of the
                 *        byte 1 payload-and-interface identification code.
                 *
                 * Names the reference standard and active-line
                 * description for every code the library models (the
                 * @c Byte1_* constants), e.g.
                 * @c "SMPTE ST 425-1 - 1080-line on Level A 3 Gb/s SDI".
                 * Unregistered codes return a string pointing at the
                 * SMPTE-RA ST 352 byte 1 register, since byte 1 is an
                 * open, registry-administered namespace (ST 352:2013
                 * §5.2 / Annex A).
                 */
                String payloadDescription() const;

                /**
                 * @brief Returns the sampling+bit-depth combination as
                 *        an @ref SdiWireFormat.
                 *
                 * Combines byte 3 @c [3:0] (sampling) and byte 4
                 * @c [1:0] (bit depth) into the matching
                 * @c SdiWireFormat.  Unknown / reserved combinations
                 * return @c SdiWireFormat::Auto so callers can
                 * distinguish "wire format known" from "wire format
                 * could not be decoded".  8-bit payloads (an SDI-legal
                 * but library-unmodelled combination) return @c Auto
                 * since the @c SdiWireFormat enumerators only cover
                 * 10-bit and 12-bit on-wire payloads.
                 *
                 * Also returns @c Auto when @ref isValid is @c false
                 * (i.e. byte 1 is not a known payload code), so a
                 * default-constructed VPID doesn't claim a fabricated
                 * wire payload.
                 */
                SdiWireFormat wireFormat() const;

                /**
                 * @brief Returns the raw picture-rate code from byte 2
                 *        @c [3:0] (one of the @c Rate_* constants when
                 *        recognised).
                 */
                uint8_t pictureRateCode() const { return static_cast<uint8_t>(_bytes[1] & 0x0F); }

                /**
                 * @brief Returns the picture rate as a @ref FrameRate.
                 *
                 * Maps the byte 2 @c [3:0] code to the matching
                 * @c FrameRate registry entry per ST 352:2013 Table 2.
                 * Unknown / reserved codes return a default-constructed
                 * (invalid) @c FrameRate.
                 *
                 * @note This is always the @em frame rate (the rate of
                 *       the originating source image), never the field
                 *       rate.  An interlaced 1080i59.94 signal carries
                 *       byte 2 @c [3:0] = @c 6h (29.97) and the
                 *       @ref videoScanMode bits distinguish the scan
                 *       ordering.
                 */
                FrameRate pictureRate() const;

                /**
                 * @brief Returns @c true when byte 2 bit 7 marks the
                 *        transport as progressive.
                 *
                 * Per ST 352:2013 Table 1b default field definitions.
                 * For an SD-SDI VPID (byte 1 = @c 0x81), Annex B.1
                 * Table B.1 marks this bit Reserved — the accessor
                 * still reads the raw bit value but it has no
                 * meaningful interpretation in that schema.
                 */
                bool isProgressiveTransport() const { return (_bytes[1] & 0x80) != 0; }

                /**
                 * @brief Returns @c true when byte 2 bit 6 marks the
                 *        picture as progressive.
                 *
                 * For a PsF (progressive-segmented-frame) signal the
                 * picture bit is @c 1 while the transport bit is @c 0.
                 *
                 * For an SD-SDI VPID (byte 1 = @c 0x81), this is the
                 * only scan flag (Annex B.1 uses byte 2 bit 6 alone
                 * to mark interlaced=0 vs progressive=1, with bit 7
                 * Reserved).
                 */
                bool isProgressivePicture() const { return (_bytes[1] & 0x40) != 0; }

                /**
                 * @brief Returns @c true when byte 1 names an SD-SDI
                 *        payload (Annex B.1 schema).
                 *
                 * SD VPIDs differ from the Table 1b default layout in
                 * two ways: byte 2 bit 7 is Reserved (only bit 6
                 * carries the I/P picture flag), and byte 3 bit 6
                 * carries the horizontal-sample-count field
                 * (720=0, 960=1) which is Reserved in the default
                 * Table 1b schema.  Use @ref sdHas960Samples to read
                 * that field.
                 */
                bool isSdSchema() const { return _bytes[0] == Byte1_SL_SD; }

                /**
                 * @brief Returns @c true when byte 3 bit 6 marks the
                 *        SD signal as having 960 active luma samples
                 *        per line (vs 720).
                 *
                 * Only meaningful when @ref isSdSchema is @c true; in
                 * the ST 2081-10 / 2082-10 schemas the same bit
                 * carries the @c 1920/2048 horizontal-sample
                 * distinction; in the ST 352:2013 default Table 1b
                 * layout the bit is Reserved.  Use
                 * @ref has2048Samples for the 6G/12G interpretation.
                 */
                bool sdHas960Samples() const { return (_bytes[2] & 0x40) != 0; }

                /**
                 * @brief Returns @c true when byte 3 bit 6 marks the
                 *        sub-image as 2048 active luma samples wide
                 *        (vs 1920) in the ST 2081-10 / 2082-10
                 *        schema.
                 *
                 * Only meaningful when @ref isExtendedSchema is
                 * @c true.  The same bit is @ref sdHas960Samples for
                 * SD payloads and Reserved in the default Table 1b
                 * layout.
                 */
                bool has2048Samples() const { return (_bytes[2] & 0x40) != 0; }

                /**
                 * @brief Returns @c true when byte 3 bit 7 marks the
                 *        image as 16:9 aspect (vs 4:3).
                 */
                bool is16x9() const { return (_bytes[2] & 0x80) != 0; }

                /**
                 * @brief Returns the decoded @ref VideoScanMode for
                 *        this VPID.
                 *
                 * Translates @ref isProgressiveTransport and
                 * @ref isProgressivePicture into the canonical scan
                 * mode triple per ST 352 byte 2 bits 7-6:
                 *
                 *  - PT=1, PS=1 → @c VideoScanMode::Progressive
                 *  - PT=0, PS=1 → @c VideoScanMode::PsF
                 *  - PT=0, PS=0 → @c VideoScanMode::Interlaced
                 *  - PT=1, PS=0 → reserved per ST 352 (returns
                 *    @c VideoScanMode::Progressive)
                 */
                VideoScanMode videoScanMode() const;

                /**
                 * @brief Returns the raw sampling code from byte 3
                 *        @c [3:0] (the on-wire ST 352:2013 Table 3
                 *        nibble).
                 *
                 * See @ref sampling for the decoded @ref VpidSampling.
                 */
                uint8_t samplingCode() const { return static_cast<uint8_t>(_bytes[2] & 0x0F); }

                /**
                 * @brief Returns the decoded byte 3 @c [3:0] sampling
                 *        structure as a @ref VpidSampling.
                 *
                 * Maps the raw nibble through ST 352:2013 Table 3.
                 * Reserved / unrecognised codes (@c Bh, @c Ch, @c Dh,
                 * @c Fh) return @c VpidSampling::Unknown; use
                 * @ref samplingCode to recover the raw nibble in that
                 * case.
                 */
                VpidSampling sampling() const;

                /**
                 * @brief Writes the byte 3 @c [3:0] sampling structure
                 *        from a @ref VpidSampling.
                 *
                 * The aspect-ratio bit (byte 3 @c [7]) and the reserved
                 * bits (byte 3 @c [6:4]) are preserved.  Passing
                 * @c VpidSampling::Unknown clears the nibble to @c 0h
                 * (Y'CbCr 4:2:2) since "Unknown" has no wire encoding.
                 *
                 * @param sampling  The sampling structure to encode.
                 */
                void setSampling(const VpidSampling &sampling);

                /**
                 * @brief Returns the bit-depth code from byte 4
                 *        @c [1:0].
                 *
                 * Interpretation depends on the schema:
                 *  - ST 352:2013 (SD/HD/3G): @c BitDepth_8 /
                 *    @c BitDepth_10 / @c BitDepth_12 / Reserved.
                 *  - ST 2081-10 / 2082-10 (6G/12G): @c BitDepth_10_Full /
                 *    @c BitDepth_10 / @c BitDepth_12 / @c BitDepth_12_Full.
                 *
                 * Use @ref bitDepth and @ref isFullRange for the
                 * schema-resolved values.
                 */
                uint8_t bitDepthCode() const { return static_cast<uint8_t>(_bytes[3] & 0x03); }

                /**
                 * @brief Returns the bit depth in bits per component
                 *        (@c 8, @c 10, or @c 12), or @c 0 for the
                 *        reserved code and for an invalid VPID.
                 *
                 * Schema-aware: the byte 1 code selects the right
                 * interpretation of the byte 4 @c [1:0] field.  See
                 * @ref bitDepthCode for the raw code.
                 */
                int bitDepth() const;

                /**
                 * @brief Returns @c true when the VPID names a full-range
                 *        (extended) quantisation rather than the legal /
                 *        narrow range commonly used for SDR broadcast.
                 *
                 * Only meaningful when @ref isExtendedSchema is @c true
                 * (i.e. the VPID is a 6G/12G payload that carries the
                 * ST 2081-10 / 2082-10 bit-depth encoding).  Returns
                 * @c false for SD/HD/3G payloads since ST 352:2013 does
                 * not carry an explicit range flag.
                 */
                bool isFullRange() const;

                /**
                 * @brief Returns @c true when byte 1 names a 6G or 12G
                 *        payload — i.e. the byte 2 / 3 / 4 fields carry
                 *        the extended ST 2081-10 / 2082-10 schema
                 *        (transfer characteristic, colorimetry,
                 *        luminance signal type, extended bit-depth).
                 *
                 * Accessors that consult those fields
                 * (@ref transferCharacteristic, @ref colorimetry,
                 * @ref isIctcp, @ref isFullRange) return Unspecified /
                 * @c false defaults when this is @c false.
                 */
                bool isExtendedSchema() const;

                /**
                 * @brief Returns the raw transfer-characteristic code
                 *        from byte 2 @c [5:4] (one of @c Transfer_SDR /
                 *        @c Transfer_HLG / @c Transfer_PQ /
                 *        @c Transfer_Unspecified).
                 *
                 * Only meaningful when @ref isExtendedSchema is @c true.
                 * Per ST 352:2013 byte 2 bits 5:4 are Reserved and may
                 * carry application-specific data in that schema; the
                 * accessor returns the raw 2-bit value regardless and
                 * leaves interpretation to the caller.
                 */
                uint8_t transferCode() const { return static_cast<uint8_t>((_bytes[1] >> 4) & 0x03); }

                /**
                 * @brief Returns the decoded transfer characteristic
                 *        as a @ref TransferCharacteristics enum.
                 *
                 *  - @c Transfer_SDR         → @c TransferCharacteristics::BT709
                 *  - @c Transfer_HLG         → @c TransferCharacteristics::ARIB_STD_B67
                 *  - @c Transfer_PQ          → @c TransferCharacteristics::SMPTE2084
                 *  - @c Transfer_Unspecified → @c TransferCharacteristics::Unspecified
                 *
                 * Returns @c Unspecified when @ref isExtendedSchema is
                 * @c false (the field doesn't exist in ST 352:2013
                 * SD/HD/3G payloads).
                 */
                TransferCharacteristics transferCharacteristic() const;

                /**
                 * @brief Returns the raw colorimetry code from byte 3
                 *        @c [5:4] (one of @c Colorimetry_Rec709 /
                 *        @c Colorimetry_VANC / @c Colorimetry_UHDTV /
                 *        @c Colorimetry_Unknown).
                 *
                 * Only meaningful when @ref isExtendedSchema is @c true.
                 */
                uint8_t colorimetryCode() const { return static_cast<uint8_t>((_bytes[2] >> 4) & 0x03); }

                /**
                 * @brief Returns the decoded colorimetry as a
                 *        @ref ColorPrimaries enum.
                 *
                 *  - @c Colorimetry_Rec709  → @c ColorPrimaries::BT709
                 *  - @c Colorimetry_VANC    → @c ColorPrimaries::Unspecified
                 *    (defined externally by a Color VANC packet)
                 *  - @c Colorimetry_UHDTV   → @c ColorPrimaries::BT2020
                 *  - @c Colorimetry_Unknown → @c ColorPrimaries::Unspecified
                 *
                 * Returns @c Unspecified when @ref isExtendedSchema is
                 * @c false.
                 */
                ColorPrimaries colorimetry() const;

                /**
                 * @brief Returns @c true when byte 4 bit 4 marks the
                 *        luminance / colour-difference signal as ICtCp
                 *        (BT.2100) rather than Y'CbCr (ST 2036-1 / 274).
                 *
                 * Only meaningful when @ref isExtendedSchema is @c true.
                 * For RGB / RGBA payloads (byte 3 @c [3:0] = @c 2h or
                 * @c 6h) the spec says this bit may be ignored.
                 */
                bool isIctcp() const { return (_bytes[3] & 0x10) != 0; }

                /**
                 * @brief Returns the channel assignment from byte 4
                 *        @c [7:5] (0 = single-link or channel 1 of a
                 *        multi-channel payload; 1-7 = channels 2-8).
                 */
                uint8_t channelAssignment() const { return static_cast<uint8_t>((_bytes[3] >> 5) & 0x07); }

                /**
                 * @brief Writes the channel assignment into byte 4
                 *        @c [7:5] (low 3 bits of @p ch are kept).
                 *
                 * Used by dual-link and quad-link encoders to stamp
                 * the per-sub-image VPID with the correct sub-image
                 * index — channels 0..3 for quad-link 2SI sub-images,
                 * channels 0..1 for dual-link A/B carriers.  The
                 * existing bit-depth field (byte 4 @c [1:0]) and any
                 * reserved bits are preserved.
                 *
                 * @param ch  Channel index (0..7; high bits are masked off).
                 */
                void setChannelAssignment(uint8_t ch) {
                        _bytes[3] = static_cast<uint8_t>((_bytes[3] & 0x1F) | ((ch & 0x07) << 5));
                }

                /**
                 * @brief Writes the transfer-characteristic code into
                 *        byte 2 @c [5:4] (low 2 bits of @p code kept).
                 *
                 * Meaningful only when the byte 1 code names a 6G/12G
                 * payload (otherwise the bits are Reserved per
                 * ST 352:2013 byte 2 @c [5:4]; the setter writes them
                 * regardless to support handcrafting test packets).
                 */
                void setTransferCode(uint8_t code) {
                        _bytes[1] = static_cast<uint8_t>((_bytes[1] & 0xCF) | ((code & 0x03) << 4));
                }

                /**
                 * @brief Writes the colorimetry code into byte 3
                 *        @c [5:4] (low 2 bits of @p code kept).
                 */
                void setColorimetryCode(uint8_t code) {
                        _bytes[2] = static_cast<uint8_t>((_bytes[2] & 0xCF) | ((code & 0x03) << 4));
                }

                /**
                 * @brief Sets byte 4 bit 4 (luminance signal type:
                 *        0 = Y'CbCr, 1 = ICtCp).
                 */
                void setIctcp(bool ictcp) {
                        if (ictcp) _bytes[3] = static_cast<uint8_t>(_bytes[3] | 0x10);
                        else       _bytes[3] = static_cast<uint8_t>(_bytes[3] & ~0x10);
                }

                /**
                 * @brief Writes the bit-depth code into byte 4
                 *        @c [1:0] (low 2 bits of @p code kept).
                 */
                void setBitDepthCode(uint8_t code) {
                        _bytes[3] = static_cast<uint8_t>((_bytes[3] & 0xFC) | (code & 0x03));
                }

                // ============================================================
                // Encoders
                // ============================================================

                /**
                 * @brief Builds a best-effort VPID from a @ref VideoFormat,
                 *        @ref SdiWireFormat, and @ref SdiLinkStandard.
                 *
                 * Combines the three descriptors into a 4-byte VPID by
                 * looking up the matching byte 1, picture-rate code,
                 * scan flags, sampling code, and bit-depth code from
                 * the well-known mappings documented on @ref SdiVpid.
                 * The aspect bit defaults to 16:9 (every modern HD+
                 * SDI signal is widescreen); 4:3 callers can clear
                 * the bit via @ref setByte3 after the encode.  The
                 * channel-assignment field is left at 0
                 * (single-link / channel 1 of multi-channel); callers
                 * driving multi-channel sub-image links should set
                 * it via @ref setByte4 after the encode.
                 *
                 * Fields that cannot be encoded leave the
                 * corresponding byte at @c 0x00; the resulting
                 * @ref isValid is driven by byte 1 (so an
                 * unrecognised standard produces an invalid VPID
                 * even if the other fields were fillable).
                 *
                 * For HD-SDI single-link the encoder picks @c 84h
                 * (ST 292-1, 720-line) when the raster height is 720,
                 * and @c 85h (ST 292-1, 1080-line) otherwise.  3G
                 * Level A similarly picks @c 88h for 720-line and
                 * @c 89h for 1080-line.
                 *
                 * @param videoFormat  Raster, rate, and scan triple.
                 * @param wireFormat   Sampling + bit-depth payload.
                 * @param standard     SMPTE link standard.
                 * @return A populated @c SdiVpid.
                 */
                static SdiVpid encode(const VideoFormat &videoFormat,
                                      const SdiWireFormat &wireFormat,
                                      const SdiLinkStandard &standard);

                /**
                 * @brief Same as @ref encode but stamps the byte 4
                 *        channel-assignment field as well.
                 *
                 * Convenience for building per-sub-image VPIDs of
                 * dual-link or quad-link signals where each sub-image
                 * link advertises its own channel index.  For a
                 * quad-link 2SI signal carrying UHD: channel 0 on
                 * link 1, channel 1 on link 2, channel 2 on link 3,
                 * channel 3 on link 4.
                 *
                 * @param videoFormat   Raster, rate, and scan triple.
                 * @param wireFormat    Sampling + bit-depth payload.
                 * @param standard      SMPTE link standard.
                 * @param channelIndex  0..7 sub-image index (low
                 *                      3 bits used).
                 * @return A populated @c SdiVpid with the channel
                 *         field set.
                 */
                static SdiVpid encode(const VideoFormat &videoFormat,
                                      const SdiWireFormat &wireFormat,
                                      const SdiLinkStandard &standard,
                                      int channelIndex);

                /**
                 * @brief Encodes a picture-rate code from a @ref FrameRate.
                 *
                 * Returns @c Rate_Unknown for rates that don't match a
                 * well-known code.  Pure function — no side effects.
                 */
                static uint8_t encodePictureRateCode(const FrameRate &rate);

                // ============================================================
                // Integer conversion
                // ============================================================

                /**
                 * @brief Returns the 4 VPID bytes packed as a big-endian
                 *        32-bit value (byte 1 in MSB, byte 4 in LSB).
                 *
                 * Matches the convention SDI test instruments and
                 * monitoring tools use when they display a VPID as a
                 * single hex value.  For example, 1080p59.94 over 3G
                 * Level A 4:2:2 10-bit comes out as @c 0x89CA8001.
                 */
                uint32_t toUint32BE() const;

                /**
                 * @brief Builds a VPID from a big-endian 32-bit packed
                 *        value (byte 1 in MSB, byte 4 in LSB).
                 *
                 * Inverse of @ref toUint32BE.
                 */
                static SdiVpid fromUint32BE(uint32_t v);

                // ============================================================
                // ANC packet round-trip (SMPTE ST 291-1 carriage)
                // ============================================================

                /**
                 * @brief Returns the SMPTE ST 291-1 ANC packet that
                 *        carries this VPID on the wire.
                 *
                 * Wraps the 4 VPID bytes in a Type-1 ANC packet with
                 * DID @c 0x41, SDID @c 0x01, DataCount @c 0x04, and
                 * the computed checksum, tagged with the
                 * @ref AncFormat::Vpid logical format.  Caller is
                 * responsible for picking the VANC line number —
                 * use @ref recommendedAncLine to look up the
                 * spec-recommended placement for the surrounding
                 * @ref VideoFormat.
                 *
                 * @param line    VANC line number to stamp on the
                 *                packet's @c AncPacket::st291Line()
                 *                field.  Use @c 0 to mean
                 *                "unspecified" — backends typically
                 *                substitute their own default in
                 *                that case.
                 * @param fieldB  @c true for field 2 of an interlaced
                 *                signal (sets @c AncPacket::st291FieldB()),
                 *                @c false for field 1 or progressive.
                 * @param cBit    @c true to place the packet in the C
                 *                stream, @c false (default) for the Y
                 *                stream.  ST 352 §6 does not mandate
                 *                either; Y is the conventional choice.
                 * @return The built packet.
                 */
                St291Packet toSt291Packet(uint16_t line = 0, bool fieldB = false, bool cBit = false) const;

                /**
                 * @brief Promotes an ANC packet carrying a VPID back
                 *        to a @c SdiVpid.
                 *
                 * Validates that the packet's DID/SDID is @c 0x41/0x01
                 * and that its DataCount is exactly 4 user-data words
                 * (with the 8-bit data bytes extracted from each
                 * 10-bit word).  Returns @c Error::InvalidArgument
                 * for any other shape.  The packet's @c checksum is
                 * not re-validated here (it's already validated by
                 * the @ref St291Packet capture path); use
                 * @ref St291Packet::checksumValid to do that check
                 * separately when needed.
                 *
                 * @param pkt  Source ANC packet.
                 * @return The decoded VPID on success, or
                 *         @c Error::InvalidArgument on shape mismatch.
                 */
                static Result<SdiVpid> fromSt291Packet(const St291Packet &pkt);

                /**
                 * @brief Returns the recommended VANC line number for
                 *        a given video format and field index, per
                 *        ST 352:2013 §6.2.
                 *
                 * Lookup table:
                 *
                 * | Raster      | Scan         | Field 1 | Field 2 |
                 * |-------------|--------------|---------|---------|
                 * | 525-line SD | interlaced   |   13    |   276   |
                 * | 525-line SD | progressive  |   13    |    —    |
                 * | 625-line SD | interlaced   |    9    |   322   |
                 * | 625-line SD | progressive  |    9    |    —    |
                 * | 750-line HD | progressive  |   10    |    —    |
                 * | 1125-line   | interlaced   |   10    |   572   |
                 * | 1125-line   | PsF          |   10    |   572   |
                 * | 1125-line   | progressive  |   10    |    —    |
                 *
                 * Returns @c 0 for rasters the spec doesn't cover
                 * (UHD / 8K, custom rasters) so callers can detect
                 * "use backend default."  Callers that need a
                 * non-zero default when the spec is silent can layer
                 * their own fallback on top.
                 *
                 * @param fmt    The surrounding video format
                 *               (raster + scan mode drive the line
                 *               choice; frame rate is ignored).
                 * @param field  @c 1 (default) for field 1 / single
                 *               field of progressive,
                 *               @c 2 for field 2 of an interlaced
                 *               or PsF signal.  Other values
                 *               return @c 0.
                 * @return The recommended VANC line, or @c 0 when
                 *         the format isn't in the lookup table.
                 */
                static int recommendedAncLine(const VideoFormat &fmt, int field = 1);

                // ============================================================
                // String form, equality, DataStream
                // ============================================================

                /**
                 * @brief Returns the @c "aa:bb:cc:dd" lower-case hex form.
                 */
                String toString() const;

                /**
                 * @brief Parses the string form produced by @ref toString.
                 *
                 * Accepts colon-separated 2-digit hex bytes
                 * (case-insensitive), with optional whitespace around
                 * the colons.  Returns @c Error::InvalidArgument when
                 * the input has the wrong shape or contains non-hex
                 * digits.
                 */
                static Result<SdiVpid> fromString(const String &s);

                /** @brief Byte-wise equality. */
                bool operator==(const SdiVpid &other) const { return _bytes == other._bytes; }

                /** @brief Inequality. */
                bool operator!=(const SdiVpid &other) const { return !(*this == other); }

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 *
                 * Writes the four raw bytes in payload order.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<SdiVpid> readFromStream(DataStream &s);

        private:
                ByteArray _bytes{};
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
