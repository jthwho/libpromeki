/**
 * @file      prioritysocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/prioritysocket.h>

PROMEKI_NAMESPACE_BEGIN

PrioritySocket::PrioritySocket(ObjectBase *parent)
        : UdpSocket(parent) { }

PrioritySocket::~PrioritySocket() = default;

Error PrioritySocket::setPriority(Priority p) {
        Error err = setDscp(static_cast<uint8_t>(p));
        if(err.isOk()) _priority = p;
        return err;
}

PROMEKI_NAMESPACE_END
