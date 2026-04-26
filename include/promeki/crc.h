/**
 * @file      crc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Table-driven cyclic redundancy check (CRC) computation.
 * @ingroup core
 *
 * @tparam T Unsigned integer holding the CRC register and result.  One
 *           of @c uint8_t, @c uint16_t, @c uint32_t, or @c uint64_t.
 *
 * The class follows the Rocksoft / "CRC catalogue" parameter model
 * (https://reveng.sourceforge.io/crc-catalogue/all.htm): every
 * configurable CRC variant is described by a small @ref Params struct
 * holding @c poly, @c init, @c xorOut, and a single @c reflect flag
 * that controls *both* input and output bit reflection (the asymmetric
 * @c refIn != @c refOut combinations are exotic and not supported here).
 *
 * Each @c CRC instance owns a 256-entry lookup table that is computed
 * once in the constructor.  After that, @ref update is a tight
 * byte-at-a-time loop with one table read, one XOR, and one shift per
 * input byte — fast enough for video-rate use without any further
 * vectorisation.
 *
 * @par Example
 * @code
 * CRC<uint8_t> crc(CrcParams::Crc8Autosar);
 * crc.update(payload, 8);
 * uint8_t value = crc.value();
 *
 * // Or one-shot:
 * uint8_t value2 = CRC<uint8_t>::compute(CrcParams::Crc8Autosar, payload, 8);
 *
 * // Or via the named factory:
 * Crc8 c = crc8_autosar();
 * c.update(payload, 8);
 * @endcode
 *
 * @par Verifying parameters against published catalogue check values
 * The Rocksoft catalogue lists a "check" value for each named CRC: the
 * 8-bit ASCII string @c "123456789" (without the trailing nul) fed
 * through the CRC must produce a known result.  The unit tests for
 * this class use those check values to validate the @ref CrcParams
 * presets.
 *
 * @see CrcParams, crc8_smbus, crc8_autosar, crc16_ccitt_false, crc32_iso_hdlc
 */
template <typename T> class CRC {
        public:
                static_assert(std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> ||
                                      std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>,
                              "CRC<T> requires T = uint8_t / uint16_t / uint32_t / uint64_t");

                /** @brief CRC register / result type. */
                using Value = T;

                /** @brief Register width in bits. */
                static constexpr int Width = static_cast<int>(sizeof(T) * 8);

                /**
                 * @brief Configurable parameters for one CRC variant.
                 *
                 * Mirrors the Rocksoft model with one simplification:
                 * @c reflect controls both input and output reflection
                 * together.  All catalogued CRCs the library currently
                 * provides have @c refIn == @c refOut.
                 */
                struct Params {
                                T           poly;    ///< Generator polynomial (canonical, MSB-first form).
                                T           init;    ///< Initial value of the working register.
                                T           xorOut;  ///< Final value XORed into the result.
                                bool        reflect; ///< @c true reflects both input bytes and the final result.
                                const char *name;    ///< Human-readable name (used by diagnostics; may be @c nullptr).
                };

                /**
                 * @brief Constructs a CRC and computes its lookup table.
                 * @param params Algorithm parameters.
                 */
                explicit CRC(const Params &params);

                /** @brief Resets the working register to the initial value. */
                void reset();

                /**
                 * @brief Folds @p len bytes from @p data into the running CRC.
                 * @param data Pointer to the input bytes.
                 * @param len  Number of bytes to process.
                 */
                void update(const void *data, size_t len);

                /**
                 * @brief Returns the final CRC value, including the @c xorOut step.
                 *
                 * @c value can be called multiple times without disturbing
                 * the running register; only @ref reset clears state.
                 *
                 * @return The current CRC value, masked to @ref Width bits.
                 */
                T value() const;

                /**
                 * @brief One-shot CRC over a buffer.
                 *
                 * Equivalent to constructing a fresh @c CRC, calling
                 * @ref update once, and reading @ref value.  Convenient
                 * for callers that do not need to reuse the table.  For
                 * frequently-computed CRCs that share parameters, prefer
                 * to keep a long-lived @c CRC instance and reset it
                 * between messages so the table is built only once.
                 *
                 * @param params Algorithm parameters.
                 * @param data   Input bytes.
                 * @param len    Number of bytes.
                 * @return The CRC value.
                 */
                static T compute(const Params &params, const void *data, size_t len);

                /** @brief Returns the parameters this CRC was constructed with. */
                const Params &params() const { return _params; }

        private:
                void buildTable();

                Params _params{};
                T      _table[256]{};
                T      _crc{};
};

extern template class CRC<uint8_t>;
extern template class CRC<uint16_t>;
extern template class CRC<uint32_t>;
extern template class CRC<uint64_t>;

/** @brief 8-bit CRC. */
using Crc8 = CRC<uint8_t>;
/** @brief 16-bit CRC. */
using Crc16 = CRC<uint16_t>;
/** @brief 32-bit CRC. */
using Crc32 = CRC<uint32_t>;
/** @brief 64-bit CRC. */
using Crc64 = CRC<uint64_t>;

/**
 * @brief Catalogue of standard CRC parameter sets.
 * @ingroup core
 *
 * Each entry is a @c constexpr @ref CRC::Params instance — pass any
 * one to a @c CRC constructor or to @ref CRC::compute.  Add new
 * variants as needed; this is intentionally not exhaustive.
 *
 * @see CRC, crc8_smbus, crc8_autosar
 */
namespace CrcParams {

        /// CRC-8/SMBus — poly 0x07, init 0x00, no reflection, no xor.
        inline constexpr Crc8::Params Crc8Smbus{0x07, 0x00, 0x00, false, "CRC-8/SMBus"};
        /// CRC-8/AUTOSAR — poly 0x2F, init 0xFF, no reflection, xor 0xFF.
        /// Best Hamming distance among the 8-bit CRCs at small payload sizes.
        inline constexpr Crc8::Params Crc8Autosar{0x2F, 0xFF, 0xFF, false, "CRC-8/AUTOSAR"};
        /// CRC-8/Bluetooth — poly 0xA7, reflected, no xor.
        inline constexpr Crc8::Params Crc8Bluetooth{0xA7, 0x00, 0x00, true, "CRC-8/Bluetooth"};

        /// CRC-16/CCITT-FALSE — poly 0x1021, init 0xFFFF, no reflection, no xor.
        /// Also known as CRC-16/AUTOSAR / CRC-16/IBM-3740.
        inline constexpr Crc16::Params Crc16CcittFalse{0x1021, 0xFFFF, 0x0000, false, "CRC-16/CCITT-FALSE"};
        /// CRC-16/Kermit — poly 0x1021, init 0x0000, reflected, no xor.
        inline constexpr Crc16::Params Crc16Kermit{0x1021, 0x0000, 0x0000, true, "CRC-16/Kermit"};

        /// CRC-32/ISO-HDLC — the canonical zlib / IEEE 802.3 / PNG CRC.
        /// poly 0x04C11DB7, init 0xFFFFFFFF, reflected, xor 0xFFFFFFFF.
        inline constexpr Crc32::Params Crc32IsoHdlc{0x04C11DB7u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, "CRC-32/ISO-HDLC"};
        /// CRC-32/BZIP2 — same poly as ISO-HDLC but unreflected.
        inline constexpr Crc32::Params Crc32Bzip2{0x04C11DB7u, 0xFFFFFFFFu, 0xFFFFFFFFu, false, "CRC-32/BZIP2"};

} // namespace CrcParams

/// @brief Returns a fresh @ref Crc8 configured for CRC-8/SMBus.
inline Crc8 crc8_smbus() {
        return Crc8(CrcParams::Crc8Smbus);
}
/// @brief Returns a fresh @ref Crc8 configured for CRC-8/AUTOSAR.
inline Crc8 crc8_autosar() {
        return Crc8(CrcParams::Crc8Autosar);
}
/// @brief Returns a fresh @ref Crc16 configured for CRC-16/CCITT-FALSE.
inline Crc16 crc16_ccitt_false() {
        return Crc16(CrcParams::Crc16CcittFalse);
}
/// @brief Returns a fresh @ref Crc32 configured for CRC-32/ISO-HDLC.
inline Crc32 crc32_iso_hdlc() {
        return Crc32(CrcParams::Crc32IsoHdlc);
}

PROMEKI_NAMESPACE_END
