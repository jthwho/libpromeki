/**
 * @file      rtppayloadrawvideo.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/enums_st2110.h>
#include <promeki/enums_video.h>
#include <promeki/namespace.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RTP payload handler for IETF RFC 4175 / SMPTE ST 2110-20
 *        uncompressed video.
 * @ingroup network
 *
 * Packs uncompressed video frames as a sequence of RTP packets that
 * each carry a 2-octet @b Extended @b Sequence @b Number (ESN)
 * followed by 1 to 3 @b Sample @b Row @b Data (SRD) Headers and the
 * matching scan-line segments.  The wire format is defined by
 * RFC 4175 §4 and refined by SMPTE ST 2110-20:2022 §6.1.4 / §6.2 /
 * §6.3.
 *
 * @par Payload header layout (§6.1.4)
 *
 * @code
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Extended Sequence Number   |          SRD Length           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |F|        SRD Row Number       |C|         SRD Offset          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      SRD Length (optional)    |F|     SRD Row Number          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |C|     SRD Offset              |  ...further SRDs / data...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * @endcode
 *
 *  - **ESN** — 16-bit high half of the per-stream 32-bit extended
 *    packet sequence counter; the RTP header's @c sequence @c number
 *    carries the low 16 bits.  Wraps every ~93 hours at 1 Gbps so
 *    cross-frame ordering remains unambiguous (§6.1.4).
 *  - **SRD Length** — 16-bit big-endian byte count of sample data
 *    that follows this SRD's header, and shall be a multiple of the
 *    pgroup octet length (§6.2.1).  A length of 0 is forbidden
 *    except in the case where exactly one SRD Header is present and
 *    no sample data follows it — that is the empty-payload
 *    keep-alive form.
 *  - **F bit** — Field Identification (§6.1.4 / §6.1.5):
 *    - Progressive: always 0.
 *    - Interlaced: 0 for the temporally first field, 1 for the
 *      second.
 *    - PsF: 0 for the first segment, 1 for the second.
 *  - **SRD Row Number** — 15-bit zero-based row index within the
 *    active sample array; restarts at 0 at the top of each field
 *    (interlaced) or segment (PsF).  For 4:2:0 progressive video,
 *    set to the **first** luma row of the paired pair; only every
 *    other Sample Row is signaled (§6.1.5).
 *  - **C bit** — Continuation; set to 1 if another SRD Header
 *    follows in the same packet (§6.1.4).  At most three SRD
 *    Headers may appear in a single packet (§6.2.1).
 *  - **SRD Offset** — 15-bit big-endian pixel offset within the row,
 *    indexed at the full-bandwidth luma sample (§6.1.4).
 *
 * @par 32-bit extended sequence counter
 *
 * The class maintains a per-stream 32-bit packet counter that
 * increments on every emitted packet across @ref pack invocations.
 * The high 16 bits go to the ESN field; the low 16 bits are stamped
 * by the @ref RtpSession into the RTP header sequence number.  Use
 * @ref setPacketCounter to align the payload's counter with the
 * session's chosen starting RTP sequence number before the first
 * @ref pack call — the depacketizer recovers the full 32-bit count
 * by concatenating @c (ESN << 16) | RTP_seq.
 *
 * @par Packing strategy (E20a — pgroup-faithful single-line GPM)
 *
 * For E20a, the packer keeps the same @em pgroup @em bytes
 * abstraction that the earlier RFC 4175 path used (one fixed
 * @c pgroupBytes value per stream).  Multi-SRD packing is emitted
 * opportunistically — when consecutive short scan lines fit
 * alongside another SRD, they are coalesced into a single packet
 * (up to three SRDs) to avoid sub-1000-octet datagrams flagged by
 * ST 2110-20 §6.3.2.  Long lines that exceed the per-packet payload
 * budget still fragment into multiple single-SRD packets.
 *
 * F-bit support is wired through @ref setFieldBit but the per-frame
 * source split (interlaced / PsF) lands in a later phase.  4:2:0
 * paired-row signaling, BPM packing mode, and the full ST 2110-20
 * pgroup matrix land in subsequent phases (E20b / E20d / E20e).
 *
 * @par Example
 * @code
 * RtpPayloadRawVideo payload(1920, 1080, 24);  // 1080p RGB8
 * payload.setPacketCounter(session.nextSeq32());
 * auto packets = payload.pack(frameData, frameSize);
 * @endcode
 */
class RtpPayloadRawVideo : public RtpPayload {
        public:
                /// @brief RTP clock rate for RFC 4175 / ST 2110-20.
                ///        §6.1.3 hard-mandates 90 kHz.
                static constexpr uint32_t ClockRate = 90000;

                /// @brief Default dynamic RTP payload type.  RFC 3551
                ///        §6 reserves 96-127 for dynamic mapping;
                ///        ST 2110-10 §6.2 inherits this rule.
                static constexpr uint8_t DefaultPayloadType = 96;

                /// @brief Bytes consumed by the Extended Sequence
                ///        Number field at the start of every RTP
                ///        payload (§6.1.4).
                static constexpr size_t ExtSeqSize = 2;

                /// @brief Bytes consumed by one Sample Row Data
                ///        Header — SRD Length (16) + F+Row (16) +
                ///        C+Offset (16) (§6.1.4).
                static constexpr size_t SrdHeaderSize = 6;

                /// @brief Maximum number of SRD Headers permitted in a
                ///        single RTP packet (§6.2.1).
                static constexpr size_t MaxSrdsPerPacket = 3;

                /// @brief Block size in octets for ST 2110-20 §6.3.3
                ///        Block Packing Mode.  Every BPM RTP payload is
                ///        an integer multiple of this size; the
                ///        receiver expects fixed-size packets and
                ///        zero-padding after the last SRD's sample
                ///        data within each block-aligned packet.
                static constexpr size_t BpmBlockOctets = 180;

                /// @brief Standard-UDP block count per BPM packet
                ///        (§6.3.3).  7 × 180 = 1260-octet payload fits
                ///        the Standard UDP Size Limit (1500 - 20 IP
                ///        - 8 UDP - 12 RTP = 1460 budget) with room
                ///        for IP/UDP/RTP overhead.  Extended UDP Size
                ///        Limit is forbidden in BPM (§6.3.3).
                static constexpr size_t BpmStandardBlocksPerPacket = 7;

                /// @brief Returns the largest @ref BpmBlockOctets
                ///        multiple that fits within @p maxPayloadSize
                ///        octets, capped at @ref BpmStandardBlocksPerPacket
                ///        × 180 = 1260 (the Standard UDP limit per
                ///        §6.3.3, which forbids Extended UDP in BPM).
                ///        Returns 0 when @p maxPayloadSize cannot hold
                ///        a single block plus ESN + SRD header
                ///        overhead.
                static size_t bpmBlocksPerPacket(size_t maxPayloadSize);

                /// @brief Returns @ref bpmBlocksPerPacket multiplied
                ///        by @ref BpmBlockOctets — i.e. the exact RTP
                ///        payload size every BPM packet emits at the
                ///        given UDP budget.
                static size_t bpmTargetPayloadSize(size_t maxPayloadSize);

                /// @brief ST 2110-20 §6.3.2 GPM short-packet floor —
                ///        General Packing Mode payloads below this size
                ///        should be avoided except at the tail of a
                ///        field/frame.  The packetizer logs a
                ///        throttled warning when it is forced to emit
                ///        a shorter non-tail packet (typically caused
                ///        by an MTU below ~1100 octets).
                static constexpr size_t GpmShortPacketFloor = 1000;

                /**
                 * @brief Builds a single SRD-Length=0 "keep-alive"
                 *        packet per ST 2110-20 §6.2.1.
                 *
                 * The packet contains exactly one SRD Header with
                 * @c SRD @c Length=0, no sample data, the @c C bit
                 * cleared, and the supplied @p fieldBit / @p srdRow
                 * stamped into the F + Row Number fields.  Useful for
                 * low-rate streams that want to assert a frame-boundary
                 * marker without forwarding any pixel data — the
                 * receiver's @ref unpack tolerates the zero-length SRD
                 * as a no-op write.
                 *
                 * @param packetCounter 32-bit extended sequence number;
                 *        the high 16 bits go into the wire ESN field
                 *        and the low 16 bits are carried in the RTP
                 *        header sequence number by the caller / session.
                 * @param srdRow SRD Row Number to stamp (15 bits).
                 * @param fieldBit F-bit value (false = first field /
                 *        segment / progressive; true = second field /
                 *        segment).
                 * @param payloadType RTP payload type (defaults to
                 *        @ref DefaultPayloadType).
                 *
                 * @return One @ref RtpPacket carrying the ESN + a
                 *         single zero-length SRD Header (total RTP
                 *         payload size = ExtSeqSize + SrdHeaderSize =
                 *         8 octets) with the marker bit set so the
                 *         receiver's frame-boundary detector fires.
                 */
                static RtpPacket makeKeepAlive(uint32_t packetCounter,
                                               uint16_t srdRow      = 0,
                                               bool     fieldBit    = false,
                                               uint8_t  payloadType = DefaultPayloadType);

                /**
                 * @brief Constructs a raw video payload handler.
                 * @param width Frame width in pixels.
                 * @param height Frame height in pixels.
                 * @param bitsPerPixel Bits per pixel of the in-memory
                 *        source (used to compute pixel offsets from
                 *        byte offsets on the wire).  For 4:2:0 wire
                 *        formats the value is wire-bits-per-image-row-
                 *        pixel multiplied by @p rowsPerSrd (e.g. 30
                 *        for 10-bit 4:2:0 — pgroup is 15 octets per 4
                 *        image-row pixels paired with the next image
                 *        row's 4 pixels).  See §6.2.5.
                 * @param pgroupBytes RFC 4175 pixel-group size in
                 *        octets.  A pgroup is the smallest byte-aligned
                 *        unit of pixel data — e.g. 3 for RGB 8-bit, 4
                 *        for YCbCr-4:2:2 8-bit, 5 for YCbCr-4:2:2
                 *        10-bit.  When 0, defaults to
                 *        @c bitsPerPixel/8 (correct for any sampling
                 *        where one pixel forms one pgroup).
                 * @param rowsPerSrd Image rows that each emitted SRD
                 *        covers.  1 (default) for progressive
                 *        4:4:4 / 4:2:2 / Key / XYZ — one SRD per image
                 *        row.  2 for 4:2:0 progressive per §6.2.5 —
                 *        each SRD covers a row pair, with SRD Row
                 *        Number set to the first luma row of the
                 *        pair.  The buffer width × @c bitsPerPixel
                 *        per pgroup-aligned row covers @c rowsPerSrd
                 *        image rows of pixel data.
                 */
                RtpPayloadRawVideo(int width, int height, int bitsPerPixel, int pgroupBytes = 0, int rowsPerSrd = 1);

                /** @copydoc RtpPayload::payloadType() */
                uint8_t payloadType() const override { return _payloadType; }
                /** @copydoc RtpPayload::clockRate() */
                uint32_t clockRate() const override { return ClockRate; }
                /** @copydoc RtpPayload::pack() */
                RtpPacket::List pack(const void *mediaData, size_t size) override;
                /** @copydoc RtpPayload::unpack() */
                Buffer unpack(const RtpPacket::List &packets) override;

                /** @brief Sets the RTP payload type number. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Returns the frame width. */
                int width() const { return _width; }

                /** @brief Returns the frame height. */
                int height() const { return _height; }

                /** @brief Returns the source bits per pixel. */
                int bitsPerPixel() const { return _bitsPerPixel; }

                /** @brief Returns the pgroup size in bytes. */
                int pgroupBytes() const { return _pgroupBytes; }

                /// @brief Number of image rows each emitted SRD covers
                ///        (1 for progressive 4:4:4 / 4:2:2 / Key / XYZ,
                ///        2 for 4:2:0 progressive per §6.2.5).
                int rowsPerSrd() const { return _rowsPerSrd; }

                /**
                 * @brief Sets the 32-bit packet counter used to
                 *        populate the Extended Sequence Number on
                 *        emitted packets.
                 *
                 * The high 16 bits of @p value land in the ESN field
                 * of the first packet of the next @ref pack call; the
                 * counter then increments by one per emitted packet
                 * and persists across @ref pack invocations.
                 *
                 * Call this with the @ref RtpSession's chosen starting
                 * RTP sequence number (extended to 32 bits) on stream
                 * start so the high 16 bits of the ESN and the low 16
                 * bits of the RTP header concatenate into the same
                 * 32-bit packet count on the receiver.
                 *
                 * @param value New value of the 32-bit packet counter.
                 */
                void setPacketCounter(uint32_t value) { _packetCounter = value; }

                /**
                 * @brief Returns the current value of the 32-bit
                 *        packet counter.
                 *
                 * Reads back the next packet's ESN base after a
                 * @ref pack call so the caller can sync any
                 * separately-maintained sequence-number state.
                 */
                uint32_t packetCounter() const { return _packetCounter; }

                /**
                 * @brief Sets the F (Field) bit value stamped into
                 *        every SRD Header emitted by the next
                 *        @ref pack call when @ref interlaceMode is
                 *        @c InterlaceMode::Progressive.
                 *
                 * Manual override — useful for tests that need to
                 * exercise the wire F-bit handling without involving
                 * the field-splitting pack path.  In @c Interlaced /
                 * @c Psf modes this value is ignored; the packer
                 * stamps F=0 on the first field/segment and F=1 on
                 * the second.
                 *
                 * @param f @c true to set F=1 on emission, @c false
                 *          (default) for F=0.
                 */
                void setFieldBit(bool f) { _fieldBit = f; }

                /** @brief Returns the current manual F-bit override. */
                bool fieldBit() const { return _fieldBit; }

                /**
                 * @brief Selects the frame-splitting mode used by
                 *        @ref pack / @ref unpack per §6.1.5.
                 *
                 * Default is @c VideoScanMode::Progressive — no
                 * splitting; one pass over the source frame with F=0
                 * on every SRD and the marker bit stamped only on
                 * the last packet of the frame.
                 *
                 *  - @c Interlaced (incl. @c InterlacedEvenFirst /
                 *    @c InterlacedOddFirst) — source frame is split
                 *    into two fields by row parity.  Field 0 (top
                 *    for even-first, bottom for odd-first) is sent
                 *    with F=0, then field 1 with F=1.  SRD Row
                 *    Numbers within each field restart at 0 and
                 *    count the field's own rows.  Odd-height sources
                 *    give the temporally-first field one extra line
                 *    per §6.1.5.  @c Interlaced (unspecified field
                 *    order) defaults to even-first.
                 *  - @c PsF — Progressive segmented Frame.  Source
                 *    is split into top + bottom segments; F=0 first,
                 *    F=1 second; SRD Row Numbers restart at 0 per
                 *    segment; odd-height first segment carries the
                 *    extra line.
                 *  - @c Unknown — treated as @c Progressive (the
                 *    SDP default per §7.3 when @c interlace is
                 *    absent).
                 *
                 * The marker bit is stamped on the last packet of
                 * each field/segment for the interlaced and PsF
                 * modes so the receiver's frame-boundary detector
                 * fires on field boundaries per §6.1.2.
                 *
                 * The depacketizer uses the same setting to
                 * reassemble the receive-side buffer (fields row-
                 * interleaved for Interlaced; segments vertically
                 * concatenated for PsF).
                 *
                 * @param m New scan mode.
                 */
                void setScanMode(VideoScanMode m) { _scanMode = m; }

                /// @brief Returns the currently-selected scan mode.
                VideoScanMode scanMode() const { return _scanMode; }

                /**
                 * @brief Sets the SMPTE ST 2110-20 §6.3 packing mode.
                 *
                 * @c Gpm (default — General Packing Mode, §6.3.2) uses
                 * variable-size RTP payloads up to @ref maxPayloadSize
                 * and packs 1..3 SRDs per packet to avoid sub-1000-
                 * octet datagrams.
                 *
                 * @c Bpm (Block Packing Mode, §6.3.3) emits fixed-size
                 * payloads of @ref bpmTargetPayloadSize octets — every
                 * RTP payload is an integer multiple of
                 * @ref BpmBlockOctets, including the last packet of a
                 * field/frame which is zero-padded to maintain the
                 * same packet size.  Extended UDP Size Limit is
                 * forbidden in BPM.
                 *
                 * BPM requires the pgroup size to divide
                 * @ref BpmBlockOctets evenly — every standard ST
                 * 2110-20 §6.2 pgroup (1, 3, 4, 5, 6, 9, 15 octets)
                 * satisfies this, but pgroup-8 formats (4:2:2/16 and
                 * 4:2:2/16f) do not and the packer falls back to GPM
                 * with a one-shot warning.
                 *
                 * @param pm Packing mode.  Pass @c St2110PackingMode::Bpm
                 *           to opt into 180-octet block packing.
                 */
                void setPackingMode(St2110PackingMode pm) { _packingMode = pm; }

                /// @brief Returns the currently-selected packing mode.
                St2110PackingMode packingMode() const { return _packingMode; }

        private:
                int               _width;
                int               _height;
                int               _bitsPerPixel;
                int               _pgroupBytes;
                int               _rowsPerSrd = 1;
                uint8_t           _payloadType = DefaultPayloadType;
                uint32_t          _packetCounter = 0;
                bool              _fieldBit = false;
                St2110PackingMode _packingMode = St2110PackingMode::Gpm;
                VideoScanMode     _scanMode = VideoScanMode::Progressive;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
