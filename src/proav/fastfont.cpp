/**
 * @file      fastfont.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#include <promeki/fastfont.h>
#include <promeki/file.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

FastFont::FastFont(const PaintEngine &pe) : Font(pe) {

}

FastFont::~FastFont() {
        invalidateAll();
}

void FastFont::onStateChanged() {
        invalidateAll();
}

void FastFont::invalidateGlyphs() {
        _glyphCache.clear();
        _pixelsDirty = true;
}

void FastFont::invalidateFont() {
        invalidateGlyphs();
        if(_ftFace != nullptr) {
                FT_Done_Face(static_cast<FT_Face>(_ftFace));
                _ftFace = nullptr;
        }
        // Free the font byte buffer only after the FT_Face has been
        // destroyed — FT_New_Memory_Face borrows the bytes and reads
        // from them on demand.
        _fontData = Buffer();
        _ascender = 0;
        _descender = 0;
        _lineHeight = 0;
}

void FastFont::invalidateAll() {
        invalidateFont();
        if(_ftLibrary != nullptr) {
                FT_Done_FreeType(static_cast<FT_Library>(_ftLibrary));
                _ftLibrary = nullptr;
        }
}

bool FastFont::ensureFontLoaded() {
        if(_ftFace != nullptr) return true;

        if(_ftLibrary == nullptr) {
                FT_Library ft;
                if(FT_Init_FreeType(&ft)) {
                        promekiErr("Could not init FreeType library");
                        return false;
                }
                _ftLibrary = ft;
        }

        // Load the font bytes via promeki::File so the same code path
        // serves filesystem fonts and ":/.PROMEKI/fonts/..." resources.
        // When _fontFilename is empty the base Font class hands us
        // the bundled default via effectiveFilename(). The buffer is
        // held on the FastFont so it outlives the FT_Face that
        // borrows it.
        const String path = effectiveFilename();
        File f(path);
        Error openErr = f.open(File::ReadOnly);
        if(openErr.isError()) {
                promekiErr("Could not open font '%s': %s",
                        path.cstr(), openErr.name().cstr());
                return false;
        }
        _fontData = f.readAll();
        f.close();
        if(!_fontData.isValid() || _fontData.size() == 0) {
                promekiErr("Font '%s' is empty", path.cstr());
                return false;
        }

        FT_Library ft = static_cast<FT_Library>(_ftLibrary);
        FT_Face face;
        if(FT_New_Memory_Face(ft,
                              static_cast<const FT_Byte *>(_fontData.data()),
                              static_cast<FT_Long>(_fontData.size()),
                              0, &face)) {
                promekiErr("Could not parse font '%s'", path.cstr());
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

void FastFont::ensurePixels() {
        if(!_pixelsDirty) return;
        _fgPixel = _paintEngine.createPixel(_fg);
        _bgPixel = _paintEngine.createPixel(_bg);
        _pixelsDirty = false;
}

const FastFont::CachedGlyph *FastFont::getGlyph(uint32_t codepoint) {
        auto it = _glyphCache.find(codepoint);
        if(it != _glyphCache.end()) return &it->second;

        FT_Face face = static_cast<FT_Face>(_ftFace);
        if(FT_Load_Char(face, codepoint, FT_LOAD_RENDER)) {
                promekiWarn("Could not load character 0x%X in '%s'",
                        (unsigned int)codepoint, effectiveFilename().cstr());
                return nullptr;
        }

        FT_Bitmap *bitmap = &face->glyph->bitmap;
        int bitmapLeft = face->glyph->bitmap_left;
        int bitmapTop = face->glyph->bitmap_top;
        int advanceX = face->glyph->advance.x >> 6;

        // Create a temporary Image for this glyph cell and render into it
        // using the PaintEngine, which handles all pixel format specifics.
        int cellWidth = advanceX;
        if(cellWidth <= 0) cellWidth = 1;
        Image glyphImg(cellWidth, _lineHeight, _paintEngine.pixelDesc().id());

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

bool FastFont::drawText(const String &text, int x, int y) {
        if(!ensureFontLoaded()) return false;
        ensurePixels();

        // y is baseline; top of cell is at y - ascender
        int cellTop = y - _ascender;
        int penX = x;

        FT_Face face = _kerning ? static_cast<FT_Face>(_ftFace) : nullptr;
        bool hasKerning = face != nullptr && FT_HAS_KERNING(face);
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

                const CachedGlyph *glyph = getGlyph(c.codepoint());
                if(glyph == nullptr) continue;

                _paintEngine.blit(Point2Di32(penX, cellTop), glyph->image);
                penX += glyph->advanceX;

                if(hasKerning) prevIndex = glyphIndex;
        }

        return true;
}

int FastFont::measureText(const String &text) {
        if(!ensureFontLoaded()) return 0;
        ensurePixels();

        int width = 0;

        FT_Face face = _kerning ? static_cast<FT_Face>(_ftFace) : nullptr;
        bool hasKerning = face != nullptr && FT_HAS_KERNING(face);
        FT_UInt prevIndex = 0;

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

                const CachedGlyph *glyph = getGlyph(c.codepoint());
                if(glyph == nullptr) continue;
                width += glyph->advanceX;

                if(hasKerning) prevIndex = glyphIndex;
        }
        return width;
}

int FastFont::lineHeight() {
        ensureFontLoaded();
        return _lineHeight;
}

int FastFont::ascender() {
        ensureFontLoaded();
        return _ascender;
}

int FastFont::descender() {
        ensureFontLoaded();
        return _descender;
}

PROMEKI_NAMESPACE_END
