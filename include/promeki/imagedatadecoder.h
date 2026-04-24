/**
 * @file      imagedatadecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/list.h>
#include <promeki/imagedesc.h>
#include <promeki/imagedataencoder.h>

PROMEKI_NAMESPACE_BEGIN

class Image;
class UncompressedVideoPayload;

/**
 * @brief Companion decoder for @ref ImageDataEncoder.
 * @ingroup proav
 *
 * Reads back the 64-bit payloads written by @ref ImageDataEncoder
 * from a raster @ref Image, with enough robustness to survive
 * compression, scaling, and other "real-world" image-pipeline
 * indignities.  See @ref imagedataencoder for the wire format
 * specification — anything described there is normative; this class
 * is the reference implementation of a compatible reader.
 *
 * @par Algorithm
 * For each band the caller hands in:
 *   1. The relevant scan lines are extracted from the source image
 *      and converted to @c RGBA8_sRGB via the libpromeki @ref csc
 *      "CSC pipeline".  This is the slow but format-agnostic step;
 *      future revisions can add hand-rolled fast paths for the
 *      common formats and bypass the CSC entirely.
 *   2. The R channel of the converted scan-line strip is
 *      vertically averaged into a single 1D row of length
 *      @c imageWidth.  Multi-line averaging is essentially free SNR:
 *      a 16-line band gives sqrt(16) = 4× improvement against
 *      Gaussian-style noise.  Single-line mode (@ref MiddleLine) is
 *      available as a faster, less robust alternative.
 *   3. The averaged row is binarised against a threshold computed
 *      using @b Otsu's method — the textbook variance-minimising
 *      threshold for bimodal signals — so the decoder picks up
 *      whatever white / black levels actually exist in the channel
 *      after compression has had its way with them.
 *   4. The @b sync nibble at the start of the binarised row supplies
 *      the bit pitch.  Its four runs are measured and averaged into
 *      a sub-pixel-accurate @c bitWidth value.  The decoder
 *      validates the result against @c imageWidth/76 ± 50% and
 *      rejects rows that fall outside that band as
 *      @c Error::CorruptData.
 *   5. The 76 bit cells are sampled at their centres at the
 *      established pitch, the sync nibble is verified, the CRC is
 *      recomputed against the catalogued CRC-8/AUTOSAR parameters,
 *      and the payload is returned.
 *
 * @par Lifetime and reuse
 * Construct one decoder per (PixelFormat, image dimensions) pair and
 * reuse it across many frames — the per-decode allocation is just a
 * scratch RGBA8 conversion buffer, which gets re-allocated lazily
 * for each band.
 *
 * @par Example
 * @code
 * ImageDataDecoder dec(img.desc());
 * if(!dec.isValid()) {
 *     // Image too narrow for any 76-cell row at this format's
 *     // alignment quantum.
 * }
 *
 * List<ImageDataDecoder::Band> bands;
 * bands.pushToBack({  0, 16 });   // first 16 lines
 * bands.pushToBack({ 16, 16 });   // next 16 lines
 *
 * List<ImageDataDecoder::DecodedItem> items;
 * dec.decode(img, bands, items);
 *
 * for(const auto &it : items) {
 *     if(it.error.isOk()) {
 *         // it.payload is the decoded 64-bit value.
 *     }
 * }
 * @endcode
 *
 * @see ImageDataEncoder, @ref imagedataencoder
 */
class ImageDataDecoder {
        public:
                /** @copydoc ImageDataEncoder::SyncBits */
                static constexpr uint32_t SyncBits    = ImageDataEncoder::SyncBits;
                /** @copydoc ImageDataEncoder::PayloadBits */
                static constexpr uint32_t PayloadBits = ImageDataEncoder::PayloadBits;
                /** @copydoc ImageDataEncoder::CrcBits */
                static constexpr uint32_t CrcBits     = ImageDataEncoder::CrcBits;
                /** @copydoc ImageDataEncoder::BitsPerRow */
                static constexpr uint32_t BitsPerRow  = ImageDataEncoder::BitsPerRow;
                /** @copydoc ImageDataEncoder::SyncNibble */
                static constexpr uint8_t  SyncNibble  = ImageDataEncoder::SyncNibble;

                /**
                 * @brief How the decoder samples the image rows in a band.
                 */
                enum class SampleMode {
                        /// @brief Convert every scan line in the band and
                        /// vertically average the R channel.  Default —
                        /// gives @c sqrt(N) SNR improvement on noisy /
                        /// compressed input.
                        AverageBand,
                        /// @brief Convert just the middle scan line of the
                        /// band.  Faster but does not benefit from
                        /// multi-line averaging.
                        MiddleLine,
                };

                /**
                 * @brief One scan-line band the decoder should look at.
                 *
                 * The decoder does not search for bands — the caller must
                 * supply the @c (firstLine, lineCount) pair that matches
                 * what the encoder wrote.
                 */
                struct Band {
                        uint32_t firstLine;   ///< First (luma) scan line of the band.
                        uint32_t lineCount;   ///< Number of (luma) scan lines covered.
                };

                /**
                 * @brief Result of decoding one band.
                 *
                 * @c error is the canonical "did this band decode" check.
                 * The other fields are populated when @c error.isOk() and
                 * left at default values otherwise; @c bitWidth and
                 * @c syncStartCol are also populated for diagnostic
                 * purposes when the sync nibble was located but a later
                 * step failed (e.g. CRC mismatch).
                 */
                struct DecodedItem {
                        uint64_t payload      = 0;        ///< Decoded 64-bit payload.
                        double   bitWidth     = 0;        ///< Sub-pixel-accurate bit cell width discovered from the sync nibble.
                        uint32_t syncStartCol = 0;        ///< Column where the sync nibble began.
                        uint8_t  decodedSync  = 0;        ///< Sync nibble actually read from the image (must match @ref SyncNibble).
                        uint8_t  decodedCrc   = 0;        ///< CRC byte read from the image.
                        uint8_t  expectedCrc  = 0;        ///< CRC recomputed locally over the decoded payload.
                        Error    error        = Error::Ok;///< @c Error::Ok on success.
                };

                /** @brief List of decoded items, one per supplied band. */
                using DecodedList = List<DecodedItem>;

                /** @brief Constructs an invalid decoder. */
                ImageDataDecoder() = default;

                /**
                 * @brief Constructs a decoder for an image of the given
                 *        descriptor.
                 *
                 * The descriptor's pixel description and dimensions are
                 * captured for later validation; calls to @ref decode
                 * must pass an @c Image whose descriptor compares equal.
                 *
                 * @param desc Image descriptor.
                 */
                explicit ImageDataDecoder(const ImageDesc &desc);

                /** @brief Returns @c true if the decoder is ready to use. */
                bool isValid() const { return _valid; }

                /** @brief Returns the descriptor the decoder was built for. */
                const ImageDesc &desc() const { return _desc; }

                /** @brief Returns the expected bit width based on the image width and the format alignment. */
                uint32_t expectedBitWidth() const { return _expectedBitWidth; }

                /** @brief Returns the lower acceptance bound for the discovered bit width. */
                uint32_t bitWidthMin() const { return _bitWidthMin; }

                /** @brief Returns the upper acceptance bound for the discovered bit width. */
                uint32_t bitWidthMax() const { return _bitWidthMax; }

                /** @brief Returns the current sample mode. */
                SampleMode sampleMode() const { return _sampleMode; }

                /** @brief Sets the sample mode (default: @ref SampleMode::AverageBand). */
                void setSampleMode(SampleMode mode) { _sampleMode = mode; }

                /**
                 * @brief Decodes a list of bands from an image.
                 *
                 * @p out is cleared before decoding starts.  After the
                 * call returns, @p out contains exactly @c bands.size()
                 * @ref DecodedItem entries — one per supplied band, in
                 * the same order, including failed ones.  Inspect each
                 * item's @c error field to tell success from failure.
                 *
                 * @param payload Payload to read from.  Must have the same
                 *                descriptor as the one supplied to the
                 *                constructor.
                 * @param bands   Bands to decode.
                 * @param out     Output list of decoded items.
                 * @return @c Error::Ok on success (every band attempted
                 *         and the result list is fully populated), or
                 *         an error code if the payload descriptor does
                 *         not match the decoder.
                 */
                Error decode(const UncompressedVideoPayload &payload,
                             const List<Band> &bands, DecodedList &out) const;

                /**
                 * @brief Convenience overload — decodes one band and
                 *        returns its DecodedItem directly.
                 */
                DecodedItem decode(const UncompressedVideoPayload &payload,
                                   const Band &band) const;

        private:
                ImageDesc       _desc;
                uint32_t        _expectedBitWidth = 0;
                uint32_t        _bitWidthMin      = 0;
                uint32_t        _bitWidthMax      = 0;
                size_t          _maxVSubsampling  = 1;
                SampleMode      _sampleMode       = SampleMode::AverageBand;
                bool            _valid            = false;

                DecodedItem decodeOne(const UncompressedVideoPayload &payload,
                                      const Band &band) const;
};

PROMEKI_NAMESPACE_END
