/**
 * @file      hexdump.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Fluent builder for canonical hex dumps of raw byte ranges.
 * @ingroup util
 *
 * Produces the classic "address: hex bytes  ASCII" layout familiar from
 * @c hexdump / @c xxd, with every column independently configurable:
 *
 * @code
 * String out = HexDump()
 *                  .setBaseAddress(0x0123ABCD)
 *                  .setBytesPerLine(8)
 *                  .build(payload);
 * // 0123ABCD: 01 02 03 04 AA BB CC DD  ........
 * @endcode
 *
 * Every setter returns @c *this so calls chain, and the configuration is
 * reusable — build the dumper once, then @ref build many ranges with it.
 * The defaults (16 bytes per line, 8-digit address, ASCII gutter, upper-
 * case hex) match the typical tooling output; tune from there.
 *
 * The address column is purely cosmetic: @ref setBaseAddress sets the
 * value printed for the first byte and it advances by the line width for
 * each subsequent row, so a dump can be labelled with the data's real
 * memory / file offset regardless of where the bytes actually live.
 */
class HexDump {
        public:
                /** @brief Constructs a dumper with the default layout. */
                HexDump() = default;

                /**
                 * @brief Sets how many bytes are shown per line.
                 *
                 * Clamped to a minimum of 1.  Default is 16.
                 */
                HexDump &setBytesPerLine(size_t n) {
                        _bytesPerLine = n < 1 ? 1 : n;
                        return *this;
                }

                /**
                 * @brief Enables or disables the leading address column.
                 *
                 * When enabled (the default) each line is prefixed with the
                 * running address followed by @ref setAddressSuffix.
                 */
                HexDump &setShowAddress(bool on) {
                        _showAddress = on;
                        return *this;
                }

                /**
                 * @brief Sets the address printed for the first byte.
                 *
                 * The value advances by @ref setBytesPerLine for each row.
                 * Default is 0.
                 */
                HexDump &setBaseAddress(uint64_t addr) {
                        _baseAddress = addr;
                        return *this;
                }

                /**
                 * @brief Sets the zero-padded width of the address column.
                 *
                 * Clamped to a minimum of 1.  Default is 8 (e.g. @c 0123ABCD).
                 */
                HexDump &setAddressDigits(int digits) {
                        _addressDigits = digits < 1 ? 1 : digits;
                        return *this;
                }

                /**
                 * @brief Sets the text printed after the address.
                 *
                 * Default is @c ": ".
                 */
                HexDump &setAddressSuffix(const String &suffix) {
                        _addressSuffix = suffix;
                        return *this;
                }

                /**
                 * @brief Enables or disables the trailing ASCII gutter.
                 *
                 * When enabled (the default) each line ends with the byte
                 * values rendered as ASCII, with any non-printable byte shown
                 * as @ref setNonPrintable.  Short final lines pad the hex
                 * column so the ASCII gutter stays aligned.
                 */
                HexDump &setShowAscii(bool on) {
                        _showAscii = on;
                        return *this;
                }

                /**
                 * @brief Sets the character substituted for non-printable bytes
                 *        in the ASCII gutter.
                 *
                 * Default is @c '.'.
                 */
                HexDump &setNonPrintable(char c) {
                        _nonPrintable = c;
                        return *this;
                }

                /**
                 * @brief Selects upper- or lower-case hex digits.
                 *
                 * Applies to both the address and the byte columns.  Default
                 * is upper-case.
                 */
                HexDump &setUppercase(bool on) {
                        _uppercase = on;
                        return *this;
                }

                /**
                 * @brief Sets a fixed prefix prepended to every line.
                 *
                 * Useful for indenting a dump under a heading.  Default is
                 * empty.
                 */
                HexDump &setIndent(const String &indent) {
                        _indent = indent;
                        return *this;
                }

                /**
                 * @brief Renders @p len bytes from @p data as a multi-line dump.
                 *
                 * Each line (including the last) is terminated with a newline.
                 * An empty range yields an empty string.
                 */
                String build(const void *data, size_t len) const;

                /** @brief Convenience overload dumping the contents of a @ref Buffer. */
                String build(const Buffer &buf) const {
                        return build(buf.data(), buf.size());
                }

        private:
                size_t   _bytesPerLine = 16;
                uint64_t _baseAddress = 0;
                int      _addressDigits = 8;
                String   _addressSuffix = ": ";
                String   _indent;
                char     _nonPrintable = '.';
                bool     _showAddress = true;
                bool     _showAscii = true;
                bool     _uppercase = true;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
