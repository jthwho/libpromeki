/**
 * @file      inputtest.cpp
 * @brief     Minimal test to debug raw terminal input
 */

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <fcntl.h>

int main() {
        // Save original terminal state
        struct termios orig;
        tcgetattr(STDIN_FILENO, &orig);

        // Set raw mode (same as Terminal::enableRawMode)
        struct termios raw = orig;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

        // Restore on exit
        auto cleanup = [&]() {
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
        };

        // Use write() since OPOST is off (no newline translation)
        auto writeStr = [](const char *s) {
                write(STDOUT_FILENO, s, strlen(s));
        };

        writeStr("Raw input test. Press keys (Ctrl+Q to quit).\r\n");
        writeStr("Each byte shown as hex. Sequences grouped by read().\r\n\r\n");

        char buf[256];
        for (;;) {
                struct pollfd pfd;
                pfd.fd = STDIN_FILENO;
                pfd.events = POLLIN;
                int ret = poll(&pfd, 1, 100);

                if (ret > 0 && (pfd.revents & POLLIN)) {
                        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                        if (n > 0) {
                                char out[1024];
                                int  pos = 0;
                                pos += snprintf(out + pos, sizeof(out) - pos, "read %zd bytes: ", n);
                                for (ssize_t i = 0; i < n; i++) {
                                        pos += snprintf(out + pos, sizeof(out) - pos, "0x%02X ", (unsigned char)buf[i]);
                                }
                                // Also show printable interpretation
                                pos += snprintf(out + pos, sizeof(out) - pos, " [");
                                for (ssize_t i = 0; i < n; i++) {
                                        char c = buf[i];
                                        if (c == 0x09)
                                                pos += snprintf(out + pos, sizeof(out) - pos, "TAB");
                                        else if (c == 0x0D)
                                                pos += snprintf(out + pos, sizeof(out) - pos, "CR");
                                        else if (c == 0x0A)
                                                pos += snprintf(out + pos, sizeof(out) - pos, "LF");
                                        else if (c == 0x1B)
                                                pos += snprintf(out + pos, sizeof(out) - pos, "ESC");
                                        else if (c >= 1 && c <= 26)
                                                pos += snprintf(out + pos, sizeof(out) - pos, "^%c", c + 'A' - 1);
                                        else if (c >= 32 && c < 127)
                                                pos += snprintf(out + pos, sizeof(out) - pos, "%c", c);
                                        else
                                                pos += snprintf(out + pos, sizeof(out) - pos, "?");
                                        if (i < n - 1) pos += snprintf(out + pos, sizeof(out) - pos, " ");
                                }
                                pos += snprintf(out + pos, sizeof(out) - pos, "]\r\n");
                                write(STDOUT_FILENO, out, pos);

                                // Ctrl+Q to quit
                                if (n == 1 && buf[0] == 0x11) break;
                        }
                }
        }

        cleanup();
        return 0;
}
