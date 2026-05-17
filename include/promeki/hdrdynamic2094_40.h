/**
 * @file      hdrdynamic2094_40.h
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

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief SMPTE ST 2094-40 dynamic HDR metadata (HDR10+).
 * @ingroup proav
 *
 * Structural model of one frame's worth of HDR10+ dynamic metadata
 * exactly as defined by the SMPTE ST 2094-40 metadata syntax: one
 * targeted display max-luminance, optional targeted-display actual
 * peak-luminance map, then one @ref WindowProcessing entry per
 * sub-image window with per-window MaxSCL / AverageMaxRGB / MaxRGB
 * distribution percentiles / fraction-bright-pixels, optional Bezier
 * tone-mapping curve, and optional colour-saturation weighting.
 *
 * @par Wire form — canonical ST 2094-40 bitstream
 *
 * @ref toBuffer / @ref fromBuffer transcribe the ST 2094-40 §6.1
 * metadata syntax into / out of a packed MSB-first bitstream:
 *
 * @code
 * application_version                                  u(8)
 * num_windows                                          u(2)
 * for (w = 1; w < num_windows; w++) {
 *     window_upper_left_corner_x                       u(16)
 *     window_upper_left_corner_y                       u(16)
 *     window_lower_right_corner_x                      u(16)
 *     window_lower_right_corner_y                      u(16)
 *     center_of_ellipse_x                              u(16)
 *     center_of_ellipse_y                              u(16)
 *     rotation_angle                                   u(8)
 *     semimajor_axis_internal_ellipse                  u(16)
 *     semimajor_axis_external_ellipse                  u(16)
 *     semiminor_axis_external_ellipse                  u(16)
 *     overlap_process_option                           u(1)
 * }
 * targeted_system_display_maximum_luminance            u(27)
 * targeted_system_display_actual_peak_luminance_flag   u(1)
 * if (..._flag == 1) {
 *     num_rows                                         u(5)
 *     num_cols                                         u(5)
 *     for (i,j) targeted_..._actual_peak_luminance     u(4)
 * }
 * for (w = 0; w < num_windows; w++) {
 *     for (i = 0; i < 3; i++) maxscl[w][i]             u(17)
 *     average_maxrgb[w]                                u(17)
 *     num_distribution_maxrgb_percentiles[w]           u(4)
 *     for (i = 0; i < ...; i++) {
 *         distribution_maxrgb_percentages[w][i]        u(7)
 *         distribution_maxrgb_percentiles[w][i]        u(17)
 *     }
 *     fraction_bright_pixels[w]                        u(10)
 * }
 * mastering_display_actual_peak_luminance_flag         u(1)
 * if (..._flag == 1) {
 *     num_rows                                         u(5)
 *     num_cols                                         u(5)
 *     for (i,j) mastering_..._actual_peak_luminance    u(4)
 * }
 * for (w = 0; w < num_windows; w++) {
 *     tone_mapping_flag[w]                             u(1)
 *     if (tone_mapping_flag[w] == 1) {
 *         knee_point_x[w]                              u(12)
 *         knee_point_y[w]                              u(12)
 *         num_bezier_curve_anchors[w]                  u(4)
 *         for (i = 0; ...; i++) bezier_curve_anchors   u(10)
 *     }
 *     color_saturation_mapping_flag[w]                 u(1)
 *     if (..._flag == 1)
 *         color_saturation_weight[w]                   u(6)
 * }
 * // Zero-bit pad to next byte boundary.
 * @endcode
 *
 * The bitstream is identical to the @c registered_user_data_payload
 * body HEVC carries as HDR10+ SEI (less the four-byte
 * @c itu_t_t35_terminal_provider_code +
 * @c itu_t_t35_terminal_provider_oriented_code +
 * @c application_identifier framing that the SEI wrapper prepends
 * — see @c AncFormat::HdrDynamic2094_40 codecs for the per-transport
 * envelopes).
 *
 * @par HDMI VSIF carriage
 *
 * On @c AncTransport::HdmiInfoFrame the body is a 3-byte HDR10+
 * OUI (@c 0x90-84-8B, wire order @c 0x8B,0x84,0x90 — see
 * @ref HdrPlusOui) followed by the canonical ST 2094-40 bitstream
 * above.  The codec uses InfoFrame Type @c 0x81 (Vendor-Specific)
 * and Version @c 1.
 *
 * @par SDI carriage
 *
 * SMPTE ST 2108-2 SDI carriage is registered (Phase 3 of
 * @c devplan/proav/ancdata.md) but the per-byte layout requires the
 * SMPTE-published spec values; this header pre-declares the format
 * via @c AncFormat::HdrDynamic2094_40 so the St291 codec lands as a
 * drop-in once the DID/SDID + body layout is locked down.
 *
 * @par Variant integration
 *
 * Registered as @c DataTypeHdrDynamic2094_40 so @ref
 * AncTranslator parse / build functions return / consume it through
 * their @c Result<Variant> interfaces.
 *
 * @par Thread Safety
 * Plain value type — copies are independent.  Distinct instances may
 * be used concurrently; concurrent access to a single instance is not
 * internally synchronised.
 *
 * @see HdrStaticMetadata, AncFormat::HdrDynamic2094_40, HdmiInfoFrame,
 *      AncTranslator
 */
class HdrDynamic2094_40 {
        public:
                PROMEKI_DATATYPE(HdrDynamic2094_40, DataTypeHdrDynamic2094_40, 1)

                /** @brief Maximum @c num_windows value (u(2) wire field). */
                static constexpr uint8_t MaxWindows = 3;

                /** @brief Maximum @c num_distribution_maxrgb_percentiles per window (u(4)). */
                static constexpr uint8_t MaxDistributionPercentiles = 15;

                /** @brief Maximum @c num_bezier_curve_anchors per window (u(4)). */
                static constexpr uint8_t MaxBezierCurveAnchors = 15;

                /** @brief HDR10+ HDMI VSIF InfoFrame Type byte. */
                static constexpr uint8_t InfoFrameType = 0x81;

                /** @brief HDR10+ HDMI VSIF InfoFrame Version byte. */
                static constexpr uint8_t InfoFrameVersion = 1;

                /**
                 * @brief HDR10+ LLC IEEE OUI (24 bits, MSB-first).
                 *
                 * Wire order in a CEA-861 Vendor-Specific InfoFrame
                 * body is LSB-first: byte 0 = @c 0x8B, byte 1 = @c 0x84,
                 * byte 2 = @c 0x90.
                 */
                static constexpr uint32_t HdrPlusOui = 0x90848B;

                /**
                 * @brief Geometry of one HDR10+ processing window above
                 *        window 0 (the implicit full-image window).
                 *
                 * Wire form mirrors the ST 2094-40 per-window block in
                 * the @c for(w=1; ...) loop.  Unused when @ref numWindows
                 * == 1.
                 */
                struct Window {
                                uint16_t upperLeftCornerX = 0;
                                uint16_t upperLeftCornerY = 0;
                                uint16_t lowerRightCornerX = 0;
                                uint16_t lowerRightCornerY = 0;
                                uint16_t centerOfEllipseX = 0;
                                uint16_t centerOfEllipseY = 0;
                                uint8_t  rotationAngle = 0;
                                uint16_t semimajorAxisInternalEllipse = 0;
                                uint16_t semimajorAxisExternalEllipse = 0;
                                uint16_t semiminorAxisExternalEllipse = 0;
                                bool     overlapProcessOption = false;

                                bool operator==(const Window &o) const;
                                bool operator!=(const Window &o) const { return !(*this == o); }
                };

                /**
                 * @brief Actual-peak-luminance grid (used for both the
                 *        targeted system display and the mastering
                 *        display).
                 *
                 * Each grid cell is a u(4) value; the matrix is
                 * row-major with @c numRows rows and @c numCols
                 * columns.  When @c numRows == 0 or @c numCols == 0
                 * the structure is treated as "not present" by the
                 * encoder.
                 */
                struct ActualPeakLuminance {
                                uint8_t       numRows = 0; ///< u(5) row count.
                                uint8_t       numCols = 0; ///< u(5) column count.
                                List<uint8_t> values;      ///< @c numRows * @c numCols cells, u(4) each.

                                /** @brief @c true when the grid carries any cells. */
                                bool isPresent() const { return numRows > 0 && numCols > 0; }

                                bool operator==(const ActualPeakLuminance &o) const;
                                bool operator!=(const ActualPeakLuminance &o) const { return !(*this == o); }
                };

                /**
                 * @brief One @c (percentage, percentile) pair from the
                 *        per-window MaxRGB distribution.
                 */
                struct DistributionMaxRgb {
                                uint8_t  percentage = 0; ///< u(7), nominal range 0–100.
                                uint32_t percentile = 0; ///< u(17), MaxRGB at @c percentage.

                                bool operator==(const DistributionMaxRgb &o) const {
                                        return percentage == o.percentage && percentile == o.percentile;
                                }
                                bool operator!=(const DistributionMaxRgb &o) const { return !(*this == o); }
                };

                /**
                 * @brief Bezier tone-mapping curve descriptor for one
                 *        window.
                 *
                 * Knee-point + up to @ref MaxBezierCurveAnchors anchor
                 * values.  The number of anchors is the wire's
                 * @c num_bezier_curve_anchors (u(4)).
                 */
                struct ToneMapping {
                                uint16_t       kneePointX = 0;       ///< u(12)
                                uint16_t       kneePointY = 0;       ///< u(12)
                                List<uint16_t> bezierCurveAnchors;   ///< u(10) each, size 0..@ref MaxBezierCurveAnchors.

                                bool operator==(const ToneMapping &o) const;
                                bool operator!=(const ToneMapping &o) const { return !(*this == o); }
                };

                /**
                 * @brief Per-window picture statistics and optional
                 *        tone-mapping + colour-saturation parameters.
                 *
                 * One @c WindowProcessing entry exists per window
                 * (including window 0).  When @ref numWindows == 1
                 * exactly one entry is present.
                 */
                struct WindowProcessing {
                                uint32_t                   maxScl[3] = {0, 0, 0}; ///< u(17) each, R/G/B.
                                uint32_t                   averageMaxRgb = 0;     ///< u(17).
                                List<DistributionMaxRgb>   distribution;          ///< size 0..@ref MaxDistributionPercentiles.
                                uint16_t                   fractionBrightPixels = 0; ///< u(10).

                                bool        hasToneMapping = false; ///< wire @c tone_mapping_flag.
                                ToneMapping toneMapping;            ///< populated when @c hasToneMapping.

                                bool    hasColorSaturationMapping = false; ///< wire @c color_saturation_mapping_flag.
                                uint8_t colorSaturationWeight = 0;         ///< u(6), populated when set.

                                bool operator==(const WindowProcessing &o) const;
                                bool operator!=(const WindowProcessing &o) const { return !(*this == o); }
                };

                /** @brief Default-constructs an empty (single-window, all-zero) descriptor. */
                HdrDynamic2094_40();

                // -- Top-level field accessors -----------------------------

                /** @brief Returns the wire @c application_version (u(8)). */
                uint8_t applicationVersion() const { return _applicationVersion; }
                /** @brief Sets the wire @c application_version (u(8)). */
                void setApplicationVersion(uint8_t v) { _applicationVersion = v; }

                /** @brief Returns @c num_windows (1..3).  Always
                 *  @ref windowProcessing's size and
                 *  @ref extraWindows.size() + 1. */
                uint8_t numWindows() const { return _numWindows; }

                /**
                 * @brief Reshapes the descriptor to @p n windows.
                 *
                 * Resizes @ref windowProcessing to @p n entries and
                 * @ref extraWindows to @p n - 1.  Clamps @p n to
                 * @c [1, MaxWindows].
                 */
                void setNumWindows(uint8_t n);

                /**
                 * @brief Returns the geometry of windows 1..
                 *        @c numWindows-1.
                 *
                 * Window 0 is implicit (covers the full image) and has
                 * no geometry.
                 */
                const List<Window> &extraWindows() const { return _extraWindows; }
                /** @brief Mutable accessor mirroring @ref extraWindows. */
                List<Window> &extraWindows() { return _extraWindows; }

                /**
                 * @brief Returns the targeted system display max
                 *        luminance, wire form (u(27), units of
                 *        0.0001 cd/m² per LSB).
                 */
                uint32_t targetedSystemDisplayMaximumLuminance() const {
                        return _targetedSystemDisplayMaximumLuminance;
                }
                /** @brief Sets @ref targetedSystemDisplayMaximumLuminance. */
                void setTargetedSystemDisplayMaximumLuminance(uint32_t v) {
                        _targetedSystemDisplayMaximumLuminance = v & 0x07FFFFFFu;
                }

                /** @brief Returns the optional targeted-display actual-peak grid. */
                const ActualPeakLuminance &targetedSystemDisplayActualPeakLuminance() const {
                        return _targetedSystemDisplayActualPeakLuminance;
                }
                /** @brief Mutable accessor mirroring @ref targetedSystemDisplayActualPeakLuminance. */
                ActualPeakLuminance &targetedSystemDisplayActualPeakLuminance() {
                        return _targetedSystemDisplayActualPeakLuminance;
                }

                /** @brief Returns the per-window processing entries (size = @ref numWindows). */
                const List<WindowProcessing> &windowProcessing() const { return _windowProcessing; }
                /** @brief Mutable accessor mirroring @ref windowProcessing. */
                List<WindowProcessing> &windowProcessing() { return _windowProcessing; }

                /** @brief Returns the optional mastering-display actual-peak grid. */
                const ActualPeakLuminance &masteringDisplayActualPeakLuminance() const {
                        return _masteringDisplayActualPeakLuminance;
                }
                /** @brief Mutable accessor mirroring @ref masteringDisplayActualPeakLuminance. */
                ActualPeakLuminance &masteringDisplayActualPeakLuminance() {
                        return _masteringDisplayActualPeakLuminance;
                }

                // -- Wire round-trip ---------------------------------------

                /**
                 * @brief Serialises to the canonical ST 2094-40
                 *        bitstream (MSB-first packing, zero-bit pad
                 *        to the next byte boundary).
                 *
                 * Values are masked to their wire bit widths on the
                 * way out — out-of-range integer assignments silently
                 * truncate rather than failing.  An empty descriptor
                 * (default-constructed) round-trips through @ref
                 * fromBuffer to itself.
                 */
                Buffer toBuffer() const;

                /**
                 * @brief Parses a canonical ST 2094-40 bitstream.
                 *
                 * Validates the bit-level field widths and the array
                 * size bounds (@c num_windows ≤ 3,
                 * @c num_distribution_maxrgb_percentiles ≤ 15,
                 * @c num_bezier_curve_anchors ≤ 15).  Returns
                 * @c Error::CorruptData when the input would require
                 * reading past the end of the buffer or any bound
                 * fails.  Bits remaining in the final byte after the
                 * structured fields are ignored (the spec mandates
                 * zero-bit padding but does not assign meaning to the
                 * padding).
                 */
                static Result<HdrDynamic2094_40> fromBuffer(const void *data, size_t size);

                /** @brief Convenience overload accepting a @ref Buffer. */
                static Result<HdrDynamic2094_40> fromBuffer(const Buffer &buf);

                // -- Inspection / debugging --------------------------------

                /**
                 * @brief Produces a JSON representation for inspection.
                 *
                 * Schema mirrors the value type one-to-one with the
                 * wire field names lower-camel-cased (e.g.
                 * @c targetedSystemDisplayMaximumLuminance,
                 * @c windowProcessing[i].toneMapping.kneePointX).
                 * Absent optional structures emit
                 * @c {"present": false}.
                 */
                JsonObject toJson() const;

                /** @brief Field-wise equality. */
                bool operator==(const HdrDynamic2094_40 &o) const;
                /** @brief Inequality. */
                bool operator!=(const HdrDynamic2094_40 &o) const { return !(*this == o); }

                /** @brief Returns a short human-readable summary. */
                String toString() const;

                /**
                 * @brief DataStream body writer for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 *
                 * Wire body: the canonical ST 2094-40 byte stream
                 * (the same bytes @ref toBuffer produces) length-prefixed
                 * as a @ref Buffer.  Round-trips through @ref fromBuffer.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream body reader for the
                 *        @ref PROMEKI_DATATYPE member-API path.
                 */
                template <uint32_t V> static Result<HdrDynamic2094_40> readFromStream(DataStream &s);

        private:
                uint8_t                _applicationVersion = 0;
                uint8_t                _numWindows = 1;
                List<Window>           _extraWindows;
                uint32_t               _targetedSystemDisplayMaximumLuminance = 0;
                ActualPeakLuminance    _targetedSystemDisplayActualPeakLuminance;
                List<WindowProcessing> _windowProcessing;
                ActualPeakLuminance    _masteringDisplayActualPeakLuminance;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
