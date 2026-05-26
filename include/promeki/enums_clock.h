/**
 * @file      enums_clock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Clock-epoch, byte-count formatting, and inspector-test enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Well-known Enum type for clock domain epoch behaviour.
 *
 * Describes whether timestamps from a @ref ClockDomain are comparable
 * across independent streams and/or across machines.
 *
 * - @c PerStream  — each stream has its own origin.  Timestamps are
 *   only meaningful within a single stream; cross-stream subtraction
 *   is undefined without an external synchronisation event.
 * - @c Correlated — all streams in this domain share a common epoch
 *   within a process or machine (e.g. CLOCK_MONOTONIC).  Subtracting
 *   timestamps from different streams on the same machine yields a
 *   meaningful offset.
 * - @c Absolute   — the epoch is a defined real-world time reference
 *   (e.g. PTP/TAI, GPS).  Timestamps from different machines are
 *   directly comparable.
 */
class ClockEpoch : public TypedEnum<ClockEpoch> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("ClockEpoch", "Clock Epoch", 1,
                                                   {"PerStream", 0, "Per-Stream Origin"},
                                                   {"Correlated", 1, "Machine-Correlated"},
                                                   {"Absolute", 2, "Absolute (PTP / TAI / GPS)"}); // default: Correlated

                using TypedEnum<ClockEpoch>::TypedEnum;

                static const ClockEpoch PerStream;
                static const ClockEpoch Correlated;
                static const ClockEpoch Absolute;
};

inline const ClockEpoch ClockEpoch::PerStream{0};
inline const ClockEpoch ClockEpoch::Correlated{1};
inline const ClockEpoch ClockEpoch::Absolute{2};

/**
 * @brief Well-known Enum type for human-readable byte-count formatting.
 *
 * Selects the unit family used by @ref Units::fromByteCount when
 * formatting an allocation size or similar byte-valued quantity:
 *
 * - @c Metric — powers of 1000 with SI suffixes: `B`, `KB`, `MB`,
 *               `GB`, `TB`, `PB`, `EB`.
 * - @c Binary — powers of 1024 with IEC suffixes: `B`, `KiB`, `MiB`,
 *               `GiB`, `TiB`, `PiB`, `EiB`.
 *
 * Inherits from @ref TypedEnum so function signatures can take
 * `const ByteCountStyle &` and get compile-time type checking —
 * other enum kinds (e.g. @c VideoPattern) will fail to compile
 * when passed in the same slot.  Runtime compatibility with
 * @ref Variant and any API that takes a plain @ref Enum is
 * preserved via implicit derived-to-base slicing.
 *
 * @par Example
 * @code
 * String s = Units::fromByteCount(1048576, 3, ByteCountStyle::Binary);
 * // → "1 MiB"
 *
 * // Round-trip through Variant still works:
 * Variant v = ByteCountStyle::Binary;          // Enum slice
 * Enum e = v.get<Enum>();                      // "ByteCountStyle::Binary"
 * ByteCountStyle back{e.value()};              // back to typed
 * @endcode
 */
class ByteCountStyle : public TypedEnum<ByteCountStyle> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("ByteCountStyle", "Byte Count Style", 0,
                                                   {"Metric", 0, "Metric (Powers of 1000)"},
                                                   {"Binary", 1, "Binary (Powers of 1024)"}); // default: Metric

                using TypedEnum<ByteCountStyle>::TypedEnum;

                static const ByteCountStyle Metric; ///< Powers of 1000 (`KB`, `MB`, ...).
                static const ByteCountStyle Binary; ///< Powers of 1024 (`KiB`, `MiB`, ...).
};

inline const ByteCountStyle ByteCountStyle::Metric{0};
inline const ByteCountStyle ByteCountStyle::Binary{1};

/**
 * @brief Well-known Enum type for @c InspectorMediaIO test selection.
 *
 * Element type for the @ref MediaConfig::InspectorTests EnumList — the
 * inspector consumes a list of tests to run.  An empty list runs the
 * default suite (every value below); a non-empty list runs exactly the
 * listed tests and disables the rest.
 *
 * - @c ImageData  — decode the @c ImageDataEncoder bands carried in
 *                   the picture (frame number, stream ID, picture TC).
 * - @c Ltc        — decode LTC from the audio track (independent
 *                   feature; not required by @c AvSync).
 * - @c AvSync     — A/V sync offset in samples, derived from the
 *                   shared frame-number marker that
 *                   @ref ImageDataEncoder and @ref AudioDataEncoder
 *                   both stamp.  Implies @c ImageData + @c AudioData.
 * - @c Continuity — frame number / stream ID / TC continuity.
 *                   Implies @c ImageData.
 * - @c Timestamp    — per-essence @ref MediaTimeStamp existence
 *                     check, frame-to-frame delta (min / max / avg),
 *                     and actual observed FPS.
 * - @c AudioSamples — per-frame audio sample count (min / max / avg)
 *                     plus measured audio sample rate derived from
 *                     cumulative samples and audio MediaTimeStamps.
 * - @c CaptureStats — write a per-frame TSV record (timestamps, image
 *                     and audio formats, buffer sizes) to the file
 *                     named by @ref MediaConfig::InspectorStatsFile
 *                     (or a unique file in @c Dir::temp() when that
 *                     key is empty).
 * - @c AudioData    — decode the @ref AudioDataEncoder
 *                     @c [stream:8][channel:8][frame:48] codeword from
 *                     every audio channel, validate CRC + sync nibble,
 *                     and flag mismatched channel bytes as
 *                     @ref InspectorDiscontinuity::AudioChannelMismatch.
 *                     Default-on; opt out when the upstream carries no
 *                     @c PcmMarker channels.
 */
class InspectorTest : public TypedEnum<InspectorTest> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("InspectorTest", "Inspector Test", 0,
                                                   {"ImageData", 0, "Image Data"}, {"Ltc", 1, "LTC Audio Timecode"},
                                                   {"AvSync", 2, "A/V Sync"}, {"Continuity", 3, "Continuity"},
                                                   {"Timestamp", 4, "Timestamp"},
                                                   {"AudioSamples", 5, "Audio Sample Count"},
                                                   {"CaptureStats", 6, "Capture Statistics"},
                                                   {"AudioData", 7, "Audio Data"}, {"AncData", 8, "Ancillary Data"});

                using TypedEnum<InspectorTest>::TypedEnum;

                static const InspectorTest ImageData;
                static const InspectorTest Ltc;
                static const InspectorTest AvSync;
                static const InspectorTest Continuity;
                static const InspectorTest Timestamp;
                static const InspectorTest AudioSamples;
                static const InspectorTest CaptureStats;
                static const InspectorTest AudioData;
                static const InspectorTest AncData;
};

inline const InspectorTest InspectorTest::ImageData{0};
inline const InspectorTest InspectorTest::Ltc{1};
inline const InspectorTest InspectorTest::AvSync{2};
inline const InspectorTest InspectorTest::Continuity{3};
inline const InspectorTest InspectorTest::Timestamp{4};
inline const InspectorTest InspectorTest::AudioSamples{5};
inline const InspectorTest InspectorTest::CaptureStats{6};
inline const InspectorTest InspectorTest::AudioData{7};
inline const InspectorTest InspectorTest::AncData{8};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
