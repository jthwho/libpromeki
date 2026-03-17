/**
 * @file      encodeddesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/encodeddesc.h>

PROMEKI_NAMESPACE_BEGIN

String EncodedDesc::toString() const {
        char c0 = static_cast<char>((_codec.value() >> 24) & 0xFF);
        char c1 = static_cast<char>((_codec.value() >> 16) & 0xFF);
        char c2 = static_cast<char>((_codec.value() >> 8) & 0xFF);
        char c3 = static_cast<char>(_codec.value() & 0xFF);
        if(_quality >= 0) {
                return String::sprintf("[%c%c%c%c q=%d]", c0, c1, c2, c3, _quality);
        }
        return String::sprintf("[%c%c%c%c]", c0, c1, c2, c3);
}

PROMEKI_NAMESPACE_END
