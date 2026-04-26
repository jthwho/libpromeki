/**
 * @file      imagedataencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <numeric>
#include <promeki/imagedataencoder.h>
#include <promeki/paintengine.h>
#include <promeki/color.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/mediaconfig.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Returns the smallest cell-width in luma-pixel units that satisfies
        // every plane's natural alignment quantum.  Combines the format's
        // pixels-per-block (e.g. v210's 6) with the LCM of all planes'
        // horizontal subsampling factors (typically 1 or 2).
        size_t cellWidthAlignment(const PixelMemLayout &pf) {
                size_t align = pf.pixelsPerBlock();
                if (align == 0) align = 1;
                for (size_t i = 0; i < pf.planeCount(); i++) {
                        const auto &pd = pf.planeDesc(i);
                        if (pd.hSubsampling > 0) align = std::lcm(align, pd.hSubsampling);
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
                if (pd.bytesPerSample > 0) {
                        // Planar / semi-planar formats: bytesPerSample already
                        // accounts for any per-pixel grouping (e.g. NV12 chroma
                        // packs Cb+Cr into 2 bytes per chroma sample).
                        return (cellPixels / pd.hSubsampling) * pd.bytesPerSample;
                }
                // Block-encoded interleaved (v210, RGBA8 with bytesPerBlock):
                // pixelsPerBlock + bytesPerBlock describe the cell.
                const size_t ppb = pf.pixelsPerBlock();
                const size_t bpb = pf.bytesPerBlock();
                if (ppb == 0) return 0; // Format declares neither — unsupported.
                return (cellPixels / ppb) * bpb;
        }

        // Build a small primer payload of width @c cellPixels filled with
        // @p color, then convert it to @p targetDesc.  Returns a null Ptr on
        // any failure.
        UncompressedVideoPayload::Ptr buildPrimerPayload(size_t cellPixels, const Color &color,
                                                         const PixelFormat &targetDesc) {
                if (cellPixels == 0) return UncompressedVideoPayload::Ptr();

                // Render into RGBA8_sRGB first because it always has a paint
                // engine — RGBA8 → anything is the canonical CSC fast path.
                ImageDesc rgbaDesc(cellPixels, size_t(1), PixelFormat::RGBA8_sRGB);
                auto      rgba = UncompressedVideoPayload::allocate(rgbaDesc);
                if (!rgba.isValid()) return UncompressedVideoPayload::Ptr();
                // RGBA8_sRGB always has a working paint engine, so no need to
                // validate the engine itself — only YUV / packed / compressed
                // formats fall back to the no-op engine.
                PaintEngine pe = rgba->createPaintEngine();
                auto        pixel = pe.createPixel(color);
                pe.fill(pixel);

                if (targetDesc.id() == PixelFormat::RGBA8_sRGB) {
                        // No conversion needed — pass through.
                        return rgba;
                }
                return rgba->convert(targetDesc, Metadata(), MediaConfig());
        }

} // namespace

ImageDataEncoder::ImageDataEncoder(const ImageDesc &desc) : _desc(desc) {
        if (!_desc.isValid()) return;

        const PixelFormat &pd = _desc.pixelFormat();
        if (!pd.isValid() || pd.isCompressed()) {
                promekiErr("ImageDataEncoder: invalid or compressed pixel description");
                return;
        }

        const PixelMemLayout &pf = pd.memLayout();
        const size_t          imgWidth = _desc.width();
        const size_t          align = cellWidthAlignment(pf);

        // Largest cell width in pixels such that BitsPerRow cells fit
        // and the width is a multiple of the alignment quantum.
        size_t maxCells = imgWidth / BitsPerRow;
        size_t cellPx = (maxCells / align) * align;
        if (cellPx == 0) {
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
        if (_planeCount > MaxPlanes) {
                promekiErr("ImageDataEncoder: format has %zu planes (max %zu)", _planeCount, MaxPlanes);
                return;
        }

        // Capture per-plane geometry that the hot path needs.
        for (size_t i = 0; i < _planeCount; i++) {
                const auto &plane = pf.planeDesc(i);
                _planes[i].lineStride = pd.lineStride(i, _desc);
                _planes[i].hSubsampling = plane.hSubsampling > 0 ? plane.hSubsampling : 1;
                _planes[i].vSubsampling = plane.vSubsampling > 0 ? plane.vSubsampling : 1;
                _planes[i].cellBytes = cellBytesForPlane(pf, i, _bitWidth);
                _planes[i].padBytes = _padWidth > 0 ? cellBytesForPlane(pf, i, _padWidth) : 0;
        }

        if (!buildPrimers()) {
                promekiErr("ImageDataEncoder: failed to build primer cells for %s", pd.name().cstr());
                return;
        }

        _valid = true;
}

bool ImageDataEncoder::buildPrimers() {
        const PixelFormat    &pd = _desc.pixelFormat();
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
        const Color midGray(static_cast<uint8_t>(128), static_cast<uint8_t>(128), static_cast<uint8_t>(128));

        auto whiteCell = buildPrimerPayload(_bitWidth, Color::White, pd);
        auto blackCell = buildPrimerPayload(_bitWidth, Color::Black, pd);
        auto neutralCell = buildPrimerPayload(_bitWidth, midGray, pd);
        if (!whiteCell.isValid() || !blackCell.isValid() || !neutralCell.isValid()) return false;

        UncompressedVideoPayload::Ptr padBlack;
        UncompressedVideoPayload::Ptr padNeutral;
        if (_padWidth > 0) {
                padBlack = buildPrimerPayload(_padWidth, Color::Black, pd);
                padNeutral = buildPrimerPayload(_padWidth, midGray, pd);
                if (!padBlack.isValid() || !padNeutral.isValid()) return false;
        }

        // For each plane, copy the leading data bytes (no trailing
        // line-stride padding) into the encoder-owned primer Buffers.
        // Sub-sampled planes are treated as "chroma" and pull both
        // their "1" and "0" data from the neutral primer so cells are
        // visually flat in chroma.
        for (size_t i = 0; i < _planeCount; i++) {
                PlaneInfo  &p = _planes[i];
                const auto &plane = pf.planeDesc(i);
                const bool  isChroma = (plane.hSubsampling > 1) || (plane.vSubsampling > 1);

                const UncompressedVideoPayload &cellOneSrc = isChroma ? *neutralCell : *whiteCell;
                const UncompressedVideoPayload &cellZeroSrc = isChroma ? *neutralCell : *blackCell;
                const UncompressedVideoPayload *padSrc =
                        (_padWidth > 0) ? (isChroma ? padNeutral.ptr() : padBlack.ptr()) : nullptr;

                if (p.cellBytes > 0) {
                        p.oneCell = Buffer(p.cellBytes);
                        p.zeroCell = Buffer(p.cellBytes);
                        if (!p.oneCell.isValid() || !p.zeroCell.isValid()) return false;
                        std::memcpy(p.oneCell.data(), cellOneSrc.plane(i).data(), p.cellBytes);
                        std::memcpy(p.zeroCell.data(), cellZeroSrc.plane(i).data(), p.cellBytes);
                        p.oneCell.setSize(p.cellBytes);
                        p.zeroCell.setSize(p.cellBytes);
                }

                if (p.padBytes > 0 && padSrc != nullptr) {
                        p.padBuf = Buffer(p.padBytes);
                        if (!p.padBuf.isValid()) return false;
                        std::memcpy(p.padBuf.data(), padSrc->plane(i).data(), p.padBytes);
                        p.padBuf.setSize(p.padBytes);
                }
        }
        return true;
}

void ImageDataEncoder::writeOneScanline(uint8_t *planeBase, size_t planeIndex, size_t lineInPlane, uint8_t syncBits,
                                        uint64_t payloadBits, uint8_t crcBits) const {
        const PlaneInfo &p = _planes[planeIndex];
        uint8_t         *dest = planeBase + lineInPlane * p.lineStride;
        const size_t     cb = p.cellBytes;

        // 76-bit row laid out in three pieces (passed in directly so the
        // top sync nibble doesn't have to fit alongside the 64-bit
        // payload in a single uint64_t):
        //   sync nibble  (4 bits) — MSB-first, written first
        //   payload      (64 bits) — MSB-first
        //   CRC          (8 bits) — MSB-first

        // Sync nibble (4 bits, MSB-first).
        for (int i = SyncBits - 1; i >= 0; --i) {
                const bool b = ((syncBits >> i) & 1u) != 0;
                std::memcpy(dest, b ? p.oneCell.data() : p.zeroCell.data(), cb);
                dest += cb;
        }
        // Payload (64 bits, MSB-first).
        for (int i = PayloadBits - 1; i >= 0; --i) {
                const bool b = ((payloadBits >> i) & 1u) != 0;
                std::memcpy(dest, b ? p.oneCell.data() : p.zeroCell.data(), cb);
                dest += cb;
        }
        // CRC (8 bits, MSB-first).
        for (int i = CrcBits - 1; i >= 0; --i) {
                const bool b = ((crcBits >> i) & 1u) != 0;
                std::memcpy(dest, b ? p.oneCell.data() : p.zeroCell.data(), cb);
                dest += cb;
        }
        // Trailing pad / neutral region.
        if (p.padBytes > 0) {
                std::memcpy(dest, p.padBuf.data(), p.padBytes);
        }
}

namespace {

        // Common inner loop: stamp each requested item across every plane.
        // Plane base pointers are resolved by the caller so this works for
        // both Image and UncompressedVideoPayload.
        template <typename PlaneBaseFn>
        Error encodeCommon(const ImageDataEncoder *self, size_t imgHeight, size_t planeCount,
                           const List<ImageDataEncoder::Item> &items, PlaneBaseFn getPlaneBase) {
                Crc8 crc(CrcParams::Crc8Autosar);
                for (const ImageDataEncoder::Item &it : items) {
                        // Bounds-check against the luma plane.  Items that
                        // overrun the bottom of the image are rejected outright
                        // rather than silently truncated.
                        const uint64_t lastEx = static_cast<uint64_t>(it.firstLine) + it.lineCount;
                        if (it.lineCount == 0) continue;
                        if (lastEx > imgHeight) return Error::OutOfRange;

                        // Build the 76-bit codeword once per item.  Layout:
                        //   bits 75..72  sync nibble
                        //   bits 71..8   payload (MSB-first)
                        //   bits  7..0   CRC-8/AUTOSAR over the 8 payload
                        //                bytes interpreted big-endian.
                        uint8_t payloadBytes[8];
                        for (int b = 0; b < 8; b++) {
                                payloadBytes[b] = static_cast<uint8_t>((it.payload >> ((7 - b) * 8)) & 0xffu);
                        }
                        crc.reset();
                        crc.update(payloadBytes, 8);
                        const uint8_t crcVal = crc.value();

                        // For each plane, walk the chroma-row range that
                        // overlaps the luma range and emit one cell row per
                        // chroma scan line.
                        for (size_t pi = 0; pi < planeCount; pi++) {
                                uint8_t *base = getPlaneBase(pi);
                                if (base == nullptr) continue;
                                self->writeScanlineBase(base, pi, it, lastEx, ImageDataEncoder::SyncNibble, crcVal);
                        }
                }
                return Error::Ok;
        }

} // namespace

void ImageDataEncoder::writeScanlineBase(uint8_t *planeBase, size_t planeIndex, const Item &item, uint64_t lastEx,
                                         uint8_t syncBits, uint8_t crcVal) const {
        const PlaneInfo &p = _planes[planeIndex];
        if (p.cellBytes == 0) return;
        const size_t vsub = p.vSubsampling;
        const size_t firstP = item.firstLine / vsub;
        const size_t lastExP = (lastEx + vsub - 1) / vsub;
        for (size_t line = firstP; line < lastExP; line++) {
                writeOneScanline(planeBase, planeIndex, line, syncBits, item.payload, crcVal);
        }
}

Error ImageDataEncoder::encode(UncompressedVideoPayload &inout, const List<Item> &items) const {
        if (!_valid) return Error::Invalid;
        if (!inout.isValid()) return Error::Invalid;
        if (inout.desc().size() != _desc.size() || inout.desc().pixelFormat() != _desc.pixelFormat()) {
                return Error::InvalidArgument;
        }
        if (inout.planeCount() < _planeCount) return Error::Invalid;
        return encodeCommon(this, _desc.height(), _planeCount, items,
                            [&inout](size_t pi) -> uint8_t * { return inout.data()[pi].data(); });
}

Error ImageDataEncoder::encode(UncompressedVideoPayload &inout, const Item &item) const {
        List<Item> single;
        single.pushToBack(item);
        return encode(inout, single);
}

PROMEKI_NAMESPACE_END
