/**
 * @file      main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Image file I/O test utility.  Generates a test image with color bars,
 * R/G/B/luma ramps, geometric shapes, and colored text, then saves it
 * in every supported image file format for external validation.
 *
 * Usage: imgtest <output_dir>
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <promeki/imagefile.h>
#include <promeki/imagefileio.h>
#include <promeki/image.h>
#include <promeki/videotestpattern.h>
#include <promeki/paintengine.h>
#include <promeki/basicfont.h>
#include <promeki/color.h>
#include <promeki/string.h>
#include <promeki/rect.h>
#include <promeki/line.h>

using namespace promeki;

// Draw centered text, returns the Y advance for the next line.
static int drawCenteredText(BasicFont &font, const String &text, int cx, int y) {
        int tw = font.measureText(text);
        font.drawText(text, cx - tw / 2, y);
        return font.lineHeight();
}

// Build the master RGBA8 test image.
// Layout (640x480):
//   0..239   Color bars (top half)
//   240..319 R / G / B / Luma ramps (4 x 20px)
//   320..399 Geometric shapes (centered)
//   400..479 Text (centered)
static Image buildTestImage(size_t w, size_t h) {
        // Start with color bars in the top portion
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);
        ImageDesc desc(w, h, PixelDesc(PixelDesc::RGBA8_sRGB));
        Image img = gen.create(desc);

        PaintEngine pe = img.createPaintEngine();
        int iw = static_cast<int>(w);
        int ih = static_cast<int>(h);

        auto black = pe.createPixel(Color::Black);

        // --- R/G/B/Luma ramps ---
        int rampY = ih / 2;         // 240
        int rampH = 20;
        int rampMargin = 4;
        int rampX = rampMargin;
        int rampW = iw - rampMargin * 2;

        // Black background for ramp area
        pe.fillRect(black, Rect<int32_t>(0, rampY, iw, rampH * 4));

        // Draw ramps directly into the RGBA8 pixel buffer for efficiency
        uint8_t *pixels = static_cast<uint8_t *>(img.data());
        size_t stride = w * 4;
        for(int x = 0; x < rampW; ++x) {
                uint8_t v = static_cast<uint8_t>(x * 255 / (rampW - 1));
                int px = rampX + x;
                for(int dy = 0; dy < rampH; ++dy) {
                        // Red ramp
                        uint8_t *p = pixels + (rampY + dy) * stride + px * 4;
                        p[0] = v; p[1] = 0; p[2] = 0; p[3] = 255;
                        // Green ramp
                        p = pixels + (rampY + rampH + dy) * stride + px * 4;
                        p[0] = 0; p[1] = v; p[2] = 0; p[3] = 255;
                        // Blue ramp
                        p = pixels + (rampY + rampH * 2 + dy) * stride + px * 4;
                        p[0] = 0; p[1] = 0; p[2] = v; p[3] = 255;
                        // Luma ramp
                        p = pixels + (rampY + rampH * 3 + dy) * stride + px * 4;
                        p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
                }
        }

        // --- Geometric shapes (centered, row below ramps) ---
        int shapeY = rampY + rampH * 4 + 8;
        int shapeRowH = 50;

        // Black background for shapes area
        pe.fillRect(black, Rect<int32_t>(0, shapeY - 4, iw, shapeRowH + 8));

        auto red   = pe.createPixel(Color::Red);
        auto green = pe.createPixel(Color::Green);
        auto blue  = pe.createPixel(Color::Blue);
        auto white = pe.createPixel(Color::White);
        auto yellow = pe.createPixel(Color::Yellow);

        int cx = iw / 2;
        int shapeSpacing = 80;

        // Filled rectangles
        pe.fillRect(red,   Rect<int32_t>(cx - shapeSpacing * 3, shapeY, 40, 30));
        pe.fillRect(green, Rect<int32_t>(cx - shapeSpacing * 2, shapeY, 40, 30));
        pe.fillRect(blue,  Rect<int32_t>(cx - shapeSpacing,     shapeY, 40, 30));

        // Outlined rectangle
        pe.drawRect(yellow, Rect<int32_t>(cx, shapeY, 40, 30));

        // Filled and outlined circles
        pe.fillCircle(red,   Point2Di32(cx + shapeSpacing + 20,     shapeY + 15), 14);
        pe.drawCircle(white, Point2Di32(cx + shapeSpacing * 2 + 20, shapeY + 15), 14);

        // Diagonal lines
        pe.drawLine(white,  cx + shapeSpacing * 2 + 50, shapeY,
                            cx + shapeSpacing * 2 + 50 + 40, shapeY + 30);
        pe.drawLine(yellow, cx + shapeSpacing * 2 + 50 + 40, shapeY,
                            cx + shapeSpacing * 2 + 50, shapeY + 30);

        // --- Text (centered) ---
        int textY = shapeY + shapeRowH + 12;

        // Black background for text area
        pe.fillRect(black, Rect<int32_t>(0, textY - 4, iw, ih - textY + 4));

        BasicFont font(pe);

        // "Red  Green  Blue" in their respective colors
        font.setFontSize(36);
        int totalTextW = font.measureText("Red") + font.measureText("  ") +
                         font.measureText("Green") + font.measureText("  ") +
                         font.measureText("Blue");
        int tx = cx - totalTextW / 2;
        int ty = textY + font.ascender() + 4;

        font.setForegroundColor(Color::Red);
        font.drawText("Red", tx, ty);
        tx += font.measureText("Red  ");

        font.setForegroundColor(Color::Green);
        font.drawText("Green", tx, ty);
        tx += font.measureText("Green  ");

        font.setForegroundColor(Color::Blue);
        font.drawText("Blue", tx, ty);

        // Second line: white info text
        font.setFontSize(20);
        font.setForegroundColor(Color::White);
        ty += 44;
        drawCenteredText(font, "promeki image file I/O test", cx, ty);

        // Third line: yellow format list
        font.setForegroundColor(Color::Yellow);
        ty += 28;
        drawCenteredText(font, "DPX / Cineon / TGA / SGI / PNM", cx, ty);

        return img;
}

struct TestCase {
        const char    *filename;
        int            formatId;
        PixelDesc::ID  pixelDesc;
};

int main(int argc, char **argv) {
        if(argc < 2) {
                std::fprintf(stderr, "Usage: %s <output_dir>\n", argv[0]);
                return 1;
        }
        const char *outDir = argv[1];

        const size_t w = 640, h = 480;

        // Build the master RGBA8 test image
        Image master = buildTestImage(w, h);
        if(!master.isValid()) {
                std::fprintf(stderr, "Failed to build test image\n");
                return 1;
        }

        // Derive format-specific source images
        Image rgb8(w, h, PixelDesc(PixelDesc::RGB8_sRGB));
        {
                const uint8_t *src = static_cast<const uint8_t *>(master.data());
                uint8_t *dst = static_cast<uint8_t *>(rgb8.data());
                for(size_t i = 0; i < w * h; ++i) {
                        dst[i * 3 + 0] = src[i * 4 + 0];
                        dst[i * 3 + 1] = src[i * 4 + 1];
                        dst[i * 3 + 2] = src[i * 4 + 2];
                }
        }

        Image mono8(w, h, PixelDesc(PixelDesc::Mono8_sRGB));
        {
                const uint8_t *src = static_cast<const uint8_t *>(master.data());
                uint8_t *dst = static_cast<uint8_t *>(mono8.data());
                for(size_t i = 0; i < w * h; ++i) {
                        dst[i] = static_cast<uint8_t>(
                                (src[i * 4 + 0] * 77 + src[i * 4 + 1] * 150 + src[i * 4 + 2] * 29) >> 8);
                }
        }

        TestCase tests[] = {
                // DPX — use RGB (no alpha) to avoid GM's inverted-matte convention
                { "dpx_rgb8.dpx",      ImageFile::DPX, PixelDesc::RGB8_sRGB },
                { "dpx_rgb10_be.dpx",  ImageFile::DPX, PixelDesc::RGB10_DPX_sRGB },
                { "dpx_rgb16.dpx",     ImageFile::DPX, PixelDesc::RGB16_BE_sRGB },

                // TGA
                { "tga_rgba8.tga",     ImageFile::TGA, PixelDesc::RGBA8_sRGB },

                // SGI
                { "sgi_mono8.sgi",     ImageFile::SGI, PixelDesc::Mono8_sRGB },
                { "sgi_rgb8.sgi",      ImageFile::SGI, PixelDesc::RGB8_sRGB },
                { "sgi_rgba8.sgi",     ImageFile::SGI, PixelDesc::RGBA8_sRGB },

                // PNM
                { "pnm_rgb8.ppm",      ImageFile::PNM, PixelDesc::RGB8_sRGB },
                { "pnm_mono8.pgm",     ImageFile::PNM, PixelDesc::Mono8_sRGB },
        };

        int pass = 0, fail = 0;
        for(const auto &tc : tests) {
                char path[512];
                std::snprintf(path, sizeof(path), "%s/%s", outDir, tc.filename);

                const Image *src = &master;
                if(tc.pixelDesc == PixelDesc::RGB8_sRGB)  src = &rgb8;
                if(tc.pixelDesc == PixelDesc::Mono8_sRGB) src = &mono8;

                Image saveImg;
                if(src->pixelDesc().id() == tc.pixelDesc) {
                        saveImg = *src;
                } else {
                        saveImg = master.convert(PixelDesc(tc.pixelDesc), Metadata());
                        if(!saveImg.isValid()) {
                                saveImg = Image(w, h, PixelDesc(tc.pixelDesc));
                                if(saveImg.isValid()) {
                                        uint8_t *p = static_cast<uint8_t *>(saveImg.data());
                                        size_t bytes = saveImg.pixelDesc().pixelFormat().planeSize(0, w, h);
                                        for(size_t i = 0; i < bytes; ++i)
                                                p[i] = static_cast<uint8_t>((i * 7) & 0xFF);
                                }
                        }
                }

                ImageFile f(tc.formatId);
                f.setFilename(path);
                f.setImage(saveImg);
                Error err = f.save();
                if(err.isError()) {
                        std::fprintf(stderr, "FAIL %-30s  %s\n", tc.filename, err.name().cstr());
                        ++fail;
                } else {
                        std::printf("OK   %-30s  %zux%zu %s\n", tc.filename,
                                    w, h, PixelDesc(tc.pixelDesc).name().cstr());
                        ++pass;
                }
        }

        std::printf("\n%d/%d files generated in %s\n", pass, pass + fail, outDir);
        return fail > 0 ? 1 : 0;
}
