/**
 * @file      audioresampler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <samplerate.h>
#include <promeki/audioresampler.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(AudioResampler)

struct AudioResampler::Impl {
        SRC_STATE    *state    = nullptr;
        unsigned int  channels = 0;
        SrcQuality    quality;
        double        ratio    = 1.0;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioResampler::AudioResampler() = default;

AudioResampler::~AudioResampler() {
        if(_impl != nullptr) {
                if(_impl->state != nullptr) src_delete(_impl->state);
                delete _impl;
        }
}

bool AudioResampler::isValid() const {
        return _impl != nullptr && _impl->state != nullptr;
}

Error AudioResampler::setup(unsigned int channels, const SrcQuality &quality) {
        if(channels == 0) return Error::InvalidArgument;

        // Tear down any existing state.
        if(_impl != nullptr) {
                if(_impl->state != nullptr) {
                        src_delete(_impl->state);
                        _impl->state = nullptr;
                }
        } else {
                _impl = new Impl;
        }

        int srcErr = 0;
        _impl->state = src_new(quality.value(), static_cast<int>(channels), &srcErr);
        if(_impl->state == nullptr) {
                promekiWarn("AudioResampler: src_new() failed: %s", src_strerror(srcErr));
                return Error::LibraryFailure;
        }

        _impl->channels = channels;
        _impl->quality  = quality;
        _impl->ratio    = 1.0;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

unsigned int AudioResampler::channels() const {
        return _impl != nullptr ? _impl->channels : 0;
}

SrcQuality AudioResampler::quality() const {
        return _impl != nullptr ? _impl->quality : SrcQuality::SincMedium;
}

double AudioResampler::ratio() const {
        return _impl != nullptr ? _impl->ratio : 1.0;
}

// ---------------------------------------------------------------------------
// Ratio
// ---------------------------------------------------------------------------

Error AudioResampler::setRatio(double ratio) {
        if(!isValid()) return Error::NotSupported;
        if(ratio <= 0.0) return Error::InvalidArgument;
        _impl->ratio = ratio;
        int srcErr = src_set_ratio(_impl->state, ratio);
        if(srcErr != 0) {
                promekiWarn("AudioResampler: src_set_ratio() failed: %s", src_strerror(srcErr));
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error AudioResampler::setRatio(float inputRate, float outputRate) {
        if(inputRate <= 0.0f || outputRate <= 0.0f) return Error::InvalidArgument;
        return setRatio(static_cast<double>(outputRate) / static_cast<double>(inputRate));
}

// ---------------------------------------------------------------------------
// Process
// ---------------------------------------------------------------------------

AudioResampler::ProcessResult AudioResampler::process(
        const Audio &input, Audio &output, bool endOfInput)
{
        if(!isValid()) return ProcessResult(0, Error::NotSupported);
        if(!input.isValid() || !output.isValid()) return ProcessResult(0, Error::InvalidArgument);
        if(!input.desc().isNative() || !output.desc().isNative()) {
                promekiWarn("AudioResampler: input and output must be native float format");
                return ProcessResult(0, Error::FormatMismatch);
        }
        if(input.desc().channels() != _impl->channels ||
           output.desc().channels() != _impl->channels) {
                promekiWarn("AudioResampler: channel count mismatch "
                            "(resampler=%u, input=%u, output=%u)",
                            _impl->channels, input.desc().channels(),
                            output.desc().channels());
                return ProcessResult(0, Error::FormatMismatch);
        }

        long inputUsed = 0;
        long outputGen = 0;
        Error err = process(
                input.data<const float>(),
                static_cast<long>(input.samples()),
                output.data<float>(),
                static_cast<long>(output.maxSamples()),
                inputUsed, outputGen, endOfInput);
        if(err.isError()) return ProcessResult(0, err);
        output.resize(static_cast<size_t>(outputGen));
        return makeResult(static_cast<size_t>(outputGen));
}

Error AudioResampler::process(
        const float *dataIn, long inputFrames,
        float *dataOut, long outputFrames,
        long &inputUsed, long &outputGen,
        bool endOfInput)
{
        if(!isValid()) return Error::NotSupported;

        SRC_DATA data = {};
        data.data_in       = dataIn;
        data.input_frames  = inputFrames;
        data.data_out      = dataOut;
        data.output_frames = outputFrames;
        data.src_ratio     = _impl->ratio;
        data.end_of_input  = endOfInput ? 1 : 0;

        int srcErr = src_process(_impl->state, &data);
        if(srcErr != 0) {
                promekiWarn("AudioResampler: src_process() failed: %s", src_strerror(srcErr));
                inputUsed  = 0;
                outputGen  = 0;
                return Error::LibraryFailure;
        }

        inputUsed = data.input_frames_used;
        outputGen = data.output_frames_gen;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

Error AudioResampler::reset() {
        if(!isValid()) return Error::NotSupported;
        int srcErr = src_reset(_impl->state);
        if(srcErr != 0) {
                promekiWarn("AudioResampler: src_reset() failed: %s", src_strerror(srcErr));
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
