/**
 * @file      iodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/iodevice.h>

PROMEKI_NAMESPACE_BEGIN

static Atomic<IODevice::Option> _nextOptionType{1};

IODevice::Option IODevice::registerOptionType() {
        return _nextOptionType.fetchAndAdd(1);
}

const IODevice::Option IODevice::DirectIO      = IODevice::registerOptionType();
const IODevice::Option IODevice::Synchronous   = IODevice::registerOptionType();
const IODevice::Option IODevice::NonBlocking   = IODevice::registerOptionType();
const IODevice::Option IODevice::Unbuffered    = IODevice::registerOptionType();

IODevice::~IODevice() = default;

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

bool IODevice::seek(int64_t pos) {
        (void)pos;
        return false;
}

int64_t IODevice::pos() const {
        return 0;
}

int64_t IODevice::size() const {
        return 0;
}

bool IODevice::atEnd() const {
        return pos() >= size();
}

void IODevice::registerOption(Option opt, const Variant &defaultValue) {
        _options[opt] = defaultValue;
        return;
}

Error IODevice::setOption(Option opt, const Variant &value) {
        auto it = _options.find(opt);
        if(it == _options.end()) return Error(Error::NotSupported);
        it->second = value;
        onOptionChanged(opt, value);
        return Error();
}

Result<Variant> IODevice::option(Option opt) const {
        auto it = _options.find(opt);
        if(it == _options.end()) return makeError<Variant>(Error::NotSupported);
        return makeResult(it->second);
}

bool IODevice::optionSupported(Option opt) const {
        return _options.find(opt) != _options.end();
}

void IODevice::onOptionChanged(Option opt, const Variant &value) {
        (void)opt;
        (void)value;
        return;
}

PROMEKI_NAMESPACE_END
