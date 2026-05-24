/**
 * @file      cea708cdp.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief SMPTE 334-2 Caption Distribution Packet (CDP).
 * @ingroup proav
 *
 * Value type modelling the CEA-708 / CEA-608 caption-distribution
 * payload carried inside a SMPTE 334-2 packet on
 * @c AncTransport::St291 (DID 0x61 / SDID 0x01) and on a handful of
 * other wire transports.  The class itself is wire-format-agnostic —
 * @ref toBuffer encodes the canonical SMPTE 334-2 CDP byte layout and
 * @ref fromBuffer decodes it back, but a @c Cea708Cdp value can be
 * constructed and inspected without ever touching the wire form.
 *
 * @par CDP byte layout (SMPTE 334-2-2007)
 *
 * @code
 * Offset   Field                                Size
 * 0        cdp_identifier (= 0x9669)             2
 * 2        cdp_length (total bytes including footer) 1
 * 3        cdp_frame_rate (high nibble) | 0xF    1
 * 4        flags byte                            1
 *            bit 7  time_code_present
 *            bit 6  ccdata_present
 *            bit 5  svcinfo_present
 *            bit 4  svc_info_start
 *            bit 3  svc_info_change
 *            bit 2  svc_info_complete
 *            bit 1  caption_service_active
 *            bit 0  reserved (1)
 * 5        cdp_hdr_sequence_counter              2
 * 7..      time_code_section (optional)          5 if present
 *          cc_data_section (optional)            2 + 3 * cc_count if present
 *          ccsvcinfo_section (optional)          variable
 *          cdp_footer                            4
 *            id (= 0x74)                         1
 *            cdp_ftr_sequence_counter            2
 *            packet_checksum                     1
 * @endcode
 *
 * The current implementation models the **header**, the **time code
 * section**, the **cc_data section**, and the **footer**.  Service
 * info, future sections, and other extensions are preserved as opaque
 * bytes via @ref extraBytes so that captured packets round-trip
 * exactly even when the library does not interpret every field.
 *
 * @par CEA-608 / CEA-708 carriage
 *
 * The cc_data section holds @c cc_count triples (one byte of flags +
 * two bytes of payload).  The @c cc_type field selects the carriage:
 *
 *  - @c 0 — NTSC line 21 field 1 (CEA-608)
 *  - @c 1 — NTSC line 21 field 2 (CEA-608)
 *  - @c 2 — DTVCC channel packet data (CEA-708 continuation)
 *  - @c 3 — DTVCC channel packet start (CEA-708 service block start)
 *
 * Both line-21 streams and CEA-708 service blocks ride through the
 * same triple list; consumers walk @ref ccData and dispatch on
 * @c cc_type.
 *
 * @par Frame rate codes (SMPTE 334-2 §5.1.4)
 *
 * | Code | Frame rate |
 * |------|------------|
 * | 1    | 23.976     |
 * | 2    | 24         |
 * | 3    | 25         |
 * | 4    | 29.97      |
 * | 5    | 30         |
 * | 6    | 50         |
 * | 7    | 59.94      |
 * | 8    | 60         |
 *
 * @par Variant integration
 *
 * @c Cea708Cdp is registered as @c DataTypeCea708Cdp so that
 * parsers / builders can return it through the @ref AncTranslator
 * @c ParseResult interface.
 *
 * @par Thread Safety
 * Plain value type — copies are independent.  Distinct instances may
 * be used concurrently; concurrent access to a single instance is
 * not internally synchronised.
 *
 * @par CTA-708.1 (3D Closed Captioning Extensions) — not implemented
 * The 3D extensions defined by ANSI/CTA-708.1 R-2017 add per-window
 * 3D depth metadata to the DTVCC stream via new C3 commands and an
 * extended @c cc_data triple variant.  This class does not currently
 * produce or recognise the 3D-specific bytes; encoded CDPs emit
 * only the 2D coding layer.  3D streams that arrive on the wire are
 * still parsed correctly — the 3D-only bytes flow through as opaque
 * data in @ref extraBytes — but typed accessors for the 3D fields
 * are absent.
 *
 * @see AncTranslator, AncFormat::Cea708, St291Packet
 */
class Cea708Cdp {
        public:
                PROMEKI_DATATYPE(Cea708Cdp, DataTypeCea708Cdp, 1)

                /** @brief Magic identifier word at the start of every CDP. */
                static constexpr uint16_t Identifier = 0x9669;

                /** @brief CDP footer section ID. */
                static constexpr uint8_t FooterId = 0x74;

                /** @brief Time code section ID. */
                static constexpr uint8_t TimeCodeSectionId = 0x71;

                /** @brief cc_data section ID. */
                static constexpr uint8_t CcDataSectionId = 0x72;

                /** @brief ccsvcinfo section ID. */
                static constexpr uint8_t CcSvcInfoSectionId = 0x73;

                /**
                 * @brief One cc_data triple in the CDP payload.
                 *
                 * Modelled as the four fields the SMPTE 334-2 triple
                 * actually carries: a validity bit, a 2-bit type code,
                 * and two data bytes.  Wire encoding packs them back
                 * into a 3-byte triple at @ref toBuffer time.
                 */
                struct CcData {
                                bool    valid = true; ///< cc_valid bit.
                                uint8_t type = 0;     ///< cc_type (0..3).
                                uint8_t b1 = 0;       ///< First data byte.
                                uint8_t b2 = 0;       ///< Second data byte.

                                bool operator==(const CcData &o) const {
                                        return valid == o.valid && type == o.type && b1 == o.b1 && b2 == o.b2;
                                }
                                bool operator!=(const CcData &o) const { return !(*this == o); }
                };

                /** @brief List of cc_data triples. */
                using CcDataList = ::promeki::List<CcData>;

                /**
                 * @brief One caption-service entry in the
                 *        @c ccsvcinfo_section.
                 *
                 * Per SMPTE ST 334-2 §5.5 Table 6, each service block
                 * is 7 bytes on the wire: a 1-byte flag (reserved bit +
                 * @c csn_size + caption_service_number) followed by 6
                 * @c svc_data bytes encoded per ATSC A/65 §6.9.2 Table
                 * 6.26 (caption_service_descriptor loop entry).
                 *
                 * The fields below decode the descriptor:
                 *
                 *  - @c csnSize5Bit selects how the service number is
                 *    interpreted: @c true (csn_size = '1') → 5-bit
                 *    service number 1..31; @c false (csn_size = '0') →
                 *    6-bit service number 0..63 (with 0 reserved for
                 *    CEA-608 line-21).
                 *  - @c captionServiceNumber: the wire service number.
                 *    For @c digitalCc == false, this is 0 (line-21);
                 *    for @c digitalCc == true, it matches the
                 *    @c caption_service_number embedded in
                 *    @c svc_data_byte_4.
                 *  - @c languageCode: ISO-639.2 (B/T) three-character
                 *    language code (e.g. "eng", "spa", "fra").
                 *  - @c digitalCc: @c true when the service is DTVCC
                 *    (CEA-708); @c false when it's NTSC line-21
                 *    (CEA-608).
                 *  - @c line21Field: when @c digitalCc == false, @c true
                 *    indicates analog field 2 (= CC3 / CC4 / T3 / T4
                 *    in CEA-608 nomenclature) and @c false indicates
                 *    field 1 (= CC1 / CC2 / T1 / T2).  Ignored for
                 *    DTVCC services.
                 *  - @c easyReader: signals that the service text has
                 *    been authored for the FCC Easy Reader convention
                 *    (simplified vocabulary / phrasing).
                 *  - @c wideAspect: @c true when the service was
                 *    authored for a 16:9 display; @c false for 4:3.
                 *
                 * Service entries that the library doesn't yet decode
                 * structurally — none today, but reserved for future
                 * spec revisions — are preserved as @c extraBytes per
                 * the section's spec-mandated forward-compatible
                 * skipping behaviour (§5.7).
                 */
                struct CcSvcInfoEntry {
                                bool    csnSize5Bit = false;
                                uint8_t captionServiceNumber = 0;
                                /// @brief Three ASCII characters carrying
                                ///        the ISO-639.2 language code
                                ///        (e.g. {'e', 'n', 'g'}).
                                uint8_t languageCode[3] = {0, 0, 0};
                                bool    digitalCc = false;
                                bool    line21Field = false;
                                bool    easyReader = false;
                                bool    wideAspect = false;

                                bool operator==(const CcSvcInfoEntry &o) const {
                                        return csnSize5Bit == o.csnSize5Bit
                                               && captionServiceNumber == o.captionServiceNumber
                                               && languageCode[0] == o.languageCode[0]
                                               && languageCode[1] == o.languageCode[1]
                                               && languageCode[2] == o.languageCode[2]
                                               && digitalCc == o.digitalCc
                                               && line21Field == o.line21Field
                                               && easyReader == o.easyReader
                                               && wideAspect == o.wideAspect;
                                }
                                bool operator!=(const CcSvcInfoEntry &o) const { return !(*this == o); }
                };

                /** @brief List of caption-service info entries. */
                using CcSvcInfoEntryList = ::promeki::List<CcSvcInfoEntry>;

                // -- Header fields ----------------------------------------

                /// @brief 4-bit cdp_frame_rate code (SMPTE 334-2 §5.1.4).
                uint8_t frameRateCode = 0;

                /// @brief time_code_present flag (bit 7 of flags byte).
                bool timeCodePresent = false;

                /// @brief ccdata_present flag (bit 6 of flags byte).
                bool ccDataPresent = false;

                /// @brief svcinfo_present flag (bit 5).  Not interpreted
                /// by this class — set / cleared verbatim.
                bool svcInfoPresent = false;

                /// @brief svc_info_start flag (bit 4).
                bool svcInfoStart = false;

                /// @brief svc_info_change flag (bit 3).
                bool svcInfoChange = false;

                /// @brief svc_info_complete flag (bit 2).
                bool svcInfoComplete = false;

                /// @brief caption_service_active flag (bit 1).
                bool captionServiceActive = false;

                /// @brief cdp_hdr_sequence_counter (also stamped into the
                /// footer's cdp_ftr_sequence_counter on emit).
                uint16_t sequenceCounter = 0;

                // -- Sections ---------------------------------------------

                /// @brief Time-code section payload (when
                /// @ref timeCodePresent is true).  The hour/min/sec/frm
                /// digits and drop-frame flag round-trip through
                /// @ref Timecode.  On parse, the @ref Timecode::Mode is
                /// resolved from @ref frameRateCode (ST 334-2 §5.3
                /// Table 3) plus the wire @c drop_frame_flag bit.
                Timecode timeCode;

                /// @brief Time-code section @c tc_field_flag bit
                /// (byte 3 bit 7 of the time-code section, ST 334-2
                /// §5.3 Table 4).
                ///
                /// Used for interlaced pictures (0 = first field,
                /// 1 = second field) and HFR frame-pair labelling
                /// (>=50 Hz progressive sources).  Default @c false is
                /// correct for progressive HD where the bit has no
                /// meaning; callers with HFR / interlaced context
                /// should set this explicitly.
                bool tcFieldFlag = false;

                /// @brief cc_data triples carried in the cc_data section
                /// (when @ref ccDataPresent is true).  Caller-supplied
                /// length determines the cc_count field on emit.
                CcDataList ccData;

                /// @brief Structured caption-service-info entries from
                ///        the @c ccsvcinfo_section (SMPTE 334-2 §5.5).
                ///
                /// When @ref svcInfoPresent is true, the section is
                /// parsed into these entries on @ref fromBuffer and
                /// re-emitted by @ref toBuffer.  Per spec §4.4 a CDP
                /// stream may distribute service information across
                /// multiple packets via the @ref svcInfoStart /
                /// @ref svcInfoChange / @ref svcInfoComplete flags;
                /// receivers accumulate entries across CDPs until they
                /// see a complete set.
                CcSvcInfoEntryList ccSvcInfo;

                /// @brief Opaque bytes for sections this class does not
                /// model (future_section per §5.7 — any section ID in
                /// 0x75..0xEF).  Captured packets round-trip exactly:
                /// the parser stuffs every unrecognised section's
                /// bytes here, and the builder emits them verbatim
                /// before stamping the footer.  Recognised sections
                /// (@c time_code_section 0x71, @c ccdata_section
                /// 0x72, @c ccsvcinfo_section 0x73) are parsed into
                /// their typed fields and do not contribute to
                /// @c extraBytes.
                Buffer extraBytes;

                /// @brief Count of svcinfo entries whose entry-flag
                /// service number disagreed with @c svc_data_byte_4
                /// during the most recent @ref fromBuffer.
                ///
                /// Per SMPTE 334-2 §5.5 / ATSC A/65 §6.9.2 the two
                /// fields must agree when @c digital_cc=1; the parser
                /// keeps the entry-flag value as authoritative but
                /// tallies mismatches here so callers can detect
                /// non-compliant encoders.  Reset to 0 on each parse;
                /// not used by @ref toBuffer (the encoder always
                /// writes both fields from the same source).
                uint32_t svcInfoMismatches = 0;

                // -- Construction -----------------------------------------

                Cea708Cdp() = default;

                /**
                 * @brief Constructs a CDP carrying a single cc_data
                 *        section, no timecode, no svcinfo.
                 *
                 * Convenience overload for the common producer-side path:
                 * provide a frame-rate code and the cc_data triples; the
                 * @c ccdata_present + @c caption_service_active flags are
                 * set automatically.
                 *
                 * @param frameRateCode SMPTE 334-2 frame-rate code (1..8).
                 * @param ccData The cc_data triples to carry.
                 * @param sequenceCounter The header / footer sequence
                 *                        counter (parser-visible).
                 */
                Cea708Cdp(uint8_t frameRateCode, CcDataList ccData, uint16_t sequenceCounter = 0)
                    : frameRateCode(frameRateCode)
                    , ccDataPresent(!ccData.isEmpty())
                    , captionServiceActive(!ccData.isEmpty())
                    , sequenceCounter(sequenceCounter)
                    , ccData(std::move(ccData)) {}

                // -- Wire round-trip --------------------------------------

                /**
                 * @brief Serialises this CDP to its SMPTE 334-2 byte form.
                 *
                 * Computes the @c cdp_length, mirrors
                 * @ref sequenceCounter into the footer, and stamps the
                 * @c packet_checksum so the resulting buffer's mod-256
                 * byte sum is zero.
                 *
                 * @return A @c Buffer holding the wire bytes.
                 */
                Buffer toBuffer() const;

                /**
                 * @brief Parses a CDP from its SMPTE 334-2 byte form.
                 *
                 * Validates the @c cdp_identifier, length self-consistency,
                 * footer sequence-counter mirroring, and
                 * @c packet_checksum.  Returns
                 * @c Error::CorruptData on any structural failure.
                 *
                 * @param data Pointer to the raw bytes.
                 * @param size Number of bytes available.
                 * @return The parsed CDP on success, or the relevant error.
                 */
                static Result<Cea708Cdp> fromBuffer(const void *data, size_t size);

                /** @brief Convenience overload of @ref fromBuffer accepting a @ref Buffer. */
                static Result<Cea708Cdp> fromBuffer(const Buffer &buf);

                // -- JSON dump --------------------------------------------

                /**
                 * @brief Produces a JSON representation for inspection.
                 *
                 * Used by the @ref InspectorMediaIO ANC dump and by
                 * tooling that wants a stable, human-readable form of a
                 * decoded CDP.  Includes the header flags, the
                 * (optional) timecode digits, and the cc_data triples
                 * (as a list of @c {valid, type, b1, b2} sub-objects).
                 */
                JsonObject toJson() const;

                // -- Comparison -------------------------------------------

                /**
                 * @brief Field-wise equality.
                 *
                 * @c Buffer @ref extraBytes is compared via
                 * @c Buffer::operator==, which does identity short-circuit
                 * then byte-by-byte content compare for host-accessible
                 * impls — so freshly built CDPs with identical
                 * opaque-bytes content round-trip equal.
                 */
                bool operator==(const Cea708Cdp &o) const;

                /** @brief Inequality. */
                bool operator!=(const Cea708Cdp &o) const { return !(*this == o); }

                // -- Diagnostics ------------------------------------------

                /**
                 * @brief Returns a short human-readable summary.
                 *
                 * Reports the sequence counter, frame-rate code, flag
                 * highlights, and the cc_data triple count.  Intended
                 * for log lines, not machine consumption.
                 */
                String toString() const;

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 *
                 * Wire body: the canonical CDP byte stream (the same
                 * bytes @ref toBuffer produces) length-prefixed as a
                 * @ref Buffer.  Round-trips through @ref fromBuffer.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<Cea708Cdp> readFromStream(DataStream &s);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV