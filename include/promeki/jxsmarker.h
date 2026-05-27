/**
 * @file      jxsmarker.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstddef>
#include <cstdint>
#include <promeki/list.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief ISO/IEC 21122-1 JPEG XS codestream marker walker.
 * @ingroup network
 *
 * Standalone parser for the marker structure of a JPEG XS
 * codestream — locates the @em main header range and the byte
 * offsets of every slice so the RFC 9134 §4.2 slice-mode (K=1)
 * packetizer can group complete slices into MTU-sized RTP packets.
 *
 * @par Codestream structure
 *
 * ISO 21122-1 §A.1 defines the codestream as:
 *  - @c SOC (0xFF10) — start of codestream marker.  No length field.
 *  - Main header: a sequence of marker segments (CAP, PIH, CDT,
 *    WGT, optionally COM / NLT / CWD / CTS / CRG).  Every main-
 *    header marker carries a 2-byte big-endian length field
 *    immediately after the 2-byte marker code; the length value
 *    includes the length-field bytes themselves but @em not the
 *    marker code.
 *  - One or more slices: each begins with an @c SLH (0xFF20)
 *    marker segment, immediately followed by bit-packed
 *    coefficient data extending up to the next @c SLH or @c EOC.
 *  - @c EOC (0xFF11) — end of codestream marker.  No length
 *    field.
 *
 * Per ISO 21122-1 §A.4, 0xFF bytes in slice coefficient data are
 * reserved for marker prefix — the encoder shall not produce a
 * stray 0xFF in slice data.  Scanning forward for the next 0xFF
 * is therefore a reliable boundary detector once a slice's SLH
 * marker has been consumed.
 *
 * @par Use case
 *
 * The library's RFC 9134 packetizer (@ref RtpPayloadJpegXs) uses
 * this walker only in slice mode (K=1).  Codestream mode (K=0)
 * does not need slice boundaries; it just fragments the buffer
 * blindly into MTU-sized chunks.
 *
 * @par Thread Safety
 *
 * Stateless free-standing parse function — concurrent calls are
 * safe.  The returned @ref ParseResult holds only offsets +
 * sizes, never pointers into the input buffer, so the caller can
 * release the input as soon as @ref parse returns.
 */
class JxsMarker {
        public:
                /// @brief JPEG XS marker codes used by the walker.
                ///        The full ISO 21122-1 marker set is larger;
                ///        these are the ones whose boundaries the
                ///        slice-mode packetizer needs to find.
                enum Code : uint16_t {
                        Soc = 0xFF10, ///< Start of codestream.
                        Eoc = 0xFF11, ///< End of codestream.
                        Cap = 0xFF50, ///< Capabilities marker (length-prefixed).
                        Pih = 0xFF12, ///< Picture header (length-prefixed).
                        Cdt = 0xFF13, ///< Component table (length-prefixed).
                        Wgt = 0xFF14, ///< Weights table (length-prefixed).
                        Com = 0xFF15, ///< Extension marker (length-prefixed).
                        Nlt = 0xFF16, ///< Nonlinearity (length-prefixed).
                        Cwd = 0xFF17, ///< Component wavelet decomposition.
                        Cts = 0xFF18, ///< Colour transformation.
                        Crg = 0xFF19, ///< Component registration.
                        Slh = 0xFF20, ///< Slice header (length-prefixed).
                };

                /// @brief Byte range of a single JPEG XS slice in the
                ///        input codestream.
                ///
                /// @c offset is the byte index of the slice's @c SLH
                /// marker (first byte of @c 0xFF20); @c size is the
                /// total byte count from that offset through the
                /// last byte of the slice's coefficient data (i.e.
                /// up to but not including the next @c SLH / @c EOC).
                struct SliceRange {
                                /// @brief Byte offset of the SLH marker that opens the slice.
                                size_t offset = 0;

                                /// @brief Total byte size of the slice
                                ///        (SLH marker + its payload +
                                ///        coefficient data).
                                size_t size = 0;
                };

                /// @brief Result of @ref parse — codestream layout the
                ///        slice-mode packetizer needs.
                struct ParseResult {
                                /// @brief @c true when @ref parse succeeded
                                ///        and the codestream is well-formed.
                                bool valid = false;

                                /// @brief Byte size of the main header
                                ///        (from offset 0, ending at the
                                ///        first @c SLH marker's offset).
                                ///
                                ///        The slice-mode packetizer
                                ///        prepends this range to the
                                ///        first emitted RTP packet so
                                ///        a decoder sees the picture
                                ///        header before any slice.
                                size_t mainHeaderSize = 0;

                                /// @brief Per-slice byte ranges (in
                                ///        codestream order).  Empty
                                ///        when the codestream contains
                                ///        no @c SLH markers.
                                List<SliceRange> slices;

                                /// @brief Byte offset of the @c EOC
                                ///        marker (first byte of
                                ///        @c 0xFF11), or 0 when the
                                ///        codestream did not terminate
                                ///        with @c EOC.
                                size_t eocOffset = 0;
                };

                /**
                 * @brief Parses a JPEG XS codestream's marker structure.
                 *
                 * Walks the main-header marker segments by their
                 * length fields, then scans slice coefficient data
                 * for the next @c SLH / @c EOC marker prefix.
                 *
                 * @param data  Pointer to the codestream's first byte
                 *              (an @c SOC marker).
                 * @param size  Codestream byte size.
                 * @return A @ref ParseResult with @c valid == @c true
                 *         on success.  Malformed inputs (missing
                 *         @c SOC, truncated marker length, no @c SLH
                 *         markers between the main header and
                 *         @c EOC) yield @c valid == @c false with
                 *         the partial result populated for
                 *         diagnostic inspection.
                 */
                static ParseResult parse(const void *data, size_t size);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
