/**
 * @file      core/textstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdio>
#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/variant.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;
class Buffer;

/**
 * @brief Formatted text I/O with encoding awareness.
 *
 * TextStream provides a Qt-style interface for reading and writing
 * formatted text. It is used for human-readable output, config files,
 * log formatting, and structured text parsing.
 *
 * TextStream does not inherit from std::ostream. All operator<< and
 * operator>> overloads are provided explicitly. This separation allows
 * encoding-aware text handling and promeki-native formatting controls
 * without the complexity of std::locale.
 *
 * TextStream can be constructed from:
 * - An IODevice* (used directly, not owned)
 * - A Buffer* (wrapped in an internal BufferIODevice)
 * - A String* (wrapped in an internal StringIODevice)
 * - A FILE* (wrapped in an internal FileIODevice)
 *
 * All constructors funnel through a single IODevice code path
 * internally.
 *
 * @par Encoding
 * The default encoding is UTF-8. When a different encoding is set,
 * text is converted on read/write. Currently supported: "UTF-8" and
 * "Latin-1".
 *
 * @par Formatting
 * Integer base, field width, alignment, pad character, and
 * floating-point precision/notation are all configurable. Manipulator
 * functions (endl, flush, hex, dec, etc.) provide a convenient
 * chaining syntax.
 */
class TextStream {
        public:
                /** @brief Stream status codes. */
                enum Status {
                        Ok,          ///< @brief No error.
                        ReadPastEnd, ///< @brief Attempted to read beyond available data.
                        WriteFailed  ///< @brief A write operation failed.
                };

                /** @brief Field alignment for padded output. */
                enum FieldAlignment {
                        Left,   ///< @brief Left-aligned (pad on right).
                        Right,  ///< @brief Right-aligned (pad on left, default).
                        Center  ///< @brief Center-aligned (pad both sides).
                };

                /** @brief Floating-point notation styles. */
                enum RealNumberNotation {
                        SmartNotation, ///< @brief Use fixed or scientific as appropriate (default).
                        Fixed,         ///< @brief Always use fixed-point notation.
                        Scientific     ///< @brief Always use scientific notation.
                };

                /**
                 * @brief Constructs a TextStream on an IODevice.
                 *
                 * The device must already be open. The TextStream does not
                 * take ownership of the device.
                 *
                 * @param device The IODevice to operate on.
                 */
                explicit TextStream(IODevice *device);

                /**
                 * @brief Constructs a TextStream backed by a Buffer.
                 *
                 * An internal BufferIODevice is created and opened for
                 * ReadWrite. The Buffer must outlive the TextStream.
                 *
                 * @param buffer The Buffer to read from / write to.
                 */
                explicit TextStream(Buffer *buffer);

                /**
                 * @brief Constructs a TextStream backed by a String.
                 *
                 * An internal StringIODevice is created and opened for
                 * ReadWrite. The String must outlive the TextStream.
                 *
                 * @param string The String to read from / write to.
                 */
                explicit TextStream(String *string);

                /**
                 * @brief Constructs a TextStream wrapping a C stdio FILE.
                 *
                 * An internal FileIODevice is created with ReadWrite mode.
                 * The FILE must outlive the TextStream. The TextStream does
                 * not take ownership of the FILE (it will not be fclose'd).
                 *
                 * @param file The FILE pointer to wrap.
                 */
                explicit TextStream(FILE *file);

                /** @brief Destructor. */
                ~TextStream();

                // ============================================================
                // Status
                // ============================================================

                /**
                 * @brief Returns the current stream status.
                 * @return The status code.
                 */
                Status status() const { return _status; }

                /**
                 * @brief Resets the stream status to Ok.
                 */
                void resetStatus() { _status = Ok; }

                /**
                 * @brief Returns true if at the end of the stream.
                 * @return True if no more data can be read.
                 */
                bool atEnd() const;

                /**
                 * @brief Flushes any buffered output to the underlying device.
                 *
                 * @note Currently a no-op. TextStream does not buffer data
                 * internally; writes are forwarded immediately to the device.
                 */
                void flush();

                /**
                 * @brief Returns the underlying IODevice.
                 * @return The device pointer.
                 */
                IODevice *device() const { return _device; }

                // ============================================================
                // Encoding
                // ============================================================

                /**
                 * @brief Sets the text encoding for read/write operations.
                 *
                 * Supported values: "UTF-8" (default), "Latin-1".
                 *
                 * @param encoding The encoding name.
                 */
                void setEncoding(const String &encoding);

                /**
                 * @brief Returns the current encoding name.
                 * @return The encoding string.
                 */
                String encoding() const { return _encoding; }

                // ============================================================
                // Formatting controls
                // ============================================================

                /**
                 * @brief Sets the minimum field width for formatted output.
                 *
                 * When a value's text representation is shorter than the
                 * field width, it is padded according to the current
                 * alignment and pad character.
                 *
                 * @param width The minimum field width.
                 */
                void setFieldWidth(int width) { _fieldWidth = width; }

                /**
                 * @brief Returns the current field width.
                 * @return The field width.
                 */
                int fieldWidth() const { return _fieldWidth; }

                /**
                 * @brief Sets the field alignment.
                 * @param align The alignment mode.
                 */
                void setFieldAlignment(FieldAlignment align) { _fieldAlignment = align; }

                /**
                 * @brief Returns the current field alignment.
                 * @return The alignment mode.
                 */
                FieldAlignment fieldAlignment() const { return _fieldAlignment; }

                /**
                 * @brief Sets the padding character used for field width.
                 * @param c The pad character (default: space).
                 */
                void setPadChar(char c) { _padChar = c; }

                /**
                 * @brief Returns the current pad character.
                 * @return The pad character.
                 */
                char padChar() const { return _padChar; }

                /**
                 * @brief Sets the integer base for formatted output.
                 *
                 * Supported bases: 2, 8, 10 (default), 16.
                 *
                 * @param base The integer base.
                 */
                void setIntegerBase(int base) { _integerBase = base; }

                /**
                 * @brief Returns the current integer base.
                 * @return The integer base.
                 */
                int integerBase() const { return _integerBase; }

                /**
                 * @brief Sets the number of decimal places for float/double output.
                 * @param precision The precision (default: 6).
                 */
                void setRealNumberPrecision(int precision) { _realNumberPrecision = precision; }

                /**
                 * @brief Returns the current real number precision.
                 * @return The precision.
                 */
                int realNumberPrecision() const { return _realNumberPrecision; }

                /**
                 * @brief Sets the floating-point notation style.
                 * @param notation The notation style.
                 */
                void setRealNumberNotation(RealNumberNotation notation) { _realNumberNotation = notation; }

                /**
                 * @brief Returns the current real number notation.
                 * @return The notation style.
                 */
                RealNumberNotation realNumberNotation() const { return _realNumberNotation; }

                // ============================================================
                // Write operators
                // ============================================================

                /** @brief Writes a String. */
                TextStream &operator<<(const String &val);
                /** @brief Writes a C string. */
                TextStream &operator<<(const char *val);
                /** @brief Writes a single character. */
                TextStream &operator<<(char val);
                /** @brief Writes an int as formatted decimal text. */
                TextStream &operator<<(int val);
                /** @brief Writes an unsigned int as formatted decimal text. */
                TextStream &operator<<(unsigned int val);
                /** @brief Writes an int64_t as formatted text. */
                TextStream &operator<<(int64_t val);
                /** @brief Writes a uint64_t as formatted text. */
                TextStream &operator<<(uint64_t val);
                /** @brief Writes a float as formatted text. */
                TextStream &operator<<(float val);
                /** @brief Writes a double as formatted text. */
                TextStream &operator<<(double val);
                /** @brief Writes "true" or "false". */
                TextStream &operator<<(bool val);
                /** @brief Writes a Variant using its toString() representation. */
                TextStream &operator<<(const Variant &val);

                /** @brief Writes a manipulator function. */
                TextStream &operator<<(TextStream &(*manip)(TextStream &));

                // ============================================================
                // Read operators
                // ============================================================

                /** @brief Reads a whitespace-delimited token into a String. */
                TextStream &operator>>(String &val);
                /** @brief Reads a single character. */
                TextStream &operator>>(char &val);
                /** @brief Reads an int from text. */
                TextStream &operator>>(int &val);
                /** @brief Reads an int64_t from text. */
                TextStream &operator>>(int64_t &val);
                /** @brief Reads a double from text. */
                TextStream &operator>>(double &val);

                // ============================================================
                // Read methods
                // ============================================================

                /**
                 * @brief Reads one line of text, consuming the trailing newline.
                 *
                 * The returned string does not include the trailing newline.
                 *
                 * @return The line, or an empty string at end-of-stream.
                 */
                String readLine();

                /**
                 * @brief Reads all remaining text.
                 * @return The remaining text content.
                 */
                String readAll();

                /**
                 * @brief Reads up to maxLength characters.
                 * @param maxLength Maximum number of characters to read.
                 * @return The text read.
                 */
                String read(size_t maxLength);

        private:
                /**
                 * @brief Writes a raw string to the underlying device.
                 * @param str The string to write.
                 */
                void writeString(const String &str);

                /**
                 * @brief Applies field-width padding to a string.
                 * @param str The unpadded string.
                 * @return The padded string.
                 */
                String applyPadding(const String &str);

                /**
                 * @brief Reads a single character from the underlying device.
                 * @param ch Output character.
                 * @return True if a character was read.
                 */
                bool readChar(char &ch);

                /**
                 * @brief Puts back a character by seeking back one byte.
                 *
                 * On sequential devices where seeking is not possible,
                 * the character is silently lost.
                 *
                 * @param ch The character (unused, retained for clarity).
                 */
                void unreadChar(char ch);

                /**
                 * @brief Skips whitespace characters in the input.
                 */
                void skipWhitespace();

                /**
                 * @brief Reads a token (non-whitespace sequence) from the input.
                 * @return The token string.
                 */
                String readToken();

                IODevice                *_device                = nullptr;
                IODevice                *_ownedDevice           = nullptr;

                // Encoding
                String                  _encoding{"UTF-8"};

                // Formatting state
                int                     _fieldWidth             = 0;
                FieldAlignment          _fieldAlignment         = Right;
                char                    _padChar                = ' ';
                int                     _integerBase            = 10;
                int                     _realNumberPrecision    = 6;
                RealNumberNotation      _realNumberNotation     = SmartNotation;

                // Status
                Status                  _status = Ok;
};

// ============================================================================
// Manipulators
// ============================================================================

/** @brief Writes a newline and flushes the stream. */
inline TextStream &endl(TextStream &s) {
        s << '\n';
        s.flush();
        return s;
}

/** @brief Flushes the stream. */
inline TextStream &flush(TextStream &s) {
        s.flush();
        return s;
}

/** @brief Sets integer base to 16 (hexadecimal). */
inline TextStream &hex(TextStream &s) {
        s.setIntegerBase(16);
        return s;
}

/** @brief Sets integer base to 10 (decimal). */
inline TextStream &dec(TextStream &s) {
        s.setIntegerBase(10);
        return s;
}

/** @brief Sets integer base to 8 (octal). */
inline TextStream &oct(TextStream &s) {
        s.setIntegerBase(8);
        return s;
}

/** @brief Sets integer base to 2 (binary). */
inline TextStream &bin(TextStream &s) {
        s.setIntegerBase(2);
        return s;
}

/** @brief Sets real number notation to Fixed. */
inline TextStream &fixed(TextStream &s) {
        s.setRealNumberNotation(TextStream::Fixed);
        return s;
}

/** @brief Sets real number notation to Scientific. */
inline TextStream &scientific(TextStream &s) {
        s.setRealNumberNotation(TextStream::Scientific);
        return s;
}

/** @brief Sets field alignment to Left. */
inline TextStream &left(TextStream &s) {
        s.setFieldAlignment(TextStream::Left);
        return s;
}

/** @brief Sets field alignment to Right. */
inline TextStream &right(TextStream &s) {
        s.setFieldAlignment(TextStream::Right);
        return s;
}

/** @brief Sets field alignment to Center. */
inline TextStream &center(TextStream &s) {
        s.setFieldAlignment(TextStream::Center);
        return s;
}

PROMEKI_NAMESPACE_END
