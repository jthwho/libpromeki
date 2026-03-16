/**
 * @file      core/streamstring.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <streambuf>
#include <ostream>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Stream buffer that collects output into a String, splitting on newlines.
 * @ingroup core_streams
 *
 * StreamString is a std::streambuf subclass that accumulates characters
 * written to its associated std::ostream.  When a newline or EOF is
 * encountered, it invokes the onNewLine callback with the accumulated
 * line.  The callback returns true to clear the line buffer, or false
 * to keep accumulating.
 *
 * Example usage (routing stream output to the logger):
 * @code
 *   StreamString ss([](String &line) {
 *           promekiInfo("%s", line.cstr());
 *           return true;
 *   });
 *   ss.stream() << "Hello " << 42 << std::endl;
 * @endcode
 */
class StreamString : public std::streambuf {
        public:
                /** @brief Callback type invoked for each completed line. */
                using OnNewLineFunc = std::function<bool(String &)>;

                /** @brief Constructs a StreamString with no callback. */
                StreamString() : _stream(this) { }

                /**
                 * @brief Constructs a StreamString with a newline callback.
                 * @param func Callback invoked when a complete line is available.
                 */
                StreamString(OnNewLineFunc func) : _onNewLine(std::move(func)), _stream(this) { }

                /** @brief Deleted copy constructor (stream holds a pointer to this). */
                StreamString(const StreamString &) = delete;
                /** @brief Deleted copy assignment operator. */
                StreamString &operator=(const StreamString &) = delete;
                /** @brief Deleted move constructor. */
                StreamString(StreamString &&) = delete;
                /** @brief Deleted move assignment operator. */
                StreamString &operator=(StreamString &&) = delete;

                /**
                 * @brief Returns the std::ostream associated with this buffer.
                 * @return Reference to the output stream.
                 */
                std::ostream &stream() { return _stream; }

                /**
                 * @brief Sets or replaces the newline callback.
                 * @param func New callback to invoke on each complete line.
                 */
                void setOnNewLine(OnNewLineFunc func) {
                        _onNewLine = std::move(func);
                        return;
                }

                /**
                 * @brief Returns the current (possibly incomplete) line buffer.
                 * @return Const reference to the accumulated line.
                 */
                const String &line() const { return _line; }

                /** @brief Clears the accumulated line buffer. */
                void clear() {
                        _line.clear();
                        return;
                }

        protected:
                /**
                 * @brief Handles a single character written to the stream.
                 * @param ch The character to process, or EOF.
                 * @return The character that was processed.
                 */
                int overflow(int ch) override {
                        if(ch == '\n' || ch == EOF) {
                                flush();
                        } else {
                                _line += static_cast<char>(ch);
                        }
                        return ch;
                }

                /** @brief Synchronizes the stream buffer by flushing the current line. */
                int sync() override {
                        flush();
                        return 0;
                }

        private:
                OnNewLineFunc   _onNewLine;
                String          _line;
                std::ostream    _stream;

                void flush() {
                        if(!_line.isEmpty() && _onNewLine) {
                                if(_onNewLine(_line)) _line.clear();
                        }
                        return;
                }
};

PROMEKI_NAMESPACE_END
