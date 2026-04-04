/**
 * @file      testpatternnode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/audiolevel.h>
#include <promeki/mutex.h>
#include <promeki/timecodegenerator.h>
#include <promeki/medianode.h>
#include <promeki/videodesc.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/image.h>
#include <promeki/videotestpattern.h>
#include <promeki/audiotestpattern.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Source node that generates video and audio test patterns.
 * @ingroup pipeline
 *
 * Produces complete Frame objects with synchronized video, audio, and
 * timecode metadata on each process cycle. The video pattern, audio mode,
 * and timecode are all configurable via build().
 *
 * This is a source node: no inputs, one Frame output.
 *
 * Video pattern generation is delegated to VideoTestPattern and audio
 * generation to AudioTestPattern.
 *
 * @par Config options
 * - `Pattern` (String): Test pattern name (default: "colorbars").
 *   Values: colorbars, colorbars75, ramp, grid, crosshatch, checkerboard,
 *   solidcolor, white, black, noise, zoneplate.
 * - `Width` (uint32_t): Frame width (required).
 * - `Height` (uint32_t): Frame height (required).
 * - `PixelFormat` (PixelDesc): Pixel description (default: PixelDesc::RGB8_sRGB_Full).
 * - `FrameRate` (String): Frame rate string (required). E.g. "24", "29.97".
 * - `SolidColorR` (uint16_t): Red for SolidColor pattern (0-65535).
 * - `SolidColorG` (uint16_t): Green for SolidColor pattern.
 * - `SolidColorB` (uint16_t): Blue for SolidColor pattern.
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
 * cfg.set("Width", uint32_t(1920));
 * cfg.set("Height", uint32_t(1080));
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
                // Video
                VideoTestPattern        _videoPattern;
                VideoDesc               _videoDesc;
                ImageDesc               _imageDesc;
                double                  _motion = 0.0;
                double                  _motionOffset = 0.0;
                Image                   _cachedImage;

                // Timecode
                TimecodeGenerator       _tcGen;
                uint64_t                _frameCount = 0;

                // Audio
                AudioTestPattern        *_audioPattern = nullptr;
                AudioDesc               _audioDesc;
                bool                    _audioEnabled = true;
                size_t                  _samplesPerFrame = 0;

                // Thread safety for extendedStats()
                mutable Mutex           _statsMutex;
                uint64_t                _statsFrameCount = 0;
                Timecode                _statsTimecode;

};

PROMEKI_NAMESPACE_END
