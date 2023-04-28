/*****************************************************************************
 * string.cpp
 * April 26, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <limits>
#include <promeki/string.h>

namespace promeki {

static const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

template <typename T>
static String num(T val, 
              int base = 10, 
              int padding = 0, 
              char padchar = ' ',
              bool addPrefix = false) {
        if(base < 2 || base > 36) {
                // FIXME: Report an error the base is out of bounds
                return String();
        }

        char buf[128];
        bool isNegative = false;
        bool isPaddingNegative = false;
        if(val < 0) {
                isNegative = true;
                val = -val;
        }
        if(padding < 0) {
                isPaddingNegative = true;
                padding = -padding;
        }

        int index = 0;
        if(val == 0) {
                buf[index++] = '0';
        } else {
                T remainder;
                while(val > 0) {
                        T r = val % base;
                        buf[index++] = digits[r];
                        val /= base;
                }
        }
        if(addPrefix) {
                switch(base) {
                        case 2:  buf[index++] = 'b'; buf[index++] = '0'; break;
                        case 8:  buf[index++] = 'o'; buf[index++] = '0'; break;
                        case 16: buf[index++] = 'x'; buf[index++] = '0'; break;
                        case 10: /* base 10 has no prefix */; break;
                        // There doesn't seem to be any common prefix notation for arbitary number
                        // bases, so inventing one here.
                        default: 
                                buf[index++] = ':';
                                buf[index++] = digits[base % 10];
                                if(base / 10) buf[index++] = digits[base / 10];
                                buf[index++] = 'b';
                                break;
                }
        }
        int remaining = sizeof(buf) - index - 1;
        padding -= index;
        if(padding < 0) {
                padding = 0;
                isPaddingNegative = false;
        }
        if(padding > remaining) {
                // FIXME: Report a warning that we've run out of space for the requested padding.
                padding = remaining;
        }
        for(int i = 0; i < padding; i++) buf[index++] = padchar;
        buf[index] = 0; // And the null terminator like the candle on the C string cake.
        
        // In the case the padding was negative, it comes after the number so we leave it
        // where it is in the string reverse.
        int lastPos = index - 1;
        if(isPaddingNegative) lastPos -= padding;

        // Now reverse the whole damn thing because it's easier to build a number in reverse.
        for (int i = 0, j = lastPos; i < j; i++, j--) {
                char temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
        }
        return String(buf);
}

String String::number(int8_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint8_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(int16_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint16_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(int32_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint32_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(int64_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint64_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String &String::arg(const String &str) {
        // Find the lowest numbered unreplaced placeholder.
        int minValue = std::numeric_limits<int>::max();
        size_t minPos = std::string::npos;
        std::string placeholderToReplace;
        for(size_t i = 0; i < this->size(); ++i) {
            if((*this)[i] == '%' && i + 1 < this->size() && std::isdigit((*this)[i + 1])) {
                size_t j = i + 1;
                while (j < this->size() && std::isdigit((*this)[j])) ++j;
                std::string placeholder = this->substr(i, j - i);
                int value = std::stoi(placeholder.substr(1));
                if(value < minValue) {
                    minValue = value;
                    minPos = i;
                    placeholderToReplace = placeholder;
                }
            }
        }

        // Replace the found placeholder with the argument value.
        if(minPos != std::string::npos) {
            this->replace(minPos, placeholderToReplace.length(), str);
        }
        return *this;
}

} // namespace promeki

