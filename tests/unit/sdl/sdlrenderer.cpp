/**
 * @file      sdlrenderer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/pixelformat.h>

using namespace promeki;

TEST_SUITE("SDLVideoWidget") {

        TEST_CASE("Pixel format mapping") {
                SUBCASE("RGBA8 maps to SDL format") {
                        uint32_t fmt = SDLVideoWidget::mapPixelFormat(PixelFormat::RGBA8_sRGB);
                        CHECK(fmt != 0);
                }

                SUBCASE("RGB8 maps to SDL format") {
                        uint32_t fmt = SDLVideoWidget::mapPixelFormat(PixelFormat::RGB8_sRGB);
                        CHECK(fmt != 0);
                }

                SUBCASE("8-bit YUV formats map directly to SDL YUV formats") {
                        // SDL3 has native YUV texture support — the
                        // widget exposes these on the fast path and
                        // SDL performs the YCbCr->RGB conversion on
                        // the GPU at render time.
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_422_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_422_UYVY_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_420_SemiPlanar_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_420_NV21_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_420_Planar_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_422_Rec601));
                }

                SUBCASE("8-bit YUV formats advertise the right SDL colorspace") {
                        // Rec.709 limited -> BT709_LIMITED.
                        CHECK(SDLVideoWidget::mapColorspace(PixelFormat::YUV8_422_Rec709) != 0);
                        CHECK(SDLVideoWidget::mapColorspace(PixelFormat::YUV8_422_UYVY_Rec709) != 0);
                        CHECK(SDLVideoWidget::mapColorspace(PixelFormat::YUV8_420_Planar_Rec709) != 0);
                        // Rec.601 limited -> BT601_LIMITED.
                        CHECK(SDLVideoWidget::mapColorspace(PixelFormat::YUV8_422_Rec601) != 0);
                        // RGB formats don't override the SDL default.
                        CHECK(SDLVideoWidget::mapColorspace(PixelFormat::RGBA8_sRGB) == 0);
                        CHECK(SDLVideoWidget::mapColorspace(PixelFormat::RGB8_sRGB) == 0);
                }

                SUBCASE("10/12-bit YUV and 422 planar/NV16 fall through") {
                        // No SDL equivalents for these — they take the
                        // CSC fallback path through UncompressedVideoPayload::convert().
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV10_422_Rec709));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_422_Planar_Rec709));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_422_SemiPlanar_Rec709));
                }

                SUBCASE("RGB10 is not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::RGB10_DPX_sRGB));
                }

                SUBCASE("Compressed formats are not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::JPEG_RGBA8_sRGB));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::JPEG_RGB8_sRGB));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::JPEG_YUV8_422_Rec709));
                }

                SUBCASE("Invalid format returns zero") {
                        CHECK(SDLVideoWidget::mapPixelFormat(PixelFormat::Invalid) == 0);
                }
        }
}
