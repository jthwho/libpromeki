/**
 * @file      imagedatadecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>
#include <promeki/imagedatadecoder.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/mediaconfig.h>
#include <promeki/crc.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ---------------------------------------------------------------------------
        // Otsu's method for bimodal binarisation.
        // ---------------------------------------------------------------------------
        //
        // Returns the threshold byte value that maximises between-class variance
        // over an 8-bit histogram.  Standard textbook formulation, single
        // histogram pass.  When @p n is zero or the histogram is degenerate
        // (all samples equal), returns 128 — caller will then either binarise
        // to a constant value (no transitions found) or short-circuit to an
        // error path.
        uint8_t otsuThreshold(const uint8_t *samples, size_t n) {
                if (n == 0) return 128;

                uint32_t hist[256] = {0};
                for (size_t i = 0; i < n; i++) hist[samples[i]]++;

                uint64_t total = 0;
                for (int i = 0; i < 256; i++) total += static_cast<uint64_t>(i) * hist[i];

                uint64_t sumB = 0;
                uint32_t wB = 0;
                int      bestT = 128;
                double   bestVar = 0.0;

                for (int t = 0; t < 256; t++) {
                        wB += hist[t];
                        if (wB == 0) continue;
                        const uint32_t wF = static_cast<uint32_t>(n) - wB;
                        if (wF == 0) break;
                        sumB += static_cast<uint64_t>(t) * hist[t];
                        const double mB = static_cast<double>(sumB) / wB;
                        const double mF = static_cast<double>(total - sumB) / wF;
                        const double diff = mB - mF;
                        const double between = static_cast<double>(wB) * static_cast<double>(wF) * diff * diff;
                        if (between > bestVar) {
                                bestVar = between;
                                bestT = t;
                        }
                }
                return static_cast<uint8_t>(bestT);
        }

        // ---------------------------------------------------------------------------
        // Sync nibble localisation and bit-width measurement.
        // ---------------------------------------------------------------------------
        //
        // The encoder writes the sync nibble at column 0: white, black, white,
        // black, each cell @c bitWidth pixels wide.  After binarisation that
        // looks like four runs of approximately-equal length starting at
        // column 0 with the first run being white.  We measure the four runs,
        // average them for a sub-pixel-accurate bit width, and validate that
        // the runs are within ±25% of each other (anything wider than that
        // means the sync nibble is corrupted or the band is mis-located).
        struct SyncMeasurement {
                        bool     ok = false;
                        double   bitWidth = 0.0; ///< Sub-pixel cell width.
                        uint32_t startCol = 0;   ///< Column at which the sync starts (always 0 for now).
                        Error    error = Error::Ok;
        };

        SyncMeasurement findSync(const uint8_t *binary, size_t width) {
                SyncMeasurement r;

                if (width < 4) {
                        r.error = Error::CorruptData;
                        return r;
                }

                // The first sync bit must be white.  If it's not, the band
                // doesn't start where the caller said it does — bail.
                if (binary[0] == 0) {
                        r.error = Error::CorruptData;
                        return r;
                }

                // The sync nibble is WBWB followed by the first payload bit.
                // We can only safely measure the first three runs because the
                // fourth (the second black) merges with the payload whenever
                // payload bit 0 happens to be black — which means we'd see a
                // single run of length 2*bitWidth straddling the sync/payload
                // boundary, with no transition to mark the sync nibble's end.
                // Three runs are all we need: they give us three independent
                // bit-width samples, all bounded on both sides by encoder
                // transitions, and three is enough for an average.  We
                // separately validate the fourth sync bit (which must be
                // black) by sampling its cell centre after the bit pitch is
                // known.
                uint32_t runs[3] = {0, 0, 0};
                int      runIdx = 0;
                uint8_t  expected = 1; // first run is white
                size_t   runStart = 0;

                for (size_t i = 0; i < width && runIdx < 3; i++) {
                        if (binary[i] == expected) continue;
                        runs[runIdx] = static_cast<uint32_t>(i - runStart);
                        runIdx++;
                        if (runIdx >= 3) {
                                // 'i' is the start of the fourth run (the
                                // boundary between sync run 3 and sync run 4).
                                runStart = i;
                                expected = static_cast<uint8_t>(1 - expected);
                                break;
                        }
                        runStart = i;
                        expected = static_cast<uint8_t>(1 - expected);
                }

                if (runIdx < 3) {
                        // Reached the end of the row without finding 3 runs —
                        // either the row is uniform (no transitions) or it
                        // doesn't carry a recognisable sync pattern.
                        r.error = Error::CorruptData;
                        return r;
                }

                const double avg = (runs[0] + runs[1] + runs[2]) / 3.0;
                if (avg <= 0) {
                        r.error = Error::CorruptData;
                        return r;
                }

                // The three measured sync runs should all be within ±25% of
                // the average width.  Wider variance is a strong signal that
                // the sync nibble is corrupted, the band is at the wrong
                // line, or the image has been so badly mangled that we
                // shouldn't trust the rest of the row either.
                for (int i = 0; i < 3; i++) {
                        const double dev = std::abs(static_cast<double>(runs[i]) - avg) / avg;
                        if (dev > 0.25) {
                                r.error = Error::CorruptData;
                                return r;
                        }
                }

                r.ok = true;
                r.bitWidth = avg;
                r.startCol = 0;
                return r;
        }

        // ---------------------------------------------------------------------------
        // Slice extraction
        // ---------------------------------------------------------------------------
        //
        // Pulls out the scan-line range we want to read from the source
        // payload into a small same-format scratch payload, then runs that
        // small payload through the CSC pipeline.  The result is an RGBA8
        // strip we can read luma from at any column without per-format byte
        // unpacking.
        //
        // For sub-sampled formats, the slice height is rounded up to the
        // maximum vSubsampling so the chroma plane has properly-aligned rows
        // (otherwise CSC would be feeding off-by-one chroma into the matrix).
        UncompressedVideoPayload::Ptr extractSliceRgba(const UncompressedVideoPayload &src, uint32_t firstLine,
                                                       uint32_t lineCount, size_t maxVSub, uint32_t &sliceFirstOut) {
                const ImageDesc      &srcDesc = src.desc();
                const PixelFormat    &pd = srcDesc.pixelFormat();
                const PixelMemLayout &pf = pd.memLayout();
                const size_t          srcWidth = srcDesc.width();
                const size_t          srcHeight = srcDesc.height();

                // Snap the band to a vSub-aligned region so the chroma rows
                // line up cleanly when we copy them out.
                const uint32_t alignedFirst = (firstLine / maxVSub) * maxVSub;
                const uint32_t alignedEnd = ((firstLine + lineCount + maxVSub - 1) / maxVSub) * maxVSub;
                const uint32_t sliceHeight = alignedEnd - alignedFirst;
                sliceFirstOut = firstLine - alignedFirst;

                // Bounds check — the band must fit inside the source payload.
                if (alignedEnd > srcHeight) return UncompressedVideoPayload::Ptr();

                ImageDesc sliceDesc(srcWidth, sliceHeight, pd);
                auto      slice = UncompressedVideoPayload::allocate(sliceDesc);
                if (!slice.isValid()) return UncompressedVideoPayload::Ptr();

                // Copy each plane's relevant rows from source to slice.  Line
                // stride is reconstructed from the per-plane BufferView size
                // divided by the source plane's vertical extent.
                for (size_t p = 0; p < pf.planeCount(); p++) {
                        const auto  &plane = pf.planeDesc(p);
                        const size_t vSub = plane.vSubsampling > 0 ? plane.vSubsampling : 1;
                        const size_t srcRows = srcHeight / vSub;
                        if (srcRows == 0) continue;
                        auto           srcView = src.plane(p);
                        const size_t   lineStride = srcView.size() / srcRows;
                        const size_t   srcRowStart = alignedFirst / vSub;
                        const size_t   numRows = sliceHeight / vSub;
                        const uint8_t *srcPlane = srcView.data();
                        auto           dstView = slice.modify()->data()[p];
                        uint8_t       *dstPlane = dstView.data();
                        std::memcpy(dstPlane, srcPlane + srcRowStart * lineStride, numRows * lineStride);
                }

                // Convert the slice to RGBA8 once; the decoder reads luma
                // out of the R channel from the converted strip.
                return slice->convert(PixelFormat(PixelFormat::RGBA8_sRGB), Metadata(), MediaConfig());
        }

        // Builds the @c imageWidth-long luma array from the converted RGBA8
        // strip according to the supplied sample mode.
        void extractLumaRow(const UncompressedVideoPayload &rgba, uint32_t sliceFirst, uint32_t lineCount,
                            ImageDataDecoder::SampleMode mode, std::vector<uint8_t> &out) {
                const size_t   width = rgba.desc().width();
                const size_t   height = rgba.desc().height();
                auto           view = rgba.plane(0);
                const size_t   stride = (height > 0) ? view.size() / height : 0;
                const uint8_t *base = view.data();
                out.assign(width, 0);

                if (mode == ImageDataDecoder::SampleMode::MiddleLine || lineCount == 1) {
                        const uint32_t midRow = sliceFirst + lineCount / 2;
                        const uint8_t *line = base + static_cast<size_t>(midRow) * stride;
                        for (size_t x = 0; x < width; x++) {
                                out[x] = line[x * 4]; // R channel
                        }
                        return;
                }

                // AverageBand: sum each column across all rows, divide by
                // lineCount.  Sums fit in uint32 because lineCount * 255 <
                // 2^32 for any plausible band height.
                std::vector<uint32_t> sumBuf(width, 0);
                for (uint32_t row = 0; row < lineCount; row++) {
                        const uint8_t *line = base + static_cast<size_t>(sliceFirst + row) * stride;
                        for (size_t x = 0; x < width; x++) {
                                sumBuf[x] += line[x * 4];
                        }
                }
                const uint32_t denom = lineCount;
                for (size_t x = 0; x < width; x++) {
                        out[x] = static_cast<uint8_t>((sumBuf[x] + denom / 2) / denom);
                }
        }

} // namespace

ImageDataDecoder::ImageDataDecoder(const ImageDesc &desc) : _desc(desc) {
        if (!_desc.isValid()) return;

        const PixelFormat &pd = _desc.pixelFormat();
        if (!pd.isValid() || pd.isCompressed()) return;

        const PixelMemLayout &pf = pd.memLayout();

        // Compute the same alignment quantum the encoder uses, so the
        // expected bit width matches what the encoder would have
        // produced for an image of this size and format.
        size_t align = pf.pixelsPerBlock();
        if (align == 0) align = 1;
        for (size_t i = 0; i < pf.planeCount(); i++) {
                const auto &plane = pf.planeDesc(i);
                if (plane.hSubsampling > 0) align = std::lcm(align, plane.hSubsampling);
                if (plane.vSubsampling > _maxVSubsampling) _maxVSubsampling = plane.vSubsampling;
        }
        if (_maxVSubsampling == 0) _maxVSubsampling = 1;

        const size_t imgWidth = _desc.width();
        const size_t maxCells = imgWidth / BitsPerRow;
        const size_t cellPx = (maxCells / align) * align;
        if (cellPx == 0) {
                promekiErr("ImageDataDecoder: image too narrow (%zu px) for "
                           "%u-bit cell at alignment %zu",
                           imgWidth, BitsPerRow, align);
                return;
        }
        _expectedBitWidth = static_cast<uint32_t>(cellPx);
        // ±50% acceptance band — wide enough to absorb scaling but
        // narrow enough to reject "we're decoding the wrong line".
        _bitWidthMin = std::max<uint32_t>(1, _expectedBitWidth / 2);
        _bitWidthMax = _expectedBitWidth + _expectedBitWidth / 2;

        _valid = true;
}

ImageDataDecoder::DecodedItem ImageDataDecoder::decodeOne(const UncompressedVideoPayload &src, const Band &band) const {
        DecodedItem item;

        if (band.lineCount == 0) {
                item.error = Error::InvalidArgument;
                return item;
        }

        // Extract the band's scan lines, push them through CSC into
        // an RGBA8 strip, and pull a 1D row of luma samples out.
        uint32_t                      sliceFirst = 0;
        UncompressedVideoPayload::Ptr rgba =
                extractSliceRgba(src, band.firstLine, band.lineCount, _maxVSubsampling, sliceFirst);
        if (!rgba.isValid()) {
                item.error = Error::ConversionFailed;
                return item;
        }

        std::vector<uint8_t> row;
        extractLumaRow(*rgba, sliceFirst, band.lineCount, _sampleMode, row);

        // Otsu threshold + binarise.
        const uint8_t        threshold = otsuThreshold(row.data(), row.size());
        std::vector<uint8_t> binary(row.size());
        for (size_t i = 0; i < row.size(); i++) {
                binary[i] = (row[i] > threshold) ? uint8_t(1) : uint8_t(0);
        }

        // Locate the sync nibble and measure the bit pitch.
        SyncMeasurement sync = findSync(binary.data(), binary.size());
        if (!sync.ok) {
                item.error = sync.error;
                return item;
        }
        item.bitWidth = sync.bitWidth;
        item.syncStartCol = sync.startCol;

        // Validate the discovered bit width against the
        // image-width-derived expectation.  ±50% is intentionally
        // generous — it absorbs ordinary scale ratios while still
        // catching "we're aligned to noise" outright.
        const uint32_t bw = static_cast<uint32_t>(sync.bitWidth + 0.5);
        if (bw < _bitWidthMin || bw > _bitWidthMax) {
                item.error = Error::CorruptData;
                return item;
        }

        // Sample the centre pixel of every cell at the established
        // sub-pixel pitch and rebuild the 76-bit codeword.  Sync,
        // payload, and CRC are decoded into separate accumulators
        // because the payload doesn't fit in a single 64-bit lane
        // alongside the sync nibble.
        uint8_t  syncBits = 0;
        uint64_t payload = 0;
        uint8_t  crcBits = 0;

        for (uint32_t i = 0; i < BitsPerRow; i++) {
                const double centerCol =
                        static_cast<double>(sync.startCol) + (static_cast<double>(i) + 0.5) * sync.bitWidth;
                const size_t col = static_cast<size_t>(centerCol + 0.5);
                if (col >= binary.size()) {
                        item.error = Error::CorruptData;
                        return item;
                }
                const uint64_t bit = binary[col];
                if (i < SyncBits) {
                        syncBits = static_cast<uint8_t>((syncBits << 1) | bit);
                } else if (i < SyncBits + PayloadBits) {
                        payload = (payload << 1) | bit;
                } else {
                        crcBits = static_cast<uint8_t>((crcBits << 1) | bit);
                }
        }

        item.decodedSync = syncBits;
        item.decodedCrc = crcBits;
        item.payload = payload;

        if (syncBits != SyncNibble) {
                item.error = Error::CorruptData;
                return item;
        }

        // Recompute the CRC the same way the encoder did: 8 payload
        // bytes, big-endian, fed to CRC-8/AUTOSAR.
        uint8_t bytes[8];
        for (int b = 0; b < 8; b++) {
                bytes[b] = static_cast<uint8_t>((payload >> ((7 - b) * 8)) & 0xffu);
        }
        Crc8 crc(CrcParams::Crc8Autosar);
        crc.update(bytes, 8);
        item.expectedCrc = crc.value();
        if (item.expectedCrc != crcBits) {
                item.error = Error::CorruptData;
                return item;
        }

        item.error = Error::Ok;
        return item;
}

Error ImageDataDecoder::decode(const UncompressedVideoPayload &payload, const List<Band> &bands,
                               DecodedList &out) const {
        out.clear();
        if (!_valid) return Error::Invalid;
        if (!payload.isValid()) return Error::Invalid;
        if (payload.desc().size() != _desc.size() || payload.desc().pixelFormat() != _desc.pixelFormat()) {
                return Error::InvalidArgument;
        }

        for (const Band &b : bands) {
                out.pushToBack(decodeOne(payload, b));
        }
        return Error::Ok;
}

ImageDataDecoder::DecodedItem ImageDataDecoder::decode(const UncompressedVideoPayload &payload,
                                                       const Band                     &band) const {
        if (!_valid) {
                DecodedItem item;
                item.error = Error::Invalid;
                return item;
        }
        if (!payload.isValid() || payload.desc().size() != _desc.size() ||
            payload.desc().pixelFormat() != _desc.pixelFormat()) {
                DecodedItem item;
                item.error = Error::InvalidArgument;
                return item;
        }
        return decodeOne(payload, band);
}

PROMEKI_NAMESPACE_END
