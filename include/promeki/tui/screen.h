/**
 * @file      screen.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/char.h>
#include <promeki/tui/style.h>
#include <promeki/ansistream.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A single cell in the TUI screen buffer.
 *
 * Each cell holds a character and a TuiStyle describing its visual
 * properties (foreground, background, text attributes).
 */
struct TuiCell {
        Char            ch = Char(U' ');
        TuiStyle        style = TuiStyle(Color::White, Color::Black);

        bool operator==(const TuiCell &other) const {
                return ch == other.ch && style == other.style;
        }
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
 * NOT an ObjectBase -- this is infrastructure.
 */
class TuiScreen {
        public:
                TuiScreen();
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

        private:
                int                     _cols = 0;
                int                     _rows = 0;
                List<TuiCell>           _front;
                List<TuiCell>           _back;
                bool                    _fullRedraw = true;

                int index(int x, int y) const { return y * _cols + x; }
                bool inBounds(int x, int y) const {
                        return x >= 0 && x < _cols && y >= 0 && y < _rows;
                }

                void emitCell(AnsiStream &stream, const TuiCell &cell,
                              int &cursorX, int &cursorY, int x, int y,
                              Color &lastFg, Color &lastBg, uint8_t &lastStyle);
};

PROMEKI_NAMESPACE_END
