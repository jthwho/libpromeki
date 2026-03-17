/**
 * @file      core/audiolevel.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <limits>
#include <promeki/core/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class String;

/**
 * @brief Audio level in dBFS (decibels relative to full scale).
 * @ingroup core_audio
 *
 * Simple value type that stores level as dBFS and converts to/from
 * linear amplitude. 0 dBFS is full scale (linear 1.0), -6 dBFS is
 * approximately half amplitude, and silence is negative infinity.
 *
 * @par Example
 * @code
 * AudioLevel level = AudioLevel::fromDbfs(-20.0);
 * double linear = level.toLinear(); // ~0.1
 *
 * AudioLevel half = AudioLevel::fromLinear(0.5);
 * double db = half.dbfs(); // ~-6.02
 * @endcode
 */
class AudioLevel {
        public:
                /**
                 * @brief Creates an AudioLevel from a dBFS value.
                 * @param dbfs Level in dBFS (0.0 = full scale).
                 * @return An AudioLevel at the specified dBFS value.
                 */
                static AudioLevel fromDbfs(double dbfs) {
                        return AudioLevel(dbfs);
                }

                /**
                 * @brief Creates an AudioLevel from a linear amplitude.
                 * @param linear Linear amplitude (0.0-1.0).
                 * @return An AudioLevel converted from the linear value.
                 */
                static AudioLevel fromLinear(double linear) {
                        if(linear <= 0.0) return AudioLevel(-std::numeric_limits<double>::infinity());
                        return AudioLevel(20.0 * std::log10(linear));
                }

                /** @brief Default constructor. Creates silence (-inf dBFS). */
                AudioLevel() : _dbfs(-std::numeric_limits<double>::infinity()) { }

                /**
                 * @brief Constructs from a dBFS value.
                 * @param dbfs Level in dBFS.
                 */
                explicit AudioLevel(double dbfs) : _dbfs(dbfs) { }

                /** @brief Returns the level in dBFS. */
                double dbfs() const { return _dbfs; }

                /**
                 * @brief Converts to linear amplitude.
                 * @return Linear amplitude (0.0 for silence, 1.0 for 0 dBFS).
                 */
                double toLinear() const {
                        if(std::isinf(_dbfs) && _dbfs < 0) return 0.0;
                        return std::pow(10.0, _dbfs / 20.0);
                }

                /**
                 * @brief Converts to float linear amplitude.
                 * @return Linear amplitude as float.
                 */
                float toLinearFloat() const { return static_cast<float>(toLinear()); }

                /** @brief Returns true if the level is silence (-inf dBFS). */
                bool isSilence() const { return std::isinf(_dbfs) && _dbfs < 0; }

                /** @brief Returns true if the level exceeds 0 dBFS (clipping). */
                bool isClipping() const { return _dbfs > 0.0; }

                /**
                 * @brief Returns the level as a formatted string.
                 * @return A string in the form "X.X dBFS" or "-inf dBFS" for silence.
                 */
                String toString() const;

                /** @brief Equality comparison. */
                bool operator==(const AudioLevel &rhs) const { return _dbfs == rhs._dbfs; }
                /** @brief Inequality comparison. */
                bool operator!=(const AudioLevel &rhs) const { return _dbfs != rhs._dbfs; }
                /** @brief Less-than comparison. */
                bool operator<(const AudioLevel &rhs) const { return _dbfs < rhs._dbfs; }
                /** @brief Greater-than comparison. */
                bool operator>(const AudioLevel &rhs) const { return _dbfs > rhs._dbfs; }
                /** @brief Less-than-or-equal comparison. */
                bool operator<=(const AudioLevel &rhs) const { return _dbfs <= rhs._dbfs; }
                /** @brief Greater-than-or-equal comparison. */
                bool operator>=(const AudioLevel &rhs) const { return _dbfs >= rhs._dbfs; }

        private:
                double _dbfs; ///< Level in dBFS (0.0 = full scale, -inf = silence).
};

PROMEKI_NAMESPACE_END
