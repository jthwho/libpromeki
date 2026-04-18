/**
 * @file      contentlightlevel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/contentlightlevel.h>

PROMEKI_NAMESPACE_BEGIN

String ContentLightLevel::toString() const {
        return String::sprintf("MaxCLL=%u MaxFALL=%u cd/m²", _maxCLL, _maxFALL);
}

PROMEKI_NAMESPACE_END
