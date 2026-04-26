/**
 * @file      uuid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <promeki/uuid.h>
#include <promeki/list.h>
#include <promeki/random.h>
#include <promeki/md5.h>
#include <promeki/sha1.h>
#include <promeki/application.h>
#include <promeki/logger.h>

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

UUID UUID::generate(int version) {
        switch(version) {
                case 1: return generateV1();
                case 3: {
                        return generateV3(Application::appUUID(), Application::appName());
                }
                case 4: return generateV4();
                case 5: {
                        return generateV5(Application::appUUID(), Application::appName());
                }
                case 7: return generateV7();
                default:
                        promekiWarn("UUID::generate: unsupported version %d, returning invalid UUID", version);
                        return UUID();
        }
}

UUID UUID::generateV1() {
        promekiWarn("UUID::generateV1: not implemented, returning invalid UUID");
        return UUID();
}

UUID UUID::generateV3(const UUID &ns, const String &name) {
        // Concatenate namespace UUID bytes + name bytes, then MD5 hash
        const auto &nsData = ns.data();
        size_t nameLen = name.size();
        size_t totalLen = 16 + nameLen;
        List<uint8_t> input(totalLen);
        std::memcpy(input.data(), nsData.data(), 16);
        std::memcpy(input.data() + 16, name.cstr(), nameLen);

        MD5Digest hash = md5(input.data(), totalLen);

        DataFormat d;
        std::memcpy(d.data(), hash.data(), 16);
        d[6] = (d[6] & 0x0F) | 0x30; // Version 3
        d[8] = (d[8] & 0x3F) | 0x80; // Variant 2
        return UUID(d);
}

UUID UUID::generateV4() {
        DataFormat d;
        Error err = Random::trueRandom(d.data(), d.size());
        if(err.isOk()) {
                d[6] = (d[6] & 0x0F) | 0x40; // Version 4
                d[8] = (d[8] & 0x3F) | 0x80; // Variant 2
                return UUID(d);
        }
        return UUID();
}

UUID UUID::generateV5(const UUID &ns, const String &name) {
        // Concatenate namespace UUID bytes + name bytes, then SHA-1 hash
        const auto &nsData = ns.data();
        size_t nameLen = name.size();
        size_t totalLen = 16 + nameLen;
        List<uint8_t> input(totalLen);
        std::memcpy(input.data(), nsData.data(), 16);
        std::memcpy(input.data() + 16, name.cstr(), nameLen);

        SHA1Digest hash = sha1(input.data(), totalLen);

        // Use first 16 bytes of the 20-byte SHA-1 digest
        DataFormat d;
        std::memcpy(d.data(), hash.data(), 16);
        d[6] = (d[6] & 0x0F) | 0x50; // Version 5
        d[8] = (d[8] & 0x3F) | 0x80; // Variant 2
        return UUID(d);
}

UUID UUID::generateV7(int64_t timestampMs) {
        // 48-bit millisecond Unix timestamp + 74 bits of random data
        if(timestampMs < 0) {
                auto now = std::chrono::system_clock::now();
                timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()).count();
        }
        uint64_t timestamp = static_cast<uint64_t>(timestampMs);

        DataFormat d;
        Error err = Random::trueRandom(d.data(), d.size());
        if(!err.isOk()) return UUID();

        // Bytes 0-5: 48-bit timestamp (big-endian)
        d[0] = static_cast<uint8_t>(timestamp >> 40);
        d[1] = static_cast<uint8_t>(timestamp >> 32);
        d[2] = static_cast<uint8_t>(timestamp >> 24);
        d[3] = static_cast<uint8_t>(timestamp >> 16);
        d[4] = static_cast<uint8_t>(timestamp >> 8);
        d[5] = static_cast<uint8_t>(timestamp);

        d[6] = (d[6] & 0x0F) | 0x70; // Version 7
        d[8] = (d[8] & 0x3F) | 0x80; // Variant 2
        return UUID(d);
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
        return UUID(data);
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
