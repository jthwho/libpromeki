/**
 * @file      proav/rtpvideosinknode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <promeki/core/string.h>
#include <promeki/core/namespace.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/core/framerate.h>
#include <promeki/proav/medianode.h>

#ifdef PROMEKI_HAVE_NETWORK
#include <promeki/network/socketaddress.h>
#include <promeki/network/rtpsession.h>
#include <promeki/network/rtppayload.h>
#endif

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
 * @par Example
 * @code
 * RtpPayloadJpeg payload(1920, 1080);
 * RtpVideoSinkNode *sink = new RtpVideoSinkNode();
 * sink->setDestination(SocketAddress(Ipv4Address("239.0.0.1"), 5004));
 * sink->setFrameRate(FrameRate::FPS_30);
 * sink->setRtpPayload(&payload);
 * sink->configure();
 * sink->start();
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

#ifdef PROMEKI_HAVE_NETWORK
                /**
                 * @brief Sets the destination address for RTP packets.
                 * @param addr Unicast or multicast destination address and port.
                 */
                void setDestination(const SocketAddress &addr) { _destination = addr; return; }

                /** @brief Returns the destination address. */
                const SocketAddress &destination() const { return _destination; }

                /**
                 * @brief Sets a multicast group to join on start.
                 * @param group The multicast group address and port.
                 */
                void setMulticast(const SocketAddress &group) { _multicast = group; return; }

                /** @brief Returns the multicast group, or a null address if not set. */
                const SocketAddress &multicast() const { return _multicast; }
#endif

                /**
                 * @brief Sets the video frame rate for pacing.
                 * @param fps The target frame rate.
                 */
                void setFrameRate(const FrameRate &fps) { _frameRate = fps; return; }

                /** @brief Returns the configured frame rate. */
                const FrameRate &frameRate() const { return _frameRate; }

#ifdef PROMEKI_HAVE_NETWORK
                /**
                 * @brief Sets the RTP payload handler.
                 *
                 * The payload handler is not owned by this node. The caller
                 * must ensure it outlives the node.
                 *
                 * @param handler The RTP payload handler.
                 */
                void setRtpPayload(RtpPayload *handler) { _payload = handler; return; }

                /** @brief Returns the RTP payload handler. */
                RtpPayload *rtpPayload() const { return _payload; }
#endif

                /**
                 * @brief Sets the RTP payload type number.
                 * @param pt Payload type (default: 96).
                 */
                void setPayloadType(uint8_t pt) { _payloadType = pt; return; }

                /** @brief Returns the RTP payload type. */
                uint8_t payloadType() const { return _payloadType; }

                /**
                 * @brief Sets the RTP timestamp clock rate.
                 * @param hz Clock rate in Hz (default: 90000).
                 */
                void setClockRate(uint32_t hz) { _clockRate = hz; return; }

                /** @brief Returns the RTP clock rate. */
                uint32_t clockRate() const { return _clockRate; }

                /**
                 * @brief Sets a file path to dump the first frame's raw data.
                 *
                 * When set, the first frame processed will be written to this
                 * path as-is (e.g. a complete JPEG file for MJPEG transport).
                 * Useful for diagnosing encoding or packing issues.
                 *
                 * @param path Output file path, or empty to disable.
                 */
                void setDumpPath(const String &path) { _dumpPath = path; return; }

                /** @brief Returns the dump path. */
                const String &dumpPath() const { return _dumpPath; }

                /**
                 * @brief Sets the DSCP value for QoS marking.
                 * @param dscp DSCP value (default: 34, AF41 for broadcast video).
                 */
                void setDscp(uint8_t dscp) { _dscp = dscp; return; }

                /** @brief Returns the DSCP value. */
                uint8_t dscp() const { return _dscp; }

                // ---- Lifecycle overrides ----

                /**
                 * @brief Validates configuration and creates the RTP session.
                 *
                 * Checks that an RTP payload handler, destination address, and
                 * frame rate are set. Computes the RTP timestamp increment and
                 * frame interval for pacing.
                 *
                 * @return Error::Ok on success, or Error::Invalid.
                 */
                Error configure() override;

                /**
                 * @brief Starts the RTP session and optionally joins a multicast group.
                 * @return Error::Ok on success, or an error if the session cannot start.
                 */
                Error start() override;

                /**
                 * @brief Stops the RTP session.
                 */
                void stop() override;

                /**
                 * @brief Sends a video frame over RTP with real-time pacing.
                 *
                 * Dequeues a Frame from the input port, packs the image data
                 * into RTP packets via the payload handler, sleeps to maintain
                 * the target frame rate, and sends the packets.
                 */
                void process() override;

                /**
                 * @brief Records a video underrun event.
                 */
                void starvation() override;

                /**
                 * @brief Returns video sink statistics.
                 * @return A map containing packetsSent, bytesSent, and underrunCount.
                 */
                Map<String, Variant> extendedStats() const override;

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

                std::chrono::nanoseconds                        _frameInterval{0};
                std::chrono::steady_clock::time_point           _nextFrameTime;
                bool                                            _firstFrame = true;
                String                                          _dumpPath;

#ifdef PROMEKI_HAVE_NETWORK
                SocketAddress   _destination;
                SocketAddress   _multicast;
                RtpPayload      *_payload = nullptr;
                RtpSession      *_session = nullptr;
#endif
};

PROMEKI_NAMESPACE_END
