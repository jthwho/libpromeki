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

                SUBCASE("YUV formats are not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV8_422_Rec709));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelDesc::YUV10_422_Rec709));
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
