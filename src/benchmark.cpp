/**
 * @file      benchmark.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/benchmark.h>

PROMEKI_NAMESPACE_BEGIN

double Benchmark::duration(Id fromId, Id toId) const {
        TimeStamp fromTs;
        TimeStamp toTs;
        bool foundFrom = false;
        bool foundTo = false;
        for(const auto &entry : _entries) {
                if(!foundFrom && entry.id == fromId) {
                        fromTs = entry.timestamp;
                        foundFrom = true;
                }
                if(!foundTo && entry.id == toId) {
                        toTs = entry.timestamp;
                        foundTo = true;
                }
                if(foundFrom && foundTo) break;
        }
        if(!foundFrom || !foundTo) return 0.0;
        auto diff = toTs.value() - fromTs.value();
        return std::chrono::duration<double>(diff).count();
}

PROMEKI_NAMESPACE_END
