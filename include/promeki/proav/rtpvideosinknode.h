/**
 * @file      proav/rtpvideosinknode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/string.h>
#include <promeki/core/duration.h>
#include <promeki/core/timestamp.h>
#include <promeki/core/namespace.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/core/framerate.h>
#include <promeki/proav/medianode.h>
#include <promeki/network/socketaddress.h>
#include <promeki/network/rtpsession.h>
#include <promeki/network/rtppayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Terminal sink node that sends video frames over RTP.
 * @ingroup proav_pipeline
 *
 * RtpVideoSinkNode is a terminal MediaNode with one Image input
 * and no outputs. It paces video frame transmission at the
 * configured frame rate and sends RTP packets via the provided
 * RtpPayload handler and RtpSession.
 *
 * The node acts as the real-time pacing authority in a pipeline —
 * it sleeps between frames to maintain the target frame rate,
 * preventing drift from processing time.
 *
 * @par Config options
 * - `Destination` (String): Destination "IP:port" (required).
 * - `Multicast` (String): Multicast group "IP:port" (optional).
 * - `FrameRate` (String): Frame rate (required). E.g. "29.97", "30000/1001".
 * - `PayloadType` (uint8_t): RTP payload type (default: 96).
 * - `ClockRate` (uint32_t): RTP clock rate in Hz (default: 90000).
 * - `Dscp` (uint8_t): DSCP value for QoS (default: 34, AF41).
 * - `DumpPath` (String): File path to dump first frame's raw data.
 * - `RtpPayload` (pointer, set programmatically): RTP payload handler.
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("RtpVideoSinkNode", "videoSink");
 * cfg.set("Destination", "239.0.0.1:5004");
 * cfg.set("FrameRate", "29.97");
 * @endcode
 */
class RtpVideoSinkNode : public MediaNode {
        PROMEKI_OBJECT(RtpVideoSinkNode, MediaNode)
        public:
                /**
                 * @brief Constructs an RtpVideoSinkNode.
                 * @param parent Optional parent object.
                 */
                RtpVideoSinkNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~RtpVideoSinkNode() override;

                MediaNodeConfig defaultConfig() const override;
                BuildResult build(const MediaNodeConfig &config) override;

                /**
                 * @brief Returns video sink statistics.
                 * @return A map containing PacketsSent, BytesSent, and UnderrunCount.
                 */
                Map<String, Variant> extendedStats() const override;

        protected:
                Error start() override;
                void stop() override;
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;

        private:
                FrameRate       _frameRate;
                uint8_t         _payloadType = 96;
                uint32_t        _clockRate = 90000;
                uint8_t         _dscp = 34;
                uint32_t        _rtpTimestamp = 0;
                uint32_t        _timestampIncrement = 0;
                uint64_t        _packetsSent = 0;
                uint64_t        _bytesSent = 0;
                uint64_t        _underrunCount = 0;

                Duration                                        _frameInterval;
                TimeStamp                                       _nextFrameTime;
                bool                                            _firstFrame = true;
                String                                          _dumpPath;

                SocketAddress   _destination;
                SocketAddress   _multicast;
                RtpPayload      *_payload = nullptr;
                RtpSession      *_session = nullptr;

};

PROMEKI_NAMESPACE_END
