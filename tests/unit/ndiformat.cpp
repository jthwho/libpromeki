/**
 * @file      ndiformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <doctest/doctest.h>
#include <promeki/ndiformat.h>
#include <promeki/pixelformat.h>

using namespace promeki;

namespace {

        // Local copy of NDI's NDI_LIB_FOURCC macro so the test does not
        // need to include the SDK header.  NDI packs FourCCs little-
        // endian into a uint32 — the byte 'a' lands in the low byte.
        constexpr uint32_t fourcc(char a, char b, char c, char d) {
                return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
                       (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
        }

        constexpr uint32_t kFourCC_UYVY = fourcc('U', 'Y', 'V', 'Y');
        constexpr uint32_t kFourCC_NV12 = fourcc('N', 'V', '1', '2');
        constexpr uint32_t kFourCC_I420 = fourcc('I', '4', '2', '0');
        constexpr uint32_t kFourCC_BGRA = fourcc('B', 'G', 'R', 'A');
        constexpr uint32_t kFourCC_RGBA = fourcc('R', 'G', 'B', 'A');
        constexpr uint32_t kFourCC_BGRX = fourcc('B', 'G', 'R', 'X');
        constexpr uint32_t kFourCC_P216 = fourcc('P', '2', '1', '6');
        constexpr uint32_t kFourCC_YV12 = fourcc('Y', 'V', '1', '2');

} // namespace

TEST_CASE("NdiFormat: 8-bit FourCCs round-trip") {
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_UYVY) == PixelFormat::YUV8_422_UYVY_Rec709);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::YUV8_422_UYVY_Rec709) == kFourCC_UYVY);

        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_NV12) == PixelFormat::YUV8_420_SemiPlanar_Rec709);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::YUV8_420_SemiPlanar_Rec709) == kFourCC_NV12);

        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_I420) == PixelFormat::YUV8_420_Planar_Rec709);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::YUV8_420_Planar_Rec709) == kFourCC_I420);

        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_BGRA) == PixelFormat::BGRA8_sRGB);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::BGRA8_sRGB) == kFourCC_BGRA);

        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_RGBA) == PixelFormat::RGBA8_sRGB);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::RGBA8_sRGB) == kFourCC_RGBA);
}

TEST_CASE("NdiFormat: BGRX aliases to BGRA on receive (X treated as opaque alpha)") {
        // BGRA8_sRGB is the canonical promeki ID for both directions —
        // the receiver hands data straight through as BGRA, the sender
        // doesn't get a way to express the X-vs-A distinction.
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_BGRX) == PixelFormat::BGRA8_sRGB);
}

TEST_CASE("NdiFormat: P216 picks the right semi-planar 4:2:2 entry by bit depth") {
        // Default (Auto) gives 16-bit so receivers are precision-honest
        // about what's on the wire.
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_P216) == PixelFormat::YUV16_422_SemiPlanar_LE_Rec709);
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_P216, NdiFormat::BitDepthAuto) ==
              PixelFormat::YUV16_422_SemiPlanar_LE_Rec709);
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_P216, NdiFormat::BitDepth16) ==
              PixelFormat::YUV16_422_SemiPlanar_LE_Rec709);
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_P216, NdiFormat::BitDepth12) ==
              PixelFormat::YUV12_422_SemiPlanar_LE_Rec709);
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_P216, NdiFormat::BitDepth10) ==
              PixelFormat::YUV10_422_SemiPlanar_LE_Rec709);
}

TEST_CASE("NdiFormat: every 10/12/16-bit semi-planar 4:2:2 promeki format maps to P216") {
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::YUV10_422_SemiPlanar_LE_Rec709) == kFourCC_P216);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::YUV12_422_SemiPlanar_LE_Rec709) == kFourCC_P216);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::YUV16_422_SemiPlanar_LE_Rec709) == kFourCC_P216);
}

TEST_CASE("NdiFormat: deferred FourCCs return Invalid") {
        // YV12, UYVA, PA16 are intentionally unsupported in v1 — see
        // the deferred-work list in the NDI MediaIO plan / docs/ndi.md.
        CHECK(NdiFormat::fourccToPixelFormat(kFourCC_YV12) == PixelFormat::Invalid);
}

TEST_CASE("NdiFormat: unknown PixelFormat returns 0") {
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::Invalid) == 0u);
        CHECK(NdiFormat::pixelFormatToFourcc(PixelFormat::H264) == 0u);
}

TEST_CASE("NdiFormat: fourccToString prints the ASCII bytes for known tags") {
        CHECK(NdiFormat::fourccToString(kFourCC_UYVY) == "UYVY");
        CHECK(NdiFormat::fourccToString(kFourCC_P216) == "P216");
        CHECK(NdiFormat::fourccToString(kFourCC_NV12) == "NV12");
}

TEST_CASE("NdiFormat: fourccToString falls back to hex for non-ASCII values") {
        // Non-printable bytes — should produce a hex dump.
        CHECK(NdiFormat::fourccToString(0x00000000) == "0x00000000");
        CHECK(NdiFormat::fourccToString(0xdeadbeef) == "0xdeadbeef");
}

#endif // PROMEKI_ENABLE_NDI
