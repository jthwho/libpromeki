/**
 * @file      terminal.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <promeki/terminal.h>
#include <promeki/env.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>
#elif defined(PROMEKI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

PROMEKI_NAMESPACE_BEGIN

#if defined(PROMEKI_PLATFORM_POSIX)

// Global pointer for signal handler cleanup
static Terminal *g_activeTerminal = nullptr;

static void signalHandler(int sig) {
        if (g_activeTerminal) {
                g_activeTerminal->disableMouseTracking();
                g_activeTerminal->disableAlternateScreen();
                g_activeTerminal->disableRawMode();
        }
        // Re-raise to get default behavior
        signal(sig, SIG_DFL);
        raise(sig);
}

static void sigwinchHandler(int) {
        // The resize callback is checked during the next input poll
}

#endif

Terminal::Terminal() : _inputFd(STDIN_FILENO), _outputFd(STDOUT_FILENO) {
        init();
}

Terminal::Terminal(int inputFd, int outputFd) : _inputFd(inputFd), _outputFd(outputFd) {
        init();
}

void Terminal::init() {
#if defined(PROMEKI_PLATFORM_POSIX)
        _origState = new ::termios;
        std::memset(_origState, 0, sizeof(::termios));
#endif
}

Terminal::~Terminal() {
        if (_mouseTracking) disableMouseTracking();
        if (_bracketedPaste) disableBracketedPaste();
        if (_alternateScreen) disableAlternateScreen();
        if (_rawMode) disableRawMode();
#if defined(PROMEKI_PLATFORM_POSIX)
        if (g_activeTerminal == this) g_activeTerminal = nullptr;
        delete static_cast<::termios *>(_origState);
#endif
}

Error Terminal::enableRawMode() {
        if (_rawMode) return Error();
#if defined(PROMEKI_PLATFORM_POSIX)
        ::termios *orig = static_cast<::termios *>(_origState);
        if (tcgetattr(_inputFd, orig) == -1) return Error::syserr();
        ::termios raw = *orig;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(_inputFd, TCSAFLUSH, &raw) == -1) return Error::syserr();
        _rawMode = true;
        return Error();
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD  mode;
        if (!GetConsoleMode(hStdin, &mode)) return Error::syserr();
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        if (!SetConsoleMode(hStdin, mode)) return Error::syserr();
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  outMode;
        if (!GetConsoleMode(hStdout, &outMode)) return Error::syserr();
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hStdout, outMode)) return Error::syserr();
        _rawMode = true;
        return Error();
#else
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
        // Check for resize
        if (_resizeCallback) {
                int cols, rows;
                windowSize(cols, rows);
        }
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
        const char *seq = "\033[?1000h\033[?1003h\033[?1006h";
        auto [n, err] = writeOutput(seq, std::strlen(seq));
        if (err.isError()) return err;
        _mouseTracking = true;
        return Error();
}

Error Terminal::disableMouseTracking() {
        if (!_mouseTracking) return Error();
        const char *seq = "\033[?1006l\033[?1003l\033[?1000l";
        auto [n, err] = writeOutput(seq, std::strlen(seq));
        if (err.isError()) return err;
        _mouseTracking = false;
        return Error();
}

Error Terminal::enableBracketedPaste() {
        if (_bracketedPaste) return Error();
        const char *seq = "\033[?2004h";
        auto [n, err] = writeOutput(seq, std::strlen(seq));
        if (err.isError()) return err;
        _bracketedPaste = true;
        return Error();
}

Error Terminal::disableBracketedPaste() {
        if (!_bracketedPaste) return Error();
        const char *seq = "\033[?2004l";
        auto [n, err] = writeOutput(seq, std::strlen(seq));
        if (err.isError()) return err;
        _bracketedPaste = false;
        return Error();
}

Error Terminal::enableAlternateScreen() {
        if (_alternateScreen) return Error();
        const char *seq = "\033[?1049h";
        auto [n, err] = writeOutput(seq, std::strlen(seq));
        if (err.isError()) return err;
        _alternateScreen = true;
        return Error();
}

Error Terminal::disableAlternateScreen() {
        if (!_alternateScreen) return Error();
        const char *seq = "\033[?1049l";
        auto [n, err] = writeOutput(seq, std::strlen(seq));
        if (err.isError()) return err;
        _alternateScreen = false;
        return Error();
}

void Terminal::setResizeCallback(ResizeCallback cb) {
        _resizeCallback = std::move(cb);
#if defined(PROMEKI_PLATFORM_POSIX)
        if (_resizeCallback) {
                signal(SIGWINCH, sigwinchHandler);
        } else {
                signal(SIGWINCH, SIG_DFL);
        }
#endif
}

void Terminal::installSignalHandlers() {
#if defined(PROMEKI_PLATFORM_POSIX)
        g_activeTerminal = this;
        signal(SIGTERM, signalHandler);
        signal(SIGINT, signalHandler);
        signal(SIGQUIT, signalHandler);
        signal(SIGHUP, signalHandler);
#endif
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
#if defined(PROMEKI_PLATFORM_POSIX)
        ssize_t n = write(_outputFd, data, len);
        if (n < 0) return makeError<int>(Error::syserr());
        return makeResult(static_cast<int>(n));
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), data, len, &written, nullptr)) {
                return makeError<int>(Error::syserr());
        }
        return makeResult(static_cast<int>(written));
#else
        return makeError<int>(Error::NotSupported);
#endif
}

PROMEKI_NAMESPACE_END
