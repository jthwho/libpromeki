/**
 * @file      proav/testpatternnode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/audiolevel.h>
#include <promeki/core/timecodegenerator.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/videodesc.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/proav/audiogen.h>
#include <promeki/proav/ltcencoder.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Source node that generates video and audio test patterns.
 * @ingroup proav_pipeline
 *
 * Produces complete Frame objects with synchronized video, audio, and
 * timecode metadata on each process cycle. The video pattern, audio mode,
 * and timecode are all configurable via build().
 *
 * This is a source node: no inputs, one Frame output.
 *
 * @par Config options
 * - `pattern` (String): Test pattern name (default: "colorbars").
 *   Values: colorbars, colorbars75, ramp, grid, crosshatch, checkerboard,
 *   solidcolor, white, black, noise, zoneplate.
 * - `width` (uint32_t): Frame width (required).
 * - `height` (uint32_t): Frame height (required).
 * - `pixelFormat` (int): PixelFormat::ID (default: PixelFormat::RGB8).
 * - `frameRate` (String): Frame rate string (required). E.g. "24", "29.97".
 * - `solidColorR` (uint16_t): Red for SolidColor pattern (0-65535).
 * - `solidColorG` (uint16_t): Green for SolidColor pattern.
 * - `solidColorB` (uint16_t): Blue for SolidColor pattern.
 * - `motion` (double): Pattern motion speed (default: 0.0).
 * - `startTimecode` (String): Starting timecode (default: "00:00:00:00").
 * - `dropFrame` (bool): Enable drop-frame timecode.
 * - `audioEnabled` (bool): Enable audio generation (default: true).
 * - `audioMode` (String): Audio mode: "tone", "silence", "ltc" (default: "tone").
 * - `audioRate` (float): Audio sample rate (default: 48000).
 * - `audioChannels` (int): Audio channels (default: 2).
 * - `toneFrequency` (double): Tone frequency in Hz (default: 1000).
 * - `toneLevel` (double): Tone level in dBFS (default: -20).
 * - `ltcLevel` (double): LTC level in dBFS (default: -20).
 * - `ltcChannel` (int): Channel for LTC (-1 = all, default: 0).
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("TestPatternNode", "source");
 * cfg.set("pattern", Variant(String("colorbars")));
 * cfg.set("width", Variant(uint32_t(1920)));
 * cfg.set("height", Variant(uint32_t(1080)));
 * cfg.set("frameRate", Variant(String("29.97")));
 * @endcode
 */
class TestPatternNode : public MediaNode {
        PROMEKI_OBJECT(TestPatternNode, MediaNode)
        public:
                /** @brief Video test pattern type. */
                enum Pattern {
                        ColorBars,      ///< @brief SMPTE 100% color bars.
                        ColorBars75,    ///< @brief SMPTE 75% color bars.
                        Ramp,           ///< @brief Luminance gradient ramp.
                        Grid,           ///< @brief White grid lines on black.
                        Crosshatch,     ///< @brief Diagonal crosshatch lines.
                        Checkerboard,   ///< @brief Alternating black/white squares.
                        SolidColor,     ///< @brief Solid fill with configured color.
                        White,          ///< @brief Solid white.
                        Black,          ///< @brief Solid black.
                        Noise,          ///< @brief Random pixel noise.
                        ZonePlate       ///< @brief Circular zone plate.
                };

                /** @brief Audio generation mode. */
                enum AudioMode {
                        Tone,           ///< @brief Sine tone (configurable frequency).
                        Silence,        ///< @brief Silence.
                        LTC             ///< @brief LTC timecode audio.
                };

                /**
                 * @brief Constructs a TestPatternNode.
                 * @param parent Optional parent object.
                 */
                TestPatternNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~TestPatternNode();

                BuildResult build(const MediaNodeConfig &config) override;

                /**
                 * @brief Returns test pattern node statistics.
                 * @return A map containing framesGenerated and currentTimecode.
                 */
                Map<String, Variant> extendedStats() const override;

        protected:
                Error start() override;
                void process() override;
                void stop() override;

        private:
                // Video config
                Pattern                 _pattern = ColorBars;
                VideoDesc               _videoDesc;
                uint16_t                _solidR = 0;
                uint16_t                _solidG = 0;
                uint16_t                _solidB = 0;
                double                  _motion = 0.0;
                double                  _motionOffset = 0.0;

                // Timecode
                TimecodeGenerator       _tcGen;
                uint64_t                _frameCount = 0;

                // Audio config
                AudioDesc               _audioDesc;
                bool                    _audioEnabled = true;
                AudioMode               _audioMode = Tone;
                double                  _toneFreq = 1000.0;
                AudioLevel              _toneLevel = AudioLevel::fromDbfs(-20.0);
                AudioLevel              _ltcLevel = AudioLevel::fromDbfs(-20.0);
                int                     _ltcChannel = 0;
                List<AudioGen::Config>  _channelConfigs;

                // Runtime state
                AudioGen                *_audioGen = nullptr;
                LtcEncoder              *_ltcEncoder = nullptr;
                ImageDesc               _imageDesc;
                size_t                  _samplesPerFrame = 0;

                // Config parsing helpers
                static bool parsePattern(const String &name, Pattern &out);
                static bool parseAudioMode(const String &name, AudioMode &out);
                static bool parseFrameRate(const String &str, FrameRate &out);

                // Pattern rendering
                void renderPattern(Image &img, double motionOffset);
                void renderColorBars(Image &img, double offset, bool full);
                void renderRamp(Image &img, double offset);
                void renderGrid(Image &img, double offset);
                void renderCrosshatch(Image &img, double offset);
                void renderCheckerboard(Image &img, double offset);
                void renderZonePlate(Image &img, double phase);
                void renderNoise(Image &img);
                void renderSolid(Image &img, uint16_t r, uint16_t g, uint16_t b);
};

PROMEKI_NAMESPACE_END
