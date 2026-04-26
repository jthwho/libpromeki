/**
 * @file      hevcbitstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/h264bitstream.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief HEVCDecoderConfigurationRecord — the payload of an @c hvcC
 *        box inside an H.265 / HEVC MP4 / QuickTime sample description.
 * @ingroup media
 *
 * Defined by ISO/IEC 14496-15 §8.3.3.1.2.  Serves the same role for
 * HEVC as @ref AvcDecoderConfig does for H.264: it carries the
 * out-of-band parameter sets (VPS, SPS, PPS) and enough profile /
 * tier / level / chroma information to let a decoder be initialized
 * before any VCL NAL units are delivered.
 *
 * @par NAL unit framing
 * HEVC's NAL header is two bytes and the @c nal_unit_type lives in
 * bits 1-6 of the first byte:
 * @code
 * nal_unit_type = (header0 >> 1) & 0x3f;
 * @endcode
 * The types this module cares about are:
 *  - 32 — VPS (video parameter set)
 *  - 33 — SPS (sequence parameter set)
 *  - 34 — PPS (picture parameter set)
 *  - 19 — IDR_W_RADL (IDR with trailing RADL)
 *  - 20 — IDR_N_LP (IDR with no leading pictures)
 *
 * @par Profile/tier/level extraction (MVP)
 * @ref fromAnnexB extracts @c general_profile_space,
 * @c general_tier_flag, @c general_profile_idc,
 * @c general_profile_compatibility_flags,
 * @c general_constraint_indicator_flags, and @c general_level_idc
 * from the first SPS found, at known fixed byte offsets after the
 * SPS's 2-byte NAL header and 1-byte
 * (sps_vps_id | sps_max_sub_layers_minus1 | temporal_id_nesting_flag)
 * byte.  These 12 bytes are the same in every HEVC SPS.
 *
 * Fields that require full SPS bit-level parsing (@c chromaFormat,
 * @c bitDepthLumaMinus8, @c bitDepthChromaMinus8,
 * @c min_spatial_segmentation_idc, @c parallelismType,
 * @c avgFrameRate, @c constantFrameRate) default to standard 8-bit
 * 4:2:0 values that most players accept.  A follow-up pass can
 * extract them from the SPS RBSP when higher-profile streams need
 * them.
 */
struct HevcDecoderConfig {
                uint8_t           configurationVersion = 1;             ///< Always 1.
                uint8_t           generalProfileSpace = 0;              ///< 2 bits.
                uint8_t           generalTierFlag = 0;                  ///< 1 bit.
                uint8_t           generalProfileIdc = 0;                ///< 5 bits.
                uint32_t          generalProfileCompatibilityFlags = 0; ///< 32 bits.
                uint64_t          generalConstraintIndicatorFlags = 0;  ///< 48 bits, stored in low 48.
                uint8_t           generalLevelIdc = 0;                  ///< 8 bits.
                uint16_t          minSpatialSegmentationIdc = 0;        ///< 12 bits.
                uint8_t           parallelismType = 0;                  ///< 2 bits; 0 = unknown / mixed.
                uint8_t           chromaFormat = 1;                     ///< 2 bits; 1 = 4:2:0.
                uint8_t           bitDepthLumaMinus8 = 0;               ///< 3 bits.
                uint8_t           bitDepthChromaMinus8 = 0;             ///< 3 bits.
                uint16_t          avgFrameRate = 0;                     ///< 16 bits; 0 = unknown.
                uint8_t           constantFrameRate = 0;                ///< 2 bits; 0 = unknown.
                uint8_t           numTemporalLayers = 1;                ///< 3 bits; at least 1.
                uint8_t           temporalIdNested = 0;                 ///< 1 bit.
                uint8_t           lengthSizeMinusOne = 3;               ///< 2 bits; 3 = 4-byte prefix.
                List<Buffer::Ptr> vps; ///< Video parameter set NAL payloads (no start code, no length prefix).
                List<Buffer::Ptr> sps; ///< Sequence parameter set NAL payloads.
                List<Buffer::Ptr> pps; ///< Picture parameter set NAL payloads.

                /**
         * @brief Populate an @c HevcDecoderConfig from an Annex-B
         *        access unit.
         *
         * Walks @p au for HEVC VPS (NAL type 32), SPS (NAL type 33),
         * and PPS (NAL type 34) units.  Extracts profile / tier /
         * level information from the first SPS's
         * @c profile_tier_level structure.
         *
         * @param au   An Annex-B access unit containing at least one
         *             SPS.
         * @param out  Receives the populated configuration.
         * @return @ref Error::Ok on success,
         *         @ref Error::CorruptData if the input contains no
         *         start codes, or @ref Error::InvalidArgument if
         *         no SPS is present or the SPS is too short to
         *         contain the profile_tier_level fixed fields.
         */
                static Error fromAnnexB(const BufferView &au, HevcDecoderConfig &out);

                /**
         * @brief Parse a serialized @c hvcC payload.
         *
         * @param payload  Bytes of the @c hvcC record (no box header).
         * @param out      Receives the parsed configuration.
         */
                static Error parse(const BufferView &payload, HevcDecoderConfig &out);

                /** @brief Serialize this configuration to an @c hvcC payload. */
                Error serialize(Buffer::Ptr &outBuf) const;

                /**
         * @brief Concatenate @c vps, @c sps, and @c pps as an
         *        Annex-B byte stream with 4-byte start codes, in
         *        that order.
         */
                Error toAnnexB(Buffer::Ptr &outBuf) const;
};

PROMEKI_NAMESPACE_END
