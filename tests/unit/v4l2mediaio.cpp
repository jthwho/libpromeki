/**
 * @file      tests/v4l2mediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Coverage for V4l2MediaIO policy helpers that do not require a real
 * /dev/video device.  In particular the dma-buf zero-copy gate:
 * compressed captures (MJPEG, H.264, …) must NOT be handed downstream as
 * an opaque dma-buf fd, because the host JPEG/H.264 decoder reads them
 * through Buffer::data(), which is null for an unmapped dma-buf — libjpeg
 * then aborts with "Empty input file" even though the payload reports the
 * full byte count.  Regression for `mediaplay -s /dev/video0` dying on
 * the first MJPEG frame on a build with PROMEKI_ENABLE_DMABUF.
 */

#include <doctest/doctest.h>

#include <promeki/v4l2mediaio.h>
#include <promeki/pixelformat.h>

using namespace promeki;

// ---------------------------------------------------------------------------
// Zero-copy capture gate
// ---------------------------------------------------------------------------

TEST_CASE("V4l2MediaIO_CompressedCaptureNeverZeroCopy") {
        // Every compressed capture format the V4L2 backend can negotiate
        // must stay on the host-readable MMAP path.
        const PixelFormat::ID compressed[] = {
                PixelFormat::JPEG_YUV8_422_Rec709,
                PixelFormat::JPEG_YUV8_420_Rec709,
                PixelFormat::JPEG_YUV8_422_Rec601,
                PixelFormat::JPEG_RGB8_sRGB,
        };
        for (PixelFormat::ID id : compressed) {
                PixelFormat pd(id);
                REQUIRE(pd.isCompressed());
                CHECK_FALSE(V4l2MediaIO::captureFormatAllowsZeroCopy(pd));
        }
}

TEST_CASE("V4l2MediaIO_UncompressedCaptureMayZeroCopy") {
        // Uncompressed capture formats can take the dma-buf zero-copy path
        // (subject to driver EXPBUF support, decided separately at runtime).
        const PixelFormat::ID uncompressed[] = {
                PixelFormat::YUV8_422_Rec709,
                PixelFormat::YUV8_422_UYVY_Rec709,
                PixelFormat::RGB8_sRGB,
                PixelFormat::RGBA8_sRGB,
        };
        for (PixelFormat::ID id : uncompressed) {
                PixelFormat pd(id);
                REQUIRE_FALSE(pd.isCompressed());
                CHECK(V4l2MediaIO::captureFormatAllowsZeroCopy(pd));
        }
}
