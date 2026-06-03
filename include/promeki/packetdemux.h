/**
 * @file      packetdemux.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/enums_pcap.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/list.h>
#include <promeki/socketaddress.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A demultiplexed UDP datagram.
 * @ingroup network
 *
 * The product of @ref PacketDemux::demux when a captured frame carries
 * a complete UDP datagram: the source/destination socket addresses and
 * a @ref BufferView of the UDP payload.
 *
 * @par Zero-copy vs reassembled
 * For an unfragmented datagram @ref payload is a zero-copy view into
 * the capture's backing buffer.  For a datagram rebuilt from IP
 * fragments (@ref reassembled is @c true) the payload is a view into a
 * fresh buffer the datagram owns — fragments are scattered in the
 * capture and cannot be aliased contiguously.  Either way the view
 * keeps its backing buffer alive.
 */
struct UdpDatagram {
                /// @brief Source address + UDP source port.
                SocketAddress src;
                /// @brief Destination address + UDP destination port.
                SocketAddress dst;
                /// @brief Zero-copy (or reassembled) view of the UDP payload.
                BufferView payload;
                /// @brief IP next-protocol number (17 == UDP).
                uint8_t ipProtocol = 17;
                /// @brief @c true if produced by IP fragment reassembly.
                bool reassembled = false;
};

/// @brief Outcome of a single @ref PacketDemux::demux call.
enum class DemuxStatus {
        Ok,          ///< @ref DemuxResult::datagram holds a complete UDP datagram.
        NotUdp,      ///< Parsed cleanly but the L4 protocol is not UDP (or not IP) — skip.
        Fragment,    ///< An IP fragment was consumed into the reassembly cache; no datagram yet.
        Truncated,   ///< A header or declared length runs past the captured bytes (snap length).
        Malformed,   ///< Structurally invalid link / IP / UDP headers.
        Unsupported, ///< Link type or address family this demux does not handle.
};

/// @brief Status + (when @c Ok) the demultiplexed datagram.
struct DemuxResult {
                DemuxStatus status = DemuxStatus::Malformed;
                UdpDatagram datagram;
};

/**
 * @brief Demultiplexes captured link-layer frames down to UDP datagrams.
 * @ingroup network
 *
 * @c PacketDemux turns a @c (PcapLinkType, frame) pair — as produced by
 * @ref PcapReader, or equally by a live raw-socket capture — into a
 * @ref UdpDatagram.  It understands:
 *
 *  - **Link layer:** Ethernet II with 0..N stacked 802.1Q / 802.1ad
 *    VLAN tags, Linux cooked v1 (SLL) and v2 (SLL2), BSD loopback
 *    (NULL / LOOP), and header-less raw IP (RAW / IPV4 / IPV6).
 *  - **Network layer:** IPv4 (with options) and IPv6 (walking the
 *    Hop-by-Hop / Routing / Destination-Options / Fragment extension
 *    headers to reach the upper layer).
 *  - **Transport:** UDP.  Non-UDP frames return @ref DemuxStatus::NotUdp
 *    so a caller can cheaply skip them.
 *  - **IP fragmentation:** fragments are reassembled through a bounded
 *    cache keyed on @c (src, dst, id, protocol); each fragment returns
 *    @ref DemuxStatus::Fragment until the frame that completes a
 *    datagram returns @ref DemuxStatus::Ok.
 *
 * @par Reassembly bounds
 * Offline captures have no real-time clock to age partial datagrams
 * against, so the cache is bounded by count rather than by a wall-clock
 * timeout: at most @ref MaxInFlight partial datagrams are tracked, and
 * the least-recently-touched is evicted when a new one would overflow.
 * A reassembled datagram may not exceed @ref MaxDatagramSize bytes.
 *
 * @par Thread affinity
 * Stateful (the reassembly cache) and not internally synchronised; use
 * one instance per capture-processing thread.
 */
class PacketDemux {
        public:
                /// @brief Maximum number of partially-reassembled datagrams tracked at once.
                static constexpr size_t MaxInFlight = 64;

                /// @brief Hard cap on a reassembled datagram's size (IPv4 total-length ceiling).
                static constexpr size_t MaxDatagramSize = 65535;

                /// @brief IANA protocol number for UDP.
                static constexpr uint8_t ProtocolUdp = 17;

                /// @brief EtherType values the link layer hands to the IP layer.
                static constexpr uint16_t EtherTypeIpv4 = 0x0800;
                static constexpr uint16_t EtherTypeIpv6 = 0x86dd;
                static constexpr uint16_t EtherTypeVlan = 0x8100;  ///< 802.1Q C-VLAN.
                static constexpr uint16_t EtherTypeVlanS = 0x88a8; ///< 802.1ad S-VLAN.

                PacketDemux() = default;

                /**
                 * @brief Demultiplex one captured frame.
                 * @param linkType Link-layer type of @p frame (from the capture).
                 * @param frame    The captured bytes (must be a single
                 *                  contiguous slice, as @ref PcapReader yields).
                 * @return A @ref DemuxResult whose @c status says whether a
                 *         datagram is present, the frame should be skipped,
                 *         or it was consumed as a fragment.
                 */
                DemuxResult demux(PcapLinkType linkType, const BufferView &frame);

                /** @brief Drops all partially-reassembled datagrams. */
                void reset();

                /** @brief Number of partially-reassembled datagrams currently held. */
                size_t pendingReassemblies() const { return _reasm.size(); }

        private:
                /// @brief Result of resolving the link layer down to the IP header.
                enum class L3 {
                        Ipv4,        ///< Frame carries an IPv4 packet at the resolved offset.
                        Ipv6,        ///< Frame carries an IPv6 packet at the resolved offset.
                        NotIp,       ///< Link header parsed but the payload is not IP.
                        TooShort,    ///< Frame is too short to hold the link header.
                        Unsupported, ///< Link type this demux does not handle.
                };

                /// @brief One in-flight IP fragment reassembly.
                struct Reasm {
                                bool ipv6 = false;
                                SocketAddress src;      ///< Source address (port unknown until reassembled).
                                SocketAddress dst;      ///< Destination address.
                                uint32_t id = 0;        ///< IPv4 16-bit ID or IPv6 32-bit identification.
                                uint8_t protocol = 0;   ///< Upper-layer protocol of the fragmented payload.
                                Buffer data;            ///< Reassembly scratch (offset 0 == L4 header).
                                List<uint8_t> filled;   ///< Per-byte coverage flags (1 == received).
                                size_t totalLength = 0; ///< Known once the last fragment arrives; 0 otherwise.
                                bool haveTotal = false;
                                uint64_t tick = 0;      ///< LRU stamp.
                };

                // Resolve the link layer: returns the IP family (or a
                // non-IP / error status) and, for IP, the byte offset at
                // which the IP header begins.
                L3 resolveL3(PcapLinkType linkType, const uint8_t *p, size_t n, size_t &l3off) const;

                DemuxResult demuxIpv4(const BufferView &frame, const uint8_t *p, size_t n, size_t l3off);
                DemuxResult demuxIpv6(const BufferView &frame, const uint8_t *p, size_t n, size_t l3off);

                // Build a UDP datagram from a contiguous L4 region.  When
                // @p ownerBuf is valid the region lives in it (reassembled
                // path) and the payload view aliases it; otherwise the
                // region lives in @p frame and the payload view aliases the
                // capture buffer at absolute offset @p l4abs.
                DemuxResult finishUdp(const BufferView &frame, const uint8_t *l4, size_t l4len,
                                      const SocketAddress &srcNoPort, const SocketAddress &dstNoPort,
                                      const Buffer &ownerBuf, size_t l4abs, bool reassembled);

                // Find-or-create the reassembly entry for this fragment,
                // copy its bytes in, and return Ok with the rebuilt datagram
                // once coverage is complete (else DemuxStatus::Fragment).
                DemuxResult reassemble(const BufferView &frame, bool ipv6, const SocketAddress &srcNoPort,
                                       const SocketAddress &dstNoPort, uint32_t id, uint8_t upperProtocol,
                                       const uint8_t *fragData, size_t fragLen, size_t fragOffset, bool moreFragments);

                List<Reasm> _reasm;     ///< Bounded in-flight reassembly cache.
                uint64_t _tick = 0;     ///< Monotonic counter driving LRU eviction.
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
