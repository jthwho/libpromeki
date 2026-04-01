/**
 * @file      sdlrenderer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/proav/pixelformat.h>

using namespace promeki;

TEST_SUITE("SDLVideoWidget") {

        TEST_CASE("Pixel format mapping") {
                SUBCASE("RGBA8 maps to SDL format") {
                        uint32_t fmt = SDLVideoWidget::mapPixelFormat(PixelFormat::RGBA8);
                        CHECK(fmt != 0);
                }

                SUBCASE("RGB8 maps to SDL format") {
                        uint32_t fmt = SDLVideoWidget::mapPixelFormat(PixelFormat::RGB8);
                        CHECK(fmt != 0);
                }

                SUBCASE("YUV formats are not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV8_422));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::YUV10_422));
                }

                SUBCASE("RGB10 is not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::RGB10));
                }

                SUBCASE("Compressed formats are not directly mappable") {
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::JPEG_RGBA8));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::JPEG_RGB8));
                        CHECK_FALSE(SDLVideoWidget::isDirectlyMappable(PixelFormat::JPEG_YUV8_422));
                }

                SUBCASE("Invalid format returns zero") {
                        CHECK(SDLVideoWidget::mapPixelFormat(PixelFormat::Invalid) == 0);
                }
        }

}
