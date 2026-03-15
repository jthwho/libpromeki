/**
 * @file      audiodesc.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/audiodesc.h>
#include <promeki/core/structdatabase.h>

PROMEKI_NAMESPACE_BEGIN

static StructDatabase<int, AudioDesc::Format> db = {
        { 
                .id = AudioDesc::Invalid,
                .name = "InvalidAudioFormat",
                .desc = "Invalid Audio Format",
                .bytesPerSample = 0,
                .bitsPerSample = 0,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = [](float *, const uint8_t *, size_t){},
                .floatToSamples = [](uint8_t *, const float *, size_t){}

        },
        { 
                .id = AudioDesc::PCMI_Float32LE,
                .name = "PCMI_Float32LE",
                .desc = "PCM Interleaved 32bit Float Little Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = [](float *out, const uint8_t *inbuf, size_t samples) {
                        const float *in = reinterpret_cast<const float *>(inbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                float val = *in++;
                                if constexpr (System::isBigEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                        return;
                },
                .floatToSamples = [](uint8_t *outbuf, const float *in, size_t samples) {
                        float *out = reinterpret_cast<float *>(outbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                float val = *in++;
                                if constexpr (System::isBigEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                        return;


                }
        },
        { 
                .id = AudioDesc::PCMI_Float32BE,
                .name = "Float32BE",
                .desc = "PCM Interleaved 32bit Float Big Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = [](float *out, const uint8_t *inbuf, size_t samples) {
                        const float *in = reinterpret_cast<const float *>(inbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                float val = *in++;
                                if constexpr (System::isLittleEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                        return;
                },
                .floatToSamples = [](uint8_t *outbuf, const float *in, size_t samples) {
                        float *out = reinterpret_cast<float *>(outbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                float val = *in++;
                                if constexpr (System::isLittleEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                        return;


                }
        },
        { 
                .id = AudioDesc::PCMI_S8,
                .name = "PCMI_S8",
                .desc = "PCM Interleaved 8bit Signed",
                .bytesPerSample = 1,
                .bitsPerSample = 8,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = AudioDesc::samplesToFloat<int8_t, false>,
                .floatToSamples = AudioDesc::floatToSamples<int8_t, false>
        },
        { 
                .id = AudioDesc::PCMI_U8,
                .name = "PCMI_U8",
                .desc = "PCM Interleaved 8bit Unsigned",
                .bytesPerSample = 1,
                .bitsPerSample = 8,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = AudioDesc::samplesToFloat<uint8_t, false>,
                .floatToSamples = AudioDesc::floatToSamples<uint8_t, false>
        },
        { 
                .id = AudioDesc::PCMI_S16LE,
                .name = "PCMI_S16LE",
                .desc = "PCM Interleaved 16bit Signed Little Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = AudioDesc::samplesToFloat<int16_t, false>,
                .floatToSamples = AudioDesc::floatToSamples<int16_t, false>
        },
        { 
                .id = AudioDesc::PCMI_U16LE,
                .name = "PCMI_U16LE",
                .desc = "PCM Interleaved 16bit Unsigned Little Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = AudioDesc::samplesToFloat<uint16_t, false>,
                .floatToSamples = AudioDesc::floatToSamples<uint16_t, false>
        },
        { 
                .id = AudioDesc::PCMI_S16BE,
                .name = "PCMI_S16BE",
                .desc = "PCM Interleaved 16bit Signed Big Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = AudioDesc::samplesToFloat<int16_t, true>,
                .floatToSamples = AudioDesc::floatToSamples<int16_t, true>
        },
        { 
                .id = AudioDesc::PCMI_U16BE,
                .name = "PCMI_U16BE",
                .desc = "PCM Interleaved 16bit Unsigned Big Endian",
                .bytesPerSample = 2,
                .bitsPerSample = 16,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = AudioDesc::samplesToFloat<uint16_t, true>,
                .floatToSamples = AudioDesc::floatToSamples<uint16_t, true>
         },
         { 
                .id = AudioDesc::PCMI_S24LE,
                .name = "PCMI_S24LE",
                .desc = "PCM Interleaved 24bit Signed Little Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = [](float *out, const uint8_t *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = static_cast<int32_t>(in[0])       |
                                              static_cast<int32_t>(in[1]) << 8  |
                                              static_cast<int32_t>(in[2]) << 16;
                                *out++ = AudioDesc::integerToFloat<int32_t, AudioDesc::MinS24, AudioDesc::MaxS24>(val);
                                in += 3;
                        }
                        return;
                },
                .floatToSamples = [](uint8_t *out, const float *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = AudioDesc::floatToInteger<int32_t, AudioDesc::MinS24, AudioDesc::MaxS24>(*in++);
                                out[0] = static_cast<uint8_t>(val & 0xFF);
                                out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                                out[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                                out += 3;
                        }
                        return;

                } 
        },
        { 
                .id = AudioDesc::PCMI_U24LE,
                .name = "PCMI_U24LE",
                .desc = "PCM Interleaved 24bit Unsigned Little Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = [](float *out, const uint8_t *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = static_cast<int32_t>(in[0])       |
                                              static_cast<int32_t>(in[1]) << 8  |
                                              static_cast<int32_t>(in[2]) << 16;
                                *out++ = AudioDesc::integerToFloat<int32_t, AudioDesc::MinU24, AudioDesc::MaxU24>(val);
                                in += 3;
                        }
                        return;
                },
                .floatToSamples = [](uint8_t *out, const float *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = AudioDesc::floatToInteger<int32_t, AudioDesc::MinU24, AudioDesc::MaxU24>(*in++);
                                out[0] = static_cast<uint8_t>(val & 0xFF);
                                out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                                out[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                                out += 3;
                        }
                        return;

                } 
        },
        { 
                .id = AudioDesc::PCMI_S24BE,
                .name = "PCMI_S24BE",
                .desc = "PCM Interleaved 24bit Signed Big Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = [](float *out, const uint8_t *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = static_cast<int32_t>(in[0]) << 16 |
                                              static_cast<int32_t>(in[1]) << 8  |
                                              static_cast<int32_t>(in[2]);
                                *out++ = AudioDesc::integerToFloat<int32_t, AudioDesc::MinS24, AudioDesc::MaxS24>(val);
                                in += 3;
                        }
                        return;
                },
                .floatToSamples = [](uint8_t *out, const float *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = AudioDesc::floatToInteger<int32_t, AudioDesc::MinS24, AudioDesc::MaxS24>(*in++);
                                out[0] = static_cast<uint8_t>((val >> 16) & 0xFF);
                                out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                                out[2] = static_cast<uint8_t>(val & 0xFF);
                                out += 3;
                        }
                        return;

                } 
        },
        { 
                .id = AudioDesc::PCMI_U24BE,
                .name = "PCMI_U24BE",
                .desc = "PCM Interleaved 24bit Unsigned Big Endian",
                .bytesPerSample = 3,
                .bitsPerSample = 24,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = [](float *out, const uint8_t *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = static_cast<int32_t>(in[0]) << 16 |
                                              static_cast<int32_t>(in[1]) << 8  |
                                              static_cast<int32_t>(in[2]);
                                *out++ = AudioDesc::integerToFloat<int32_t, AudioDesc::MinU24, AudioDesc::MaxU24>(val);
                                in += 3;
                        }
                        return;
                },
                .floatToSamples = [](uint8_t *out, const float *in, size_t samples) {
                        for(size_t i = 0; i < samples; ++i) {
                                int32_t val = AudioDesc::floatToInteger<int32_t, AudioDesc::MinU24, AudioDesc::MaxU24>(*in++);
                                out[0] = static_cast<uint8_t>((val >> 16) & 0xFF);
                                out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                                out[2] = static_cast<uint8_t>(val & 0xFF);
                                out += 3;
                        }
                        return;

                } 
        },
        { 
                .id = AudioDesc::PCMI_S32LE,
                .name = "PCMI_S32LE",
                .desc = "PCM Interleaved 32bit Signed Little Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = AudioDesc::samplesToFloat<int32_t, false>,
                .floatToSamples = AudioDesc::floatToSamples<int32_t, false>
         },
         { 
                .id = AudioDesc::PCMI_U32LE,
                .name = "PCMI_U32LE",
                .desc = "PCM Interleaved 32bit Unsigned Little Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = false,
                .samplesToFloat = AudioDesc::samplesToFloat<uint32_t, false>,
                .floatToSamples = AudioDesc::floatToSamples<uint32_t, false>
        },
        { 
                .id = AudioDesc::PCMI_S32BE,
                .name = "PCMI_S32BE",
                .desc = "PCM Interleaved 32bit Signed Big Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = true,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = AudioDesc::samplesToFloat<int32_t, true>,
                .floatToSamples = AudioDesc::floatToSamples<int32_t, true>
        },
        { 
                .id = AudioDesc::PCMI_U32BE,
                .name = "PCMI_U32BE",
                .desc = "PCM Interleaved 32bit Unsigned Big Endian",
                .bytesPerSample = 4,
                .bitsPerSample = 32,
                .isSigned = false,
                .isPlanar = false,
                .isBigEndian = true,
                .samplesToFloat = AudioDesc::samplesToFloat<uint32_t, true>,
                .floatToSamples = AudioDesc::floatToSamples<uint32_t, true>
        }
};

const AudioDesc::Format *AudioDesc::lookupFormat(int id) {
        return &db.get(id);
}

AudioDesc::DataType AudioDesc::stringToDataType(const String &val) {
    DataType ret = (DataType)db.lookupKeyByName(val);
    return ret;
}

AudioDesc AudioDesc::fromJson(const JsonObject &json, Error *err) {
    DataType type = stringToDataType(json.getString("DataType"));
    float sampleRate = json.getDouble("SampleRate");
    unsigned int chans = json.getUInt("Channels");
    if(type == Invalid || sampleRate <= 0.0 || chans < 1) {
        if(err) *err = Error::Invalid;
        return AudioDesc();
    }
    if(err) *err = Error::Ok;
    return AudioDesc(type, sampleRate, chans);
}

PROMEKI_NAMESPACE_END

