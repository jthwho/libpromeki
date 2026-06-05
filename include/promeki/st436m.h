/**
 * @file      st436m.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/result.h>
#include <promeki/ancpacket.h>
#include <promeki/ancdesc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Serializer/deserializer for the SMPTE ST 436M-2006 ANC data
 *        sample format.
 * @ingroup proav
 *
 * ST 436M defines an opaque carriage for SMPTE ST 291 ancillary-data
 * packets (HANC and VANC).  Although the standard frames its
 * structures with MXF KLV wrappers, the @em ANC @em Frame @em Element
 * @em Value (everything after the KLV key + length — see ST 436M-2006
 * §6.1, Table 7 and Figure 7) is exactly the byte layout that the
 * QuickTime / ISO-BMFF @c vanc data track stores in one media sample
 * per video frame.  This class produces and consumes that value:
 *
 * @code
 * Number of ANC packets (N)        : UInt16 (big-endian)
 * repeated N times:
 *   Line Number                    : UInt16   (SMPTE 377M E.1.5)
 *   Wrapping Type                  : UInt8    (0x01 VANC Frame … 0x04 VANC Progressive)
 *   Payload Sample Coding          : UInt8    (4 = 8-bit luma … 7 = 10-bit luma)
 *   Payload Sample Count           : UInt16   (number of coded samples)
 *   ANC Payload Byte Array         : UInt8[…] (DID, SDID/DBN, DC, UDW…, [CS]; padded to UInt32)
 * @endcode
 *
 * The encoder emits the 10-bit "pass-through" sample coding
 * (ST 436M-2006 §6.2): the ST 291 packet's 10-bit words — already the
 * canonical post-ADF form held by @ref AncPacket::data — are repacked
 * three-per-UInt32 (§4.4.4), so parity and checksum survive byte-exact.
 * The decoder accepts both the 8-bit codings (4/5/6 and the 10/11/12
 * parity-error variants) and the 10-bit codings (7/8/9) so it can read
 * ST 436M samples written by other tools.
 *
 * @see AncPacket, AncDesc, AncPayload, QuickTime
 */
class St436m {
        public:
                /** @brief Wrapping-type values (ST 436M-2006 Table 7). */
                enum WrappingType : uint8_t {
                        VancFrame       = 0x01, ///< VANC, interlaced or segmented-progressive frame.
                        VancField1      = 0x02, ///< VANC, field 1.
                        VancField2      = 0x03, ///< VANC, field 2.
                        VancProgressive = 0x04, ///< VANC, progressive frame.
                        HancFrame       = 0x11, ///< HANC, interlaced or segmented-progressive frame.
                        HancField1      = 0x12, ///< HANC, field 1.
                        HancField2      = 0x13, ///< HANC, field 2.
                        HancProgressive = 0x14, ///< HANC, progressive frame.
                };

                /** @brief Payload sample-coding values (ST 436M-2006 Table 7). */
                enum SampleCoding : uint8_t {
                        Coding8BitLuma           = 4,  ///< 8-bit luma component samples.
                        Coding8BitChroma         = 5,  ///< 8-bit color-difference samples.
                        Coding8BitLumaChroma     = 6,  ///< 8-bit luma + color-difference samples.
                        Coding10BitLuma          = 7,  ///< 10-bit luma component samples (pass-through).
                        Coding10BitChroma        = 8,  ///< 10-bit color-difference samples (pass-through).
                        Coding10BitLumaChroma    = 9,  ///< 10-bit luma + color-difference samples.
                        Coding8BitLumaParityErr  = 10, ///< 8-bit luma, known parity error.
                        Coding8BitChromaParityErr= 11, ///< 8-bit color difference, known parity error.
                        Coding8BitLCParityErr    = 12, ///< 8-bit luma + chroma, known parity error.
                };

                /**
                 * @brief Encodes one frame's ST 291 ancillary packets into
                 *        an ST 436M ANC Frame Element Value.
                 *
                 * Only packets whose @ref AncPacket::transport is
                 * @c AncTransport::St291 are encoded — ST 436M carries
                 * SDI ANC packets, so non-ST-291 transports (HDMI
                 * InfoFrame, NDI XML, …) are skipped.  The wrapping type
                 * is derived from @p desc 's scan mode and each packet's
                 * F-bit.
                 *
                 * @param packets The frame's ANC packets.
                 * @param desc    The stream descriptor (scan mode informs
                 *                 the wrapping type); may be invalid.
                 * @return The ANC Frame Element Value bytes.  An empty
                 *         (zero-packet) value is still a valid sample.
                 */
                static Buffer encodeFrame(const AncPacket::List &packets, const AncDesc &desc = AncDesc());

                /**
                 * @brief Decodes an ST 436M ANC Frame Element Value back
                 *        into ST 291 ancillary packets.
                 *
                 * @param sample The ANC Frame Element Value bytes (one
                 *               QuickTime @c vanc media sample).
                 * @return The decoded packet list, or @c Error::Corrupt
                 *         if the byte layout is malformed.
                 */
                static Result<AncPacket::List> decodeFrame(const Buffer &sample);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
