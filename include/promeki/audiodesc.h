/**
 * @file      audiodesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/audioformat.h>
#include <promeki/audiochannelmap.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/json.h>

PROMEKI_NAMESPACE_BEGIN

class SdpMediaDescription;

/**
 * @brief Describes a concrete audio stream — format plus sample rate and channel count.
 * @ingroup proav
 *
 * AudioDesc is to @ref AudioFormat what @ref ImageDesc is to
 * @ref PixelFormat &mdash; AudioFormat identifies the sample encoding
 * (PCM layout, bit depth, endianness, optional compressed codec)
 * while AudioDesc binds that format to a specific sample rate,
 * channel count, and optional metadata.
 *
 * @par Example
 * @code
 * AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
 * assert(desc.isValid());
 * assert(desc.bytesPerSample() == 2);
 * assert(desc.bufferSize(1024) == 4096);
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * @c AudioDesc::Ptr uses an atomic refcount and is safe to share across
 * threads.
 */
class AudioDesc {
                PROMEKI_SHARED_FINAL(AudioDesc)
        public:
                /** @brief Shared pointer type for AudioDesc. */
                using Ptr = SharedPtr<AudioDesc>;

                /** @brief List of AudioDesc values. */
                using List = ::promeki::List<AudioDesc>;

                /** @brief List of shared AudioDesc pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /**
                 * @brief Constructs an AudioDesc from a JSON object.
                 * @param json The JSON object containing @c "Format",
                 *             @c "SampleRate", @c "Channels", and optional
                 *             @c "Metadata".
                 * @param err  Optional error output.
                 * @return The deserialized AudioDesc, or an invalid
                 *         AudioDesc on failure.
                 */
                static AudioDesc fromJson(const JsonObject &json, Error *err = nullptr);

                /**
                 * @brief Derives an AudioDesc from an SDP media description.
                 *
                 * Interprets the RTP payload encoding name in the
                 * @c a=rtpmap attribute to build an audio AudioDesc.
                 * Supported encodings (all RFC 3551 / RFC 3190):
                 *
                 *  - @c L16  → @ref AudioFormat::PCMI_S16BE (wire is big-endian 16-bit)
                 *  - @c L24  → @ref AudioFormat::PCMI_S24BE (wire is packed 24-bit big-endian)
                 *  - @c L8   → @ref AudioFormat::PCMI_U8   (unsigned 8-bit per RFC 3551 §4.5.10)
                 *
                 * Anything else (compressed codecs, non-audio media
                 * descriptions) yields an invalid @ref AudioDesc.
                 *
                 * @param md The SDP media description to interpret.
                 * @return A populated AudioDesc, or an invalid AudioDesc
                 *         on any failure.
                 */
                static AudioDesc fromSdp(const SdpMediaDescription &md);

                /** @brief Constructs an invalid (default) audio description. */
                AudioDesc() = default;

                /**
                 * @brief Constructs an audio description using the platform's
                 *        native float32 PCM format.
                 * @param sr Sample rate in Hz.
                 * @param ch Number of audio channels.
                 *
                 * FIXME: parameter order is (rate, channels) — easy to
                 * swap with @c AudioDesc(2, 48000.0f).  Revisit later
                 * with a builder / named-args style or a dedicated
                 * factory like @c AudioDesc::nativeFloat(sr, ch).
                 */
                AudioDesc(float sr, unsigned int ch)
                    : _format(AudioFormat::NativeFloat), _sampleRate(sr), _channels(ch),
                      _channelMap(AudioChannelMap::defaultForChannels(ch)) {
                        if (!validParams()) reset();
                }

                /**
                 * @brief Constructs an audio description with the given format, sample rate, and channels.
                 *
                 * The channel map defaults to @ref AudioChannelMap::defaultForChannels for @p ch
                 * (e.g. 2 → Stereo, 6 → 5.1, 8 → 7.1).  Use @ref setChannelMap to install an
                 * explicit assignment.
                 *
                 * @param fmt Audio format (sample layout / codec identity).
                 * @param sr  Sample rate in Hz.
                 * @param ch  Number of audio channels.
                 */
                AudioDesc(const AudioFormat &fmt, float sr, unsigned int ch)
                    : _format(fmt), _sampleRate(sr), _channels(ch),
                      _channelMap(AudioChannelMap::defaultForChannels(ch)) {
                        if (!validParams()) reset();
                }

                /**
                 * @brief Constructs an audio description with explicit channel map.
                 *
                 * The channel count is derived from @p map; @ref channels and the
                 * map stay in lockstep.
                 *
                 * @param fmt Audio format.
                 * @param sr  Sample rate in Hz.
                 * @param map Channel role assignment.  Must be valid (non-empty).
                 */
                AudioDesc(const AudioFormat &fmt, float sr, AudioChannelMap map)
                    : _format(fmt), _sampleRate(sr), _channels(static_cast<unsigned int>(map.channels())),
                      _channelMap(std::move(map)) {
                        if (!validParams()) reset();
                }

                /**
                 * @brief Builds an SDP media description from this AudioDesc.
                 *
                 * The inverse of @ref fromSdp.  Populates the returned
                 * @c SdpMediaDescription with @c m=audio, an @c a=rtpmap
                 * line for the appropriate encoding (@c L16, @c L24, or
                 * @c L8), and the clock rate / channel count derived
                 * from this descriptor.
                 *
                 * Returns an empty @c SdpMediaDescription if the
                 * AudioDesc is invalid or its AudioFormat has no RTP
                 * encoding mapping.
                 *
                 * @param payloadType RTP payload type (0-127).
                 * @return A populated SdpMediaDescription, or an empty
                 *         one on failure.
                 */
                SdpMediaDescription toSdp(uint8_t payloadType) const;

                /**
                 * @brief Returns true if the format, rate, channels, and channel map match.
                 * @param other The AudioDesc to compare against.
                 * @return true if the audio format matches, ignoring metadata.
                 */
                bool formatEquals(const AudioDesc &other) const {
                        return _format == other._format && _sampleRate == other._sampleRate &&
                               _channels == other._channels && _channelMap == other._channelMap;
                }

                /**
                 * @brief Full equality — includes metadata.
                 * @param other The AudioDesc to compare against.
                 * @return true if equal.
                 */
                bool operator==(const AudioDesc &other) const {
                        return formatEquals(other) && _metadata == other._metadata;
                }

                /** @brief Returns true if this audio description is valid. */
                bool isValid() const { return _format.isValid() && _sampleRate > 0.0f && _channels > 0; }

                /** @brief Returns true if this description identifies a compressed bitstream. */
                bool isCompressed() const { return _format.isCompressed(); }

                /**
                 * @brief Returns true if the format is the platform's
                 *        native float32 PCM format.
                 */
                bool isNative() const { return _format.id() == AudioFormat::NativeFloat; }

                /**
                 * @brief Returns a new AudioDesc using the platform's
                 *        native float32 PCM format with the same sample
                 *        rate and channel count.
                 */
                AudioDesc workingDesc() const {
                        return AudioDesc(AudioFormat(AudioFormat::NativeFloat), _sampleRate, _channels);
                }

                /** @brief Returns a human-readable string. */
                String toString() const {
                        return String::sprintf("[%s %fHz %uc]", _format.name().cstr(), _sampleRate, _channels);
                }

                /**
                 * @brief Serializes this audio description to a JSON object.
                 * @return A JsonObject containing @c "Format",
                 *         @c "SampleRate", @c "Channels", optional
                 *         @c "ChannelMap" (omitted when it matches the
                 *         default for @c Channels), and optional
                 *         @c "Metadata".
                 */
                JsonObject toJson() const {
                        JsonObject ret;
                        ret.set("Format", _format.name());
                        ret.set("SampleRate", _sampleRate);
                        ret.set("Channels", _channels);
                        // Only emit the channel map when it differs from the default
                        // mapping for this channel count — keeps simple descriptors clean.
                        if (_channelMap != AudioChannelMap::defaultForChannels(_channels)) {
                                ret.set("ChannelMap", _channelMap.toString());
                        }
                        if (!_metadata.isEmpty()) ret.set("Metadata", _metadata.toJson());
                        return ret;
                }

                /** @brief Returns the audio format. */
                const AudioFormat &format() const { return _format; }

                /** @brief Sets the audio format. */
                void setFormat(const AudioFormat &val) { _format = val; }

                /** @brief Returns the sample rate in Hz. */
                float sampleRate() const { return _sampleRate; }

                /** @brief Sets the sample rate. */
                void setSampleRate(float val) { _sampleRate = val; }

                /** @brief Returns the number of audio channels. */
                unsigned int channels() const { return _channels; }

                /**
                 * @brief Sets the number of audio channels.
                 *
                 * Resets the channel map to @ref AudioChannelMap::defaultForChannels
                 * for the new count so the descriptor stays internally consistent.
                 * Use @ref setChannelMap afterwards to install a different mapping.
                 */
                void setChannels(unsigned int val) {
                        _channels = val;
                        _channelMap = AudioChannelMap::defaultForChannels(val);
                }

                /** @brief Returns the channel role map. */
                const AudioChannelMap &channelMap() const { return _channelMap; }

                /**
                 * @brief Replaces the channel role map.
                 *
                 * The new map's channel count must equal @ref channels;
                 * a length mismatch leaves the descriptor unchanged.
                 *
                 * @param map The new channel map.
                 */
                void setChannelMap(const AudioChannelMap &map) {
                        if (map.channels() == _channels) _channelMap = map;
                }

                /** @brief Returns a const reference to the metadata. */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns a mutable reference to the metadata. */
                Metadata &metadata() { return _metadata; }

                /** @brief Returns the number of bytes per single sample (PCM only). */
                size_t bytesPerSample() const { return _format.bytesPerSample(); }

                /**
                 * @brief Returns the byte stride between consecutive
                 *        samples of the same channel.
                 *
                 * For planar formats this is the single sample size; for
                 * interleaved formats it is the sample size multiplied
                 * by the number of channels.
                 */
                size_t bytesPerSampleStride() const {
                        return _format.isPlanar() ? _format.bytesPerSample() : _format.bytesPerSample() * _channels;
                }

                /**
                 * @brief Returns the byte offset to a specific channel
                 *        within a sample buffer.
                 *
                 * For planar formats, the offset is @c channel * bytesPerSample * bufferSamples.
                 * For interleaved formats, it is @c channel * bytesPerSample.
                 *
                 * @param chan          The channel index (zero-based).
                 * @param bufferSamples Total number of samples per channel in the buffer.
                 * @return Byte offset to the start of the requested channel's data.
                 */
                size_t channelBufferOffset(unsigned int chan, size_t bufferSamples) const {
                        return _format.isPlanar() ? _format.bytesPerSample() * bufferSamples * chan
                                                  : _format.bytesPerSample() * chan;
                }

                /**
                 * @brief Returns the total buffer size (bytes) for the given sample count.
                 * @param samples Number of samples per channel.
                 * @return Total size in bytes (@c bytesPerSample * channels * samples).
                 */
                size_t bufferSize(size_t samples) const { return _format.bytesPerSample() * _channels * samples; }

                /**
                 * @brief Converts samples from this description's format to normalized floats.
                 * @param out     Destination buffer for float samples.
                 * @param in      Source buffer of raw sample bytes.
                 * @param samples Number of samples per channel to convert.
                 */
                void samplesToFloat(float *out, const uint8_t *in, size_t samples) const {
                        _format.samplesToFloat(out, in, samples * _channels);
                }

                /**
                 * @brief Converts normalized floats to samples in this description's format.
                 * @param out     Destination buffer for raw sample bytes.
                 * @param in      Source buffer of float samples.
                 * @param samples Number of samples per channel to convert.
                 */
                void floatToSamples(uint8_t *out, const float *in, size_t samples) const {
                        _format.floatToSamples(out, in, samples * _channels);
                }

        private:
                AudioFormat     _format;
                float           _sampleRate = 0.0f;
                unsigned int    _channels = 0;
                AudioChannelMap _channelMap;
                Metadata        _metadata;

                bool validParams() const { return _format.isValid() && _sampleRate > 0.0f && _channels > 0; }

                void reset() {
                        _format = AudioFormat();
                        _sampleRate = 0.0f;
                        _channels = 0;
                        _channelMap = AudioChannelMap();
                }
};

/**
 * @brief Writes an AudioDesc as tag + format + sample rate + channels + channel map + metadata.
 */
inline DataStream &operator<<(DataStream &stream, const AudioDesc &desc) {
        stream.writeTag(DataStream::TypeAudioDesc);
        stream << desc.format();
        stream << desc.sampleRate();
        stream << static_cast<uint32_t>(desc.channels());
        stream << desc.channelMap();
        stream << desc.metadata();
        return stream;
}

/**
 * @brief Reads an AudioDesc from its tagged wire format.
 */
inline DataStream &operator>>(DataStream &stream, AudioDesc &desc) {
        if (!stream.readTag(DataStream::TypeAudioDesc)) {
                desc = AudioDesc();
                return stream;
        }
        AudioFormat     fmt;
        float           sr = 0.0f;
        uint32_t        ch = 0;
        AudioChannelMap map;
        Metadata        meta;
        stream >> fmt >> sr >> ch >> map >> meta;
        if (stream.status() != DataStream::Ok) {
                desc = AudioDesc();
                return stream;
        }
        desc = AudioDesc(fmt, sr, ch);
        // Restore the explicit channel map if it parsed validly; otherwise keep
        // the default that the (fmt, sr, ch) constructor installs.
        if (map.channels() == ch) desc.setChannelMap(map);
        desc.metadata() = std::move(meta);
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::AudioDesc);
