/**
 * @file      enums_video.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Video scan mode, connector, SDI/HDMI, and reference enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Well-known Enum type for progressive / interlaced video scan mode.
 *
 * Describes how the rows of a single @ref ImageDesc are temporally
 * arranged.  Replaces the earlier bare @c bool interlaced flag with a
 * richer state that distinguishes progressive, interlaced with unknown
 * field order, even-field-first interlaced, and odd-field-first
 * interlaced content — which matters for field-aware deinterlacers,
 * SDI receivers that hand back raw fields, and metadata round-trips
 * through QuickTime / MXF containers.
 *
 * - @c Unknown             — scan mode is not specified (legacy /
 *                            unassigned default).
 * - @c Progressive         — all rows belong to the same temporal
 *                            sample; no field separation.
 * - @c Interlaced          — interlaced content with an unspecified
 *                            field order.  Used when the source
 *                            flagged the stream as interlaced but
 *                            didn't carry a reliable dominance
 *                            indicator — common with legacy DV, some
 *                            MXF variants, and raw SDI captures that
 *                            lost the container-level flag.
 * - @c InterlacedEvenFirst — interlaced with the even (top) field
 *                            first in time (NTSC / 480i, 1080i50,
 *                            1080i59.94 per SMPTE 274M).
 * - @c InterlacedOddFirst  — interlaced with the odd (bottom) field
 *                            first in time (PAL / 576i legacy, some
 *                            consumer DV variants).
 * - @c PsF                 — Progressive segmented Frame: a
 *                            progressive image carried as two fields
 *                            over an interlaced transport (common for
 *                            24p / 25p / 30p material on HD-SDI at
 *                            1080psf23.98, 1080psf24, 1080psf25,
 *                            1080psf29.97).  For display purposes it
 *                            behaves as progressive, but the wire
 *                            format is two-field.
 */
class VideoScanMode : public TypedEnum<VideoScanMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoScanMode", "Video Scan Mode", 0,
                                                   {"Unknown", 0, "Unknown"},
                                                   {"Progressive", 1, "Progressive"},
                                                   {"Interlaced", 2, "Interlaced (Unknown Field Order)"},
                                                   {"InterlacedEvenFirst", 3, "Interlaced, Even/Top Field First"},
                                                   {"InterlacedOddFirst", 4, "Interlaced, Odd/Bottom Field First"},
                                                   {"PsF", 5, "Progressive Segmented Frame (PsF)"}); // default: Unknown

                using TypedEnum<VideoScanMode>::TypedEnum;

                static const VideoScanMode Unknown;
                static const VideoScanMode Progressive;
                static const VideoScanMode Interlaced;
                static const VideoScanMode InterlacedEvenFirst;
                static const VideoScanMode InterlacedOddFirst;
                static const VideoScanMode PsF;

                /**
                 * @brief Returns true if this scan mode represents an
                 * interlaced (two-field) raster &mdash; @c VideoScanMode::Interlaced,
                 * @c VideoScanMode::InterlacedEvenFirst, or @c VideoScanMode::InterlacedOddFirst.
                 * @c VideoScanMode::PsF is @em not considered interlaced by this
                 * helper: its wire format is interlaced but its
                 * content (and coded bitstream, when packed) is
                 * progressive.
                 */
                bool isInterlaced() const {
                        const int v = value();
                        return v == 2    /*Interlaced*/
                               || v == 3 /*InterlacedEvenFirst*/
                               || v == 4 /*InterlacedOddFirst*/;
                }
};

inline const VideoScanMode VideoScanMode::Unknown{0};
inline const VideoScanMode VideoScanMode::Progressive{1};
inline const VideoScanMode VideoScanMode::Interlaced{2};
inline const VideoScanMode VideoScanMode::InterlacedEvenFirst{3};
inline const VideoScanMode VideoScanMode::InterlacedOddFirst{4};
inline const VideoScanMode VideoScanMode::PsF{5};

/**
 * @brief Physical connector kind on a video device.
 *
 * Identifies the family of the physical socket a video signal enters
 * or leaves through, independent of the link standard (SDI cable
 * count, HDMI spec level, etc.) running on the wire.  Paired with a
 * 1-based connector index on @ref VideoPortRef to name "the second
 * SDI input" or "the HDMI output" on a device.
 *
 * - @c Auto         — unspecified / defer to the backend.
 * - @c Sdi          — coaxial SDI BNC connector (SD/HD/3G/6G/12G).
 * - @c Hdmi         — Type-A HDMI connector.
 * - @c DisplayPort  — Standard or Mini DisplayPort.
 * - @c Composite    — analog composite (NTSC / PAL / SECAM).
 * - @c Component    — analog YPbPr / RGsB component video.
 * - @c SVideo       — analog S-Video (Y/C 4-pin mini-DIN).
 * - @c Sfp          — SFP / SFP+ cage carrying SDI-over-IP or
 *                     ST 2022-6 / ST 2110-20 traffic.
 */
class VideoConnectorKind : public TypedEnum<VideoConnectorKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoConnectorKind", "Video Connector Kind", 0,
                                                   {"Auto", 0, "Automatic"},
                                                   {"Sdi", 1, "SDI (BNC Coax)"},
                                                   {"Hdmi", 2, "HDMI (Type-A)"},
                                                   {"DisplayPort", 3, "DisplayPort"},
                                                   {"Composite", 4, "Composite (Analog)"},
                                                   {"Component", 5, "Component (Analog YPbPr)"},
                                                   {"SVideo", 6, "S-Video (Y/C)"},
                                                   {"Sfp", 7, "SFP/SFP+ (SDI-over-IP)"}); // default: Auto

                using TypedEnum<VideoConnectorKind>::TypedEnum;

                static const VideoConnectorKind Auto;
                static const VideoConnectorKind Sdi;
                static const VideoConnectorKind Hdmi;
                static const VideoConnectorKind DisplayPort;
                static const VideoConnectorKind Composite;
                static const VideoConnectorKind Component;
                static const VideoConnectorKind SVideo;
                static const VideoConnectorKind Sfp;
};

inline const VideoConnectorKind VideoConnectorKind::Auto{0};
inline const VideoConnectorKind VideoConnectorKind::Sdi{1};
inline const VideoConnectorKind VideoConnectorKind::Hdmi{2};
inline const VideoConnectorKind VideoConnectorKind::DisplayPort{3};
inline const VideoConnectorKind VideoConnectorKind::Composite{4};
inline const VideoConnectorKind VideoConnectorKind::Component{5};
inline const VideoConnectorKind VideoConnectorKind::SVideo{6};
inline const VideoConnectorKind VideoConnectorKind::Sfp{7};

/**
 * @brief Link standard for an SDI signal carrier.
 *
 * Short identifiers spell out the link topology and the rate/standard
 * fragment: @c SL_ for single-link, @c DL_ for dual-link, @c QL_ for
 * quad-link.  The trailing fragment names the SMPTE family the
 * standard belongs to (HD / 3GA / 3GB / 3G / 6G / 12G / 24G) and, for
 * the quad-link variants, the sub-image mapping (Square Division vs.
 * 2-Sample Interleave).  The Doxygen description for each value
 * carries the underlying SMPTE document number.
 *
 * - @c Auto       — defer to the backend / source.
 * - @c SL_SD      — SD-SDI single-link (SMPTE ST 259), 270 Mbps.
 * - @c SL_HD      — HD-SDI single-link (SMPTE ST 292), 1.485 Gbps.
 * - @c DL_HD      — HD-SDI dual-link (SMPTE ST 372), 2 × 1.485 Gbps.
 * - @c SL_3GA     — 3G-SDI single-link Level A (SMPTE ST 425-1), 2.97 Gbps.
 * - @c SL_3GB     — 3G-SDI single-link Level B carrying two HD streams.
 * - @c DL_3GB     — 3G-SDI Level B mapped onto two physical links
 *                   (one logical stream split across two cables).
 * - @c DL_3G      — Dual-link 3G-SDI (SMPTE ST 425-2), 2 × 2.97 Gbps.
 * - @c QL_3G_SQD  — Quad-link 3G-SDI Square Division (SMPTE ST 425-3).
 * - @c QL_3G_2SI  — Quad-link 3G-SDI 2-Sample Interleave (SMPTE ST 425-5).
 * - @c SL_6G      — 6G-SDI single-link (SMPTE ST 2081), 5.94 Gbps.
 * - @c SL_12G     — 12G-SDI single-link (SMPTE ST 2082), 11.88 Gbps.
 * - @c SL_24G     — 24G-SDI single-link (SMPTE ST 2083), 23.76 Gbps.
 */
class SdiLinkStandard : public TypedEnum<SdiLinkStandard> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SdiLinkStandard", "SDI Link Standard", 0,
                                                   {"Auto",       0, "Automatic"},
                                                   {"SL_SD",      1, "Single-Link SD-SDI (ST 259)"},
                                                   {"SL_HD",      2, "Single-Link HD-SDI (ST 292)"},
                                                   {"DL_HD",      3, "Dual-Link HD-SDI (ST 372)"},
                                                   {"SL_3GA",     4, "Single-Link 3G-SDI Level A (ST 425-1)"},
                                                   {"SL_3GB",     5, "Single-Link 3G-SDI Level B (ST 425-1)"},
                                                   {"DL_3GB",     6, "Dual-Link 3G-SDI Level B"},
                                                   {"DL_3G",      7, "Dual-Link 3G-SDI (ST 425-2)"},
                                                   {"QL_3G_SQD",  8, "Quad-Link 3G-SDI, Square Division (ST 425-3)"},
                                                   {"QL_3G_2SI",  9, "Quad-Link 3G-SDI, 2-Sample Interleave (ST 425-5)"},
                                                   {"SL_6G",     10, "Single-Link 6G-SDI (ST 2081)"},
                                                   {"SL_12G",    11, "Single-Link 12G-SDI (ST 2082)"},
                                                   {"SL_24G",    12, "Single-Link 24G-SDI (ST 2083)"}); // default: Auto

                using TypedEnum<SdiLinkStandard>::TypedEnum;

                static const SdiLinkStandard Auto;
                static const SdiLinkStandard SL_SD;
                static const SdiLinkStandard SL_HD;
                static const SdiLinkStandard DL_HD;
                static const SdiLinkStandard SL_3GA;
                static const SdiLinkStandard SL_3GB;
                static const SdiLinkStandard DL_3GB;
                static const SdiLinkStandard DL_3G;
                static const SdiLinkStandard QL_3G_SQD;
                static const SdiLinkStandard QL_3G_2SI;
                static const SdiLinkStandard SL_6G;
                static const SdiLinkStandard SL_12G;
                static const SdiLinkStandard SL_24G;
};

inline const SdiLinkStandard SdiLinkStandard::Auto{0};
inline const SdiLinkStandard SdiLinkStandard::SL_SD{1};
inline const SdiLinkStandard SdiLinkStandard::SL_HD{2};
inline const SdiLinkStandard SdiLinkStandard::DL_HD{3};
inline const SdiLinkStandard SdiLinkStandard::SL_3GA{4};
inline const SdiLinkStandard SdiLinkStandard::SL_3GB{5};
inline const SdiLinkStandard SdiLinkStandard::DL_3GB{6};
inline const SdiLinkStandard SdiLinkStandard::DL_3G{7};
inline const SdiLinkStandard SdiLinkStandard::QL_3G_SQD{8};
inline const SdiLinkStandard SdiLinkStandard::QL_3G_2SI{9};
inline const SdiLinkStandard SdiLinkStandard::SL_6G{10};
inline const SdiLinkStandard SdiLinkStandard::SL_12G{11};
inline const SdiLinkStandard SdiLinkStandard::SL_24G{12};

/**
 * @brief Canonical SMPTE SDI wire-payload formats.
 *
 * Names the discrete bit-depth + sampling combinations the SDI spec
 * family standardises as on-the-wire payloads — orthogonal to the
 * @ref SdiLinkStandard (which says how many cables and at what rate)
 * and to the @ref PixelFormat (which describes framebuffer
 * memory layout, including padding and packing that does not survive
 * the framestore↔wire boundary).
 *
 * Use @ref sdiBitsPerPixel (in @c sdistandards.h) to get the
 * intrinsic per-pixel bit count for wire-bandwidth math, and
 * @ref sdiWireFormatFor (in @c sdiwireinference.h) to map a
 * framebuffer @ref PixelFormat to the wire format that naturally
 * carries it after the on-board pack/unpack step.
 *
 * - @c Auto         — unspecified.  Wire format is whatever the
 *                     standard / backend defaults to.  Backends
 *                     typically substitute @c YCbCr_422_10 for
 *                     single-link SDI when this is set.
 * - @c YCbCr_422_10 — 10-bit Y'CbCr 4:2:2.  The canonical SDI
 *                     payload — single-link SD / HD / 3G / 6G /
 *                     12G / 24G all carry this natively.
 * - @c YCbCr_422_12 — 12-bit Y'CbCr 4:2:2.  Higher-bit-depth
 *                     payload — single-link variants on 6G+ or
 *                     dual-link @c DL_3G.
 * - @c YCbCr_444_10 — 10-bit Y'CbCr 4:4:4.  Dual-link / 12G+
 *                     payload — full chroma, no subsampling.
 * - @c YCbCr_444_12 — 12-bit Y'CbCr 4:4:4.
 * - @c RGB_444_10   — 10-bit R'G'B' 4:4:4.  Dual-link / 12G+
 *                     payload for RGB-native production paths.
 * - @c RGB_444_12   — 12-bit R'G'B' 4:4:4.
 * - @c RGBA_444_10  — 10-bit R'G'B'A' 4:4:4:4.  RGB with key
 *                     alpha; used by some fill/key SDI pipelines.
 *
 * @note No @c RGBA_444_12 entry exists.  The SDI spec family does
 *       not standardise 12-bit RGBA as a single-link wire payload —
 *       12-bit production paths that need a key channel typically
 *       carry @c RGB_444_12 on one link and the alpha on a parallel
 *       link (fill/key pair) or as a sidecar.  Use @c RGB_444_12
 *       for the RGB component and route the alpha separately.
 */
class SdiWireFormat : public TypedEnum<SdiWireFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SdiWireFormat", "SDI Wire Format", 0,
                                                   {"Auto",         0, "Automatic"},
                                                   {"YCbCr_422_10", 1, "Y'CbCr 4:2:2, 10-bit"},
                                                   {"YCbCr_422_12", 2, "Y'CbCr 4:2:2, 12-bit"},
                                                   {"YCbCr_444_10", 3, "Y'CbCr 4:4:4, 10-bit"},
                                                   {"YCbCr_444_12", 4, "Y'CbCr 4:4:4, 12-bit"},
                                                   {"RGB_444_10",   5, "R'G'B' 4:4:4, 10-bit"},
                                                   {"RGB_444_12",   6, "R'G'B' 4:4:4, 12-bit"},
                                                   {"RGBA_444_10",  7, "R'G'B'A' 4:4:4:4, 10-bit"}); // default: Auto

                using TypedEnum<SdiWireFormat>::TypedEnum;

                static const SdiWireFormat Auto;
                static const SdiWireFormat YCbCr_422_10;
                static const SdiWireFormat YCbCr_422_12;
                static const SdiWireFormat YCbCr_444_10;
                static const SdiWireFormat YCbCr_444_12;
                static const SdiWireFormat RGB_444_10;
                static const SdiWireFormat RGB_444_12;
                static const SdiWireFormat RGBA_444_10;
};

inline const SdiWireFormat SdiWireFormat::Auto{0};
inline const SdiWireFormat SdiWireFormat::YCbCr_422_10{1};
inline const SdiWireFormat SdiWireFormat::YCbCr_422_12{2};
inline const SdiWireFormat SdiWireFormat::YCbCr_444_10{3};
inline const SdiWireFormat SdiWireFormat::YCbCr_444_12{4};
inline const SdiWireFormat SdiWireFormat::RGB_444_10{5};
inline const SdiWireFormat SdiWireFormat::RGB_444_12{6};
inline const SdiWireFormat SdiWireFormat::RGBA_444_10{7};

/**
 * @brief HDMI specification version hint for an HDMI signal carrier.
 *
 * Tracks the version of the HDMI / CTA spec the source / sink is
 * announcing (or negotiated to).  Used as a hint on
 * @ref HdmiSignalConfig — the on-wire bandwidth is dictated by the
 * @ref VideoFormat in play; the version hint tells the backend
 * which feature subset (HDR static / dynamic metadata, ALLM, eARC,
 * FRL vs. TMDS, …) to advertise.
 *
 * - @c Auto    — defer to the device's EDID / capability discovery.
 * - @c Hdmi14  — HDMI 1.4b feature set (max 8.16 Gbps TMDS).
 * - @c Hdmi20  — HDMI 2.0/2.0b feature set (max 17.82 Gbps TMDS).
 * - @c Hdmi21  — HDMI 2.1 feature set (FRL up to 48 Gbps,
 *                Dynamic HDR, ALLM, VRR, eARC, …).
 */
class HdmiSpecVersion : public TypedEnum<HdmiSpecVersion> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("HdmiSpecVersion", "HDMI Specification Version", 0,
                                                   {"Auto",   0, "Automatic (EDID)"},
                                                   {"Hdmi14", 1, "HDMI 1.4b"},
                                                   {"Hdmi20", 2, "HDMI 2.0/2.0b"},
                                                   {"Hdmi21", 3, "HDMI 2.1"}); // default: Auto

                using TypedEnum<HdmiSpecVersion>::TypedEnum;

                static const HdmiSpecVersion Auto;
                static const HdmiSpecVersion Hdmi14;
                static const HdmiSpecVersion Hdmi20;
                static const HdmiSpecVersion Hdmi21;
};

inline const HdmiSpecVersion HdmiSpecVersion::Auto{0};
inline const HdmiSpecVersion HdmiSpecVersion::Hdmi14{1};
inline const HdmiSpecVersion HdmiSpecVersion::Hdmi20{2};
inline const HdmiSpecVersion HdmiSpecVersion::Hdmi21{3};

/**
 * @brief Source of a device's reference clock.
 *
 * Names the origin of the timing the device locks its outputs to.
 * Stored on @ref VideoReferenceConfig along with the rate family and
 * (when @c FromSignal) the input port the lock is sourced from.
 *
 * - @c FreeRun     — no external reference; the device generates its
 *                    own clock from a local oscillator.
 * - @c Genlock     — lock to a black-burst / tri-level reference
 *                    arriving on the device's dedicated REF / GENLOCK
 *                    BNC input.
 * - @c External    — lock to a generic external reference input
 *                    whose semantics the backend interprets.
 * - @c FromSignal  — lock to the signal arriving on one of the
 *                    device's own connectors (named by
 *                    @ref VideoReferenceConfig::signalPort).
 * - @c Ptp         — lock to a PTP / IEEE 1588 grandmaster (future).
 * - @c Word        — lock to a word-clock input (future).
 */
class VideoReferenceSource : public TypedEnum<VideoReferenceSource> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoReferenceSource", "Video Reference Source", 0,
                                                   {"FreeRun",    0, "Free Run (Internal)"},
                                                   {"Genlock",    1, "Genlock (Black Burst / Tri-Level)"},
                                                   {"External",   2, "External Reference"},
                                                   {"FromSignal", 3, "Lock to Input Signal"},
                                                   {"Ptp",        4, "PTP Grandmaster (IEEE 1588)"},
                                                   {"Word",       5, "Word Clock"}); // default: FreeRun

                using TypedEnum<VideoReferenceSource>::TypedEnum;

                static const VideoReferenceSource FreeRun;
                static const VideoReferenceSource Genlock;
                static const VideoReferenceSource External;
                static const VideoReferenceSource FromSignal;
                static const VideoReferenceSource Ptp;
                static const VideoReferenceSource Word;
};

inline const VideoReferenceSource VideoReferenceSource::FreeRun{0};
inline const VideoReferenceSource VideoReferenceSource::Genlock{1};
inline const VideoReferenceSource VideoReferenceSource::External{2};
inline const VideoReferenceSource VideoReferenceSource::FromSignal{3};
inline const VideoReferenceSource VideoReferenceSource::Ptp{4};
inline const VideoReferenceSource VideoReferenceSource::Word{5};

/**
 * @brief Rate family for a video reference clock.
 *
 * SDI / HDMI reference clocks come in two families derived from the
 * 148.5 MHz master oscillator: the integer-Hz family (24 / 25 / 30
 * / 50 / 60 fps) clocked at 148.5 MHz exactly, and the NTSC-derived
 * fractional family (23.976 / 29.97 / 59.94 fps) clocked at
 * 148.5 / 1.001 MHz.  The family pins down which lattice the device
 * generates; the actual frame rate within that family is supplied
 * by @ref MediaConfig::VideoFormat / @ref FrameRate.
 *
 * - @c Auto        — defer to the negotiated @ref VideoFormat.
 * - @c Integer     — 148.5 MHz family (24 / 25 / 30 / 50 / 60).
 * - @c Fractional  — 148.5/1.001 MHz family (23.976 / 29.97 / 59.94).
 */
class VideoReferenceRateFamily : public TypedEnum<VideoReferenceRateFamily> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoReferenceRateFamily", "Video Reference Rate Family", 0,
                                                   {"Auto",       0, "Automatic"},
                                                   {"Integer",    1, "Integer Rate (148.5 MHz)"},
                                                   {"Fractional", 2, "NTSC Fractional (148.5/1.001 MHz)"}); // default: Auto

                using TypedEnum<VideoReferenceRateFamily>::TypedEnum;

                static const VideoReferenceRateFamily Auto;
                static const VideoReferenceRateFamily Integer;
                static const VideoReferenceRateFamily Fractional;
};

inline const VideoReferenceRateFamily VideoReferenceRateFamily::Auto{0};
inline const VideoReferenceRateFamily VideoReferenceRateFamily::Integer{1};
inline const VideoReferenceRateFamily VideoReferenceRateFamily::Fractional{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
