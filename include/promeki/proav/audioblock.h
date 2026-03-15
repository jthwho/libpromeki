/**
 * @file      proav/audioblock.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class String;
class AudioDesc;

/**
 * @brief Base class for an audio processing block
 * This object defines an interface for composing an audio processing chain.
 */
class AudioBlock : public ObjectBase {
        PROMEKI_OBJECT(AudioBlock, ObjectBase);
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
                 * @brief Constructs an AudioBlock with a given configuration.
                 * @param config Block configuration specifying source and sink channel counts.
                 * @param parent Optional parent object.
                 */
                AudioBlock(const Config &config, ObjectBase *parent = nullptr);

                /** @brief Virtual destructor. */
                virtual ~AudioBlock() {}

                /**
                 * @brief Returns true if the audio block is a source
                 */
                bool isSource() const { return _blockConfig.sourceChannels > 0; }

                /**
                 * @brief Returns true if the given source index is valid.
                 * @param val Zero-based source channel index to check.
                 * @return true if the index is within the source channel range.
                 */
                bool isSourceValid(size_t val) const { return val < _blockConfig.sourceChannels; }
                
                /**
                 * @brief Returns the number of channels this object can source
                 */
                size_t sourceChannels() const { return _blockConfig.sourceChannels; }

                /**
                 * @brief Returns the audio description for a given source channel.
                 * @param channel Zero-based source channel index.
                 * @return The AudioDesc for the specified channel.
                 */
                virtual AudioDesc sourceDesc(size_t channel) const;

                /**
                 * @brief Sets the audio description for a given source channel.
                 * @param channel Zero-based source channel index.
                 * @param val     The AudioDesc to set.
                 * @return true on success, false if the channel index is invalid.
                 */
                virtual bool setSourceDesc(size_t channel, const AudioDesc &val);

                /**
                 * @brief Returns the name of a given source channel.
                 * @param channel Zero-based source channel index.
                 * @return The channel name.
                 */
                virtual String sourceName(size_t channel) const;

                /**
                 * @brief Sets the name of a given source channel.
                 * @param channel Zero-based source channel index.
                 * @param val     The name to assign.
                 * @return true on success, false if the channel index is invalid.
                 */
                virtual bool setSourceName(size_t channel, const String &val);

                /**
                 * @brief Returns the number of samples available on a source channel.
                 * @param channel Zero-based source channel index.
                 * @return Number of samples available, or -1 if unknown.
                 */
                virtual ssize_t sourceSamplesAvailable(size_t channel) const;

                /**
                 * @brief Signal emitted when a source has samples available
                 * @signal
                 */
                PROMEKI_SIGNAL(sourceHasSamples, AudioBlock *, size_t);

                /**
                 * @brief Returns true if the object is an audio sink
                 */
                bool isSink() const { return _blockConfig.sinkChannels > 0; }

                /**
                 * @brief Returns true if the given sink channel index is valid.
                 * @param val Zero-based sink channel index to check.
                 * @return true if the index is within the sink channel range.
                 */
                bool isSinkValid(size_t val) const { return val < _blockConfig.sinkChannels; }

                /**
                 * @brief Returns the number of sink channels
                 */
                size_t sinkChannels() const { return _blockConfig.sinkChannels; }

                /**
                 * @brief Returns the audio description of a given sink channel.
                 * @param channel Zero-based sink channel index.
                 * @return The AudioDesc for the specified channel.
                 */
                virtual AudioDesc sinkDesc(size_t channel) const;

                /**
                 * @brief Sets the audio description of a given sink channel.
                 * @param channel Zero-based sink channel index.
                 * @param val     The AudioDesc to set.
                 * @return true on success, false if the channel index is invalid.
                 */
                virtual bool setSinkDesc(size_t channel, const AudioDesc &val);

                /**
                 * @brief Returns the name of a given sink channel.
                 * @param channel Zero-based sink channel index.
                 * @return The channel name.
                 */
                virtual String sinkName(size_t channel) const;

                /**
                 * @brief Sets the name of a given sink channel.
                 * @param channel Zero-based sink channel index.
                 * @param val     The name to assign.
                 * @return true on success, false if the channel index is invalid.
                 */
                virtual bool setSinkName(size_t channel, const String &val);

                /**
                 * @brief Returns the number of samples a sink channel can currently accept.
                 * @param channel Zero-based sink channel index.
                 * @return Number of samples the channel can accept, or -1 if unknown.
                 */
                virtual ssize_t sinkSamplesAllowed(size_t channel) const;

                /**
                 * @brief Signal emitted whenever a sink channel can accept more samples
                 * @signal
                 */
                PROMEKI_SIGNAL(sinkReadyForSamples, AudioBlock *, size_t);

        private:
                Config          _blockConfig;
                StringList      _sourceNameList;
                StringList      _sinkNameList;

};

PROMEKI_NAMESPACE_END

