/**
 * @file      style.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/style.h>

PROMEKI_NAMESPACE_BEGIN

TuiStyle TuiStyle::merged(const TuiStyle &below) const {
        TuiStyle result;
        result._fg = hasForeground() ? _fg : below._fg;
        result._bg = hasBackground() ? _bg : below._bg;
        result._attrs = (_attrs & _attrMask) | (below._attrs & ~_attrMask);
        result._attrMask = _attrMask | below._attrMask;
        return result;
}

PROMEKI_NAMESPACE_END
