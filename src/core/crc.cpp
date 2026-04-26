/**
 * @file      crc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/crc.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Bit-reflect the lowest @p nbits of @p value.
        template <typename T> T reflectBits(T value, int nbits) {
                T result = 0;
                for (int i = 0; i < nbits; i++) {
                        if (value & (T(1) << i)) {
                                result |= (T(1) << (nbits - 1 - i));
                        }
                }
                return result;
        }

        // Mask for @c W bits where W is sizeof(T)*8.  Hand-rolled because the
        // natural expression @c (T(1) << W) - 1 is undefined behaviour when
        // the shift count equals the type's bit width.
        template <typename T> constexpr T fullMask() {
                return static_cast<T>(~T(0));
        }

} // namespace

template <typename T> CRC<T>::CRC(const Params &params) : _params(params), _crc(params.init) {
        buildTable();
}

template <typename T> void CRC<T>::reset() {
        _crc = _params.init;
}

template <typename T> void CRC<T>::buildTable() {
        constexpr int W = Width;
        const T       mask = fullMask<T>();
        if (_params.reflect) {
                // Reflected mode: use the bit-reflected polynomial and a
                // shift-right algorithm.  Each table entry is the CRC of
                // a single byte fed in starting from a zeroed register.
                const T polyR = reflectBits<T>(_params.poly, W);
                for (int b = 0; b < 256; b++) {
                        T crc = static_cast<T>(b);
                        for (int i = 0; i < 8; i++) {
                                if (crc & T(1)) {
                                        crc = static_cast<T>((crc >> 1) ^ polyR);
                                } else {
                                        crc = static_cast<T>(crc >> 1);
                                }
                        }
                        _table[b] = crc;
                }
        } else {
                // Unreflected mode: shift-left algorithm with the
                // canonical polynomial.  The byte is placed in the
                // top byte of the register before processing.
                const T topBit = static_cast<T>(T(1) << (W - 1));
                for (int b = 0; b < 256; b++) {
                        T crc = static_cast<T>(static_cast<T>(b) << (W - 8));
                        for (int i = 0; i < 8; i++) {
                                if (crc & topBit) {
                                        crc = static_cast<T>(((crc << 1) ^ _params.poly) & mask);
                                } else {
                                        crc = static_cast<T>((crc << 1) & mask);
                                }
                        }
                        _table[b] = crc;
                }
        }
}

template <typename T> void CRC<T>::update(const void *data, size_t len) {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        constexpr int  W = Width;
        const T        mask = fullMask<T>();
        if (_params.reflect) {
                T crc = _crc;
                for (size_t i = 0; i < len; i++) {
                        const uint8_t idx = static_cast<uint8_t>((crc ^ p[i]) & 0xffu);
                        crc = static_cast<T>(_table[idx] ^ (crc >> 8));
                }
                _crc = crc;
        } else {
                T crc = _crc;
                if constexpr (W == 8) {
                        // 8-bit unreflected: register is exactly one byte;
                        // the standard "crc >> (W-8)" lookup degenerates to
                        // just XORing the input byte with the register.
                        for (size_t i = 0; i < len; i++) {
                                const uint8_t idx = static_cast<uint8_t>(crc ^ p[i]);
                                crc = _table[idx];
                        }
                } else {
                        for (size_t i = 0; i < len; i++) {
                                const uint8_t idx = static_cast<uint8_t>((crc >> (W - 8)) ^ p[i]);
                                crc = static_cast<T>(((crc << 8) ^ _table[idx]) & mask);
                        }
                }
                _crc = crc;
        }
}

template <typename T> T CRC<T>::value() const {
        return static_cast<T>(_crc ^ _params.xorOut);
}

template <typename T> T CRC<T>::compute(const Params &params, const void *data, size_t len) {
        CRC<T> crc(params);
        crc.update(data, len);
        return crc.value();
}

template class CRC<uint8_t>;
template class CRC<uint16_t>;
template class CRC<uint32_t>;
template class CRC<uint64_t>;

PROMEKI_NAMESPACE_END
