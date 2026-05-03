/**
 * @file      audiobuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
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
      _capacity(other._capacity), _head(other._head), _tail(other._tail), _count(other._count),
      _gains(std::move(other._gains)), _remap(std::move(other._remap)), _meter(std::move(other._meter))
#if PROMEKI_ENABLE_SRC
      ,
      _resamplerQuality(other._resamplerQuality), _resampler(std::move(other._resampler)),
      _driftEnabled(other._driftEnabled), _driftTarget(other._driftTarget), _driftGain(other._driftGain),
      _driftRatio(other._driftRatio), _driftIntegral(other._driftIntegral)
#endif
{
        other._capacity = 0;
        other._head = 0;
        other._tail = 0;
        other._count = 0;
#if PROMEKI_ENABLE_SRC
        other._driftEnabled = false;
        other._driftTarget = 0;
        other._driftGain = 0.001;
        other._driftRatio = 1.0;
        other._driftIntegral = 0.0;
#endif
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
        _gains = std::move(other._gains);
        _remap = std::move(other._remap);
        _meter = std::move(other._meter);
#if PROMEKI_ENABLE_SRC
        _resamplerQuality = other._resamplerQuality;
        _resampler = std::move(other._resampler);
        _driftEnabled = other._driftEnabled;
        _driftTarget = other._driftTarget;
        _driftGain = other._driftGain;
        _driftRatio = other._driftRatio;
        _driftIntegral = other._driftIntegral;
        other._driftEnabled = false;
        other._driftTarget = 0;
        other._driftGain = 0.001;
        other._driftRatio = 1.0;
        other._driftIntegral = 0.0;
#endif
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

// ---------------------------------------------------------------------------
// Remap / gain / meter configuration
// ---------------------------------------------------------------------------

Error AudioBuffer::setChannelGains(const List<float> &gains) {
        Mutex::Locker lock(_mutex);
        if (gains.isEmpty()) {
                _gains.clear();
                return Error::Ok;
        }
        if (gains.size() != _format.channels()) return Error::InvalidArgument;
        _gains = gains;
        return Error::Ok;
}

List<float> AudioBuffer::channelGains() const {
        Mutex::Locker lock(_mutex);
        return _gains;
}

Error AudioBuffer::setChannelRemap(const List<int> &remap) {
        Mutex::Locker lock(_mutex);
        if (remap.isEmpty()) {
                _remap.clear();
                return Error::Ok;
        }
        if (remap.size() != _format.channels()) return Error::InvalidArgument;
        _remap = remap;
        return Error::Ok;
}

List<int> AudioBuffer::channelRemap() const {
        Mutex::Locker lock(_mutex);
        return _remap;
}

void AudioBuffer::setMeter(AudioMeter::Ptr meter) {
        Mutex::Locker lock(_mutex);
        _meter = std::move(meter);
}

AudioMeter::Ptr AudioBuffer::meter() const {
        Mutex::Locker lock(_mutex);
        return _meter;
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
        Buffer       heapScratch;
        if (totalFloats > kStackFloats) {
                heapScratch = Buffer(totalFloats * sizeof(float));
                if (!heapScratch.isValid()) return Error::NoMem;
                scratch = static_cast<float *>(heapScratch.data());
        }

        long inputUsed = 0;
        long outputGen = 0;
        err = _resampler.process(nativeData, static_cast<long>(samples), scratch, static_cast<long>(estOutput),
                                 inputUsed, outputGen, false);
        if (err.isError()) return err;

        size_t outSamples = static_cast<size_t>(outputGen);
        if (outSamples > 0) {
                if (_capacity - _count < outSamples) return Error::NoSpace;

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

        return Error::Ok;
}
#endif

Error AudioBuffer::pushLocked(const void *data, size_t samples, const AudioDesc &srcFormat) {
        if (!_format.isValid() || !srcFormat.isValid()) return Error::InvalidArgument;
        if (data == nullptr && samples > 0) return Error::InvalidArgument;
        if (samples == 0) return Error::Ok;

        // A channel-count mismatch is fine when a remap is installed —
        // the remap explains how the input maps into the output.
        // Without a remap, the legacy contract still applies.
        if (srcFormat.channels() != _format.channels() && _remap.isEmpty()) {
                promekiWarn("AudioBuffer: channel count mismatch (%u -> %u) "
                            "-- install a channel remap to handle this",
                            srcFormat.channels(), _format.channels());
                return Error::NotSupported;
        }

        // Refresh the output channel map from the input map.  The
        // buffer is opaque to stream/role semantics — it just copies
        // each output channel's (stream, role) pair from the source
        // channel indicated by the remap (or 1-to-1 when no remap is
        // installed).  Out-of-range remap entries (-1 or pointing past
        // the source) yield (Undefined, Unused).  Done here, before
        // the resampler / fastpath branches, so every successful push
        // path keeps the output descriptor in sync.  The candidate map
        // is compared against the current one and only assigned when
        // it actually differs, so steady-state pushes don't churn the
        // descriptor and most reads through @ref format() see a
        // stable snapshot.
        {
                const AudioChannelMap            &srcMap = srcFormat.channelMap();
                const AudioChannelMap::EntryList &srcEntries = srcMap.entries();
                const size_t                      outCh = _format.channels();
                AudioChannelMap::EntryList        outEntries;
                outEntries.reserve(outCh);
                for (size_t c = 0; c < outCh; ++c) {
                        int srcIdx = _remap.isEmpty() ? static_cast<int>(c) : _remap[c];
                        if (srcIdx < 0 || static_cast<size_t>(srcIdx) >= srcEntries.size()) {
                                outEntries.pushToBack(AudioChannelMap::Entry(AudioStreamDesc(), ChannelRole::Unused));
                        } else {
                                outEntries.pushToBack(srcEntries[srcIdx]);
                        }
                }
                AudioChannelMap candidate(std::move(outEntries));
                if (candidate != _format.channelMap()) _format.setChannelMap(candidate);
        }

#if PROMEKI_ENABLE_SRC
        // Resampler path: rates differ (fixed conversion) or drift
        // correction is enabled (dynamic ratio adjustment).
        if (needsResampler(srcFormat)) {
                // Convert input to interleaved native float, then resample.
                // Channel-aware convertTo() handles the planar↔interleaved
                // transpose for planar @p srcFormat; the resampler always
                // wants interleaved float input.
                const float *nativeData;
                Buffer       nativeScratch;
                if (srcFormat.isNative()) {
                        nativeData = static_cast<const float *>(data);
                } else {
                        const AudioFormat nativeFloat(AudioFormat::NativeFloat);
                        size_t            totalFloats = samples * srcFormat.channels();
                        nativeScratch = Buffer(totalFloats * sizeof(float));
                        if (!nativeScratch.isValid()) return Error::NoMem;
                        float *heapFloat = static_cast<float *>(nativeScratch.data());
                        Error  err = srcFormat.format().convertTo(nativeFloat, heapFloat, data, samples,
                                                                  srcFormat.channels(), heapFloat);
                        if (err.isError()) return err;
                        nativeData = heapFloat;
                }
                return resampleAndPush(nativeData, samples);
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

        const bool processing = needsFloatProcessing();

        // Fast path: formats match and no float-domain processing is
        // active -> direct memcpy.  Skipped when remap, gain, or meter
        // are configured because they all expect to see post-process
        // float samples.
        if (!processing && srcFormat.format().id() == _format.format().id()) {
                writeBytesAtTail(static_cast<const uint8_t *>(data), samples);
                return Error::Ok;
        }

        // No-float-processing fast path: route through
        // @ref AudioFormat::convertTo's channel-aware overload.  That
        // covers (in priority order):
        //   - same-layout direct converters     — one memory pass.
        //   - cross-layout byte transpose       — one memory pass,
        //     bit-accurate, when src and dst share the same byte
        //     pattern and differ only in planar vs interleaved.
        //   - via-float fallback                — two passes through
        //     a float scratch buffer.
        //
        // Restricted to an interleaved dst because the ring math
        // assumes one slot = one frame.  Planar dst storage is not
        // currently supported by the ring layer regardless.
        if (!processing && !_format.format().isPlanar()) {
                const size_t channels = _format.channels();
                const size_t dstBpf = _format.format().bytesPerSample() * channels;

                // Float scratch for the via-float fallback inside
                // convertTo.  The direct/byte-transpose paths leave it
                // untouched.  Stack-allocate up to kStackFloats; spill
                // to the heap above that.
                const size_t kStackFloats = 4096;
                float        stackScratch[kStackFloats];
                float       *floatScratch = stackScratch;
                Buffer       heapScratch;
                const size_t scratchFloats = samples * channels;
                if (scratchFloats > kStackFloats) {
                        heapScratch = Buffer(scratchFloats * sizeof(float));
                        if (!heapScratch.isValid()) return Error::NoMem;
                        floatScratch = static_cast<float *>(heapScratch.data());
                }

                uint8_t *base = static_cast<uint8_t *>(_storage.data());

                if (srcFormat.format().isPlanar() && channels > 1) {
                        // Planar src can't be split at frame boundaries
                        // (per-channel runs are non-adjacent in src), so
                        // convert the whole batch into a temp interleaved
                        // buffer first, then memcpy into the ring with
                        // wrap handling.
                        Buffer tempDst(samples * dstBpf);
                        if (!tempDst.isValid()) return Error::NoMem;
                        Error err = srcFormat.format().convertTo(_format.format(), tempDst.data(), data,
                                                                  samples, channels, floatScratch);
                        if (err.isError()) return err;
                        writeBytesAtTail(static_cast<const uint8_t *>(tempDst.data()), samples);
                        return Error::Ok;
                }

                // Interleaved src + interleaved dst (or mono on either
                // side, where layout is moot): split at frame boundaries
                // and convert each chunk directly into the ring.
                const uint8_t *src = static_cast<const uint8_t *>(data);
                const size_t   srcBpf = srcFormat.format().bytesPerSample() * channels;

                size_t firstChunk = samples;
                if (_tail + samples > _capacity) firstChunk = _capacity - _tail;
                size_t remainder = samples - firstChunk;

                if (firstChunk > 0) {
                        Error err = srcFormat.format().convertTo(_format.format(), base + _tail * dstBpf,
                                                                  src, firstChunk, channels, floatScratch);
                        if (err.isError()) return err;
                }
                if (remainder > 0) {
                        Error err = srcFormat.format().convertTo(_format.format(), base,
                                                                  src + firstChunk * srcBpf, remainder,
                                                                  channels, floatScratch);
                        if (err.isError()) return err;
                }
                _tail = (_tail + samples) % _capacity;
                _count += samples;
                return Error::Ok;
        }

        // Via-float path: src bytes → interleaved native float →
        // (remap, gain, meter) → dst bytes.
        //
        // The pipeline middle (remap, gain, meter) indexes frames as
        // (sample, channel) pairs and therefore requires the float
        // working buffer to be in interleaved layout, regardless of
        // src's layout.  Step 1 below routes through the channel-aware
        // @ref AudioFormat::convertTo, which transposes planar input
        // into interleaved float for us.
        //
        // The dst conversion at the end (step 5) uses
        // @ref AudioDesc::floatToSamples, which is layout-agnostic at
        // the byte level — correct for the interleaved dst case (the
        // only case the ring layer currently supports).
        const size_t inChannels = srcFormat.channels();
        const size_t outChannels = _format.channels();
        const size_t inFloatCount = samples * inChannels;
        const size_t outFloatCount = samples * outChannels;

        // When no remap is active, in and out share a single buffer; otherwise
        // we need separate buffers because channel counts may differ and the
        // remap may scatter input across output positions.
        const bool   sharesBuffer = _remap.isEmpty();
        const size_t scratchFloats = sharesBuffer ? outFloatCount : (inFloatCount + outFloatCount);

        const size_t kStackFloats = 4096;
        float        stackBuf[kStackFloats];
        float       *scratch = stackBuf;
        Buffer       heapScratch;
        if (scratchFloats > kStackFloats) {
                heapScratch = Buffer(scratchFloats * sizeof(float));
                if (!heapScratch.isValid()) return Error::NoMem;
                scratch = static_cast<float *>(heapScratch.data());
        }

        float *inFloat = scratch;
        float *outFloat = sharesBuffer ? scratch : (scratch + inFloatCount);

        // Step 1: src bytes → interleaved native float.  The
        // channel-aware convertTo handles the planar→interleaved
        // transpose when src is planar; for already-interleaved src it
        // dispatches to the direct table or the via-float identity.
        // Passing @p inFloat as both @c out and the via-float scratch
        // is safe: the only path that aliases is the float-identity
        // self-copy (a no-op on the host endian), and the cross-layout
        // via-float case allocates its own transpose buffer internally.
        {
                const AudioFormat nativeFloat(AudioFormat::NativeFloat);
                Error err = srcFormat.format().convertTo(nativeFloat, inFloat, data, samples, inChannels,
                                                         inFloat);
                if (err.isError()) return err;
        }

        // Step 2 (optional): remap input channels into output channels.
        if (!_remap.isEmpty()) {
                for (size_t f = 0; f < samples; ++f) {
                        const float *inFrame = inFloat + f * inChannels;
                        float       *outFrame = outFloat + f * outChannels;
                        for (size_t c = 0; c < outChannels; ++c) {
                                int srcCh = _remap[c];
                                if (srcCh < 0 || static_cast<size_t>(srcCh) >= inChannels) {
                                        outFrame[c] = 0.0f;
                                } else {
                                        outFrame[c] = inFrame[srcCh];
                                }
                        }
                }
        }

        // Step 3 (optional): apply per-channel linear gain in-place.
        if (!_gains.isEmpty()) {
                for (size_t f = 0; f < samples; ++f) {
                        float *frame = outFloat + f * outChannels;
                        for (size_t c = 0; c < outChannels; ++c) frame[c] *= _gains[c];
                }
        }

        // Step 4 (optional): meter the post-gain output.  Use modify()
        // since process() mutates the meter's internal atomics; with
        // CopyOnWrite disabled (AudioMeter::Ptr) modify() is a no-op
        // detach.
        if (_meter.isValid()) {
                _meter.modify()->process(outFloat, samples, outChannels);
        }

        // Step 5: native float -> destination format bytes, handling ring wrap.
        size_t   bps = bytesPerSample();
        uint8_t *base = static_cast<uint8_t *>(_storage.data());

        size_t firstChunkSamples = samples;
        if (_tail + samples > _capacity) firstChunkSamples = _capacity - _tail;
        size_t remainderSamples = samples - firstChunkSamples;

        if (firstChunkSamples > 0) {
                _format.floatToSamples(base + _tail * bps, outFloat, firstChunkSamples);
        }
        if (remainderSamples > 0) {
                _format.floatToSamples(base, outFloat + firstChunkSamples * outChannels, remainderSamples);
        }

        _tail = (_tail + samples) % _capacity;
        _count += samples;

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

Error AudioBuffer::pushSilence(size_t samples) {
        Mutex::Locker lock(_mutex);
        Error         err = pushSilenceLocked(samples);
        if (err.isOk() && samples > 0) _cv.wakeOne();
        return err;
}

Error AudioBuffer::pushSilenceLocked(size_t samples) {
        if (!_format.isValid()) return Error::InvalidArgument;
        if (samples == 0) return Error::Ok;
        if (_capacity - _count < samples) return Error::NoSpace;

        // The format-correct silence value is whatever
        // floatToSamples(0.0f) produces — that's @c 0 for signed and
        // float formats, and the midpoint for unsigned formats.  We
        // generate it once on the stack (or heap for very large fills)
        // by zeroing a float scratch the size of one chunk and running
        // floatToSamples per chunk into the ring.  Chunks are sized to
        // keep the scratch off the heap for typical gap fills (a few
        // hundred samples at a few channels).
        const size_t channels = _format.channels();
        const size_t bps      = bytesPerSample();
        const size_t kStackFloats = 4096;
        float        stackBuf[kStackFloats] = {};
        const size_t maxChunkSamples =
                channels > 0 ? (kStackFloats / channels) : kStackFloats;

        uint8_t *base = static_cast<uint8_t *>(_storage.data());
        size_t   remaining = samples;
        while (remaining > 0) {
                size_t chunk = remaining;
                if (chunk > maxChunkSamples) chunk = maxChunkSamples;

                // Clamp chunk to the linear run before wrap so each
                // floatToSamples call writes contiguous bytes.
                size_t toWrap = _capacity - _tail;
                if (chunk > toWrap) chunk = toWrap;

                _format.floatToSamples(base + _tail * bps, stackBuf, chunk);

                _tail = (_tail + chunk) % _capacity;
                _count += chunk;
                remaining -= chunk;
        }
        return Error::Ok;
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
