/**
 * @file      flvtag.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/amf0.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief FLV VIDEODATA payload framing.
 * @ingroup proav
 *
 * RTMP carries audio / video / script data using the FLV tag body
 * format — that is, a one-byte type-and-frame-info byte followed by
 * codec-specific data, but @e without the FLV file-level
 * @c TagType / @c DataSize / @c Timestamp / @c StreamID header (which
 * RTMP's chunk-stream layer replaces).  This class models the body
 * portion only.
 *
 * @par Legacy AVC (CodecID = 7)
 * Wire layout:
 *  - 1 byte: @c (FrameType << 4) | CodecID
 *  - 1 byte: AvcPacketType (0 = sequence header, 1 = NALU, 2 = end of sequence)
 *  - 3 bytes: signed CompositionTime offset in milliseconds
 *  - N bytes: AVCC NAL units / @c AVCDecoderConfigurationRecord / @c (empty)
 *
 * @par Enhanced RTMP (HEVC / VP9 / AV1)
 * Per the @c enhanced-rtmp.org v1 spec, when @c FrameType bit 7 (the
 * top bit) is set the byte is reinterpreted as
 * @c (1 << 7) | (PacketType << 3) | (FrameType & 7) and a 4-byte
 * FourCC follows identifying the codec.  PacketType 1 (CodedFrames)
 * adds a 3-byte signed CompositionTime offset before the payload;
 * PacketType 3 (CodedFramesX) omits the offset (CTO = 0) — we do not
 * emit CodedFramesX in v1, but the parser accepts both.
 *
 * The class hides the legacy / enhanced distinction from callers: the
 * @ref codec field selects the path and @ref AvcPacketType /
 * @ref compositionTimeOffsetMs / @ref data are interpreted the same
 * way regardless.
 *
 * @par Thread Safety
 * Plain value type — distinct instances may be used concurrently.
 */
class FlvVideoTag {
        public:
                /** @brief FLV FrameType field (upper nibble of the byte 0). */
                enum FrameType : uint8_t {
                        FrameTypeUnknown    = 0,
                        Keyframe            = 1, ///< IDR / IRAP / similar.
                        InterFrame          = 2,
                        DisposableInterFrame = 3,
                        GeneratedKeyframe   = 4,
                        InfoFrame           = 5
                };

                /**
                 * @brief Codec identifier.
                 *
                 * Values 0–15 match the FLV legacy CodecID nibble exactly.
                 * Values >= 100 are synthetic identifiers we use internally
                 * to drive the Enhanced-RTMP serialization path.
                 */
                enum Codec : uint8_t {
                        CodecUnknown = 0,
                        H263         = 2,
                        Screen1      = 3,
                        Vp6          = 4,
                        Vp6Alpha     = 5,
                        Screen2      = 6,
                        Avc          = 7,
                        // Enhanced RTMP: not legacy CodecID values.
                        ExHevc       = 100,
                        ExVp9        = 101,
                        ExAv1        = 102
                };

                /** @brief AVC / Enhanced-RTMP packet type. */
                enum AvcPacketType : uint8_t {
                        SequenceHeader = 0, ///< AVCDecoderConfigurationRecord / hvcC / etc.
                        Nalu           = 1, ///< Length-prefixed NAL units (AVCC).
                        EndOfSequence  = 2  ///< Empty payload — peer should flush its decoder.
                };

                /** @brief FourCCs used by the Enhanced-RTMP framing. */
                static constexpr uint32_t FourCcHvc1 = 0x68766331; ///< 'hvc1'
                static constexpr uint32_t FourCcVp09 = 0x76703039; ///< 'vp09'
                static constexpr uint32_t FourCcAv01 = 0x61763031; ///< 'av01'

                FrameType     frameType                = InterFrame;
                Codec         codec                    = Avc;
                AvcPacketType packetType               = Nalu;
                int32_t       compositionTimeOffsetMs  = 0;  ///< signed 24-bit on the wire
                Buffer        data;

                /**
                 * @brief Serialize this tag to its FLV body form, appended to @p out.
                 * @return @c Error::Ok on success, @c Error::OutOfRange on invalid
                 *         field combinations (e.g. CompositionTime outside the
                 *         signed 24-bit range).
                 */
                Error pack(Buffer &out) const;

                /**
                 * @brief Parse an FLV VIDEODATA body from a single-slice BufferView.
                 *
                 * @return @c Error::Ok on success, @c Error::CorruptData on a
                 *         malformed header byte, @c Error::OutOfRange on
                 *         truncation, @c Error::NotSupported for codec /
                 *         packet-type combinations we don't model in v1.
                 */
                static Error unpack(const BufferView &in, FlvVideoTag &out);
};

/**
 * @brief FLV AUDIODATA payload framing.
 * @ingroup proav
 *
 * Wire layout (legacy):
 *  - 1 byte: @c (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
 *  - (if AAC) 1 byte: @c AacPacketType (0 = AudioSpecificConfig, 1 = raw)
 *  - N bytes: codec payload
 *
 * For AAC the FLV @c SoundRate / @c SoundSize / @c SoundType fields
 * are conventionally fixed at @c (44 kHz / 16-bit / Stereo) regardless
 * of the actual encoded format — the @c AudioSpecificConfig blob
 * (carried in the @c AudioSpecificConfig packet) is the real
 * description.  We follow that convention on output but expose the
 * fields so a parser can round-trip non-AAC codecs faithfully.
 */
class FlvAudioTag {
        public:
                /** @brief FLV SoundFormat field (upper nibble of byte 0). */
                enum SoundFormat : uint8_t {
                        LinearPcmPlatform     = 0,
                        Adpcm                 = 1,
                        Mp3                   = 2,
                        LinearPcmLittleEndian = 3,
                        Nellymoser16k         = 4,
                        Nellymoser8k          = 5,
                        Nellymoser            = 6,
                        G711ALaw              = 7,
                        G711MuLaw             = 8,
                        Aac                   = 10,
                        Speex                 = 11,
                        Mp38k                 = 14,
                        DeviceSpecific        = 15
                };

                /** @brief FLV SoundRate field (2 bits). */
                enum SoundRate : uint8_t {
                        Rate5500  = 0,
                        Rate11000 = 1,
                        Rate22000 = 2,
                        Rate44000 = 3
                };

                /** @brief FLV SoundSize field (1 bit). */
                enum SoundSize : uint8_t { Bits8 = 0, Bits16 = 1 };

                /** @brief FLV SoundType field (1 bit). */
                enum SoundType : uint8_t { Mono = 0, Stereo = 1 };

                /** @brief AAC sub-header packet type. */
                enum AacPacketType : uint8_t {
                        AudioSpecificConfig = 0, ///< 2-5 byte AudioSpecificConfig blob.
                        Raw                 = 1  ///< Raw AAC frame payload.
                };

                SoundFormat   format        = Aac;
                SoundRate     rate          = Rate44000;
                SoundSize     size          = Bits16;
                SoundType     channelType   = Stereo;
                AacPacketType aacPacketType = Raw;
                Buffer        data;

                /** @brief Serialize this tag, appended to @p out. */
                Error pack(Buffer &out) const;

                /** @brief Parse an FLV AUDIODATA body from a single-slice BufferView. */
                static Error unpack(const BufferView &in, FlvAudioTag &out);
};

/**
 * @brief FLV SCRIPTDATA payload framing.
 * @ingroup proav
 *
 * SCRIPTDATA bodies are simply a sequence of AMF0 values on the wire.
 * The conventional @c onMetaData layout is two values: a String
 * (the data-event name, e.g. @c "onMetaData") and an EcmaArray (the
 * key/value bag).  This class fixes that layout because no other
 * SCRIPTDATA shape is meaningful to RTMP today; broader tag bodies
 * remain accessible via @ref Amf0Reader.
 */
class FlvScriptTag {
        public:
                /** @brief Data-event name — typically @c "onMetaData". */
                promeki::String name;

                /** @brief Body — typically @ref Amf0Value::EcmaArray. */
                Amf0Value body;

                /** @brief Serialize this tag, appended to @p out. */
                Error pack(Buffer &out) const;

                /** @brief Parse an FLV SCRIPTDATA body from a single-slice BufferView. */
                static Error unpack(const BufferView &in, FlvScriptTag &out);
};

PROMEKI_NAMESPACE_END
