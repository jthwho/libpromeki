/**
 * @file      basicfont.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include <promeki/basicfont.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

BasicFont::BasicFont(const PaintEngine &pe) : Font(pe) {

}

BasicFont::~BasicFont() {
        releaseFont();
}

void BasicFont::onStateChanged() {
        _fontDirty = true;
}

void BasicFont::releaseFont() {
        if(_ftFace != nullptr) {
                FT_Done_Face(static_cast<FT_Face>(_ftFace));
                _ftFace = nullptr;
        }
        if(_ftLibrary != nullptr) {
                FT_Done_FreeType(static_cast<FT_Library>(_ftLibrary));
                _ftLibrary = nullptr;
        }
        _ascender = 0;
        _descender = 0;
        _lineHeight = 0;
}

bool BasicFont::ensureFontLoaded() {
        if(!_fontDirty && _ftFace != nullptr) return true;

        releaseFont();

        FT_Library ft;
        if(FT_Init_FreeType(&ft)) {
                promekiErr("Could not init FreeType library");
                return false;
        }
        _ftLibrary = ft;

        FT_Face face;
        if(FT_New_Face(ft, _fontFilename.cstr(), 0, &face)) {
                promekiErr("Could not open font '%s'", _fontFilename.cstr());
                return false;
        }
        _ftFace = face;

        FT_Set_Pixel_Sizes(face, 0, _fontSize);

        // Cache font metrics (FreeType reports these in 26.6 fixed point)
        _ascender = face->size->metrics.ascender >> 6;
        _descender = -(face->size->metrics.descender >> 6);
        _lineHeight = _ascender + _descender;

        _fontDirty = false;
        return true;
}

bool BasicFont::drawText(const String &text, int x, int y) {
        if(!ensureFontLoaded()) return false;

        FT_Face face = static_cast<FT_Face>(_ftFace);
        int penX = x;
        int penY = y;
        List<Point2Di32> points;
        List<float> alphas;

        bool hasKerning = _kerning && FT_HAS_KERNING(face);
        FT_UInt prevIndex = 0;

        for(Char c : text) {
                FT_UInt glyphIndex = 0;
                if(hasKerning) {
                        glyphIndex = FT_Get_Char_Index(face, c.codepoint());
                        if(prevIndex != 0 && glyphIndex != 0) {
                                FT_Vector delta;
                                FT_Get_Kerning(face, prevIndex, glyphIndex, FT_KERNING_DEFAULT, &delta);
                                penX += delta.x >> 6;
                        }
                }

                if(FT_Load_Char(face, c.codepoint(), FT_LOAD_RENDER)) {
                        promekiWarn("Could not load character 0x%X in '%s'",
                                (unsigned int)c.codepoint(), _fontFilename.cstr());
                        continue;
                }

                FT_Bitmap *bitmap = &face->glyph->bitmap;
                FT_Int bitmapLeft = face->glyph->bitmap_left;
                FT_Int bitmapTop = face->glyph->bitmap_top;

                for(unsigned int row = 0; row < bitmap->rows; ++row) {
                        for(unsigned int col = 0; col < bitmap->width; ++col) {
                                int xPixel = penX + bitmapLeft + col;
                                int yPixel = penY - bitmapTop + row;
                                uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                                if(alpha > 0) {
                                        points += Point2Di32(xPixel, yPixel);
                                        alphas += static_cast<float>(alpha) / 255.0f;
                                }
                        }
                }
                penX += face->glyph->advance.x >> 6;
                penY += face->glyph->advance.y >> 6;

                if(hasKerning) prevIndex = glyphIndex;
        }

        PaintEngine::Pixel pix = _paintEngine.createPixel(_fg);
        _paintEngine.compositePoints(pix, points, alphas);

        return true;
}

int BasicFont::measureText(const String &text) {
        if(!ensureFontLoaded()) return 0;

        FT_Face face = static_cast<FT_Face>(_ftFace);
        bool hasKerning = _kerning && FT_HAS_KERNING(face);
        FT_UInt prevIndex = 0;

        int width = 0;
        for(Char c : text) {
                FT_UInt glyphIndex = 0;
                if(hasKerning) {
                        glyphIndex = FT_Get_Char_Index(face, c.codepoint());
                        if(prevIndex != 0 && glyphIndex != 0) {
                                FT_Vector delta;
                                FT_Get_Kerning(face, prevIndex, glyphIndex, FT_KERNING_DEFAULT, &delta);
                                width += delta.x >> 6;
                        }
                }

                if(FT_Load_Char(face, c.codepoint(), FT_LOAD_DEFAULT)) continue;
                width += face->glyph->advance.x >> 6;

                if(hasKerning) prevIndex = glyphIndex;
        }

        return width;
}

int BasicFont::lineHeight() {
        ensureFontLoaded();
        return _lineHeight;
}

int BasicFont::ascender() {
        ensureFontLoaded();
        return _ascender;
}

int BasicFont::descender() {
        ensureFontLoaded();
        return _descender;
}

PROMEKI_NAMESPACE_END
