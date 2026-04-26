/**
 * @file      tui/painter.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/rect.h>
#include <promeki/color.h>
#include <promeki/string.h>
#include <promeki/tui/screen.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Painting context for TUI widgets.
 * @ingroup tui_core
 *
 * Draws to a TuiScreen within a clipped region corresponding to the
 * widget's visible area.  Coordinates are relative to the clip region
 * origin (the widget's top-left corner in screen coordinates).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance — including any combination of
 * draw calls — must be externally synchronized.  In typical TUI use a
 * @c TuiPainter is short-lived and used only on the TUI thread for the
 * duration of a single paint event.
 */
class TuiPainter {
        public:
                /**
                 * @brief Constructs a TuiPainter.
                 * @param screen The screen to draw to.
                 * @param clipRect The clipping rectangle in screen coordinates.
                 */
                TuiPainter(TuiScreen &screen, const Rect2Di32 &clipRect);

                /** @brief Draws a single character at the given position. */
                void drawChar(int x, int y, char32_t ch);

                /** @brief Draws horizontal text at the given position. */
                void drawText(int x, int y, const String &text);

                /**
                 * @brief Draws a rectangle outline using the given character.
                 *
                 * If ch is 0, uses Unicode box-drawing characters.
                 */
                void drawRect(const Rect2Di32 &rect, char32_t ch = 0);

                /** @brief Fills a rectangle with the given character. */
                void fillRect(const Rect2Di32 &rect, char32_t ch = U' ');

                /** @brief Draws a horizontal line. */
                void drawHLine(int x, int y, int len, char32_t ch = U'\u2500');

                /** @brief Draws a vertical line. */
                void drawVLine(int x, int y, int len, char32_t ch = U'\u2502');

                /** @brief Sets the current foreground color. */
                void setForeground(const Color &color) { _style.setForeground(color); }

                /** @brief Sets the current background color. */
                void setBackground(const Color &color) { _style.setBackground(color); }

                /** @brief Sets the current attribute flags. */
                void setAttrs(uint8_t attrs) { _style.setAttrs(attrs); }

                /** @brief Sets all visual properties from a TuiStyle. */
                void setStyle(const TuiStyle &style) { _style = style; }

                /** @brief Returns the current foreground color. */
                Color foreground() const { return _style.foreground(); }

                /** @brief Returns the current background color. */
                Color background() const { return _style.background(); }

                /** @brief Returns the current attribute flags. */
                uint8_t attrs() const { return _style.attrs(); }

                /** @brief Returns the current style. */
                const TuiStyle &style() const { return _style; }

                /** @brief Sets the clipping rectangle. */
                void setClipRect(const Rect2Di32 &rect) { _clipRect = rect; }

                /** @brief Returns the current clipping rectangle. */
                const Rect2Di32 &clipRect() const { return _clipRect; }

        private:
                TuiScreen &_screen;
                Rect2Di32  _clipRect;
                TuiStyle   _style = TuiStyle(Color::White, Color::Black);

                void putCell(int x, int y, char32_t ch);
};

PROMEKI_NAMESPACE_END
