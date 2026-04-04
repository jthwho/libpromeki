/**
 * @file      codec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/codec.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// ImageCodec
// ============================================================================

ImageCodec::~ImageCodec() = default;

void ImageCodec::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void ImageCodec::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

Map<String, std::function<ImageCodec *()>> &ImageCodec::codecRegistry() {
        static Map<String, std::function<ImageCodec *()>> reg;
        return reg;
}

void ImageCodec::registerCodec(const String &name, std::function<ImageCodec *()> factory) {
        codecRegistry().insert(name, std::move(factory));
}

ImageCodec *ImageCodec::createCodec(const String &name) {
        auto &reg = codecRegistry();
        if(!reg.contains(name)) return nullptr;
        return reg[name]();
}

List<String> ImageCodec::registeredCodecs() {
        List<String> ret;
        for(const auto &[name, factory] : codecRegistry()) {
                ret.pushToBack(name);
        }
        return ret;
}

// ============================================================================
// AudioCodec
// ============================================================================

AudioCodec::~AudioCodec() = default;

PROMEKI_NAMESPACE_END
