/**
 * @file      streamstring.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <streambuf>
#include <ostream>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// A std::streambuf subclass that collects stream output into a String,
// splitting on newlines and calling onNewLine() for each complete line.
//
// The onNewLine callback receives a non-const reference to the line buffer
// and returns true to clear it, or false to keep accumulating.
//
// Usage with a callback (e.g. routing to the logger):
//   StreamString ss([](String &line) {
//           promekiInfo("%s", line.cstr());
//           return true;
//   });
//   ss.stream() << "Hello " << 42 << std::endl;
class StreamString : public std::streambuf {
        public:
                using OnNewLineFunc = std::function<bool(String &)>;

                StreamString() : _stream(this) { }
                StreamString(OnNewLineFunc func) : _onNewLine(std::move(func)), _stream(this) { }

                // Not copyable or movable because _stream holds a pointer to this
                StreamString(const StreamString &) = delete;
                StreamString &operator=(const StreamString &) = delete;
                StreamString(StreamString &&) = delete;
                StreamString &operator=(StreamString &&) = delete;

                std::ostream &stream() { return _stream; }

                void setOnNewLine(OnNewLineFunc func) {
                        _onNewLine = std::move(func);
                        return;
                }

                const String &line() const { return _line; }

                void clear() {
                        _line.clear();
                        return;
                }

        protected:
                int overflow(int ch) override {
                        if(ch == '\n' || ch == EOF) {
                                flush();
                        } else {
                                _line += static_cast<char>(ch);
                        }
                        return ch;
                }

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
