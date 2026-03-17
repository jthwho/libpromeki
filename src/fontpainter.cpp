/**
 * @file      fontpainter.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/thirdparty/freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include <promeki/proav/fontpainter.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

FontPainter::FontPainter() {

}

FontPainter::~FontPainter() {

}

bool FontPainter::drawText(const String &str, int x, int y, int pointSize) const {
        FT_Library ft;
        if (FT_Init_FreeType(&ft)) {
                promekiErr("Could not init FreeType library");
                return false;
        }

        FT_Face face;
        if (FT_New_Face(ft, _fontFilename.cstr(), 0, &face)) {
                promekiErr("Could not open font '%s'", _fontFilename.cstr());
                return false;
        }

        FT_Set_Pixel_Sizes(face, 0, pointSize);

        int pen_x = x;
        int pen_y = y;
        List<Point2Di32> points;
        List<float> alphas;

        for(Char c : str) {
                if (FT_Load_Char(face, c.codepoint(), FT_LOAD_RENDER)) {
                        promekiWarn("Could not load character 0x%X in '%s'", (unsigned int)c.codepoint(), _fontFilename.cstr());
                        continue;
                }

                FT_Bitmap* bitmap = &face->glyph->bitmap;
                FT_Int bitmap_left = face->glyph->bitmap_left;
                FT_Int bitmap_top = face->glyph->bitmap_top;

                for (int row = 0; row < bitmap->rows; ++row) {
                        for (int col = 0; col < bitmap->width; ++col) {
                                int x_pixel = pen_x + bitmap_left + col;
                                int y_pixel = pen_y - bitmap_top + row;
                                uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                                if(alpha > 0) {
                                        points += Point2Di32(x_pixel, y_pixel);
                                        alphas += (float)alpha / 255.0;
                                } 
                        }
                }
                pen_x += face->glyph->advance.x >> 6;
                pen_y += face->glyph->advance.y >> 6;
        }
        FT_Done_Face(face);
        FT_Done_FreeType(ft);

        PaintEngine::Pixel pix = _paintEngine.createPixel(65535, 65535, 65535);
        _paintEngine.compositePoints(pix, points, alphas);

        return true;
}

int FontPainter::measureText(const String &str, int pointSize) const {
        FT_Library ft;
        if(FT_Init_FreeType(&ft)) return 0;

        FT_Face face;
        if(FT_New_Face(ft, _fontFilename.cstr(), 0, &face)) {
                FT_Done_FreeType(ft);
                return 0;
        }

        FT_Set_Pixel_Sizes(face, 0, pointSize);

        int width = 0;
        for(Char c : str) {
                if(FT_Load_Char(face, c.codepoint(), FT_LOAD_DEFAULT)) continue;
                width += face->glyph->advance.x >> 6;
        }

        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return width;
}

PROMEKI_NAMESPACE_END

