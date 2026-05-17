/**
 * @file      hdrstaticmetadata.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/contentlightlevel.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/masteringdisplay.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief CTA-861.3 / SMPTE ST 2086 static HDR metadata descriptor.
 * @ingroup proav
 *
 * Façade value type composing the three pieces a CTA-861.3
 * Dynamic-Range-and-Mastering (DRM) InfoFrame carries together:
 *
 *  - **EOTF** (transfer function): @ref TransferCharacteristics
 *    constrained on the wire to one of @c Unspecified (SDR gamma),
 *    @c SMPTE2084 (PQ / HDR10), or @c ARIB_STD_B67 (HLG).
 *  - **Mastering display color volume** (SMPTE ST 2086): the
 *    full RGB primaries + white point + min/max display luminance
 *    descriptor.  Modelled as @ref MasteringDisplay so the existing
 *    @c Metadata::MasteringDisplay key and NVENC / NVDEC SEI paths
 *    consume it directly.
 *  - **Content light level** (CTA-861.3): MaxCLL + MaxFALL,
 *    modelled as @ref ContentLightLevel.
 *
 * The framework's @ref AncTranslator yields one typed @c Variant
 * per parsed packet, so this composite is the natural typed view
 * of an HDR static descriptor — registering separate codecs for
 * @ref MasteringDisplay and @ref ContentLightLevel would silently
 * drop fields on parse because both ride in one DRM InfoFrame.
 *
 * @par CTA-861-G DRM InfoFrame body layout
 *
 * The DRM InfoFrame uses Type byte @c 0x87 and Version @c 1.  Its
 * 26-byte body for @c Static_Metadata_Descriptor_ID = 0
 * (Static Metadata Type 1) is:
 *
 * @code
 * Offset  Field                                Size  Encoding
 * 0       EOTF (3 LSBs)                         1
 *           0 = Traditional gamma — SDR
 *           1 = Traditional gamma — HDR luminance range
 *           2 = SMPTE ST 2084 (PQ / HDR10)
 *           3 = ITU-R BT.2100 HLG
 * 1       Static_Metadata_Descriptor_ID         1   0 = Type 1 layout
 * 2..3    display_primaries_x[0]   (red x)      2   uint16 LE,
 *                                                   chromaticity * 50000
 * 4..5    display_primaries_y[0]   (red y)      2
 * 6..7    display_primaries_x[1]   (green x)    2
 * 8..9    display_primaries_y[1]   (green y)    2
 * 10..11  display_primaries_x[2]   (blue x)     2
 * 12..13  display_primaries_y[2]   (blue y)     2
 * 14..15  white_point_x                         2
 * 16..17  white_point_y                         2
 * 18..19  max_display_mastering_luminance       2   uint16 LE, 1 cd/m²
 * 20..21  min_display_mastering_luminance       2   uint16 LE, 0.0001 cd/m²
 * 22..23  Maximum_Content_Light_Level           2   uint16 LE, 1 cd/m²
 * 24..25  Maximum_Frame_Average_Light_Level     2   uint16 LE, 1 cd/m²
 * @endcode
 *
 * Note the little-endian encoding within the InfoFrame body — this
 * differs from HEVC SEI @c mastering_display_colour_volume
 * (payloadType 137), which uses the same field semantics in
 * big-endian byte order.  Wire-form code for that path lives in
 * @c nvencvideoencoder.cpp / @c nvdecvideodecoder.cpp; this
 * class handles only the CTA-861-G InfoFrame body.
 *
 * @par EOTF mapping
 *
 * The wire's 3-bit EOTF field collapses several CTA-861.3 cases
 * onto the smaller @ref TransferCharacteristics enum (which
 * mirrors H.265 VUI values):
 *
 * | CTA-861.3 EOTF | @ref TransferCharacteristics |
 * |----------------|------------------------------|
 * | 0 (SDR gamma)  | @c Unspecified               |
 * | 1 (HDR gamma)  | @c Unspecified (no exact H.265 match) |
 * | 2 (PQ)         | @c SMPTE2084                 |
 * | 3 (HLG)        | @c ARIB_STD_B67              |
 *
 * On encode, anything other than @c SMPTE2084 or @c ARIB_STD_B67
 * is written as wire-EOTF 0 (the safe default).
 *
 * @par Variant integration
 *
 * Registered as @c DataTypeHdrStaticMetadata so @ref
 * AncTranslator parse / build functions return / consume it
 * through their @c Result<Variant> interfaces.
 *
 * @par Thread Safety
 * Plain value type — copies are independent.  Distinct instances
 * may be used concurrently; concurrent access to a single instance
 * is not internally synchronised.
 *
 * @see MasteringDisplay, ContentLightLevel, TransferCharacteristics,
 *      AncTranslator, AncFormat::HdrStatic2086, HdmiInfoFrame
 */
class HdrStaticMetadata {
        public:
                PROMEKI_DATATYPE(HdrStaticMetadata, DataTypeHdrStaticMetadata, 1)

                /** @brief CTA-861-G DRM InfoFrame Type byte. */
                static constexpr uint8_t InfoFrameType = 0x87;

                /** @brief CTA-861-G DRM InfoFrame Version byte. */
                static constexpr uint8_t InfoFrameVersion = 1;

                /** @brief @c Static_Metadata_Descriptor_ID for the Type-1 layout. */
                static constexpr uint8_t DescriptorIdType1 = 0;

                /**
                 * @brief CTA-861.3 / CTA-861-G wire-EOTF codes
                 *        (3-bit field in body byte 0).
                 */
                enum WireEotf : uint8_t {
                        EotfSdrGamma   = 0, ///< Traditional gamma — SDR luminance range.
                        EotfHdrGamma   = 1, ///< Traditional gamma — HDR luminance range.
                        EotfSmpte2084  = 2, ///< SMPTE ST 2084 (PQ / HDR10).
                        EotfHlg        = 3, ///< ITU-R BT.2100 HLG.
                };

                /** @brief Body length for Static Metadata Descriptor Type 1. */
                static constexpr size_t Type1BodySize = 26;

                /**
                 * @brief Default-constructs to SDR / unspecified / zero.
                 *
                 * The mastering-display + content-light-level leaves are
                 * initialised to their wire-zero form (chromaticities
                 * @c (0,0), luminances @c 0) — not @ref MasteringDisplay's
                 * own default (CIE @c (-1,-1) sentinel), which would
                 * not survive a CTA-861.3 toBuffer / fromBuffer
                 * round-trip (negative chromaticities clamp to zero on
                 * encode).  This keeps a default-constructed value
                 * round-trippable through the wire format and
                 * @ref DataStream.
                 */
                HdrStaticMetadata()
                    : _md(CIEPoint(0.0, 0.0), CIEPoint(0.0, 0.0), CIEPoint(0.0, 0.0), CIEPoint(0.0, 0.0),
                          0.0, 0.0),
                      _cll(0, 0) {}

                /**
                 * @brief Constructs a full descriptor.
                 *
                 * @param eotf  Transfer characteristic (see EOTF mapping).
                 * @param md    Mastering display color volume.
                 * @param cll   Content light level (MaxCLL / MaxFALL).
                 */
                HdrStaticMetadata(TransferCharacteristics eotf, MasteringDisplay md, ContentLightLevel cll)
                    : _eotf(eotf), _md(std::move(md)), _cll(std::move(cll)) {}

                /** @brief Returns the in-memory transfer characteristic. */
                TransferCharacteristics eotf() const { return _eotf; }

                /** @brief Sets the in-memory transfer characteristic. */
                void setEotf(TransferCharacteristics v) { _eotf = v; }

                /** @brief Returns the mastering display color volume (may be invalid). */
                const MasteringDisplay &masteringDisplay() const { return _md; }

                /** @brief Sets the mastering display color volume. */
                void setMasteringDisplay(const MasteringDisplay &v) { _md = v; }

                /** @brief Returns the content light level (zero = unspecified per spec). */
                const ContentLightLevel &contentLightLevel() const { return _cll; }

                /** @brief Sets the content light level. */
                void setContentLightLevel(const ContentLightLevel &v) { _cll = v; }

                /**
                 * @brief Maps a @ref TransferCharacteristics value to the
                 *        CTA-861.3 wire-EOTF code (0..3).
                 *
                 * Anything outside @c {SMPTE2084, ARIB_STD_B67} maps to
                 * @c EotfSdrGamma — receivers ignore the descriptor for
                 * SDR signalling, which is the safe default for
                 * "unspecified or non-HDR."
                 */
                static uint8_t wireEotfFor(TransferCharacteristics tc);

                /**
                 * @brief Maps a CTA-861.3 wire-EOTF code back to
                 *        @ref TransferCharacteristics.
                 *
                 * @c EotfSdrGamma / @c EotfHdrGamma both map to
                 * @c Unspecified (neither has a clean H.265 VUI match);
                 * @c EotfSmpte2084 → @c SMPTE2084;
                 * @c EotfHlg → @c ARIB_STD_B67.
                 */
                static TransferCharacteristics transferFromWireEotf(uint8_t wire);

                /**
                 * @brief Serialises to the 26-byte CTA-861-G DRM
                 *        InfoFrame body (Static Metadata Type 1).
                 *
                 * Chromaticities are encoded as
                 * @c round(value * 50000), clamped to [0, 65535].
                 * Luminances are encoded as @c round(maxLum) and
                 * @c round(minLum * 10000) respectively, clamped to
                 * [0, 65535].  MaxCLL / MaxFALL are written as plain
                 * uint16 LE.  An invalid @ref MasteringDisplay (zero
                 * primaries / zero max luminance) emits zero bytes in
                 * those positions — the CTA-861.3 "unspecified" form.
                 */
                Buffer toBuffer() const;

                /**
                 * @brief Parses a CTA-861-G DRM InfoFrame body.
                 *
                 * Validates @c size >= @ref Type1BodySize and that
                 * @c Static_Metadata_Descriptor_ID == 0; returns
                 * @c Error::CorruptData otherwise.  Trailing bytes past
                 * the Type-1 layout are ignored (forward-compatible with
                 * future descriptor sizes).
                 *
                 * @param data Pointer to the body bytes (no 4-byte
                 *             InfoFrame header — strip that first).
                 * @param size Number of bytes available at @p data.
                 */
                static Result<HdrStaticMetadata> fromBuffer(const void *data, size_t size);

                /** @brief Convenience overload accepting a @ref Buffer. */
                static Result<HdrStaticMetadata> fromBuffer(const Buffer &buf);

                /**
                 * @brief Produces a JSON representation for inspection.
                 *
                 * Schema:
                 * @code
                 * {
                 *   "eotf": "SMPTE2084",
                 *   "wireEotf": 2,
                 *   "masteringDisplay": {
                 *       "red":   {"x": 0.708, "y": 0.292},
                 *       "green": {"x": 0.170, "y": 0.797},
                 *       "blue":  {"x": 0.131, "y": 0.046},
                 *       "whitePoint": {"x": 0.3127, "y": 0.3290},
                 *       "minLuminance": 0.005,
                 *       "maxLuminance": 1000.0
                 *   },
                 *   "contentLightLevel": {"maxCLL": 1000, "maxFALL": 400}
                 * }
                 * @endcode
                 *
                 * @ref MasteringDisplay and @ref ContentLightLevel
                 * sub-objects appear regardless of validity so the
                 * shape is stable; consumers check @c maxLuminance > 0
                 * and @c maxCLL > 0 for "specified" semantics.
                 */
                JsonObject toJson() const;

                /** @brief Field-wise equality. */
                bool operator==(const HdrStaticMetadata &o) const;

                /** @brief Inequality. */
                bool operator!=(const HdrStaticMetadata &o) const { return !(*this == o); }

                /** @brief Returns a short human-readable summary. */
                String toString() const;

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 *
                 * Wire body: the canonical DRM InfoFrame byte stream
                 * (the same bytes @ref toBuffer produces) length-prefixed
                 * as a @ref Buffer.  Round-trips through @ref fromBuffer.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<HdrStaticMetadata> readFromStream(DataStream &s);

        private:
                TransferCharacteristics _eotf = TransferCharacteristics::Unspecified;
                MasteringDisplay        _md;
                ContentLightLevel       _cll;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV