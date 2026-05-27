/**
 * @file      st2110video.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/pixelformat.h>
#endif
#include <promeki/enums_st2110.h>
#include <promeki/enums_video.h>
#include <promeki/framerate.h>
#include <promeki/map.h>
#include <promeki/namespace.h>
#include <promeki/pixelaspect.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Wire-format primitives for SMPTE ST 2110-20 uncompressed video.
 * @ingroup network
 *
 * Encapsulates the @b pgroup table (ST 2110-20:2022 §6.2 Tables
 * 1-4) and the big-endian bit-packing primitives used by the
 * @ref RtpPayloadRawVideo packetizer.  Everything here is pure data
 * over @c uint8_t buffers — no PixelFormat, no SDP, no MediaIO
 * coupling.  Higher layers (@ref RtpMediaIO / @ref ImageDesc::toSdp)
 * compose this with their own format / signalling logic.
 *
 * The class is a static-only namespace dressed as a class so it can
 * expose nested types (@ref Pgroup) the way @ref RtpPayloadRawVideo
 * exposes its own constants.
 *
 * @par pgroup encoding (§6.1.1)
 *
 * Every pgroup is a contiguous run of samples in network byte order
 * (most-significant bit / byte first).  For each sample the low
 * @c depthBits bits of the source @c uint16_t are emitted in MSB
 * order, packed densely with no inter-sample padding.  Depth-16f
 * samples are passed as their IEEE-754 @c binary16 bit pattern in
 * the @c uint16_t — the codec does not interpret the float value,
 * it copies the 16 bits big-endian.  Depths 8 and 16 are byte
 * aligned and reduce to plain @c memcpy with optional byte swap;
 * depths 10 and 12 carry odd-bit-width packing whose loop is the
 * reason this helper exists.
 *
 * @par Sample order per (sampling, depth)
 *
 * The Sample Order column in §6.2 Tables 1-4 defines which samples
 * the pgroup carries and in what order.  The full canonical orders
 * are:
 *  - **4:4:4 YCbCr / CLYCbCr** — @c (C'B, Y', C'R) per pixel.
 *  - **4:4:4 ICtCp** — @c (CT, I, CP) per pixel.
 *  - **4:4:4 RGB** — @c (R, G, B) per pixel (linear or non-linear;
 *    the wire bits are identical, only the @c TCS attribute
 *    differs).
 *  - **4:4:4 XYZ** — @c (X', Y', Z') per pixel.
 *  - **4:2:2 YCbCr / CLYCbCr** — @c (C'B, Y'0, C'R, Y'1) per pixel
 *    pair.
 *  - **4:2:2 ICtCp** — @c (CT, I0, CP, I1) per pixel pair.
 *  - **4:2:0 YCbCr (8/12-bit)** — @c (Y'00, Y'01, Y'10, Y'11,
 *    C'B00, C'R00) per 4-pixel 2×2 group.
 *  - **4:2:0 YCbCr (10-bit)** — two 4-pixel groups packed together:
 *    @c (Y'00, Y'01, Y'10, Y'11, C'B00, C'R00, Y'02, Y'03, Y'12,
 *    Y'13, C'B01, C'R01) per 8 pixels.
 *  - **Key** — N samples of @c K per pgroup, where N matches the
 *    pixel coverage column (1, 4, 2, or 1 for depths 8, 10, 12, 16
 *    respectively).
 *
 * Callers gather samples in this canonical interleaved order and
 * hand them to @ref packSamplesBE in one contiguous run.  The
 * sampling-aware @ref packPgroup convenience wrapper assumes the
 * caller has already done the gather; it exists purely to compute
 * @c pgroup_octets from @c pgroup_samples × @c depth without making
 * the caller chase the lookup table separately.
 *
 * @par Reverse (unpack)
 *
 * @ref unpackSamplesBE inverts @ref packSamplesBE: same depth, same
 * sample count.  Samples are stored in the low @c depthBits of each
 * output @c uint16_t with the high bits cleared.  16f samples are
 * stored as their @c binary16 bit pattern.
 */
class St2110Video {
        public:
                /**
                 * @brief pgroup descriptor for a (sampling, depth) pair.
                 *
                 * @c octets — pgroup size in octets (Table 1-4
                 *   "pgroup size" column).
                 * @c pixels — pixel coverage of one pgroup (Table 1-4
                 *   "pgroup coverage" column).
                 * @c samples — sample count in one pgroup; equals
                 *   @c octets * 8 / @c depthBits for every entry in
                 *   the standard.
                 *
                 * A default-constructed @c Pgroup with @c octets==0
                 * signals "unsupported (sampling, depth) combo".
                 */
                struct Pgroup {
                                size_t octets = 0;
                                size_t pixels = 0;
                                size_t samples = 0;
                };

                /// @brief Maximum number of samples in any standard
                ///        pgroup — the 4:2:0 10-bit and 4:4:4 10-bit
                ///        pgroups carry 12 samples each (15 octets ×
                ///        8 / 10 = 12).  Sized for stack buffers used
                ///        by the row-level helpers.
                static constexpr size_t MaxSamplesPerPgroup = 12;

                /**
                 * @brief Returns the pgroup descriptor for the given
                 *        (sampling, depth) pair.
                 *
                 * Returns @c {0, 0, 0} when the combo is not defined
                 * by ST 2110-20 (e.g. XYZ depth-8 / depth-10, KEY
                 * depth-16f matches but 4:2:0 depth-16 does not).
                 *
                 * @param sampling One of the values from §7.4.1.
                 * @param depth One of the values from §7.4.2.
                 * @return A populated @ref Pgroup, or @c {0,0,0}.
                 */
                static Pgroup pgroup(const St2110Sampling &sampling, const St2110Depth &depth);

                /**
                 * @brief Convenience wrapper returning whether @ref pgroup
                 *        returns a defined (octets > 0) descriptor for
                 *        the (sampling, depth) pair.
                 */
                static bool isSupported(const St2110Sampling &sampling, const St2110Depth &depth);

                /**
                 * @brief Returns the integer per-sample width carried
                 *        on the wire for the given @ref St2110Depth.
                 *
                 * Returns 8 / 10 / 12 / 16 for @c Bits8 / @c Bits10 /
                 * @c Bits12 / @c Bits16, 16 for @c Bits16f (half-float
                 * is 16 bits wide), and 0 for @c Invalid.
                 */
                static int bitsPerSample(const St2110Depth &depth);

                /**
                 * @brief Returns whether the @ref St2110Depth carries
                 *        IEEE-754 @c binary16 floating-point samples
                 *        (@c true only for @c Bits16f).
                 */
                static bool isFloatDepth(const St2110Depth &depth);

                /**
                 * @brief Packs @p nSamples big-endian samples of
                 *        @p depthBits each from the low bits of
                 *        @p samples into @p dst.
                 *
                 * Returns the number of octets written, or 0 on error
                 * (@p dstCap too small, @p depthBits out of range).
                 * For @p depthBits == 16 (including the binary16
                 * carrier) and @p depthBits == 8 the loop reduces to
                 * a byte-by-byte big-endian copy; for 10 / 12 the
                 * helper packs the bit stream densely with no
                 * inter-sample padding.  Trailing bits in the final
                 * octet — if any — are zero-filled per ST 2110-20
                 * §6.2.1 ("the sender shall fill the remaining sample
                 * positions of the final pgroup with zero").
                 *
                 * @param dst Output buffer.
                 * @param dstCap Bytes available in @p dst.
                 * @param samples Source samples; each one occupies the
                 *        low @p depthBits of a @c uint16_t.
                 * @param nSamples Number of samples to write.
                 * @param depthBits Bits per sample (8 / 10 / 12 / 16).
                 * @return Octets written (or 0 on failure).
                 */
                static size_t packSamplesBE(uint8_t *dst, size_t dstCap,
                                            const uint16_t *samples, size_t nSamples,
                                            int depthBits);

                /**
                 * @brief Inverse of @ref packSamplesBE.
                 *
                 * Reads @p nSamples big-endian samples of @p depthBits
                 * each from @p src into @p samples.  Each output
                 * sample occupies the low @p depthBits of its
                 * @c uint16_t with the high bits cleared.  Returns
                 * the number of octets consumed, or 0 on error
                 * (@p srcSize too small, @p depthBits out of range).
                 */
                static size_t unpackSamplesBE(const uint8_t *src, size_t srcSize,
                                              uint16_t *samples, size_t nSamples,
                                              int depthBits);

                /**
                 * @brief Packs a whole row of @p nPixels pixels into
                 *        @p dst, given canonical-interleaved
                 *        @p samples for the @p (sampling, depth) pair.
                 *
                 * The caller hands in @c (pgroup.samples * nPixels /
                 * pgroup.pixels) sample entries in the order defined
                 * by the per-sampling Sample Order column.  This
                 * helper does the lookup, sanity-checks the @p nPixels
                 * alignment, and delegates to @ref packSamplesBE.
                 *
                 * @param sampling Wire-format sampling.
                 * @param depth Wire-format depth.
                 * @param nPixels Pixel count (must be a multiple of
                 *        @c pgroup.pixels — partial pgroups are
                 *        forbidden per §6.2.2).
                 * @param samples Source samples in canonical order.
                 * @param dst Output buffer.
                 * @param dstCap Bytes available in @p dst.
                 * @return Octets written.  Returns 0 on unsupported
                 *         combo, misaligned @p nPixels, or
                 *         insufficient @p dstCap.
                 */
                static size_t packRow(const St2110Sampling &sampling, const St2110Depth &depth,
                                      size_t nPixels, const uint16_t *samples,
                                      uint8_t *dst, size_t dstCap);

                /**
                 * @brief Inverse of @ref packRow.
                 *
                 * Reads enough octets to cover @p nPixels pixels of
                 * the given (sampling, depth) — i.e.
                 * @c pgroup.octets * nPixels / pgroup.pixels — and
                 * writes the unpacked canonical-interleaved samples
                 * to @p samples.  Returns octets consumed or 0 on
                 * error.
                 */
                static size_t unpackRow(const St2110Sampling &sampling, const St2110Depth &depth,
                                        size_t nPixels, const uint8_t *src, size_t srcSize,
                                        uint16_t *samples);

                /**
                 * @brief Returns the octet count required to carry
                 *        @p nPixels pixels for the given (sampling,
                 *        depth) — i.e. @c pgroup.octets * nPixels /
                 *        pgroup.pixels — or 0 when the combo is
                 *        unsupported or @p nPixels is not a multiple
                 *        of @c pgroup.pixels.
                 */
                static size_t rowOctets(const St2110Sampling &sampling, const St2110Depth &depth,
                                        size_t nPixels);

                /**
                 * @brief Returns the canonical sample count for
                 *        @p nPixels pixels of the given (sampling,
                 *        depth), or 0 when the combo is unsupported
                 *        or @p nPixels misaligned.  Equivalent to
                 *        @c pgroup.samples * nPixels / pgroup.pixels.
                 */
                static size_t rowSamples(const St2110Sampling &sampling, const St2110Depth &depth,
                                         size_t nPixels);

                // -----------------------------------------------------
                // Wire-form ↔ project-form conversion for the §7 SDP
                // fmtp parameter values.  The wire form is what the
                // standard's tables spell (e.g. @c YCbCr-4:2:2,
                // @c ST2065-1, @c BT2100LINPQ, @c 2110GPM,
                // @c FULLPROTECT); the project form uses CamelCase
                // identifiers per the [[feedback-enum-naming-camelcase]]
                // convention (e.g. @c YCbCr422, @c St2065_1,
                // @c Bt2100LinPq, @c Gpm, @c FullProtect).  These
                // helpers are the single mapping point — the SDP
                // builder / parser only ever calls into here.
                // -----------------------------------------------------

                /// @brief Wire form for the @c sampling fmtp value.
                static String samplingWire(const St2110Sampling &sampling);
                /// @brief Wire form for the @c depth fmtp value.
                static String depthWire(const St2110Depth &depth);
                /// @brief Wire form for the @c colorimetry fmtp value.
                static String colorimetryWire(const St2110Colorimetry &c);
                /// @brief Wire form for the @c TCS fmtp value.
                static String tcsWire(const St2110Tcs &t);
                /// @brief Wire form for the @c RANGE fmtp value.
                static String rangeWire(const St2110Range &r);
                /// @brief Wire form for the @c PM fmtp value.
                static String packingModeWire(const St2110PackingMode &p);

                /// @brief Inverse of @ref samplingWire.  Returns
                ///        @c St2110Sampling::Invalid on miss.
                static St2110Sampling samplingFromWire(const String &s);
                static St2110Depth depthFromWire(const String &s);
                static St2110Colorimetry colorimetryFromWire(const String &s);
                static St2110Tcs tcsFromWire(const String &s);
                static St2110Range rangeFromWire(const String &s);
                static St2110PackingMode packingModeFromWire(const String &s);

                /**
                 * @brief Builds the SMPTE Standard Number (@c SSN
                 *        fmtp) for the given (colorimetry, tcs) pair
                 *        per §7.2.
                 *
                 * Returns @c "ST2110-20:2017" unless @p colorimetry is
                 * @c Alpha or @p tcs is @c St2115LogS3, in which case
                 * @c "ST2110-20:2022" is required.  The two values are
                 * the only ones §7.2 calls out — every other
                 * (colorimetry, tcs) combo stays on the 2017 SSN even
                 * when the rest of the stream is otherwise
                 * 2022-edition-shaped.
                 */
                static String ssnFor(const St2110Colorimetry &colorimetry, const St2110Tcs &tcs);

                /**
                 * @brief Parsed / buildable view of a complete §7 fmtp
                 *        parameter set.
                 *
                 * Mirrors the §7.2 (required) + §7.3 (default-valued)
                 * grammar.  Build a populated @ref Fmtp and emit via
                 * @ref toFmtp; parse via @ref fromFmtp.  Round-trip is
                 * lossless across all spec-defined fields.
                 */
                struct Fmtp {
                                // §7.2 Required Media Type Parameters.
                                St2110Sampling     sampling;                              ///< @c sampling
                                St2110Depth        depth;                                 ///< @c depth
                                uint32_t           width = 0;                             ///< @c width
                                uint32_t           height = 0;                            ///< @c height
                                FrameRate          exactFrameRate;                        ///< @c exactframerate
                                St2110Colorimetry  colorimetry;                           ///< @c colorimetry
                                St2110PackingMode  pm{St2110PackingMode::Gpm};            ///< @c PM (default GPM)

                                // §7.3 Media Type Parameters with default values.
                                bool               interlace = false;                    ///< @c interlace present
                                bool               segmented = false;                    ///< @c segmented present
                                St2110Tcs          tcs{St2110Tcs::Sdr};                  ///< @c TCS (default SDR)
                                St2110Range        range{St2110Range::Narrow};           ///< @c RANGE (default NARROW)
                                uint32_t           maxUdp = 0;                            ///< @c MAXUDP (0 = absent)
                                PixelAspect        par;                                   ///< @c PAR (square = absent / 1:1)
                                String             ssnOverride;                           ///< explicit @c SSN (empty = derive)
                };

                /**
                 * @brief Returns the SDP wire form of a @ref FrameRate
                 *        per ST 2110-20:2022 §7.2.
                 *
                 * Integer rates emit a single decimal (@c "25") while
                 * non-integer rates emit @c "num/den" using the
                 * canonical simplified form stored by @ref Rational.
                 * Differs from @ref FrameRate::toString — which always
                 * emits @c "num/den" — to match the standard's
                 * "integer rates shall be signaled as a single decimal
                 * number" requirement.
                 */
                static String frameRateToWire(const FrameRate &fr);

                /**
                 * @brief Builds a §7 @c a=fmtp body (no payload-type
                 *        prefix) from the parameter set.
                 *
                 * Output is a single line of @c "name=value" entries
                 * separated by @c "; " per §7.1.  Required parameters
                 * always appear first in canonical order (sampling,
                 * depth, width, height, exactframerate, colorimetry,
                 * PM, SSN); default-valued parameters follow in
                 * declaration order, emitted only when they differ
                 * from the §7.3 default.  Standalone parameters
                 * (@c interlace, @c segmented) appear without an
                 * @c =value tail.
                 *
                 * @param fmtp Parameter set to serialise.
                 * @return The fmtp body, or an empty String when
                 *         the required fields are not populated
                 *         (caller bug).
                 */
                static String toFmtp(const Fmtp &fmtp);

                /**
                 * @brief Parses an already-split fmtp parameter map.
                 *
                 * Accepts the @c Map<String, String> returned by
                 * @c SdpMediaDescription::fmtpParameters() — name → value
                 * pairs already pre-decoded.  Standalone parameters
                 * (@c interlace, @c segmented) appear in the map with
                 * an empty value.
                 *
                 * Unknown parameters are ignored — receivers must per
                 * §7.3 silently skip parameters they don't recognise.
                 * Required parameters that are missing leave the
                 * corresponding field at its default-constructed
                 * value; callers can detect partial parses by checking
                 * @c sampling.isValid() etc.
                 *
                 * Spec-illegal combinations are cleared with a
                 * one-shot warning rather than rejected outright:
                 *  - @c segmented without @c interlace is forbidden
                 *    (§7.3); the parser clears @c segmented.
                 *  - 4:2:0 sampling combined with @c interlace or
                 *    @c segmented is forbidden (§6.2.5); the parser
                 *    clears both flags.
                 *
                 * @param params Parameter map from the SDP layer.
                 * @return Populated @ref Fmtp.
                 */
                static Fmtp fromFmtp(const Map<String, String> &params);

                /**
                 * @brief Returns the @ref VideoScanMode implied by an
                 *        @ref Fmtp's @c interlace / @c segmented pair
                 *        (§7.3).
                 *
                 *  - !interlace && !segmented → @c Progressive
                 *  - interlace && !segmented → @c Interlaced (the
                 *    SDP form does not distinguish field order;
                 *    callers wanting @c InterlacedEvenFirst /
                 *    @c InterlacedOddFirst must convey that through
                 *    a side channel, e.g. @c MediaConfig::VideoScanMode)
                 *  - interlace && segmented → @c PsF
                 *  - !interlace && segmented → invalid; returns
                 *    @c Progressive (the parser already cleared the
                 *    spurious flag)
                 *
                 * @param fmtp Parameter set.
                 * @return The @c VideoScanMode implied by the flags.
                 */
                static VideoScanMode fmtpScanMode(const Fmtp &fmtp);

                /**
                 * @brief Stamps an @ref Fmtp's @c interlace / @c segmented
                 *        flags from a @ref VideoScanMode (§7.3).
                 *
                 * Inverse of @ref fmtpScanMode.  @c Progressive and
                 * @c Unknown clear both flags; every interlaced
                 * variant sets @c interlace and clears @c segmented;
                 * @c PsF sets both.
                 *
                 * @param fmtp Parameter set to modify.
                 * @param mode Scan mode to encode.
                 */
                static void setFmtpScanMode(Fmtp &fmtp, VideoScanMode mode);

#if PROMEKI_ENABLE_PROAV
                /**
                 * @brief Maps a @ref PixelFormat to the corresponding
                 *        ST 2110-20 wire-format descriptor.
                 *
                 * Best-effort lookup: returns @c sampling=Invalid for
                 * @ref PixelFormat values that have no ST 2110-20
                 * equivalent (e.g. compressed codecs, ARGB, mono).
                 * The @c tcs field defaults to @c Sdr; HDR-eligible
                 * PixelFormats whose ColorModel encodes a non-SDR
                 * transfer characteristic upgrade @c tcs to the
                 * matching value (PQ / HLG / Linear).
                 */
                struct PixelFormatBridge {
                                St2110Sampling    sampling;                              ///< Inferred sampling.
                                St2110Depth       depth;                                 ///< Inferred depth.
                                St2110Colorimetry colorimetry;                           ///< Inferred colorimetry.
                                St2110Tcs         tcs{St2110Tcs::Sdr};                   ///< Inferred TCS.
                                St2110Range       range{St2110Range::Narrow};            ///< Inferred RANGE.
                };

                /// @brief Inverse-cast view: returns @c sampling.isValid()
                ///        when a meaningful ST 2110-20 mapping exists.
                static PixelFormatBridge bridgeForPixelFormat(const PixelFormat &pd);

                /**
                 * @brief Reverse lookup: given a §7 fmtp tuple, return
                 *        the @ref PixelFormat whose memory layout IS
                 *        the matching ST 2110-20 wire-format pgroup
                 *        stream.
                 *
                 * The returned PixelFormat is the target of the
                 * source→wire CSC conversion on the sender, and the
                 * source of the wire→app CSC conversion on the
                 * receiver.  For (sampling, depth) pairs whose wire
                 * format is byte-identical to an existing layout
                 * (8-bit, 16-bit, 16f for most samplings), the returned
                 * PixelFormat reuses the existing ID (e.g. RGB8_sRGB
                 * for 4:4:4 / 8 RGB).  Returns @c PixelFormat(Invalid)
                 * for combinations the standard does not define or
                 * that we have not registered yet (e.g. ICtCp / CLYCbCr
                 * — landing alongside their CSC kernels).
                 *
                 * The @p range parameter selects between Narrow and
                 * Full where the same (sampling, depth) admits both;
                 * pass @c St2110Range::Invalid to let the function pick
                 * the standard's default (Full for RGB, Narrow for
                 * YCbCr).
                 *
                 * @param sampling   Wire-format sampling.
                 * @param depth      Wire-format depth.
                 * @param colorimetry Wire-format colorimetry (selects Rec.601 / 709 / 2020 / 2100 / XYZ / Alpha).
                 * @param range      Wire-format range (Narrow / Full / FullProtect / Invalid = default).
                 * @return @c PixelFormat for the wire format, or @c PixelFormat(Invalid) if unsupported.
                 */
                static PixelFormat wirePixelFormatFor(const St2110Sampling   &sampling,
                                                     const St2110Depth      &depth,
                                                     const St2110Colorimetry &colorimetry,
                                                     const St2110Range       &range);
#endif // PROMEKI_ENABLE_PROAV
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
