/**
 * @file      proav/basicfont.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/proav/font.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Alpha-compositing font renderer.
 * @ingroup proav_paint
 *
 * BasicFont renders text with per-pixel alpha compositing, producing
 * correct anti-aliased text over any background.  Each glyph is
 * rasterized with FreeType and composited directly onto the target
 * surface using the foreground color and per-pixel alpha values from
 * the glyph bitmap.
 *
 * @par When to use
 * Use BasicFont for text overlaid on varying or complex backgrounds
 * where transparency is required, for one-off or infrequent text
 * rendering, or when memory usage is a concern.
 *
 * @par Tradeoffs
 * - Slower than FastFont for repeated rendering because every glyph
 *   is composited pixel-by-pixel on each drawText() call.
 * - No glyph caching, so memory usage is minimal.
 * - The background color property is not used; BasicFont composites
 *   the foreground color over whatever is already in the target image.
 *
 * @par Example
 * @code
 * Image img(1920, 1080, PixelFormat::RGBA8);
 * BasicFont bf(img.createPaintEngine());
 * bf.setFontFilename("/path/to/font.ttf");
 * bf.setFontSize(24);
 * bf.setForegroundColor(Color::White);
 * bf.drawText("Hello, World!", 100, 200);
 * @endcode
 *
 * @see Font, FastFont
 * @see @ref fonts "Font Rendering"
 */
class BasicFont : public Font {
        public:
                /**
                 * @brief Constructs a BasicFont with the given paint engine.
                 * @param pe The PaintEngine determining the target pixel format.
                 */
                BasicFont(const PaintEngine &pe);

                /** @brief Destroys the BasicFont and releases all resources. */
                ~BasicFont() override;

                bool drawText(const String &text, int x, int y) override;
                int measureText(const String &text) override;
                int lineHeight() override;
                int ascender() override;
                int descender() override;

        protected:
                void onStateChanged() override;

        private:
                bool ensureFontLoaded();
                void releaseFont();

                void    *_ftLibrary = nullptr;
                void    *_ftFace = nullptr;
                int     _ascender = 0;
                int     _descender = 0;
                int     _lineHeight = 0;
                bool    _fontDirty = true;
};

PROMEKI_NAMESPACE_END
