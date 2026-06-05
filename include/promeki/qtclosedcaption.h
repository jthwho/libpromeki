/**
 * @file      qtclosedcaption.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Serializer/deserializer for QuickTime closed-caption (@c c608)
 *        media samples.
 * @ingroup proav
 *
 * A QuickTime CEA-608 caption track (handler @c clcp, sample entry
 * @c c608) stores each frame's line-21 caption bytes as a sequence of
 * sample-data atoms:
 *
 * @code
 * [UInt32 atom_size][FourCC 'cdat'][field-1 byte pairs …]   // CEA-608 field 1
 * [UInt32 atom_size][FourCC 'cdt2'][field-2 byte pairs …]   // CEA-608 field 2 (optional)
 * @endcode
 *
 * Each atom's @c atom_size is @c 8 + 2·N where @c N is the number of
 * byte pairs; the payload is the raw two-byte CEA-608 control/character
 * pairs for that field.  This is the layout used by ffmpeg's MOV
 * muxer/demuxer (@c AV_CODEC_ID_EIA_608), which is the de-facto
 * interoperability reference.
 *
 * The codec works in terms of @ref Cea708Cdp::CcData triples (the same
 * @c {valid, type, b1, b2} representation the rest of the caption stack
 * uses): @c cc_type 0 maps to the @c cdat (field 1) atom and @c cc_type
 * 1 to the @c cdt2 (field 2) atom.  CEA-708 DTVCC triples (@c cc_type
 * 2/3) are not part of a @c c608 sample and are ignored.
 *
 * @see Cea708Cdp, QuickTime, St436m
 */
class QtClosedCaption {
        public:
                /**
                 * @brief Encodes the CEA-608 (cc_type 0/1) entries of @p ccData
                 *        into a QuickTime @c c608 media sample.
                 *
                 * A @c cdat atom is always emitted (empty when no field-1
                 * data is present, matching ffmpeg); a @c cdt2 atom is
                 * emitted only when field-2 data is present.
                 *
                 * @param ccData The cc_data triples for one frame.
                 * @return The @c c608 sample bytes (at least an 8-byte empty
                 *         @c cdat atom).
                 */
                static Buffer encode608(const Cea708Cdp::CcDataList &ccData);

                /**
                 * @brief Decodes a QuickTime @c c608 media sample back into
                 *        CEA-608 cc_data triples.
                 *
                 * @param sample The @c c608 media sample bytes.
                 * @return The decoded @c {valid, type, b1, b2} triples
                 *         (@c cc_type 0 for @c cdat, @c cc_type 1 for
                 *         @c cdt2).
                 */
                static Cea708Cdp::CcDataList decode608(const Buffer &sample);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
