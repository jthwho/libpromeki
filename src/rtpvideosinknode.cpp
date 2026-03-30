/**
 * @file      rtpvideosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <promeki/proav/rtpvideosinknode.h>
#include <promeki/proav/medianodeconfig.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/network/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(RtpVideoSinkNode)

RtpVideoSinkNode::RtpVideoSinkNode(ObjectBase *parent) : MediaNode(parent) {
        setName("RtpVideoSinkNode");
        addSink(MediaSink::Ptr::create("input", ContentVideo));
}

RtpVideoSinkNode::~RtpVideoSinkNode() {
        delete _session;
}

bool RtpVideoSinkNode::parseFrameRate(const String &str, FrameRate &out) {
        if(str == "23.976" || str == "23.98") { out = FrameRate(FrameRate::FPS_2398); return true; }
        if(str == "24")                       { out = FrameRate(FrameRate::FPS_24);   return true; }
        if(str == "25")                       { out = FrameRate(FrameRate::FPS_25);   return true; }
        if(str == "29.97")                    { out = FrameRate(FrameRate::FPS_2997); return true; }
        if(str == "30")                       { out = FrameRate(FrameRate::FPS_30);   return true; }
        if(str == "50")                       { out = FrameRate(FrameRate::FPS_50);   return true; }
        if(str == "59.94")                    { out = FrameRate(FrameRate::FPS_5994); return true; }
        if(str == "60")                       { out = FrameRate(FrameRate::FPS_60);   return true; }

        const char *slash = strchr(str.cstr(), '/');
        if(slash) {
                unsigned int num = static_cast<unsigned int>(atoi(str.cstr()));
                unsigned int den = static_cast<unsigned int>(atoi(slash + 1));
                if(num > 0 && den > 0) {
                        out = FrameRate(FrameRate::RationalType(num, den));
                        return true;
                }
        }
        return false;
}

MediaNodeConfig RtpVideoSinkNode::defaultConfig() const {
        MediaNodeConfig cfg("RtpVideoSinkNode", "");
        cfg.set("PayloadType", uint8_t(96));
        cfg.set("ClockRate", uint32_t(90000));
        cfg.set("Dscp", uint8_t(34));
        return cfg;
}

BuildResult RtpVideoSinkNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Read config
        _payloadType = config.get("PayloadType", uint8_t(96)).get<uint8_t>();
        _clockRate = config.get("ClockRate", uint32_t(90000)).get<uint32_t>();
        _dscp = config.get("Dscp", uint8_t(34)).get<uint8_t>();
        _dumpPath = config.get("DumpPath", String()).get<String>();

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

        // Parse multicast
        String mcastStr = config.get("Multicast", String()).get<String>();
        if(!mcastStr.isEmpty()) {
                auto [addr, err] = SocketAddress::fromString(mcastStr);
                if(err.isError()) {
                        result.addError("Invalid multicast address: " + mcastStr);
                        return result;
                }
                _multicast = addr;
        }

        // Parse frame rate
        String fpsStr = config.get("FrameRate", String()).get<String>();
        if(!fpsStr.isEmpty()) {
                if(!parseFrameRate(fpsStr, _frameRate)) {
                        result.addError("Invalid frame rate: " + fpsStr);
                        return result;
                }
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

        if(!_frameRate.isValid()) {
                result.addError("No frame rate set");
                return result;
        }

        // Compute RTP timestamp increment per frame:
        // increment = clockRate * denominator / numerator
        _timestampIncrement = (uint32_t)((uint64_t)_clockRate * _frameRate.denominator() / _frameRate.numerator());

        // Compute frame interval in nanoseconds
        int64_t intervalNs = (int64_t)((double)_frameRate.denominator() / (double)_frameRate.numerator() * 1e9);
        _frameInterval = Duration::fromNanoseconds(intervalNs);

        delete _session;
        _session = new RtpSession(this);
        _session->setPayloadType(_payloadType);
        _session->setClockRate(_clockRate);

        _rtpTimestamp = 0;
        _packetsSent = 0;
        _bytesSent = 0;
        _underrunCount = 0;
        _firstFrame = true;

        setState(Configured);
        return result;
}

Error RtpVideoSinkNode::start() {
        if(state() != Configured) return Error(Error::Invalid);

        Error err = _session->start(SocketAddress::any(0));
        if(err.isError()) {
                emitError("Failed to start RTP session");
                return err;
        }

        _session->socket()->setDscp(_dscp);

        if(!_multicast.isNull()) {
                _session->socket()->joinMulticastGroup(_multicast);
        }

        return MediaNode::start();
}

void RtpVideoSinkNode::stop() {
        MediaNode::stop();
        if(_session != nullptr) {
                _session->stop();
        }
        _firstFrame = true;
        return;
}

void RtpVideoSinkNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;
        (void)deliveries;

        if(!frame.isValid()) return;

        if(frame->imageList().isEmpty()) return;

        Image::Ptr img = frame->imageList()[0];

        // Determine data pointer and size
        const void *dataPtr;
        size_t dataSize;
        if(img->isCompressed()) {
                dataPtr = img->data();
                dataSize = img->compressedSize();
        } else {
                dataPtr = img->data();
                dataSize = img->lineStride() * img->height();
        }

        // One-shot frame dump for diagnostics
        if(!_dumpPath.isEmpty()) {
                FILE *f = fopen(_dumpPath.cstr(), "wb");
                if(f) {
                        fwrite(dataPtr, 1, dataSize, f);
                        fclose(f);
                        emitMessage(Severity::Info,
                                    String("Dumped frame to ") + _dumpPath +
                                    " (" + String::number((uint64_t)dataSize) + " bytes)");
                } else {
                        emitWarning(String("Failed to write dump file: ") + _dumpPath);
                }
                _dumpPath = String();
        }

        // Pack into RTP packets
        RtpPacket::List packets = _payload->pack(dataPtr, dataSize);

        // Pacing: first frame sets the clock, subsequent frames sleep
        if(_firstFrame) {
                _nextFrameTime = TimeStamp::now();
                _firstFrame = false;
        } else {
                _nextFrameTime += TimeStamp::secondsToDuration(
                        _frameInterval.toSecondsDouble());
                _nextFrameTime.sleepUntil();
        }

        // For large packet counts (uncompressed video), spread packets
        // across 90% of the frame interval to avoid bursting hundreds of
        // packets at once (ST 2110-21 style pacing).  For small counts
        // (compressed formats like MJPEG), send immediately — pacing a
        // handful of packets over tens of milliseconds would cause the
        // receiver to time out waiting for fragments.
        static constexpr size_t PacingThreshold = 32;
        if(packets.size() >= PacingThreshold) {
                Duration spreadInterval = _frameInterval * 9 / 10;
                _session->sendPacketsPaced(packets, _rtpTimestamp, _destination,
                                           spreadInterval, true);
        } else {
                _session->sendPackets(packets, _rtpTimestamp, _destination, true);
        }

        // Update stats
        _packetsSent += packets.size();
        for(size_t i = 0; i < packets.size(); i++) {
                _bytesSent += packets[i].size();
        }

        // Advance RTP timestamp
        _rtpTimestamp += _timestampIncrement;
        return;
}

Map<String, Variant> RtpVideoSinkNode::extendedStats() const {
        Map<String, Variant> ret;
        ret.insert("PacketsSent", Variant((uint64_t)_packetsSent));
        ret.insert("BytesSent", Variant((uint64_t)_bytesSent));
        ret.insert("UnderrunCount", Variant((uint64_t)_underrunCount));
        return ret;
}

PROMEKI_NAMESPACE_END
