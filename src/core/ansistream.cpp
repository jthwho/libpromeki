/**
 * @file      ansistream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <climits>
#include <cstdlib>
#include <cstdio>
#include <unordered_map>
#include <promeki/ansistream.h>
#include <promeki/iodevice.h>
#include <promeki/platform.h>
#include <promeki/error.h>
#include <promeki/terminal.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

// -- 256-color ANSI palette (indices 0-15: system colors, 16-231: 6x6x6 cube, 232-255: grayscale) --

static const uint8_t ansiPalette[256][3] = {
        // 0-15: Standard 16 system colors (xterm defaults)
        {  0,   0,   0}, {128,   0,   0}, {  0, 128,   0}, {128, 128,   0},
        {  0,   0, 128}, {128,   0, 128}, {  0, 128, 128}, {192, 192, 192},
        {128, 128, 128}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
        {  0,   0, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255},
        // 16-231: 6x6x6 color cube
        {  0,   0,   0}, {  0,   0,  95}, {  0,   0, 135}, {  0,   0, 175},
        {  0,   0, 215}, {  0,   0, 255}, {  0,  95,   0}, {  0,  95,  95},
        {  0,  95, 135}, {  0,  95, 175}, {  0,  95, 215}, {  0,  95, 255},
        {  0, 135,   0}, {  0, 135,  95}, {  0, 135, 135}, {  0, 135, 175},
        {  0, 135, 215}, {  0, 135, 255}, {  0, 175,   0}, {  0, 175,  95},
        {  0, 175, 135}, {  0, 175, 175}, {  0, 175, 215}, {  0, 175, 255},
        {  0, 215,   0}, {  0, 215,  95}, {  0, 215, 135}, {  0, 215, 175},
        {  0, 215, 215}, {  0, 215, 255}, {  0, 255,   0}, {  0, 255,  95},
        {  0, 255, 135}, {  0, 255, 175}, {  0, 255, 215}, {  0, 255, 255},
        { 95,   0,   0}, { 95,   0,  95}, { 95,   0, 135}, { 95,   0, 175},
        { 95,   0, 215}, { 95,   0, 255}, { 95,  95,   0}, { 95,  95,  95},
        { 95,  95, 135}, { 95,  95, 175}, { 95,  95, 215}, { 95,  95, 255},
        { 95, 135,   0}, { 95, 135,  95}, { 95, 135, 135}, { 95, 135, 175},
        { 95, 135, 215}, { 95, 135, 255}, { 95, 175,   0}, { 95, 175,  95},
        { 95, 175, 135}, { 95, 175, 175}, { 95, 175, 215}, { 95, 175, 255},
        { 95, 215,   0}, { 95, 215,  95}, { 95, 215, 135}, { 95, 215, 175},
        { 95, 215, 215}, { 95, 215, 255}, { 95, 255,   0}, { 95, 255,  95},
        { 95, 255, 135}, { 95, 255, 175}, { 95, 255, 215}, { 95, 255, 255},
        {135,   0,   0}, {135,   0,  95}, {135,   0, 135}, {135,   0, 175},
        {135,   0, 215}, {135,   0, 255}, {135,  95,   0}, {135,  95,  95},
        {135,  95, 135}, {135,  95, 175}, {135,  95, 215}, {135,  95, 255},
        {135, 135,   0}, {135, 135,  95}, {135, 135, 135}, {135, 135, 175},
        {135, 135, 215}, {135, 135, 255}, {135, 175,   0}, {135, 175,  95},
        {135, 175, 135}, {135, 175, 175}, {135, 175, 215}, {135, 175, 255},
        {135, 215,   0}, {135, 215,  95}, {135, 215, 135}, {135, 215, 175},
        {135, 215, 215}, {135, 215, 255}, {135, 255,   0}, {135, 255,  95},
        {135, 255, 135}, {135, 255, 175}, {135, 255, 215}, {135, 255, 255},
        {175,   0,   0}, {175,   0,  95}, {175,   0, 135}, {175,   0, 175},
        {175,   0, 215}, {175,   0, 255}, {175,  95,   0}, {175,  95,  95},
        {175,  95, 135}, {175,  95, 175}, {175,  95, 215}, {175,  95, 255},
        {175, 135,   0}, {175, 135,  95}, {175, 135, 135}, {175, 135, 175},
        {175, 135, 215}, {175, 135, 255}, {175, 175,   0}, {175, 175,  95},
        {175, 175, 135}, {175, 175, 175}, {175, 175, 215}, {175, 175, 255},
        {175, 215,   0}, {175, 215,  95}, {175, 215, 135}, {175, 215, 175},
        {175, 215, 215}, {175, 215, 255}, {175, 255,   0}, {175, 255,  95},
        {175, 255, 135}, {175, 255, 175}, {175, 255, 215}, {175, 255, 255},
        {215,   0,   0}, {215,   0,  95}, {215,   0, 135}, {215,   0, 175},
        {215,   0, 215}, {215,   0, 255}, {215,  95,   0}, {215,  95,  95},
        {215,  95, 135}, {215,  95, 175}, {215,  95, 215}, {215,  95, 255},
        {215, 135,   0}, {215, 135,  95}, {215, 135, 135}, {215, 135, 175},
        {215, 135, 215}, {215, 135, 255}, {215, 175,   0}, {215, 175,  95},
        {215, 175, 135}, {215, 175, 175}, {215, 175, 215}, {215, 175, 255},
        {215, 215,   0}, {215, 215,  95}, {215, 215, 135}, {215, 215, 175},
        {215, 215, 215}, {215, 215, 255}, {215, 255,   0}, {215, 255,  95},
        {215, 255, 135}, {215, 255, 175}, {215, 255, 215}, {215, 255, 255},
        {255,   0,   0}, {255,   0,  95}, {255,   0, 135}, {255,   0, 175},
        {255,   0, 215}, {255,   0, 255}, {255,  95,   0}, {255,  95,  95},
        {255,  95, 135}, {255,  95, 175}, {255,  95, 215}, {255,  95, 255},
        {255, 135,   0}, {255, 135,  95}, {255, 135, 135}, {255, 135, 175},
        {255, 135, 215}, {255, 135, 255}, {255, 175,   0}, {255, 175,  95},
        {255, 175, 135}, {255, 175, 175}, {255, 175, 215}, {255, 175, 255},
        {255, 215,   0}, {255, 215,  95}, {255, 215, 135}, {255, 215, 175},
        {255, 215, 215}, {255, 215, 255}, {255, 255,   0}, {255, 255,  95},
        {255, 255, 135}, {255, 255, 175}, {255, 255, 215}, {255, 255, 255},
        // 232-255: Grayscale ramp (8 + 10*i for i in 0..23)
        {  8,   8,   8}, { 18,  18,  18}, { 28,  28,  28}, { 38,  38,  38},
        { 48,  48,  48}, { 58,  58,  58}, { 68,  68,  68}, { 78,  78,  78},
        { 88,  88,  88}, { 98,  98,  98}, {108, 108, 108}, {118, 118, 118},
        {128, 128, 128}, {138, 138, 138}, {148, 148, 148}, {158, 158, 158},
        {168, 168, 168}, {178, 178, 178}, {188, 188, 188}, {198, 198, 198},
        {208, 208, 208}, {218, 218, 218}, {228, 228, 228}, {238, 238, 238}
};

static uint32_t colorHash(uint8_t r, uint8_t g, uint8_t b) {
        return (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8) | b;
}

static const std::unordered_map<uint32_t, uint8_t> &paletteHashMap() {
        static std::unordered_map<uint32_t, uint8_t> map = []() {
                std::unordered_map<uint32_t, uint8_t> m;
                m.reserve(256);
                for(int i = 0; i < 256; ++i) {
                        uint32_t h = colorHash(ansiPalette[i][0], ansiPalette[i][1], ansiPalette[i][2]);
                        // First entry wins — system colors take priority over cube duplicates
                        if(m.find(h) == m.end()) {
                                m[h] = static_cast<uint8_t>(i);
                        }
                }
                return m;
        }();
        return map;
}

static int colorDistance(int r1, int g1, int b1, int r2, int g2, int b2) {
        int rmean = (r1 + r2) / 2;
        int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
        return ((512 + rmean) * dr * dr >> 8) + 4 * dg * dg +
               ((767 - rmean) * db * db >> 8);
}

static bool isChromatic(uint8_t r, uint8_t g, uint8_t b) {
        int maxC = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int minC = r < g ? (r < b ? r : b) : (g < b ? g : b);
        return maxC > 8 && (maxC - minC) > maxC / 4;
}

Color AnsiStream::ansiColor(int index) {
        if(index < 0 || index > 255) return Color();
        return Color(ansiPalette[index][0], ansiPalette[index][1], ansiPalette[index][2]);
}

AnsiStream::AnsiColor AnsiStream::findClosestAnsiColor(const Color &color, int maxIndex) {
        uint8_t r = color.r8(), g = color.g8(), b = color.b8();

        // Try exact match via hash map
        const auto &map = paletteHashMap();
        auto it = map.find(colorHash(r, g, b));
        if(it != map.end() && it->second <= maxIndex) {
                return static_cast<AnsiColor>(it->second);
        }

        // Search colored entries and grayscale separately.  For chromatic
        // inputs, only accept a grayscale match if it's substantially
        // closer — this prevents dark saturated colors from collapsing
        // to gray.
        int bestColorDist = INT_MAX;
        uint8_t bestColorIdx = 0;
        int bestGrayDist = INT_MAX;
        uint8_t bestGrayIdx = 0;

        for(int i = 0; i <= maxIndex; ++i) {
                int d = colorDistance(r, g, b,
                                     ansiPalette[i][0], ansiPalette[i][1], ansiPalette[i][2]);
                bool gray = (i >= 232) ||
                            (ansiPalette[i][0] == ansiPalette[i][1] &&
                             ansiPalette[i][1] == ansiPalette[i][2]);
                if(gray) {
                        if(d < bestGrayDist) {
                                bestGrayDist = d;
                                bestGrayIdx = static_cast<uint8_t>(i);
                        }
                } else {
                        if(d < bestColorDist) {
                                bestColorDist = d;
                                bestColorIdx = static_cast<uint8_t>(i);
                        }
                }
        }

        if(!isChromatic(r, g, b)) {
                return static_cast<AnsiColor>((bestGrayDist <= bestColorDist) ? bestGrayIdx : bestColorIdx);
        }
        if(bestGrayDist * 3 < bestColorDist) {
                return static_cast<AnsiColor>(bestGrayIdx);
        }
        return static_cast<AnsiColor>(bestColorIdx);
}

AnsiStream &AnsiStream::setForeground(AnsiColor color) {
        if(!_enabled) return *this;
        uint8_t idx = static_cast<uint8_t>(color);
        if(idx < 8) {
                *this << "\033[" << (30 + idx) << "m";
        } else if(idx < 16) {
                *this << "\033[" << (90 + idx - 8) << "m";
        } else {
                setForeground256(idx);
        }
        return *this;
}

AnsiStream &AnsiStream::setBackground(AnsiColor color) {
        if(!_enabled) return *this;
        uint8_t idx = static_cast<uint8_t>(color);
        if(idx < 8) {
                *this << "\033[" << (40 + idx) << "m";
        } else if(idx < 16) {
                *this << "\033[" << (100 + idx - 8) << "m";
        } else {
                setBackground256(idx);
        }
        return *this;
}

AnsiStream &AnsiStream::setForeground(const Color &color, int maxIndex) {
        if(!_enabled) return *this;
        return setForeground(findClosestAnsiColor(color, maxIndex));
}

AnsiStream &AnsiStream::setBackground(const Color &color, int maxIndex) {
        if(!_enabled) return *this;
        return setBackground(findClosestAnsiColor(color, maxIndex));
}

bool AnsiStream::stdoutSupportsANSI() {
        return Terminal::colorSupport() > Terminal::NoColor;
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
        const std::string &s = text.str();
        _device->write(s.data(), static_cast<int64_t>(s.size()));
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
        int len = std::snprintf(buf, sizeof(buf), "%d", val);
        if(len > 0) _device->write(buf, len);
        return *this;
}

void AnsiStream::flush() {
        _device->flush();
}

bool AnsiStream::getCursorPosition(IODevice *input, int &row, int &col) {
        if(!_enabled) return false;
        // Request position
        *this << "\033[6n";

        String response;
        char ch;
        bool success = false;

        // Read the response from the specified input device
        for(int i = 0; i < 20; i++) {
                if(input->read(&ch, 1) != 1) return false;
                if(ch == 'R') {
                        success = true;
                        break;
                }
                response += ch;
        }

        // Parse the response to extract the row and column values
        if(response.length() >= 4 && response[0] == '\033' && response[1] == '[') {
                size_t semicolonPos = response.find(';');
                if(semicolonPos != std::string::npos) {
                        Error err;
                        row = response.substr(2, semicolonPos - 2).toInt(&err);
                        if(err.isError()) return false;
                        col = response.substr(semicolonPos + 1).toInt(&err);
                        return err.isOk();
                }
        }
        return false;
}

PROMEKI_NAMESPACE_END
