/**
 * @file      codec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/codec.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// ImageCodec — minimal base class kept as the implementation
// scaffolding behind JpegImageCodec / JpegXsImageCodec, which the
// JpegVideoEncoder / JpegVideoDecoder wrappers delegate to.  The
// public registry surface (registerCodec / createCodec /
// registeredCodecs / PROMEKI_REGISTER_IMAGE_CODEC) was retired in
// task 37 — codec discovery flows through the typed VideoCodec
// registry now.
// ============================================================================

ImageCodec::~ImageCodec() = default;

void ImageCodec::configure(const MediaConfig &config) {
        // Default no-op: codecs without configurable knobs accept any
        // MediaConfig and pick none of its keys.
        (void)config;
}

void ImageCodec::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void ImageCodec::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

// ============================================================================
// VideoEncoder
// ============================================================================

VideoEncoder::~VideoEncoder() = default;

void VideoEncoder::configure(const MediaConfig &config) {
        (void)config;
}

void VideoEncoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void VideoEncoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

Map<String, std::function<VideoEncoder *()>> &VideoEncoder::encoderRegistry() {
        static Map<String, std::function<VideoEncoder *()>> reg;
        return reg;
}

void VideoEncoder::registerEncoder(const String &name,
                                   std::function<VideoEncoder *()> factory) {
        encoderRegistry().insert(name, std::move(factory));
}

VideoEncoder *VideoEncoder::createEncoder(const String &name) {
        auto &reg = encoderRegistry();
        if(!reg.contains(name)) return nullptr;
        return reg[name]();
}

List<String> VideoEncoder::registeredEncoders() {
        List<String> ret;
        for(const auto &[name, factory] : encoderRegistry()) {
                ret.pushToBack(name);
        }
        return ret;
}

// ============================================================================
// VideoDecoder
// ============================================================================

VideoDecoder::~VideoDecoder() = default;

void VideoDecoder::configure(const MediaConfig &config) {
        (void)config;
}

void VideoDecoder::setError(Error err, const String &msg) {
        _lastError = err;
        _lastErrorMessage = msg;
}

void VideoDecoder::clearError() {
        _lastError = Error::Ok;
        _lastErrorMessage = String();
}

Map<String, std::function<VideoDecoder *()>> &VideoDecoder::decoderRegistry() {
        static Map<String, std::function<VideoDecoder *()>> reg;
        return reg;
}

void VideoDecoder::registerDecoder(const String &name,
                                   std::function<VideoDecoder *()> factory) {
        decoderRegistry().insert(name, std::move(factory));
}

VideoDecoder *VideoDecoder::createDecoder(const String &name) {
        auto &reg = decoderRegistry();
        if(!reg.contains(name)) return nullptr;
        return reg[name]();
}

List<String> VideoDecoder::registeredDecoders() {
        List<String> ret;
        for(const auto &[name, factory] : decoderRegistry()) {
                ret.pushToBack(name);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
