/**
 * @file      rxpayloadbundle.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/ancdesc.h>
#include <promeki/ancpacket.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/framenumber.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>
#include <promeki/namespace.h>
#include <promeki/ntptime.h>
#include <promeki/timestamp.h>
#include <promeki/videopayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One reassembled video access unit en route from a per-stream
 *        depacketizer thread to the @c RtpAggregatorThread.
 * @ingroup network
 *
 * The video depacketizer thread fills this struct after each
 * marker-bit / TS-change flush and pushes it onto its outbound
 * @c Queue<RxVideoFrame>.  The aggregator pops it on its own thread
 * and merges it with audio + data into a combined @c Frame for the
 * reader strand.
 *
 * @par captureTime semantics
 * @ref captureTime is the depacketizer's resolved per-frame
 * presentation instant.  When an SR has been observed for the
 * stream, it is derived via @c RtpStreamClock::toNtp() and converted
 * to the local steady clock.  Before the first SR (or whenever the
 * sender's RTCP path is permanently blocked), the depacketizer
 * interpolates against the per-stream @c StreamAnchor —
 * @c arrivalT0 + (rtpTimestamp - rtpTs0) / clockRate.  The
 * aggregator does not re-resolve which mode is in effect; it trusts
 * the value as resolved by the depacketizer.
 *
 * @par wallclockNtp
 * Set when @c streamClock.isValid() — i.e. an SR has arrived.  The
 * aggregator uses it for cross-stream wallclock alignment (RFC 7273-
 * style); pre-SR frames carry an invalid @c NtpTime and the
 * aggregator falls back to the stream-anchor @c captureTime.
 *
 * @par Thread safety
 * Plain value type, no synchronization.  Producer (depacketizer
 * thread) writes once and pushes onto the outbound queue; consumer
 * (aggregator thread) reads after pop.  Queue handoff provides the
 * happens-before relationship.
 */
struct RxVideoFrame {
                /// @brief Reassembled access unit.  Compressed video
                ///        carries an Annex-B bitstream with paramSets
                ///        already injected (where applicable);
                ///        uncompressed video carries native pixel
                ///        bytes per @ref imageDesc.
                VideoPayload::Ptr payload;

                /// @brief Image geometry / pixel format for this
                ///        frame.  Resolved from the per-stream config
                ///        (or, for JPEG, from the JFIF SOF parsed by
                ///        @c JpegGeometryProbe on first frame and on
                ///        geometry change).
                ImageDesc imageDesc;

                /// @brief 32-bit RTP timestamp on the marker packet
                ///        (i.e. the timestamp shared by every packet
                ///        of this access unit).
                uint32_t rtpTimestamp = 0;

                /// @brief Number of RTP packets that participated in
                ///        the reassembly.  Negative values are
                ///        reserved for future "indeterminate"
                ///        sentinels; today the depacketizer always
                ///        sets this to a non-negative count.
                int32_t packetCount = 0;

                /// @brief NTP wallclock for this frame's capture
                ///        instant, derived from
                ///        @c RtpStreamClock::toNtp() when an SR has
                ///        been observed.  @c isValid() is @c false
                ///        before the first SR; the aggregator must
                ///        check before using.
                NtpTime wallclockNtp;

                /// @brief Resolved presentation instant on the local
                ///        steady clock.  Always valid when the frame
                ///        is dispatched — the depacketizer falls
                ///        back to @c StreamAnchor interpolation
                ///        before the first SR.
                TimeStamp captureTime;

                /// @brief Set on H.264 IDR / HEVC IRAP access units
                ///        (or any uncompressed / RFC 2435 frame —
                ///        every uncompressed frame is independently
                ///        decodable).  Used by downstream consumers
                ///        that key on keyframe boundaries.
                bool keyframe = false;

                /// @brief @c TimeStamp::now() snapshot taken at
                ///        @c recvfrom() return on the FIRST RTP
                ///        packet that participated in the
                ///        reassembly.  Useful for jitter / latency
                ///        measurement that needs the first-byte-on-
                ///        wire instant rather than the post-
                ///        reassembly @ref captureTime.
                TimeStamp firstPacketArrival;

                /// @brief Zero-based monotone counter incremented by
                ///        the depacketizer for each successfully
                ///        emitted frame on this stream.  Resets on
                ///        SSRC reset.
                FrameNumber streamFrameIndex;
};

/**
 * @brief One depacketized PCM chunk en route from
 *        @c AudioDepacketizerThread to @c RtpAggregatorThread.
 * @ingroup network
 *
 * Each RTP audio packet produces exactly one @c RxAudioChunk; the
 * depacketizer does no per-frame buffering of its own.  The
 * aggregator owns the @c AudioBuffer FIFO and pulls chunks off the
 * audio queue inside its captureTime-window drain.
 *
 * @par PCM payload
 * @ref pcmBytes is a @ref Buffer slice into the original RTP
 * packet's payload region — no copy is performed at depacketization
 * time.  The aggregator's @c AudioBuffer::push consumes the bytes
 * and is the place format conversion (planar / sample type) happens
 * if needed.
 *
 * @par Thread safety
 * Plain value type.  Same producer-/consumer-ordering rules as
 * @ref RxVideoFrame.
 */
struct RxAudioChunk {
                /// @brief Wire-format PCM bytes for this chunk
                ///        (typically L16 big-endian per RFC 3551
                ///        §4.5.10).  Slice of the underlying RTP
                ///        packet @ref Buffer; no per-chunk
                ///        allocation.
                Buffer pcmBytes;

                /// @brief Wire format describing @ref pcmBytes —
                ///        sample type, sample rate, channel count.
                ///        Used by @c AudioBuffer::push to drive any
                ///        needed conversion / resampling.  This
                ///        carries the actual wire desc seen on the
                ///        packet, so the aggregator's FIFO can be
                ///        constructed lazily on first arrival
                ///        without the desc-mismatch warning the
                ///        old eagerly-constructed FIFO produced.
                AudioDesc wireDesc;

                /// @brief 32-bit RTP timestamp of the first sample
                ///        in this chunk.
                uint32_t rtpTimestamp = 0;

                /// @brief Number of PCM samples per channel in
                ///        @ref pcmBytes.  Total bytes is
                ///        @c sampleCount * channels * bytesPerSample.
                size_t sampleCount = 0;

                /// @brief NTP wallclock for the first sample's
                ///        capture instant when an SR has been
                ///        observed.  Invalid pre-SR.
                NtpTime wallclockNtp;

                /// @brief Resolved local-steady presentation
                ///        instant for the first sample.  Always
                ///        valid when the chunk is dispatched.
                TimeStamp captureTime;

                /// @brief @c TimeStamp::now() snapshot taken at
                ///        @c recvfrom() return on the underlying
                ///        RTP packet.
                TimeStamp firstPacketArrival;
};

/**
 * @brief One reassembled JSON metadata blob en route from
 *        @c DataDepacketizerThread to @c RtpAggregatorThread.
 * @ingroup network
 *
 * The data depacketizer parses JSON inside its own thread and
 * publishes a fully-typed @ref Metadata blob; the aggregator merges
 * the most-recent message whose @ref captureTime falls in the
 * current video window into the outgoing @c Frame.
 *
 * @par Thread safety
 * Plain value type.  @ref metadata is a CoW handle, so copy / move
 * are cheap; no per-message allocation in the aggregator's drain
 * loop.
 */
struct RxDataMessage {
                /// @brief Parsed metadata payload, ready to merge
                ///        into the outgoing @c Frame::metadata().
                Metadata metadata;

                /// @brief 32-bit RTP timestamp on the marker packet.
                uint32_t rtpTimestamp = 0;

                /// @brief Number of RTP packets that participated
                ///        in the reassembly.
                int32_t packetCount = 0;

                /// @brief NTP wallclock for the message's capture
                ///        instant when an SR has been observed.
                ///        Invalid pre-SR.
                NtpTime wallclockNtp;

                /// @brief Resolved local-steady presentation
                ///        instant for the message.  Always valid
                ///        when dispatched.
                TimeStamp captureTime;

                /// @brief @c TimeStamp::now() snapshot taken at
                ///        @c recvfrom() return on the FIRST RTP
                ///        packet that participated in the
                ///        reassembly.
                TimeStamp firstPacketArrival;
};

/**
 * @brief One reassembled ANC frame en route from
 *        @c RtpAncDepacketizerThread to @c RtpAggregatorThread.
 * @ingroup network
 *
 * RFC 8331 ANC packets ride through the pipeline in their canonical
 * ST 291 form (@ref AncTransport::St291).  The depacketizer thread
 * accumulates RTP packets across the marker bit / timestamp boundary,
 * runs @c RtpPayloadAnc::unpackAncPackets, and emits one
 * @ref RxAncFrame carrying every ANC packet in that timestamp.  The
 * aggregator merges the frame's ANC packets into the outgoing
 * @c Frame's @ref AncPayload at the same RTP-TS slot as the paired
 * video / audio.
 *
 * @par Stream descriptor
 *
 * @ref desc captures the per-stream @ref AncDesc the depacketizer
 * stamped onto this frame — typically the static descriptor configured
 * at SDP-apply time (raster + scan mode inherited from the paired
 * video, allowedFormats from the fmtp DID_SDID list).  The aggregator
 * forwards it onto the @ref AncPayload it builds.
 *
 * @par Thread safety
 *
 * Plain value type — same producer/consumer ordering rules as
 * @ref RxVideoFrame, @ref RxAudioChunk, @ref RxDataMessage.
 * @ref AncPacket is a CoW value-handle so the contained @ref packets
 * list is cheap to move / copy across the queue handoff.
 */
struct RxAncFrame {
                /// @brief Per-stream descriptor.  Carries the
                ///        configured @ref AncDesc::allowedFormats list
                ///        and any paired-video raster context the
                ///        configure step populated; the aggregator
                ///        forwards it onto the produced
                ///        @ref AncPayload without modification.
                AncDesc desc;

                /// @brief Every ANC packet on this frame, in arrival
                ///        order.
                AncPacket::List packets;

                /// @brief 32-bit RTP timestamp shared by every packet
                ///        in this frame.
                uint32_t rtpTimestamp = 0;

                /// @brief Number of RTP packets that participated in
                ///        the reassembly.
                int32_t packetCount = 0;

                /// @brief NTP wallclock for the frame's capture
                ///        instant when an SR has been observed.
                ///        Invalid pre-SR.
                NtpTime wallclockNtp;

                /// @brief Resolved local-steady presentation instant
                ///        for the frame.  Always valid when
                ///        dispatched.
                TimeStamp captureTime;

                /// @brief @c TimeStamp::now() snapshot taken at
                ///        @c recvfrom() return on the FIRST RTP
                ///        packet that participated in the
                ///        reassembly.
                TimeStamp firstPacketArrival;
};

PROMEKI_NAMESPACE_END
