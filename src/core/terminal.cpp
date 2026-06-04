/**
 * @file      terminal.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <promeki/terminal.h>
#include <promeki/ansistream.h>
#include <promeki/file.h>
#include <promeki/env.h>
#include <promeki/logger.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#elif defined(PROMEKI_PLATFORM_WINDOWS)
#include <io.h>
#include <windows.h>
#endif

PROMEKI_NAMESPACE_BEGIN

Terminal::Terminal() : _inputFd(STDIN_FILENO), _outputFd(STDOUT_FILENO) {
        init();
}

Terminal::Terminal(int inputFd, int outputFd) : _inputFd(inputFd), _outputFd(outputFd) {
        init();
}

void Terminal::init() {
        // Build the single ordered write path: a File adopting the output fd,
        // with the AnsiStream layered over it.  ownsHandle is false so the
        // device flushes but never closes the borrowed _outputFd (e.g.
        // STDOUT_FILENO).  Write buffering is enabled so a full-screen redraw
        // — which emits an escape sequence a few bytes at a time per cell
        // through the AnsiStream — coalesces into a handful of write syscalls
        // rather than thousands.  Every Terminal mode change and the TUI
        // screen flush call AnsiStream::flush(), which drains the buffer, so
        // nothing lingers unwritten across a point where the loop would block.
        _outDevice = UniquePtr<File>::create(_outputFd, IODevice::WriteOnly, /*ownsHandle=*/false);
        _outDevice->setWriteBuffered(true);
        _ansi = UniquePtr<AnsiStream>::create(_outDevice.get());
#if defined(PROMEKI_PLATFORM_POSIX)
        _origState = new ::termios;
        std::memset(_origState, 0, sizeof(::termios));
#endif
}

AnsiStream &Terminal::ansiStream() {
        return *_ansi;
}

void Terminal::emergencyRestore() {
#if defined(PROMEKI_PLATFORM_POSIX)
        // Constant cleanup sequence written with a single raw ::write so the
        // whole thing is async-signal-safe (no allocation, no AnsiStream).
        static const char cleanup[] = "\033[?2026l"                                   // end synchronized update
                                      "\033[?1006l\033[?1003l\033[?1002l\033[?1000l" // mouse tracking off
                                      "\033[?2004l"                                   // bracketed paste off
                                      "\033[?1004l"                                   // focus reporting off
                                      "\033[?1049l"                                   // leave alternate screen
                                      "\033[?25h"                                     // show cursor
                                      "\033[0m";                                      // reset attributes
        ssize_t r = ::write(_outputFd, cleanup, sizeof(cleanup) - 1);
        (void)r;
        // Only restore termios if we actually captured it via enableRawMode;
        // otherwise _origState is zero-initialized and would corrupt the tty.
        if (_rawMode) tcsetattr(_inputFd, TCSAFLUSH, static_cast<::termios *>(_origState));
#endif
}

Terminal::~Terminal() {
        if (_mouseTracking) disableMouseTracking();
        if (_bracketedPaste) disableBracketedPaste();
        if (_focusReporting) disableFocusReporting();
        if (_alternateScreen) disableAlternateScreen();
        if (_rawMode) disableRawMode();
#if defined(PROMEKI_PLATFORM_POSIX)
        delete static_cast<::termios *>(_origState);
#endif
}

Error Terminal::enableRawMode() {
        if (_rawMode) return Error();
#if defined(PROMEKI_PLATFORM_POSIX)
        ::termios *orig = static_cast<::termios *>(_origState);
        if (tcgetattr(_inputFd, orig) == -1) {
                Error e = Error::syserr();
                promekiWarn("Terminal::enableRawMode tcgetattr(fd=%d) failed: %s (errno=%d)",
                            _inputFd, e.name().cstr(), e.systemError());
                return e;
        }
        ::termios raw = *orig;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(_inputFd, TCSAFLUSH, &raw) == -1) {
                Error e = Error::syserr();
                promekiWarn("Terminal::enableRawMode tcsetattr(fd=%d) failed: %s (errno=%d)",
                            _inputFd, e.name().cstr(), e.systemError());
                return e;
        }
        _rawMode = true;
        return Error();
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD  mode;
        if (!GetConsoleMode(hStdin, &mode)) {
                Error e = Error::syserr();
                promekiWarn("Terminal::enableRawMode GetConsoleMode(stdin) failed: %s", e.name().cstr());
                return e;
        }
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        if (!SetConsoleMode(hStdin, mode)) {
                Error e = Error::syserr();
                promekiWarn("Terminal::enableRawMode SetConsoleMode(stdin) failed: %s", e.name().cstr());
                return e;
        }
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  outMode;
        if (!GetConsoleMode(hStdout, &outMode)) {
                Error e = Error::syserr();
                promekiWarn("Terminal::enableRawMode GetConsoleMode(stdout) failed: %s", e.name().cstr());
                return e;
        }
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hStdout, outMode)) {
                Error e = Error::syserr();
                promekiWarn("Terminal::enableRawMode SetConsoleMode(stdout) failed: %s", e.name().cstr());
                return e;
        }
        _rawMode = true;
        return Error();
#else
        promekiWarnOnce("Terminal::enableRawMode refused: not supported on this platform");
        return Error(Error::NotSupported);
#endif
}

Error Terminal::disableRawMode() {
        if (!_rawMode) return Error();
#if defined(PROMEKI_PLATFORM_POSIX)
        if (tcsetattr(_inputFd, TCSAFLUSH, static_cast<::termios *>(_origState)) == -1) return Error::syserr();
        _rawMode = false;
        return Error();
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        _rawMode = false;
        return Error();
#else
        return Error(Error::NotSupported);
#endif
}

Result<int> Terminal::readInput(char *buf, int maxLen) {
#if defined(PROMEKI_PLATFORM_POSIX)
        ssize_t n = read(_inputFd, buf, maxLen);
        if (n == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return makeResult(0);
                return makeError<int>(Error::syserr());
        }
        return makeResult(static_cast<int>(n));
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD  available = 0;
        if (!PeekConsoleInput(hStdin, nullptr, 0, &available)) return makeError<int>(Error::syserr());
        if (available == 0) return makeResult(0);
        DWORD bytesRead = 0;
        if (!ReadFile(hStdin, buf, maxLen, &bytesRead, nullptr)) return makeError<int>(Error::syserr());
        return makeResult(static_cast<int>(bytesRead));
#else
        return makeError<int>(Error::NotSupported);
#endif
}

Error Terminal::windowSize(int &cols, int &rows) const {
#if defined(PROMEKI_PLATFORM_POSIX)
        struct winsize ws;
        if (ioctl(_outputFd, TIOCGWINSZ, &ws) != 0) return Error::syserr();
        cols = ws.ws_col;
        rows = ws.ws_row;
        return Error();
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) return Error::syserr();
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return Error();
#else
        return Error(Error::NotSupported);
#endif
}

Size2Di32 Terminal::size() const {
        int cols = 0, rows = 0;
        windowSize(cols, rows);
        return Size2Di32(cols, rows);
}

Error Terminal::enableMouseTracking() {
        if (_mouseTracking) return Error();
        _ansi->enableMouseTracking(AnsiStream::MouseDrag);
        _ansi->flush();
        _mouseTracking = true;
        return Error();
}

Error Terminal::disableMouseTracking() {
        if (!_mouseTracking) return Error();
        _ansi->disableMouseTracking();
        _ansi->flush();
        _mouseTracking = false;
        return Error();
}

Error Terminal::enableBracketedPaste() {
        if (_bracketedPaste) return Error();
        _ansi->enableBracketedPaste();
        _ansi->flush();
        _bracketedPaste = true;
        return Error();
}

Error Terminal::disableBracketedPaste() {
        if (!_bracketedPaste) return Error();
        _ansi->disableBracketedPaste();
        _ansi->flush();
        _bracketedPaste = false;
        return Error();
}

Error Terminal::enableFocusReporting() {
        if (_focusReporting) return Error();
        _ansi->enableFocusReporting();
        _ansi->flush();
        _focusReporting = true;
        return Error();
}

Error Terminal::disableFocusReporting() {
        if (!_focusReporting) return Error();
        _ansi->disableFocusReporting();
        _ansi->flush();
        _focusReporting = false;
        return Error();
}

Error Terminal::enableAlternateScreen() {
        if (_alternateScreen) return Error();
        _ansi->useAlternateScreenBuffer();
        _ansi->flush();
        _alternateScreen = true;
        return Error();
}

Error Terminal::disableAlternateScreen() {
        if (!_alternateScreen) return Error();
        _ansi->useMainScreenBuffer();
        _ansi->flush();
        _alternateScreen = false;
        return Error();
}

Terminal::ColorSupport Terminal::colorSupport() {
        static bool         detected = false;
        static ColorSupport level = NoColor;
        if (detected) return level;
        detected = true;

        // NO_COLOR convention: https://no-color.org/
        bool noColor = Env::isSet("NO_COLOR");

        // PROMEKI_COLOR override: allow forcing a specific level.
        String force = Env::get("PROMEKI_COLOR");
        if (!force.isEmpty()) {
                if (force == "truecolor" || force == "24bit") {
                        level = noColor ? GrayscaleTrue : TrueColor;
                        return level;
                }
                if (force == "256") {
                        level = noColor ? Grayscale256 : Color256;
                        return level;
                }
                if (force == "basic" || force == "ansi" || force == "16") {
                        level = noColor ? Grayscale16 : Basic;
                        return level;
                }
                if (force == "none") return level;
        }

        // COLORTERM is set to "truecolor" or "24bit" by many modern terminals.
        String colorTerm = Env::get("COLORTERM");
        String term = Env::get("TERM");

        if (colorTerm == "truecolor" || colorTerm == "24bit") {
                level = TrueColor;
        } else if (!term.isEmpty() && term.contains("256color")) {
                level = Color256;
        } else if (!term.isEmpty() && term.contains("truecolor")) {
                level = TrueColor;
        } else if (!term.isEmpty() && term == "dumb") {
                // "dumb" terminals have no color.
        } else if (!colorTerm.isEmpty()) {
                // If COLORTERM is set at all, at least basic color is supported.
                level = Basic;
        } else {
                // Check for TERM_PROGRAM known to support color.
                String termProg = Env::get("TERM_PROGRAM");
                if (!termProg.isEmpty()) {
                        if (termProg == "iTerm.app" || termProg == "Hyper") {
                                level = TrueColor;
                        } else if (termProg == "Apple_Terminal") {
                                level = Color256;
                        } else {
                                // Other known terminal programs generally support basic color.
                                level = Basic;
                        }
                } else if (!term.isEmpty()) {
                        // Known ANSI-capable TERM values that didn't match above patterns.
                        static const char *basicTerminals[] = {"xterm",        "vt100", "screen", "tmux",  "rxvt",
                                                               "rxvt-unicode", "ansi",  "linux",  "cygwin"};
                        for (const char *t : basicTerminals) {
                                if (term == t) {
                                        level = Basic;
                                        break;
                                }
                        }
                }
#if defined(PROMEKI_PLATFORM_WINDOWS)
                else {
                        // Windows 10+ with VT support provides truecolor.
                        level = TrueColor;
                }
#endif
        }

        // If NO_COLOR is set, map detected capability to grayscale equivalent.
        if (noColor) {
                switch (level) {
                        case Basic: level = Grayscale16; break;
                        case Color256: level = Grayscale256; break;
                        case TrueColor: level = GrayscaleTrue; break;
                        default: break;
                }
        }

        return level;
}

Result<int> Terminal::writeOutput(const char *data, int len) {
        // Route through the same raw-fd device the internal AnsiStream uses,
        // so raw byte writes and escape sequences share one ordered buffer.
        int64_t n = _outDevice->write(data, len);
        if (n < 0) return makeError<int>(Error::syserr());
        // writeOutput is the low-level "put these bytes on the terminal now"
        // call, so flush rather than leave them in the output buffer.  This
        // also drains any AnsiStream-buffered bytes ahead of them, preserving
        // call order across the two write paths.
        _outDevice->flush();
        return makeResult(static_cast<int>(n));
}

PROMEKI_NAMESPACE_END
