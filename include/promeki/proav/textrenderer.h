/**
 * @file      proav/textrenderer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/map.h>
#include <promeki/core/color.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/image.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Fast cached text renderer that pre-renders glyphs in the native pixel format.
 * @ingroup proav_paint
 *
 * TextRenderer is optimized for repeated rendering of the same characters.
 * Each glyph is rasterized once with FreeType, then pre-rendered into the
 * target pixel format using the configured foreground and background colors.
 * The cached glyph covers the full character cell (advance width by line
 * height), so subsequent draws are a memcpy per scanline with no alpha
 * blending at render time.
 *
 * The renderer must be configured with a PaintEngine (which determines the
 * target pixel format and provides createPixel()), a TrueType font file,
 * a font size, and foreground/background colors. Any change to these
 * settings invalidates the glyph cache, which is lazily rebuilt on the
 * next draw or measure call.
 *
 * @par Example
 * @code
 * TextRenderer tr;
 * tr.setFontFilename("/path/to/font.ttf");
 * tr.setFontSize(48);
 * tr.setForegroundColor(Color::White);
 * tr.setBackgroundColor(Color::Black);
 * tr.setPaintEngine(image.createPaintEngine());
 * tr.drawText("01:02:03:04", 100, 200);
 * @endcode
 */
class TextRenderer {
        public:
                /** @brief Constructs a TextRenderer with default settings. */
                TextRenderer();

                /** @brief Destroys the TextRenderer and releases all resources. */
                ~TextRenderer();

                TextRenderer(const TextRenderer &) = delete;
                TextRenderer &operator=(const TextRenderer &) = delete;

                /**
                 * @brief Sets the path to the TrueType font file.
                 * @param val Path to the font file.
                 *
                 * Invalidates the entire glyph cache.
                 */
                void setFontFilename(const String &val);

                /**
                 * @brief Returns the current font filename.
                 * @return The font filename.
                 */
                const String &fontFilename() const { return _fontFilename; }

                /**
                 * @brief Sets the font size in pixels.
                 * @param val Font size in pixels.
                 *
                 * Invalidates the entire glyph cache.
                 */
                void setFontSize(int val);

                /**
                 * @brief Returns the current font size in pixels.
                 * @return The font size.
                 */
                int fontSize() const { return _fontSize; }

                /**
                 * @brief Returns the line height in pixels.
                 *
                 * This is the full height of the character cell (ascender +
                 * descender) as reported by the font metrics. Returns 0 if
                 * the font has not been loaded yet.
                 *
                 * @return Line height in pixels.
                 */
                int lineHeight() const { return _lineHeight; }

                /**
                 * @brief Returns the ascender in pixels.
                 *
                 * The ascender is the distance from the baseline to the top
                 * of the character cell. Returns 0 if the font has not been
                 * loaded yet.
                 *
                 * @return Ascender in pixels.
                 */
                int ascender() const { return _ascender; }

                /**
                 * @brief Sets the foreground (text) color.
                 * @param color The foreground Color.
                 *
                 * Invalidates the entire glyph cache.
                 */
                void setForegroundColor(const Color &color);

                /**
                 * @brief Sets the background color.
                 * @param color The background Color.
                 *
                 * Invalidates the entire glyph cache.
                 */
                void setBackgroundColor(const Color &color);

                /**
                 * @brief Sets the paint engine used for pixel format conversion.
                 * @param pe The PaintEngine whose createPixel() will be used to
                 *           convert color values into the native pixel format.
                 *
                 * Invalidates the entire glyph cache.
                 */
                void setPaintEngine(const PaintEngine &pe);

                /**
                 * @brief Draws text into the image bound to the current PaintEngine.
                 *
                 * Each character is blitted from the pre-rendered glyph cache
                 * into the destination via PaintEngine::blit().  No alpha
                 * blending is performed at render time.
                 *
                 * @param text The text string to render.
                 * @param x    The x coordinate of the text origin in pixels.
                 * @param y    The y coordinate of the text baseline in pixels.
                 * @return True if the text was drawn successfully.
                 */
                bool drawText(const String &text, int x, int y);

                /**
                 * @brief Measures the pixel width of a text string without drawing it.
                 * @param text The text string to measure.
                 * @return The width in pixels, or 0 on error.
                 */
                int measureText(const String &text);

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

                String                          _fontFilename;
                int                             _fontSize = 12;
                Color                           _fg = Color::White;
                Color                           _bg = Color::Black;
                PaintEngine                     _paintEngine;

                void                            *_ftLibrary = nullptr;
                void                            *_ftFace = nullptr;
                int                             _ascender = 0;
                int                             _descender = 0;
                int                             _lineHeight = 0;
                PaintEngine::Pixel              _fgPixel;
                PaintEngine::Pixel              _bgPixel;
                bool                            _pixelsDirty = true;
                Map<uint32_t, CachedGlyph>      _glyphCache;
};

PROMEKI_NAMESPACE_END
