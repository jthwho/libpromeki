/**
 * @file      keyevent.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/keyevent.h>

PROMEKI_NAMESPACE_BEGIN

const Event::Type KeyEvent::KeyPress = Event::registerType();
const Event::Type KeyEvent::KeyRelease = Event::registerType();

String KeyEvent::keyName(Key key) {
        switch(key) {
                case Key_Unknown:       return "Unknown";
                case Key_Space:         return "Space";
                case Key_Escape:        return "Escape";
                case Key_Enter:         return "Enter";
                case Key_Tab:           return "Tab";
                case Key_Backspace:     return "Backspace";
                case Key_Insert:        return "Insert";
                case Key_Delete:        return "Delete";
                case Key_Home:          return "Home";
                case Key_End:           return "End";
                case Key_PageUp:        return "PageUp";
                case Key_PageDown:      return "PageDown";
                case Key_Up:            return "Up";
                case Key_Down:          return "Down";
                case Key_Left:          return "Left";
                case Key_Right:         return "Right";
                case Key_F1:            return "F1";
                case Key_F2:            return "F2";
                case Key_F3:            return "F3";
                case Key_F4:            return "F4";
                case Key_F5:            return "F5";
                case Key_F6:            return "F6";
                case Key_F7:            return "F7";
                case Key_F8:            return "F8";
                case Key_F9:            return "F9";
                case Key_F10:           return "F10";
                case Key_F11:           return "F11";
                case Key_F12:           return "F12";
                default: {
                        int k = static_cast<int>(key);
                        if(k >= 33 && k <= 126) {
                                return String(1, static_cast<char>(k));
                        }
                        return "0x" + String::number(k, 16);
                }
        }
}

String KeyEvent::modifierString(uint8_t modifiers) {
        String result;
        if(modifiers & ShiftModifier) result += "Shift+";
        if(modifiers & CtrlModifier)  result += "Ctrl+";
        if(modifiers & AltModifier)   result += "Alt+";
        if(modifiers & MetaModifier)  result += "Meta+";
        return result;
}

PROMEKI_NAMESPACE_END
