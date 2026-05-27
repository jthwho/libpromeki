/**
 * @file      fastfont.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H
#include FT_SYNTHESIS_H

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

void FastFont::onColorChanged() {
        // Glyph cache is keyed on (codepoint, fg, bg, style), so the
        // default-colour switch needs no cache invalidation.  Glyphs
        // already rendered against any colour stay valid; the next
        // draw at the new default just adds new cache entries on the
        // fly.
}

void FastFont::invalidateGlyphs() {
        _glyphCache.clear();
}

void FastFont::invalidateFont() {
        invalidateGlyphs();
        // FT_Face borrows from the LoadedFace's Buffer, so destroy the
        // face first, then release the bytes.
        for (LoadedFace &lf : _ftFaces) {
                if (lf.face != nullptr) {
                        FT_Done_Face(static_cast<FT_Face>(lf.face));
                        lf.face = nullptr;
                }
                lf.data = Buffer();
        }
        _ftFaces.clear();
        _ascender = 0;
        _descender = 0;
        _lineHeight = 0;
        _underlineThickness = 1;
        _underlinePosition = 0;
}

void FastFont::invalidateAll() {
        invalidateFont();
        if (_ftLibrary != nullptr) {
                FT_Done_FreeType(static_cast<FT_Library>(_ftLibrary));
                _ftLibrary = nullptr;
        }
}

bool FastFont::loadFace(const String &path, LoadedFace &out) {
        File  f(path);
        Error openErr = f.open(File::ReadOnly);
        if (openErr.isError()) {
                promekiErr("Could not open font '%s': %s", path.cstr(), openErr.name().cstr());
                return false;
        }
        Buffer data = f.readAll();
        f.close();
        if (!data.isValid() || data.size() == 0) {
                promekiErr("Font '%s' is empty", path.cstr());
                return false;
        }

        FT_Library ft = static_cast<FT_Library>(_ftLibrary);
        FT_Face    face;
        if (FT_New_Memory_Face(ft, static_cast<const FT_Byte *>(data.data()),
                               static_cast<FT_Long>(data.size()), 0, &face)) {
                promekiErr("Could not parse font '%s'", path.cstr());
                return false;
        }
        FT_Set_Pixel_Sizes(face, 0, _fontSize);

        out.face = face;
        out.data = data;
        out.path = path;
        return true;
}

bool FastFont::ensureFontLoaded() {
        if (!_ftFaces.isEmpty() && _ftFaces[0].face != nullptr) return true;

        if (_ftLibrary == nullptr) {
                FT_Library ft;
                if (FT_Init_FreeType(&ft)) {
                        promekiErr("Could not init FreeType library");
                        return false;
                }
                _ftLibrary = ft;
        }

        // Load the primary face via promeki::File so the same code
        // path serves filesystem fonts and ":/.PROMEKI/fonts/..."
        // resources.  When _fontFilename is empty the base Font class
        // hands us the bundled default through effectiveFilename().
        // Each LoadedFace's Buffer outlives its FT_Face, which borrows
        // the bytes and reads from them on demand.
        LoadedFace primary;
        if (!loadFace(effectiveFilename(), primary)) return false;
        _ftFaces.pushToBack(primary);

        // Load fallback faces — best-effort. A missing fallback is not
        // fatal; the glyph lookup walks whichever faces did load and
        // falls back to the primary's .notdef for codepoints no face
        // carries.
        const StringList fallbacks = effectiveFallbacks();
        for (const String &fbPath : fallbacks) {
                if (fbPath == effectiveFilename()) continue; // Already loaded as primary.
                LoadedFace fb;
                if (loadFace(fbPath, fb)) _ftFaces.pushToBack(fb);
        }

        FT_Face face = static_cast<FT_Face>(_ftFaces[0].face);

        // Cache font metrics from the primary face only (FreeType
        // reports these in 26.6 fixed point).  Fallback faces may
        // have slightly different ascender / descender values, but
        // mixing baselines per glyph would break monospace alignment;
        // every glyph is rendered into a cell whose top sits at
        // `baseline - _ascender` regardless of which face produced it.
        _ascender = face->size->metrics.ascender >> 6;
        _descender = -(face->size->metrics.descender >> 6);
        _lineHeight = _ascender + _descender;

        // Underline metrics — face exposes these in font units, scaled
        // here to the current pixel size.  underline_position is the
        // *top* of the underline relative to the baseline and is
        // typically negative (below the baseline in FreeType's
        // coordinate space).  We flip the sign so the on-screen
        // distance below the baseline is positive.
        const FT_Pos uPos = FT_MulFix(face->underline_position, face->size->metrics.y_scale);
        const FT_Pos uThick = FT_MulFix(face->underline_thickness, face->size->metrics.y_scale);
        _underlinePosition = -static_cast<int>(uPos >> 6);
        if (_underlinePosition < 1) _underlinePosition = static_cast<int>(_descender / 2);
        _underlineThickness = static_cast<int>(uThick >> 6);
        if (_underlineThickness < 1) _underlineThickness = 1;

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

uint32_t FastFont::colorKey(const Color &c) {
        if (!c.isValid()) return 0u; // Reserved "no colour" key.
        // 32-bit RGBA quantization — colours that match at 8-bit per
        // channel render identically, so collapsing them into the same
        // cache slot is correct.  Bit 31 is the validity sentinel so
        // valid colours never collide with the "no colour" (0) key.
        uint32_t r = static_cast<uint32_t>(c.r8());
        uint32_t g = static_cast<uint32_t>(c.g8());
        uint32_t b = static_cast<uint32_t>(c.b8());
        uint32_t a = static_cast<uint32_t>(c.a8());
        return 0x80000000u | (r << 0) | (g << 8) | (b << 16) | ((a & 0x7F) << 24);
}

FastFont::GlyphKey FastFont::makeKey(uint32_t codepoint, const DrawStyle &style, Color &outFg, Color &outBg) const {
        outFg = style.foreground.isValid() ? style.foreground : _fg;
        outBg = style.background.isValid() ? style.background : _bg;
        GlyphKey k;
        k.codepoint = codepoint;
        k.fgRGBA = colorKey(outFg);
        k.bgRGBA = colorKey(outBg);
        k.styleFlags = 0;
        if (style.bold) k.styleFlags |= 0x01;
        if (style.italic) k.styleFlags |= 0x02;
        // Underline is drawn outside the glyph cell, so it does NOT
        // affect cache identity.
        return k;
}

int FastFont::resolveFaceIndex(uint32_t codepoint) const {
        // Walk faces in order and return the first one whose cmap
        // carries the requested codepoint.  Index 0 is reserved as
        // the last-resort answer: when no face has the glyph the
        // primary renders its .notdef, which is at least a consistent
        // visual hint that *something* is unmapped.
        for (size_t i = 0; i < _ftFaces.size(); ++i) {
                FT_Face f = static_cast<FT_Face>(_ftFaces[i].face);
                if (f == nullptr) continue;
                if (FT_Get_Char_Index(f, codepoint) != 0) return static_cast<int>(i);
        }
        return 0;
}

const FastFont::CachedGlyph *FastFont::getGlyph(uint32_t codepoint, const GlyphKey &key, const Color &fg,
                                                const Color &bg) {
        auto it = _glyphCache.find(key);
        if (it != _glyphCache.end()) return &it->second;

        const int faceIndex = resolveFaceIndex(codepoint);
        FT_Face   face = static_cast<FT_Face>(_ftFaces[faceIndex].face);
        // Load the outline without immediate rendering so the
        // FT_GlyphSlot_Embolden / FT_GlyphSlot_Oblique synthesis hooks
        // can transform the glyph before rasterisation.
        if (FT_Load_Char(face, codepoint, FT_LOAD_DEFAULT)) {
                promekiWarn("Could not load character 0x%X in '%s'", (unsigned int)codepoint,
                            _ftFaces[faceIndex].path.cstr());
                return nullptr;
        }
        if ((key.styleFlags & 0x01) != 0) FT_GlyphSlot_Embolden(face->glyph);
        if ((key.styleFlags & 0x02) != 0) FT_GlyphSlot_Oblique(face->glyph);
        if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
                if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) {
                        promekiWarn("Could not render character 0x%X in '%s'", (unsigned int)codepoint,
                                    _ftFaces[faceIndex].path.cstr());
                        return nullptr;
                }
        }

        FT_Bitmap *bitmap = &face->glyph->bitmap;
        int        bitmapLeft = face->glyph->bitmap_left;
        int        bitmapTop = face->glyph->bitmap_top;
        int        advanceX = face->glyph->advance.x >> 6;

        // Italic shear can push bitmap pixels to the right of the
        // glyph's nominal advance.  Make the cached cell wide enough
        // to hold every pixel the bitmap actually carries; otherwise
        // the rightmost slant gets clipped at the cell edge.
        int cellWidth = advanceX;
        const int bitmapRight = bitmapLeft + static_cast<int>(bitmap->width);
        if (bitmapRight > cellWidth) cellWidth = bitmapRight;

        // Round advance + cell width up to _alignX so accumulated penX
        // stays aligned through every glyph.  Cell width follows so
        // the cached glyph payload's chroma plane width is a whole
        // number of chroma samples (NV12 needs even cellWidth for the
        // multi-plane blit fast path).  For unsubsampled formats
        // _alignX == 1 and this is a no-op.
        advanceX = roundUpToAlign(advanceX, _alignX);
        cellWidth = roundUpToAlign(cellWidth, _alignX);
        if (cellWidth <= 0) cellWidth = _alignX;

        PixelFormat pd = _paintEngine.pixelFormat();
        ImageDesc   cellDesc(Size2Du32(cellWidth, _lineHeight), pd);
        auto        glyphPayload = UncompressedVideoPayload::allocate(cellDesc);
        if (!glyphPayload.isValid()) return nullptr;

        PaintEngine pe = glyphPayload->createPaintEngine();

        // Build the per-glyph fg / bg pixels just for this cell.  This
        // is the slow path (one createPixel pair per *new* cache
        // entry), so a Color → Pixel cache is not warranted yet.
        PaintEngine::Pixel fgPixel = pe.createPixel(fg.isValid() ? fg : _fg);
        PaintEngine::Pixel bgPixel = pe.createPixel(bg.isValid() ? bg : _bg);

        pe.fill(bgPixel);

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

        pe.compositePoints(fgPixel, points, alphas);

        // Cache the rendered glyph payload.
        CachedGlyph glyph;
        glyph.payload = glyphPayload;
        glyph.advanceX = advanceX;
        glyph.faceIndex = faceIndex;

        _glyphCache.insert(key, glyph);
        return &_glyphCache[key];
}

bool FastFont::drawText(const String &text, int x, int y) {
        return drawText(text, x, y, DrawStyle());
}

int FastFont::measureText(const String &text) {
        return measureText(text, DrawStyle());
}

bool FastFont::drawText(const String &text, int x, int y, const DrawStyle &style) {
        if (!ensureFontLoaded()) return false;

        // y is baseline; top of cell is at y - ascender.  Snap cellTop
        // down to _alignY so the per-glyph blits land on chroma row
        // boundaries — the multi-plane PaintEngine fast path requires
        // it.  _alignY == 1 collapses both calls to no-ops.
        int cellTop = roundDownToAlign(y - _ascender, _alignY);
        int penX = roundDownToAlign(x, _alignX);
        int startX = penX;

        // Kerning only makes sense within a single FT_Face — FreeType's
        // kerning table indexes glyphs by face-local glyph index, so
        // crossing a face boundary breaks the prevIndex chain.
        int     prevFaceIndex = -1;
        FT_UInt prevIndex = 0;

        for (Char c : text) {
                Color    fg, bg;
                GlyphKey key = makeKey(c.codepoint(), style, fg, bg);

                const CachedGlyph *glyph = getGlyph(c.codepoint(), key, fg, bg);
                if (glyph == nullptr) continue;

                FT_Face face = static_cast<FT_Face>(_ftFaces[glyph->faceIndex].face);
                FT_UInt glyphIndex = 0;
                if (_kerning && FT_HAS_KERNING(face)) {
                        glyphIndex = FT_Get_Char_Index(face, c.codepoint());
                        if (glyph->faceIndex == prevFaceIndex && prevIndex != 0 && glyphIndex != 0) {
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

                if (glyph->payload.isValid()) {
                        _paintEngine.blit(Point2Di32(penX, cellTop), *glyph->payload);
                }
                penX += glyph->advanceX;

                prevFaceIndex = glyph->faceIndex;
                prevIndex = glyphIndex;
        }

        if (style.underline && _underlineThickness > 0 && penX > startX) {
                // Draw the underline rectangle below the baseline,
                // spanning the full text width.  Y position uses the
                // pre-snapped cellTop so the underline lands on the
                // same chroma grid as the glyph cells.
                const Color &fg = style.foreground.isValid() ? style.foreground : _fg;
                PaintEngine::Pixel fgPixel = _paintEngine.createPixel(fg);
                int       uy = cellTop + _ascender + _underlinePosition;
                int       uHeight = roundUpToAlign(_underlineThickness, _alignY);
                if (uHeight < _alignY) uHeight = _alignY;
                _paintEngine.fillRect(fgPixel, Rect<int32_t>(startX, uy, penX - startX, uHeight));
        }

        return true;
}

int FastFont::measureText(const String &text, const DrawStyle &style) {
        if (!ensureFontLoaded()) return 0;

        int width = 0;

        // Mirror drawText's per-glyph face routing so cross-face
        // transitions reset the kerning chain.
        int     prevFaceIndex = -1;
        FT_UInt prevIndex = 0;

        for (Char c : text) {
                Color    fg, bg;
                GlyphKey key = makeKey(c.codepoint(), style, fg, bg);

                const CachedGlyph *glyph = getGlyph(c.codepoint(), key, fg, bg);
                if (glyph == nullptr) continue;

                FT_Face face = static_cast<FT_Face>(_ftFaces[glyph->faceIndex].face);
                FT_UInt glyphIndex = 0;
                if (_kerning && FT_HAS_KERNING(face)) {
                        glyphIndex = FT_Get_Char_Index(face, c.codepoint());
                        if (glyph->faceIndex == prevFaceIndex && prevIndex != 0 && glyphIndex != 0) {
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

                width += glyph->advanceX;

                prevFaceIndex = glyph->faceIndex;
                prevIndex = glyphIndex;
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
