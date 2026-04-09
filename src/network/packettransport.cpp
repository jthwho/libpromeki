/**
 * @file      packettransport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/packettransport.h>

PROMEKI_NAMESPACE_BEGIN

Error PacketTransport::setPacingRate(uint64_t /*bytesPerSec*/) {
        return Error::NotSupported;
}

Error PacketTransport::setTxTime(bool /*enable*/) {
        return Error::NotSupported;
}

PROMEKI_NAMESPACE_END
