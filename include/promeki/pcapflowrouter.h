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
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/list.h>
#include <promeki/packetdemux.h>
#include <promeki/pcapsdpmap.h>
#include <promeki/rtppacket.h>
#include <promeki/rtpseqtracker.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>

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
                                // -- RFC 3550 health (via RtpSeqTracker), cumulative over this SSRC --
                                uint64_t lostPackets = 0;       ///< Sequence-number gaps (§6.4.1 cumulative-lost, clamped ≥0).
                                uint64_t duplicatePackets = 0;  ///< Sequence numbers received more than once.
                                uint64_t reorderedPackets = 0;  ///< Late arrivals inside the §A.1 reorder window.
                                uint32_t timestampRegressions = 0; ///< Times the RTP timestamp jumped backward.
                                Duration maxJitter = Duration::zero(); ///< Peak interarrival jitter (§A.8); zero if the flow's clock is unknown.
                };

                /**
                 * @brief A discrete RTP-level anomaly observed on a flow.
                 *
                 * Delivered through @ref onRtpAnomaly as packets are
                 * processed, in capture order, so a consumer can interleave
                 * the warning with the surrounding decoded frames.  Cumulative
                 * quality counts (loss / duplicate / reorder / jitter) also
                 * land on the matching @ref FlowStat; these events are the
                 * point-in-time signals that a counter alone can't place.
                 */
                struct RtpAnomaly {
                                /// @brief The class of anomaly.
                                enum class Kind {
                                        SsrcChange,          ///< A different SSRC appeared on this destination.
                                        PayloadTypeChange,   ///< The RTP payload type changed on this SSRC.
                                        PacketLoss,          ///< A forward sequence gap (one or more packets missing).
                                        Reorder,             ///< A packet arrived behind the sequence cursor.
                                        Duplicate,           ///< A sequence number was seen again.
                                        TimestampRegression, ///< The RTP timestamp moved backward.
                                        JitterExceeded,      ///< Interarrival jitter rose above the configured threshold.
                                };
                                Kind          kind = Kind::SsrcChange; ///< Which class of anomaly was observed.
                                PcapFlowKind  flowKind = PcapFlowKind::Unknown; ///< SDP label of the dst flow, if any.
                                SocketAddress dst;             ///< Destination the anomaly was seen on.
                                uint32_t      ssrc = 0;        ///< Current SSRC.
                                /// @brief Prior value where one applies: the old SSRC
                                ///        (@c SsrcChange), the old payload type
                                ///        (@c PayloadTypeChange), or the prior RTP
                                ///        timestamp (@c TimestampRegression).
                                uint32_t      previous = 0;
                                uint32_t      count = 0;       ///< Packets missing (@c PacketLoss); else 0.
                                Duration      jitter = Duration::zero(); ///< Measured interarrival jitter (@c JitterExceeded).
                                DateTime      captureTime;     ///< Capture time of the triggering packet.
                };

                /// @brief Callback invoked for each observed RTP anomaly.
                using RtpAnomalyCallback = Function<void(const RtpAnomaly &)>;

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

                /** @brief Set the RTP-anomaly callback (loss, SSRC change, etc.). */
                void onRtpAnomaly(RtpAnomalyCallback cb) { _anomalyCb = std::move(cb); }

                /**
                 * @brief Sets the interarrival-jitter warning threshold.
                 *
                 * When a flow's RFC 3550 §A.8 jitter rises above @p threshold,
                 * a @ref RtpAnomaly::Kind::JitterExceeded event is emitted
                 * (once per excursion; it re-arms when the jitter falls back
                 * below the threshold).  A zero or invalid Duration (the
                 * default) disables jitter warnings.  Jitter is measured
                 * against the 90 kHz media clock and is only computed for
                 * flows whose clock rate is known (ANC / video).
                 */
                void setJitterWarnThreshold(const Duration &threshold) { _jitterWarn = threshold; }

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

                /// @brief Per-destination RTP health state.  Held behind a
                ///        UniquePtr so the embedded @ref RtpSeqTracker (which
                ///        owns a Mutex and so is non-movable) keeps a stable
                ///        address as the list grows.
                struct FlowHealth {
                                SocketAddress dst;
                                uint32_t      ssrc = 0;
                                bool          haveSsrc = false;
                                uint8_t       payloadType = 0;
                                bool          havePt = false;
                                uint32_t      lastTimestamp = 0;
                                bool          haveTs = false;
                                uint32_t      lastExtendedSeq = 0;
                                bool          haveSeq = false;
                                // §A.8 jitter state (computed directly in ns; see trackRtpHealth).
                                int64_t       prevArrivalNs = 0;
                                uint32_t      prevRtpTs = 0;
                                int64_t       jitterNs = 0;
                                bool          haveJitterPrev = false;
                                Duration      maxJitter = Duration::zero();
                                bool          jitterOver = false; ///< Hysteresis: currently above the warn threshold.
                                uint32_t      timestampRegressions = 0;
                                RtpSeqTracker tracker;
                };

                void handleDatagram(const UdpDatagram &dg, const DateTime &captureTime);
                void routeAnc(const UdpDatagram &dg, const PcapFlow &flow, const RtpPacket &pkt, const DateTime &captureTime);
                void flushAnc(AncReasm &r);
                FlowStat &statFor(const SocketAddress &dst, uint32_t ssrc, uint8_t pt, PcapFlowKind kind);
                AncReasm &ancReasmFor(const UdpDatagram &dg, const PcapFlow &flow);
                FlowHealth &healthFor(const SocketAddress &dst);
                void trackRtpHealth(const SocketAddress &dst, PcapFlowKind kind, const RtpPacket &pkt, FlowStat &st,
                                    const DateTime &captureTime);
                void emitAnomaly(RtpAnomaly::Kind kind, PcapFlowKind flowKind, const SocketAddress &dst, uint32_t ssrc,
                                 uint32_t previous, uint32_t count, const DateTime &captureTime,
                                 const Duration &jitter = Duration::zero());

                Error runReader(class PcapReader &reader);

                PcapSdpMap _map;
                PacketDemux _demux;
                AncFrameCallback _ancCb;
                RtpAnomalyCallback _anomalyCb;
                Duration _jitterWarn = Duration::zero(); ///< Jitter warn threshold; zero/invalid = disabled.
                List<FlowStat> _stats;
                List<AncReasm> _ancFlows;
                List<UniquePtr<FlowHealth>> _health;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
