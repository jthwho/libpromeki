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
 * @par Example
 * @code
 * RtpPayloadL24 payload(48000, 2);
 * RtpAudioSinkNode *sink = new RtpAudioSinkNode();
 * sink->setDestination(SocketAddress(Ipv4Address("239.0.0.1"), 5006));
 * sink->setRtpPayload(&payload);
 * sink->setPacketTime(4.0); // 4ms packet time
 * sink->configure();
 * sink->start();
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

                /**
                 * @brief Sets the destination address for RTP packets.
                 * @param addr Unicast or multicast destination address and port.
                 */
                void setDestination(const SocketAddress &addr) { _destination = addr; return; }

                /** @brief Returns the destination address. */
                const SocketAddress &destination() const { return _destination; }

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

                /**
                 * @brief Sets the RTP payload type number.
                 * @param pt Payload type (default: 97).
                 */
                void setPayloadType(uint8_t pt) { _payloadType = pt; return; }

                /** @brief Returns the RTP payload type. */
                uint8_t payloadType() const { return _payloadType; }

                /**
                 * @brief Sets the RTP timestamp clock rate.
                 * @param hz Clock rate in Hz (default: 48000).
                 */
                void setClockRate(uint32_t hz) { _clockRate = hz; return; }

                /** @brief Returns the RTP clock rate. */
                uint32_t clockRate() const { return _clockRate; }

                /**
                 * @brief Sets the packet time in milliseconds.
                 * @param ptime Packet time (default: 4.0ms). Use 1.0ms for AES67.
                 */
                void setPacketTime(double ptime) { _packetTime = ptime; return; }

                /** @brief Returns the packet time in milliseconds. */
                double packetTime() const { return _packetTime; }

                /**
                 * @brief Sets the output sample format for RTP transmission.
                 *
                 * When set to a format other than AudioDesc::Invalid, incoming audio
                 * is automatically converted to this format before packing into RTP
                 * packets. For AES67/L24, use AudioDesc::PCMI_S24BE.
                 *
                 * @param fmt The target AudioDesc::DataType.
                 */
                void setOutputFormat(AudioDesc::DataType fmt) { _outputFormat = fmt; return; }

                /** @brief Returns the output sample format. */
                AudioDesc::DataType outputFormat() const { return _outputFormat; }

                /**
                 * @brief Sets the DSCP value for QoS marking.
                 * @param dscp DSCP value (default: 46, EF for real-time audio).
                 */
                void setDscp(uint8_t dscp) { _dscp = dscp; return; }

                /** @brief Returns the DSCP value. */
                uint8_t dscp() const { return _dscp; }

                // ---- Lifecycle overrides ----

                /**
                 * @brief Validates configuration and creates the RTP session.
                 *
                 * Checks that an RTP payload handler and destination address are
                 * set, computes samples-per-packet from the packet time and clock
                 * rate, and allocates the accumulation buffer.
                 *
                 * @return Error::Ok on success, or Error::Invalid.
                 */
                Error configure() override;

                /**
                 * @brief Starts the RTP session and begins accepting audio.
                 * @return Error::Ok on success, or an error if the session cannot start.
                 */
                Error start() override;

                /**
                 * @brief Flushes remaining audio and stops the RTP session.
                 */
                void stop() override;

                /**
                 * @brief Accumulates audio samples and sends complete RTP packets.
                 *
                 * Dequeues a Frame from the input port, optionally converts the
                 * audio to the configured output format, appends samples to the
                 * accumulation buffer, and sends packets when enough samples
                 * have accumulated.
                 */
                void process() override;

                /**
                 * @brief Records an audio underrun event.
                 */
                void starvation() override;

                /**
                 * @brief Returns audio sink statistics.
                 * @return A map containing packetsSent, samplesSent, and underrunCount.
                 */
                Map<String, Variant> extendedStats() const override;

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
