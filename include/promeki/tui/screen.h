/**
 * @file      tui/screen.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/char.h>
#include <promeki/terminal.h>
#include <promeki/tui/style.h>
#include <promeki/ansistream.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A single cell in the TUI screen buffer.
 * @ingroup tui_core
 *
 * Each cell holds a character and a TuiStyle describing its visual
 * properties (foreground, background, text attributes).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * @c TuiCell is a small value type; copying is the recommended way to
 * share it across threads.
 */
struct TuiCell {
        Char            ch = Char(U' ');        ///< The Unicode character in this cell.
        TuiStyle        style = TuiStyle(Color::White, Color::Black); ///< Visual style (colors and attributes).

        /** @brief Returns true if both character and style match. */
        bool operator==(const TuiCell &other) const {
                return ch == other.ch && style == other.style;
        }
        /** @brief Returns true if character or style differ. */
        bool operator!=(const TuiCell &other) const {
                return !(*this == other);
        }
};

/**
 * @brief Double-buffered character cell grid for TUI rendering.
 *
 * Maintains front and back buffers.  Widgets draw to the back buffer
 * via setCell().  flush() diffs front vs back and emits minimal ANSI
 * escape sequences for changed cells, then swaps buffers.
 *
 * Colors are stored as full RGB values in each TuiCell.  During
 * flush(), the active colorMode() determines how those colors are
 * emitted: TrueColor uses 24-bit RGB sequences, Color256 maps to the
 * closest ANSI 256-color palette entry, Basic restricts to 16 system
 * colors, and the Grayscale variants convert colors to perceptual
 * luminance before emitting.  NoColor suppresses all color output.
 * The TUI will do its best to gracefully degrade color, but for
 * optimal results, use a TuiPalette whose colors are tuned for the
 * active color mode.
 *
 * NOT an ObjectBase -- this is infrastructure.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance — including any combination of
 * @c setCell() / @c flush() / @c resize() — must be externally synchronized.
 * In typical use a screen is owned and driven by a single TUI thread.
 */
class TuiScreen {
        public:
                /** @brief Constructs an empty screen with no buffers. */
                TuiScreen();

                /** @brief Destructor. */
                ~TuiScreen();

                /**
                 * @brief Resizes both buffers to the given dimensions.
                 * @param cols Number of columns.
                 * @param rows Number of rows.
                 */
                void resize(int cols, int rows);

                /** @brief Returns the number of columns. */
                int cols() const { return _cols; }

                /** @brief Returns the number of rows. */
                int rows() const { return _rows; }

                /**
                 * @brief Sets a cell in the back buffer.
                 * @param x Column (0-based).
                 * @param y Row (0-based).
                 * @param cell The cell data to write.
                 */
                void setCell(int x, int y, const TuiCell &cell);

                /**
                 * @brief Returns the cell at the given position in the back buffer.
                 * @param x Column (0-based).
                 * @param y Row (0-based).
                 * @return The cell data, or a default cell if out of bounds.
                 */
                TuiCell cell(int x, int y) const;

                /**
                 * @brief Clears the back buffer with blank cells.
                 * @param fg Default foreground color.
                 * @param bg Default background color.
                 */
                void clear(const Color &fg = Color::White, const Color &bg = Color::Black);

                /**
                 * @brief Diffs front vs back buffer and emits minimal ANSI updates.
                 * @param stream The AnsiStream to write to.
                 */
                void flush(AnsiStream &stream);

                /**
                 * @brief Forces a full redraw on the next flush.
                 */
                void invalidate();

                /**
                 * @brief Sets the color mode used when emitting colors during flush.
                 *
                 * All RGB colors in the cell buffer are converted to the nearest
                 * representable value for this mode.  For best results, pair
                 * with a TuiPalette designed for the chosen mode.
                 *
                 * @param mode The color support level to use.
                 * @see Terminal::colorSupport()
                 */
                void setColorMode(Terminal::ColorSupport mode) { _colorMode = mode; }

                /**
                 * @brief Returns the current color mode.
                 * @return The color support level in use.
                 */
                Terminal::ColorSupport colorMode() const { return _colorMode; }

        private:
                int                     _cols = 0;
                int                     _rows = 0;
                List<TuiCell>           _front;
                List<TuiCell>           _back;
                bool                    _fullRedraw = true;
                Terminal::ColorSupport  _colorMode = Terminal::TrueColor;

                int index(int x, int y) const { return y * _cols + x; }
                bool inBounds(int x, int y) const {
                        return x >= 0 && x < _cols && y >= 0 && y < _rows;
                }

                void emitCell(AnsiStream &stream, const TuiCell &cell,
                              int &cursorX, int &cursorY, int x, int y,
                              Color &lastFg, Color &lastBg, uint8_t &lastStyle);
};

PROMEKI_NAMESPACE_END
