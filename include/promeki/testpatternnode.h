/**
 * @file      testpatternnode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mutex.h>
#include <promeki/medianode.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Source node that generates video and audio test patterns.
 * @ingroup pipeline
 *
 * Produces complete Frame objects with synchronized video, audio, and
 * timecode metadata on each process cycle.  Delegates all generation
 * to a MediaIO_TPG instance.
 *
 * This is a source node: no inputs, one Frame output.
 *
 * @par Config options
 * - `Pattern` (String): Test pattern name (default: "colorbars").
 *   Values: colorbars, colorbars75, ramp, grid, crosshatch, checkerboard,
 *   solidcolor, white, black, noise, zoneplate.
 * - `Size` (Size2Du32): Frame size (default: 1920x1080).
 * - `PixelFormat` (PixelDesc): Pixel description (default: PixelDesc::RGB8_sRGB).
 * - `FrameRate` (String): Frame rate string (required). E.g. "24", "29.97".
 * - `SolidColor` (Color): Fill color for SolidColor pattern (default: black).
 * - `Motion` (double): Pattern motion speed (default: 0.0).
 * - `StartTimecode` (String): Starting timecode (default: "00:00:00:00").
 * - `DropFrame` (bool): Enable drop-frame timecode.
 * - `AudioEnabled` (bool): Enable audio generation (default: true).
 * - `AudioMode` (String): Audio mode: "tone", "silence", "ltc" (default: "tone").
 * - `AudioRate` (float): Audio sample rate (default: 48000).
 * - `AudioChannels` (int): Audio channels (default: 2).
 * - `ToneFrequency` (double): Tone frequency in Hz (default: 1000).
 * - `ToneLevel` (double): Tone level in dBFS (default: -20).
 * - `LtcLevel` (double): LTC level in dBFS (default: -20).
 * - `LtcChannel` (int): Channel for LTC (-1 = all, default: 0).
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("TestPatternNode", "source");
 * cfg.set("Pattern", "colorbars");
 * cfg.set("Size", Size2Du32(1920, 1080));
 * cfg.set("FrameRate", "29.97");
 * @endcode
 */
class TestPatternNode : public MediaNode {
        PROMEKI_OBJECT(TestPatternNode, MediaNode)
        public:
                /**
                 * @brief Constructs a TestPatternNode.
                 * @param parent Optional parent object.
                 */
                TestPatternNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~TestPatternNode();

                MediaNodeConfig defaultConfig() const override;
                BuildResult build(const MediaNodeConfig &config) override;

                /**
                 * @brief Returns test pattern node statistics.
                 * @return A map containing FramesGenerated and CurrentTimecode.
                 */
                Map<String, Variant> extendedStats() const override;

        protected:
                Error start() override;
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;
                void stop() override;

        private:
                MediaIO                 *_io = nullptr;

                // Thread safety for extendedStats()
                mutable Mutex           _statsMutex;
                int64_t                 _statsFrameCount = 0;
                Timecode                _statsTimecode;
};

PROMEKI_NAMESPACE_END
