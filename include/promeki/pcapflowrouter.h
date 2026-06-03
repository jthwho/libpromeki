/**
 * @file      pcapflowrouter.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/ancpacket.h>
#include <promeki/buffer.h>
#include <promeki/datetime.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/list.h>
#include <promeki/packetdemux.h>
#include <promeki/pcapsdpmap.h>
#include <promeki/rtppacket.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class SdpSession;

/**
 * @brief Drives a capture through demux + SDP labelling and emits
 *        decoded ST 2110-40 ANC frames.
 * @ingroup network
 *
 * @c PcapFlowRouter is the top of the offline ingest path.  It reads a
 * capture with @ref PcapReader, demultiplexes each frame with
 * @ref PacketDemux, matches the resulting UDP datagrams against an
 * ingested @ref PcapSdpMap, and — for ANC flows — reassembles RTP
 * packets across the marker-bit / timestamp boundary and runs
 * @ref RtpPayloadAnc::unpackAncPackets, exactly as the live
 * @c RtpAncDepacketizerThread does.  Each completed ANC frame is
 * delivered through @ref onAncFrame.
 *
 * @par Flow discovery
 * Every RTP datagram seen — labelled or not — is tallied into
 * @ref flowStats, so a capture with no SDP can still be inspected
 * (each unique @c (dst, ssrc, payloadType) becomes a row, with packet
 * and byte counts).  SDP labelling is the primary path; the stats are
 * the auto-discovery fallback.
 *
 * @par Audio / video
 * Audio and video flows are recognised and tallied but not yet decoded
 * here — that is a later phase.  The dispatch seam exists so those
 * decoders slot in without re-plumbing the reader/demux front end.
 */
class PcapFlowRouter {
        public:
                /// @brief One completed ANC frame, with its pcap context.
                struct RoutedAncFrame {
                                /// @brief Source address + UDP source port.
                                SocketAddress src;
                                /// @brief Destination (multicast) address + port.
                                SocketAddress dst;
                                /// @brief RTP SSRC the frame was reassembled from.
                                uint32_t ssrc = 0;
                                /// @brief Wall-clock capture time of the frame-completing packet.
                                DateTime captureTime;
                                /// @brief The reassembled ANC bundle (the same value
                                ///        type the live depacketizer emits).
                                RxAncFrame anc;
                };

                /// @brief Callback invoked for each completed ANC frame.
                using AncFrameCallback = Function<void(const RoutedAncFrame &)>;

                /// @brief One row of the auto-discovered flow table.
                struct FlowStat {
                                SocketAddress dst;      ///< Destination address + port.
                                uint32_t ssrc = 0;      ///< RTP SSRC.
                                uint8_t payloadType = 0;///< RTP payload type.
                                PcapFlowKind kind = PcapFlowKind::Unknown; ///< SDP label, if any.
                                uint64_t packets = 0;   ///< RTP packets seen on this flow.
                                uint64_t bytes = 0;     ///< Total UDP payload bytes seen.
                };

                PcapFlowRouter() = default;

                /**
                 * @brief Ingest an SDP session so flows are labelled.
                 * @param sdp Parsed SDP describing the capture's flows.
                 * @return As @ref PcapSdpMap::ingest.
                 */
                Error setSdp(const SdpSession &sdp);

                /**
                 * @brief Manually designate a destination as an ANC flow.
                 *
                 * The no-SDP escape hatch: decode an ST 2110-40 stream by
                 * naming its destination.  Adds to (does not replace) any
                 * SDP-ingested flows, so it can also augment a partial SDP.
                 *
                 * @param dst         Destination address + UDP port.
                 * @param payloadType Expected RTP payload type, or negative
                 *                    to accept any PT on this destination.
                 */
                void addAncFlow(const SocketAddress &dst, int payloadType = -1) {
                        _map.addAncFlow(dst.address(), dst.port(), payloadType);
                }

                /** @brief Set the per-ANC-frame callback. */
                void onAncFrame(AncFrameCallback cb) { _ancCb = std::move(cb); }

                /**
                 * @brief Process an entire capture file.
                 * @param path Path to a @c .pcap / @c .pcapng file.
                 * @return @c Error::Ok on a clean run (a truncated tail is
                 *         tolerated), or an open / read error.
                 */
                Error processFile(const String &path);

                /**
                 * @brief Process a capture already resident in memory.
                 * @param buf A buffer holding a complete capture image.
                 */
                Error processBuffer(const Buffer &buf);

                /** @brief The auto-discovered flow table (valid after processing). */
                const List<FlowStat> &flowStats() const { return _stats; }

                /** @brief The SDP-derived flow map. */
                const PcapSdpMap &sdpMap() const { return _map; }

                /** @brief Reset discovery stats and in-flight ANC reassembly (keeps the SDP map). */
                void reset();

        private:
                /// @brief Per-ANC-flow reassembly state.
                struct AncReasm {
                                SocketAddress dst;
                                SocketAddress src;
                                uint32_t ssrc = 0;
                                bool haveSsrc = false;
                                bool haveTs = false;
                                uint32_t timestamp = 0;
                                uint8_t payloadType = 0;
                                AncDesc desc;
                                RtpPacket::List packets;
                                DateTime captureTime;
                };

                void handleDatagram(const UdpDatagram &dg, const DateTime &captureTime);
                void routeAnc(const UdpDatagram &dg, const PcapFlow &flow, const RtpPacket &pkt, const DateTime &captureTime);
                void flushAnc(AncReasm &r);
                FlowStat &statFor(const SocketAddress &dst, uint32_t ssrc, uint8_t pt, PcapFlowKind kind);
                AncReasm &ancReasmFor(const UdpDatagram &dg, const PcapFlow &flow);

                Error runReader(class PcapReader &reader);

                PcapSdpMap _map;
                PacketDemux _demux;
                AncFrameCallback _ancCb;
                List<FlowStat> _stats;
                List<AncReasm> _ancFlows;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
