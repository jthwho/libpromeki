/*****************************************************************************
 * ansistream.cpp
 * April 28, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <promeki/ansistream.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

static bool checkForAnsiSupport() {
        static const char *supportedTerminals[] = {
                "xterm",
                "xterm-256color",
                "vt100",
                "screen",
                "rxvt-unicode",
                "rxvt",
                "ansi",
                "linux"
        };

        const char* force = std::getenv("PROMEKI_ANSI");
        if(force) return String(force).toBool();

        const char* term = std::getenv("TERM");
        if(term) {
                for(int i = 0; i < PROMEKI_ARRAY_SIZE(supportedTerminals); i++) {
                        if(!std::strcmp(term, supportedTerminals[i])) return true;
                }
        }

        // Check for the presence of the COLORTERM environment variable,
        // which is often set by terminal emulators that support ANSI colors.
        const char* colorTerm = std::getenv("COLORTERM");
        if(colorTerm) return true;

        // Check for the presence of the TERM_PROGRAM environment variable,
        // which is set by some macOS terminal emulators.
        const char* termProgram = std::getenv("TERM_PROGRAM");
        if(termProgram) return true;

        return false;
}

bool AnsiStream::stdoutSupportsANSI() {
        // Only needs to happen once, so we check for ansi support and then
        // cache the answer for the future.
        static bool checked = false;
        static bool ret = false;
        if(!checked) ret = checkForAnsiSupport();
        return ret;
}

bool AnsiStream::stdoutWindowSize(int &rows, int &cols) {
#if defined(_WIN32) || defined(_WIN64)
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

bool AnsiStream::getCursorPosition(std::istream &input, int &row, int &col) {
        if(!_enabled) return false;
        // Request position
        *this << "\033[6n";

        String response;
        char ch;
        bool success = false;

        // Read the response from the specified input stream
        for(int i = 0; i < 20; i++) {
                if(input.fail()) return false;
                input.get(ch);
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

