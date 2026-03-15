/**
 * @file      image.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/proav/image.h>
#include <promeki/proav/imagefile.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/fontpainter.h>

using namespace promeki;

PROMEKI_DEBUG(ImageTest);

PROMEKI_TEST_BEGIN(Image)
        ImageDesc d(1920, 1080, PixelFormat::RGBA8);
        promekiInfo("ImageDesc: %s", d.toString().cstr());
        PROMEKI_TEST(d.size().isValid());

        Image img1(d);
        promekiInfo("Image: %s", img1.desc().toString().cstr());
        PROMEKI_TEST(d.pixelFormat()->lineStride(0, d) == 1920 * 4);
        PROMEKI_TEST(d.pixelFormat()->planeSize(0, d) == 1920 * 1080 * 4);
        PROMEKI_TEST(img1.size().isValid());
        PROMEKI_TEST(img1.desc().isValid());
        PROMEKI_TEST(img1.isValid());
        PROMEKI_TEST(img1.fill(42));

        char *data = static_cast<char *>(img1.plane().data());

        String hexDump;
        for(int i = 0; i < 16; i++) {
                hexDump += String::number(data[i], 16, 2, '0');
                hexDump += ' ';
        }
        promekiInfo("Data: %s", hexDump.cstr());
        PROMEKI_TEST(data[0] == 42);
        PROMEKI_TEST(data[1] == 42);
        PROMEKI_TEST(data[2] == 42);
        PROMEKI_TEST(data[3] == 42);
        
        PaintEngine p = img1.createPaintEngine();
        PaintEngine::Pixel pix1 = p.createPixel(1, 2, 3, 4);
        PROMEKI_TEST(p.fill(pix1));
        PROMEKI_TEST(data[0] == 1);
        PROMEKI_TEST(data[1] == 2);
        PROMEKI_TEST(data[2] == 3);
        PROMEKI_TEST(data[3] == 4);
        PROMEKI_TEST(data[4] == 1);
        PROMEKI_TEST(data[5] == 2);
        PROMEKI_TEST(data[6] == 3);
        PROMEKI_TEST(data[7] == 4);

        PaintEngine::Pixel black = p.createPixel(0x00, 0x00, 0x00);
        PaintEngine::Pixel red = p.createPixel(0xFF, 0x00, 0x00);
        PROMEKI_BENCHMARK_BEGIN(fill_black);
        PROMEKI_TEST(p.fill(black));
        PROMEKI_BENCHMARK_END(fill_black);

        PROMEKI_BENCHMARK_BEGIN(draw_lines);
        PROMEKI_TEST(p.drawLine(red, 0, 0, 1920, 1080) == 1);
        PROMEKI_TEST(p.drawLine(red, 0, 1080, 1920, 0) == 1);
        PROMEKI_BENCHMARK_END(draw_lines);

        PROMEKI_BENCHMARK_BEGIN(draw_text);
        FontPainter fp;
        fp.setPaintEngine(p);
        fp.setFontFilename("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
        fp.drawText("Hello World", 50, 50, 30);
        PROMEKI_BENCHMARK_END(draw_text);

        PROMEKI_BENCHMARK_BEGIN(save_png);
        ImageFile png(ImageFile::PNG);
        png.setFilename("test.png");
        png.setImage(img1);
        PROMEKI_TEST(png.save().isOk());
        PROMEKI_BENCHMARK_END(save_png);

PROMEKI_TEST_END()

