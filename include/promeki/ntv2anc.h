/**
 * @file      ntv2anc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <cstddef>
#include <cstdint>
#include <promeki/ancpayload.h>
#include <promeki/error.h>
#include <promeki/framerate.h>
#include <promeki/namespace.h>
#include <promeki/size2d.h>

PROMEKI_NAMESPACE_BEGIN

class VideoScanMode;

/**
 * @brief Converters between AJA's hardware ANC buffer format (GUMP)
 *        and libpromeki's @ref AncPayload + @ref AncPacket types.
 * @ingroup proav
 *
 * AutoCirculate captures and emits ancillary data as a pair of host
 * byte buffers (one per field), each laid out in AJA's "GUMP" wire
 * format.  These helpers translate to / from the canonical
 * @ref AncTransport::St291 packet form that the rest of libpromeki's
 * ANC stack (codecs, RTP packetiser, AncPayload pipeline stages) uses,
 * letting the NTV2 backend slot into the same ANC contract as the
 * generic ANC layer.
 *
 * Capture (@ref ntv2AncToPackets):
 *  - Parses the F1 / F2 GUMP buffers via
 *    @c AJAAncillaryList::AddFromDeviceAncBuffer (so the F1 vs F2
 *    origin survives as the @c AncPacket::st291FieldB() flag).
 *  - Translates each AJA packet's DID/SDID/UDW/location into an
 *    @ref St291Packet using @ref St291Packet::buildRaw (which packs the
 *    10-bit wire bytes and computes parity for us).
 *  - Looks up the @ref AncFormat via
 *    @ref AncFormat::fromSt291DidSdid — unknown DID/SDID pairs are
 *    still carried verbatim with @c AncFormat::Invalid (wire fidelity
 *    is preserved).
 *
 * Insertion (@ref packetsToNtv2Anc):
 *  - Promotes each @c AncTransport::St291 packet on the payload back
 *    to an AJA @c AJAAncillaryData object with the recorded DID/SDID,
 *    8-bit payload bytes, line/horiz-offset/field/channel/stream
 *    location, and appends to a fresh @c AJAAncillaryList.
 *  - Calls @c AJAAncillaryList::GetTransmitData to encode the GUMP
 *    F1 / F2 buffers ready for @c AUTOCIRCULATE_TRANSFER::SetAncBuffers.
 *  - Warns (does not error) on out-of-range lines so a partial frame
 *    of valid ANC still ships.
 *
 * Both directions are pure conversion — no SDK device handle is
 * required.  All hardware-free, all unit-testable.
 */
namespace Ntv2Anc {

        /**
         * @brief Decode an AJA F1/F2 GUMP-format ANC buffer pair into
         *        an @ref AncPayload.
         *
         * @param f1Buf      Pointer to the F1 GUMP buffer (may be
         *                   @c nullptr if @p f1Bytes is zero).
         * @param f1Bytes    Length of the F1 buffer.
         * @param f2Buf      Pointer to the F2 GUMP buffer (may be
         *                   @c nullptr or zero-length on progressive
         *                   sources).
         * @param f2Bytes    Length of the F2 buffer.
         * @param raster     Source raster for the @ref AncDesc binding.
         * @param scanMode   Source scan mode for the @ref AncDesc binding.
         * @param frameRate  Frame rate for the @ref AncDesc binding.
         * @return A freshly-built @ref AncPayload::Ptr.  The payload's
         *         packet list may be empty when both buffers parsed to
         *         zero packets.
         */
        AncPayload::Ptr ntv2AncToPackets(const uint8_t *f1Buf, size_t f1Bytes,
                                         const uint8_t *f2Buf, size_t f2Bytes,
                                         const Size2Du32 &raster,
                                         const VideoScanMode &scanMode,
                                         const FrameRate &frameRate);

        /**
         * @brief Encode an @ref AncPayload's @c St291 packets into
         *        AJA GUMP-format F1 / F2 buffers.
         *
         * Caller pre-allocates @p f1Buf / @p f2Buf — AJA recommends
         * 2048-byte pages per field for non-HANC use; @ref kPreferredBufBytes
         * matches that.  The buffers are zero-padded to capacity by
         * @c AJAAncillaryList::GetTransmitData; the function does not
         * resize them.
         *
         * @param payload          The payload whose @c St291 packets
         *                         to encode.  Non-St291 packets on the
         *                         payload are ignored.
         * @param f1Buf            Field-1 destination buffer
         *                         (8-byte-aligned).
         * @param f1BytesCapacity  Capacity of @p f1Buf in bytes.
         * @param f2Buf            Field-2 destination buffer (may be
         *                         @c nullptr on progressive sources).
         * @param f2BytesCapacity  Capacity of @p f2Buf in bytes.
         * @param isProgressive    @c true to skip Field 2 entirely.
         * @param f2StartLine      For interlaced sources, the SMPTE
         *                         line number where Field 2 begins
         *                         (used by the encoder to decide
         *                         which packets go to F2).  Ignored
         *                         on progressive sources.
         * @return @c Error::Ok on success, @c Error::Invalid on a
         *         buffer-too-small failure from the AJA encoder.
         */
        Error packetsToNtv2Anc(const AncPayload &payload, uint8_t *f1Buf, size_t f1BytesCapacity, uint8_t *f2Buf,
                               size_t f2BytesCapacity, bool isProgressive, uint16_t f2StartLine);

        /**
         * @brief Preferred ANC buffer size per field.
         *
         * Matches AJA's "2048-byte per field" recommendation in the
         * @c SetAncBuffers Doxygen.  Sufficient for typical VANC
         * payloads (CEA-708, AFD, ATC, HDR metadata).  For HANC
         * capture or very wide CDPs the caller can over-allocate.
         */
        constexpr size_t kPreferredBufBytes = 2048;

} // namespace Ntv2Anc

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
