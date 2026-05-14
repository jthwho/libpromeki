/**
 * @file      fastfont.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/font.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/buffer.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief High-performance cached font renderer.
 * @ingroup paint
 *
 * FastFont is optimized for repeated rendering of the same characters
 * at the same size.  Each glyph is rasterized once with FreeType,
 * then pre-rendered into a small Image in the target pixel format
 * using the configured foreground and background colors and style
 * flags.  The cached glyph covers the full character cell (advance
 * width by line height), so subsequent draws are a memcpy per
 * scanline with no alpha blending at render time.
 *
 * @par Multi-keyed glyph cache
 * The cache is keyed by `(codepoint, foreground colour, background
 * colour, style flags)` — switching colour or toggling
 * bold/italic/underline mid-rendering grows the cache instead of
 * dropping it.  Subtitle renderers, multi-coloured HUDs, and any
 * caller that paints heterogeneous spans hit the cache fast-path on
 * every glyph after the first occurrence.  The cache *is* dropped
 * when the font file, font size, or paint engine pixel format
 * changes — those alter the glyph geometry the cells depend on.
 *
 * @par When to use
 * Use FastFont for high-frame-rate overlays such as timecodes, HUD
 * text, subtitle burn-in, or any scenario where the same characters
 * are drawn repeatedly across many frames.  It is the fastest
 * rendering path available.
 *
 * @par Tradeoffs
 * - Glyph cells are opaque: the background color fills the entire
 *   cell area, so FastFont is not suitable for transparent text
 *   overlays where the underlying image must show through.
 * - Higher memory usage due to the per-glyph image cache.
 * - Changes to the font filename, size, or paint-engine pixel
 *   format invalidate the entire cache.  Colour and style switches
 *   do not.
 *
 * @par Example
 * @code
 * Image img(1920, 1080, PixelFormat::RGB8_sRGB);
 * FastFont ff(img.createPaintEngine());
 * ff.setFontFilename("/path/to/font.ttf");
 * ff.setFontSize(48);
 * ff.setForegroundColor(Color::White);
 * ff.setBackgroundColor(Color::Black);
 * ff.drawText("01:02:03:04", 100, 200);
 *
 * FastFont::DrawStyle red{Color::Red, Color::Black, false, true, false};
 * ff.drawText(" italic red", 300, 200, red);
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref Font.  The internal
 * glyph cache is not synchronized.
 *
 * @see Font, BasicFont
 * @see @ref fonts "Font Rendering"
 */
class FastFont : public Font {
        public:
                /** @brief Unique-ownership pointer to a FastFont. */
                using UPtr = UniquePtr<FastFont>;

                /**
                 * @brief Per-call style override for @ref drawText /
                 *        @ref measureText.
                 *
                 * Callers wanting to render a span in a different
                 * colour or weight than the font's defaults pass a
                 * @c DrawStyle without changing the font's stored
                 * configuration.  An invalid @ref foreground /
                 * @ref background falls back to the font's stored
                 * @ref Font::foregroundColor / @ref Font::backgroundColor.
                 */
                struct DrawStyle {
                                Color foreground; ///< Invalid = inherit from base font.
                                Color background; ///< Invalid = inherit from base font.
                                bool  bold = false;
                                bool  italic = false;
                                bool  underline = false;
                };

                /**
                 * @brief Constructs a FastFont with the given paint engine.
                 * @param pe The PaintEngine determining the target pixel format.
                 */
                FastFont(const PaintEngine &pe);

                /** @brief Destroys the FastFont and releases all resources. */
                ~FastFont() override;

                bool drawText(const String &text, int x, int y) override;
                int  measureText(const String &text) override;
                int  lineHeight() override;
                int  ascender() override;
                int  descender() override;

                /**
                 * @brief Draws text using a per-call style override.
                 *
                 * Identical to the base @ref drawText except that the
                 * supplied @p style overrides the font's foreground /
                 * background colour and toggles bold / italic /
                 * underline for this call only.  Each unique
                 * (codepoint, foreground, background, style) tuple
                 * occupies one glyph-cache entry.
                 */
                bool drawText(const String &text, int x, int y, const DrawStyle &style);

                /**
                 * @brief Measures the pixel width using the supplied
                 *        @p style.
                 *
                 * Bold and italic shift glyph advances; this overload
                 * routes through the same glyph-cache lookup that
                 * @ref drawText uses so the measured width matches
                 * what would actually be drawn.
                 */
                int measureText(const String &text, const DrawStyle &style);

        protected:
                void onStateChanged() override;
                /// @brief No-op: the glyph cache is keyed on colour, so
                ///        a default-colour switch needs no invalidation.
                void onColorChanged() override;

        private:
                struct GlyphKey {
                                uint32_t codepoint = 0;
                                uint32_t fgRGBA = 0;
                                uint32_t bgRGBA = 0;
                                uint8_t  styleFlags = 0; ///< bit0=bold, bit1=italic
                                bool     operator<(const GlyphKey &o) const {
                                        if (codepoint != o.codepoint) return codepoint < o.codepoint;
                                        if (fgRGBA != o.fgRGBA) return fgRGBA < o.fgRGBA;
                                        if (bgRGBA != o.bgRGBA) return bgRGBA < o.bgRGBA;
                                        return styleFlags < o.styleFlags;
                                }
                                bool operator==(const GlyphKey &o) const {
                                        return codepoint == o.codepoint && fgRGBA == o.fgRGBA
                                                && bgRGBA == o.bgRGBA && styleFlags == o.styleFlags;
                                }
                };

                struct CachedGlyph {
                                UncompressedVideoPayload::Ptr payload; ///< Pre-rendered glyph payload.
                                int advanceX = 0;                      ///< Horizontal advance to next glyph in pixels.
                                /// @brief Index into @c _ftFaces of the face that produced
                                ///        this glyph.  Used by drawText / measureText to
                                ///        route kerning lookups (kerning only makes sense
                                ///        within a single face).
                                int faceIndex = 0;
                };

                /// @brief A single TrueType face plus the bytes that
                ///        FT_Face borrows from.  The buffer must live
                ///        as long as the face does.
                struct LoadedFace {
                                void  *face = nullptr;
                                Buffer data;
                                String path;
                };

                bool               ensureFontLoaded();
                bool               loadFace(const String &path, LoadedFace &out);
                const CachedGlyph *getGlyph(uint32_t codepoint, const GlyphKey &key, const Color &fg, const Color &bg);
                void               invalidateGlyphs();
                void               invalidateFont();
                void               invalidateAll();

                /// @brief Builds a cache key from @p style.  Pulls the
                ///        base font's @c _fg / @c _bg in when the style
                ///        carries no explicit colour.
                GlyphKey makeKey(uint32_t codepoint, const DrawStyle &style, Color &outFg, Color &outBg) const;

                /// @brief Packs an sRGB Color into a 32-bit cache key.
                static uint32_t colorKey(const Color &c);

                /// @brief Finds the face index whose cmap carries
                ///        @p codepoint, walking @c _ftFaces in order.
                ///        Returns 0 (primary face) when no face has
                ///        the glyph — that path then renders the
                ///        primary's @c .notdef as the last resort.
                int resolveFaceIndex(uint32_t codepoint) const;

                void                      *_ftLibrary = nullptr;
                List<LoadedFace>           _ftFaces; ///< [0] = primary, [1..] = fallbacks.
                int                        _ascender = 0;
                int                        _descender = 0;
                int                        _lineHeight = 0;
                /// Underline thickness in pixels (1 minimum).
                int _underlineThickness = 1;
                /// Underline Y offset from baseline (positive = below).
                int _underlinePosition = 0;
                /// Horizontal alignment derived from the deepest chroma
                /// subsampling of the target pixel format.  Glyph cell
                /// widths, advance, and blit X positions are snapped to
                /// multiples of this so per-glyph blits on chroma-
                /// subsampled formats (NV12, YV12, ...) hit the
                /// PaintEngine_MultiPlane same-format fast path instead
                /// of falling back to the scalar smear-on-edge path.
                int                        _alignX = 1;
                /// Vertical counterpart to _alignX.
                int                        _alignY = 1;
                Map<GlyphKey, CachedGlyph> _glyphCache;
};

PROMEKI_NAMESPACE_END
