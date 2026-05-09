/**
 * @file      rtcppacket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/ntptime.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Builders for RFC 3550 RTCP packet wire formats.
 * @ingroup network
 *
 * Provides static helpers that produce the exact byte-shape RTCP
 * receivers expect.  The builders return @ref Buffer by value
 * (independent allocation per packet); a compound packet — the form
 * RFC 3550 §6.1 mandates that every RTCP datagram must take — is
 * produced by @ref compound, which concatenates several builder
 * outputs into a single contiguous buffer.
 *
 * @par Common header (RFC 3550 §6.4.1)
 * @code
 * Byte 0:    V(2) | P(1) | RC(5)
 * Byte 1:    PT(8)
 * Bytes 2-3: Length (in 32-bit words minus one, big-endian)
 * @endcode
 *
 * Sender Report (PT=200) extends this with:
 * @code
 * SSRC          (4 bytes)
 * NTP timestamp (8 bytes — seconds, then fraction)
 * RTP timestamp (4 bytes)
 * Packet count  (4 bytes)
 * Octet count   (4 bytes)
 * [+ RC × 24-byte report blocks; this builder always emits RC=0.]
 * @endcode
 *
 * SDES (PT=202) carries one or more chunks of typed text items per
 * SSRC.  This builder emits the minimal compliant shape: one chunk,
 * containing exactly one @c CNAME item, terminated and aligned to a
 * 32-bit boundary.
 *
 * @par Why separate builders rather than parsed structs
 * RTCP is small, low-rate, and we generate far more than we receive
 * (we are a sender today).  Direct byte-shape builders are easier to
 * audit against the RFCs than a generic struct + serialize design,
 * and the unit tests assert byte-shape directly.
 */
class RtcpPacket {
        public:
                /// @brief RFC 3550 RTCP packet types.
                enum Type : uint8_t {
                        SenderReport = 200,      ///< RFC 3550 §6.4.1
                        ReceiverReport = 201,    ///< RFC 3550 §6.4.2
                        SourceDescription = 202, ///< RFC 3550 §6.5
                        Goodbye = 203,           ///< RFC 3550 §6.6
                        Application = 204,       ///< RFC 3550 §6.7
                };

                /// @brief RFC 3550 §6.5 SDES item types.
                enum SdesItemType : uint8_t {
                        SdesEnd = 0,
                        SdesCname = 1,
                        SdesName = 2,
                        SdesEmail = 3,
                        SdesPhone = 4,
                        SdesLoc = 5,
                        SdesTool = 6,
                        SdesNote = 7,
                        SdesPriv = 8,
                };

                /**
                 * @brief Builds a Sender Report (PT=200) with no report
                 *        blocks (RC=0).
                 *
                 * The returned buffer is exactly 28 bytes — the 4-byte
                 * common header plus the 24-byte sender info block.
                 * Suitable for direct concatenation into a compound
                 * packet via @ref compound.
                 *
                 * @param ssrc                The sender's SSRC.
                 * @param ntp                 Wall-clock timestamp at the
                 *                            moment this report is built.
                 * @param rtpTimestamp        RTP timestamp corresponding
                 *                            to the same instant as @p ntp,
                 *                            in this stream's RTP clock.
                 * @param senderPacketCount   Total RTP packets the sender
                 *                            has emitted on this stream
                 *                            since it started sending
                 *                            (not since the last SR).
                 * @param senderOctetCount    Total RTP-payload octets
                 *                            (excludes RTP headers).
                 */
                static Buffer buildSenderReport(uint32_t ssrc, const NtpTime &ntp, uint32_t rtpTimestamp,
                                                uint32_t senderPacketCount, uint32_t senderOctetCount);

                /**
                 * @brief Builds a Source Description (PT=202) packet
                 *        carrying a single chunk with a single CNAME
                 *        item.
                 *
                 * RFC 3550 §6.5 mandates every CNAME be sent at least
                 * once per RTCP interval.  This builder emits the
                 * minimal compliant shape — empty CNAME strings emit a
                 * zero-length CNAME item, which is a legal value.
                 *
                 * @param ssrc  The sender's SSRC.
                 * @param cname The CNAME text — typically of the form
                 *              @c "user@host" or any stable per-source
                 *              identifier the receiver can use to
                 *              correlate streams that share a CNAME
                 *              even if they have different SSRCs.
                 */
                static Buffer buildSourceDescriptionCname(uint32_t ssrc, const String &cname);

                /**
                 * @brief Concatenates RTCP packets into a single
                 *        compound packet.
                 *
                 * Every RTCP datagram on the wire MUST be a compound
                 * packet starting with an SR or RR (RFC 3550 §6.1).
                 * The compound is just the byte concatenation of each
                 * member packet; no extra framing is required.
                 *
                 * @param packets List of pre-built RTCP packets in the
                 *                order they should appear on the wire.
                 *                Must be non-empty.
                 */
                static Buffer compound(const List<Buffer> &packets);

                /**
                 * @brief Parsed RTCP common header (RFC 3550 §6.4.1).
                 *
                 * @c lengthBytes is the total packet length on the
                 * wire in bytes — the @c length field of the common
                 * header carries it as 32-bit words minus one, this
                 * struct surfaces the byte count for direct use in
                 * compound walking.
                 */
                struct Header {
                        uint8_t version = 0;     ///< @brief RTCP version (must be 2).
                        bool    padding = false; ///< @brief P bit.
                        uint8_t rc = 0;          ///< @brief RC / SC count.
                        uint8_t pt = 0;          ///< @brief Packet type.
                        size_t  lengthBytes = 0; ///< @brief Total packet length in bytes.

                        /// @brief True when the header is structurally
                        ///        sound (V=2 and a non-zero length).
                        bool isValid() const { return version == 2 && lengthBytes >= 4; }
                };

                /**
                 * @brief Parses the common header from the first 4
                 *        bytes of @p data.
                 *
                 * Returns a header with @ref Header::isValid set to
                 * @c false on truncated input.  Does not validate
                 * that @c lengthBytes fits within @p size — that
                 * check is the caller's responsibility (see
                 * @ref findSenderReports for the standard pattern).
                 *
                 * @param data First byte of the RTCP packet.
                 * @param size Bytes available at @p data.
                 */
                static Header parseHeader(const uint8_t *data, size_t size);

                /**
                 * @brief Parsed sender report payload.
                 *
                 * Receivers use the @c (ntp, rtpTimestamp) pair to
                 * derive the wallclock instant of any subsequent
                 * RTP packet on this stream — see @ref RtpStreamClock.
                 * Report blocks (one per source other than the SR's
                 * own SSRC) live at offset 28 inside the SR and are
                 * not surfaced today; receivers we ship don't act on
                 * them.
                 */
                struct SenderReportInfo {
                        uint32_t ssrc = 0;              ///< @brief SR's source SSRC.
                        NtpTime  ntp;                   ///< @brief Wallclock at the SR sender.
                        uint32_t rtpTimestamp = 0;      ///< @brief RTP-TS aligning with @c ntp.
                        uint32_t senderPacketCount = 0; ///< @brief Cumulative packets sent.
                        uint32_t senderOctetCount = 0;  ///< @brief Cumulative payload octets sent.
                };

                /**
                 * @brief Parses a single Sender Report packet body.
                 *
                 * @p data must point at the start of an SR packet
                 * (V/P/RC | PT=200 | length | SSRC | NTP | …).  The
                 * report blocks (RC × 24 bytes) following the sender
                 * info block are not parsed; only the fixed sender
                 * info is extracted.
                 *
                 * @param data First byte of the SR packet.
                 * @param size Bytes available at @p data.  Must cover
                 *             the full 28-byte SR sender info block.
                 * @param out  Receives the parsed SR fields on success.
                 * @return @c true on a structurally valid SR (V=2,
                 *         PT=200, length covers the sender info block);
                 *         @c false otherwise (caller must treat
                 *         @p out as garbage).
                 */
                static bool parseSenderReport(const uint8_t *data, size_t size, SenderReportInfo *out);

                /**
                 * @brief Walks a compound RTCP datagram and extracts
                 *        every Sender Report it contains.
                 *
                 * The standard receive-thread pattern: when the
                 * transport hands us a compound RTCP packet, we walk
                 * the sub-packets, collect SRs (which we use for
                 * lip-sync), and ignore everything else.  RTCP is
                 * forward-compatible by design — unknown packet
                 * types and SDES / BYE / APP packets are dropped
                 * silently.  Malformed sub-packets terminate the walk.
                 *
                 * Compound packets typically carry one SR (the one
                 * @c RtpSession::emitRtcpSr produces) followed by an
                 * SDES.  Multi-SR compounds are RFC-legal and the
                 * walk handles them — every SR's
                 * @ref SenderReportInfo is appended to the result.
                 *
                 * @param data First byte of the compound packet.
                 * @param size Bytes available at @p data.
                 * @return The list of parsed sender reports (possibly
                 *         empty when the compound carried none).
                 */
                static List<SenderReportInfo> findSenderReports(const uint8_t *data, size_t size);
};

PROMEKI_NAMESPACE_END
