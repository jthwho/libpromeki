/**
 * @file      ancafd.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief SMPTE ST 2016-3 Active Format Description value, including
 *        the companion Bar Data fields carried in the same packet.
 * @ingroup proav
 *
 * The ST 2016-3 §4 combined ANC packet (DID 0x41 / SDID 0x05, DC=8)
 * carries both the AFD code + AR aspect-ratio flag in UDW 1 and the
 * letterbox / pillarbox @em Bar @em Data (defined by ST 2016-1) in
 * UDWs 4-8.  This value type round-trips every wire field — capture
 * paths that decode an AFD packet preserve any bar-data the source
 * stamped on it, and producers that have letterbox / pillarbox
 * positions to publish ride them through the same packet rather than
 * requiring a separate carriage.
 *
 * The class is plain-value: copies are independent and there is no
 * internal shared pointer.  Distinct instances may be used
 * concurrently.
 *
 * @par Wire mapping (ST 2016-3 §4.1 Table 1, indexed UDW 1..8)
 *
 * | UDW | Bits 7..0                                                  |
 * |-----|------------------------------------------------------------|
 * | 1   | '0' | a3 | a2 | a1 | a0 | AR | '0' | '0'                  |
 * | 2   | Reserved (zero)                                            |
 * | 3   | Reserved (zero)                                            |
 * | 4   | Top | Bot | Left | Right | '0' | '0' | '0' | '0'          |
 * | 5-6 | Bar Data Value 1 (16 bits, MSB-first across the two UDWs)  |
 * | 7-8 | Bar Data Value 2 (16 bits, MSB-first across the two UDWs)  |
 *
 * The @ref BarFlag bit positions match UDW 4 verbatim so callers can
 * stamp the byte directly with @ref setBarFlags or use the per-flag
 * convenience accessors below.
 *
 * @par Bar-data semantics (ST 2016-1 §6)
 *
 * ST 2016-1 defines the four flag bits and the two 16-bit values as:
 *
 *  - @ref TopBar set + @ref BottomBar set → @em letterbox; Value 1 =
 *    line number at the end of the upper black bar (first active
 *    line minus one), Value 2 = line number at the start of the
 *    lower black bar.
 *  - @ref LeftBar set + @ref RightBar set → @em pillarbox; Value 1 =
 *    horizontal pixel offset of the last black pixel on the left
 *    bar, Value 2 = horizontal pixel offset of the first black
 *    pixel on the right bar.
 *
 * No flag set is the common "no bar data published" state — value
 * bytes ride as zero and consumers ignore them.  The library does
 * not interpret combinations beyond round-tripping the bytes.
 *
 * @par Example
 *
 * @code
 * AncAfd afd;
 * afd.setAfdCode(0x0A);                      // 16:9 active in 16:9 frame
 * afd.setArFlag(true);
 * afd.setBarFlag(AncAfd::TopBar, true);
 * afd.setBarFlag(AncAfd::BottomBar, true);
 * afd.setBarValue1(60);                      // top-bar end line
 * afd.setBarValue2(420);                     // bottom-bar start line
 *
 * Variant v(afd);
 * @endcode
 *
 * @par Thread Safety
 * Plain value type.  Copies are independent and may be used
 * concurrently; concurrent mutation of a single instance is not
 * synchronised.
 *
 * @see AncFormat::Afd, AncFormat::PanScan
 */
class AncAfd {
        public:
                PROMEKI_DATATYPE(AncAfd, DataTypeAncAfd, 1)

                /**
                 * @brief Bar-data flag bits as they sit in UDW 4 of an
                 *        ST 2016-3 AFD packet.
                 *
                 * The values match the bit positions on the wire so a
                 * @ref barFlags byte can be written into UDW 4
                 * directly without re-shifting.
                 */
                enum BarFlag : uint8_t {
                        TopBar    = 0x80, ///< UDW 4 bit 7 — letterbox top bar present.
                        BottomBar = 0x40, ///< UDW 4 bit 6 — letterbox bottom bar present.
                        LeftBar   = 0x20, ///< UDW 4 bit 5 — pillarbox left bar present.
                        RightBar  = 0x10, ///< UDW 4 bit 4 — pillarbox right bar present.
                };

                /** @brief Default-constructs an empty AFD value (zero code, AR off, no bar data). */
                AncAfd() = default;

                /** @brief Constructs from an AFD code and AR flag; bar-data fields default to zero. */
                AncAfd(uint8_t afdCode, bool ar)
                        : _afdCode(static_cast<uint8_t>(afdCode & 0x0F)), _arFlag(ar) {}

                // -- AFD code + AR flag -----------------------------------

                /** @brief Returns the 4-bit AFD code (low 4 bits of the byte). */
                uint8_t afdCode() const { return _afdCode; }

                /** @brief Replaces the AFD code (low 4 bits taken from @p code). */
                void setAfdCode(uint8_t code) { _afdCode = static_cast<uint8_t>(code & 0x0F); }

                /** @brief Returns the AR aspect-ratio flag (UDW 1 bit 2). */
                bool arFlag() const { return _arFlag; }

                /** @brief Replaces the AR aspect-ratio flag. */
                void setArFlag(bool on) { _arFlag = on; }

                // -- Bar-data flags ---------------------------------------

                /** @brief Returns the raw bar-data flag byte (UDW 4). */
                uint8_t barFlags() const { return _barFlags; }

                /**
                 * @brief Replaces the raw bar-data flag byte.
                 *
                 * Only the top nibble (bits 7..4 = Top / Bot / Left /
                 * Right) is preserved; the low nibble per ST 2016-3
                 * Table 1 is reserved-zero and is masked off.
                 */
                void setBarFlags(uint8_t flags) {
                        _barFlags = static_cast<uint8_t>(flags & 0xF0);
                }

                /** @brief Returns @c true when @p f is set in @ref barFlags. */
                bool hasBarFlag(BarFlag f) const {
                        return (_barFlags & static_cast<uint8_t>(f)) != 0;
                }

                /** @brief Sets (@p on=true) or clears (@p on=false) bar flag @p f. */
                void setBarFlag(BarFlag f, bool on) {
                        if (on) _barFlags = static_cast<uint8_t>(_barFlags | static_cast<uint8_t>(f));
                        else _barFlags = static_cast<uint8_t>(_barFlags & ~static_cast<uint8_t>(f));
                }

                /** @name Per-flag convenience accessors */
                /// @{
                bool topBar() const { return hasBarFlag(TopBar); }
                void setTopBar(bool on) { setBarFlag(TopBar, on); }
                bool bottomBar() const { return hasBarFlag(BottomBar); }
                void setBottomBar(bool on) { setBarFlag(BottomBar, on); }
                bool leftBar() const { return hasBarFlag(LeftBar); }
                void setLeftBar(bool on) { setBarFlag(LeftBar, on); }
                bool rightBar() const { return hasBarFlag(RightBar); }
                void setRightBar(bool on) { setBarFlag(RightBar, on); }
                /// @}

                /**
                 * @brief Returns @c true when any of the four bar-data
                 *        flags is set.
                 *
                 * Convenience for the common "this packet actually
                 * carries letterbox / pillarbox data" predicate.
                 */
                bool hasBarData() const { return _barFlags != 0; }

                // -- Bar-data values --------------------------------------

                /** @brief Returns the first bar-data 16-bit value (UDW 5-6, MSB-first). */
                uint16_t barValue1() const { return _barValue1; }

                /** @brief Replaces the first bar-data 16-bit value. */
                void setBarValue1(uint16_t v) { _barValue1 = v; }

                /** @brief Returns the second bar-data 16-bit value (UDW 7-8, MSB-first). */
                uint16_t barValue2() const { return _barValue2; }

                /** @brief Replaces the second bar-data 16-bit value. */
                void setBarValue2(uint16_t v) { _barValue2 = v; }

                // -- Comparison -------------------------------------------

                /** @brief Field-wise equality. */
                bool operator==(const AncAfd &o) const {
                        return _afdCode == o._afdCode && _arFlag == o._arFlag &&
                               _barFlags == o._barFlags && _barValue1 == o._barValue1 &&
                               _barValue2 == o._barValue2;
                }

                /** @brief Inequality. */
                bool operator!=(const AncAfd &o) const { return !(*this == o); }

                // -- Diagnostics ------------------------------------------

                /** @brief Returns a short human-readable summary (code + AR + bar data when present). */
                String toString() const;

                /** @brief Returns a structured JSON representation. */
                JsonObject toJson() const;

                // -- DataStream -------------------------------------------

                /** @brief Writes the canonical wire body via @ref PROMEKI_DATATYPE. */
                Error writeToStream(DataStream &s) const;

                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<AncAfd> readFromStream(DataStream &s);

        private:
                uint8_t  _afdCode = 0;
                bool     _arFlag = false;
                uint8_t  _barFlags = 0;
                uint16_t _barValue1 = 0;
                uint16_t _barValue2 = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
