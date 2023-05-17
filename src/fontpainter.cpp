/*****************************************************************************
 * fontpainter.cpp
 * May 16, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include <promeki/fontpainter.h>
#include <promeki/logger.h>

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
        List<Point2D> points;
        List<float> alphas;

        for(char c : str) {
                if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                        promekiWarn("Could not load character 0x%X in '%s'", (unsigned int)c, _fontFilename.cstr());
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
                                        points += Point2D(x_pixel, y_pixel);
                                        alphas += (float)alpha / 255.0;
                                } 
                        }
                }
                pen_x += face->glyph->advance.x >> 6;
                pen_y += face->glyph->advance.y >> 6;
        }
        FT_Done_Face(face);
        FT_Done_FreeType(ft);

        PaintEngine::Pixel pix = _paintEngine.createPixel(0xFF, 0xFF, 0xFF);
        _paintEngine.compositePoints(pix, points, alphas);

        return true;
}

PROMEKI_NAMESPACE_END

