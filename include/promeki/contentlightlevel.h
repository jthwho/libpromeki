/**
 * @file      contentlightlevel.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Content light level information (CTA-861.3 / ITU-T H.265 Annex D).
 * @ingroup color
 *
 * Describes the brightness characteristics of the actual content,
 * complementing @ref MasteringDisplay which describes the display
 * used during grading.  Together they give downstream tone-mappers
 * the information needed to adapt HDR content to the viewer's
 * display.
 *
 * | Field   | Unit           | Description                                |
 * |---------|----------------|--------------------------------------------|
 * | maxCLL  | cd/m² (nits)   | Maximum Content Light Level — brightest pixel in the entire stream. |
 * | maxFALL | cd/m² (nits)   | Maximum Frame-Average Light Level — brightest per-frame average.   |
 *
 * A default-constructed ContentLightLevel has both fields at zero,
 * which means "unspecified" per CTA-861.3.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — const operations are safe, but
 * concurrent mutation requires external synchronization.
 *
 * @par Example
 * @code
 * ContentLightLevel cll(1000, 400);
 * assert(cll.maxCLL()  == 1000);
 * assert(cll.maxFALL() == 400);
 * @endcode
 */
class ContentLightLevel {
        public:
                ContentLightLevel() = default;

                ContentLightLevel(uint32_t maxCLL, uint32_t maxFALL)
                        : _maxCLL(maxCLL), _maxFALL(maxFALL) {}

                bool isValid() const { return _maxCLL > 0; }

                uint32_t maxCLL() const  { return _maxCLL; }
                uint32_t maxFALL() const { return _maxFALL; }

                void setMaxCLL(uint32_t v)  { _maxCLL = v; }
                void setMaxFALL(uint32_t v) { _maxFALL = v; }

                bool operator==(const ContentLightLevel &o) const {
                        return _maxCLL == o._maxCLL && _maxFALL == o._maxFALL;
                }
                bool operator!=(const ContentLightLevel &o) const { return !(*this == o); }

                String toString() const;

        private:
                uint32_t _maxCLL  = 0;
                uint32_t _maxFALL = 0;
};

PROMEKI_NAMESPACE_END
