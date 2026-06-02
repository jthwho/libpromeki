/**
 * @file      v4l2rawformat.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_V4L2

#include <cstring>
#include <linux/videodev2.h>

#include <promeki/v4l2rawformat.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelmemlayout.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Supported raw semi-planar formats.  Order = preference (the supported list
// the planner sees).  10-bit 4:2:2 (no standard 16-bit V4L2 FourCC) and the
// Xilinx-native packed XV15 / XV20 are intentionally absent — both are a
// separate follow-up (packed-format handling, VCU-only validation).
// ---------------------------------------------------------------------------

static const V4l2RawFormat kRawFormats[] = {
        {V4L2_PIX_FMT_NV12, PixelFormat::YUV8_420_SemiPlanar_Rec709, 1, 0, 2, "NV12"},
        {V4L2_PIX_FMT_NV16, PixelFormat::YUV8_422_SemiPlanar_Rec709, 1, 0, 1, "NV16"},
        {V4L2_PIX_FMT_P010, PixelFormat::YUV10_420_SemiPlanar_LE_Rec709, 2, 6, 2, "P010"},
};

const V4l2RawFormat *v4l2RawFormatForPixelFormat(PixelFormat::ID id) {
        for (const V4l2RawFormat &f : kRawFormats) {
                if (f.pixelFormatId == id) return &f;
        }
        return nullptr;
}

const V4l2RawFormat *v4l2RawFormatForFourcc(uint32_t fourcc) {
        for (const V4l2RawFormat &f : kRawFormats) {
                if (f.fourcc == fourcc) return &f;
        }
        return nullptr;
}

List<int> v4l2SupportedRawPixelFormats() {
        List<int> out;
        for (const V4l2RawFormat &f : kRawFormats) out.pushToBack(static_cast<int>(f.pixelFormatId));
        return out;
}

// Copy one plane row-by-row honouring independent src/dst strides.  For
// 8-bit (bps==1) a plain byte copy; for 10-bit (bps==2) a per-sample copy
// applying the P010 MSB-align shift in the requested direction.
//   toV4l2 == true  : promeki (LSB) → V4L2 (MSB), sample <<= shift.
//   toV4l2 == false : V4L2 (MSB) → promeki (LSB), sample >>= shift.
static void copyPlaneRows(uint8_t *dst, size_t dstStride, const uint8_t *src, size_t srcStride,
                          uint32_t samplesPerRow, uint32_t rows, uint8_t bps, uint8_t shift, bool toV4l2) {
        const size_t rowBytes = static_cast<size_t>(samplesPerRow) * bps;
        for (uint32_t r = 0; r < rows; ++r) {
                if (bps == 1 || shift == 0) {
                        std::memcpy(dst, src, rowBytes);
                } else {
                        const uint16_t *s = reinterpret_cast<const uint16_t *>(src);
                        uint16_t       *d = reinterpret_cast<uint16_t *>(dst);
                        if (toV4l2) {
                                for (uint32_t x = 0; x < samplesPerRow; ++x) {
                                        d[x] = static_cast<uint16_t>((s[x] & 0x03FF) << shift);
                                }
                        } else {
                                for (uint32_t x = 0; x < samplesPerRow; ++x) {
                                        d[x] = static_cast<uint16_t>((s[x] >> shift) & 0x03FF);
                                }
                        }
                }
                dst += dstStride;
                src += srcStride;
        }
}

void v4l2PackSemiPlanar(const UncompressedVideoPayload &src, const V4l2RawFormat &fmt,
                        const List<V4l2M2mCodec::OutPlane> &dst, List<size_t> &bytesused) {
        bytesused.clear();
        if (dst.isEmpty()) return;

        const ImageDesc      &idesc = src.desc();
        const PixelMemLayout &ml = idesc.pixelFormat().memLayout();
        const uint32_t        w = idesc.size().width();
        const uint32_t        h = idesc.size().height();
        const uint32_t        cRows = h / fmt.chromaVDiv;
        const uint32_t        samplesPerRow = w; // luma and interleaved-CbCr both span w samples.

        const uint8_t *ySrc = src.plane(0).data();
        const uint8_t *cSrc = src.plane(1).data();
        const size_t   ySrcStride = ml.lineStride(0, w);
        const size_t   cSrcStride = ml.lineStride(1, w);

        if (dst.size() >= 2) {
                const size_t yDstStride = dst[0].stride ? dst[0].stride : samplesPerRow * fmt.bytesPerSample;
                const size_t cDstStride = dst[1].stride ? dst[1].stride : samplesPerRow * fmt.bytesPerSample;
                copyPlaneRows(dst[0].data, yDstStride, ySrc, ySrcStride, samplesPerRow, h, fmt.bytesPerSample,
                              fmt.shift, true);
                copyPlaneRows(dst[1].data, cDstStride, cSrc, cSrcStride, samplesPerRow, cRows,
                              fmt.bytesPerSample, fmt.shift, true);
                bytesused.pushToBack(yDstStride * h);
                bytesused.pushToBack(cDstStride * cRows);
        } else {
                const size_t stride = dst[0].stride ? dst[0].stride : samplesPerRow * fmt.bytesPerSample;
                copyPlaneRows(dst[0].data, stride, ySrc, ySrcStride, samplesPerRow, h, fmt.bytesPerSample,
                              fmt.shift, true);
                copyPlaneRows(dst[0].data + stride * h, stride, cSrc, cSrcStride, samplesPerRow, cRows,
                              fmt.bytesPerSample, fmt.shift, true);
                bytesused.pushToBack(stride * h + stride * cRows);
        }
}

void v4l2UnpackSemiPlanar(const List<V4l2M2mCodec::CapturePlane> &src, const V4l2RawFormat &fmt,
                          UncompressedVideoPayload *dst) {
        if (!dst || src.isEmpty()) return;

        const ImageDesc      &idesc = dst->desc();
        const PixelMemLayout &ml = idesc.pixelFormat().memLayout();
        const uint32_t        w = idesc.size().width();
        const uint32_t        h = idesc.size().height();
        const uint32_t        cRows = h / fmt.chromaVDiv;
        const uint32_t        samplesPerRow = w;

        uint8_t     *yDst = dst->data()[0].data();
        uint8_t     *cDst = dst->data()[1].data();
        const size_t yDstStride = ml.lineStride(0, w);
        const size_t cDstStride = ml.lineStride(1, w);

        const uint8_t *ySrc = src[0].data;
        const size_t   ySrcStride = src[0].stride ? src[0].stride : samplesPerRow * fmt.bytesPerSample;
        const uint8_t *cSrc = nullptr;
        size_t         cSrcStride = ySrcStride;
        if (src.size() >= 2) {
                cSrc = src[1].data;
                cSrcStride = src[1].stride ? src[1].stride : samplesPerRow * fmt.bytesPerSample;
        } else {
                cSrc = src[0].data + ySrcStride * h; // single contiguous plane: CbCr follows Y.
        }

        copyPlaneRows(yDst, yDstStride, ySrc, ySrcStride, samplesPerRow, h, fmt.bytesPerSample, fmt.shift,
                      false);
        copyPlaneRows(cDst, cDstStride, cSrc, cSrcStride, samplesPerRow, cRows, fmt.bytesPerSample, fmt.shift,
                      false);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
