/**
 * @file      proav/testpatternnode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
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
 * and timecode are all configurable.
 *
 * This is a source node: no inputs, one Frame output.
 *
 * @par Example
 * @code
 * TestPatternNode *src = new TestPatternNode();
 * src->setPattern(TestPatternNode::ColorBars);
 * src->setVideoDesc(videoDesc);
 * src->setAudioDesc(audioDesc);
 * src->setStartTimecode(Timecode(Timecode::NDF24, 1, 0, 0, 0));
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

                // ---- Video configuration ----

                /**
                 * @brief Sets the video test pattern.
                 * @param p The pattern to generate.
                 */
                void setPattern(Pattern p) { _pattern = p; return; }

                /** @brief Returns the current pattern. */
                Pattern pattern() const { return _pattern; }

                /**
                 * @brief Sets the video description (frame rate, resolution, pixel format).
                 * @param desc The video description.
                 */
                void setVideoDesc(const VideoDesc &desc) { _videoDesc = desc; return; }

                /** @brief Returns the video description. */
                const VideoDesc &videoDesc() const { return _videoDesc; }

                /**
                 * @brief Sets the solid color for SolidColor pattern.
                 * @param r Red component (0-65535).
                 * @param g Green component (0-65535).
                 * @param b Blue component (0-65535).
                 */
                void setSolidColor(uint16_t r, uint16_t g, uint16_t b) {
                        _solidR = r; _solidG = g; _solidB = b;
                        return;
                }

                // ---- Motion ----

                /**
                 * @brief Sets the pattern motion speed.
                 * @param speed 0.0 = static, positive = forward, negative = reverse.
                 *              1.0 = one pattern-width per second.
                 */
                void setMotion(double speed) { _motion = speed; return; }

                /** @brief Returns the motion speed. */
                double motion() const { return _motion; }

                // ---- Timecode ----

                /** @brief Returns a mutable reference to the internal timecode generator. */
                TimecodeGenerator &timecodeGenerator() { return _tcGen; }

                /** @brief Returns a const reference to the internal timecode generator. */
                const TimecodeGenerator &timecodeGenerator() const { return _tcGen; }

                /**
                 * @brief Sets the starting timecode.
                 * @param tc The starting timecode value.
                 */
                void setStartTimecode(const Timecode &tc) { _tcGen.setTimecode(tc); return; }

                /**
                 * @brief Enables or disables drop-frame timecode.
                 * @param df true for drop-frame (only effective at 30000/1001 fps).
                 */
                void setDropFrame(bool df) { _tcGen.setDropFrame(df); return; }

                /** @brief Returns the current timecode value. */
                Timecode currentTimecode() const { return _tcGen.timecode(); }

                /** @brief Returns the total number of frames generated. */
                uint64_t frameCount() const { return _frameCount; }

                // ---- Audio ----

                /**
                 * @brief Sets the audio description.
                 * @param desc The audio format description.
                 */
                void setAudioDesc(const AudioDesc &desc) { _audioDesc = desc; return; }

                /** @brief Returns the audio description. */
                const AudioDesc &audioDesc() const { return _audioDesc; }

                /**
                 * @brief Enables or disables audio generation.
                 * @param enable true to enable audio output.
                 */
                void setAudioEnabled(bool enable) { _audioEnabled = enable; return; }

                /** @brief Returns true if audio generation is enabled. */
                bool audioEnabled() const { return _audioEnabled; }

                /**
                 * @brief Sets the audio generation mode.
                 * @param mode Tone, Silence, or LTC.
                 */
                void setAudioMode(AudioMode mode) { _audioMode = mode; return; }

                /** @brief Returns the current audio mode. */
                AudioMode audioMode() const { return _audioMode; }

                /**
                 * @brief Sets per-channel audio configuration (Tone mode).
                 * @param chan Channel index.
                 * @param config The audio generator config for that channel.
                 */
                void setChannelConfig(size_t chan, AudioGen::Config config);

                /**
                 * @brief Sets all channels to a sine tone at the given frequency.
                 * @param hz Frequency in Hz.
                 */
                void setToneFrequency(double hz) { _toneFreq = hz; return; }

                /**
                 * @brief Sets the amplitude for all tone channels.
                 * @param amplitude Amplitude (0.0-1.0).
                 */
                void setToneAmplitude(double amplitude) { _toneAmplitude = amplitude; return; }

                /**
                 * @brief Sets the LTC output amplitude.
                 * @param level Level (0.0-1.0).
                 */
                void setLtcLevel(float level) { _ltcLevel = level; return; }

                /**
                 * @brief Sets which channel carries LTC.
                 * @param chan Channel index, or -1 for all channels.
                 */
                void setLtcChannel(int chan) { _ltcChannel = chan; return; }

                // ---- Lifecycle overrides ----

                Error configure() override;
                Error start() override;
                void process() override;
                void stop() override;

                // ---- Extended stats ----

                Map<String, Variant> extendedStats() const override;

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
                double                  _toneAmplitude = 0.5;
                float                   _ltcLevel = 0.5f;
                int                     _ltcChannel = 0;
                List<AudioGen::Config>  _channelConfigs;

                // Runtime state
                AudioGen                *_audioGen = nullptr;
                LtcEncoder              *_ltcEncoder = nullptr;
                ImageDesc               _imageDesc;
                size_t                  _samplesPerFrame = 0;

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
