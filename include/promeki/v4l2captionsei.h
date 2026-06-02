/**
 * @file      v4l2captionsei.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_V4L2
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Software SEI injection / extraction for V4L2 hardware codecs.
 * @ingroup proav
 *
 * V4L2 has no control for injecting arbitrary user-data SEI (closed
 * captions), and a hardware decoder discards the SEI it doesn't act on.
 * These helpers do the bitstream surgery in software around the codec:
 * the encoder splices a caption SEI NAL into the coded access unit, and
 * the decoder parses caption SEI out of the input bitstream.  The SEI
 * @em payload bodies are built / consumed by the existing
 * @ref VideoEncoderSei and @ref AncTranslator machinery; this module only
 * handles the Annex-B NAL framing (FF-coded SEI message header,
 * emulation-prevention, and placement before the first VCL NAL).
 *
 * Both H.264 (SEI @c nal_unit_type 6) and HEVC (prefix-SEI
 * @c nal_unit_type 39) are supported, selected by the @p hevc flag.
 */

/**
 * @brief Builds one complete Annex-B SEI NAL from a payload body.
 *
 * Wraps @p payloadBody (the SEI message payload, without header or
 * emulation-prevention) as @c nal_header + @c sei_message (FF-coded
 * @p payloadType + size + body) + @c rbsp_trailing_bits, then applies
 * emulation-prevention.  Returns the raw NAL bytes (no start code).
 */
Buffer v4l2BuildSeiNal(int payloadType, const BufferView &payloadBody, bool hevc);

/**
 * @brief Splices SEI NALs into a coded access unit before its first VCL NAL.
 *
 * @param      codedIn The codec's Annex-B output access unit.
 * @param      seiNals Complete SEI NALs from @ref v4l2BuildSeiNal.
 * @param      hevc    HEVC (true) vs H.264 (false) NAL-type semantics.
 * @param[out] out     The rewritten Annex-B access unit.
 * @return @c Error::Ok (a parse failure copies @p codedIn through unchanged).
 */
Error v4l2InjectSeiNals(const BufferView &codedIn, const List<Buffer> &seiNals, bool hevc, Buffer &out);

/**
 * @brief Extracts SEI payload bodies of a given type from a coded access unit.
 *
 * Walks the SEI NALs, removes emulation-prevention, parses each
 * @c sei_message, and returns the bodies whose @c payloadType matches
 * @p payloadType (e.g. 4 = user_data_registered for captions).
 */
List<Buffer> v4l2ExtractSeiPayloads(const BufferView &codedIn, int payloadType, bool hevc);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
