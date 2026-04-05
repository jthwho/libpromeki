/**
 * @file      cscregistry.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cscregistry.h>
#include <promeki/map.h>
#include <promeki/pair.h>

PROMEKI_NAMESPACE_BEGIN

using FastPathKey = Pair<int, int>;

static Map<FastPathKey, CSCRegistry::LineFuncPtr> &registry() {
        static Map<FastPathKey, CSCRegistry::LineFuncPtr> r;
        return r;
}

void CSCRegistry::registerFastPath(const PixelDesc &src, const PixelDesc &dst, LineFuncPtr func) {
        registry()[FastPathKey(static_cast<int>(src.id()), static_cast<int>(dst.id()))] = func;
        return;
}

CSCRegistry::LineFuncPtr CSCRegistry::lookupFastPath(const PixelDesc &src, const PixelDesc &dst) {
        auto &r = registry();
        FastPathKey key(static_cast<int>(src.id()), static_cast<int>(dst.id()));
        if(r.contains(key)) return r[key];
        return nullptr;
}

PROMEKI_NAMESPACE_END
