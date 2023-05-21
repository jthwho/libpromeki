/** 
 * @file audioblock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#include <promeki/audioblock.h>
#include <promeki/audiodesc.h>
#include <promeki/audio.h>

PROMEKI_NAMESPACE_BEGIN

AudioBlock::AudioBlock(const Config &config, ObjectBase *parent) :
        ObjectBase(parent), 
        _blockConfig(config) 
{
        for(size_t i = 0; i < _blockConfig.sinkChannels; ++i) {
                _sinkNameList += String::sprintf("Sink %d", (int)i);
        }
        for(size_t i = 0; i < _blockConfig.sourceChannels; ++i) {
                _sourceNameList += String::sprintf("Source %d", (int)i);
        }
}

AudioDesc AudioBlock::sourceDesc(size_t channel) const {
        return AudioDesc();
}

bool AudioBlock::setSourceDesc(size_t channel, const AudioDesc &val) {
        return false;
}

String AudioBlock::sourceName(size_t channel) const {
        return _sourceNameList.at(channel);
}

bool AudioBlock::setSourceName(size_t channel, const String &val) {
        return _sourceNameList.set(channel, val);
}

ssize_t AudioBlock::sourceSamplesAvailable(size_t channel) const {
        return -1;
}

AudioDesc AudioBlock::sinkDesc(size_t channel) const {
        return AudioDesc();
}

bool AudioBlock::setSinkDesc(size_t channel, const AudioDesc &val) {
        return false;
}

String AudioBlock::sinkName(size_t channel) const {
        return _sinkNameList.at(channel);
}

bool AudioBlock::setSinkName(size_t channel, const String &val) {
        return _sinkNameList.set(channel, val);
}

ssize_t AudioBlock::sinkSamplesAllowed(size_t channel) const {
        return -1;
}

PROMEKI_NAMESPACE_END

