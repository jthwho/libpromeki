/**
 * @file      inputparser.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/inputparser.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

TuiInputParser::TuiInputParser() = default;

List<TuiInputParser::ParsedEvent> TuiInputParser::feed(const char *data, int len) {
        List<ParsedEvent> events;

        for (int i = 0; i < len; ++i) {
                char c = data[i];

                switch (_state) {
                        case Normal:
                                if (c == '\033') {
                                        _state = EscapeStart;
                                        _buf.clear();
                                } else if (c == '\r' || c == '\n') {
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = KeyEvent::Key_Enter;
                                        events += ev;
                                } else if (c == '\t') {
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = KeyEvent::Key_Tab;
                                        events += ev;
                                } else if (c == 127 || c == '\b') {
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = KeyEvent::Key_Backspace;
                                        events += ev;
                                } else if (c >= 1 && c <= 26) {
                                        // Ctrl+letter
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = static_cast<KeyEvent::Key>(c + 'a' - 1);
                                        ev.modifiers = KeyEvent::CtrlModifier;
                                        events += ev;
                                } else if (static_cast<uint8_t>(c) >= 0x80) {
                                        // UTF-8 multi-byte: collect and emit
                                        String text;
                                        text += c;
                                        uint8_t lead = static_cast<uint8_t>(c);
                                        int     remaining = 0;
                                        if ((lead & 0xE0) == 0xC0)
                                                remaining = 1;
                                        else if ((lead & 0xF0) == 0xE0)
                                                remaining = 2;
                                        else if ((lead & 0xF8) == 0xF0)
                                                remaining = 3;
                                        for (int j = 0; j < remaining && i + 1 < len; ++j) {
                                                i++;
                                                text += data[i];
                                        }
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = KeyEvent::Key_Unknown;
                                        ev.text = text;
                                        events += ev;
                                } else {
                                        // Regular printable character
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = static_cast<KeyEvent::Key>(c);
                                        ev.text = String(1, c);
                                        events += ev;
                                }
                                break;

                        case EscapeStart:
                                if (c == '[') {
                                        _state = CSIParam;
                                        _buf.clear();
                                } else if (c == 'O') {
                                        _state = SS3;
                                        _buf.clear();
                                } else {
                                        // Alt+key
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Key;
                                        ev.key = static_cast<KeyEvent::Key>(c);
                                        ev.modifiers = KeyEvent::AltModifier;
                                        if (c >= 32 && c < 127) {
                                                ev.text = String(1, c);
                                        }
                                        events += ev;
                                        _state = Normal;
                                }
                                break;

                        case CSIParam:
                                if (c == '<') {
                                        // SGR mouse
                                        _state = MouseSGR;
                                        _buf.clear();
                                } else if ((c >= '0' && c <= '9') || c == ';') {
                                        _buf += c;
                                } else {
                                        // End of CSI sequence.  parseCSI may switch
                                        // us into Paste mode (ESC [ 200 ~); only
                                        // fall back to Normal if it didn't change
                                        // the state out from under us.
                                        _buf += c;
                                        parseCSI(_buf, events);
                                        if (_state == CSIParam) _state = Normal;
                                }
                                break;

                        case SS3:
                                parseSS3(c, events);
                                _state = Normal;
                                break;

                        case MouseSGR:
                                if (c == 'M' || c == 'm') {
                                        _buf += c;
                                        parseMouseSGR(_buf, events);
                                        _state = Normal;
                                } else {
                                        _buf += c;
                                }
                                break;

                        case Paste: {
                                // Collect raw paste content until the end marker
                                // ESC [ 201 ~ arrives, then emit it as one event.
                                // The content may legitimately contain ESC bytes,
                                // so we match on the full terminator suffix.
                                _pasteBuf += c;
                                if (_pasteBuf.endsWith("\033[201~")) {
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Paste;
                                        ev.text = _pasteBuf.left(_pasteBuf.length() - 6);
                                        events += ev;
                                        _pasteBuf.clear();
                                        _state = Normal;
                                } else if (_pasteBuf.length() > kMaxPasteBytes) {
                                        // Defensive bail-out: a paste that never
                                        // terminates (terminal bug / disconnect)
                                        // must not grow the buffer or wedge the
                                        // parser forever.  Flush what we have.
                                        ParsedEvent ev;
                                        ev.type = ParsedEvent::Paste;
                                        ev.text = _pasteBuf;
                                        events += ev;
                                        _pasteBuf.clear();
                                        _state = Normal;
                                }
                                break;
                        }

                        default: _state = Normal; break;
                }
        }

        // Handle bare Escape (no following character)
        if (_state == EscapeStart) {
                ParsedEvent ev;
                ev.type = ParsedEvent::Key;
                ev.key = KeyEvent::Key_Escape;
                events += ev;
                _state = Normal;
        }

        return events;
}

void TuiInputParser::parseCSI(const String &seq, List<ParsedEvent> &events) {
        if (seq.isEmpty()) return;

        Char   final = seq.charAt(seq.length() - 1);
        String params = seq.substr(0, seq.length() - 1);

        // Parse semicolon-separated parameters
        List<int> nums;
        if (!params.isEmpty()) {
                size_t pos = 0;
                while (pos < params.length()) {
                        size_t semi = params.find(';', pos);
                        if (semi == String::npos) semi = params.length();
                        String part = params.substr(pos, semi - pos);
                        Error  err;
                        int    val = part.toInt(&err);
                        nums += err.isOk() ? val : 0;
                        pos = semi + 1;
                }
        }

        int code = nums.isEmpty() ? 0 : nums[0];
        int modifier = nums.size() > 1 ? nums[1] : 0;

        // Bracketed paste start (ESC [ 200 ~): switch to paste-collection mode.
        // The matching ESC [ 201 ~ is consumed by the Paste state, not here.
        if (final == '~' && code == 200) {
                _state = Paste;
                _pasteBuf.clear();
                return;
        }

        // Focus events (ESC [ I / ESC [ O, mode 1004).  These reach CSI parsing
        // because 'I' / 'O' are non-numeric CSI finals.
        if (final == 'I') {
                ParsedEvent ev;
                ev.type = ParsedEvent::FocusIn;
                events += ev;
                return;
        }
        if (final == 'O') {
                ParsedEvent ev;
                ev.type = ParsedEvent::FocusOut;
                events += ev;
                return;
        }

        ParsedEvent ev;
        ev.type = ParsedEvent::Key;

        switch (final.codepoint()) {
                case 'A': ev.key = KeyEvent::Key_Up; break;
                case 'B': ev.key = KeyEvent::Key_Down; break;
                case 'C': ev.key = KeyEvent::Key_Right; break;
                case 'D': ev.key = KeyEvent::Key_Left; break;
                case 'H': ev.key = KeyEvent::Key_Home; break;
                case 'F': ev.key = KeyEvent::Key_End; break;
                case '~':
                        switch (code) {
                                case 1: ev.key = KeyEvent::Key_Home; break;
                                case 2: ev.key = KeyEvent::Key_Insert; break;
                                case 3: ev.key = KeyEvent::Key_Delete; break;
                                case 4: ev.key = KeyEvent::Key_End; break;
                                case 5: ev.key = KeyEvent::Key_PageUp; break;
                                case 6: ev.key = KeyEvent::Key_PageDown; break;
                                case 15: ev.key = KeyEvent::Key_F5; break;
                                case 17: ev.key = KeyEvent::Key_F6; break;
                                case 18: ev.key = KeyEvent::Key_F7; break;
                                case 19: ev.key = KeyEvent::Key_F8; break;
                                case 20: ev.key = KeyEvent::Key_F9; break;
                                case 21: ev.key = KeyEvent::Key_F10; break;
                                case 23: ev.key = KeyEvent::Key_F11; break;
                                case 24: ev.key = KeyEvent::Key_F12; break;
                                default: return;
                        }
                        break;
                case 'P': ev.key = KeyEvent::Key_F1; break;
                case 'Q': ev.key = KeyEvent::Key_F2; break;
                case 'R': ev.key = KeyEvent::Key_F3; break;
                case 'S': ev.key = KeyEvent::Key_F4; break;
                default: return;
        }

        // Decode xterm modifier
        if (modifier > 1) {
                modifier -= 1;
                if (modifier & 1) ev.modifiers |= KeyEvent::ShiftModifier;
                if (modifier & 2) ev.modifiers |= KeyEvent::AltModifier;
                if (modifier & 4) ev.modifiers |= KeyEvent::CtrlModifier;
                if (modifier & 8) ev.modifiers |= KeyEvent::MetaModifier;
        }

        events += ev;
}

void TuiInputParser::parseSS3(char ch, List<ParsedEvent> &events) {
        ParsedEvent ev;
        ev.type = ParsedEvent::Key;

        switch (ch) {
                case 'P': ev.key = KeyEvent::Key_F1; break;
                case 'Q': ev.key = KeyEvent::Key_F2; break;
                case 'R': ev.key = KeyEvent::Key_F3; break;
                case 'S': ev.key = KeyEvent::Key_F4; break;
                case 'A': ev.key = KeyEvent::Key_Up; break;
                case 'B': ev.key = KeyEvent::Key_Down; break;
                case 'C': ev.key = KeyEvent::Key_Right; break;
                case 'D': ev.key = KeyEvent::Key_Left; break;
                case 'H': ev.key = KeyEvent::Key_Home; break;
                case 'F': ev.key = KeyEvent::Key_End; break;
                default: return;
        }

        events += ev;
}

void TuiInputParser::parseMouseSGR(const String &seq, List<ParsedEvent> &events) {
        // Format: <button;col;row[Mm]
        if (seq.isEmpty()) return;

        Char   final = seq.charAt(seq.length() - 1);
        String params = seq.substr(0, seq.length() - 1);

        List<int> nums;
        size_t    pos = 0;
        while (pos < params.length()) {
                size_t semi = params.find(';', pos);
                if (semi == String::npos) semi = params.length();
                String part = params.substr(pos, semi - pos);
                Error  err;
                nums += part.toInt(&err);
                pos = semi + 1;
        }

        if (nums.size() < 3) return;

        int buttonCode = nums[0];
        int col = nums[1] - 1; // Convert to 0-based
        int row = nums[2] - 1;

        ParsedEvent ev;
        ev.type = ParsedEvent::Mouse;
        ev.mousePos = Point2Di32(col, row);

        // Decode modifiers from button code
        if (buttonCode & 4) ev.modifiers |= KeyEvent::ShiftModifier;
        if (buttonCode & 8) ev.modifiers |= KeyEvent::AltModifier;
        if (buttonCode & 16) ev.modifiers |= KeyEvent::CtrlModifier;

        int  btn = buttonCode & 3;
        bool motion = (buttonCode & 32) != 0;
        bool scroll = (buttonCode & 64) != 0;

        // Map SGR button code to Button flag
        auto btnFlag = [](int b) -> MouseEvent::Button {
                switch (b) {
                        case 0: return MouseEvent::LeftButton;
                        case 1: return MouseEvent::MiddleButton;
                        case 2: return MouseEvent::RightButton;
                        default: return MouseEvent::NoButton;
                }
        };

        if (scroll) {
                ev.mouseButton = MouseEvent::NoButton;
                ev.mouseAction = (btn == 0) ? MouseEvent::ScrollUp : MouseEvent::ScrollDown;
        } else if (motion) {
                ev.mouseAction = MouseEvent::Move;
                ev.mouseButton = btnFlag(btn);
        } else {
                ev.mouseButton = btnFlag(btn);
                if (final == 'M') {
                        ev.mouseAction = MouseEvent::Press;
                        _buttonState |= ev.mouseButton;
                } else {
                        ev.mouseAction = MouseEvent::Release;
                        _buttonState &= ~ev.mouseButton;
                }
        }

        ev.mouseButtons = _buttonState;
        events += ev;
}

KeyEvent::Key TuiInputParser::csiToKey(int code, int modifier) {
        (void)modifier;
        switch (code) {
                case 1: return KeyEvent::Key_Home;
                case 2: return KeyEvent::Key_Insert;
                case 3: return KeyEvent::Key_Delete;
                case 4: return KeyEvent::Key_End;
                case 5: return KeyEvent::Key_PageUp;
                case 6: return KeyEvent::Key_PageDown;
                default: return KeyEvent::Key_Unknown;
        }
}

PROMEKI_NAMESPACE_END
