/**
 * @file      pcapsdpmap.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/ancdesc.h>
#include <promeki/enums_pcap.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/networkaddress.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class SdpSession;
class SdpMediaDescription;

/**
 * @brief One RTP flow described by an SDP session.
 * @ingroup network
 *
 * A flow is identified on the wire by its destination
 * @c (address, port).  The remaining fields are the SDP labelling:
 * what kind of essence it is, its expected RTP payload type, and — for
 * ANC flows — the @ref AncDesc resolved from the media description's
 * @c fmtp DID/SDID list and paired-video context.
 */
struct PcapFlow {
                /// @brief Destination (typically multicast) address.
                NetworkAddress address;
                /// @brief Destination UDP port.
                uint16_t port = 0;
                /// @brief Essence kind classified from the SDP media line.
                PcapFlowKind kind = PcapFlowKind::Unknown;
                /// @brief Expected RTP payload type (valid only if @ref hasPayloadType).
                uint8_t payloadType = 0;
                /// @brief @c true when @ref payloadType was given by the SDP.
                bool hasPayloadType = false;
                /// @brief @c rtpmap encoding name (e.g. @c "smpte291", @c "raw", @c "L24").
                String encoding;
                /// @brief @c rtpmap clock rate in Hz (0 if unknown).
                uint32_t clockRate = 0;
                /// @brief Per-stream ANC descriptor — populated only when @ref kind is @c Anc.
                AncDesc anc;
                /// @brief Human label (the SDP @c m= media type).
                String label;

                /// @brief Convenience: the flow's destination as a SocketAddress.
                SocketAddress destination() const { return SocketAddress(address, port); }
};

/**
 * @brief Destination-keyed routing table built from an SDP session.
 * @ingroup network
 *
 * @c PcapSdpMap turns an @ref SdpSession into a lookup from a flow's
 * destination @c (address, port) onto a @ref PcapFlow.  This is the
 * "SDP labelling" layer: a multi-essence ST 2110 capture is sorted into
 * its video / audio / ANC flows by matching each datagram's destination
 * against the table, with the SDP-declared payload type available for
 * cross-checking.
 *
 * @par Address resolution
 * Each media line's destination address is the media-level @c c= line
 * when present, otherwise the session-level @c c=.  A multicast TTL /
 * address-count suffix (@c "239.1.2.3/64") is stripped.  Both IPv4 and
 * IPv6 connection addresses are parsed; a media line whose address does
 * not parse as a numeric IP is skipped (it cannot be matched on the
 * wire).
 *
 * @par Classification
 * @c rtpmap @c smpte291 → @ref PcapFlowKind::Anc (regardless of the
 * @c m= type, which is @c video for ANC); otherwise the @c m= media
 * type maps @c audio → @ref PcapFlowKind::Audio, @c video →
 * @ref PcapFlowKind::Video, @c application → @ref PcapFlowKind::Data.
 */
class PcapSdpMap {
        public:
                PcapSdpMap() = default;

                /**
                 * @brief Replace the table with the flows described by @p sdp.
                 * @param sdp The parsed SDP session.
                 * @return @c Error::Ok (always; unparseable media lines are
                 *         skipped rather than failing the whole ingest).
                 *
                 * @note Replaces the table; any flows added via @ref addFlow
                 *       / @ref addAncFlow beforehand are discarded.  Add
                 *       manual flows *after* @c ingest to combine the two.
                 */
                Error ingest(const SdpSession &sdp);

                /**
                 * @brief Append a flow to the table (does not clear).
                 *
                 * Useful for capture sets that have no SDP, or to augment an
                 * ingested SDP with a flow it omitted.
                 */
                void addFlow(const PcapFlow &flow) { _flows.pushToBack(flow); }

                /**
                 * @brief Append a manually-designated ANC flow.
                 *
                 * The escape hatch for decoding an ST 2110-40 stream when no
                 * SDP is available: name the destination and the flow is
                 * routed as @ref PcapFlowKind::Anc.
                 *
                 * @param address      Destination (multicast) address.
                 * @param port         Destination UDP port.
                 * @param payloadType  Expected RTP payload type, or a
                 *                     negative value to accept any PT on this
                 *                     destination (the usual choice when the
                 *                     PT is unknown).
                 */
                void addAncFlow(const NetworkAddress &address, uint16_t port, int payloadType = -1);

                /** @brief Drop all flows. */
                void clear() { _flows.clear(); }

                /** @brief @c true when no flows have been ingested. */
                bool isEmpty() const { return _flows.isEmpty(); }

                /** @brief The full flow list (in SDP media-line order). */
                const List<PcapFlow> &flows() const { return _flows; }

                /**
                 * @brief Look up the flow for a destination address + port.
                 * @return The matching flow, or @c nullptr if none.
                 */
                const PcapFlow *find(const NetworkAddress &address, uint16_t port) const;

                /** @copydoc find(const NetworkAddress&, uint16_t) const */
                const PcapFlow *find(const SocketAddress &dst) const { return find(dst.address(), dst.port()); }

                /**
                 * @brief Classify an SDP media description into an essence kind.
                 *
                 * Exposed as a static helper so the auto-discovery path can
                 * reuse the same rule.
                 */
                static PcapFlowKind classify(const SdpMediaDescription &md);

        private:
                List<PcapFlow> _flows;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
