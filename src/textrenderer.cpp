/**
 * @file      textrenderer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/thirdparty/freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include <promeki/proav/textrenderer.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

TextRenderer::TextRenderer() {

}

TextRenderer::~TextRenderer() {
        invalidateAll();
}

void TextRenderer::setFontFilename(const String &val) {
        if(_fontFilename == val) return;
        _fontFilename = val;
        invalidateAll();
}

void TextRenderer::setFontSize(int val) {
        if(_fontSize == val) return;
        _fontSize = val;
        invalidateFont();
}

void TextRenderer::setForegroundColor(const Color &color) {
        if(_fg == color) return;
        _fg = color;
        invalidateGlyphs();
}

void TextRenderer::setBackgroundColor(const Color &color) {
        if(_bg == color) return;
        _bg = color;
        invalidateGlyphs();
}

void TextRenderer::setPaintEngine(const PaintEngine &pe) {
        _paintEngine = pe;
        invalidateGlyphs();
}

void TextRenderer::invalidateGlyphs() {
        _glyphCache.clear();
        _pixelsDirty = true;
}

void TextRenderer::invalidateFont() {
        invalidateGlyphs();
        if(_ftFace != nullptr) {
                FT_Done_Face(static_cast<FT_Face>(_ftFace));
                _ftFace = nullptr;
        }
        _ascender = 0;
        _descender = 0;
        _lineHeight = 0;
}

void TextRenderer::invalidateAll() {
        invalidateFont();
        if(_ftLibrary != nullptr) {
                FT_Done_FreeType(static_cast<FT_Library>(_ftLibrary));
                _ftLibrary = nullptr;
        }
}

bool TextRenderer::ensureFontLoaded() {
        if(_ftFace != nullptr) return true;

        if(_ftLibrary == nullptr) {
                FT_Library ft;
                if(FT_Init_FreeType(&ft)) {
                        promekiErr("Could not init FreeType library");
                        return false;
                }
                _ftLibrary = ft;
        }

        FT_Library ft = static_cast<FT_Library>(_ftLibrary);
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

        return true;
}

void TextRenderer::ensurePixels() {
        if(!_pixelsDirty) return;
        _fgPixel = _paintEngine.createPixel(_fg);
        _bgPixel = _paintEngine.createPixel(_bg);
        _pixelsDirty = false;
}

const TextRenderer::CachedGlyph *TextRenderer::getGlyph(uint32_t codepoint) {
        auto it = _glyphCache.find(codepoint);
        if(it != _glyphCache.end()) return &it->second;

        FT_Face face = static_cast<FT_Face>(_ftFace);
        if(FT_Load_Char(face, codepoint, FT_LOAD_RENDER)) {
                promekiWarn("Could not load character 0x%X in '%s'",
                        (unsigned int)codepoint, _fontFilename.cstr());
                return nullptr;
        }

        FT_Bitmap *bitmap = &face->glyph->bitmap;
        int bitmapLeft = face->glyph->bitmap_left;
        int bitmapTop = face->glyph->bitmap_top;
        int advanceX = face->glyph->advance.x >> 6;

        // Create a temporary Image for this glyph cell and render into it
        // using the PaintEngine, which handles all pixel format specifics.
        const PixelFormat *pf = _paintEngine.pixelFormat();
        int cellWidth = advanceX;
        if(cellWidth <= 0) cellWidth = 1;
        Image glyphImg(cellWidth, _lineHeight, pf->id());

        PaintEngine pe = glyphImg.createPaintEngine();

        // Fill entire cell with background color
        pe.fill(_bgPixel);

        // Build point and alpha lists from the FreeType bitmap, positioned
        // within the cell at (bitmapLeft, ascender - bitmapTop).
        int originX = bitmapLeft;
        int originY = _ascender - bitmapTop;

        List<Point2Di32> points;
        List<float> alphas;

        for(unsigned int row = 0; row < bitmap->rows; ++row) {
                int cellY = originY + row;
                if(cellY < 0 || cellY >= _lineHeight) continue;
                for(unsigned int col = 0; col < bitmap->width; ++col) {
                        int cellX = originX + col;
                        if(cellX < 0 || cellX >= cellWidth) continue;

                        uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                        if(alpha == 0) continue;

                        points += Point2Di32(cellX, cellY);
                        alphas += static_cast<float>(alpha) / 255.0f;
                }
        }

        pe.compositePoints(_fgPixel, points, alphas);

        // Cache the rendered glyph image
        CachedGlyph glyph;
        glyph.image = glyphImg;
        glyph.advanceX = advanceX;

        _glyphCache.insert(codepoint, glyph);
        return &_glyphCache[codepoint];
}

bool TextRenderer::drawText(const String &text, int x, int y) {
        if(!ensureFontLoaded()) return false;
        ensurePixels();

        // y is baseline; top of cell is at y - ascender
        int cellTop = y - _ascender;
        int penX = x;

        for(Char c : text) {
                const CachedGlyph *glyph = getGlyph(c.codepoint());
                if(glyph == nullptr) continue;

                _paintEngine.blit(Point2Di32(penX, cellTop), glyph->image);
                penX += glyph->advanceX;
        }

        return true;
}

int TextRenderer::measureText(const String &text) {
        if(!ensureFontLoaded()) return 0;
        ensurePixels();

        int width = 0;
        for(Char c : text) {
                const CachedGlyph *glyph = getGlyph(c.codepoint());
                if(glyph == nullptr) continue;
                width += glyph->advanceX;
        }
        return width;
}

PROMEKI_NAMESPACE_END
