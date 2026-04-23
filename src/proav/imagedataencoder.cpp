/**
 * @file      imagedataencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <numeric>
#include <promeki/imagedataencoder.h>
#include <promeki/image.h>
#include <promeki/paintengine.h>
#include <promeki/color.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Returns the smallest cell-width in luma-pixel units that satisfies
// every plane's natural alignment quantum.  Combines the format's
// pixels-per-block (e.g. v210's 6) with the LCM of all planes'
// horizontal subsampling factors (typically 1 or 2).
size_t cellWidthAlignment(const PixelMemLayout &pf) {
        size_t align = pf.pixelsPerBlock();
        if(align == 0) align = 1;
        for(size_t i = 0; i < pf.planeCount(); i++) {
                const auto &pd = pf.planeDesc(i);
                if(pd.hSubsampling > 0) align = std::lcm(align, pd.hSubsampling);
        }
        return align == 0 ? 1 : align;
}

// Returns the unpadded byte count for a 1-line image of width
// @c cellPixels in the supplied plane.  Unlike PixelMemLayout::lineStride,
// this returns the *exact* data byte count with no trailing padding —
// required by the encoder which must place adjacent bit cells with no
// gaps in between (some PixelMemLayouts — notably v210 — force lineStride
// to a 128-byte alignment, which would corrupt subsequent cells if
// applied per-cell).
size_t cellBytesForPlane(const PixelMemLayout &pf, size_t planeIndex, size_t cellPixels) {
        const auto &pd = pf.planeDesc(planeIndex);
        if(pd.bytesPerSample > 0) {
                // Planar / semi-planar formats: bytesPerSample already
                // accounts for any per-pixel grouping (e.g. NV12 chroma
                // packs Cb+Cr into 2 bytes per chroma sample).
                return (cellPixels / pd.hSubsampling) * pd.bytesPerSample;
        }
        // Block-encoded interleaved (v210, RGBA8 with bytesPerBlock):
        // pixelsPerBlock + bytesPerBlock describe the cell.
        const size_t ppb = pf.pixelsPerBlock();
        const size_t bpb = pf.bytesPerBlock();
        if(ppb == 0) return 0;  // Format declares neither — unsupported.
        return (cellPixels / ppb) * bpb;
}

// Build a small primer image of width @c cellPixels filled with
// @p color, then convert it to @p targetDesc.  Returns an invalid
// Image on any failure.
Image buildPrimerImage(size_t cellPixels, const Color &color, const PixelFormat &targetDesc) {
        if(cellPixels == 0) return Image();

        // Render into RGBA8_sRGB first because it always has a paint
        // engine — RGBA8 → anything is the canonical CSC fast path.
        Image rgba(static_cast<size_t>(cellPixels), size_t(1),
                   PixelFormat(PixelFormat::RGBA8_sRGB));
        if(!rgba.isValid()) return Image();
        // RGBA8_sRGB always has a working paint engine, so no need to
        // validate the engine itself — only YUV / packed / compressed
        // formats fall back to the no-op engine.
        PaintEngine pe = rgba.createPaintEngine();
        auto pixel = pe.createPixel(color);
        pe.fill(pixel);

        if(targetDesc.id() == PixelFormat::RGBA8_sRGB) {
                // No conversion needed — pass through.
                return rgba;
        }
        return rgba.convert(targetDesc, Metadata());
}

}  // namespace

ImageDataEncoder::ImageDataEncoder(const ImageDesc &desc) : _desc(desc) {
        if(!_desc.isValid()) return;

        const PixelFormat &pd = _desc.pixelFormat();
        if(!pd.isValid() || pd.isCompressed()) {
                promekiErr("ImageDataEncoder: invalid or compressed pixel description");
                return;
        }

        const PixelMemLayout &pf = pd.memLayout();
        const size_t imgWidth = _desc.width();
        const size_t align    = cellWidthAlignment(pf);

        // Largest cell width in pixels such that BitsPerRow cells fit
        // and the width is a multiple of the alignment quantum.
        size_t maxCells = imgWidth / BitsPerRow;
        size_t cellPx   = (maxCells / align) * align;
        if(cellPx == 0) {
                promekiErr("ImageDataEncoder: image too narrow (%zu px) for "
                           "%u-bit cell at alignment %zu",
                           imgWidth, BitsPerRow, align);
                return;
        }
        _bitWidth = static_cast<uint32_t>(cellPx);
        const size_t patternPx = static_cast<size_t>(_bitWidth) * BitsPerRow;
        // Round pad width down to the alignment so the pad buffer is
        // also block-aligned for v210-style formats.
        size_t padPx = imgWidth - patternPx;
        padPx = (padPx / align) * align;
        _padWidth = static_cast<uint32_t>(padPx);

        _planeCount = pf.planeCount();
        if(_planeCount > MaxPlanes) {
                promekiErr("ImageDataEncoder: format has %zu planes (max %zu)",
                           _planeCount, MaxPlanes);
                return;
        }

        // Capture per-plane geometry that the hot path needs.
        for(size_t i = 0; i < _planeCount; i++) {
                const auto &plane = pf.planeDesc(i);
                _planes[i].lineStride   = pd.lineStride(i, _desc);
                _planes[i].hSubsampling = plane.hSubsampling > 0 ? plane.hSubsampling : 1;
                _planes[i].vSubsampling = plane.vSubsampling > 0 ? plane.vSubsampling : 1;
                _planes[i].cellBytes    = cellBytesForPlane(pf, i, _bitWidth);
                _planes[i].padBytes     = _padWidth > 0 ? cellBytesForPlane(pf, i, _padWidth) : 0;
        }

        if(!buildPrimers()) {
                promekiErr("ImageDataEncoder: failed to build primer cells for %s",
                           pd.name().cstr());
                return;
        }

        _valid = true;
}

bool ImageDataEncoder::buildPrimers() {
        const PixelFormat &pd = _desc.pixelFormat();
        const PixelMemLayout &pf = pd.memLayout();

        // We render up to three primer images via the CSC pipeline:
        //
        //   - white  : full-on RGB.  Source for the "1" cell on luma /
        //              RGB planes.
        //   - black  : full-off RGB.  Source for the "0" cell on luma /
        //              RGB planes, and for the trailing pad on those
        //              planes.
        //   - neutral: mid-gray RGB.  Source for *both* cells on chroma
        //              planes (i.e. any plane that is sub-sampled).
        //              The reason we render a third primer instead of
        //              just using one of white/black: the CSC pipeline's
        //              float-to-integer rounding can land white-cell
        //              chroma and black-cell chroma on neighbouring
        //              integers (e.g. white→127, black→128 for Cb at
        //              limited range), which would create visible
        //              chroma flicker between cells.  Using a single
        //              neutral primer for the chroma plane forces a
        //              uniform value across the entire row.
        const Color midGray(static_cast<uint8_t>(128),
                            static_cast<uint8_t>(128),
                            static_cast<uint8_t>(128));

        Image whiteCell   = buildPrimerImage(_bitWidth, Color::White, pd);
        Image blackCell   = buildPrimerImage(_bitWidth, Color::Black, pd);
        Image neutralCell = buildPrimerImage(_bitWidth, midGray,      pd);
        if(!whiteCell.isValid() || !blackCell.isValid() || !neutralCell.isValid()) return false;

        Image padBlack;
        Image padNeutral;
        if(_padWidth > 0) {
                padBlack   = buildPrimerImage(_padWidth, Color::Black, pd);
                padNeutral = buildPrimerImage(_padWidth, midGray,      pd);
                if(!padBlack.isValid() || !padNeutral.isValid()) return false;
        }

        // For each plane, copy the leading data bytes (no trailing
        // line-stride padding) into the encoder-owned primer Buffers.
        // Sub-sampled planes are treated as "chroma" and pull both
        // their "1" and "0" data from the neutral primer so cells are
        // visually flat in chroma.
        for(size_t i = 0; i < _planeCount; i++) {
                PlaneInfo &p = _planes[i];
                const auto &plane = pf.planeDesc(i);
                const bool isChroma = (plane.hSubsampling > 1) || (plane.vSubsampling > 1);

                const Image &cellOneSrc  = isChroma ? neutralCell : whiteCell;
                const Image &cellZeroSrc = isChroma ? neutralCell : blackCell;
                const Image *padSrc      = (_padWidth > 0) ? (isChroma ? &padNeutral : &padBlack) : nullptr;

                if(p.cellBytes > 0) {
                        p.oneCell  = Buffer(p.cellBytes);
                        p.zeroCell = Buffer(p.cellBytes);
                        if(!p.oneCell.isValid() || !p.zeroCell.isValid()) return false;
                        std::memcpy(p.oneCell.data(),  cellOneSrc.data(static_cast<int>(i)),  p.cellBytes);
                        std::memcpy(p.zeroCell.data(), cellZeroSrc.data(static_cast<int>(i)), p.cellBytes);
                        p.oneCell.setSize(p.cellBytes);
                        p.zeroCell.setSize(p.cellBytes);
                }

                if(p.padBytes > 0 && padSrc != nullptr) {
                        p.padBuf = Buffer(p.padBytes);
                        if(!p.padBuf.isValid()) return false;
                        std::memcpy(p.padBuf.data(), padSrc->data(static_cast<int>(i)), p.padBytes);
                        p.padBuf.setSize(p.padBytes);
                }
        }
        return true;
}

void ImageDataEncoder::writeOneScanline(Image &img, size_t planeIndex,
                                        size_t lineInPlane,
                                        uint8_t syncBits,
                                        uint64_t payloadBits,
                                        uint8_t crcBits) const {
        const PlaneInfo &p = _planes[planeIndex];
        uint8_t *base = static_cast<uint8_t *>(img.data(static_cast<int>(planeIndex)));
        uint8_t *dest = base + lineInPlane * p.lineStride;
        const size_t cb = p.cellBytes;

        // 76-bit row laid out in three pieces (passed in directly so the
        // top sync nibble doesn't have to fit alongside the 64-bit
        // payload in a single uint64_t):
        //   sync nibble  (4 bits) — MSB-first, written first
        //   payload      (64 bits) — MSB-first
        //   CRC          (8 bits) — MSB-first

        // Sync nibble (4 bits, MSB-first).
        for(int i = SyncBits - 1; i >= 0; --i) {
                const bool b = ((syncBits >> i) & 1u) != 0;
                std::memcpy(dest, b ? p.oneCell.data() : p.zeroCell.data(), cb);
                dest += cb;
        }
        // Payload (64 bits, MSB-first).
        for(int i = PayloadBits - 1; i >= 0; --i) {
                const bool b = ((payloadBits >> i) & 1u) != 0;
                std::memcpy(dest, b ? p.oneCell.data() : p.zeroCell.data(), cb);
                dest += cb;
        }
        // CRC (8 bits, MSB-first).
        for(int i = CrcBits - 1; i >= 0; --i) {
                const bool b = ((crcBits >> i) & 1u) != 0;
                std::memcpy(dest, b ? p.oneCell.data() : p.zeroCell.data(), cb);
                dest += cb;
        }
        // Trailing pad / neutral region.
        if(p.padBytes > 0) {
                std::memcpy(dest, p.padBuf.data(), p.padBytes);
        }
}

Error ImageDataEncoder::encode(Image &img, const List<Item> &items) const {
        if(!_valid) return Error::Invalid;
        if(!img.isValid()) return Error::Invalid;
        if(img.desc().size() != _desc.size() ||
           img.desc().pixelFormat() != _desc.pixelFormat()) {
                return Error::InvalidArgument;
        }

        const size_t imgHeight = _desc.height();

        // CRC instance built once per encode() call so the 256-entry
        // table is computed exactly once regardless of how many items
        // we process.  Cheap enough that we don't bother caching it
        // across calls — encode() runs at frame rate, not per pixel.
        Crc8 crc(CrcParams::Crc8Autosar);

        for(const Item &it : items) {
                // Bounds-check against the luma plane.  Items that
                // overrun the bottom of the image are rejected outright
                // rather than silently truncated.
                const uint64_t lastEx = static_cast<uint64_t>(it.firstLine) + it.lineCount;
                if(it.lineCount == 0) continue;
                if(lastEx > imgHeight) return Error::OutOfRange;

                // Build the 76-bit codeword once per item.  Layout:
                //   bits 75..72  sync nibble
                //   bits 71..8   payload (MSB-first)
                //   bits  7..0   CRC-8/AUTOSAR over the 8 payload
                //                bytes interpreted big-endian.
                uint8_t payloadBytes[8];
                for(int b = 0; b < 8; b++) {
                        payloadBytes[b] = static_cast<uint8_t>((it.payload >> ((7 - b) * 8)) & 0xffu);
                }
                crc.reset();
                crc.update(payloadBytes, 8);
                const uint8_t crcVal = crc.value();

                // For each plane, walk the chroma-row range that
                // overlaps the luma range and emit one cell row per
                // chroma scan line.  For luma planes (vSubsampling
                // == 1) this is just the luma range itself.
                for(size_t pi = 0; pi < _planeCount; pi++) {
                        const PlaneInfo &p = _planes[pi];
                        if(p.cellBytes == 0) continue;

                        const size_t vsub = p.vSubsampling;
                        const size_t firstP = it.firstLine / vsub;
                        const size_t lastExP = (lastEx + vsub - 1) / vsub;
                        for(size_t line = firstP; line < lastExP; line++) {
                                writeOneScanline(img, pi, line,
                                                 SyncNibble, it.payload, crcVal);
                        }
                }
        }
        return Error::Ok;
}

Error ImageDataEncoder::encode(Image &img, const Item &item) const {
        List<Item> single;
        single.pushToBack(item);
        return encode(img, single);
}

PROMEKI_NAMESPACE_END
