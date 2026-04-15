/**
 * @file      h264bitstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level H.264 / HEVC NAL-unit framing helpers.
 * @ingroup media
 *
 * Elementary bitstreams for H.264 (ISO/IEC 14496-10) and HEVC
 * (ISO/IEC 23008-2) are expressed as a sequence of NAL units.  Two
 * on-the-wire framings are in common use:
 *
 *  - **Annex-B** byte stream: each NAL unit is preceded by a start
 *    code, either three bytes @c 00 00 01 or four bytes @c 00 00 00 01.
 *    This is the format produced and consumed by most hardware codecs
 *    (NVENC / NVDec), by elementary-stream files, and by RTP depay.
 *
 *  - **AVCC** (a.k.a. length-prefixed): each NAL unit is preceded by a
 *    big-endian byte length of 1, 2, or 4 bytes that gives the NAL
 *    unit's size.  This is the format used inside MP4 / QuickTime
 *    @c mdat boxes, paired with a sample-description extension
 *    (@c avcC or @c hvcC) that declares the length-prefix size.
 *
 * Both formats share the same NAL payload — only the separator
 * differs.  H.264 and HEVC differ in how they interpret the first
 * byte(s) of each NAL unit (NAL type, temporal layer, etc.), but the
 * framing layer does not care: the helpers here work for either
 * codec.  Codec-specific classification (SPS / PPS / VPS / IDR
 * detection) belongs in higher-layer parsers such as
 * @ref AvcDecoderConfig and @ref HevcDecoderConfig.
 *
 * @par Example — convert an Annex-B access unit from an encoder to
 *      MP4-friendly AVCC bytes:
 * @code
 * Buffer::Ptr annexBBytes = encoderOutput();   // 00 00 00 01 | NAL | ...
 * auto [avccBytes, err] = H264Bitstream::annexBToAvcc(
 *         BufferView(annexBBytes, 0, annexBBytes->size()));
 * if(err.isError()) return err;
 * qtWriter.writeSample(trackId, avccBytes);
 * @endcode
 *
 * @par Example — walk NAL units in an Annex-B stream to extract
 *      SPS and PPS payloads:
 * @code
 * H264Bitstream::forEachAnnexBNal(view,
 *         [&](const H264Bitstream::NalUnit &nal) {
 *                 uint8_t h264Type = nal.header0 & 0x1f;
 *                 if(h264Type == 7) spsList.pushToBack(nal.view);
 *                 if(h264Type == 8) ppsList.pushToBack(nal.view);
 *                 return Error::Ok;
 *         });
 * @endcode
 */
class H264Bitstream {
        public:
                /**
                 * @brief Zero-copy view onto one NAL unit within a
                 *        larger buffer.
                 *
                 * @c view covers the NAL payload only — no start code
                 * and no length prefix.  @c header0 is the first byte
                 * of that payload (convenient for H.264, where
                 * @c nal_unit_type = header0 & 0x1f).  @c header1 is
                 * the second byte (meaningful for HEVC, whose NAL
                 * header is two bytes).
                 */
                struct NalUnit {
                        BufferView      view;             ///< Raw NAL payload (no start code / length prefix).
                        uint8_t         header0 = 0;      ///< First byte of the NAL payload.
                        uint8_t         header1 = 0;      ///< Second byte of the NAL payload.
                };

                /** @brief Callback type for NAL-unit iteration.
                 *
                 * Return @ref Error::Ok to continue iterating.  Any
                 * other value stops iteration and is returned as the
                 * final result of the walk.
                 */
                using Visitor = std::function<Error(const NalUnit &)>;

                /**
                 * @brief Iterates every NAL unit in an Annex-B byte
                 *        stream.
                 *
                 * Accepts both 3-byte (@c 00 00 01) and 4-byte
                 * (@c 00 00 00 01) start codes and passes the payload
                 * of each NAL to @p visit as a @ref BufferView that
                 * shares @p in 's backing storage.  Trailing zero
                 * bytes after the last NAL are tolerated.
                 *
                 * @param in       Annex-B bytes to walk.
                 * @param visit    Callback invoked once per NAL unit.
                 * @return @ref Error::Ok on success, or the first
                 *         non-Ok result from @p visit.  Returns
                 *         @ref Error::CorruptData if no start code is
                 *         found anywhere in a non-empty input.
                 */
                static Error forEachAnnexBNal(const BufferView &in, const Visitor &visit);

                /**
                 * @brief Iterates every NAL unit in a length-prefixed
                 *        (AVCC) stream.
                 *
                 * @param in       Length-prefixed bytes to walk.
                 * @param lenSize  Length-prefix size in bytes; must be
                 *                 1, 2, or 4 (the values allowed by the
                 *                 avcC / hvcC @c lengthSizeMinusOne
                 *                 field).
                 * @param visit    Callback invoked once per NAL unit.
                 * @return @ref Error::Ok on success, @ref Error::InvalidArgument
                 *         for an unsupported @p lenSize, @ref Error::CorruptData
                 *         if a length prefix implies a NAL that runs
                 *         past @p in 's end, or the first non-Ok
                 *         result from @p visit.
                 */
                static Error forEachAvccNal(const BufferView &in, uint8_t lenSize,
                                            const Visitor &visit);

                /**
                 * @brief Convert an Annex-B byte stream to AVCC
                 *        (length-prefixed) form.
                 *
                 * The output buffer contains each input NAL payload
                 * preceded by a big-endian length field of @p lenSize
                 * bytes.  The NAL payloads themselves are copied
                 * verbatim (including any emulation-prevention
                 * bytes).
                 *
                 * @param in       Annex-B input.
                 * @param lenSize  Length-prefix size (1, 2, or 4).
                 * @param outBuf   Receives a freshly allocated buffer
                 *                 with the converted bytes.  Its size
                 *                 is set to the total length of the
                 *                 output.
                 * @return @ref Error::Ok, @ref Error::InvalidArgument
                 *         for an unsupported @p lenSize, or
                 *         @ref Error::CorruptData if a NAL payload
                 *         does not fit in @p lenSize bytes.
                 */
                static Error annexBToAvcc(const BufferView &in, uint8_t lenSize,
                                          Buffer::Ptr &outBuf);

                /**
                 * @brief Convert an Annex-B byte stream to AVCC form,
                 *        keeping only NAL units for which @p keep
                 *        returns @c true.
                 *
                 * Intended use is MP4 / QuickTime writers that emit the
                 * @c avc1 / @c hvc1 sample entries, where
                 * ISO/IEC 14496-15 requires parameter sets (SPS, PPS,
                 * and for HEVC VPS) to be carried only in the
                 * @c avcC / @c hvcC configuration record — not inline
                 * in each sample.  The caller supplies a predicate
                 * that returns @c false for the parameter-set NAL
                 * types.
                 *
                 * @param in       Annex-B input.
                 * @param lenSize  Length-prefix size (1, 2, or 4).
                 * @param keep     Predicate invoked once per NAL; NALs
                 *                 for which it returns @c false are
                 *                 dropped from the output.
                 * @param outBuf   Receives a freshly allocated buffer
                 *                 with the converted bytes.
                 * @return @ref Error::Ok, @ref Error::InvalidArgument
                 *         for an unsupported @p lenSize, or
                 *         @ref Error::CorruptData if a NAL payload
                 *         does not fit in @p lenSize bytes.
                 */
                static Error annexBToAvccFiltered(const BufferView &in,
                                                  uint8_t lenSize,
                                                  const std::function<bool(const NalUnit &)> &keep,
                                                  Buffer::Ptr &outBuf);

                /**
                 * @brief Convert an AVCC byte stream to Annex-B form
                 *        with 4-byte start codes.
                 *
                 * @param in       AVCC input.
                 * @param lenSize  Length-prefix size in @p in (1, 2,
                 *                 or 4).
                 * @param outBuf   Receives a freshly allocated buffer
                 *                 with the converted bytes.
                 * @return @ref Error::Ok, @ref Error::InvalidArgument
                 *         for an unsupported @p lenSize, or
                 *         @ref Error::CorruptData if a length prefix
                 *         implies a NAL that runs past the input end.
                 */
                static Error avccToAnnexB(const BufferView &in, uint8_t lenSize,
                                          Buffer::Ptr &outBuf);

                /**
                 * @brief Build an Annex-B byte stream from a list of
                 *        raw NAL payloads.
                 *
                 * Writes a 4-byte start code before each NAL.  Used
                 * when emitting parameter-set blobs (SPS / PPS / VPS)
                 * that were captured out-of-band (e.g. from an @c avcC
                 * record) so that a downstream decoder expecting
                 * Annex-B can ingest them.
                 *
                 * @param nals     Input NAL payloads.  May share
                 *                 backing buffers with other views.
                 * @param outBuf   Receives a freshly allocated buffer
                 *                 with the concatenated Annex-B
                 *                 bytes.  Empty if @p nals is empty.
                 * @return @ref Error::Ok, or an allocation failure.
                 */
                static Error wrapNalsAsAnnexB(const List<BufferView> &nals,
                                              Buffer::Ptr &outBuf);
};

/**
 * @brief AVCDecoderConfigurationRecord — the payload of an @c avcC
 *        box inside an H.264 MP4 / QuickTime sample description.
 * @ingroup media
 *
 * Defined by ISO/IEC 14496-15 §5.3.3.1.2.  Carries the out-of-band
 * parameter sets (SPS, PPS) and profile / level identifiers that an
 * H.264 decoder needs to initialize before it can decode the VCL NAL
 * units stored in the MP4 @c mdat.
 *
 * @par Typical producer flow (writer side)
 * @code
 * // On the first IDR access unit, extract parameter sets and
 * // build the avcC record to place inside the stsd entry.
 * AvcDecoderConfig cfg;
 * H264Bitstream::fromAnnexB(idrAccessUnit, cfg);  // not here; see fromAnnexB
 * Buffer::Ptr avccPayload;
 * cfg.serialize(avccPayload);
 * qtWriter.setTrackAvcC(trackId, avccPayload);
 * @endcode
 *
 * @par Typical consumer flow (reader side)
 * @code
 * // Parse the stsd's avcC blob and re-emit parameter sets as
 * // Annex-B so a hardware decoder expecting Annex-B can ingest
 * // them before the first VCL packet.
 * AvcDecoderConfig cfg;
 * AvcDecoderConfig::parse(avccPayload, cfg);
 * Buffer::Ptr psAnnexB;
 * cfg.toAnnexB(psAnnexB);
 * decoder.submitPacket(MediaPacket(psAnnexB, PixelDesc::H264,
 *                                  MediaPacket::ParameterSet));
 * @endcode
 *
 * @par Extended-profile fields not modeled
 * The record has additional fields (@c chroma_format,
 * @c bit_depth_luma_minus8, @c bit_depth_chroma_minus8, SPS-Ext NALs)
 * that appear only for @c profile_idc values 100, 110, 122, or 144.
 * This implementation does not read or write them; they default to
 * the standard 8-bit 4:2:0 interpretation when absent.  A follow-up
 * can add parsing for high-profile workflows when the codecs need
 * them.
 */
struct AvcDecoderConfig {
        uint8_t                configurationVersion = 1;     ///< Always 1.
        uint8_t                avcProfileIndication = 0;     ///< SPS byte 1 (profile_idc).
        uint8_t                profileCompatibility = 0;     ///< SPS byte 2 (constraint flags + reserved).
        uint8_t                avcLevelIndication   = 0;     ///< SPS byte 3 (level_idc).
        uint8_t                lengthSizeMinusOne   = 3;     ///< NAL length prefix size minus 1; 3 = 4-byte.
        List<Buffer::Ptr>      sps;                          ///< Sequence parameter set NALs (payload only, no start code or length prefix).
        List<Buffer::Ptr>      pps;                          ///< Picture parameter set NALs.

        /**
         * @brief Populate an @c AvcDecoderConfig from an Annex-B
         *        access unit.
         *
         * Walks the input for H.264 SPS (NAL type 7) and PPS (NAL
         * type 8) units, appending each to @c sps / @c pps.
         * Extracts @c avcProfileIndication / @c profileCompatibility
         * / @c avcLevelIndication from bytes 1-3 of the first SPS
         * found.  Parameter sets are stored as freshly allocated
         * Buffer::Ptr copies so the result outlives @p au.
         *
         * @param au   An Annex-B access unit that contains at least
         *             one SPS and one PPS — typically the access
         *             unit of the first IDR frame emitted by the
         *             encoder.
         * @param out  Receives the populated configuration.  On
         *             failure, @p out is left partially populated
         *             (do not use).
         * @return @ref Error::Ok on success,
         *         @ref Error::CorruptData if the input contains no
         *         start codes, or @ref Error::InvalidArgument if
         *         the input has no SPS.
         */
        static Error fromAnnexB(const BufferView &au, AvcDecoderConfig &out);

        /**
         * @brief Parse a serialized @c avcC payload (the bytes
         *        inside an @c avcC box, excluding the box header).
         *
         * @param payload  Bytes of the @c avcC record.
         * @param out      Receives the parsed configuration.
         * @return @ref Error::Ok on success or
         *         @ref Error::CorruptData if the input is
         *         structurally malformed.
         */
        static Error parse(const BufferView &payload, AvcDecoderConfig &out);

        /**
         * @brief Serialize this configuration to an @c avcC payload.
         *
         * The output is the bytes that go inside an @c avcC box (no
         * box header).  Extended high-profile fields are not
         * emitted.
         *
         * @param outBuf  Receives the freshly allocated payload
         *                buffer.
         */
        Error serialize(Buffer::Ptr &outBuf) const;

        /**
         * @brief Concatenate @c sps and @c pps as an Annex-B byte
         *        stream with 4-byte start codes.
         *
         * Useful when feeding parameter sets to a decoder that
         * expects Annex-B (e.g. NVDec) after parsing an @c avcC
         * record out of an MP4 container.
         */
        Error toAnnexB(Buffer::Ptr &outBuf) const;
};

PROMEKI_NAMESPACE_END
