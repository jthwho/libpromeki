/**
 * @file      proav/fontpainter.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/proav/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Renders text into an image using a TrueType font.
 * @ingroup proav_paint
 *
 * Uses a PaintEngine to draw glyphs loaded from a TrueType font file.
 * Set the paint engine and font filename before calling drawText().
 */
class FontPainter {
        public:
                /** @brief Constructs a FontPainter with no paint engine or font set. */
                FontPainter();

                /** @brief Destroys the FontPainter and releases font resources. */
                ~FontPainter();

                /**
                 * @brief Sets the paint engine used for rendering.
                 * @param val The PaintEngine to use for drawing operations.
                 */
                void setPaintEngine(const PaintEngine &val) {
                        _paintEngine = val;
                        return;
                }

                /**
                 * @brief Sets the path to the TrueType font file.
                 * @param val The filename or path of the font to load.
                 */
                void setFontFilename(const String &val) {
                        _fontFilename = val;
                        return;
                }

                /**
                 * @brief Draws text at the specified position.
                 * @param text      The text string to render.
                 * @param x         The x coordinate of the text origin.
                 * @param y         The y coordinate of the text origin (baseline).
                 * @param pointSize The font size in points (default 12).
                 * @return True if the text was drawn successfully.
                 */
                bool drawText(const String &text, int x, int y, int pointSize = 12) const;

                /**
                 * @brief Measures the pixel width of a text string without drawing it.
                 * @param text      The text string to measure.
                 * @param pointSize The font size in points (default 12).
                 * @return The width in pixels, or 0 on error.
                 */
                int measureText(const String &text, int pointSize = 12) const;

        private:
                PaintEngine     _paintEngine;
                String          _fontFilename;
};

PROMEKI_NAMESPACE_END

