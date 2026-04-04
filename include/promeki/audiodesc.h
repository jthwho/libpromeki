/**
 * @file      audiodesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/system.h>
#include <promeki/json.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Describes an audio format including sample type, rate, and channel count.
 * @ingroup proav
 *
 * AudioDesc encapsulates the complete description of an audio format: the sample
 * data type (e.g. PCM signed 16-bit little-endian), sample rate, and number of
 * channels. It also provides static utility methods for converting between integer
 * sample representations and normalized floating-point values in the range [-1, 1].
 */
class AudioDesc {
        PROMEKI_SHARED_FINAL(AudioDesc)
        public:
                /** @brief Shared pointer type for AudioDesc. */
                using Ptr = SharedPtr<AudioDesc>;

                /** @brief Minimum value of a signed 24-bit integer. */
                static const int32_t MinS24 = -8388608;
                /** @brief Maximum value of a signed 24-bit integer. */
                static const int32_t MaxS24 = 8388607;
                /** @brief Minimum value of an unsigned 24-bit integer. */
                static const int32_t MinU24 = 0;
                /** @brief Maximum value of an unsigned 24-bit integer. */
                static const int32_t MaxU24 = 16777215;

                /**
                 * @brief Converts an integer sample value to a normalized float in [-1, 1].
                 *
                 * Maps the integer range [Min, Max] linearly onto the float range [-1.0, 1.0].
                 *
                 * @tparam IntegerType The integer sample type.
                 * @tparam Min        The minimum value of the integer range.
                 * @tparam Max        The maximum value of the integer range.
                 * @param  value      The integer sample to convert.
                 * @return The corresponding normalized float value.
                 */
                template <typename IntegerType, IntegerType Min, IntegerType Max>
                static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        constexpr float min = static_cast<float>(Min);
                        constexpr float max = static_cast<float>(Max);
                        return((static_cast<float>(value) - min) * 2.0f / (max - min)) - 1.0f;
                }

                /**
                 * @brief Converts an integer sample value to a normalized float using the type's full range.
                 * @tparam IntegerType The integer sample type whose numeric_limits define the range.
                 * @param  value       The integer sample to convert.
                 * @return The corresponding normalized float value.
                 */
                template <typename IntegerType>
                static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        return integerToFloat<IntegerType,
                               std::numeric_limits<IntegerType>::min(),
                               std::numeric_limits<IntegerType>::max()>(value);
                }

                /**
                 * @brief Converts a normalized float in [-1, 1] to an integer sample value.
                 *
                 * Maps the float range [-1.0, 1.0] linearly onto the integer range [Min, Max].
                 * Values outside [-1, 1] are clamped.
                 *
                 * @tparam IntegerType The integer sample type.
                 * @tparam Min        The minimum value of the integer range.
                 * @tparam Max        The maximum value of the integer range.
                 * @param  value      The normalized float sample to convert.
                 * @return The corresponding integer sample value, clamped to [Min, Max].
                 */
                template <typename IntegerType, IntegerType Min, IntegerType Max>
                static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const float min = static_cast<float>(Min);
                        const float max = static_cast<float>(Max);
                        if(value <= -1.0f) return Min;
                        else if(value >= 1.0f) return Max;
                        return static_cast<IntegerType>((value + 1.0f) * 0.5f * (max - min) + min);
                }

                /**
                 * @brief Converts a normalized float to an integer sample using the type's full range.
                 * @tparam IntegerType The integer sample type whose numeric_limits define the range.
                 * @param  value       The normalized float sample to convert.
                 * @return The corresponding integer sample value, clamped to the type's range.
                 */
                template <typename IntegerType>
                static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        return floatToInteger<IntegerType,
                               std::numeric_limits<IntegerType>::min(),
                               std::numeric_limits<IntegerType>::max()>(value);
                }

                /**
                 * @brief Converts a buffer of integer samples to normalized floats.
                 *
                 * Handles endian conversion if the input byte order differs from the host.
                 *
                 * @tparam IntegerType      The integer sample type.
                 * @tparam InputIsBigEndian  True if the input buffer is big-endian.
                 * @param  out     Destination buffer for float samples.
                 * @param  inbuf   Source buffer of raw integer sample bytes.
                 * @param  samples Number of samples to convert.
                 */
                template <typename IntegerType, bool InputIsBigEndian>
                static void samplesToFloat(float *out, const uint8_t *inbuf, size_t samples) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const IntegerType *in = reinterpret_cast<const IntegerType *>(inbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                IntegerType val = *in++;
                                if constexpr (InputIsBigEndian != System::isBigEndian()) System::swapEndian(val);
                                *out++ = integerToFloat<IntegerType>(val);
                        }
                        return;
                }

                /**
                 * @brief Converts a buffer of normalized floats to integer samples.
                 *
                 * Handles endian conversion if the output byte order differs from the host.
                 *
                 * @tparam IntegerType       The integer sample type.
                 * @tparam OutputIsBigEndian  True if the output buffer should be big-endian.
                 * @param  outbuf  Destination buffer for raw integer sample bytes.
                 * @param  in      Source buffer of float samples.
                 * @param  samples Number of samples to convert.
                 */
                template <typename IntegerType, bool OutputIsBigEndian>
                static void floatToSamples(uint8_t *outbuf, const float *in, size_t samples) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        IntegerType *out = reinterpret_cast<IntegerType *>(outbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                IntegerType val = floatToInteger<IntegerType>(*in++);
                                if constexpr (OutputIsBigEndian != System::isBigEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                        return;
                }

                /**
                 * @brief Descriptor for a specific audio sample format.
                 *
                 * Contains the format's properties and function pointers for converting
                 * between the format's native representation and normalized floats.
                 */
                struct Format {
                        int             id;             ///< @brief Format identifier matching a DataType enum value.
                        String          name;           ///< @brief Short format name (e.g. "PCMI_S16LE").
                        String          desc;           ///< @brief Human-readable format description.
                        size_t          bytesPerSample; ///< @brief Number of bytes per single sample.
                        size_t          bitsPerSample;  ///< @brief Number of bits per single sample.
                        bool            isSigned;       ///< @brief True if the format uses signed integers.
                        bool            isPlanar;       ///< @brief True if channels are stored in separate planes.
                        bool            isBigEndian;    ///< @brief True if the format uses big-endian byte order.
                        void (*samplesToFloat)(float *out, const uint8_t *in, size_t samples); ///< @brief Conversion function from this format to float.
                        void (*floatToSamples)(uint8_t *out, const float *in, size_t samples); ///< @brief Conversion function from float to this format.
                };

                /**
                 * @brief Looks up a Format descriptor by its DataType id.
                 * @param id The DataType enum value to look up.
                 * @return A pointer to the matching Format, or the Invalid format descriptor.
                 */
                static const Format *lookupFormat(int id);

                /**
                 * @brief Enumeration of supported audio sample data types.
                 *
                 * PCMI = PCM audio with interleaved channels.
                 * PCMP = PCM audio with planar channels.
                 */
                enum DataType {
                        Invalid = 0,    ///< @brief Invalid / unset format.
                        PCMI_Float32LE, ///< @brief 32-bit float, little-endian, interleaved.
                        PCMI_Float32BE, ///< @brief 32-bit float, big-endian, interleaved.
                        PCMI_S8,        ///< @brief Signed 8-bit integer, interleaved.
                        PCMI_U8,        ///< @brief Unsigned 8-bit integer, interleaved.
                        PCMI_S16LE,     ///< @brief Signed 16-bit integer, little-endian, interleaved.
                        PCMI_U16LE,     ///< @brief Unsigned 16-bit integer, little-endian, interleaved.
                        PCMI_S16BE,     ///< @brief Signed 16-bit integer, big-endian, interleaved.
                        PCMI_U16BE,     ///< @brief Unsigned 16-bit integer, big-endian, interleaved.
                        PCMI_S24LE,     ///< @brief Signed 24-bit integer, little-endian, interleaved.
                        PCMI_U24LE,     ///< @brief Unsigned 24-bit integer, little-endian, interleaved.
                        PCMI_S24BE,     ///< @brief Signed 24-bit integer, big-endian, interleaved.
                        PCMI_U24BE,     ///< @brief Unsigned 24-bit integer, big-endian, interleaved.
                        PCMI_S32LE,     ///< @brief Signed 32-bit integer, little-endian, interleaved.
                        PCMI_U32LE,     ///< @brief Unsigned 32-bit integer, little-endian, interleaved.
                        PCMI_S32BE,     ///< @brief Signed 32-bit integer, big-endian, interleaved.
                        PCMI_U32BE      ///< @brief Unsigned 32-bit integer, big-endian, interleaved.
                };

                /** @brief The native float format for the current platform's endianness. */
                static constexpr DataType NativeType = System::isLittleEndian() ? PCMI_Float32LE : PCMI_Float32BE;

                /**
                 * @brief Converts a string name to its corresponding DataType enum value.
                 * @param val The format name string (e.g. "PCMI_S16LE").
                 * @return The matching DataType, or Invalid if the string is not recognized.
                 */
                static DataType stringToDataType(const String &val);

                /**
                 * @brief Constructs an AudioDesc from a JSON object.
                 * @param json The JSON object containing "DataType", "SampleRate", "Channels", and optional "Metadata".
                 * @param err  Optional error output.
                 * @return The deserialized AudioDesc, or an invalid AudioDesc on failure.
                 */
                static AudioDesc fromJson(const JsonObject &json, Error *err = nullptr);

                /** @brief Constructs an invalid (default) audio description. */
                AudioDesc() : _format(lookupFormat(Invalid)) { }

                /**
                 * @brief Constructs an audio description with the native float format.
                 *
                 * If the resulting description is not valid, all fields are reset to invalid.
                 *
                 * @param sr Sample rate in Hz.
                 * @param ch Number of audio channels.
                 */
                AudioDesc(float sr, unsigned int ch) :
                        _dataType(NativeType), _sampleRate(sr),
                        _channels(ch), _format(lookupFormat(NativeType))
                {
                        if(!isValid()) {
                                _dataType = Invalid;
                                _sampleRate = 0.0f;
                                _channels = 0;
                                _format = lookupFormat(Invalid);
                        }
                }

                /**
                 * @brief Constructs an audio description with the specified data type, sample rate, and channels.
                 *
                 * If the resulting description is not valid, all fields are reset to invalid.
                 *
                 * @param dt Data type (sample format).
                 * @param sr Sample rate in Hz.
                 * @param ch Number of audio channels.
                 */
                AudioDesc(DataType dt, float sr, unsigned int ch) :
                        _dataType(dt), _sampleRate(sr),
                        _channels(ch), _format(lookupFormat(dt))
                {
                        if(!isValid()) {
                                _dataType = Invalid;
                                _sampleRate = 0.0f;
                                _channels = 0;
                                _format = lookupFormat(Invalid);
                        }
                }

                /**
                 * @brief Returns true if both audio descriptions have equal format (type, rate, channels).
                 * @param other The AudioDesc to compare against.
                 * @return true if the audio format matches, ignoring metadata.
                 */
                bool formatEquals(const AudioDesc &other) const {
                        return _dataType == other._dataType &&
                               _sampleRate == other._sampleRate &&
                               _channels == other._channels;
                }

                /**
                 * @brief Returns true if both audio descriptions are fully equal, including metadata.
                 * @param other The AudioDesc to compare against.
                 * @return true if equal.
                 */
                bool operator==(const AudioDesc &other) const {
                        return formatEquals(other) &&
                               _metadata == other._metadata;
                }

                /**
                 * @brief Returns true if this audio description has a valid data type, sample rate, and channel count.
                 * @return true if valid.
                 */
                bool isValid() const {
                        return _dataType != 0 && _sampleRate > 0.0f && _channels > 0;
                }

                /**
                 * @brief Returns true if the data type is the platform's native float format.
                 * @return true if the data type equals NativeType.
                 */
                bool isNative() const {
                        return _dataType == NativeType;
                }

                /**
                 * @brief Returns a new AudioDesc with the same sample rate and channels but using the native float format.
                 * @return An AudioDesc suitable for internal processing.
                 */
                AudioDesc workingDesc() const {
                        return AudioDesc(NativeType, _sampleRate, _channels);
                }

                /**
                 * @brief Returns a human-readable string representation of this audio description.
                 * @return A String in the format "[FormatName SampleRateHz Channelsc]".
                 */
                String toString() const {
                        return String::sprintf("[%s %fHz %uc]",
                                _format->name.cstr(), _sampleRate, _channels);
                }

                /**
                 * @brief Serializes this audio description to a JSON object.
                 * @return A JsonObject containing "DataType", "SampleRate", "Channels", and optional "Metadata".
                 */
                JsonObject toJson() const {
                        JsonObject ret;
                        ret.set("DataType", _format->name);
                        ret.set("SampleRate", _sampleRate);
                        ret.set("Channels", _channels);
                        if(!_metadata.isEmpty()) ret.set("Metadata", _metadata.toJson());
                        return ret;
                }

                /**
                 * @brief Returns the number of bytes per single sample.
                 * @return Bytes per sample.
                 */
                size_t bytesPerSample() const {
                        return _format->bytesPerSample;
                }

                /**
                 * @brief Returns the byte stride between consecutive samples of the same channel.
                 *
                 * For planar formats this is the single sample size; for interleaved formats
                 * it is the sample size multiplied by the number of channels.
                 *
                 * @return Byte stride per sample.
                 */
                size_t bytesPerSampleStride() const {
                        return _format->isPlanar ?
                                _format->bytesPerSample :
                                _format->bytesPerSample * _channels;
                }

                /**
                 * @brief Returns the byte offset to a specific channel within a sample buffer.
                 *
                 * For planar formats, the offset is channel * bytesPerSample * bufferSamples.
                 * For interleaved formats, it is channel * bytesPerSample.
                 *
                 * @param chan          The channel index (zero-based).
                 * @param bufferSamples Total number of samples per channel in the buffer.
                 * @return Byte offset to the start of the requested channel's data.
                 */
                size_t channelBufferOffset(unsigned int chan, size_t bufferSamples) const {
                        return _format->isPlanar ?
                                _format->bytesPerSample * bufferSamples * chan :
                                _format->bytesPerSample * chan;
                }

                /**
                 * @brief Returns the total buffer size in bytes needed to store the given number of samples.
                 * @param samples Number of samples per channel.
                 * @return Total size in bytes (bytesPerSample * channels * samples).
                 */
                size_t bufferSize(size_t samples) const {
                        return _format->bytesPerSample * _channels * samples;
                }

                /**
                 * @brief Returns the data type of this audio description.
                 * @return The DataType enum value.
                 */
                DataType dataType() const {
                        return (DataType)_dataType;
                }

                /**
                 * @brief Sets the data type.
                 * @param val The new DataType value.
                 */
                void setDataType(DataType val) {
                        _dataType = val;
                        return;
                }

                /**
                 * @brief Returns the sample rate in Hz.
                 * @return The sample rate.
                 */
                float sampleRate() const {
                        return _sampleRate;
                }

                /**
                 * @brief Sets the sample rate.
                 * @param val The new sample rate in Hz.
                 */
                void setSampleRate(float val) {
                        _sampleRate = val;
                        return;
                }

                /**
                 * @brief Returns the number of audio channels.
                 * @return The channel count.
                 */
                unsigned int channels() const {
                        return _channels;
                }

                /**
                 * @brief Sets the number of audio channels.
                 * @param val The new channel count.
                 */
                void setChannels(unsigned int val) {
                        _channels = val;
                        return;
                }

                /** @brief Returns a const reference to the metadata. */
                const Metadata &metadata() const {
                        return _metadata;
                }

                /** @brief Returns a mutable reference to the metadata. */
                Metadata &metadata() {
                        return _metadata;
                }

                /**
                 * @brief Converts samples from this description's format to normalized floats.
                 * @param out     Destination buffer for float samples.
                 * @param in      Source buffer of raw sample bytes.
                 * @param samples Number of samples per channel to convert.
                 */
                void samplesToFloat(float *out, const uint8_t *in, size_t samples) const {
                        _format->samplesToFloat(out, in, samples * _channels);
                        return;
                }

                /**
                 * @brief Converts normalized floats to samples in this description's format.
                 * @param out     Destination buffer for raw sample bytes.
                 * @param in      Source buffer of float samples.
                 * @param samples Number of samples per channel to convert.
                 */
                void floatToSamples(uint8_t *out, const float *in, size_t samples) const {
                        _format->floatToSamples(out, in, samples * _channels);
                        return;
                }

        private:
                int                     _dataType = 0;
                float                   _sampleRate = 0.0f;
                unsigned int            _channels = 0;
                Metadata                _metadata;
                const Format            *_format = nullptr;
};

PROMEKI_NAMESPACE_END

