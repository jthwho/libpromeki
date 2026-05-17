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
 * @c Result<Variant> interfaces.
 *
 * @par Thread Safety
 * Plain value type — copies are independent.  Distinct instances may
 * be used concurrently; concurrent access to a single instance is
 * not internally synchronised.
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
                /// @ref Timecode.
                Timecode timeCode;

                /// @brief cc_data triples carried in the cc_data section
                /// (when @ref ccDataPresent is true).  Caller-supplied
                /// length determines the cc_count field on emit.
                CcDataList ccData;

                /// @brief Opaque bytes for sections this class does not
                /// model (ccsvcinfo and future_section).  Captured packets
                /// round-trip exactly: the parser stuffs every byte
                /// between the cc_data section and the footer here, and
                /// the builder emits them verbatim before stamping the
                /// footer.
                Buffer extraBytes;

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