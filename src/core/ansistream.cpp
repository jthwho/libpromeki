/**
 * @file      ansistream.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <promeki/ansistream.h>
#include <promeki/base64.h>
#include <promeki/hashmap.h>
#include <promeki/iodevice.h>
#include <promeki/platform.h>
#include <promeki/error.h>
#include <promeki/terminal.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

// -- 256-color ANSI palette (indices 0-15: system colors, 16-231: 6x6x6 cube, 232-255: grayscale) --

static const uint8_t ansiPalette[256][3] = {
        // 0-15: Standard 16 system colors (xterm defaults)
        {0, 0, 0},
        {128, 0, 0},
        {0, 128, 0},
        {128, 128, 0},
        {0, 0, 128},
        {128, 0, 128},
        {0, 128, 128},
        {192, 192, 192},
        {128, 128, 128},
        {255, 0, 0},
        {0, 255, 0},
        {255, 255, 0},
        {0, 0, 255},
        {255, 0, 255},
        {0, 255, 255},
        {255, 255, 255},
        // 16-231: 6x6x6 color cube
        {0, 0, 0},
        {0, 0, 95},
        {0, 0, 135},
        {0, 0, 175},
        {0, 0, 215},
        {0, 0, 255},
        {0, 95, 0},
        {0, 95, 95},
        {0, 95, 135},
        {0, 95, 175},
        {0, 95, 215},
        {0, 95, 255},
        {0, 135, 0},
        {0, 135, 95},
        {0, 135, 135},
        {0, 135, 175},
        {0, 135, 215},
        {0, 135, 255},
        {0, 175, 0},
        {0, 175, 95},
        {0, 175, 135},
        {0, 175, 175},
        {0, 175, 215},
        {0, 175, 255},
        {0, 215, 0},
        {0, 215, 95},
        {0, 215, 135},
        {0, 215, 175},
        {0, 215, 215},
        {0, 215, 255},
        {0, 255, 0},
        {0, 255, 95},
        {0, 255, 135},
        {0, 255, 175},
        {0, 255, 215},
        {0, 255, 255},
        {95, 0, 0},
        {95, 0, 95},
        {95, 0, 135},
        {95, 0, 175},
        {95, 0, 215},
        {95, 0, 255},
        {95, 95, 0},
        {95, 95, 95},
        {95, 95, 135},
        {95, 95, 175},
        {95, 95, 215},
        {95, 95, 255},
        {95, 135, 0},
        {95, 135, 95},
        {95, 135, 135},
        {95, 135, 175},
        {95, 135, 215},
        {95, 135, 255},
        {95, 175, 0},
        {95, 175, 95},
        {95, 175, 135},
        {95, 175, 175},
        {95, 175, 215},
        {95, 175, 255},
        {95, 215, 0},
        {95, 215, 95},
        {95, 215, 135},
        {95, 215, 175},
        {95, 215, 215},
        {95, 215, 255},
        {95, 255, 0},
        {95, 255, 95},
        {95, 255, 135},
        {95, 255, 175},
        {95, 255, 215},
        {95, 255, 255},
        {135, 0, 0},
        {135, 0, 95},
        {135, 0, 135},
        {135, 0, 175},
        {135, 0, 215},
        {135, 0, 255},
        {135, 95, 0},
        {135, 95, 95},
        {135, 95, 135},
        {135, 95, 175},
        {135, 95, 215},
        {135, 95, 255},
        {135, 135, 0},
        {135, 135, 95},
        {135, 135, 135},
        {135, 135, 175},
        {135, 135, 215},
        {135, 135, 255},
        {135, 175, 0},
        {135, 175, 95},
        {135, 175, 135},
        {135, 175, 175},
        {135, 175, 215},
        {135, 175, 255},
        {135, 215, 0},
        {135, 215, 95},
        {135, 215, 135},
        {135, 215, 175},
        {135, 215, 215},
        {135, 215, 255},
        {135, 255, 0},
        {135, 255, 95},
        {135, 255, 135},
        {135, 255, 175},
        {135, 255, 215},
        {135, 255, 255},
        {175, 0, 0},
        {175, 0, 95},
        {175, 0, 135},
        {175, 0, 175},
        {175, 0, 215},
        {175, 0, 255},
        {175, 95, 0},
        {175, 95, 95},
        {175, 95, 135},
        {175, 95, 175},
        {175, 95, 215},
        {175, 95, 255},
        {175, 135, 0},
        {175, 135, 95},
        {175, 135, 135},
        {175, 135, 175},
        {175, 135, 215},
        {175, 135, 255},
        {175, 175, 0},
        {175, 175, 95},
        {175, 175, 135},
        {175, 175, 175},
        {175, 175, 215},
        {175, 175, 255},
        {175, 215, 0},
        {175, 215, 95},
        {175, 215, 135},
        {175, 215, 175},
        {175, 215, 215},
        {175, 215, 255},
        {175, 255, 0},
        {175, 255, 95},
        {175, 255, 135},
        {175, 255, 175},
        {175, 255, 215},
        {175, 255, 255},
        {215, 0, 0},
        {215, 0, 95},
        {215, 0, 135},
        {215, 0, 175},
        {215, 0, 215},
        {215, 0, 255},
        {215, 95, 0},
        {215, 95, 95},
        {215, 95, 135},
        {215, 95, 175},
        {215, 95, 215},
        {215, 95, 255},
        {215, 135, 0},
        {215, 135, 95},
        {215, 135, 135},
        {215, 135, 175},
        {215, 135, 215},
        {215, 135, 255},
        {215, 175, 0},
        {215, 175, 95},
        {215, 175, 135},
        {215, 175, 175},
        {215, 175, 215},
        {215, 175, 255},
        {215, 215, 0},
        {215, 215, 95},
        {215, 215, 135},
        {215, 215, 175},
        {215, 215, 215},
        {215, 215, 255},
        {215, 255, 0},
        {215, 255, 95},
        {215, 255, 135},
        {215, 255, 175},
        {215, 255, 215},
        {215, 255, 255},
        {255, 0, 0},
        {255, 0, 95},
        {255, 0, 135},
        {255, 0, 175},
        {255, 0, 215},
        {255, 0, 255},
        {255, 95, 0},
        {255, 95, 95},
        {255, 95, 135},
        {255, 95, 175},
        {255, 95, 215},
        {255, 95, 255},
        {255, 135, 0},
        {255, 135, 95},
        {255, 135, 135},
        {255, 135, 175},
        {255, 135, 215},
        {255, 135, 255},
        {255, 175, 0},
        {255, 175, 95},
        {255, 175, 135},
        {255, 175, 175},
        {255, 175, 215},
        {255, 175, 255},
        {255, 215, 0},
        {255, 215, 95},
        {255, 215, 135},
        {255, 215, 175},
        {255, 215, 215},
        {255, 215, 255},
        {255, 255, 0},
        {255, 255, 95},
        {255, 255, 135},
        {255, 255, 175},
        {255, 255, 215},
        {255, 255, 255},
        // 232-255: Grayscale ramp (8 + 10*i for i in 0..23)
        {8, 8, 8},
        {18, 18, 18},
        {28, 28, 28},
        {38, 38, 38},
        {48, 48, 48},
        {58, 58, 58},
        {68, 68, 68},
        {78, 78, 78},
        {88, 88, 88},
        {98, 98, 98},
        {108, 108, 108},
        {118, 118, 118},
        {128, 128, 128},
        {138, 138, 138},
        {148, 148, 148},
        {158, 158, 158},
        {168, 168, 168},
        {178, 178, 178},
        {188, 188, 188},
        {198, 198, 198},
        {208, 208, 208},
        {218, 218, 218},
        {228, 228, 228},
        {238, 238, 238}};

static uint32_t colorHash(uint8_t r, uint8_t g, uint8_t b) {
        return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

static const HashMap<uint32_t, uint8_t> &paletteHashMap() {
        static HashMap<uint32_t, uint8_t> map = []() {
                HashMap<uint32_t, uint8_t> m;
                m.reserve(256);
                for (int i = 0; i < 256; ++i) {
                        uint32_t h = colorHash(ansiPalette[i][0], ansiPalette[i][1], ansiPalette[i][2]);
                        // First entry wins — system colors take priority over cube duplicates
                        if (!m.contains(h)) {
                                m.insert(h, static_cast<uint8_t>(i));
                        }
                }
                return m;
        }();
        return map;
}

static int colorDistance(int r1, int g1, int b1, int r2, int g2, int b2) {
        int rmean = (r1 + r2) / 2;
        int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
        return ((512 + rmean) * dr * dr >> 8) + 4 * dg * dg + ((767 - rmean) * db * db >> 8);
}

static bool isChromatic(uint8_t r, uint8_t g, uint8_t b) {
        int maxC = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int minC = r < g ? (r < b ? r : b) : (g < b ? g : b);
        return maxC > 8 && (maxC - minC) > maxC / 4;
}

// Convert a Color to its perceptual grayscale value (Rec. 709).
static uint8_t colorToGray(const Color &color) {
        return static_cast<uint8_t>(
                std::round(255.0 * (0.2126 * color.r() + 0.7152 * color.g() + 0.0722 * color.b())));
}

// Map a grayscale value to one of the 4 gray levels in the 16-color palette.
// Only exact black (0) maps to Black; everything else maps to DarkGray,
// Silver, or White.
static AnsiStream::AnsiColor grayToAnsi16(uint8_t gray) {
        if (gray == 0) return AnsiStream::Black;
        if (gray <= 96) return AnsiStream::DarkGray;
        if (gray <= 192) return AnsiStream::Silver;
        return AnsiStream::White;
}

// Map a grayscale value to the closest entry in the 256-color grayscale ramp.
// Uses Black (0) and White (15) for the extremes, and the 24-entry ramp
// (232-255) which covers gray levels 8, 18, ..., 238 in steps of 10.
static AnsiStream::AnsiColor grayToAnsi256(uint8_t gray) {
        if (gray < 4) return AnsiStream::Black;
        if (gray > 246) return AnsiStream::White;
        int idx = static_cast<int>(std::round((gray - 8.0) / 10.0));
        if (idx < 0) idx = 0;
        if (idx > 23) idx = 23;
        return static_cast<AnsiStream::AnsiColor>(232 + idx);
}

Color AnsiStream::ansiColor(int index) {
        if (index < 0 || index > 255) return Color();
        return Color(ansiPalette[index][0], ansiPalette[index][1], ansiPalette[index][2]);
}

AnsiStream::AnsiColor AnsiStream::findClosestAnsiColor(const Color &color, int maxIndex) {
        uint8_t r = color.r8(), g = color.g8(), b = color.b8();

        // Try exact match via hash map
        const auto &map = paletteHashMap();
        auto        it = map.find(colorHash(r, g, b));
        if (it != map.end() && it->second <= maxIndex) {
                return static_cast<AnsiColor>(it->second);
        }

        // Search colored entries and grayscale separately.  For chromatic
        // inputs, only accept a grayscale match if it's substantially
        // closer — this prevents dark saturated colors from collapsing
        // to gray.
        int     bestColorDist = INT_MAX;
        uint8_t bestColorIdx = 0;
        int     bestGrayDist = INT_MAX;
        uint8_t bestGrayIdx = 0;

        for (int i = 0; i <= maxIndex; ++i) {
                int  d = colorDistance(r, g, b, ansiPalette[i][0], ansiPalette[i][1], ansiPalette[i][2]);
                bool gray = (i >= 232) ||
                            (ansiPalette[i][0] == ansiPalette[i][1] && ansiPalette[i][1] == ansiPalette[i][2]);
                if (gray) {
                        if (d < bestGrayDist) {
                                bestGrayDist = d;
                                bestGrayIdx = static_cast<uint8_t>(i);
                        }
                } else {
                        if (d < bestColorDist) {
                                bestColorDist = d;
                                bestColorIdx = static_cast<uint8_t>(i);
                        }
                }
        }

        if (!isChromatic(r, g, b)) {
                return static_cast<AnsiColor>((bestGrayDist <= bestColorDist) ? bestGrayIdx : bestColorIdx);
        }
        if (bestGrayDist * 3 < bestColorDist) {
                return static_cast<AnsiColor>(bestGrayIdx);
        }
        return static_cast<AnsiColor>(bestColorIdx);
}

// -- Static escape-sequence builders (single source of truth) --

static String sgr(int code) {
        String s("\033[");
        s += String::number(code);
        s += "m";
        return s;
}

String AnsiStream::resetSeq() {
        return String("\033[0m");
}

String AnsiStream::styleSeq(TextStyle style, bool enable) {
        if (enable) return sgr(static_cast<int>(style));
        int code;
        switch (style) {
                case Bold:
                case Dim: code = 22; break; // Bold and Dim share a single off code.
                case Italic: code = 23; break;
                case Underlined: code = 24; break;
                case Blink: code = 25; break;
                case Inverted: code = 27; break;
                case Hidden: code = 28; break;
                case Strikethrough: code = 29; break;
                default: code = 0; break;
        }
        return sgr(code);
}

String AnsiStream::foregroundSeq(AnsiColor color) {
        uint8_t idx = static_cast<uint8_t>(color);
        if (idx < 8) return sgr(30 + idx);
        if (idx < 16) return sgr(90 + idx - 8);
        return foreground256Seq(idx);
}

String AnsiStream::backgroundSeq(AnsiColor color) {
        uint8_t idx = static_cast<uint8_t>(color);
        if (idx < 8) return sgr(40 + idx);
        if (idx < 16) return sgr(100 + idx - 8);
        return background256Seq(idx);
}

String AnsiStream::foreground256Seq(uint8_t index) {
        String s("\033[38;5;");
        s += String::number(static_cast<int>(index));
        s += "m";
        return s;
}

String AnsiStream::background256Seq(uint8_t index) {
        String s("\033[48;5;");
        s += String::number(static_cast<int>(index));
        s += "m";
        return s;
}

String AnsiStream::foregroundRGBSeq(uint8_t r, uint8_t g, uint8_t b) {
        String s("\033[38;2;");
        s += String::number(static_cast<int>(r));
        s += ";";
        s += String::number(static_cast<int>(g));
        s += ";";
        s += String::number(static_cast<int>(b));
        s += "m";
        return s;
}

String AnsiStream::backgroundRGBSeq(uint8_t r, uint8_t g, uint8_t b) {
        String s("\033[48;2;");
        s += String::number(static_cast<int>(r));
        s += ";";
        s += String::number(static_cast<int>(g));
        s += ";";
        s += String::number(static_cast<int>(b));
        s += "m";
        return s;
}

String AnsiStream::foregroundSeq(const Color &color, Terminal::ColorSupport support) {
        switch (support) {
                case Terminal::NoColor: return String();
                case Terminal::Grayscale16: return foregroundSeq(grayToAnsi16(colorToGray(color)));
                case Terminal::Grayscale256: return foregroundSeq(grayToAnsi256(colorToGray(color)));
                case Terminal::GrayscaleTrue: {
                        uint8_t y = colorToGray(color);
                        return foregroundRGBSeq(y, y, y);
                }
                case Terminal::TrueColor: return foregroundRGBSeq(color.r8(), color.g8(), color.b8());
                case Terminal::Color256: return foregroundSeq(findClosestAnsiColor(color, 255));
                case Terminal::Basic: return foregroundSeq(findClosestAnsiColor(color, 15));
        }
        return String();
}

String AnsiStream::backgroundSeq(const Color &color, Terminal::ColorSupport support) {
        switch (support) {
                case Terminal::NoColor: return String();
                case Terminal::Grayscale16: return backgroundSeq(grayToAnsi16(colorToGray(color)));
                case Terminal::Grayscale256: return backgroundSeq(grayToAnsi256(colorToGray(color)));
                case Terminal::GrayscaleTrue: {
                        uint8_t y = colorToGray(color);
                        return backgroundRGBSeq(y, y, y);
                }
                case Terminal::TrueColor: return backgroundRGBSeq(color.r8(), color.g8(), color.b8());
                case Terminal::Color256: return backgroundSeq(findClosestAnsiColor(color, 255));
                case Terminal::Basic: return backgroundSeq(findClosestAnsiColor(color, 15));
        }
        return String();
}

String AnsiStream::hyperlinkSeq(const String &url, const String &text) {
        String s("\033]8;;");
        s += url;
        s += "\033\\"; // ST (string terminator)
        s += text;
        s += "\033]8;;\033\\";
        return s;
}

AnsiStream &AnsiStream::setForeground(AnsiColor color) {
        if (!_enabled) return *this;
        return write(foregroundSeq(color));
}

AnsiStream &AnsiStream::setBackground(AnsiColor color) {
        if (!_enabled) return *this;
        return write(backgroundSeq(color));
}

AnsiStream &AnsiStream::setForeground(const Color &color, int maxIndex) {
        if (!_enabled) return *this;
        return setForeground(findClosestAnsiColor(color, maxIndex));
}

AnsiStream &AnsiStream::setBackground(const Color &color, int maxIndex) {
        if (!_enabled) return *this;
        return setBackground(findClosestAnsiColor(color, maxIndex));
}

AnsiStream &AnsiStream::setForeground(const Color &color, Terminal::ColorSupport support) {
        if (!_enabled) return *this;
        return write(foregroundSeq(color, support));
}

AnsiStream &AnsiStream::setBackground(const Color &color, Terminal::ColorSupport support) {
        if (!_enabled) return *this;
        return write(backgroundSeq(color, support));
}

AnsiStream &AnsiStream::hyperlink(const String &url, const String &text) {
        // When ANSI is disabled, fall back to plain visible text so callers
        // can emit links unconditionally without leaking escape bytes into
        // pipes / log files.
        if (!_enabled) return write(text);
        return write(hyperlinkSeq(url, text));
}

AnsiStream &AnsiStream::setWindowTitle(const String &title) {
        if (!_enabled) return *this;
        String s("\033]2;");
        s += title;
        s += "\033\\";
        return write(s);
}

AnsiStream &AnsiStream::copyToClipboard(const String &text) {
        if (!_enabled) return *this;
        String s("\033]52;c;");
        s += Base64::encode(text.cstr(), text.byteCount());
        s += "\033\\";
        return write(s);
}

namespace {
        // Shared implementation behind stdoutSupportsANSI / stderrSupportsANSI.
        // @p isTerminal is the platform isatty result for the fd in question.
        bool consoleSupportsANSI(bool isTerminal) {
                // Even when env vars say the terminal supports color, redirecting
                // the stream to a pipe / file / CI log should drop the escape
                // codes — they show up as `^[[36m` garbage in saved logs and
                // break downstream parsers that don't know to strip them.  isatty
                // is the cheapest signal for "the stream is actually a terminal".
                if (!isTerminal) return false;
                // NO_COLOR (https://no-color.org/) is "no ANSI codes at all".
                // Terminal::colorSupport degrades to a grayscale level when
                // NO_COLOR is set so TUIs that want to keep some structure can
                // still render — but raw-text users (help output, log lines)
                // expect zero escape sequences, so we treat NO_COLOR as a hard
                // off here.  Presence-only per the spec.
                const char *noColor = std::getenv("NO_COLOR");
                if (noColor != nullptr && *noColor != '\0') return false;
                return Terminal::colorSupport() > Terminal::NoColor;
        }
} // namespace

bool AnsiStream::stdoutSupportsANSI() {
#if defined(PROMEKI_PLATFORM_WINDOWS)
        return consoleSupportsANSI(_isatty(_fileno(stdout)) != 0);
#else
        return consoleSupportsANSI(::isatty(STDOUT_FILENO) != 0);
#endif
}

bool AnsiStream::stderrSupportsANSI() {
#if defined(PROMEKI_PLATFORM_WINDOWS)
        return consoleSupportsANSI(_isatty(_fileno(stderr)) != 0);
#else
        return consoleSupportsANSI(::isatty(STDERR_FILENO) != 0);
#endif
}

bool AnsiStream::stdoutWindowSize(int &rows, int &cols) {
#if defined(PROMEKI_PLATFORM_WINDOWS)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
                cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
                rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
                return true;
        }
#else
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
                rows = ws.ws_row;
                cols = ws.ws_col;
                return true;
        }
#endif
        return false;
}

AnsiStream &AnsiStream::write(const String &text) {
        _device->write(text.cstr(), static_cast<int64_t>(text.byteCount()));
        return *this;
}

AnsiStream &AnsiStream::write(const char *text) {
        size_t len = std::strlen(text);
        _device->write(text, static_cast<int64_t>(len));
        return *this;
}

AnsiStream &AnsiStream::write(char ch) {
        _device->write(&ch, 1);
        return *this;
}

AnsiStream &AnsiStream::write(int val) {
        char buf[32];
        int  len = std::snprintf(buf, sizeof(buf), "%d", val);
        if (len > 0) _device->write(buf, len);
        return *this;
}

AnsiStream &AnsiStream::write(int64_t val) {
        char buf[32];
        int  len = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(val));
        if (len > 0) _device->write(buf, len);
        return *this;
}

AnsiStream &AnsiStream::write(uint64_t val) {
        char buf[32];
        int  len = std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(val));
        if (len > 0) _device->write(buf, len);
        return *this;
}

AnsiStream &AnsiStream::write(double val) {
        char buf[64];
        int  len = std::snprintf(buf, sizeof(buf), "%g", val);
        if (len > 0) _device->write(buf, len);
        return *this;
}

void AnsiStream::flush() {
        _device->flush();
}

bool AnsiStream::getCursorPosition(IODevice *input, int &row, int &col) {
        if (!_enabled) return false;
        // Request position
        *this << "\033[6n";

        String response;
        char   ch;
        bool   success = false;

        // Read the response from the specified input device
        for (int i = 0; i < 20; i++) {
                if (input->read(&ch, 1) != 1) return false;
                if (ch == 'R') {
                        success = true;
                        break;
                }
                response += ch;
        }
        // Loop exited without seeing the 'R' terminator: response is
        // truncated and unsafe to parse.
        if (!success) return false;

        // Parse the response to extract the row and column values
        if (response.length() >= 4 && response.charAt(0) == '\033' && response.charAt(1) == '[') {
                size_t semicolonPos = response.find(';');
                if (semicolonPos != String::npos) {
                        Error err;
                        row = response.substr(2, semicolonPos - 2).toInt(&err);
                        if (err.isError()) return false;
                        col = response.substr(semicolonPos + 1).toInt(&err);
                        return err.isOk();
                }
        }
        return false;
}

PROMEKI_NAMESPACE_END
