/**
 * @file      ancpacket.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/ancformat.h>
#include <promeki/buffer.h>
#include <promeki/metadata.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief The generic carrier for one ancillary-data packet.
 * @ingroup proav
 *
 * Holds the four pieces of state every ANC packet needs regardless of
 * its wire format:
 *
 *  - @ref AncFormat — the logical "what kind of data" (CEA-708,
 *    AFD, ATC LTC, …).
 *  - @ref AncTransport — the wire carrier the bytes currently live in
 *    (ST 291 packet, NDI XML element, RTMP AMF script tag, …).
 *  - @ref Buffer — the wire-form bytes themselves, in the layout the
 *    transport defines.  Stored as-received so byte-exact replay on a
 *    matching sink is possible without re-encoding.
 *  - @ref Metadata — per-transport framing sidecar (the VANC line
 *    a captured ST 291 packet was on, the AMF script-tag name on an
 *    RTMP packet, the InfoFrame type byte on an HDMI packet, …).  The
 *    keys are declared in @c ancmeta.h.
 *
 * @par CoW value-type handle
 *
 * @c AncPacket is a CoW value-type handle wrapping a
 * @c SharedPtr&lt;Impl&gt;: copying an @c AncPacket bumps a refcount;
 * mutators (@c setFormat, @c setTransport, @c setData, @c setMeta,
 * @c dataMut, @c metaMut) detach into a freshly allocated @c Impl
 * when the refcount is greater than one.  The handle is two pointers
 * wide (the @c SharedPtr header plus the data pointer) and is cheap
 * to pass through a pipeline by value.
 *
 * Per the post-2026-05-07 convention, @c AncPacket does **not**
 * expose a @c ::Ptr alias: the internal @c SharedPtr is what makes
 * the value-type API cheap, and an additional outer shared pointer
 * would just double-wrap the same refcounted data.  @c ::List is
 * @c List&lt;AncPacket&gt; — a vector of one-pointer values that an
 * @ref AncPayload threads through the pipeline.
 *
 * @par Lifecycle
 *
 * @c AncPacket is produced by:
 *  - Backends on capture, by wrapping freshly received wire bytes
 *    (via @ref St291Packet::buildRaw, @ref HdmiInfoFrame::buildRaw,
 *    or a transport-specific equivalent).
 *  - Translators on the egress side, by calling
 *    @ref AncTranslator::translate or @ref AncTranslator::build.
 *  - Typed builders (e.g. @c Cea708Cdp::build, @c Atc::build) that
 *    encode a typed Variant onto a chosen transport.
 *
 * It is consumed by:
 *  - Sinks on egress, by reading @ref data and @ref meta and emitting
 *    the wire bytes.
 *  - Typed parsers (@c Cea708Cdp::parse, @c Atc::parse, …) that
 *    decode the wire bytes back into a typed Variant.
 *
 * @par Validity
 *
 * A default-constructed @c AncPacket is invalid: its format is
 * @c AncFormat::Invalid, transport is @c AncTransport::Invalid, data
 * is empty, meta is empty.  @ref isValid is the gate-and-go check
 * (true when both @c format and @c transport are non-invalid).
 *
 * @par Thread Safety
 *
 * The handle itself is **not** thread-safe — the same caveat as
 * every @c SharedPtr-backed value type in the library.  Copying or
 * destroying an @c AncPacket from one thread while another thread
 * holds a copy is safe (refcount operations are atomic); but
 * mutating one handle while another thread reads the same handle
 * is undefined behaviour.  Hand @c AncPackets to other threads by
 * value (cheap refcount bump) rather than by reference.
 *
 * @see AncFormat, AncTransport, AncMeta, AncTranslator,
 *      AncPayload, St291Packet, HdmiInfoFrame
 */
class AncPacket {
        public:
                /** @brief List of @c AncPackets. */
                using List = ::promeki::List<AncPacket>;

                /**
                 * @brief Default-constructs an invalid @c AncPacket.
                 *
                 * Format is @c AncFormat::Invalid, transport is
                 * @c AncTransport::Invalid, data is an empty
                 * @ref Buffer, meta is an empty @ref Metadata.  Returns
                 * @c false from @ref isValid.
                 */
                AncPacket();

                /**
                 * @brief Constructs an @c AncPacket from a complete
                 *        (format, transport, data, meta) tuple.
                 *
                 * Used by backends on the capture path and by builders
                 * on the egress path.
                 *
                 * @param fmt       The logical format.
                 * @param transport The wire transport.
                 * @param data      The wire-form bytes.
                 * @param meta      The per-transport sidecar (empty by default).
                 */
                AncPacket(const AncFormat &fmt, const AncTransport &transport, Buffer data, Metadata meta = Metadata());

                /** @brief Returns the logical format. */
                const AncFormat &format() const;

                /** @brief Returns the wire transport. */
                const AncTransport &transport() const;

                /**
                 * @brief Returns the wire-form bytes.
                 *
                 * For @c AncTransport::St291 packets the buffer holds
                 * the canonical post-ADF payload — DID, SDID
                 * (or DBN for Type-1), DataCount, UDW…, Checksum — as
                 * 10-bit words packed MSB-first.  The Ancillary Data
                 * Flag (ADF, ST 291-1 §5.2: @c 000h @c 3FFh @c 3FFh for
                 * component or @c 3FCh for composite) is **not** stored
                 * here.  RFC 8331 §1 makes the same choice for the RTP
                 * carriage.
                 *
                 * Backends that ingest raw SDI VANC (e.g. NTV2) must
                 * strip the ADF before constructing the @c AncPacket;
                 * backends that emit to SDI must prepend the ADF
                 * themselves.  Transports that have no ADF concept
                 * (RTP, NDI, RTMP, HDMI InfoFrame, MPEG-TS) see the
                 * same canonical post-ADF bytes via this accessor.
                 */
                const Buffer &data() const;

                /** @brief Returns the per-transport metadata sidecar.
                 *
                 *  Holds application-defined keys and any
                 *  transport-specific framing the library does **not**
                 *  promote to a direct field on this handle.  The hot
                 *  ST 291 framing fields (line, hOffset, fieldB, cBit,
                 *  streamNum) live as direct accessors on this class
                 *  (see @ref st291Line) so the RTP pack/unpack hot path
                 *  does not pay Metadata-lookup cost; only transports
                 *  whose framing is rarer or longer (ATC rate, HDMI
                 *  InfoFrame type, RTMP script name, NDI XML element,
                 *  MPEG-TS PID, SEI payload type) still use this
                 *  container. */
                const Metadata &meta() const;

                /** @brief ST 291 VANC line number the packet was captured on.
                 *
                 *  Default @c 0x7FE — the RFC 8331 §2.2 / RP 168
                 *  sentinel meaning "any line from two lines after the
                 *  switching point to the last line before active
                 *  video", which ST 2110-40 §5.2.2 recommends as the
                 *  default when an exact line is unknown.  Other
                 *  reserved sentinels are @c 0x7FF (no specific line)
                 *  and @c 0x7FD (line number larger than 11 bits).  See
                 *  @ref St291Packet for the named constants. */
                uint16_t st291Line() const;

                /** @brief ST 291 horizontal offset within the line.
                 *
                 *  Default @c 0xFFF — RFC 8331 §2.2 "no specific
                 *  horizontal location".  Other reserved sentinels are
                 *  @c 0xFFE (within HANC space), @c 0xFFD (within
                 *  active-video region), and @c 0xFFC (value larger
                 *  than 12 bits). */
                uint16_t st291HOffset() const;

                /** @brief ST 291 F-bit: @c true when the packet was
                 *         found on field 2 of an interlaced source;
                 *         @c false on progressive sources or field 1. */
                bool st291FieldB() const;

                /** @brief ST 291 C-bit: @c true for chrominance-data-
                 *         stream packets, @c false for luminance data
                 *         stream (the common case). */
                bool st291CBit() const;

                /** @brief ST 291 stream number (RFC 8331 StreamNum
                 *         field).  Zero for single-link SDI; non-zero
                 *         distinguishes packets on multi-link / 12G
                 *         sub-streams. */
                uint8_t st291StreamNum() const;

                /**
                 * @brief Replaces the logical format.
                 *
                 * Performs CoW detach when the underlying @c Impl is
                 * shared.  Used by parsers that promote a vendor-specific
                 * format ID after decoding an OUI (e.g. promoting
                 * @c VendorInfoFrame to @c HdrDynamic2094_40).
                 */
                void setFormat(const AncFormat &fmt);

                /**
                 * @brief Replaces the wire transport.
                 *
                 * CoW-detaches when shared.  Translators use this on
                 * the freshly produced output packet rather than on the
                 * (immutable) input.
                 */
                void setTransport(const AncTransport &transport);

                /**
                 * @brief Replaces the wire-form bytes.  CoW-detaches when shared.
                 */
                void setData(Buffer data);

                /**
                 * @brief Replaces the per-transport metadata sidecar.
                 *
                 * CoW-detaches when shared.  Pair with the
                 * @ref AncMeta typed key constants from @c ancmeta.h
                 * to set well-known framing fields.
                 */
                void setMeta(Metadata meta);

                /** @brief Sets the ST 291 line number (see @ref st291Line). */
                void setSt291Line(uint16_t line);

                /** @brief Sets the ST 291 horizontal offset (see @ref st291HOffset). */
                void setSt291HOffset(uint16_t hOffset);

                /** @brief Sets the ST 291 F-bit (see @ref st291FieldB). */
                void setSt291FieldB(bool fieldB);

                /** @brief Sets the ST 291 C-bit (see @ref st291CBit). */
                void setSt291CBit(bool cBit);

                /** @brief Sets the ST 291 stream number (see @ref st291StreamNum). */
                void setSt291StreamNum(uint8_t streamNum);

                /**
                 * @brief Sets all five ST 291 framing fields in one
                 *        CoW detach.
                 *
                 * RFC 8331 unpack stamps every field on each freshly
                 * built @c AncPacket; calling the five individual
                 * setters paid five back-to-back @c SharedPtr::modify
                 * calls (each an atomic refcount probe).  This
                 * consolidates them into one mutation, dropping the RX
                 * hot-path per-packet cost noticeably.
                 *
                 * @param line       VANC line number (RFC 8331 §2.2).
                 * @param hOffset    Horizontal offset (RFC 8331 §2.2).
                 * @param fieldB     F-bit (true for field 2).
                 * @param cBit       C-bit (true for chrominance stream).
                 * @param streamNum  Multi-link stream number.
                 */
                void setSt291Framing(uint16_t line, uint16_t hOffset,
                                     bool fieldB, bool cBit, uint8_t streamNum);

                /**
                 * @brief Returns a mutable reference to the wire-form
                 *        bytes after CoW-detaching when shared.
                 *
                 * The caller must not hold the returned reference
                 * across a copy of this @c AncPacket — the underlying
                 * @c Buffer is the only thing detached, so a subsequent
                 * copy of the handle does not invalidate the reference,
                 * but mutating through the reference after another
                 * handle has been mutated could surprise readers.
                 */
                Buffer &dataMut();

                /**
                 * @brief Returns a mutable reference to the metadata
                 *        sidecar after CoW-detaching when shared.
                 *
                 * Same "don't hold across a copy" caveat as @ref dataMut.
                 */
                Metadata &metaMut();

                /**
                 * @brief Returns @c true when both @ref format and
                 *        @ref transport are non-invalid.
                 *
                 * The data and meta fields are not consulted — an
                 * @c AncPacket with empty data is still a valid (but
                 * empty) packet.
                 */
                bool isValid() const;

                /** @brief Equality compares format, transport, data, and meta. */
                bool operator==(const AncPacket &o) const;

                /** @brief Inequality. */
                bool operator!=(const AncPacket &o) const { return !(*this == o); }

                /**
                 * @brief Returns a human-readable diagnostic summary.
                 *
                 * @param verbose When @c false (default), emits a short
                 *                one-line form
                 *                <tt>"AncPacket(Cea708 on St291, 23 bytes, line=11)"</tt>.
                 *                When @c true, also lists every meta
                 *                key/value pair in sorted order.
                 */
                String toString(bool verbose = false) const;

                /**
                 * @brief Private @c Impl struct holding the wrapper's
                 *        state.
                 *
                 * Marked @c PROMEKI_SHARED_FINAL so @c SharedPtr<Impl>
                 * can refcount it natively with CoW semantics.
                 *
                 * The five @c st291* fields hold the ST 291 framing
                 * sidecar directly instead of going through @c meta —
                 * the RTP pack/unpack hot path reads these once per
                 * packet (~9000/sec on HD-60), so the Metadata hash
                 * lookup was paying real cost.  Defaults match the
                 * RFC 8331 §2.2 sentinels for "unspecified" so a
                 * default-constructed packet round-trips cleanly
                 * through the carriage layer.
                 */
                struct Impl {
                                PROMEKI_SHARED_FINAL(Impl)

                                AncFormat    format;
                                AncTransport transport;
                                Buffer       data;
                                Metadata     meta;
                                uint16_t     st291Line = 0x7FE;
                                uint16_t     st291HOffset = 0xFFF;
                                bool         st291FieldB = false;
                                bool         st291CBit = false;
                                uint8_t      st291StreamNum = 0;
                };

        private:
                SharedPtr<Impl> _d;
};

/**
 * @brief Writes an @c AncPacket's four fields (format, transport,
 *        data, meta) onto a stream **without** the outer
 *        @c TypeAncPacket tag.
 *
 * Used internally by the tagged @ref operator<<(DataStream&,
 * const AncPacket&) and by the Variant read dispatcher to consume
 * the payload after the dispatcher has already read the tag.
 * Application code should prefer the tagged operator.
 */
void writeAncPacketData(DataStream &stream, const AncPacket &pkt);

/**
 * @brief Reads an @c AncPacket's four fields from a stream
 *        **without** consuming a leading tag.
 *
 * Companion to @ref writeAncPacketData.  Used by the Variant read
 * dispatcher; application code should prefer the tagged
 * @ref operator>>(DataStream&, AncPacket&).
 */
AncPacket readAncPacketData(DataStream &stream);

/**
 * @brief Writes an @c AncPacket as a @c TypeAncPacket tag followed
 *        by the four fields (each with its own type tag).
 */
DataStream &operator<<(DataStream &stream, const AncPacket &pkt);

/**
 * @brief Reads an @c AncPacket from the inverse of
 *        @ref operator<<(DataStream&, const AncPacket&).
 */
DataStream &operator>>(DataStream &stream, AncPacket &pkt);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV