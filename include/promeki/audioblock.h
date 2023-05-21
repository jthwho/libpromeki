/*****************************************************************************
 * audioblock.h
 * May 19, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class String;
class AudioDesc;

/**
 * @brief Base class for an audio processing block
 * This object defines an interface for composing an audio processing chain.
 */
class AudioBlock : public ObjectBase {
        PROMEKI_OBJECT
        public: 
                /**
                 * @brief Config used by the derived class to configure AudioBlock
                 * This config is passed in by the derived class to configure the
                 * audio block on instantiation.
                 */
                class Config {
                        public:
                                /**
                                 * @brief Number of channels this block can source
                                 * Should be set to zero if this block can not source any channels
                                 */
                                size_t  sourceChannels = 0;
                                /**
                                 * @brief Number of channels this block can sink
                                 * Should be set to zero if this block can not sink any channels
                                 */
                                 size_t  sinkChannels = 0;
                };

                /**
                 * @brief Constructs an AudioBlock with a given configuration
                 */
                AudioBlock(const Config &config, ObjectBase *parent = nullptr);
                virtual ~AudioBlock() {}

                /**
                 * @brief Returns true if the audio block is a source
                 */
                bool isSource() const { return _blockConfig.sourceChannels > 0; }

                /**
                 * @brief Returns true if the given source index is valid
                 */
                bool isSourceValid(size_t val) const { return val < _blockConfig.sourceChannels; }
                
                /**
                 * @brief Returns the number of channels this object can source
                 */
                size_t sourceChannels() const { return _blockConfig.sourceChannels; }

                /**
                 * @brief Returns the audio description for a given source channel
                 */
                virtual AudioDesc sourceDesc(size_t channel) const;

                /**
                 * @brief Sets the audio description for a given source channel
                 */
                virtual bool setSourceDesc(size_t channel, const AudioDesc &val);

                /**
                 * @brief Returns the name of a given source channel
                 */
                virtual String sourceName(size_t channel) const;

                /**
                 * @brief Sets the name of a given source channel
                 */
                virtual bool setSourceName(size_t channel, const String &val);

                /**
                 * @brief Returns the number of samples that are available on a source channel
                 * @return Number of samples available or -1 if unknown 
                 */
                virtual ssize_t sourceSamplesAvailable(size_t channel) const;

                /**
                 * @brief Returns true if the object is an audio sink
                 */
                bool isSink() const { return _blockConfig.sinkChannels > 0; }

                /**
                 * @brief Returns true if the given sink channel is valid
                 */
                bool isSinkValid(size_t val) const { return val < _blockConfig.sinkChannels; }

                /**
                 * @brief Returns the number of sink channels
                 */
                size_t sinkChannels() const { return _blockConfig.sinkChannels; }

                /**
                 * @brief Returns the audio description of a given sink channel
                 */
                virtual AudioDesc sinkDesc(size_t channel) const;

                /**
                 * @brief Sets the audio description of a given sink channel
                 */
                virtual bool setSinkDesc(size_t channel, const AudioDesc &val);

                /**
                 * @brief Returns the name of a given sink channel
                 */
                virtual String sinkName(size_t channel) const;

                /**
                 * @brief Sets the name of a given sink channel
                 */
                virtual bool setSinkName(size_t channel, const String &val);

                /**
                 * @brief Returns the number of samples a sink channel can currently accept
                 * Will return the number of sample the sink channel can accept or -1 if unknown
                 */
                virtual ssize_t sinkSamplesAllowed(size_t channel) const;

        private:
                Config          _blockConfig;
                StringList      _sourceNameList;
                StringList      _sinkNameList;

};

PROMEKI_NAMESPACE_END

