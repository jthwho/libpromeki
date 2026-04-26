/**
 * @file      iodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/iodevice.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

IODevice::~IODevice() = default;

void IODevice::flush() {
        // No-op for unbuffered devices
}

int64_t IODevice::bytesAvailable() const {
        return 0;
}

bool IODevice::waitForReadyRead(unsigned int timeoutMs) {
        (void)timeoutMs;
        return false;
}

bool IODevice::waitForBytesWritten(unsigned int timeoutMs) {
        (void)timeoutMs;
        return false;
}

bool IODevice::isSequential() const {
        return false;
}

Error IODevice::seek(int64_t pos) {
        (void)pos;
        return Error(Error::NotSupported);
}

int64_t IODevice::pos() const {
        return 0;
}

Result<int64_t> IODevice::size() const {
        return makeResult<int64_t>(0);
}

bool IODevice::atEnd() const {
        auto [s, err] = size();
        if (err.isError()) return true;
        return pos() >= s;
}

PROMEKI_NAMESPACE_END
