/*****************************************************************************
 * uuid.cpp
 * May 03, 2023
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

#include <promeki/uuid.h>

PROMEKI_NAMESPACE_BEGIN

inline int hexCharToVal(char num) {
        int val;
        if(num >= 'A' && num <= 'F') {
                val = static_cast<int>(num) - static_cast<int>('A') + 10;
        } else if(num >= 'a' && num <= 'f') {
                val = static_cast<int>(num) - static_cast<int>('a') + 10;
        } else if(num >= '0' && num <= '9') {
                val = static_cast<int>(num) - static_cast<int>('0');
        } else {
                val = -1;
        }
        return val;
}

inline bool hexStr(uint8_t *to, const char *buf, Error *err) {
        int d1 = hexCharToVal(buf[0]);
        if(d1 == -1) {
                if(err != nullptr) *err = Error::Invalid;
                return false;
        }
        int d2 = hexCharToVal(buf[1]);
        if(d2 == -1) {
                if(err != nullptr) *err = Error::Invalid;
                return false;
        }
        *to = d1 * 16 + d2;
        return true;
}

UUID UUID::fromString(const char *str, Error *err) {
        DataFormat data;
        uint8_t *d = data.data();
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(*str++ != '-') {
                if(err != nullptr) *err = Error::Invalid;
                return UUID();
        }
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(*str++ != '-') {
                if(err != nullptr) *err = Error::Invalid;
                return UUID();
        }
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(*str++ != '-') {
                if(err != nullptr) *err = Error::Invalid;
                return UUID();
        }
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(*str++ != '-') {
                if(err != nullptr) *err = Error::Invalid;
                return UUID();
        }
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d++, str, err)) return UUID(); str += 2;
        if(!hexStr(d, str, err)) return UUID();

        if(err != nullptr) *err = Error::Ok;
        return static_cast<DataFormat>(data);
}

inline void strHex(char *str, const uint8_t *d) {
        static const char digits[] = "0123456789abcdef";
        uint8_t val = *d;
        str[0] = digits[val / 16];
        str[1] = digits[val % 16];
        return;
}

String UUID::toString() const {
        char buf[37];
        char *str = buf;
        const uint8_t *data = d.data();
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        *str++ = '-';
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        *str++ = '-';
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        *str++ = '-';
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        *str++ = '-';
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        strHex(str, data++); str += 2;
        strHex(str, data); str += 2;
        *str = 0;
        return buf;
}

PROMEKI_NAMESPACE_END

