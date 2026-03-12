/**
 * @file      terminal.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/size2d.h>
#include <promeki/platform.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level terminal I/O abstraction.
 *
 * Provides raw terminal mode, non-blocking input, window size queries,
 * mouse tracking, and alternate screen buffer management.  Platform-specific
 * implementations use termios on POSIX and Console API on Windows.
 */
class Terminal {
        public:
                /** @brief Callback type for window resize notifications. */
                using ResizeCallback = std::function<void(int cols, int rows)>;

                Terminal();
                ~Terminal();

                Terminal(const Terminal &) = delete;
                Terminal &operator=(const Terminal &) = delete;

                /**
                 * @brief Enables raw terminal mode (disables line buffering, echo, etc).
                 * @return True on success.
                 */
                bool enableRawMode();

                /**
                 * @brief Restores the terminal to its original mode.
                 * @return True on success.
                 */
                bool disableRawMode();

                /** @brief Returns true if raw mode is currently enabled. */
                bool isRawMode() const { return _rawMode; }

                /**
                 * @brief Reads available input bytes without blocking.
                 * @param buf    Buffer to read into.
                 * @param maxLen Maximum bytes to read.
                 * @return Number of bytes read, or 0 if nothing available, or -1 on error.
                 */
                int readInput(char *buf, int maxLen);

                /**
                 * @brief Queries the current terminal window size.
                 * @param cols Output: number of columns.
                 * @param rows Output: number of rows.
                 * @return True on success.
                 */
                bool windowSize(int &cols, int &rows) const;

                /**
                 * @brief Returns the terminal size as a Size2Du32 (width x height).
                 */
                Size2Di32 size() const;

                /**
                 * @brief Enables xterm SGR mouse tracking.
                 * @return True on success.
                 */
                bool enableMouseTracking();

                /**
                 * @brief Disables mouse tracking.
                 * @return True on success.
                 */
                bool disableMouseTracking();

                /** @brief Returns true if mouse tracking is enabled. */
                bool isMouseTrackingEnabled() const { return _mouseTracking; }

                /**
                 * @brief Enables bracketed paste mode.
                 * @return True on success.
                 */
                bool enableBracketedPaste();

                /**
                 * @brief Disables bracketed paste mode.
                 * @return True on success.
                 */
                bool disableBracketedPaste();

                /**
                 * @brief Switches to the alternate screen buffer.
                 * @return True on success.
                 */
                bool enableAlternateScreen();

                /**
                 * @brief Switches back to the main screen buffer.
                 * @return True on success.
                 */
                bool disableAlternateScreen();

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
                 * @brief Writes raw bytes to stdout.
                 * @param data The data to write.
                 * @param len  Number of bytes to write.
                 * @return Number of bytes written.
                 */
                int writeOutput(const char *data, int len);

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
