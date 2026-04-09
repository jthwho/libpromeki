/**
 * @file      audiobuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <promeki/audiobuffer.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioBuffer::AudioBuffer(const AudioDesc &format) :
        _format(format), _inputFormat(format) {}

AudioBuffer::AudioBuffer(const AudioDesc &format, size_t capacity) :
        _format(format), _inputFormat(format)
{
        reserve(capacity);
}

AudioBuffer::AudioBuffer(AudioBuffer &&other) noexcept :
        _format(other._format),
        _inputFormat(other._inputFormat),
        _storage(std::move(other._storage)),
        _capacity(other._capacity),
        _head(other._head),
        _tail(other._tail),
        _count(other._count)
{
        other._capacity = 0;
        other._head = 0;
        other._tail = 0;
        other._count = 0;
}

AudioBuffer &AudioBuffer::operator=(AudioBuffer &&other) noexcept {
        if(this == &other) return *this;
        _format       = other._format;
        _inputFormat  = other._inputFormat;
        _storage      = std::move(other._storage);
        _capacity     = other._capacity;
        _head         = other._head;
        _tail         = other._tail;
        _count        = other._count;
        other._capacity = 0;
        other._head     = 0;
        other._tail     = 0;
        other._count    = 0;
        return *this;
}

void AudioBuffer::setFormat(const AudioDesc &format) {
        _format      = format;
        _inputFormat = format;
        clear();
        _storage  = Buffer();
        _capacity = 0;
}

void AudioBuffer::setInputFormat(const AudioDesc &input) {
        _inputFormat = input;
}

// ---------------------------------------------------------------------------
// Capacity
// ---------------------------------------------------------------------------

size_t AudioBuffer::bytesPerSample() const {
        if(!_format.isValid()) return 0;
        return _format.bytesPerSampleStride();
}

Error AudioBuffer::reserve(size_t samples) {
        if(!_format.isValid()) return Error::InvalidArgument;
        if(samples <= _capacity) return Error::Ok;
        if(samples < _count)     return Error::InvalidArgument;

        size_t bps = bytesPerSample();
        Buffer newStorage(samples * bps);
        if(!newStorage.isValid()) return Error::NoMem;

        // Linearize existing contents into the new storage at index 0.
        uint8_t *dst = static_cast<uint8_t *>(newStorage.data());
        if(_count > 0) {
                readBytesFromHead(dst, _count, 0);
        }
        newStorage.setSize(samples * bps);

        _storage  = std::move(newStorage);
        _capacity = samples;
        _head     = 0;
        _tail     = _count;
        return Error::Ok;
}

void AudioBuffer::clear() {
        _head  = 0;
        _tail  = 0;
        _count = 0;
}

// ---------------------------------------------------------------------------
// Internal ring reads/writes (samples-aware, wrap-around-aware)
// ---------------------------------------------------------------------------

void AudioBuffer::writeBytesAtTail(const uint8_t *data, size_t samples) {
        if(samples == 0) return;
        size_t bps = bytesPerSample();
        uint8_t *base = static_cast<uint8_t *>(_storage.data());

        size_t firstChunk = samples;
        if(_tail + samples > _capacity) firstChunk = _capacity - _tail;
        std::memcpy(base + _tail * bps, data, firstChunk * bps);
        size_t remainder = samples - firstChunk;
        if(remainder > 0) {
                std::memcpy(base, data + firstChunk * bps, remainder * bps);
        }
        _tail = (_tail + samples) % _capacity;
        _count += samples;
}

void AudioBuffer::readBytesFromHead(uint8_t *dst, size_t samples, size_t skip) const {
        if(samples == 0) return;
        size_t bps = bytesPerSample();
        const uint8_t *base = static_cast<const uint8_t *>(_storage.data());
        size_t start = (_head + skip) % _capacity;

        size_t firstChunk = samples;
        if(start + samples > _capacity) firstChunk = _capacity - start;
        std::memcpy(dst, base + start * bps, firstChunk * bps);
        size_t remainder = samples - firstChunk;
        if(remainder > 0) {
                std::memcpy(dst + firstChunk * bps, base, remainder * bps);
        }
}

// ---------------------------------------------------------------------------
// Push
// ---------------------------------------------------------------------------

Error AudioBuffer::push(const Audio &audio) {
        if(!audio.isValid()) return Error::InvalidArgument;
        return push(audio.buffer()->data(), audio.samples(), audio.desc());
}

Error AudioBuffer::push(const void *data, size_t samples, const AudioDesc &srcFormat) {
        if(!_format.isValid() || !srcFormat.isValid()) return Error::InvalidArgument;
        if(data == nullptr && samples > 0)            return Error::InvalidArgument;
        if(samples == 0)                              return Error::Ok;

        // Sample rate + channel count must match. A resampler / channel-map
        // hook is reserved for a follow-up; this path returns NotSupported
        // with a clear diagnostic so the caller knows what's missing.
        if(srcFormat.sampleRate() != _format.sampleRate()) {
                promekiWarn("AudioBuffer: sample rate mismatch (%.1f → %.1f) "
                            "— resampling not yet implemented",
                            srcFormat.sampleRate(), _format.sampleRate());
                return Error::NotSupported;
        }
        if(srcFormat.channels() != _format.channels()) {
                promekiWarn("AudioBuffer: channel count mismatch (%u → %u) "
                            "— channel-map not yet implemented",
                            srcFormat.channels(), _format.channels());
                return Error::NotSupported;
        }

        if(_capacity - _count < samples) return Error::NoSpace;

        // Fast path: formats match → direct memcpy.
        if(srcFormat.dataType() == _format.dataType()) {
                writeBytesAtTail(static_cast<const uint8_t *>(data), samples);
                return Error::Ok;
        }

        // Conversion path: src → native float → dst. For the storage format
        // to end up right, we convert one stride at a time using a stack
        // scratch buffer (bounded in size by samples × channels × sizeof(float)).
        // For large pushes we fall back to heap allocation to avoid stack
        // pressure.
        size_t totalFloats = samples * _format.channels();
        const size_t kStackFloats = 4096;
        float  stackBuf[kStackFloats];
        float *scratch = stackBuf;
        float *heapScratch = nullptr;
        if(totalFloats > kStackFloats) {
                heapScratch = static_cast<float *>(std::malloc(totalFloats * sizeof(float)));
                if(heapScratch == nullptr) return Error::NoMem;
                scratch = heapScratch;
        }

        // Step 1: srcFormat bytes → native float.
        srcFormat.samplesToFloat(scratch, static_cast<const uint8_t *>(data), samples);

        // Step 2: native float → destination format bytes (possibly in two
        // passes if we wrap around the ring buffer).
        size_t bps = bytesPerSample();
        uint8_t *base = static_cast<uint8_t *>(_storage.data());

        size_t firstChunkSamples = samples;
        if(_tail + samples > _capacity) firstChunkSamples = _capacity - _tail;
        size_t remainderSamples  = samples - firstChunkSamples;

        if(firstChunkSamples > 0) {
                _format.floatToSamples(base + _tail * bps, scratch, firstChunkSamples);
        }
        if(remainderSamples > 0) {
                _format.floatToSamples(base,
                                       scratch + firstChunkSamples * _format.channels(),
                                       remainderSamples);
        }

        _tail = (_tail + samples) % _capacity;
        _count += samples;

        if(heapScratch != nullptr) std::free(heapScratch);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Pop / peek / drop
// ---------------------------------------------------------------------------

size_t AudioBuffer::pop(Audio &audio, size_t samples) {
        if(!audio.isValid()) return 0;
        if(audio.desc().dataType() != _format.dataType() ||
           audio.desc().sampleRate() != _format.sampleRate() ||
           audio.desc().channels() != _format.channels()) {
                promekiWarn("AudioBuffer: pop format mismatch");
                return 0;
        }
        if(samples > audio.maxSamples()) samples = audio.maxSamples();
        size_t got = pop(audio.buffer()->data(), samples);
        audio.resize(got);
        return got;
}

size_t AudioBuffer::pop(void *dst, size_t samples) {
        if(_count == 0 || samples == 0 || dst == nullptr) return 0;
        size_t toRead = samples > _count ? _count : samples;
        readBytesFromHead(static_cast<uint8_t *>(dst), toRead, 0);
        _head = (_head + toRead) % _capacity;
        _count -= toRead;
        return toRead;
}

size_t AudioBuffer::peek(void *dst, size_t samples) const {
        if(_count == 0 || samples == 0 || dst == nullptr) return 0;
        size_t toRead = samples > _count ? _count : samples;
        readBytesFromHead(static_cast<uint8_t *>(dst), toRead, 0);
        return toRead;
}

size_t AudioBuffer::drop(size_t samples) {
        if(_count == 0 || samples == 0) return 0;
        size_t toDrop = samples > _count ? _count : samples;
        _head = (_head + toDrop) % _capacity;
        _count -= toDrop;
        return toDrop;
}

PROMEKI_NAMESPACE_END
