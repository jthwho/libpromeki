/**
 * @file      sdlrenderer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/pixeldesc.h>

using namespace promeki;

TEST_SUITE("SDLVideoWidget") {

        TEST_CASE("Pixel format mapping") {
                SUBCASE("RGBA8 maps to SDL format") {
                        uint32_t fmt = SDLVideoWidget::mapPixelDesc(PixelDesc::RGBA8_sRGB);
                        CHECK(fmt != 0);
                }

                SUBCASE("RGB8 maps to SDL format") {
                        uint32_t fmt = SDLVideoWidget::mapPixelDesc(PixelDesc::RGB8_sRGB);
                        CHECK(fmt != 0);
                }

                SUBCASE("8-bit YUV formats map directly to SDL YUV formats") {
                        // SDL3 has native YUV texture support — the
                        // widget exposes these on the fast path and
                        // SDL performs the YCbCr->RGB conversion on
                        // the GPU at render time.
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_422_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_422_UYVY_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_420_SemiPlanar_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_420_NV21_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_420_Planar_Rec709));
                        CHECK(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_422_Rec601));
                }

                SUBCASE("8-bit YUV formats advertise the right SDL colorspace") {
                        // Rec.709 limited -> BT709_LIMITED.
                        CHECK(SDLVideoWidget::mapColorspace(PixelDesc::YUV8_422_Rec709) != 0);
                        CHECK(SDLVideoWidget::mapColorspace(PixelDesc::YUV8_422_UYVY_Rec709) != 0);
                        CHECK(SDLVideoWidget::mapColorspace(PixelDesc::YUV8_420_Planar_Rec709) != 0);
                        // Rec.601 limited -> BT601_LIMITED.
                        CHECK(SDLVideoWidget::mapColorspace(PixelDesc::YUV8_422_Rec601) != 0);
                        // RGB formats don't override the SDL default.
                        CHECK(SDLVideoWidget::mapColorspace(PixelDesc::RGBA8_sRGB) == 0);
                        CHECK(SDLVideoWidget::mapColorspace(PixelDesc::RGB8_sRGB) == 0);
                }

                SUBCASE("10/12-bit YUV and 422 planar/NV16 fall through") {
                        // No SDL equivalents for these — they take the
                        // CSC fallback path through Image::convert().
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV10_422_Rec709));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_422_Planar_Rec709));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_422_SemiPlanar_Rec709));
                }

                SUBCASE("RGB10 is not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::RGB10_DPX_sRGB));
                }

                SUBCASE("Compressed formats are not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::JPEG_RGBA8_sRGB));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::JPEG_RGB8_sRGB));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::JPEG_YUV8_422_Rec709));
                }

                SUBCASE("Invalid format returns zero") {
                        CHECK(SDLVideoWidget::mapPixelDesc(PixelDesc::Invalid) == 0);
                }
        }

}
