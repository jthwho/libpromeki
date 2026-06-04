/**
 * @file      ansistream.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/util.h>
#include <promeki/color.h>
#include <promeki/terminal.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief ANSI escape code writer backed by an IODevice.
 * Writes ANSI escape sequences and raw text to an IODevice.
 * Also serves as the source of truth for ANSI color palette data
 * and color matching.
 * @ingroup strings
 */
class AnsiStream {
        public:
                /**
                 * @brief ANSI text styles.
                 */
                enum TextStyle {
                        Bold = 1,         ///< Bold or increased intensity.
                        Dim = 2,          ///< Faint or decreased intensity.
                        Italic = 3,       ///< Italic text.
                        Underlined = 4,   ///< Underlined text.
                        Blink = 5,        ///< Blinking text.
                        Inverted = 7,     ///< Swapped foreground and background colors.
                        Hidden = 8,       ///< Hidden (invisible) text.
                        Strikethrough = 9 ///< Struck-through (crossed-out) text.
                };

                /**
                 * @brief Mouse-tracking detail level.
                 *
                 * All levels use the SGR (1006) extended coordinate encoding;
                 * they differ only in which motion events the terminal reports.
                 */
                enum MouseTracking {
                        MouseButtons, ///< Press / release only (mode 1000).
                        MouseDrag,    ///< Press / release + motion while a button is held (1000 + 1002).
                        MouseAny      ///< Press / release + all motion, even with no button (1000 + 1003).
                };

                /**
                 * @brief Cursor shape styles (DECSCUSR).
                 *
                 * Not all terminals honour every value; unsupported shapes
                 * typically fall back to the terminal default.
                 */
                enum CursorStyle {
                        DefaultCursor = 0,       ///< Terminal default shape.
                        BlinkingBlock = 1,       ///< Blinking block.
                        SteadyBlock = 2,         ///< Steady block.
                        BlinkingUnderline = 3,   ///< Blinking underline.
                        SteadyUnderline = 4,     ///< Steady underline.
                        BlinkingBar = 5,         ///< Blinking vertical bar.
                        SteadyBar = 6            ///< Steady vertical bar.
                };

                /**
                 * @brief ANSI 256-color palette indices.
                 *
                 * Values 0-15 are the 16 standard system colors.
                 * Values 16-231 are the 6x6x6 color cube.
                 * Values 232-255 are the grayscale ramp.
                 * Any uint8_t value is a valid AnsiColor.
                 *
                 * Xterm standard names are used throughout.  Where the same
                 * xterm name appears at multiple palette indices, the index
                 * is appended as a suffix to disambiguate.
                 *
                 * @see https://www.ditig.com/256-colors-cheat-sheet
                 */
                enum AnsiColor : uint8_t {
                        // -- System colors (0-15) --
                        Black = 0,
                        Maroon = 1,
                        DarkRed = 1, ///< Alias for Maroon.
                        Green = 2,
                        DarkGreen = 2, ///< Alias for Green (system).
                        Olive = 3,
                        DarkYellow = 3, ///< Alias for Olive.
                        Navy = 4,
                        DarkBlue = 4, ///< Alias for Navy.
                        Purple = 5,
                        DarkMagenta = 5, ///< Alias for Purple (system).
                        Teal = 6,
                        DarkCyan = 6, ///< Alias for Teal.
                        Silver = 7,
                        LightGray = 7, ///< Alias for Silver.
                        Grey = 8,
                        DarkGray = 8, ///< Alias for Grey.
                        Red = 9,
                        Lime = 10,
                        Yellow = 11,
                        Blue = 12,
                        Fuchsia = 13,
                        Magenta = 13, ///< Alias for Fuchsia.
                        Aqua = 14,
                        Cyan = 14, ///< Alias for Aqua.
                        White = 15,

                        // -- Color cube (16-231) --
                        Grey0 = 16,
                        NavyBlue = 17,
                        DarkBlue_18 = 18,
                        Blue3_19 = 19,
                        Blue3_20 = 20,
                        Blue1 = 21,
                        DarkGreen_22 = 22,
                        DeepSkyBlue4_23 = 23,
                        DeepSkyBlue4_24 = 24,
                        DeepSkyBlue4_25 = 25,
                        DodgerBlue3 = 26,
                        DodgerBlue2 = 27,
                        Green4 = 28,
                        SpringGreen4 = 29,
                        Turquoise4 = 30,
                        DeepSkyBlue3_31 = 31,
                        DeepSkyBlue3_32 = 32,
                        DodgerBlue1 = 33,
                        Green3_34 = 34,
                        SpringGreen3_35 = 35,
                        DarkCyan_36 = 36,
                        LightSeaGreen = 37,
                        DeepSkyBlue2 = 38,
                        DeepSkyBlue1 = 39,
                        Green3_40 = 40,
                        SpringGreen3_41 = 41,
                        SpringGreen2_42 = 42,
                        Cyan3 = 43,
                        DarkTurquoise = 44,
                        Turquoise2 = 45,
                        Green1 = 46,
                        SpringGreen2_47 = 47,
                        SpringGreen1 = 48,
                        MediumSpringGreen = 49,
                        Cyan2 = 50,
                        Cyan1 = 51,
                        DarkRed_52 = 52,
                        DeepPink4_53 = 53,
                        Purple4_54 = 54,
                        Purple4_55 = 55,
                        Purple3 = 56,
                        BlueViolet = 57,
                        Orange4_58 = 58,
                        Grey37 = 59,
                        MediumPurple4 = 60,
                        SlateBlue3_61 = 61,
                        SlateBlue3_62 = 62,
                        RoyalBlue1 = 63,
                        Chartreuse4 = 64,
                        DarkSeaGreen4_65 = 65,
                        PaleTurquoise4 = 66,
                        SteelBlue = 67,
                        SteelBlue3 = 68,
                        CornflowerBlue = 69,
                        Chartreuse3_70 = 70,
                        DarkSeaGreen4_71 = 71,
                        CadetBlue_72 = 72,
                        CadetBlue_73 = 73,
                        SkyBlue3 = 74,
                        SteelBlue1_75 = 75,
                        Chartreuse3_76 = 76,
                        PaleGreen3_77 = 77,
                        SeaGreen3 = 78,
                        Aquamarine3 = 79,
                        MediumTurquoise = 80,
                        SteelBlue1_81 = 81,
                        Chartreuse2_82 = 82,
                        SeaGreen2 = 83,
                        SeaGreen1_84 = 84,
                        SeaGreen1_85 = 85,
                        Aquamarine1_86 = 86,
                        DarkSlateGray2 = 87,
                        DarkRed_88 = 88,
                        DeepPink4_89 = 89,
                        DarkMagenta_90 = 90,
                        DarkMagenta_91 = 91,
                        DarkViolet_92 = 92,
                        Purple_93 = 93,
                        Orange4_94 = 94,
                        LightPink4 = 95,
                        Plum4 = 96,
                        MediumPurple3_97 = 97,
                        MediumPurple3_98 = 98,
                        SlateBlue1 = 99,
                        Yellow4_100 = 100,
                        Wheat4 = 101,
                        Grey53 = 102,
                        LightSlateGrey = 103,
                        MediumPurple = 104,
                        LightSlateBlue = 105,
                        Yellow4_106 = 106,
                        DarkOliveGreen3_107 = 107,
                        DarkSeaGreen = 108,
                        LightSkyBlue3_109 = 109,
                        LightSkyBlue3_110 = 110,
                        SkyBlue2 = 111,
                        Chartreuse2_112 = 112,
                        DarkOliveGreen3_113 = 113,
                        PaleGreen3_114 = 114,
                        DarkSeaGreen3_115 = 115,
                        DarkSlateGray3 = 116,
                        SkyBlue1 = 117,
                        Chartreuse1 = 118,
                        LightGreen_119 = 119,
                        LightGreen_120 = 120,
                        PaleGreen1_121 = 121,
                        Aquamarine1_122 = 122,
                        DarkSlateGray1 = 123,
                        Red3_124 = 124,
                        DeepPink4_125 = 125,
                        MediumVioletRed = 126,
                        Magenta3_127 = 127,
                        DarkViolet_128 = 128,
                        Purple_129 = 129,
                        DarkOrange3_130 = 130,
                        IndianRed_131 = 131,
                        HotPink3_132 = 132,
                        MediumOrchid3 = 133,
                        MediumOrchid = 134,
                        MediumPurple2_135 = 135,
                        DarkGoldenrod = 136,
                        LightSalmon3_137 = 137,
                        RosyBrown = 138,
                        Grey63 = 139,
                        MediumPurple2_140 = 140,
                        MediumPurple1 = 141,
                        Gold3_142 = 142,
                        DarkKhaki = 143,
                        NavajoWhite3 = 144,
                        Grey69 = 145,
                        LightSteelBlue3 = 146,
                        LightSteelBlue = 147,
                        Yellow3_148 = 148,
                        DarkOliveGreen3_149 = 149,
                        DarkSeaGreen3_150 = 150,
                        DarkSeaGreen2_151 = 151,
                        LightCyan3 = 152,
                        LightSkyBlue1 = 153,
                        GreenYellow = 154,
                        DarkOliveGreen2 = 155,
                        PaleGreen1_156 = 156,
                        DarkSeaGreen2_157 = 157,
                        DarkSeaGreen1_158 = 158,
                        PaleTurquoise1 = 159,
                        Red3_160 = 160,
                        DeepPink3_161 = 161,
                        DeepPink3_162 = 162,
                        Magenta3_163 = 163,
                        Magenta3_164 = 164,
                        Magenta2_165 = 165,
                        DarkOrange3_166 = 166,
                        IndianRed_167 = 167,
                        HotPink3_168 = 168,
                        HotPink2 = 169,
                        Orchid = 170,
                        MediumOrchid1_171 = 171,
                        Orange3 = 172,
                        LightSalmon3_173 = 173,
                        LightPink3 = 174,
                        Pink3 = 175,
                        Plum3 = 176,
                        Violet = 177,
                        Gold3_178 = 178,
                        LightGoldenrod3 = 179,
                        Tan = 180,
                        MistyRose3 = 181,
                        Thistle3 = 182,
                        Plum2 = 183,
                        Yellow3_184 = 184,
                        Khaki3 = 185,
                        LightGoldenrod2_186 = 186,
                        LightYellow3 = 187,
                        Grey84 = 188,
                        LightSteelBlue1 = 189,
                        Yellow2 = 190,
                        DarkOliveGreen1_191 = 191,
                        DarkOliveGreen1_192 = 192,
                        DarkSeaGreen1_193 = 193,
                        Honeydew2 = 194,
                        LightCyan1 = 195,
                        Red1 = 196,
                        DeepPink2 = 197,
                        DeepPink1_198 = 198,
                        DeepPink1_199 = 199,
                        Magenta2_200 = 200,
                        Magenta1 = 201,
                        OrangeRed1 = 202,
                        IndianRed1_203 = 203,
                        IndianRed1_204 = 204,
                        HotPink_205 = 205,
                        HotPink_206 = 206,
                        MediumOrchid1_207 = 207,
                        DarkOrange = 208,
                        Salmon1 = 209,
                        LightCoral = 210,
                        PaleVioletRed1 = 211,
                        Orchid2 = 212,
                        Orchid1 = 213,
                        Orange1 = 214,
                        SandyBrown = 215,
                        LightSalmon1 = 216,
                        LightPink1 = 217,
                        Pink1 = 218,
                        Plum1 = 219,
                        Gold1 = 220,
                        LightGoldenrod2_221 = 221,
                        LightGoldenrod2_222 = 222,
                        NavajoWhite1 = 223,
                        MistyRose1 = 224,
                        Thistle1 = 225,
                        Yellow1 = 226,
                        LightGoldenrod1 = 227,
                        Khaki1 = 228,
                        Wheat1 = 229,
                        Cornsilk1 = 230,
                        Grey100 = 231,

                        // -- Grayscale ramp (232-255) --
                        Grey3 = 232,
                        Grey7 = 233,
                        Grey11 = 234,
                        Grey15 = 235,
                        Grey19 = 236,
                        Grey23 = 237,
                        Grey27 = 238,
                        Grey30 = 239,
                        Grey35 = 240,
                        Grey39 = 241,
                        Grey42 = 242,
                        Grey46 = 243,
                        Grey50 = 244,
                        Grey54 = 245,
                        Grey58 = 246,
                        Grey62 = 247,
                        Grey66 = 248,
                        Grey70 = 249,
                        Grey74 = 250,
                        Grey78 = 251,
                        Grey82 = 252,
                        Grey85 = 253,
                        Grey89 = 254,
                        Grey93 = 255
                };

                /**
                 * @brief Returns the RGB color for a 256-color palette entry.
                 * @param index Palette index (0-255).
                 * @return The RGB color, or an invalid Color if out of range.
                 */
                static Color ansiColor(int index);

                /**
                 * @brief Returns the RGB color for an AnsiColor palette entry.
                 * @param color The palette entry.
                 * @return The RGB color.
                 */
                static Color ansiColor(AnsiColor color) { return ansiColor(static_cast<int>(color)); }

                /**
                 * @brief Finds the closest ANSI palette entry for an RGB color.
                 *
                 * Uses redmean-weighted Euclidean distance for perceptual
                 * accuracy.  Chromatic inputs are biased toward colored
                 * palette entries over grays.
                 *
                 * @param color The RGB color to match.
                 * @param maxIndex Maximum palette index to search
                 *        (15 for 16-color, 255 for 256-color).
                 * @return The closest palette entry.
                 */
                static AnsiColor findClosestAnsiColor(const Color &color, int maxIndex = 255);

                // ---------------------------------------------------------------
                // Static escape-sequence builders.
                //
                // These produce the raw escape bytes as a String without needing
                // an IODevice, so code paths that compose styled text into a
                // buffer (the Logger console formatter, TUI cell flushing, help
                // text colorizers) can share the single source of truth instead
                // of hand-rolling "\033[...m" literals.  The instance methods
                // below are thin wrappers that write() these sequences.
                // ---------------------------------------------------------------

                /**
                 * @brief Returns the full-reset sequence (SGR 0).
                 * @return The escape sequence String.
                 */
                static String resetSeq();

                /**
                 * @brief Returns the SGR sequence for a single text style.
                 * @param style The text style.
                 * @param enable True to enable the style, false to emit its
                 *        corresponding off code (e.g. Bold/Dim share 22).
                 * @return The escape sequence String.
                 */
                static String styleSeq(TextStyle style, bool enable = true);

                /** @brief Returns the foreground SGR sequence for a palette color. */
                static String foregroundSeq(AnsiColor color);
                /** @brief Returns the background SGR sequence for a palette color. */
                static String backgroundSeq(AnsiColor color);
                /** @brief Returns the foreground SGR sequence for a 256-color index. */
                static String foreground256Seq(uint8_t index);
                /** @brief Returns the background SGR sequence for a 256-color index. */
                static String background256Seq(uint8_t index);
                /** @brief Returns the foreground SGR sequence for a 24-bit RGB color. */
                static String foregroundRGBSeq(uint8_t r, uint8_t g, uint8_t b);
                /** @brief Returns the background SGR sequence for a 24-bit RGB color. */
                static String backgroundRGBSeq(uint8_t r, uint8_t g, uint8_t b);

                /**
                 * @brief Returns the foreground sequence for an RGB color at a
                 *        given color-support level.
                 *
                 * Emits 24-bit truecolor, a nearest 256-cube match, a nearest
                 * 16-color match, or a grayscale approximation, degrading to an
                 * empty String for Terminal::NoColor.  This is the single place
                 * the library decides how to render an arbitrary RGB color for a
                 * terminal's capability.
                 *
                 * @param color The RGB color.
                 * @param support The terminal color-support level.
                 * @return The escape sequence String (empty for NoColor).
                 */
                static String foregroundSeq(const Color &color, Terminal::ColorSupport support);
                /** @brief Background counterpart to foregroundSeq(const Color&, Terminal::ColorSupport). */
                static String backgroundSeq(const Color &color, Terminal::ColorSupport support);

                /**
                 * @brief Returns an OSC 8 hyperlink sequence wrapping @p text.
                 * @param url The target URI.
                 * @param text The visible, clickable text.
                 * @return The escape sequence String.
                 */
                static String hyperlinkSeq(const String &url, const String &text);

                /**
                 * @brief Returns the window size of the current STDOUT device.
                 * @param[out] rows Number of rows in window.
                 * @param[out] cols Number of columns in window.
                 * @return True if the window size was retrieved successfully.
                 */
                static bool stdoutWindowSize(int &rows, int &cols);

                /**
                 * @brief Returns true if the current STDOUT can support ANSI signaling.
                 * @return True if STDOUT supports ANSI escape sequences.
                 */
                static bool stdoutSupportsANSI();

                /**
                 * @brief Returns true if the current STDERR can support ANSI signaling.
                 * @return True if STDERR supports ANSI escape sequences.
                 */
                static bool stderrSupportsANSI();

                /**
                 * @brief Constructs an AnsiStream writing to the given device.
                 * @param device The IODevice to write to. Must be open for writing.
                 */
                AnsiStream(IODevice *device) : _device(device), _enabled(true) {}

                /**
                 * @brief Sets the ANSI output enabled.
                 * If not enabled, no ANSI codes will be output but non-ANSI
                 * content will pass-thru.
                 * @param val True to enable ANSI output, false to disable.
                 */
                void setAnsiEnabled(bool val) {
                        _enabled = val;
                        return;
                }

                /**
                 * @brief Returns the underlying IODevice.
                 * @return Pointer to the IODevice this stream writes to.
                 */
                IODevice *device() const { return _device; }

                /**
                 * @brief Writes raw text to the underlying device.
                 * @param text The text to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(const String &text);

                /**
                 * @brief Writes a C string to the underlying device.
                 * @param text The null-terminated string to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(const char *text);

                /**
                 * @brief Writes a single character to the underlying device.
                 * @param ch The character to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(char ch);

                /**
                 * @brief Writes an integer to the underlying device.
                 * @param val The integer to write (formatted as decimal).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(int val);

                /**
                 * @brief Writes a 64-bit signed integer (formatted as decimal).
                 * @param val The value to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(int64_t val);

                /**
                 * @brief Writes a 64-bit unsigned integer (formatted as decimal).
                 *
                 * Also covers @c size_t on LP64 platforms.
                 *
                 * @param val The value to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(uint64_t val);

                /**
                 * @brief Writes a floating-point value (shortest round-trip form).
                 * @param val The value to write.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &write(double val);

                /**
                 * @brief Flushes the underlying device.
                 */
                void flush();

                /** @brief Writes a String via operator<<. */
                AnsiStream &operator<<(const String &text) { return write(text); }
                /** @brief Writes a C string via operator<<. */
                AnsiStream &operator<<(const char *text) { return write(text); }
                /** @brief Writes a single character via operator<<. */
                AnsiStream &operator<<(char ch) { return write(ch); }
                /** @brief Writes an integer via operator<<. */
                AnsiStream &operator<<(int val) { return write(val); }
                /** @brief Writes a 64-bit signed integer via operator<<. */
                AnsiStream &operator<<(int64_t val) { return write(val); }
                /** @brief Writes a 64-bit unsigned integer via operator<<. */
                AnsiStream &operator<<(uint64_t val) { return write(val); }
                /** @brief Writes a floating-point value via operator<<. */
                AnsiStream &operator<<(double val) { return write(val); }

                /**
                 * @brief Sets the foreground to an ANSI palette color.
                 *
                 * For indices 0-7, emits the standard SGR code (30-37).
                 * For indices 8-15, emits the bright SGR code (90-97).
                 * For indices 16-255, emits a 256-color sequence.
                 *
                 * @param color The palette index (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForeground(AnsiColor color);

                /**
                 * @brief Sets the background to an ANSI palette color.
                 *
                 * For indices 0-7, emits the standard SGR code (40-47).
                 * For indices 8-15, emits the bright SGR code (100-107).
                 * For indices 16-255, emits a 256-color sequence.
                 *
                 * @param color The palette index (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackground(AnsiColor color);

                /**
                 * @brief Sets the foreground to the closest ANSI palette match.
                 * @param color The RGB color to match.
                 * @param maxIndex Maximum palette index (15 for 16-color, 255 for 256-color).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForeground(const Color &color, int maxIndex);

                /**
                 * @brief Sets the background to the closest ANSI palette match.
                 * @param color The RGB color to match.
                 * @param maxIndex Maximum palette index (15 for 16-color, 255 for 256-color).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackground(const Color &color, int maxIndex);

                /**
                 * @brief Sets the foreground to an RGB color, degrading to the
                 *        terminal's color capability.
                 *
                 * Emits truecolor, 256-color, 16-color, or grayscale as
                 * appropriate for @p support.  Prefer this over manually
                 * choosing setForegroundRGB / setForeground256 / setForeground —
                 * it keeps the capability decision in one place.
                 *
                 * @param color The RGB color.
                 * @param support The terminal color-support level.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForeground(const Color &color, Terminal::ColorSupport support);

                /** @brief Background counterpart to setForeground(const Color&, Terminal::ColorSupport). */
                AnsiStream &setBackground(const Color &color, Terminal::ColorSupport support);

                /**
                 * @brief Sets the foreground to an RGB color using the
                 *        auto-detected terminal color capability.
                 * @param color The RGB color.
                 * @return Reference to this stream for chaining.
                 * @see Terminal::colorSupport()
                 */
                AnsiStream &setForeground(const Color &color) {
                        return setForeground(color, Terminal::colorSupport());
                }

                /**
                 * @brief Sets the background to an RGB color using the
                 *        auto-detected terminal color capability.
                 * @param color The RGB color.
                 * @return Reference to this stream for chaining.
                 * @see Terminal::colorSupport()
                 */
                AnsiStream &setBackground(const Color &color) {
                        return setBackground(color, Terminal::colorSupport());
                }

                /**
                 * @brief Enables or disables a single text style.
                 * @param style The text style to change.
                 * @param enable True to enable, false to emit the matching off
                 *        code (Bold/Dim share off code 22).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setStyle(TextStyle style, bool enable = true) {
                        if (!_enabled) return *this;
                        return write(styleSeq(style, enable));
                }

                /**
                 * @brief Moves the cursor up N rows.
                 * @param[in] n Number of rows to move up.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorUp(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "A";
                        return *this;
                }

                /**
                 * @brief Moves the cursor down N rows.
                 * @param[in] n Number of rows to move down.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorDown(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "B";
                        return *this;
                }

                /**
                 * @brief Moves the cursor right N columns.
                 * @param[in] n Number of columns to move right.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorRight(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "C";
                        return *this;
                }

                /**
                 * @brief Moves the cursor left N columns.
                 * @param[in] n Number of columns to move left.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorLeft(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "D";
                        return *this;
                }

                /**
                 * @brief Moves the cursor down N rows to the start of that line (CNL).
                 * @param[in] n Number of rows to move down.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorNextLine(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "E";
                        return *this;
                }

                /**
                 * @brief Moves the cursor up N rows to the start of that line (CPL).
                 * @param[in] n Number of rows to move up.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorPrevLine(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "F";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to an absolute column on the current row (CHA).
                 * @param[in] c Column (1-based).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setCursorColumn(int c) {
                        if (!_enabled) return *this;
                        *this << "\033[" << c << "G";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the home position (row 1, column 1).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &cursorHome() {
                        if (!_enabled) return *this;
                        *this << "\033[H";
                        return *this;
                }

                /**
                 * @brief Sets the cursor shape (DECSCUSR).
                 * @param[in] style The cursor shape.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setCursorStyle(CursorStyle style) {
                        if (!_enabled) return *this;
                        *this << "\033[" << static_cast<int>(style) << " q";
                        return *this;
                }

                /**
                 * @brief Sets the absolute cursor position.
                 * @param[in] r Row to move cursor.
                 * @param[in] c Column to move cursor.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setCursorPosition(int r, int c) {
                        if (!_enabled) return *this;
                        *this << "\033[" << r << ";" << c << "H";
                        return *this;
                }

                /**
                 * @brief Clears the screen.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearScreen() {
                        if (!_enabled) return *this;
                        *this << "\033[2J";
                        return *this;
                }

                /**
                 * @brief Clears the entire screen and homes the cursor.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearScreenAndHome() {
                        if (!_enabled) return *this;
                        *this << "\033[2J\033[H";
                        return *this;
                }

                /**
                 * @brief Clears from the cursor to the start of the screen.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearScreenBeforeCursor() {
                        if (!_enabled) return *this;
                        *this << "\033[1J";
                        return *this;
                }

                /**
                 * @brief Clears from the cursor to the end of the screen.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearScreenAfterCursor() {
                        if (!_enabled) return *this;
                        *this << "\033[0J";
                        return *this;
                }

                /**
                 * @brief Clears the scrollback buffer (xterm extension).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearScrollback() {
                        if (!_enabled) return *this;
                        *this << "\033[3J";
                        return *this;
                }

                /**
                 * @brief Inserts N blank lines at the cursor, scrolling lines down.
                 * @param[in] n Number of lines to insert.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &insertLines(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "L";
                        return *this;
                }

                /**
                 * @brief Deletes N lines at the cursor, scrolling lines up.
                 * @param[in] n Number of lines to delete.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &deleteLines(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "M";
                        return *this;
                }

                /**
                 * @brief Inserts N blank characters at the cursor, shifting the rest right.
                 * @param[in] n Number of characters to insert.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &insertCharacters(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "@";
                        return *this;
                }

                /**
                 * @brief Deletes N characters at the cursor, shifting the rest left.
                 * @param[in] n Number of characters to delete.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &deleteCharacters(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "P";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the start of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &moveToStartOfLine() {
                        if (!_enabled) return *this;
                        *this << "\033[0G";
                        return *this;
                }

                /**
                 * @brief Moves the cursor to the end of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &moveToEndOfLine() {
                        if (!_enabled) return *this;
                        *this << "\033[999G";
                        return *this;
                }

                /**
                 * @brief Clears the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearLine() {
                        if (!_enabled) return *this;
                        *this << "\033[2K";
                        return *this;
                }

                /**
                 * @brief Clears between the cursor and the start of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearLineBeforeCursor() {
                        if (!_enabled) return *this;
                        *this << "\033[1K";
                        return *this;
                }

                /**
                 * @brief Clears between the cursor and the end of the current line.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &clearLineAfterCursor() {
                        if (!_enabled) return *this;
                        *this << "\033[0K";
                        return *this;
                }

                /**
                 * @brief Resets the terminal to default configuration.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &reset() {
                        if (!_enabled) return *this;
                        *this << "\033[0m";
                        return *this;
                }

                /**
                 * @brief Resets the foreground to the terminal default.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &resetForeground() {
                        if (!_enabled) return *this;
                        *this << "\033[39m";
                        return *this;
                }

                /**
                 * @brief Resets the background to the terminal default.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &resetBackground() {
                        if (!_enabled) return *this;
                        *this << "\033[49m";
                        return *this;
                }

                /**
                 * @brief Makes the cursor visible.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &showCursor() {
                        if (!_enabled) return *this;
                        *this << "\033[?25h";
                        return *this;
                }

                /**
                 * @brief Makes the cursor invisible.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &hideCursor() {
                        if (!_enabled) return *this;
                        *this << "\033[?25l";
                        return *this;
                }

                /**
                 * @brief Saves the cursor position for later recall.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &saveCursorPosition() {
                        if (!_enabled) return *this;
                        *this << "\033[s";
                        return *this;
                }

                /**
                 * @brief Recalls a saved cursor position.
                 * @see saveCursorPosition()
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &restoreCursorPosition() {
                        if (!_enabled) return *this;
                        *this << "\033[u";
                        return *this;
                }

                /**
                 * @brief Enables a region of the screen to scroll.
                 * @param[in] startRow Row where scrolling should start.
                 * @param[in] endRow Row where scrolling should end.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &enableScrollingRegion(int startRow, int endRow) {
                        if (!_enabled) return *this;
                        *this << "\033[" << startRow << ";" << endRow << "r";
                        return *this;
                }

                /**
                 * @brief Resets the scrolling region to the full screen.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &resetScrollingRegion() {
                        if (!_enabled) return *this;
                        *this << "\033[r";
                        return *this;
                }

                /**
                 * @brief Causes the scrolling region to scroll up N rows.
                 * @param[in] n Number of rows to scroll.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &scrollUp(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "S";
                        return *this;
                }

                /**
                 * @brief Causes the scrolling region to scroll down N rows.
                 * @param[in] n Number of rows to scroll.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &scrollDown(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "T";
                        return *this;
                }

                /**
                 * @brief Erases N characters at cursor.
                 * @param n Number of characters to erase.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &eraseCharacters(int n) {
                        if (!_enabled) return *this;
                        *this << "\033[" << n << "X";
                        return *this;
                }

                /**
                 * @brief Sets the foreground to a 256-color palette index.
                 *
                 * Always uses the extended 256-color escape sequence format
                 * (\\033[38;5;Nm) regardless of index.  Use setForeground()
                 * for automatic selection of the most compatible format.
                 *
                 * @param index Color index (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForeground256(uint8_t index) {
                        if (!_enabled) return *this;
                        return write(foreground256Seq(index));
                }

                /**
                 * @brief Sets the background to a 256-color palette index.
                 *
                 * Always uses the extended 256-color escape sequence format
                 * (\\033[48;5;Nm) regardless of index.  Use setBackground()
                 * for automatic selection of the most compatible format.
                 *
                 * @param index Color index (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackground256(uint8_t index) {
                        if (!_enabled) return *this;
                        return write(background256Seq(index));
                }

                /**
                 * @brief Sets the foreground to a 24-bit RGB color.
                 * @param r Red component (0-255).
                 * @param g Green component (0-255).
                 * @param b Blue component (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setForegroundRGB(uint8_t r, uint8_t g, uint8_t b) {
                        if (!_enabled) return *this;
                        return write(foregroundRGBSeq(r, g, b));
                }

                /**
                 * @brief Sets the background to a 24-bit RGB color.
                 * @param r Red component (0-255).
                 * @param g Green component (0-255).
                 * @param b Blue component (0-255).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setBackgroundRGB(uint8_t r, uint8_t g, uint8_t b) {
                        if (!_enabled) return *this;
                        return write(backgroundRGBSeq(r, g, b));
                }

                /**
                 * @brief Writes a clickable hyperlink (OSC 8).
                 *
                 * Terminals that don't understand OSC 8 render @p text as plain
                 * text and ignore the link, so this is always safe to emit.
                 *
                 * @param[in] url The target URI.
                 * @param[in] text The visible, clickable text.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &hyperlink(const String &url, const String &text);

                /**
                 * @brief Sets the terminal window / tab title (OSC 2).
                 * @param[in] title The title text.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &setWindowTitle(const String &title);

                /**
                 * @brief Copies text to the system clipboard (OSC 52).
                 *
                 * Requires terminal support (often disabled by default for
                 * security).  The payload is base64-encoded per the protocol.
                 *
                 * @param[in] text The text to place on the clipboard.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &copyToClipboard(const String &text);

                /**
                 * @brief Enables SGR mouse tracking at the given detail level.
                 *
                 * Always enables the SGR (1006) coordinate encoding so the
                 * input parser receives the unambiguous extended form.
                 *
                 * @param[in] mode Which motion events to report.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &enableMouseTracking(MouseTracking mode = MouseDrag) {
                        if (!_enabled) return *this;
                        *this << "\033[?1000h";
                        if (mode == MouseDrag) *this << "\033[?1002h";
                        if (mode == MouseAny) *this << "\033[?1003h";
                        *this << "\033[?1006h";
                        return *this;
                }

                /**
                 * @brief Disables SGR mouse tracking.
                 *
                 * Emits the off codes for every motion mode, so it cleanly
                 * undoes any level passed to enableMouseTracking().
                 *
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &disableMouseTracking() {
                        if (!_enabled) return *this;
                        *this << "\033[?1006l\033[?1003l\033[?1002l\033[?1000l";
                        return *this;
                }

                /**
                 * @brief Begins a synchronized output frame (mode 2026).
                 *
                 * Terminals that support synchronized output buffer everything
                 * between begin and end and present it atomically, eliminating
                 * tearing / flicker during a full-screen redraw.  Terminals
                 * that don't support it ignore the sequence, so it is always
                 * safe to emit.  Pair with endSynchronizedUpdate().
                 *
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &beginSynchronizedUpdate() {
                        if (!_enabled) return *this;
                        *this << "\033[?2026h";
                        return *this;
                }

                /**
                 * @brief Ends a synchronized output frame (mode 2026).
                 * @return Reference to this stream for chaining.
                 * @see beginSynchronizedUpdate()
                 */
                AnsiStream &endSynchronizedUpdate() {
                        if (!_enabled) return *this;
                        *this << "\033[?2026l";
                        return *this;
                }

                /**
                 * @brief Enables bracketed paste mode (mode 2004).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &enableBracketedPaste() {
                        if (!_enabled) return *this;
                        *this << "\033[?2004h";
                        return *this;
                }

                /**
                 * @brief Disables bracketed paste mode.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &disableBracketedPaste() {
                        if (!_enabled) return *this;
                        *this << "\033[?2004l";
                        return *this;
                }

                /**
                 * @brief Enables focus in/out reporting (mode 1004).
                 *
                 * The terminal then emits @c \\033[I when the window gains focus
                 * and @c \\033[O when it loses focus.  The input parser decodes
                 * these into focus events; without this, those bytes are never
                 * sent.
                 *
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &enableFocusReporting() {
                        if (!_enabled) return *this;
                        *this << "\033[?1004h";
                        return *this;
                }

                /**
                 * @brief Disables focus in/out reporting (mode 1004).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &disableFocusReporting() {
                        if (!_enabled) return *this;
                        *this << "\033[?1004l";
                        return *this;
                }

                /**
                 * @brief Rings the terminal bell (BEL).
                 *
                 * Emitted regardless of the ANSI-enabled flag, since the bell
                 * is a control character rather than an escape sequence.
                 *
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &bell() {
                        *this << '\a';
                        return *this;
                }

                /**
                 * @brief Performs a soft terminal reset (DECSTR).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &softReset() {
                        if (!_enabled) return *this;
                        *this << "\033[!p";
                        return *this;
                }

                /**
                 * @brief Performs a full terminal reset (RIS).
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &hardReset() {
                        if (!_enabled) return *this;
                        *this << "\033c";
                        return *this;
                }

                /**
                 * @brief Terminal should switch to an alternate buffer.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &useAlternateScreenBuffer() {
                        if (!_enabled) return *this;
                        *this << "\033[?1049h";
                        return *this;
                }

                /**
                 * @brief Terminal should switch to main screen buffer.
                 * @return Reference to this stream for chaining.
                 */
                AnsiStream &useMainScreenBuffer() {
                        if (!_enabled) return *this;
                        *this << "\033[?1049l";
                        return *this;
                }

                /**
                 * @brief Requests the current cursor position from the terminal.
                 * @param[in] input Input IODevice for the terminal (e.g. stdinDevice())
                 * @param[out] row Cursor Row
                 * @param[out] col Cursor Column
                 * @return True if successful.
                 */
                bool getCursorPosition(IODevice *input, int &row, int &col);

                /**
                 * @brief Scoped style guard that resets the stream on destruction.
                 *
                 * Emits AnsiStream::reset() when it goes out of scope, so a block
                 * that sets colors / styles can't leak them into later output
                 * (the classic "the shell prompt is now red" bug).
                 *
                 * @code
                 * {
                 *     AnsiStream::StyleScope guard(stream);
                 *     stream.setForeground(AnsiStream::Red).setStyle(AnsiStream::Bold);
                 *     stream << "error!";
                 * } // reset() emitted here
                 * @endcode
                 */
                class StyleScope {
                        public:
                                /** @brief Captures the stream to reset on destruction. */
                                explicit StyleScope(AnsiStream &stream) : _stream(stream) {}
                                /** @brief Emits reset() on the captured stream. */
                                ~StyleScope() { _stream.reset(); }
                                StyleScope(const StyleScope &) = delete;
                                StyleScope &operator=(const StyleScope &) = delete;

                        private:
                                AnsiStream &_stream;
                };

        private:
                IODevice *_device;
                bool      _enabled;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
