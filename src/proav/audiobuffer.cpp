/**
 * @file      audiobuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <promeki/config.h>
#include <promeki/audiobuffer.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioBuffer::AudioBuffer(const AudioDesc &format) : _format(format), _inputFormat(format) {}

AudioBuffer::AudioBuffer(const AudioDesc &format, size_t capacity) : _format(format), _inputFormat(format) {
        reserve(capacity);
}

AudioBuffer::AudioBuffer(AudioBuffer &&other) noexcept
    : _format(other._format), _inputFormat(other._inputFormat), _storage(std::move(other._storage)),
      _capacity(other._capacity), _head(other._head), _tail(other._tail), _count(other._count) {
        other._capacity = 0;
        other._head = 0;
        other._tail = 0;
        other._count = 0;
}

AudioBuffer &AudioBuffer::operator=(AudioBuffer &&other) noexcept {
        if (this == &other) return *this;
        _format = other._format;
        _inputFormat = other._inputFormat;
        _storage = std::move(other._storage);
        _capacity = other._capacity;
        _head = other._head;
        _tail = other._tail;
        _count = other._count;
        other._capacity = 0;
        other._head = 0;
        other._tail = 0;
        other._count = 0;
        return *this;
}

void AudioBuffer::setFormat(const AudioDesc &format) {
        Mutex::Locker lock(_mutex);
        _format = format;
        _inputFormat = format;
        _head = 0;
        _tail = 0;
        _count = 0;
        _storage = Buffer();
        _capacity = 0;
}

void AudioBuffer::setInputFormat(const AudioDesc &input) {
        Mutex::Locker lock(_mutex);
        _inputFormat = input;
#if PROMEKI_ENABLE_SRC
        if (_resampler.isValid()) _resampler.reset();
#endif
}

Error AudioBuffer::setResamplerQuality(const SrcQuality &quality) {
#if PROMEKI_ENABLE_SRC
        Mutex::Locker lock(_mutex);
        _resamplerQuality = quality;
        if (_resampler.isValid()) {
                _resampler.setup(_format.channels(), _resamplerQuality);
        }
        return Error::Ok;
#else
        (void)quality;
        promekiWarn("AudioBuffer: setResamplerQuality() not available (PROMEKI_ENABLE_SRC=OFF)");
        return Error::NotSupported;
#endif
}

Error AudioBuffer::enableDriftCorrection(size_t targetSamples, double gain) {
#if PROMEKI_ENABLE_SRC
        Mutex::Locker lock(_mutex);
        _driftEnabled = true;
        _driftTarget = targetSamples;
        _driftGain = gain;
        _driftRatio = 1.0;
        _driftIntegral = 0.0;
        if (_resampler.isValid()) _resampler.reset();
        return Error::Ok;
#else
        (void)targetSamples;
        (void)gain;
        promekiWarn("AudioBuffer: enableDriftCorrection() not available (PROMEKI_ENABLE_SRC=OFF)");
        return Error::NotSupported;
#endif
}

void AudioBuffer::disableDriftCorrection() {
#if PROMEKI_ENABLE_SRC
        Mutex::Locker lock(_mutex);
        _driftEnabled = false;
        _driftRatio = 1.0;
        _driftIntegral = 0.0;
        if (_resampler.isValid()) _resampler.reset();
#endif
}

bool AudioBuffer::driftCorrectionEnabled() const {
#if PROMEKI_ENABLE_SRC
        Mutex::Locker lock(_mutex);
        return _driftEnabled;
#else
        return false;
#endif
}

double AudioBuffer::driftRatio() const {
#if PROMEKI_ENABLE_SRC
        Mutex::Locker lock(_mutex);
        return _driftRatio;
#else
        return 1.0;
#endif
}

#if PROMEKI_ENABLE_SRC
bool AudioBuffer::needsResampler(const AudioDesc &srcFormat) const {
        if (_driftEnabled) return true;
        return srcFormat.sampleRate() != _format.sampleRate();
}

double AudioBuffer::computeRatio(const AudioDesc &srcFormat) {
        double nominal = static_cast<double>(_format.sampleRate()) / static_cast<double>(srcFormat.sampleRate());
        if (!_driftEnabled || _driftTarget == 0) return nominal;

        double error =
                (static_cast<double>(_count) - static_cast<double>(_driftTarget)) / static_cast<double>(_driftTarget);

        // PI controller: integral accumulates error to eliminate
        // steady-state offset from constant clock drift.
        // Ki = Kp / 1000 — slow integrator avoids overshoot.
        _driftIntegral += error;

        // Clamp integral to prevent windup during large transients.
        const double kMaxIntegral = 1000.0;
        if (_driftIntegral > kMaxIntegral) _driftIntegral = kMaxIntegral;
        if (_driftIntegral < -kMaxIntegral) _driftIntegral = -kMaxIntegral;

        double ki = _driftGain * 0.001;
        return nominal * (1.0 - _driftGain * error - ki * _driftIntegral);
}
#endif

// ---------------------------------------------------------------------------
// Capacity
// ---------------------------------------------------------------------------

size_t AudioBuffer::bytesPerSample() const {
        if (!_format.isValid()) return 0;
        return _format.bytesPerSampleStride();
}

Error AudioBuffer::reserve(size_t samples) {
        Mutex::Locker lock(_mutex);
        if (!_format.isValid()) return Error::InvalidArgument;
        if (samples <= _capacity) return Error::Ok;
        if (samples < _count) return Error::InvalidArgument;

        size_t bps = bytesPerSample();
        Buffer newStorage(samples * bps);
        if (!newStorage.isValid()) return Error::NoMem;

        // Linearize existing contents into the new storage at index 0.
        uint8_t *dst = static_cast<uint8_t *>(newStorage.data());
        if (_count > 0) {
                readBytesFromHead(dst, _count, 0);
        }
        newStorage.setSize(samples * bps);

        _storage = std::move(newStorage);
        _capacity = samples;
        _head = 0;
        _tail = _count;
        return Error::Ok;
}

void AudioBuffer::clear() {
        Mutex::Locker lock(_mutex);
        _head = 0;
        _tail = 0;
        _count = 0;
}

// ---------------------------------------------------------------------------
// Internal ring reads/writes (samples-aware, wrap-around-aware)
// ---------------------------------------------------------------------------

void AudioBuffer::writeBytesAtTail(const uint8_t *data, size_t samples) {
        if (samples == 0) return;
        size_t   bps = bytesPerSample();
        uint8_t *base = static_cast<uint8_t *>(_storage.data());

        size_t firstChunk = samples;
        if (_tail + samples > _capacity) firstChunk = _capacity - _tail;
        std::memcpy(base + _tail * bps, data, firstChunk * bps);
        size_t remainder = samples - firstChunk;
        if (remainder > 0) {
                std::memcpy(base, data + firstChunk * bps, remainder * bps);
        }
        _tail = (_tail + samples) % _capacity;
        _count += samples;
}

void AudioBuffer::readBytesFromHead(uint8_t *dst, size_t samples, size_t skip) const {
        if (samples == 0) return;
        size_t         bps = bytesPerSample();
        const uint8_t *base = static_cast<const uint8_t *>(_storage.data());
        size_t         start = (_head + skip) % _capacity;

        size_t firstChunk = samples;
        if (start + samples > _capacity) firstChunk = _capacity - start;
        std::memcpy(dst, base + start * bps, firstChunk * bps);
        size_t remainder = samples - firstChunk;
        if (remainder > 0) {
                std::memcpy(dst + firstChunk * bps, base, remainder * bps);
        }
}

// ---------------------------------------------------------------------------
// Lock-free internals (caller holds _mutex)
// ---------------------------------------------------------------------------

#if PROMEKI_ENABLE_SRC
Error AudioBuffer::resampleAndPush(const float *nativeData, size_t samples) {
        // Lazy-init the resampler.
        if (!_resampler.isValid()) {
                Error err = _resampler.setup(_format.channels(), _resamplerQuality);
                if (err.isError()) return err;
        }

        double ratio = computeRatio(_inputFormat);
        _driftRatio = ratio;
        Error err = _resampler.setRatio(ratio);
        if (err.isError()) return err;

        // Estimate output size: ceil(input * ratio) + small margin for
        // the sinc filter tail.
        size_t estOutput = static_cast<size_t>(static_cast<double>(samples) * ratio + 32.0);

        // Check capacity before allocating.
        if (_capacity - _count < estOutput) return Error::NoSpace;

        // Allocate scratch for resampled native-float output.
        size_t       totalFloats = estOutput * _format.channels();
        const size_t kStackFloats = 4096;
        float        stackBuf[kStackFloats];
        float       *scratch = stackBuf;
        float       *heapScratch = nullptr;
        if (totalFloats > kStackFloats) {
                heapScratch = static_cast<float *>(std::malloc(totalFloats * sizeof(float)));
                if (heapScratch == nullptr) return Error::NoMem;
                scratch = heapScratch;
        }

        long inputUsed = 0;
        long outputGen = 0;
        err = _resampler.process(nativeData, static_cast<long>(samples), scratch, static_cast<long>(estOutput),
                                 inputUsed, outputGen, false);
        if (err.isError()) {
                if (heapScratch != nullptr) std::free(heapScratch);
                return err;
        }

        size_t outSamples = static_cast<size_t>(outputGen);
        if (outSamples > 0) {
                if (_capacity - _count < outSamples) {
                        if (heapScratch != nullptr) std::free(heapScratch);
                        return Error::NoSpace;
                }

                if (_format.isNative()) {
                        // Direct write: resampled float -> ring.
                        writeBytesAtTail(reinterpret_cast<const uint8_t *>(scratch), outSamples);
                } else {
                        // Convert resampled float -> storage format -> ring.
                        size_t   bps = bytesPerSample();
                        uint8_t *base = static_cast<uint8_t *>(_storage.data());

                        size_t firstChunk = outSamples;
                        if (_tail + outSamples > _capacity) firstChunk = _capacity - _tail;
                        size_t remainder = outSamples - firstChunk;

                        if (firstChunk > 0) {
                                _format.floatToSamples(base + _tail * bps, scratch, firstChunk);
                        }
                        if (remainder > 0) {
                                _format.floatToSamples(base, scratch + firstChunk * _format.channels(), remainder);
                        }
                        _tail = (_tail + outSamples) % _capacity;
                        _count += outSamples;
                }
        }

        if (heapScratch != nullptr) std::free(heapScratch);
        return Error::Ok;
}
#endif

Error AudioBuffer::pushLocked(const void *data, size_t samples, const AudioDesc &srcFormat) {
        if (!_format.isValid() || !srcFormat.isValid()) return Error::InvalidArgument;
        if (data == nullptr && samples > 0) return Error::InvalidArgument;
        if (samples == 0) return Error::Ok;

        if (srcFormat.channels() != _format.channels()) {
                promekiWarn("AudioBuffer: channel count mismatch (%u -> %u) "
                            "-- channel-map not yet implemented",
                            srcFormat.channels(), _format.channels());
                return Error::NotSupported;
        }

#if PROMEKI_ENABLE_SRC
        // Resampler path: rates differ (fixed conversion) or drift
        // correction is enabled (dynamic ratio adjustment).
        if (needsResampler(srcFormat)) {
                // Convert input to native float, then resample.
                const float *nativeData;
                float       *heapFloat = nullptr;
                if (srcFormat.isNative()) {
                        nativeData = static_cast<const float *>(data);
                } else {
                        size_t totalFloats = samples * srcFormat.channels();
                        heapFloat = static_cast<float *>(std::malloc(totalFloats * sizeof(float)));
                        if (heapFloat == nullptr) return Error::NoMem;
                        srcFormat.samplesToFloat(heapFloat, static_cast<const uint8_t *>(data), samples);
                        nativeData = heapFloat;
                }
                Error err = resampleAndPush(nativeData, samples);
                if (heapFloat != nullptr) std::free(heapFloat);
                return err;
        }
#else
        if (srcFormat.sampleRate() != _format.sampleRate()) {
                promekiWarn("AudioBuffer: sample rate mismatch (%.1f -> %.1f) "
                            "-- resampling not available (PROMEKI_ENABLE_SRC=OFF)",
                            srcFormat.sampleRate(), _format.sampleRate());
                return Error::NotSupported;
        }
#endif

        if (_capacity - _count < samples) return Error::NoSpace;

        // Fast path: formats match -> direct memcpy.
        if (srcFormat.format().id() == _format.format().id()) {
                writeBytesAtTail(static_cast<const uint8_t *>(data), samples);
                return Error::Ok;
        }

        // Conversion path: src -> native float -> dst.
        size_t       totalFloats = samples * _format.channels();
        const size_t kStackFloats = 4096;
        float        stackBuf[kStackFloats];
        float       *scratch = stackBuf;
        float       *heapScratch = nullptr;
        if (totalFloats > kStackFloats) {
                heapScratch = static_cast<float *>(std::malloc(totalFloats * sizeof(float)));
                if (heapScratch == nullptr) return Error::NoMem;
                scratch = heapScratch;
        }

        // Step 1: srcFormat bytes -> native float.
        srcFormat.samplesToFloat(scratch, static_cast<const uint8_t *>(data), samples);

        // Step 2: native float -> destination format bytes.
        size_t   bps = bytesPerSample();
        uint8_t *base = static_cast<uint8_t *>(_storage.data());

        size_t firstChunkSamples = samples;
        if (_tail + samples > _capacity) firstChunkSamples = _capacity - _tail;
        size_t remainderSamples = samples - firstChunkSamples;

        if (firstChunkSamples > 0) {
                _format.floatToSamples(base + _tail * bps, scratch, firstChunkSamples);
        }
        if (remainderSamples > 0) {
                _format.floatToSamples(base, scratch + firstChunkSamples * _format.channels(), remainderSamples);
        }

        _tail = (_tail + samples) % _capacity;
        _count += samples;

        if (heapScratch != nullptr) std::free(heapScratch);
        return Error::Ok;
}

size_t AudioBuffer::popLocked(void *dst, size_t samples) {
        if (_count == 0 || samples == 0 || dst == nullptr) return 0;
        size_t toRead = samples > _count ? _count : samples;
        readBytesFromHead(static_cast<uint8_t *>(dst), toRead, 0);
        _head = (_head + toRead) % _capacity;
        _count -= toRead;
        return toRead;
}

// ---------------------------------------------------------------------------
// Push (public, locked)
// ---------------------------------------------------------------------------

Error AudioBuffer::push(const void *data, size_t samples, const AudioDesc &srcFormat) {
        Mutex::Locker lock(_mutex);
        Error         err = pushLocked(data, samples, srcFormat);
        if (err.isOk() && samples > 0) _cv.wakeOne();
        return err;
}

// ---------------------------------------------------------------------------
// Pop (public, locked, non-blocking)
// ---------------------------------------------------------------------------

AudioBuffer::PopResult AudioBuffer::pop(void *dst, size_t samples) {
        Mutex::Locker lock(_mutex);
        size_t        got = popLocked(dst, samples);
        return makeResult(got);
}

// ---------------------------------------------------------------------------
// popWait (public, locked, blocking)
// ---------------------------------------------------------------------------

AudioBuffer::PopResult AudioBuffer::popWait(void *dst, size_t samples, unsigned int timeoutMs) {
        Mutex::Locker lock(_mutex);
        if (_count < samples) {
                Error waitErr = _cv.wait(_mutex, [&]() { return _count >= samples; }, timeoutMs);
                if (waitErr != Error::Ok) return PopResult(0, waitErr);
        }
        size_t got = popLocked(dst, samples);
        return makeResult(got);
}

// ---------------------------------------------------------------------------
// Peek / drop (public, locked)
// ---------------------------------------------------------------------------

AudioBuffer::PopResult AudioBuffer::peek(void *dst, size_t samples) const {
        Mutex::Locker lock(_mutex);
        if (_count == 0 || samples == 0 || dst == nullptr) return makeResult<size_t>(0);
        size_t toRead = samples > _count ? _count : samples;
        readBytesFromHead(static_cast<uint8_t *>(dst), toRead, 0);
        return makeResult(toRead);
}

AudioBuffer::PopResult AudioBuffer::drop(size_t samples) {
        Mutex::Locker lock(_mutex);
        if (_count == 0 || samples == 0) return makeResult<size_t>(0);
        size_t toDrop = samples > _count ? _count : samples;
        _head = (_head + toDrop) % _capacity;
        _count -= toDrop;
        return makeResult(toDrop);
}

PROMEKI_NAMESPACE_END
