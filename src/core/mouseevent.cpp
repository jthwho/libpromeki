/**
 * @file      mouseevent.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

const Event::Type MouseEvent::Mouse = Event::registerType();

String MouseEvent::buttonName(Button button) {
        switch(button) {
                case NoButton:          return "None";
                case LeftButton:        return "Left";
                case MiddleButton:      return "Middle";
                case RightButton:       return "Right";
                default:                return "?";
        }
}

String MouseEvent::buttonsString(uint8_t buttons) {
        if(buttons == 0) return "None";
        String result;
        if(buttons & LeftButton)   result += "Left ";
        if(buttons & MiddleButton) result += "Middle ";
        if(buttons & RightButton)  result += "Right ";
        return result;
}

String MouseEvent::actionName(Action action) {
        switch(action) {
                case Press:             return "Press";
                case Release:           return "Release";
                case Move:              return "Move";
                case DoubleClick:       return "DoubleClick";
                case ScrollUp:          return "ScrollUp";
                case ScrollDown:        return "ScrollDown";
        }
        return "?";
}

PROMEKI_NAMESPACE_END
