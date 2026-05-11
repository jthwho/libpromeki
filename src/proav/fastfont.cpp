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
#include <promeki/pixelmemlayout.h>

namespace {
        // Round @c v up to the nearest multiple of @c a.  Safe for
        // @c a == 1 (returns @c v unchanged) and for negative @c v
        // (rounds toward +inf so kerning that pushes penX backwards
        // by less than an alignment quantum collapses to zero rather
        // than overshooting in the wrong direction).
        inline int roundUpToAlign(int v, int a) {
                if (a <= 1) return v;
                if (v >= 0) return ((v + a - 1) / a) * a;
                return -(((-v) / a) * a);
        }
        // Round @c v down to the nearest multiple of @c a.  Symmetric
        // partner to @c roundUpToAlign.
        inline int roundDownToAlign(int v, int a) {
                if (a <= 1) return v;
                if (v >= 0) return (v / a) * a;
                return -(((-v + a - 1) / a) * a);
        }
}

PROMEKI_NAMESPACE_BEGIN

FastFont::FastFont(const PaintEngine &pe) : Font(pe) {}

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
        if (_ftFace != nullptr) {
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
        if (_ftLibrary != nullptr) {
                FT_Done_FreeType(static_cast<FT_Library>(_ftLibrary));
                _ftLibrary = nullptr;
        }
}

bool FastFont::ensureFontLoaded() {
        if (_ftFace != nullptr) return true;

        if (_ftLibrary == nullptr) {
                FT_Library ft;
                if (FT_Init_FreeType(&ft)) {
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
        File         f(path);
        Error        openErr = f.open(File::ReadOnly);
        if (openErr.isError()) {
                promekiErr("Could not open font '%s': %s", path.cstr(), openErr.name().cstr());
                return false;
        }
        _fontData = f.readAll();
        f.close();
        if (!_fontData.isValid() || _fontData.size() == 0) {
                promekiErr("Font '%s' is empty", path.cstr());
                return false;
        }

        FT_Library ft = static_cast<FT_Library>(_ftLibrary);
        FT_Face    face;
        if (FT_New_Memory_Face(ft, static_cast<const FT_Byte *>(_fontData.data()),
                               static_cast<FT_Long>(_fontData.size()), 0, &face)) {
                promekiErr("Could not parse font '%s'", path.cstr());
                return false;
        }
        _ftFace = face;

        FT_Set_Pixel_Sizes(face, 0, _fontSize);

        // Cache font metrics (FreeType reports these in 26.6 fixed point)
        _ascender = face->size->metrics.ascender >> 6;
        _descender = -(face->size->metrics.descender >> 6);
        _lineHeight = _ascender + _descender;

        // Snap font geometry to the target format's chroma subsampling.
        // The multi-plane paint engine has a same-format scanline-memcpy
        // fast path that only fires when blit positions, sizes, and
        // strides are multiples of the deepest plane subsampling — for
        // NV12 that's (2, 2).  Rounding _ascender and _descender up to
        // _alignY here means cellTop = y - _ascender stays alignY-aligned
        // when the caller passes an aligned baseline y.  drawText() / the
        // glyph cache snap penX and cellWidth to _alignX so per-glyph
        // blits land on the fast path too.  For RGB and other non-
        // subsampled formats _alignX = _alignY = 1 and rounding is a
        // no-op.
        _alignX = 1;
        _alignY = 1;
        if (_paintEngine.pixelFormat().isValid()) {
                const PixelMemLayout &ml = _paintEngine.pixelFormat().memLayout();
                for (size_t p = 0; p < ml.planeCount(); p++) {
                        const auto &pd = ml.planeDesc(p);
                        const int   hs = static_cast<int>(pd.hSubsampling > 0 ? pd.hSubsampling : 1);
                        const int   vs = static_cast<int>(pd.vSubsampling > 0 ? pd.vSubsampling : 1);
                        if (hs > _alignX) _alignX = hs;
                        if (vs > _alignY) _alignY = vs;
                }
        }
        if (_alignY > 1) {
                _ascender = roundUpToAlign(_ascender, _alignY);
                _descender = roundUpToAlign(_descender, _alignY);
                _lineHeight = _ascender + _descender;
        }

        return true;
}

void FastFont::ensurePixels() {
        if (!_pixelsDirty) return;
        _fgPixel = _paintEngine.createPixel(_fg);
        _bgPixel = _paintEngine.createPixel(_bg);
        _pixelsDirty = false;
}

const FastFont::CachedGlyph *FastFont::getGlyph(uint32_t codepoint) {
        auto it = _glyphCache.find(codepoint);
        if (it != _glyphCache.end()) return &it->second;

        FT_Face face = static_cast<FT_Face>(_ftFace);
        if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER)) {
                promekiWarn("Could not load character 0x%X in '%s'", (unsigned int)codepoint,
                            effectiveFilename().cstr());
                return nullptr;
        }

        FT_Bitmap *bitmap = &face->glyph->bitmap;
        int        bitmapLeft = face->glyph->bitmap_left;
        int        bitmapTop = face->glyph->bitmap_top;
        int        advanceX = face->glyph->advance.x >> 6;

        // Round advance up to _alignX so accumulated penX stays aligned
        // through every glyph.  Cell width follows so the cached glyph
        // payload's chroma plane width is a whole number of chroma
        // samples (NV12 needs even cellWidth for the multi-plane blit
        // fast path).  For unsubsampled formats _alignX == 1 and this
        // is a no-op.
        advanceX = roundUpToAlign(advanceX, _alignX);
        int cellWidth = advanceX;
        if (cellWidth <= 0) cellWidth = _alignX;
        PixelFormat pd = _paintEngine.pixelFormat();
        ImageDesc   cellDesc(Size2Du32(cellWidth, _lineHeight), pd);
        auto        glyphPayload = UncompressedVideoPayload::allocate(cellDesc);
        if (!glyphPayload.isValid()) return nullptr;

        PaintEngine pe = glyphPayload->createPaintEngine();

        // Fill entire cell with background color
        pe.fill(_bgPixel);

        // Build point and alpha lists from the FreeType bitmap, positioned
        // within the cell at (bitmapLeft, ascender - bitmapTop).
        int originX = bitmapLeft;
        int originY = _ascender - bitmapTop;

        List<Point2Di32> points;
        List<float>      alphas;

        for (unsigned int row = 0; row < bitmap->rows; ++row) {
                int cellY = originY + row;
                if (cellY < 0 || cellY >= _lineHeight) continue;
                for (unsigned int col = 0; col < bitmap->width; ++col) {
                        int cellX = originX + col;
                        if (cellX < 0 || cellX >= cellWidth) continue;

                        uint8_t alpha = bitmap->buffer[row * bitmap->pitch + col];
                        if (alpha == 0) continue;

                        points += Point2Di32(cellX, cellY);
                        alphas += static_cast<float>(alpha) / 255.0f;
                }
        }

        pe.compositePoints(_fgPixel, points, alphas);

        // Cache the rendered glyph payload.
        CachedGlyph glyph;
        glyph.payload = glyphPayload;
        glyph.advanceX = advanceX;

        _glyphCache.insert(codepoint, glyph);
        return &_glyphCache[codepoint];
}

bool FastFont::drawText(const String &text, int x, int y) {
        if (!ensureFontLoaded()) return false;
        ensurePixels();

        // y is baseline; top of cell is at y - ascender.  Snap cellTop
        // down to _alignY so the per-glyph blits land on chroma row
        // boundaries — the multi-plane PaintEngine fast path requires
        // it.  _alignY == 1 collapses both calls to no-ops.
        int cellTop = roundDownToAlign(y - _ascender, _alignY);
        int penX = roundDownToAlign(x, _alignX);

        FT_Face face = _kerning ? static_cast<FT_Face>(_ftFace) : nullptr;
        bool    hasKerning = face != nullptr && FT_HAS_KERNING(face);
        FT_UInt prevIndex = 0;

        for (Char c : text) {
                FT_UInt glyphIndex = 0;
                if (hasKerning) {
                        glyphIndex = FT_Get_Char_Index(face, c.codepoint());
                        if (prevIndex != 0 && glyphIndex != 0) {
                                FT_Vector delta;
                                FT_Get_Kerning(face, prevIndex, glyphIndex, FT_KERNING_DEFAULT, &delta);
                                penX += delta.x >> 6;
                                // Re-snap after kerning so the next blit
                                // is still aligned.  Accumulated penX
                                // then drifts off-grid only inside this
                                // glyph step, not across it — keeping
                                // every actual blit on a fast-path row.
                                penX = roundDownToAlign(penX, _alignX);
                        }
                }

                const CachedGlyph *glyph = getGlyph(c.codepoint());
                if (glyph == nullptr) continue;

                if (glyph->payload.isValid()) {
                        _paintEngine.blit(Point2Di32(penX, cellTop), *glyph->payload);
                }
                penX += glyph->advanceX;

                if (hasKerning) prevIndex = glyphIndex;
        }

        return true;
}

int FastFont::measureText(const String &text) {
        if (!ensureFontLoaded()) return 0;
        ensurePixels();

        int width = 0;

        FT_Face face = _kerning ? static_cast<FT_Face>(_ftFace) : nullptr;
        bool    hasKerning = face != nullptr && FT_HAS_KERNING(face);
        FT_UInt prevIndex = 0;

        for (Char c : text) {
                FT_UInt glyphIndex = 0;
                if (hasKerning) {
                        glyphIndex = FT_Get_Char_Index(face, c.codepoint());
                        if (prevIndex != 0 && glyphIndex != 0) {
                                FT_Vector delta;
                                FT_Get_Kerning(face, prevIndex, glyphIndex, FT_KERNING_DEFAULT, &delta);
                                width += delta.x >> 6;
                                // Mirror drawText()'s snap-after-kerning
                                // so the reported width tracks the
                                // actual rendered advance — applyBurn
                                // uses measureText() to size the burn
                                // background rect and to centre each
                                // line, and divergence would push the
                                // background off the right edge of the
                                // text on subsampled formats.
                                width = roundDownToAlign(width, _alignX);
                        }
                }

                const CachedGlyph *glyph = getGlyph(c.codepoint());
                if (glyph == nullptr) continue;
                width += glyph->advanceX;

                if (hasKerning) prevIndex = glyphIndex;
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
