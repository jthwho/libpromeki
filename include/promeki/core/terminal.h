/**
 * @file      core/terminal.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/core/namespace.h>
#include <promeki/core/size2d.h>
#include <promeki/core/platform.h>
#include <promeki/core/error.h>
#include <promeki/core/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level terminal I/O abstraction.
 * @ingroup core_util
 *
 * Provides raw terminal mode, non-blocking input, window size queries,
 * mouse tracking, alternate screen buffer management, and color capability
 * detection.  Platform-specific implementations use termios on POSIX and
 * Console API on Windows.
 */
class Terminal {
        public:
                /** @brief Callback type for window resize notifications. */
                using ResizeCallback = std::function<void(int cols, int rows)>;

                /**
                 * @brief Describes the color capability level of the terminal.
                 */
                enum ColorSupport {
                        NoColor,        ///< No color support (e.g. dumb terminal, NO_COLOR set).
                        Basic,          ///< Basic 8/16 color support (standard ANSI).
                        Color256,       ///< 256 color support (xterm-256color and similar).
                        TrueColor       ///< 24-bit true color support.
                };

                Terminal();
                ~Terminal();

                Terminal(const Terminal &) = delete;
                Terminal &operator=(const Terminal &) = delete;

                /**
                 * @brief Enables raw terminal mode (disables line buffering, echo, etc).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error enableRawMode();

                /**
                 * @brief Restores the terminal to its original mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error disableRawMode();

                /** @brief Returns true if raw mode is currently enabled. */
                bool isRawMode() const { return _rawMode; }

                /**
                 * @brief Reads available input bytes without blocking.
                 * @param buf    Buffer to read into.
                 * @param maxLen Maximum bytes to read.
                 * @return A Result containing the number of bytes read (0 if
                 *         nothing available), or an error.
                 */
                Result<int> readInput(char *buf, int maxLen);

                /**
                 * @brief Queries the current terminal window size.
                 * @param cols Output: number of columns.
                 * @param rows Output: number of rows.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error windowSize(int &cols, int &rows) const;

                /**
                 * @brief Returns the terminal size as a Size2Du32 (width x height).
                 */
                Size2Di32 size() const;

                /**
                 * @brief Enables xterm SGR mouse tracking.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error enableMouseTracking();

                /**
                 * @brief Disables mouse tracking.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error disableMouseTracking();

                /** @brief Returns true if mouse tracking is enabled. */
                bool isMouseTrackingEnabled() const { return _mouseTracking; }

                /**
                 * @brief Enables bracketed paste mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error enableBracketedPaste();

                /**
                 * @brief Disables bracketed paste mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error disableBracketedPaste();

                /**
                 * @brief Switches to the alternate screen buffer.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error enableAlternateScreen();

                /**
                 * @brief Switches back to the main screen buffer.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error disableAlternateScreen();

                /**
                 * @brief Sets a callback for window resize events (SIGWINCH on POSIX).
                 * @param cb The callback to invoke on resize.
                 */
                void setResizeCallback(ResizeCallback cb);

                /**
                 * @brief Installs signal handlers for clean terminal restoration.
                 *
                 * Ensures raw mode and alternate screen are restored on SIGTERM,
                 * SIGINT, and other termination signals.
                 */
                void installSignalHandlers();

                /**
                 * @brief Detects the color support level of the terminal.
                 *
                 * Examines environment variables (NO_COLOR, COLORTERM, TERM, etc.)
                 * and platform capabilities to determine the level of color support.
                 * The result is cached after the first call.
                 *
                 * @return The detected ColorSupport level.
                 */
                static ColorSupport colorSupport();

                /**
                 * @brief Writes raw bytes to stdout.
                 * @param data The data to write.
                 * @param len  Number of bytes to write.
                 * @return A Result containing the number of bytes written,
                 *         or an error.
                 */
                Result<int> writeOutput(const char *data, int len);

        private:
                bool            _rawMode = false;
                bool            _mouseTracking = false;
                bool            _bracketedPaste = false;
                bool            _alternateScreen = false;
                ResizeCallback  _resizeCallback;

                // Opaque storage for platform-specific terminal state.
                // On POSIX this holds a struct termios.
                void            *_origState = nullptr;
};

PROMEKI_NAMESPACE_END
