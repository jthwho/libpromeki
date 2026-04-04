/**
 * @file      rtpaudiosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/rtpaudiosinknode.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>
#include <promeki/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(RtpAudioSinkNode)

RtpAudioSinkNode::RtpAudioSinkNode(ObjectBase *parent) : MediaNode(parent) {
        setName("RtpAudioSinkNode");
        addSink(MediaSink::Ptr::create("input", ContentAudio));

}

RtpAudioSinkNode::~RtpAudioSinkNode() {
        delete _session;
}

MediaNodeConfig RtpAudioSinkNode::defaultConfig() const {
        MediaNodeConfig cfg("RtpAudioSinkNode", "");
        cfg.set("PayloadType", uint8_t(97));
        cfg.set("ClockRate", uint32_t(48000));
        cfg.set("PacketTime", 4.0);
        cfg.set("Dscp", uint8_t(46));
        return cfg;
}

BuildResult RtpAudioSinkNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Read config
        _payloadType = config.get("PayloadType", uint8_t(97)).get<uint8_t>();
        _clockRate = config.get("ClockRate", uint32_t(48000)).get<uint32_t>();
        _packetTime = config.get("PacketTime", 4.0).get<double>();
        _dscp = config.get("Dscp", uint8_t(46)).get<uint8_t>();

        // Parse output format
        // TODO: parse from string name when formats are registered by name
        Variant fmtVar = config.get("OutputFormat");
        if(fmtVar.isValid()) {
                _outputFormat = static_cast<AudioDesc::DataType>(fmtVar.get<int>());
        }

        // Parse destination
        String destStr = config.get("Destination", String()).get<String>();
        if(!destStr.isEmpty()) {
                auto [addr, err] = SocketAddress::fromString(destStr);
                if(err.isError()) {
                        result.addError("Invalid destination address: " + destStr);
                        return result;
                }
                _destination = addr;
        }

        // Accept RTP payload handler passed as uint64_t (pointer cast)
        Variant payloadVar = config.get("RtpPayload");
        if(payloadVar.isValid()) {
                _payload = reinterpret_cast<RtpPayload *>(payloadVar.get<uint64_t>());
        }

        // Validate
        if(_payload == nullptr) {
                result.addError("No RTP payload handler set");
                return result;
        }
        if(_destination.isNull()) {
                result.addError("No destination address set");
                return result;
        }

        // Compute samples per packet from packet time
        _samplesPerPacket = (size_t)(_packetTime * 0.001 * _clockRate);
        if(_samplesPerPacket == 0) {
                result.addError("Invalid packet time / clock rate combination");
                return result;
        }

        // Bytes per sample frame will be determined from actual audio data
        // in process() when the first frame arrives.
        if(_outputFormat != AudioDesc::Invalid) {
                const AudioDesc::Format *fmt = AudioDesc::lookupFormat(_outputFormat);
                if(fmt == nullptr || fmt->bytesPerSample == 0) {
                        result.addError("Invalid output format");
                        return result;
                }
        }
        _bytesPerSampleFrame = 0;

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
        return result;
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

        return MediaNode::start();
}

void RtpAudioSinkNode::stop() {
        MediaNode::stop();
        flushRemaining();

        if(_session != nullptr) {
                _session->stop();
        }

        return;
}

void RtpAudioSinkNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;
        (void)deliveries;

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

Map<String, Variant> RtpAudioSinkNode::extendedStats() const {
        Map<String, Variant> ret;
        ret.insert("PacketsSent", Variant((uint64_t)_packetsSent));
        ret.insert("SamplesSent", Variant((uint64_t)_samplesSent));
        ret.insert("UnderrunCount", Variant((uint64_t)_underrunCount));
        return ret;
}

PROMEKI_NAMESPACE_END
