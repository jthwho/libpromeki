/**
 * @file      rtpaudiosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/proav/rtpaudiosinknode.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/core/metadata.h>
#include <promeki/core/logger.h>
#include <promeki/network/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(RtpAudioSinkNode)

RtpAudioSinkNode::RtpAudioSinkNode(ObjectBase *parent) : MediaNode(parent) {
        setName("RtpAudioSinkNode");
        auto input = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Audio);
        addInputPort(input);
}

RtpAudioSinkNode::~RtpAudioSinkNode() {
        delete _session;
}

Error RtpAudioSinkNode::configure() {
        if(state() != Idle) return Error(Error::Invalid);

        if(_payload == nullptr) {
                emitError("No RTP payload handler set");
                return Error(Error::Invalid);
        }
        if(_destination.isNull()) {
                emitError("No destination address set");
                return Error(Error::Invalid);
        }

        // Compute samples per packet from packet time
        _samplesPerPacket = (size_t)(_packetTime * 0.001 * _clockRate);
        if(_samplesPerPacket == 0) {
                emitError("Invalid packet time / clock rate combination");
                return Error(Error::Invalid);
        }

        // Get bytes per sample frame from the output format (if set) or input port.
        // If the input port's audioDesc is not yet available (port descriptors
        // are not propagated until the first frame arrives), leave
        // _bytesPerSampleFrame at 0 so that process() detects the correct
        // value from the actual audio data.
        const AudioDesc &adesc = inputPort(0)->audioDesc();
        if(_outputFormat != AudioDesc::Invalid) {
                const AudioDesc::Format *fmt = AudioDesc::lookupFormat(_outputFormat);
                if(fmt == nullptr || fmt->bytesPerSample == 0) {
                        emitError("Invalid output format");
                        return Error(Error::Invalid);
                }
                if(adesc.isValid()) {
                        _bytesPerSampleFrame = fmt->bytesPerSample * adesc.channels();
                } else {
                        _bytesPerSampleFrame = 0;
                }
        } else {
                if(adesc.isValid()) {
                        _bytesPerSampleFrame = adesc.bytesPerSampleStride();
                } else {
                        _bytesPerSampleFrame = 0;
                }
        }

        _packetBytes = _samplesPerPacket * (_bytesPerSampleFrame > 0 ? _bytesPerSampleFrame : 1);

        // Allocate accumulation buffer (4x packet size for headroom)
        _accumBuffer = Buffer::Ptr::create(_packetBytes * 4);
        _accumOffset = 0;

        delete _session;
        _session = new RtpSession(this);
        _session->setPayloadType(_payloadType);
        _session->setClockRate(_clockRate);

        _rtpTimestamp = 0;
        _packetsSent = 0;
        _samplesSent = 0;
        _underrunCount = 0;

        setState(Configured);
        return Error(Error::Ok);
}

Error RtpAudioSinkNode::start() {
        if(state() != Configured) return Error(Error::Invalid);

        Error err = _session->start(SocketAddress::any(0));
        if(err.isError()) {
                emitError("Failed to start RTP session");
                return err;
        }

        _session->socket()->setDscp(_dscp);

        _accumOffset = 0;

        setState(Running);
        return Error(Error::Ok);
}

void RtpAudioSinkNode::stop() {
        flushRemaining();

        if(_session != nullptr) {
                _session->stop();
        }

        setState(Idle);
        return;
}

void RtpAudioSinkNode::process() {
        Frame::Ptr frame = dequeueInput();
        if(!frame.isValid()) return;

        if(frame->audioList().isEmpty()) return;

        Audio::Ptr audio = frame->audioList()[0];

        // Convert to output format if configured
        if(_outputFormat != AudioDesc::Invalid && audio->desc().dataType() != _outputFormat) {
                Audio converted = audio->convertTo(_outputFormat);
                if(!converted.isValid()) {
                        emitError("Audio format conversion failed");
                        return;
                }
                audio = Audio::Ptr::create(converted);
        }

        // Update bytes per sample frame if not yet known
        if(_bytesPerSampleFrame == 0) {
                _bytesPerSampleFrame = audio->desc().bytesPerSampleStride();
                _packetBytes = _samplesPerPacket * _bytesPerSampleFrame;
                // Reallocate accumulation buffer
                _accumBuffer = Buffer::Ptr::create(_packetBytes * 4);
                _accumOffset = 0;
        }

        // Append audio data to accumulation buffer
        size_t audioBytes = audio->samples() * _bytesPerSampleFrame;
        const uint8_t *audioData = audio->data<uint8_t>();

        // Grow accumulation buffer if needed
        size_t needed = _accumOffset + audioBytes;
        if(needed > _accumBuffer->size()) {
                size_t newSize = needed * 2;
                Buffer::Ptr newBuf = Buffer::Ptr::create(newSize);
                std::memcpy(newBuf->data(), _accumBuffer->data(), _accumOffset);
                _accumBuffer = newBuf;
        }

        uint8_t *accumData = static_cast<uint8_t *>(_accumBuffer->data());
        std::memcpy(accumData + _accumOffset, audioData, audioBytes);
        _accumOffset += audioBytes;

        // Send complete packets
        sendAccumulatedPackets();

        // On EOS, flush any remaining partial packet
        if(frame->metadata().contains(Metadata::EndOfStream)) {
                flushRemaining();
        }
        return;
}

void RtpAudioSinkNode::sendAccumulatedPackets() {
        uint8_t *accumData = static_cast<uint8_t *>(_accumBuffer->data());
        while(_accumOffset >= _packetBytes) {
                RtpPacket::List packets = _payload->pack(accumData, _packetBytes);
                _session->sendPackets(packets, _rtpTimestamp, _destination, false);

                _packetsSent += packets.size();
                _samplesSent += _samplesPerPacket;
                _rtpTimestamp += (uint32_t)_samplesPerPacket;

                // Shift remaining data
                size_t remaining = _accumOffset - _packetBytes;
                if(remaining > 0) {
                        std::memmove(accumData, accumData + _packetBytes, remaining);
                }
                _accumOffset = remaining;
        }
        return;
}

void RtpAudioSinkNode::flushRemaining() {
        if(_accumOffset > 0 && _payload != nullptr && _session != nullptr && _session->isRunning()) {
                RtpPacket::List packets = _payload->pack(_accumBuffer->data(), _accumOffset);
                _session->sendPackets(packets, _rtpTimestamp, _destination, true);

                size_t samplesInPacket = _accumOffset / (_bytesPerSampleFrame > 0 ? _bytesPerSampleFrame : 1);
                _packetsSent += packets.size();
                _samplesSent += samplesInPacket;
                _rtpTimestamp += (uint32_t)samplesInPacket;
                _accumOffset = 0;
        }
        return;
}

void RtpAudioSinkNode::starvation() {
        _underrunCount++;
        emitWarning("audio underrun");
        return;
}

Map<String, Variant> RtpAudioSinkNode::extendedStats() const {
        Map<String, Variant> ret;
        ret.insert("packetsSent", Variant((uint64_t)_packetsSent));
        ret.insert("samplesSent", Variant((uint64_t)_samplesSent));
        ret.insert("underrunCount", Variant((uint64_t)_underrunCount));
        return ret;
}

PROMEKI_NAMESPACE_END
