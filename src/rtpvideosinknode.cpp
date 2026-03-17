/**
 * @file      rtpvideosinknode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/proav/rtpvideosinknode.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#ifdef PROMEKI_HAVE_NETWORK
#include <promeki/network/udpsocket.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(RtpVideoSinkNode)

RtpVideoSinkNode::RtpVideoSinkNode(ObjectBase *parent) : MediaNode(parent) {
        setName("RtpVideoSinkNode");
        auto input = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Image);
        addInputPort(input);
}

RtpVideoSinkNode::~RtpVideoSinkNode() {
#ifdef PROMEKI_HAVE_NETWORK
        delete _session;
#endif
}

Error RtpVideoSinkNode::configure() {
        if(state() != Idle) return Error(Error::Invalid);

#ifdef PROMEKI_HAVE_NETWORK
        if(_payload == nullptr) {
                emitError("No RTP payload handler set");
                return Error(Error::Invalid);
        }
        if(_destination.isNull()) {
                emitError("No destination address set");
                return Error(Error::Invalid);
        }
#endif

        if(!_frameRate.isValid()) {
                emitError("No frame rate set");
                return Error(Error::Invalid);
        }

        // Compute RTP timestamp increment per frame:
        // increment = clockRate * denominator / numerator
        _timestampIncrement = (uint32_t)((uint64_t)_clockRate * _frameRate.denominator() / _frameRate.numerator());

        // Compute frame interval in nanoseconds
        int64_t intervalNs = (int64_t)((double)_frameRate.denominator() / (double)_frameRate.numerator() * 1e9);
        _frameInterval = Duration::fromNanoseconds(intervalNs);

#ifdef PROMEKI_HAVE_NETWORK
        delete _session;
        _session = new RtpSession(this);
        _session->setPayloadType(_payloadType);
        _session->setClockRate(_clockRate);
#endif

        _rtpTimestamp = 0;
        _packetsSent = 0;
        _bytesSent = 0;
        _underrunCount = 0;
        _firstFrame = true;

        setState(Configured);
        return Error(Error::Ok);
}

Error RtpVideoSinkNode::start() {
        if(state() != Configured) return Error(Error::Invalid);

#ifdef PROMEKI_HAVE_NETWORK
        Error err = _session->start(SocketAddress::any(0));
        if(err.isError()) {
                emitError("Failed to start RTP session");
                return err;
        }

        _session->socket()->setDscp(_dscp);

        if(!_multicast.isNull()) {
                _session->socket()->joinMulticastGroup(_multicast);
        }
#endif

        setState(Running);
        return Error(Error::Ok);
}

void RtpVideoSinkNode::stop() {
#ifdef PROMEKI_HAVE_NETWORK
        if(_session != nullptr) {
                _session->stop();
        }
#endif
        _firstFrame = true;
        setState(Idle);
        return;
}

void RtpVideoSinkNode::process() {
        Frame::Ptr frame = dequeueInput();
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

#ifdef PROMEKI_HAVE_NETWORK
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
#endif

        // Advance RTP timestamp
        _rtpTimestamp += _timestampIncrement;
        return;
}

void RtpVideoSinkNode::starvation() {
        _underrunCount++;
        emitWarning("video underrun");
        return;
}

Map<String, Variant> RtpVideoSinkNode::extendedStats() const {
        Map<String, Variant> ret;
        ret.insert("packetsSent", Variant((uint64_t)_packetsSent));
        ret.insert("bytesSent", Variant((uint64_t)_bytesSent));
        ret.insert("underrunCount", Variant((uint64_t)_underrunCount));
        return ret;
}

PROMEKI_NAMESPACE_END
