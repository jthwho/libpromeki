/**
 * @file      proav/rtpaudiosinknode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/core/buffer.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/network/socketaddress.h>
#include <promeki/network/rtpsession.h>
#include <promeki/network/rtppayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Terminal sink node that sends audio samples over RTP.
 * @ingroup proav_pipeline
 *
 * RtpAudioSinkNode is a terminal MediaNode with one Audio input
 * and no outputs. It accumulates incoming audio samples and emits
 * RTP packets at the configured packet time interval.
 *
 * Unlike RtpVideoSinkNode, this node does not perform its own
 * pacing. Audio packets are sent as soon as enough samples
 * accumulate. In a typical vidgen pipeline the video sink is the
 * timing authority, and audio flows at the rate frames arrive.
 *
 * @par Config options
 * - `Destination` (String): Destination "IP:port" (required).
 * - `PayloadType` (uint8_t): RTP payload type (default: 97).
 * - `ClockRate` (uint32_t): RTP clock rate in Hz (default: 48000).
 * - `PacketTime` (double): Packet time in ms (default: 4.0).
 * - `OutputFormat` (String): Output sample format name (optional).
 * - `Dscp` (uint8_t): DSCP value for QoS (default: 46, EF).
 * - `RtpPayload` (pointer, set programmatically): RTP payload handler.
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("RtpAudioSinkNode", "audioSink");
 * cfg.set("Destination", "239.0.0.1:5006");
 * cfg.set("PacketTime", 4.0);
 * @endcode
 */
class RtpAudioSinkNode : public MediaNode {
        PROMEKI_OBJECT(RtpAudioSinkNode, MediaNode)
        public:
                /**
                 * @brief Constructs an RtpAudioSinkNode.
                 * @param parent Optional parent object.
                 */
                RtpAudioSinkNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~RtpAudioSinkNode() override;

                MediaNodeConfig defaultConfig() const override;
                BuildResult build(const MediaNodeConfig &config) override;

                /**
                 * @brief Returns audio sink statistics.
                 * @return A map containing PacketsSent, SamplesSent, and UnderrunCount.
                 */
                Map<String, Variant> extendedStats() const override;

        protected:
                Error start() override;
                void stop() override;
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;

        private:
                void sendAccumulatedPackets();
                void flushRemaining();

                uint8_t         _payloadType = 97;
                uint32_t        _clockRate = 48000;
                double          _packetTime = 4.0;
                uint8_t         _dscp = 46;
                AudioDesc::DataType _outputFormat = AudioDesc::Invalid;
                uint32_t        _rtpTimestamp = 0;
                size_t          _samplesPerPacket = 0;
                size_t          _bytesPerSampleFrame = 0;
                size_t          _packetBytes = 0;
                uint64_t        _packetsSent = 0;
                uint64_t        _samplesSent = 0;
                uint64_t        _underrunCount = 0;

                Buffer::Ptr     _accumBuffer;
                size_t          _accumOffset = 0;

                SocketAddress   _destination;
                RtpPayload      *_payload = nullptr;
                RtpSession      *_session = nullptr;
};

PROMEKI_NAMESPACE_END
