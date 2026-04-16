/**
 * @file      timecodegenerator.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/framerate.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief General-purpose timecode generator.
 * @ingroup time
 *
 * Produces a sequence of timecode values with controllable direction
 * and jam capability. Automatically derives the correct Timecode::Mode
 * from the configured frame rate and drop-frame setting.
 *
 * This is a simple value class — cheap to copy, no shared state.
 *
 * @par Example
 * @code
 * TimecodeGenerator gen;
 * gen.setFrameRate(FrameRate(FrameRate::FPS_29_97));
 * gen.setTimecode(Timecode(Timecode::NDF30, 1, 0, 0, 0));
 * for(int i = 0; i < 100; i++) {
 *     Timecode tc = gen.advance();
 *     // tc is the timecode for this frame
 * }
 * @endcode
 */
class TimecodeGenerator {
        public:
                /** @brief Run mode for the generator. */
                enum RunMode {
                        Still,          ///< @brief Timecode does not advance.
                        Forward,        ///< @brief Timecode increments each frame.
                        Reverse         ///< @brief Timecode decrements each frame.
                };

                /** @brief Constructs a default generator with no frame rate set. */
                TimecodeGenerator() = default;

                /**
                 * @brief Constructs a generator with the given frame rate.
                 * @param frameRate The frame rate.
                 * @param dropFrame Whether to use drop-frame counting (only at 30000/1001).
                 */
                TimecodeGenerator(const FrameRate &frameRate, bool dropFrame = false);

                /**
                 * @brief Returns the configured frame rate.
                 * @return The frame rate.
                 */
                FrameRate frameRate() const { return _frameRate; }

                /**
                 * @brief Sets the frame rate and re-derives the timecode mode.
                 * @param frameRate The frame rate.
                 */
                void setFrameRate(const FrameRate &frameRate);

                /**
                 * @brief Returns whether drop-frame counting is enabled.
                 * @return true if drop-frame is active.
                 */
                bool dropFrame() const { return _dropFrame; }

                /**
                 * @brief Enables or disables drop-frame counting.
                 *
                 * Only takes effect when the frame rate is 30000/1001. At all other
                 * rates, drop-frame is forced to false. Recalculates the timecode mode.
                 *
                 * @param df true to enable drop-frame.
                 */
                void setDropFrame(bool df);

                /**
                 * @brief Returns the resolved timecode mode.
                 * @return The Timecode::Mode derived from the frame rate and drop-frame setting.
                 */
                Timecode::Mode timecodeMode() const { return _mode; }

                /**
                 * @brief Returns the current run mode.
                 * @return The RunMode (Still, Forward, or Reverse).
                 */
                RunMode runMode() const { return _runMode; }

                /**
                 * @brief Sets the run mode.
                 * @param mode The run mode (Still, Forward, or Reverse).
                 */
                void setRunMode(RunMode mode) { _runMode = mode; return; }

                /**
                 * @brief Returns the current timecode value.
                 * @return The current Timecode.
                 */
                Timecode timecode() const { return _timecode; }

                /**
                 * @brief Sets the current timecode value.
                 *
                 * Also sets the initial value used by reset().
                 *
                 * @param tc The timecode to set.
                 */
                void setTimecode(const Timecode &tc);

                /**
                 * @brief Jams to a new timecode value mid-run.
                 *
                 * Equivalent to setTimecode() but semantically distinct —
                 * use for mid-run resync. Does not change the reset value.
                 *
                 * @param tc The timecode to jam to.
                 */
                void jam(const Timecode &tc);

                /**
                 * @brief Advances the generator by one frame.
                 *
                 * Returns the **previous** timecode value (the value for the current
                 * frame). Forward increments, Reverse decrements, Still returns
                 * the same value repeatedly.
                 *
                 * @return The timecode value before advancing.
                 */
                Timecode advance();

                /**
                 * @brief Returns the total number of frames advanced.
                 * @return The number of advance() calls since construction or reset.
                 */
                uint64_t frameCount() const { return _frameCount; }

                /**
                 * @brief Resets the generator.
                 *
                 * Resets frameCount to 0 and timecode to the value set by setTimecode().
                 */
                void reset();

        private:
                FrameRate       _frameRate;
                bool            _dropFrame = false;
                Timecode::Mode  _mode;
                RunMode         _runMode = Forward;
                Timecode        _timecode;
                Timecode        _startTimecode;
                uint64_t        _frameCount = 0;

                void deriveMode();
                void applyMode();
};

PROMEKI_NAMESPACE_END
