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
#include <promeki/error.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level terminal I/O abstraction.
 * @ingroup util
 *
 * Provides raw terminal mode, non-blocking input, window size queries,
 * mouse tracking, alternate screen buffer management, and color capability
 * detection.  Platform-specific implementations use termios on POSIX and
 * Console API on Windows.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  The terminal is a process-wide
 * resource; mutating its mode (raw / canonical) or screen buffer
 * from multiple threads concurrently produces undefined behavior.
 * Read-only queries (@c size, @c colorCapability) are safe.
 * Typical usage confines all Terminal calls to a single
 * input/render thread.
 */
class Terminal {
        public:
                /** @brief Callback type for window resize notifications. */
                using ResizeCallback = std::function<void(int cols, int rows)>;

                /**
                 * @brief Describes the color capability level of the terminal.
                 *
                 * The TUI rendering pipeline always works with full RGB Color
                 * values internally.  During TuiScreen::flush(), each cell's
                 * foreground and background are converted to the nearest
                 * representable color for the active ColorSupport level.
                 * This means the UI degrades gracefully to any color mode,
                 * but for the best visual results, applications should
                 * provide a TuiPalette whose colors are chosen with the
                 * target color mode in mind.
                 *
                 * When the @c NO_COLOR environment variable is set, the
                 * detected color capability is mapped to its grayscale
                 * equivalent (e.g. TrueColor becomes GrayscaleTrue).
                 * Grayscale modes convert each color to its perceptual
                 * luminance (Rec. 709) before emitting it.
                 *
                 * @see TuiScreen::setColorMode()
                 * @see TuiPalette
                 */
                enum ColorSupport {
                        NoColor,        ///< No color support (e.g. dumb terminal).
                        Grayscale16,    ///< Grayscale via the 4 gray entries in the 16-color palette (NO_COLOR + Basic).
                        Grayscale256,   ///< Grayscale via the 24-entry grayscale ramp in the 256-color palette (NO_COLOR + 256).
                        GrayscaleTrue,  ///< Grayscale via 24-bit RGB with equal R=G=B (NO_COLOR + TrueColor).
                        Basic,          ///< Basic 8/16 color support (standard ANSI).
                        Color256,       ///< 256 color support (xterm-256color and similar).
                        TrueColor       ///< 24-bit true color support.
                };

                /** @brief Constructs a Terminal using stdin/stdout. */
                Terminal();

                /**
                 * @brief Constructs a Terminal with explicit I/O file descriptors.
                 * @param inputFd  File descriptor for input (e.g. STDIN_FILENO or a PTY).
                 * @param outputFd File descriptor for output (e.g. STDOUT_FILENO or a PTY).
                 */
                Terminal(int inputFd, int outputFd);

                /** @brief Destructor. Restores terminal state and cleans up. */
                ~Terminal();

                Terminal(const Terminal &) = delete;
                Terminal &operator=(const Terminal &) = delete;

                /** @brief Returns the input file descriptor. */
                int inputFd() const { return _inputFd; }

                /** @brief Returns the output file descriptor. */
                int outputFd() const { return _outputFd; }

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
                 * Examines environment variables to determine the level of
                 * color support.  The @c PROMEKI_COLOR variable, if set,
                 * overrides auto-detection (accepted values: "truecolor",
                 * "24bit", "256", "basic", "ansi", "16", "none").
                 * Otherwise, @c COLORTERM, @c TERM, and @c TERM_PROGRAM
                 * are examined.
                 *
                 * When @c NO_COLOR is set (see https://no-color.org/),
                 * the detected capability is mapped to its grayscale
                 * equivalent.  The result is cached after the first call.
                 *
                 * @return The detected ColorSupport level.
                 */
                static ColorSupport colorSupport();

                /**
                 * @brief Writes raw bytes to the output file descriptor.
                 * @param data The data to write.
                 * @param len  Number of bytes to write.
                 * @return A Result containing the number of bytes written,
                 *         or an error.
                 */
                Result<int> writeOutput(const char *data, int len);

        private:
                void init();

                int             _inputFd;
                int             _outputFd;
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
