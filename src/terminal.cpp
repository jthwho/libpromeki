/**
 * @file      terminal.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <promeki/terminal.h>

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
        if(g_activeTerminal) {
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

Terminal::Terminal() {
#if defined(PROMEKI_PLATFORM_POSIX)
        _origState = new ::termios;
        std::memset(_origState, 0, sizeof(::termios));
#endif
}

Terminal::~Terminal() {
        if(_mouseTracking) disableMouseTracking();
        if(_bracketedPaste) disableBracketedPaste();
        if(_alternateScreen) disableAlternateScreen();
        if(_rawMode) disableRawMode();
#if defined(PROMEKI_PLATFORM_POSIX)
        if(g_activeTerminal == this) g_activeTerminal = nullptr;
        delete static_cast<::termios *>(_origState);
#endif
}

bool Terminal::enableRawMode() {
        if(_rawMode) return true;
#if defined(PROMEKI_PLATFORM_POSIX)
        ::termios *orig = static_cast<::termios *>(_origState);
        if(tcgetattr(STDIN_FILENO, orig) == -1) return false;
        ::termios raw = *orig;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return false;
        _rawMode = true;
        return true;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hStdin, &mode);
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(hStdin, mode);
        HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD outMode;
        GetConsoleMode(hStdout, &outMode);
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hStdout, outMode);
        _rawMode = true;
        return true;
#else
        return false;
#endif
}

bool Terminal::disableRawMode() {
        if(!_rawMode) return true;
#if defined(PROMEKI_PLATFORM_POSIX)
        if(tcsetattr(STDIN_FILENO, TCSAFLUSH, static_cast<::termios *>(_origState)) == -1) return false;
        _rawMode = false;
        return true;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        _rawMode = false;
        return true;
#else
        return false;
#endif
}

int Terminal::readInput(char *buf, int maxLen) {
#if defined(PROMEKI_PLATFORM_POSIX)
        // Check for resize
        if(_resizeCallback) {
                int cols, rows;
                if(windowSize(cols, rows)) {
                        // We'll let the caller handle resize detection
                }
        }
        ssize_t n = read(STDIN_FILENO, buf, maxLen);
        if(n == -1) {
                if(errno == EAGAIN || errno == EWOULDBLOCK) return 0;
                return -1;
        }
        return static_cast<int>(n);
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD available = 0;
        if(!PeekConsoleInput(hStdin, nullptr, 0, &available)) return -1;
        if(available == 0) return 0;
        DWORD bytesRead = 0;
        if(!ReadFile(hStdin, buf, maxLen, &bytesRead, nullptr)) return -1;
        return static_cast<int>(bytesRead);
#else
        return -1;
#endif
}

bool Terminal::windowSize(int &cols, int &rows) const {
#if defined(PROMEKI_PLATFORM_POSIX)
        struct winsize ws;
        if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                cols = ws.ws_col;
                rows = ws.ws_row;
                return true;
        }
        return false;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
                cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
                rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
                return true;
        }
        return false;
#else
        return false;
#endif
}

Size2Di32 Terminal::size() const {
        int cols = 0, rows = 0;
        windowSize(cols, rows);
        return Size2Di32(cols, rows);
}

bool Terminal::enableMouseTracking() {
        if(_mouseTracking) return true;
        // Enable SGR mouse mode with any-event tracking (xterm)
        const char *seq = "\033[?1000h\033[?1003h\033[?1006h";
        writeOutput(seq, std::strlen(seq));
        _mouseTracking = true;
        return true;
}

bool Terminal::disableMouseTracking() {
        if(!_mouseTracking) return true;
        const char *seq = "\033[?1006l\033[?1003l\033[?1000l";
        writeOutput(seq, std::strlen(seq));
        _mouseTracking = false;
        return true;
}

bool Terminal::enableBracketedPaste() {
        if(_bracketedPaste) return true;
        const char *seq = "\033[?2004h";
        writeOutput(seq, std::strlen(seq));
        _bracketedPaste = true;
        return true;
}

bool Terminal::disableBracketedPaste() {
        if(!_bracketedPaste) return true;
        const char *seq = "\033[?2004l";
        writeOutput(seq, std::strlen(seq));
        _bracketedPaste = false;
        return true;
}

bool Terminal::enableAlternateScreen() {
        if(_alternateScreen) return true;
        const char *seq = "\033[?1049h";
        writeOutput(seq, std::strlen(seq));
        _alternateScreen = true;
        return true;
}

bool Terminal::disableAlternateScreen() {
        if(!_alternateScreen) return true;
        const char *seq = "\033[?1049l";
        writeOutput(seq, std::strlen(seq));
        _alternateScreen = false;
        return true;
}

void Terminal::setResizeCallback(ResizeCallback cb) {
        _resizeCallback = std::move(cb);
#if defined(PROMEKI_PLATFORM_POSIX)
        if(_resizeCallback) {
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

int Terminal::writeOutput(const char *data, int len) {
#if defined(PROMEKI_PLATFORM_POSIX)
        ssize_t n = write(STDOUT_FILENO, data, len);
        return n >= 0 ? static_cast<int>(n) : -1;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), data, len, &written, nullptr);
        return static_cast<int>(written);
#else
        return -1;
#endif
}

PROMEKI_NAMESPACE_END
