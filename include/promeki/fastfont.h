/**
 * @file      fastfont.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/font.h>
#include <promeki/map.h>
#include <promeki/image.h>
#include <promeki/buffer.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief High-performance cached font renderer.
 * @ingroup paint
 *
 * FastFont is optimized for repeated rendering of the same characters
 * at the same size and color.  Each glyph is rasterized once with
 * FreeType, then pre-rendered into a small Image in the target pixel
 * format using the configured foreground and background colors.  The
 * cached glyph covers the full character cell (advance width by line
 * height), so subsequent draws are a memcpy per scanline with no
 * alpha blending at render time.
 *
 * @par When to use
 * Use FastFont for high-frame-rate overlays such as timecodes, HUD
 * text, or any scenario where the same characters are drawn repeatedly
 * across many frames.  It is the fastest rendering path available.
 *
 * @par Tradeoffs
 * - Glyph cells are opaque: the background color fills the entire
 *   cell area, so FastFont is not suitable for transparent text
 *   overlays where the underlying image must show through.
 * - Higher memory usage due to the per-glyph image cache.
 * - Any change to font properties (filename, size, colors, paint
 *   engine pixel format) invalidates the entire cache.
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
 * @endcode
 *
 * @see Font, BasicFont
 * @see @ref fonts "Font Rendering"
 */
class FastFont : public Font {
        public:
                /** @brief Unique-ownership pointer to a FastFont. */
                using UPtr = UniquePtr<FastFont>;

                /**
                 * @brief Constructs a FastFont with the given paint engine.
                 * @param pe The PaintEngine determining the target pixel format.
                 */
                FastFont(const PaintEngine &pe);

                /** @brief Destroys the FastFont and releases all resources. */
                ~FastFont() override;

                bool drawText(const String &text, int x, int y) override;
                int measureText(const String &text) override;
                int lineHeight() override;
                int ascender() override;
                int descender() override;

        protected:
                void onStateChanged() override;

        private:
                struct CachedGlyph {
                        Image   image;          ///< Pre-rendered glyph image in the target pixel format.
                        int     advanceX = 0;   ///< Horizontal advance to next glyph in pixels.
                };

                bool ensureFontLoaded();
                void ensurePixels();
                const CachedGlyph *getGlyph(uint32_t codepoint);
                void invalidateGlyphs();
                void invalidateFont();
                void invalidateAll();

                void                            *_ftLibrary = nullptr;
                void                            *_ftFace = nullptr;
                Buffer                          _fontData;          ///< Owns the font bytes for the lifetime of _ftFace.
                int                             _ascender = 0;
                int                             _descender = 0;
                int                             _lineHeight = 0;
                PaintEngine::Pixel              _fgPixel;
                PaintEngine::Pixel              _bgPixel;
                bool                            _pixelsDirty = true;
                Map<uint32_t, CachedGlyph>      _glyphCache;
};

PROMEKI_NAMESPACE_END
