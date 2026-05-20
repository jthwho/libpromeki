/**
 * @file      anccodec_hdrdynamic2094_40_st291.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 2108-2:2019 HDR/WCG KLV Metadata Ancillary Data Packet
 * codec for @c AncFormat::HdrDynamic2094_40 (HDR10+) on
 * @c AncTransport::St291.
 *
 * @par Wire structure
 *
 * ST 291 packet (DID = @c 0x41, SDID = @c 0x0D, Type-2):
 *
 * @code
 * UDW[0]    Packet Count (u8, 1-based; first packet = 0x01)
 * UDW[1..]  HDR/WCG Metadata Message bytes
 * @endcode
 *
 * Multi-packet support: a full single-window HDR10+ Message
 * (~290 bytes) exceeds the 255-byte ST 291 UDW cap, so the builder
 * splits the Message across multiple ANC packets with Packet Count
 * incrementing from @c 0x01.  The parser is registered as a
 * @ref AncTranslator::MultiParserFn so the framework hands it the
 * full set of related packets at once; it sorts by Packet Count,
 * validates the @c [1, 2, …, N] sequence, concatenates the
 * Message-bytes, and walks the KLV frames inside.
 *
 * @par Message structure
 *
 * @code
 * Message Length (u16 BE)  count of bytes in following Frames
 * Frame 1
 *   Key   (16-byte SMPTE Universal Label)
 *   Length (BER-encoded — short form for L ≤ 127, long form otherwise)
 *   Value (Length bytes)
 * Frame 2
 *   ...
 * @endcode
 *
 * @par DMCVT Application 4 Set
 *
 * Each HDR10+ processing window emits one DMCVT App 4 Set frame.
 * The Frame Key is the App 4 Set UL
 * (@c 06.0E.2B.34.02.53.01.01.05.31.02.04.00.00.00.00); the Length
 * is BER long-form (4 bytes total: leading @c 0x83 then 3 length
 * bytes per ST 2094-2:2017 §6.1); the Value is the local-set body
 * — a sequence of {Local Tag (u16 BE), Local Length (u16 BE),
 * Value (L bytes)} triples.
 *
 * The wire-bitstream u(N) widths of @ref HdrDynamic2094_40 align
 * one-to-one with the ST 2094-2 Rational denominators for every
 * field except @c TargetedSystemDisplayMaximumLuminance (wire u(27)
 * is in 0.0001 cd/m² steps, ST 2094-2 Rational denominator is 100,
 * so the wire value is divided by 100 on emit).  This means HDR10+
 * round-trips through ST 2108-2 KLV at exactly the resolution the
 * wire form carries.
 *
 * @par Spec deviations / scope
 *
 *  - The ST 2094-40 §9.1 mandatory @c ProcessingWindow items
 *    (UpperLeftCorner, LowerRightCorner) for window 0 require the
 *    full image dimensions, which the value type does not carry.
 *    Window 0 emits sentinel UpperLeftCorner = @c (0,0) and
 *    LowerRightCorner = @c (0xFFFF, 0xFFFF).  A pixel-exact image
 *    window can be supplied via
 *    @c AncTranslateConfig::HdrDynamicImageWidth /
 *    @c HdrDynamicImageHeight once those keys land; today the
 *    sentinel is a documented limitation rather than a structural
 *    gap.
 *  - ST 2094-40 §9.1 also requires @c TimeInterval (Start +
 *    Duration); the codec emits the canonical per-frame defaults
 *    @c Start = 0 and @c Duration = 1.
 *  - The other three HDR/WCG Metadata Frame types defined by
 *    ST 2108-2 §5.4.2 (Mastering Display Color Volume Metadata
 *    §5.4.2.2, Maximum Light Level Metadata §5.4.2.3, DMCVT App 1
 *    Metadata §5.4.2.4) are out of scope for this codec.  HDR
 *    static is carried via ST 2108-1 SEI through @c HdrStatic2086.
 *    App 1 (ST 2094-10) would need its own codec.  The parser
 *    walks past unknown Frame Keys, so a mixed-type UDW that
 *    contains both App 4 and any of the other three frame types
 *    still decodes the App 4 portion correctly.
 *  - Parser tolerates per-set tag reordering only in the common
 *    case: if a non-App-4 Set leads with non-Identifier tags that
 *    happen to write to global @c HdrDynamic2094_40 state (e.g.
 *    @c ApplicationVersionNumber, @c TargetedSystemDisplayMaxLum)
 *    before the ApplicationIdentifier tag clarifies that the Set
 *    is not App 4, the global writes are not rolled back.  In
 *    practice both the library's builder and well-formed ST 2094-2
 *    senders emit ApplicationIdentifier first.
 *  - The optional @c TargetedSystemDisplayActualPeakLuminance and
 *    @c MasteringDisplayActualPeakLuminance grids round-trip when
 *    present.  Their wire bitstream stores u(4) values in 1/15
 *    units; ST 2094-2 stores them as UInt8 in the same 1/15 unit
 *    space (per ST 2094-2 Table 11 note "code values ... shall be
 *    15 × actual values"), so the bytes are written directly.
 *
 * @par Spec verification trail (D5c audit, 2026-05-20)
 *
 * Verified byte-for-byte against:
 *
 *  - ST 2108-2:2019 §5.1 (DID 0x41 / SDID 0x0D, Type-2, DC 02h–FFh).
 *  - ST 2108-2:2019 §5.3 (UDW[0] Packet Count, 1-based; UDW[1..]
 *    Metadata Message; multi-packet concatenation rule).
 *  - ST 2108-2:2019 §5.4 (16-bit BE Message Length excluding self,
 *    followed by KLV Frames).
 *  - ST 2094-2:2017 Table 10 (DMCVT App 4 Set key, 16 bytes).
 *  - ST 2094-2:2017 §6.1 (Set Length always 4-byte BER long form
 *    @c 0x83 + 3 length bytes; Local Tag + Local Length 2 bytes
 *    each).
 *  - ST 2094-2:2017 Table 11 (every emitted/parsed Local Tag
 *    matches: 36.01, 36.02, 36.04, 36.05, 36.06, 36.07, 36.08,
 *    36.0B, 36.30–36.41).
 *  - ST 2094-2:2017 Table 11 (Rational denominators: 100, 100000,
 *    100000, 100000, 1000, 4095, 1023, 8 — all match).
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ---------------------------------------------------------------
        // ST 291 / ST 2108-2 constants
        // ---------------------------------------------------------------

        constexpr uint8_t kSt2108_2Did  = 0x41;
        constexpr uint8_t kSt2108_2Sdid = 0x0D;

        // 16-byte Universal Label for DMCVT App 4 Set (ST 2094-2 Table 10).
        constexpr uint8_t kDmcvtApp4SetKey[16] = {
                0x06, 0x0E, 0x2B, 0x34, 0x02, 0x53, 0x01, 0x01,
                0x05, 0x31, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00,
        };

        // ---------------------------------------------------------------
        // ST 2094-2 Local Tags (Annex A Table A.2)
        // ---------------------------------------------------------------

        constexpr uint16_t kTagApplicationIdentifier             = 0x3601;
        constexpr uint16_t kTagApplicationVersionNumber          = 0x3602;
        constexpr uint16_t kTagTimeIntervalStart                  = 0x3604;
        constexpr uint16_t kTagTimeIntervalDuration               = 0x3605;
        constexpr uint16_t kTagUpperLeftCorner                    = 0x3606;
        constexpr uint16_t kTagLowerRightCorner                   = 0x3607;
        constexpr uint16_t kTagWindowNumber                       = 0x3608;
        constexpr uint16_t kTagTargetedSystemDisplayMaxLum        = 0x360B;
        constexpr uint16_t kTagCenterOfEllipse                    = 0x3630;
        constexpr uint16_t kTagRotationAngle                      = 0x3631;
        constexpr uint16_t kTagSemiMajorAxisInternalEllipse       = 0x3632;
        constexpr uint16_t kTagSemiMajorAxisExternalEllipse       = 0x3633;
        constexpr uint16_t kTagSemiMinorAxisExternalEllipse       = 0x3634;
        constexpr uint16_t kTagOverlapProcessOption               = 0x3635;
        constexpr uint16_t kTagTargetedSysDispActualPeakLuminance = 0x3636;
        constexpr uint16_t kTagRowsInTargetedSysDispActualPeak    = 0x3637;
        constexpr uint16_t kTagMasteringDispActualPeakLuminance   = 0x3638;
        constexpr uint16_t kTagRowsInMasteringDispActualPeak      = 0x3639;
        constexpr uint16_t kTagMaxSCL                             = 0x363A;
        constexpr uint16_t kTagAverageMaxRGB                      = 0x363B;
        constexpr uint16_t kTagDistributionMaxRGBPercentages      = 0x363C;
        constexpr uint16_t kTagDistributionMaxRGBPercentiles      = 0x363D;
        constexpr uint16_t kTagFractionBrightPixels               = 0x363E;
        constexpr uint16_t kTagKneePoint                          = 0x363F;
        constexpr uint16_t kTagBezierCurveAnchors                 = 0x3640;
        constexpr uint16_t kTagColorSaturationWeight              = 0x3641;

        // ---------------------------------------------------------------
        // Rational denominators (ST 2094-2 Tables 3, 7, 11)
        // ---------------------------------------------------------------

        constexpr uint32_t kDenTargetedSysDispMaxLum = 100;     // cd/m²/100
        constexpr uint32_t kDenMaxSCL                = 100000;
        constexpr uint32_t kDenAverageMaxRGB         = 100000;
        constexpr uint32_t kDenDistribMaxRGBPercentiles = 100000;
        constexpr uint32_t kDenFractionBrightPixels  = 1000;
        constexpr uint32_t kDenKneePoint             = 4095;
        constexpr uint32_t kDenBezierCurveAnchors    = 1023;
        constexpr uint32_t kDenColorSaturationWeight = 8;

        constexpr uint8_t kHdrDynApplicationIdentifier = 4;

        // ---------------------------------------------------------------
        // Byte accumulator helpers
        // ---------------------------------------------------------------

        void appendU8(List<uint8_t> &out, uint8_t v) { out.pushToBack(v); }

        void appendU16Be(List<uint8_t> &out, uint16_t v) {
                out.pushToBack(static_cast<uint8_t>((v >> 8) & 0xFF));
                out.pushToBack(static_cast<uint8_t>(v & 0xFF));
        }

        void appendU32Be(List<uint8_t> &out, uint32_t v) {
                out.pushToBack(static_cast<uint8_t>((v >> 24) & 0xFF));
                out.pushToBack(static_cast<uint8_t>((v >> 16) & 0xFF));
                out.pushToBack(static_cast<uint8_t>((v >> 8) & 0xFF));
                out.pushToBack(static_cast<uint8_t>(v & 0xFF));
        }

        void appendBytes(List<uint8_t> &out, const uint8_t *src, size_t n) {
                for (size_t i = 0; i < n; ++i) out.pushToBack(src[i]);
        }

        // ASN.1 BER long-form length (always 4 bytes total per ST 2094-2
        // §6.1: 0x83 followed by 3 length bytes).
        void appendBerLongLength4(List<uint8_t> &out, uint32_t length) {
                out.pushToBack(0x83);
                out.pushToBack(static_cast<uint8_t>((length >> 16) & 0xFF));
                out.pushToBack(static_cast<uint8_t>((length >> 8) & 0xFF));
                out.pushToBack(static_cast<uint8_t>(length & 0xFF));
        }

        // ---------------------------------------------------------------
        // Local-set item emitters
        // ---------------------------------------------------------------

        void emitLocalItemHeader(List<uint8_t> &out, uint16_t tag, uint16_t length) {
                appendU16Be(out, tag);
                appendU16Be(out, length);
        }

        void emitUInt8Item(List<uint8_t> &out, uint16_t tag, uint8_t value) {
                emitLocalItemHeader(out, tag, 1);
                appendU8(out, value);
        }

        void emitUInt32Item(List<uint8_t> &out, uint16_t tag, uint32_t value) {
                emitLocalItemHeader(out, tag, 4);
                appendU32Be(out, value);
        }

        void emitRationalItem(List<uint8_t> &out, uint16_t tag, uint32_t numerator, uint32_t denominator) {
                emitLocalItemHeader(out, tag, 8);
                appendU32Be(out, numerator);
                appendU32Be(out, denominator);
        }

        // RationalArray header is the standard MXF strong array header:
        // u32 count + u32 element size (=8 for Rational), then count
        // elements of (u32 num, u32 den).
        void beginRationalArray(List<uint8_t> &out, uint16_t tag, uint32_t count) {
                emitLocalItemHeader(out, tag, static_cast<uint16_t>(8u + 8u * count));
                appendU32Be(out, count);
                appendU32Be(out, 8);
        }

        void appendRationalElement(List<uint8_t> &out, uint32_t numerator, uint32_t denominator) {
                appendU32Be(out, numerator);
                appendU32Be(out, denominator);
        }

        // UInt8Array uses the same 8-byte header (count + element size = 1)
        // followed by count single-byte values.
        void emitUInt8ArrayItem(List<uint8_t> &out, uint16_t tag, const uint8_t *src, uint32_t count) {
                emitLocalItemHeader(out, tag, static_cast<uint16_t>(8u + count));
                appendU32Be(out, count);
                appendU32Be(out, 1);
                for (uint32_t i = 0; i < count; ++i) appendU8(out, src[i]);
        }

        // UInt16Array — count + element size (= 2) + count*2 bytes.
        void emitUInt16ArrayItem(List<uint8_t> &out, uint16_t tag, const uint16_t *src, uint32_t count) {
                emitLocalItemHeader(out, tag, static_cast<uint16_t>(8u + 2u * count));
                appendU32Be(out, count);
                appendU32Be(out, 2);
                for (uint32_t i = 0; i < count; ++i) appendU16Be(out, src[i]);
        }

        void emitUInt16PairItem(List<uint8_t> &out, uint16_t tag, uint16_t a, uint16_t b) {
                uint16_t arr[2] = {a, b};
                emitUInt16ArrayItem(out, tag, arr, 2);
        }

        // ---------------------------------------------------------------
        // Build: HdrDynamic2094_40 → ST 291 packet
        // ---------------------------------------------------------------

        void emitWindowItems(List<uint8_t> &out, const HdrDynamic2094_40 &md, uint8_t windowIndex,
                              uint32_t imageWidth, uint32_t imageHeight) {
                emitUInt8Item(out, kTagWindowNumber, windowIndex);

                if (windowIndex == 0) {
                        // ST 2094-40 §9.2: "Processing Window 0 shall be
                        // always present and shall cover all pixels in
                        // an image" — the rectangle must match real
                        // image dimensions.  When the caller stamped
                        // @c HdrDynamicImageWidth / @c HdrDynamicImageHeight
                        // we emit that; otherwise we fall back to the
                        // legacy (0xFFFF, 0xFFFF) sentinel and log a
                        // warn (sentinel is not §9.2-conformant — a
                        // receiver reading it sees a 65536×65536
                        // window).
                        emitUInt16PairItem(out, kTagUpperLeftCorner, 0, 0);
                        if (imageWidth == 0 || imageHeight == 0) {
                                promekiWarn("anccodec_hdrdynamic2094_40_st291: Window 0 image "
                                            "dimensions absent — emitting (0xFFFF, 0xFFFF) sentinel "
                                            "(not ST 2094-40 §9.2 conformant); set "
                                            "AncTranslateConfig::HdrDynamicImageWidth/Height to fix");
                                emitUInt16PairItem(out, kTagLowerRightCorner, 0xFFFF, 0xFFFF);
                        } else {
                                // ST 2094-40 §9.2 expresses the corner
                                // coordinates as pixel-1 / pixel-(N-1),
                                // i.e. inclusive last-pixel indices.
                                const uint16_t rx = static_cast<uint16_t>(imageWidth > 0xFFFFu
                                                                  ? 0xFFFFu : (imageWidth - 1));
                                const uint16_t ry = static_cast<uint16_t>(imageHeight > 0xFFFFu
                                                                  ? 0xFFFFu : (imageHeight - 1));
                                emitUInt16PairItem(out, kTagLowerRightCorner, rx, ry);
                        }
                        return;
                }

                const size_t extraIdx = static_cast<size_t>(windowIndex - 1);
                if (extraIdx >= md.extraWindows().size()) {
                        // Shouldn't happen if num_windows is consistent with extraWindows.size();
                        // emit a degenerate window to avoid crashing.
                        emitUInt16PairItem(out, kTagUpperLeftCorner, 0, 0);
                        emitUInt16PairItem(out, kTagLowerRightCorner, 0, 0);
                        return;
                }

                const HdrDynamic2094_40::Window &w = md.extraWindows()[extraIdx];
                emitUInt16PairItem(out, kTagUpperLeftCorner, w.upperLeftCornerX, w.upperLeftCornerY);
                emitUInt16PairItem(out, kTagLowerRightCorner, w.lowerRightCornerX, w.lowerRightCornerY);
                emitUInt16PairItem(out, kTagCenterOfEllipse, w.centerOfEllipseX, w.centerOfEllipseY);
                emitUInt8Item(out, kTagRotationAngle, w.rotationAngle);
                emitLocalItemHeader(out, kTagSemiMajorAxisInternalEllipse, 2);
                appendU16Be(out, w.semimajorAxisInternalEllipse);
                emitLocalItemHeader(out, kTagSemiMajorAxisExternalEllipse, 2);
                appendU16Be(out, w.semimajorAxisExternalEllipse);
                emitLocalItemHeader(out, kTagSemiMinorAxisExternalEllipse, 2);
                appendU16Be(out, w.semiminorAxisExternalEllipse);
                emitUInt8Item(out, kTagOverlapProcessOption, w.overlapProcessOption ? 1 : 0);
        }

        void emitActualPeakLuminance(List<uint8_t> &out, uint16_t valuesTag, uint16_t rowsTag,
                                      const HdrDynamic2094_40::ActualPeakLuminance &grid) {
                if (!grid.isPresent()) return;
                const uint32_t cells = static_cast<uint32_t>(grid.numRows) * static_cast<uint32_t>(grid.numCols);
                // The wire stores u(4) cells; ST 2094-2 stores UInt8 with the
                // same 1/15 unit (per Table 11 note "15 × actual values; the
                // four most significant bits in each UInt8 are zero").
                emitUInt8ArrayItem(out, valuesTag, grid.values.data(), cells);
                emitUInt8Item(out, rowsTag, grid.numRows);
        }

        Buffer buildDmcvtApp4SetValue(const HdrDynamic2094_40 &md, uint8_t windowIndex,
                                       uint32_t imageWidth, uint32_t imageHeight) {
                const HdrDynamic2094_40::WindowProcessing &wp =
                        windowIndex < md.windowProcessing().size() ? md.windowProcessing()[windowIndex]
                                                                    : HdrDynamic2094_40::WindowProcessing();
                // ST 2094-40:2020 §9.3 / §9.4 forbid three optional
                // items (TargetedSystemDisplayActualPeakLuminance,
                // MasteringDisplayActualPeakLuminance,
                // ColorSaturationWeight) at both AppVer=0 and AppVer=1
                // — with SHOULD NOT at AppVer=0 and SHALL NOT at
                // AppVer=1.  Honour the spec on emission: strip the
                // items at AppVer=1 (with a warn so the caller knows
                // their value type carried fields the wire dropped);
                // warn-but-emit at AppVer=0 so legacy senders are not
                // surprised by silent stripping.
                const bool appVer1 = (md.applicationVersion() == 1u);
                const bool appVer0 = (md.applicationVersion() == 0u);
                auto warnIfAppVer0 = [&](const char *fieldName) {
                        if (appVer0) {
                                promekiWarn("HdrDynamic2094_40: %s present at ApplicationVersion=0 "
                                            "(ST 2094-40:2020 §9.3 SHOULD NOT include) — emitting anyway",
                                            fieldName);
                        }
                };
                auto warnIfAppVer1 = [&](const char *fieldName) {
                        if (appVer1) {
                                promekiWarn("HdrDynamic2094_40: %s forbidden at ApplicationVersion=1 "
                                            "(ST 2094-40:2020 §9.4 SHALL NOT include) — stripping",
                                            fieldName);
                        }
                };

                List<uint8_t> bytes;

                emitUInt8Item(bytes, kTagApplicationIdentifier, kHdrDynApplicationIdentifier);
                emitUInt8Item(bytes, kTagApplicationVersionNumber, md.applicationVersion());

                // TimeInterval: emit per-frame defaults (Start=0, Duration=1).
                emitUInt32Item(bytes, kTagTimeIntervalStart, 0);
                emitUInt32Item(bytes, kTagTimeIntervalDuration, 1);

                // ProcessingWindow.
                emitWindowItems(bytes, md, windowIndex, imageWidth, imageHeight);

                // TargetedSystemDisplay: required TargetedSystemDisplayMaximumLuminance.
                // Wire u(27) is 0.0001 cd/m² steps; ST 2094-2 Rational uses /100
                // (cd/m² with two-decimal resolution), so divide by 100.
                emitRationalItem(bytes, kTagTargetedSystemDisplayMaxLum,
                                  md.targetedSystemDisplayMaximumLuminance() / 100u, kDenTargetedSysDispMaxLum);
                // §9.3 / §9.4: TargetedSystemDisplayActualPeakLuminance.
                if (md.targetedSystemDisplayActualPeakLuminance().isPresent()) {
                        if (appVer1) {
                                warnIfAppVer1("TargetedSystemDisplayActualPeakLuminance");
                        } else {
                                warnIfAppVer0("TargetedSystemDisplayActualPeakLuminance");
                                emitActualPeakLuminance(bytes, kTagTargetedSysDispActualPeakLuminance,
                                                         kTagRowsInTargetedSysDispActualPeak,
                                                         md.targetedSystemDisplayActualPeakLuminance());
                        }
                }

                // ColorVolumeTransform: MaxSCL (3 elements), AverageMaxRGB, Distribution*, FractionBrightPixels.
                beginRationalArray(bytes, kTagMaxSCL, 3);
                for (int c = 0; c < 3; ++c) appendRationalElement(bytes, wp.maxScl[c], kDenMaxSCL);
                emitRationalItem(bytes, kTagAverageMaxRGB, wp.averageMaxRgb, kDenAverageMaxRGB);

                const uint32_t numPerc = static_cast<uint32_t>(wp.distribution.size());
                {
                        List<uint8_t> percentages;
                        percentages.reserve(numPerc);
                        for (uint32_t i = 0; i < numPerc; ++i) percentages.pushToBack(wp.distribution[i].percentage);
                        emitUInt8ArrayItem(bytes, kTagDistributionMaxRGBPercentages,
                                            percentages.data(), numPerc);
                }
                beginRationalArray(bytes, kTagDistributionMaxRGBPercentiles, numPerc);
                for (uint32_t i = 0; i < numPerc; ++i) {
                        appendRationalElement(bytes, wp.distribution[i].percentile, kDenDistribMaxRGBPercentiles);
                }
                emitRationalItem(bytes, kTagFractionBrightPixels, wp.fractionBrightPixels,
                                  kDenFractionBrightPixels);

                // §9.3 / §9.4: MasteringDisplayActualPeakLuminance.
                // Shared across windows (per-set); emit once on window 0.
                if (windowIndex == 0 && md.masteringDisplayActualPeakLuminance().isPresent()) {
                        if (appVer1) {
                                warnIfAppVer1("MasteringDisplayActualPeakLuminance");
                        } else {
                                warnIfAppVer0("MasteringDisplayActualPeakLuminance");
                                emitActualPeakLuminance(bytes, kTagMasteringDispActualPeakLuminance,
                                                         kTagRowsInMasteringDispActualPeak,
                                                         md.masteringDisplayActualPeakLuminance());
                        }
                }

                // BezierCurveToneMapper (optional, allowed at both versions).
                if (wp.hasToneMapping) {
                        beginRationalArray(bytes, kTagKneePoint, 2);
                        appendRationalElement(bytes, wp.toneMapping.kneePointX, kDenKneePoint);
                        appendRationalElement(bytes, wp.toneMapping.kneePointY, kDenKneePoint);
                        const uint32_t anchorCount =
                                static_cast<uint32_t>(wp.toneMapping.bezierCurveAnchors.size());
                        if (anchorCount > 0) {
                                beginRationalArray(bytes, kTagBezierCurveAnchors, anchorCount);
                                for (uint32_t i = 0; i < anchorCount; ++i) {
                                        appendRationalElement(bytes, wp.toneMapping.bezierCurveAnchors[i],
                                                              kDenBezierCurveAnchors);
                                }
                        }
                }
                // §9.3 / §9.4: ColorSaturationWeight.
                if (wp.hasColorSaturationMapping) {
                        if (appVer1) {
                                warnIfAppVer1("ColorSaturationWeight");
                        } else {
                                warnIfAppVer0("ColorSaturationWeight");
                                emitRationalItem(bytes, kTagColorSaturationWeight, wp.colorSaturationWeight,
                                                  kDenColorSaturationWeight);
                        }
                }

                Buffer buf(bytes.size());
                if (!bytes.isEmpty()) {
                        buf.copyFrom(bytes.data(), bytes.size(), 0);
                        buf.setSize(bytes.size());
                }
                return buf;
        }

        Buffer buildFullMessage(const HdrDynamic2094_40 &md, uint32_t imageWidth, uint32_t imageHeight) {
                // For each window, emit one DMCVT App 4 Frame.
                List<uint8_t> frames;
                // ST 2094-40 §9.2: "the maximum number of processing
                // windows within one image shall be 3".  Clamp here
                // rather than silently emit a non-conformant payload.
                uint8_t       numWindows = md.numWindows() < 1 ? 1 : md.numWindows();
                if (numWindows > 3) {
                        promekiWarn("anccodec_hdrdynamic2094_40_st291: numWindows=%u exceeds "
                                    "ST 2094-40 §9.2 cap of 3; clamping",
                                    static_cast<unsigned>(numWindows));
                        numWindows = 3;
                }
                // ST 2094-40:2020 §9.4: at ApplicationVersion=1,
                // "WindowNumber > 0 shall not be present" — so the
                // metadata set is limited to one window (window 0).
                // Clamp with a warn rather than silently emit a
                // non-conformant multi-window payload.  §9.3 has the
                // SHOULD-NOT softer form at AppVer=0; warn but emit.
                if (md.applicationVersion() == 1u && numWindows > 1) {
                        promekiWarn("HdrDynamic2094_40: numWindows=%u at ApplicationVersion=1 "
                                    "(ST 2094-40:2020 §9.4 SHALL NOT include WindowNumber > 0) — "
                                    "clamping to 1",
                                    static_cast<unsigned>(numWindows));
                        numWindows = 1;
                }
                if (md.applicationVersion() == 0u && numWindows > 1) {
                        promekiWarn("HdrDynamic2094_40: numWindows=%u at ApplicationVersion=0 "
                                    "(ST 2094-40:2020 §9.3 SHOULD NOT include WindowNumber > 0) — "
                                    "emitting anyway",
                                    static_cast<unsigned>(numWindows));
                }
                for (uint8_t w = 0; w < numWindows; ++w) {
                        Buffer       setValue = buildDmcvtApp4SetValue(md, w, imageWidth, imageHeight);
                        const size_t valSize = setValue.size();
                        appendBytes(frames, kDmcvtApp4SetKey, sizeof(kDmcvtApp4SetKey));
                        appendBerLongLength4(frames, static_cast<uint32_t>(valSize));
                        if (valSize > 0) {
                                appendBytes(frames, static_cast<const uint8_t *>(setValue.data()), valSize);
                        }
                }

                // Message Length (u16 BE) then frames concatenated.
                List<uint8_t> message;
                appendU16Be(message, static_cast<uint16_t>(frames.size()));
                appendBytes(message, frames.data(), frames.size());

                Buffer buf(message.size());
                buf.copyFrom(message.data(), message.size(), 0);
                buf.setSize(message.size());
                return buf;
        }

        AncTranslator::PacketsResult buildHdrDynamicSt291(const Variant &v, const AncTranslateConfig &cfg) {
                HdrDynamic2094_40 md = v.get<HdrDynamic2094_40>();
                const uint32_t imageWidth = cfg.getAs<uint32_t>(
                        AncTranslateConfig::HdrDynamicImageWidth, uint32_t(0));
                const uint32_t imageHeight = cfg.getAs<uint32_t>(
                        AncTranslateConfig::HdrDynamicImageHeight, uint32_t(0));
                Buffer            message = buildFullMessage(md, imageWidth, imageHeight);

                uint16_t line = cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine,
                                                    St291Packet::UnspecifiedLine);
                bool     fieldB = cfg.getAs<bool>(AncTranslateConfig::St291FieldB, false);
                bool     cBit = cfg.getAs<bool>(AncTranslateConfig::St291BuildCBit, false);

                // ST 291 DC field is one byte → UDW max is 255 bytes per
                // packet.  UDW[0] is the Packet Count, leaving 254 bytes of
                // Message payload per packet.  Split the Message into
                // chunks of up to kMessageBytesPerPacket bytes and emit one
                // ANC packet per chunk with Packet Count incrementing from
                // 0x01.  ST 2108-2 §5.3 specifies this exact framing for
                // oversize Messages.
                constexpr size_t kMessageBytesPerPacket = 254;
                const size_t     totalMessageBytes = message.size();
                const uint8_t   *mp = static_cast<const uint8_t *>(message.data());

                AncPacket::List out;
                if (totalMessageBytes == 0) {
                        // Empty Message — emit one packet with just the
                        // Packet Count byte.  Useful for edge-case testing;
                        // real producers always emit at least the 2-byte
                        // Message Length header.
                        List<uint16_t> udw;
                        udw.pushToBack(0x01);
                        St291Packet p = St291Packet::build(AncFormat(AncFormat::HdrDynamic2094_40), udw, line,
                                                            St291Packet::UnspecifiedHOffset, fieldB, cBit);
                        out.pushToBack(p.packet());
                        return makeResult<AncPacket::List>(std::move(out));
                }

                const size_t totalPackets =
                        (totalMessageBytes + kMessageBytesPerPacket - 1) / kMessageBytesPerPacket;
                if (totalPackets > 255) {
                        // Packet Count is a u8; > 255 packets violates the
                        // wire model.  At 254 bytes per packet that's 64 KB
                        // of Message — well beyond any realistic HDR10+
                        // payload.
                        return makeError<AncPacket::List>(Error::OutOfRange);
                }

                for (size_t pktIdx = 0; pktIdx < totalPackets; ++pktIdx) {
                        const size_t off = pktIdx * kMessageBytesPerPacket;
                        const size_t remaining = totalMessageBytes - off;
                        const size_t chunk = remaining < kMessageBytesPerPacket ? remaining
                                                                                 : kMessageBytesPerPacket;
                        List<uint16_t> udw;
                        udw.reserve(1 + chunk);
                        udw.pushToBack(static_cast<uint16_t>(pktIdx + 1)); // Packet Count, 1-based.
                        for (size_t i = 0; i < chunk; ++i) udw.pushToBack(mp[off + i]);

                        St291Packet p = St291Packet::build(AncFormat(AncFormat::HdrDynamic2094_40), udw, line,
                                                            St291Packet::UnspecifiedHOffset, fieldB, cBit);
                        out.pushToBack(p.packet());
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

        // ---------------------------------------------------------------
        // Parse: ST 291 packet → HdrDynamic2094_40
        // ---------------------------------------------------------------

        // Reads a length encoded per ASN.1 BER (short or long form) from
        // @p data[idx..].  Advances @p idx past the length bytes; returns
        // the parsed length, or 0 with @p ok cleared on underflow.
        uint32_t readBerLength(const uint8_t *data, size_t size, size_t &idx, bool &ok) {
                if (idx >= size) {
                        ok = false;
                        return 0;
                }
                uint8_t first = data[idx++];
                if ((first & 0x80) == 0) return first; // short form
                uint8_t bytes = first & 0x7F;
                if (bytes == 0 || bytes > 4) {
                        ok = false;
                        return 0;
                }
                if (idx + bytes > size) {
                        ok = false;
                        return 0;
                }
                uint32_t len = 0;
                for (uint8_t i = 0; i < bytes; ++i) len = (len << 8) | data[idx++];
                return len;
        }

        uint16_t readU16Be(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

        uint32_t readU32Be(const uint8_t *p) {
                // Each byte is cast to uint32_t before shifting; under C++20 the
                // `int`-promoted shift was well-defined (result fits in
                // unsigned int) but the explicit cast keeps the intent obvious
                // and avoids integer-promotion gotchas on platforms with
                // narrower `int`.
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) <<  8) |  static_cast<uint32_t>(p[3]);
        }

        bool isApp4SetKey(const uint8_t *key) {
                for (size_t i = 0; i < sizeof(kDmcvtApp4SetKey); ++i) {
                        if (key[i] != kDmcvtApp4SetKey[i]) return false;
                }
                return true;
        }

        // Reads a Rational item value (8 bytes: num + den) and returns
        // the numerator.  Caller validates length == 8 first.
        uint32_t readRationalNumerator(const uint8_t *p) { return readU32Be(p); }

        // Walks the array-typed items.  Returns false on size mismatch.
        bool readArrayHeader(const uint8_t *itemValue, uint32_t itemLen, uint32_t &count, uint32_t &elemSize) {
                if (itemLen < 8) return false;
                count = readU32Be(itemValue);
                elemSize = readU32Be(itemValue + 4);
                return static_cast<uint64_t>(8) + static_cast<uint64_t>(count) * elemSize == itemLen;
        }

        // Parses one DMCVT App 4 Set Value into @p md.  @p windowIndex
        // is unknown until we hit the WindowNumber item; the caller-owned
        // bookkeeping picks the matching WindowProcessing entry.  Returns
        // false on a structurally-invalid item.
        bool parseDmcvtApp4SetValue(const uint8_t *data, size_t size, HdrDynamic2094_40 &md) {
                size_t  idx = 0;
                uint8_t windowIndex = 0;
                bool    haveWindowIndex = false;
                HdrDynamic2094_40::Window           win;
                bool                                 haveWindowGeometry = false;
                HdrDynamic2094_40::WindowProcessing  wp;

                while (idx + 4 <= size) {
                        uint16_t tag = readU16Be(data + idx); idx += 2;
                        uint16_t len = readU16Be(data + idx); idx += 2;
                        if (idx + len > size) return false;
                        const uint8_t *value = data + idx;

                        switch (tag) {
                                case kTagApplicationIdentifier:
                                        if (len != 1 || value[0] != kHdrDynApplicationIdentifier) {
                                                // Not an App 4 set — bail gracefully (caller skips this frame).
                                                return false;
                                        }
                                        break;
                                case kTagApplicationVersionNumber:
                                        if (len == 1) md.setApplicationVersion(value[0]);
                                        break;
                                case kTagWindowNumber:
                                        if (len == 1) {
                                                windowIndex = value[0];
                                                haveWindowIndex = true;
                                        }
                                        break;
                                case kTagUpperLeftCorner: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && c == 2 && es == 2) {
                                                win.upperLeftCornerX = readU16Be(value + 8);
                                                win.upperLeftCornerY = readU16Be(value + 10);
                                                haveWindowGeometry = true;
                                        }
                                        break;
                                }
                                case kTagLowerRightCorner: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && c == 2 && es == 2) {
                                                win.lowerRightCornerX = readU16Be(value + 8);
                                                win.lowerRightCornerY = readU16Be(value + 10);
                                                haveWindowGeometry = true;
                                        }
                                        break;
                                }
                                case kTagCenterOfEllipse: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && c == 2 && es == 2) {
                                                win.centerOfEllipseX = readU16Be(value + 8);
                                                win.centerOfEllipseY = readU16Be(value + 10);
                                        }
                                        break;
                                }
                                case kTagRotationAngle:
                                        if (len == 1) win.rotationAngle = value[0];
                                        break;
                                case kTagSemiMajorAxisInternalEllipse:
                                        if (len == 2) win.semimajorAxisInternalEllipse = readU16Be(value);
                                        break;
                                case kTagSemiMajorAxisExternalEllipse:
                                        if (len == 2) win.semimajorAxisExternalEllipse = readU16Be(value);
                                        break;
                                case kTagSemiMinorAxisExternalEllipse:
                                        if (len == 2) win.semiminorAxisExternalEllipse = readU16Be(value);
                                        break;
                                case kTagOverlapProcessOption:
                                        if (len == 1) win.overlapProcessOption = (value[0] != 0);
                                        break;
                                case kTagTargetedSystemDisplayMaxLum:
                                        if (len == 8) {
                                                uint32_t num = readRationalNumerator(value);
                                                // Wire form is 0.0001 cd/m²; Rational Den 100 means cd/m²×100.
                                                md.setTargetedSystemDisplayMaximumLuminance(num * 100u);
                                        }
                                        break;
                                case kTagTargetedSysDispActualPeakLuminance: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && es == 1) {
                                                auto &grid = md.targetedSystemDisplayActualPeakLuminance();
                                                grid.numCols = static_cast<uint8_t>(c);
                                                grid.values.resize(c);
                                                for (uint32_t i = 0; i < c; ++i) grid.values[i] = value[8 + i];
                                        }
                                        break;
                                }
                                case kTagRowsInTargetedSysDispActualPeak:
                                        if (len == 1) {
                                                auto &grid = md.targetedSystemDisplayActualPeakLuminance();
                                                grid.numRows = value[0];
                                                if (grid.numRows > 0) {
                                                        // numCols was set from the array length; partition the flat values
                                                        // list into a rows*cols matrix in row-major order — already the
                                                        // emission format.
                                                        grid.numCols = static_cast<uint8_t>(grid.values.size() /
                                                                                            grid.numRows);
                                                }
                                        }
                                        break;
                                case kTagMasteringDispActualPeakLuminance: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && es == 1) {
                                                auto &grid = md.masteringDisplayActualPeakLuminance();
                                                grid.numCols = static_cast<uint8_t>(c);
                                                grid.values.resize(c);
                                                for (uint32_t i = 0; i < c; ++i) grid.values[i] = value[8 + i];
                                        }
                                        break;
                                }
                                case kTagRowsInMasteringDispActualPeak:
                                        if (len == 1) {
                                                auto &grid = md.masteringDisplayActualPeakLuminance();
                                                grid.numRows = value[0];
                                                if (grid.numRows > 0) {
                                                        grid.numCols = static_cast<uint8_t>(grid.values.size() /
                                                                                            grid.numRows);
                                                }
                                        }
                                        break;
                                case kTagMaxSCL: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && c == 3 && es == 8) {
                                                for (uint32_t i = 0; i < 3; ++i) {
                                                        wp.maxScl[i] = readRationalNumerator(value + 8 + i * 8);
                                                }
                                        }
                                        break;
                                }
                                case kTagAverageMaxRGB:
                                        if (len == 8) wp.averageMaxRgb = readRationalNumerator(value);
                                        break;
                                case kTagDistributionMaxRGBPercentages: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && es == 1) {
                                                wp.distribution.resize(c);
                                                for (uint32_t i = 0; i < c; ++i) {
                                                        wp.distribution[i].percentage = value[8 + i];
                                                }
                                        }
                                        break;
                                }
                                case kTagDistributionMaxRGBPercentiles: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && es == 8) {
                                                if (wp.distribution.size() != c) wp.distribution.resize(c);
                                                for (uint32_t i = 0; i < c; ++i) {
                                                        wp.distribution[i].percentile =
                                                                readRationalNumerator(value + 8 + i * 8);
                                                }
                                        }
                                        break;
                                }
                                case kTagFractionBrightPixels:
                                        if (len == 8) {
                                                wp.fractionBrightPixels =
                                                        static_cast<uint16_t>(readRationalNumerator(value));
                                        }
                                        break;
                                case kTagKneePoint: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && c == 2 && es == 8) {
                                                wp.hasToneMapping = true;
                                                wp.toneMapping.kneePointX =
                                                        static_cast<uint16_t>(readRationalNumerator(value + 8));
                                                wp.toneMapping.kneePointY =
                                                        static_cast<uint16_t>(readRationalNumerator(value + 16));
                                        }
                                        break;
                                }
                                case kTagBezierCurveAnchors: {
                                        uint32_t c = 0, es = 0;
                                        if (readArrayHeader(value, len, c, es) && es == 8) {
                                                wp.hasToneMapping = true;
                                                wp.toneMapping.bezierCurveAnchors.resize(c);
                                                for (uint32_t i = 0; i < c; ++i) {
                                                        wp.toneMapping.bezierCurveAnchors[i] =
                                                                static_cast<uint16_t>(
                                                                        readRationalNumerator(value + 8 + i * 8));
                                                }
                                        }
                                        break;
                                }
                                case kTagColorSaturationWeight:
                                        if (len == 8) {
                                                wp.hasColorSaturationMapping = true;
                                                wp.colorSaturationWeight =
                                                        static_cast<uint8_t>(readRationalNumerator(value));
                                        }
                                        break;
                                default:
                                        // Skip unknown / unsupported tags (TimeInterval, etc.)
                                        break;
                        }
                        idx += len;
                }

                if (!haveWindowIndex) return false;
                if (windowIndex >= HdrDynamic2094_40::MaxWindows) return false;

                // Extend the value type to cover @p windowIndex if needed.
                if (windowIndex + 1 > md.numWindows()) md.setNumWindows(windowIndex + 1);

                md.windowProcessing()[windowIndex] = wp;
                if (windowIndex >= 1 && haveWindowGeometry) {
                        const size_t extraIdx = static_cast<size_t>(windowIndex - 1);
                        if (extraIdx < md.extraWindows().size()) md.extraWindows()[extraIdx] = win;
                }
                return true;
        }

        // Multi-packet parser: receives the full set of ST 291 packets
        // (in capture order) that share DID=0x41 / SDID=0x0D within one
        // AncPayload.  Validates the Packet Count sequence, concatenates
        // the per-packet Message-bytes into the full HDR/WCG Metadata
        // Message, then walks the KLV frames inside.
        AncTranslator::ParseResult parseHdrDynamicSt291(const AncPacket::List &pkts,
                                              const AncTranslateConfig &cfg) {
                if (pkts.isEmpty()) return makeError<Variant>(Error::InvalidArgument);
                // ST 2108-2 §5.3: Packet Count is u8 (1..255), so a
                // Message cannot span more than 255 ANC packets.
                // Reject up front instead of wrapping uint8_t(256) → 0
                // in the sequence validator below.
                if (pkts.size() > 255) {
                        promekiWarn("anccodec_hdrdynamic2094_40_st291: parseGroup received "
                                    "%zu packets but ST 2108-2 §5.3 caps Packet Count at 255",
                                    pkts.size());
                        return makeError<Variant>(Error::OutOfRange);
                }

                // Step 1: extract (Packet Count, message-bytes) from every
                // packet.  Packet Count is the first UDW; the remaining
                // UDW bytes are the Message-bytes slice for that packet.
                struct Segment {
                                uint8_t       packetCount = 0;
                                List<uint8_t> bytes;
                };
                List<Segment> segs;
                segs.resize(pkts.size());
                AncChecksumPolicy policy = cfg.checksumPolicy();
                for (size_t i = 0; i < pkts.size(); ++i) {
                        Result<St291Packet> rp = St291Packet::from(pkts[i], policy);
                        if (rp.second().isError()) return makeError<Variant>(rp.second());
                        List<uint16_t> udw = rp.first().udw();
                        if (udw.isEmpty()) return makeError<Variant>(Error::CorruptData);
                        segs[i].packetCount = static_cast<uint8_t>(udw[0] & 0xFF);
                        segs[i].bytes.resize(udw.size() - 1);
                        for (size_t j = 1; j < udw.size(); ++j) {
                                segs[i].bytes[j - 1] = static_cast<uint8_t>(udw[j] & 0xFF);
                        }
                }

                // Step 2: sort segments by Packet Count.  Hardware ought
                // to deliver them in order, but the spec allows any
                // ordering as long as Packet Count is monotonic across
                // sequence numbers, so re-sort defensively.
                for (size_t i = 1; i < segs.size(); ++i) {
                        for (size_t j = i; j > 0; --j) {
                                if (segs[j].packetCount < segs[j - 1].packetCount) {
                                        Segment tmp = std::move(segs[j]);
                                        segs[j] = std::move(segs[j - 1]);
                                        segs[j - 1] = std::move(tmp);
                                } else {
                                        break;
                                }
                        }
                }

                // Step 3: validate the Packet Count sequence — must begin
                // at 1 and increment without gaps.
                for (size_t i = 0; i < segs.size(); ++i) {
                        if (segs[i].packetCount != static_cast<uint8_t>(i + 1)) {
                                return makeError<Variant>(Error::CorruptData);
                        }
                }

                // Step 4: concatenate Message-bytes across segments.
                List<uint8_t> bytes;
                size_t        total = 0;
                for (const Segment &s : segs) total += s.bytes.size();
                bytes.reserve(total);
                for (const Segment &s : segs) {
                        for (size_t i = 0; i < s.bytes.size(); ++i) bytes.pushToBack(s.bytes[i]);
                }
                if (bytes.size() < 2) return makeError<Variant>(Error::CorruptData);

                // Step 5: peel off the Message Length header and walk the
                // KLV frames.
                const uint16_t msgLen = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
                if (static_cast<size_t>(msgLen) + 2 > bytes.size()) {
                        return makeError<Variant>(Error::CorruptData);
                }
                const uint8_t *frames = bytes.data() + 2;
                const size_t   framesSize = msgLen;

                HdrDynamic2094_40 md;
                md.setNumWindows(1); // Will grow if we see a higher WindowNumber.

                size_t idx = 0;
                bool   sawAnyApp4 = false;
                while (idx + 16 + 1 <= framesSize) {
                        const uint8_t *key = frames + idx;
                        idx += 16;
                        bool     ok = true;
                        uint32_t valLen = readBerLength(frames, framesSize, idx, ok);
                        if (!ok || idx + valLen > framesSize) return makeError<Variant>(Error::CorruptData);
                        const uint8_t *val = frames + idx;
                        idx += valLen;

                        if (isApp4SetKey(key)) {
                                if (parseDmcvtApp4SetValue(val, valLen, md)) sawAnyApp4 = true;
                        }
                        // Other frame keys (MD Color Volume, Maximum Light Level, App 1/2/3)
                        // are skipped — this codec specifically surfaces HDR10+ (App 4).
                }

                if (!sawAnyApp4) return makeError<Variant>(Error::NotSupported);
                return makeResult<Variant>(Variant(md));
        }

        // HDR10+ dynamic metadata is per-frame, but on a frame held over
        // multiple output slots the receiver wants the same per-window
        // statistics as the originally-captured frame — repeating the
        // packet preserves exactly that.  On Drop we lose one sample;
        // the next surviving frame's metadata re-establishes scene state.
        //
        // Note: this codec is registered as a MultiParser (Messages can
        // span multiple ANC packets via incrementing Packet Count), but
        // SyncPolicy operates per-packet.  Each constituent packet copies
        // through individually, so the multi-packet structure is
        // preserved bit-for-bit on Repeat — the receiver re-aggregates
        // from the Packet Count bytes already in the wire payload.
        //
        // ST 2108-2 §5.3 defines Packet Count as **intra-Message**
        // sequencing (resets to 1 for each new Message), so re-emitting
        // the same Message across a Repeat run does not produce a
        // sequence violation; both Repeat instances carry identical
        // Packet Count bytes (1, 2, ..., N) and parse to the same
        // Message.  Receivers that track Message-level identity for
        // deduplication see two identical Messages and either de-dup or
        // accept both — either way the wire is conformant.
        AncTranslator::PacketsResult syncPolicyHdrDynamic2094_40(const AncPacket &pkt, FrameSyncDisposition d,
                                                             uint8_t /*repeatIndex*/,
                                                             const AncTranslateConfig & /*cfg*/) {
                AncPacket::List out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
        }

} // namespace

PROMEKI_NAMESPACE_END

PROMEKI_REGISTER_ANC_MULTI_PARSER(HdrDynamic2094_40_St291, HdrDynamic2094_40,
                                   ::promeki::AncTransport::St291,
                                   ::promeki::parseHdrDynamicSt291)
PROMEKI_REGISTER_ANC_BUILDER(HdrDynamic2094_40_St291, HdrDynamic2094_40, ::promeki::AncTransport::St291,
                              ::promeki::buildHdrDynamicSt291)
PROMEKI_REGISTER_ANC_SYNC_POLICY(HdrDynamic2094_40, HdrDynamic2094_40,
                                  ::promeki::syncPolicyHdrDynamic2094_40)
