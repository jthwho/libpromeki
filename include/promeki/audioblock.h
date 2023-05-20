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
#include <promeki/list.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class String;
class AudioDesc;

// Base class for any object that does audio processing.
class AudioBlock {
        public: 
                class Config {
                        public:
                                // Number of channels this object can source,
                                // or 0 if not a source
                                size_t  sourceChannels = 0;
                                // Number of channels this object can sink,
                                // or 0 if not a sink
                                size_t  sinkChannels = 0;
                };

                AudioBlock(const Config &config) : _blockConfig(config) {}
                virtual ~AudioBlock() {}

                // Source Functions
                bool isSource() const { return _blockConfig.sourceChannels > 0; }
                bool isSourceValid(size_t val) const { return val < _blockConfig.sourceChannels; }
                size_t sourceChannels() const { return _blockConfig.sourceChannels; }
                virtual AudioDesc sourceDesc(size_t channel) const;
                virtual bool setSourceDesc(size_t channel, const AudioDesc &val);
                virtual String sourceName(size_t channel) const;
                virtual bool setSourceName(size_t channel, const String &val);
                virtual ssize_t sourceSamplesAvailable(size_t channel) const;
                virtual void setSourceNotify(size_t channel, NotifyFunc func);

                // Sink Functions
                bool isSink() const { return _blockConfig.sinkChannels > 0; }
                bool isSinkValid(size_t val) const { return val < _blockConfig.sinkValid; }
                size_t sinkChannels() const { return _blockConfig.sinkChannels; }
                virtual AudioDesc sinkDesc(size_t channel) const;
                virtual bool setSinkDesc(size_t channel, const AudioDesc &val);
                virtual String sinkName(size_t channel) const;
                virtual bool setSinkName(size_t channel, const String &val);
                virtual ssize_t sinkSamplesAllowed(size_t channel) const;
                virtual void setSinkNotify(size_t channel, NotifyFunc func);

        private:
                Config          _blockConfig;

};

PROMEKI_NAMESPACE_END

