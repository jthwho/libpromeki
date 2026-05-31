/**
 * @file      mpegts.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/error.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level MPEG-2 Transport Stream (ITU-T H.222.0 / ISO/IEC 13818-1)
 *        framing primitives.
 * @ingroup proav
 *
 * The Transport Stream wraps elementary streams (video, audio,
 * metadata) into fixed-size 188-byte packets, each tagged with a
 * 13-bit Packet Identifier (PID).  Program Specific Information (PSI)
 * tables (PAT, PMT) describe which PIDs carry which elementary
 * streams; Packetized Elementary Stream (PES) headers carry per-access-
 * unit timing (PTS, DTS); the Program Clock Reference (PCR) embedded
 * in a chosen PID's adaptation field synchronises the decoder's clock
 * to the encoder's.
 *
 * This class is the bottom layer of the project's TS support: pure
 * static helpers that build / inspect the wire-level fields and have
 * no state of their own.  Higher layers add stream management
 * (@ref MpegTsMuxer) and I/O (@ref MpegTsFileMediaIO).
 *
 * @par References
 *  - ITU-T H.222.0 (12/2021) / ISO/IEC 13818-1 — the TS specification.
 *  - ETSI TR 101 290 — recommended interval guidelines (PAT/PMT, PCR).
 *  - ISO/IEC 13818-1 §2.4.4 — PSI table syntax and CRC32 polynomial.
 *  - ISO/IEC 13818-1 §2.4.3.7 — PES packet syntax (PTS/DTS encoding).
 *
 * @par Thread Safety
 * All members are pure functions of their arguments and are safe to
 * call concurrently from any thread.
 */
class MpegTs {
        public:
                /** @brief Fixed TS packet size in bytes. */
                static constexpr int PacketSize = 188;

                /** @brief Mandatory first byte of every TS packet. */
                static constexpr uint8_t SyncByte = 0x47;

                /** @brief Maximum PID value (13-bit field). */
                static constexpr uint16_t MaxPid = 0x1FFF;

                /** @brief Reserved PID for the Program Association Table. */
                static constexpr uint16_t PidPat = 0x0000;

                /** @brief Reserved PID for the Conditional Access Table. */
                static constexpr uint16_t PidCat = 0x0001;

                /** @brief Reserved PID for the Transport Stream Description Table. */
                static constexpr uint16_t PidTsdt = 0x0002;

                /** @brief Reserved PID for the IPMP Information Table. */
                static constexpr uint16_t PidIpmp = 0x0003;

                /** @brief Reserved null-packet PID. */
                static constexpr uint16_t PidNull = 0x1FFF;

                /** @brief Default PID for the program's PMT. */
                static constexpr uint16_t DefaultPmtPid = 0x1000;

                /** @brief Default PID for the program's first video stream. */
                static constexpr uint16_t DefaultVideoPid = 0x0100;

                /** @brief Default PID for the program's first audio stream. */
                static constexpr uint16_t DefaultAudioPid = 0x0101;

                /** @brief Default program number for a single-program TS. */
                static constexpr uint16_t DefaultProgramNumber = 1;

                /** @brief PES packet start-code prefix (24-bit). */
                static constexpr uint32_t PesStartCode = 0x000001;

                /**
                 * @brief PSI / PES @c stream_type values for the codecs
                 *        this implementation supports.
                 *
                 * Drawn from ISO/IEC 13818-1 Table 2-34 (canonical
                 * MPEG-2 Systems values) plus ATSC A/53 for AC-3 in
                 * MPEG-TS contexts.  Values not listed here are
                 * permitted on the wire (the PMT carries an 8-bit
                 * @c stream_type field) but are not produced by this
                 * library's muxer; the demuxer reports unknown values
                 * verbatim.
                 */
                enum StreamType : uint8_t {
                        StreamTypeReserved = 0x00, ///< Reserved / invalid.
                        StreamTypeMpeg1Video = 0x01, ///< ISO/IEC 11172-2 video (MPEG-1).
                        StreamTypeMpeg2Video = 0x02, ///< ITU-T H.262 / ISO/IEC 13818-2 video.
                        StreamTypeMpeg1Audio = 0x03, ///< ISO/IEC 11172-3 audio (MP1/MP2).
                        StreamTypeMpeg2Audio = 0x04, ///< ISO/IEC 13818-3 audio (MP2).
                        /**
                         * @brief PES packets containing private data
                         *        (ISO/IEC 13818-1 Table 2-34).
                         *
                         * Overloaded by several modern codecs that all
                         * share this @c stream_type and disambiguate via
                         * a PMT @c registration_descriptor: SMPTE 302M
                         * uncompressed PCM (@c BSSD), Opus
                         * (@c Opus, per Opus-in-MPEG-TS draft), and
                         * AV1 (@c AV01, per AOMedia AV1 MPEG-2 TS
                         * mapping).  Use @ref RegFormatSmpte302M /
                         * @ref RegFormatOpus / @ref RegFormatAv1 in the
                         * descriptor to identify the underlying codec.
                         */
                        StreamTypePrivatePes = 0x06,
                        StreamTypeAacAdts = 0x0F, ///< ISO/IEC 13818-7 AAC in ADTS framing.
                        StreamTypeAacLatm = 0x11, ///< ISO/IEC 14496-3 AAC in LOAS/LATM framing.
                        StreamTypeMetadata = 0x15, ///< ISO/IEC 13818-1 metadata in PES (used for KLV by SMPTE RP 217).
                        StreamTypeH264 = 0x1B, ///< ITU-T H.264 / ISO/IEC 14496-10 video.
                        StreamTypeHevc = 0x24, ///< ITU-T H.265 / ISO/IEC 23008-2 video.
                        /**
                         * @brief JPEG XS video — ISO/IEC 13818-1 AMD 3
                         *        / SMPTE ST 2049-1 mapping.
                         *
                         * Paired with a @c JXSV registration descriptor
                         * in the PMT's @c ES_info loop
                         * (@ref RegFormatJpegXs).
                         */
                        StreamTypeJpegXs = 0x32,
                        StreamTypeAc3 = 0x81, ///< Dolby AC-3 (ATSC A/53 Annex B).
                        StreamTypeScte35 = 0x86, ///< SCTE-35 splice information.
                };

                /**
                 * @brief Coarse classification of a PMT stream entry.
                 *
                 * MPEG-TS @c stream_type values are mostly self-classifying
                 * — @ref StreamTypeH264 is obviously video, @ref StreamTypeAacAdts
                 * is obviously audio.  Modern codecs ride on
                 * @ref StreamTypePrivatePes (@c 0x06), which is ambiguous;
                 * the caller resolves the ambiguity by passing one of
                 * these enumerators to @ref MpegTsMuxer::addStream so
                 * the muxer can pick the right PES @c stream_id range
                 * (video uses @c 0xE0+, audio uses @c 0xBD / private_stream_1).
                 */
                enum class StreamKind : uint8_t {
                        Auto = 0,    ///< Derive kind from @ref StreamType.
                        Video = 1,   ///< Video elementary stream.
                        Audio = 2,   ///< Audio elementary stream.
                        Data = 3,    ///< Private data (e.g. SCTE-35 splice info).
                        Metadata = 4 ///< Metadata stream (KLV, ID3, etc.).
                };

                /**
                 * @brief Registration descriptor @c format_identifier
                 *        values for codecs this implementation maps.
                 *
                 * The @c registration_descriptor (ISO/IEC 13818-1 §2.6.8,
                 * descriptor tag @c 0x05) carries a 32-bit
                 * @c format_identifier — four printable ASCII bytes,
                 * big-endian — that disambiguates @c stream_type
                 * @c 0x06 entries on the PMT loop.
                 */
                static constexpr uint32_t RegFormatSmpte302M = 0x42535344; ///< 'BSSD' — SMPTE 302M.
                static constexpr uint32_t RegFormatOpus      = 0x4F707573; ///< 'Opus' — Opus in MPEG-TS.
                static constexpr uint32_t RegFormatAv1       = 0x41563031; ///< 'AV01' — AV1 in MPEG-TS.
                static constexpr uint32_t RegFormatJpegXs    = 0x4A585356; ///< 'JXSV' — JPEG XS per AMD 3.

                /** @brief Descriptor-tag value of @c registration_descriptor. */
                static constexpr uint8_t DescriptorTagRegistration = 0x05;

                /**
                 * @brief PES @c stream_id values used for the codecs
                 *        this implementation supports.
                 *
                 * PES headers carry an 8-bit @c stream_id that the
                 * spec splits into a generic "video stream"
                 * (0xE0-0xEF), "audio stream" (0xC0-0xDF), and a set
                 * of reserved single-purpose IDs.  Most TS muxers
                 * pick a per-stream index inside the appropriate
                 * range and stick with it.
                 */
                enum PesStreamId : uint8_t {
                        PesStreamIdVideoFirst = 0xE0, ///< First video @c stream_id.
                        PesStreamIdAudioFirst = 0xC0, ///< First audio @c stream_id.
                        PesStreamIdPrivate1 = 0xBD, ///< Private stream 1 (AC-3, DTS, LPCM in MPEG-2).
                        PesStreamIdPadding = 0xBE, ///< Padding stream.
                        PesStreamIdPrivate2 = 0xBF, ///< Private stream 2.
                        PesStreamIdMetadata = 0xFC, ///< Metadata stream.
                };

                /**
                 * @brief One-shot PSI CRC32 over @p len bytes at @p data.
                 *
                 * MPEG PSI tables use CRC-32/MPEG-2 (poly @c 0x04C11DB7,
                 * init @c 0xFFFFFFFF, no reflection, no final XOR).
                 * Defined in ISO/IEC 13818-1 §2.4.4.10 Annex A.
                 *
                 * @param data Pointer to the bytes to check.
                 * @param len  Number of bytes.
                 * @return Big-endian-encodable CRC32 value.
                 */
                static uint32_t psiCrc32(const void *data, size_t len);

                /** @brief @c BufferView overload of @ref psiCrc32. */
                static uint32_t psiCrc32(const BufferView &v);

                /**
                 * @brief Encodes a 33-bit timestamp into the 5-byte
                 *        PES PTS / DTS field.
                 *
                 * The PES PTS / DTS field is a 5-byte big-endian
                 * group with a 4-bit prefix tag, the 33-bit value
                 * interleaved across three 15-bit sub-fields, and
                 * three reserved marker bits forced to 1
                 * (ISO/IEC 13818-1 §2.4.3.7).  The @p prefix argument
                 * is the 4-bit tag that goes in the top nibble — the
                 * caller uses @c 0x2 for PTS-only ('0010'), @c 0x3 for
                 * the PTS half of a PTS+DTS pair, and @c 0x1 for the
                 * DTS half ('0001').
                 *
                 * @param value90k The 33-bit timestamp in 90 kHz ticks.
                 * @param prefix   4-bit prefix tag (@c 0x1 / @c 0x2 / @c 0x3).
                 * @param out      Receives 5 bytes.
                 */
                static void encodePesPts(uint64_t value90k, uint8_t prefix, uint8_t out[5]);

                /**
                 * @brief Decodes a 5-byte PES PTS / DTS field back to
                 *        a 33-bit timestamp.
                 *
                 * Inverse of @ref encodePesPts.  Does not validate the
                 * prefix or marker bits.
                 *
                 * @param in   Pointer to 5 bytes of PES PTS / DTS data.
                 * @return The 33-bit value in 90 kHz ticks.
                 */
                static uint64_t decodePesPts(const uint8_t in[5]);

                /**
                 * @brief Encodes a 42-bit PCR value into the 6-byte
                 *        adaptation-field PCR group.
                 *
                 * The PCR group is a 33-bit base (90 kHz, identical
                 * to PTS units) plus a 9-bit extension (27 MHz minus
                 * the base × 300), separated by 6 reserved bits forced
                 * to 1 (ISO/IEC 13818-1 §2.4.3.5).  The standard
                 * convention is @c base = pcr27mhz / 300 and
                 * @c ext = pcr27mhz % 300; this helper takes the full
                 * 27 MHz value and does the split.
                 *
                 * @param value27mhz The PCR value in 27 MHz ticks
                 *                   (range 0 .. 2^33 × 300 - 1).
                 * @param out        Receives 6 bytes.
                 */
                static void encodePcr(uint64_t value27mhz, uint8_t out[6]);

                /**
                 * @brief Decodes a 6-byte adaptation-field PCR group
                 *        back to its 27 MHz value.
                 *
                 * Inverse of @ref encodePcr.
                 *
                 * @param in Pointer to 6 bytes of PCR data.
                 * @return The PCR value in 27 MHz ticks.
                 */
                static uint64_t decodePcr(const uint8_t in[6]);

                /**
                 * @brief Builds the Program Association Table (PAT)
                 *        payload — section + table header through CRC.
                 *
                 * One-program form: a single (@p programNumber → @p pmtPid)
                 * association is emitted.  The output is the PSI
                 * section starting with @c table_id @c 0x00 (PAT)
                 * through the trailing CRC32; the caller is
                 * responsible for prefixing the one-byte
                 * @c pointer_field (typically @c 0x00) inside the TS
                 * packet payload.
                 *
                 * @param transportStreamId The 16-bit @c transport_stream_id.
                 * @param programNumber     The single program's number.
                 * @param pmtPid            The PID carrying that program's PMT.
                 * @param versionNumber     5-bit @c version_number (modulo 32).
                 * @param outBuf            Receives the PAT section bytes.
                 * @return @c Error::Ok on success.
                 */
                static Error buildPat(uint16_t transportStreamId, uint16_t programNumber, uint16_t pmtPid,
                                      uint8_t versionNumber, Buffer &outBuf);

                /**
                 * @brief One stream entry in a PMT loop.
                 *
                 * Programs with no codec-private descriptors leave
                 * @c descriptors empty.  The muxer's writer-side does
                 * not generate descriptors automatically — that
                 * decision belongs to the producer (e.g. an AAC
                 * stream may need an @c MPEG-4_audio_descriptor when
                 * the container relies on out-of-band config).
                 */
                struct PmtStream {
                                uint8_t  streamType = 0;  ///< @ref StreamType value.
                                uint16_t pid = 0;         ///< PID carrying this stream's PES packets.
                                Buffer   descriptors;     ///< Raw ES_info descriptor bytes (may be empty).
                };

                /**
                 * @brief Builds the Program Map Table (PMT) payload —
                 *        section + table header through CRC.
                 *
                 * @param programNumber  The 16-bit @c program_number.
                 * @param pcrPid         The PID carrying the PCR for this program
                 *                       (typically the video PID).
                 * @param versionNumber  5-bit @c version_number (modulo 32).
                 * @param programDescriptors Raw program-level descriptor bytes
                 *                           (may be empty).
                 * @param streams        Per-stream entries; each entry's
                 *                       @c streamType / @c pid / @c descriptors
                 *                       drives one PMT loop iteration.
                 * @param outBuf         Receives the PMT section bytes.
                 * @return @c Error::Ok on success.
                 */
                static Error buildPmt(uint16_t programNumber, uint16_t pcrPid, uint8_t versionNumber,
                                      const BufferView &programDescriptors, const List<PmtStream> &streams,
                                      Buffer &outBuf);

                /**
                 * @brief Fields that influence the PES header layout.
                 *
                 * @c hasPts and @c hasDts control the PTS/DTS_flags
                 * pair and the corresponding 5-byte fields; @c dts is
                 * ignored when @c hasDts is false.  @c streamId selects
                 * the PES @c stream_id byte.  @c dataAlignmentIndicator
                 * flags access-unit boundaries (decoder hint —
                 * H.264 / HEVC IDRs typically set it).  @c pesPacketLength
                 * is the value emitted on the wire; the muxer uses
                 * 0 (unbounded) for video streams whose access unit
                 * exceeds 65 525 bytes and the literal payload length
                 * for everything else.
                 */
                struct PesHeader {
                                uint8_t  streamId = 0;
                                uint16_t pesPacketLength = 0;        ///< 0 = unbounded; rare for audio.
                                bool     dataAlignmentIndicator = false;
                                bool     hasPts = false;
                                bool     hasDts = false;
                                uint64_t pts90k = 0;
                                uint64_t dts90k = 0;
                };

                /**
                 * @brief Computes the byte length of a PES header for
                 *        the given fields.
                 *
                 * Sum of:
                 *  - 6 bytes for the start code, stream_id, and PES_packet_length
                 *  - 3 bytes for the PES-extension flags
                 *  - 5 bytes when @c hasPts
                 *  - 5 more bytes when @c hasDts
                 *
                 * @param h The header fields.
                 * @return Header length in bytes.
                 */
                static size_t pesHeaderSize(const PesHeader &h);

                /**
                 * @brief Writes a PES header into @p out.
                 *
                 * @param h   Header fields.
                 * @param out Destination — must have at least
                 *            @ref pesHeaderSize(h) bytes of room.
                 */
                static void writePesHeader(const PesHeader &h, uint8_t *out);

                /**
                 * @brief Reads a PES header from @p in.
                 *
                 * Populates @p out with the decoded fields and writes
                 * the total header size (start code through the end
                 * of @c PES_header_data, i.e. the offset at which the
                 * elementary-stream payload begins) into
                 * @p headerSize.
                 *
                 * @param in           Pointer to the start of a PES
                 *                     packet (the @c 00 00 01 start
                 *                     code).
                 * @param len          Number of bytes available at
                 *                     @p in.  Must be at least 9
                 *                     (the minimum PES header size).
                 * @param out          Receives the parsed fields.
                 * @param headerSize   Receives the header size in
                 *                     bytes.
                 * @return @c Error::Ok on success,
                 *         @c Error::InvalidArgument if @p in is
                 *         shorter than 9 bytes,
                 *         @c Error::CorruptData if the start code is
                 *         missing or the encoded header runs past
                 *         @p len.
                 */
                static Error readPesHeader(const uint8_t *in, size_t len, PesHeader *out, size_t *headerSize);

                /**
                 * @brief Verifies the trailing CRC of a PSI section.
                 *
                 * @param section Pointer to the section's @c table_id byte.
                 * @param len     Total section size including the
                 *                trailing 4-byte CRC.
                 * @return @c true when the recomputed CRC matches the
                 *         on-wire value.  Always @c false for
                 *         @p len &lt; 8.
                 */
                static bool isPsiSectionValid(const uint8_t *section, size_t len);

                /**
                 * @brief One Program Association Table entry.
                 *
                 * @c programNumber @c 0 identifies the network PID
                 * (NIT) per ISO/IEC 13818-1 §2.4.4.4; non-zero
                 * entries are program → PMT-PID mappings.
                 */
                struct PatEntry {
                                uint16_t programNumber = 0;
                                uint16_t pid = 0;
                };

                /** @brief Decoded contents of a Program Association Table section. */
                struct ParsedPat {
                                uint16_t        transportStreamId = 0;
                                uint8_t         versionNumber = 0;
                                bool            currentNextIndicator = false;
                                List<PatEntry>  entries;
                };

                /**
                 * @brief Parses a PSI PAT section.
                 *
                 * @param section Pointer to the section's @c table_id
                 *                byte.  The CRC must have already been
                 *                verified by the caller (or via
                 *                @ref isPsiSectionValid).
                 * @param len     Total section size including the
                 *                trailing 4-byte CRC.
                 * @param out     Receives the parsed fields.
                 * @return @c Error::Ok on success,
                 *         @c Error::InvalidArgument if @p len is too
                 *         small for a PAT section,
                 *         @c Error::CorruptData if the @c table_id is
                 *         not @c 0x00 or if the section's internal
                 *         length fields are inconsistent with
                 *         @p len.
                 */
                static Error parsePat(const uint8_t *section, size_t len, ParsedPat *out);

                /** @brief Decoded contents of a Program Map Table section. */
                struct ParsedPmt {
                                uint16_t                programNumber = 0;
                                uint8_t                 versionNumber = 0;
                                bool                    currentNextIndicator = false;
                                uint16_t                pcrPid = 0;
                                Buffer                  programDescriptors;
                                List<PmtStream>         streams;
                };

                /**
                 * @brief Parses a PSI PMT section.
                 *
                 * Mirrors @ref parsePat.  Stream entries' descriptor
                 * bytes are copied into freshly allocated
                 * @ref Buffer values so the result outlives @p
                 * section.
                 *
                 * @param section Pointer to the section's @c table_id
                 *                byte.  CRC must already be valid.
                 * @param len     Total section size including the
                 *                trailing 4-byte CRC.
                 * @param out     Receives the parsed fields.
                 * @return @c Error::Ok on success, or @c Error::CorruptData
                 *         on a structural mismatch.
                 */
                static Error parsePmt(const uint8_t *section, size_t len, ParsedPmt *out);

                /**
                 * @brief Builds a single @c registration_descriptor
                 *        carrying @p formatIdentifier.
                 *
                 * Emits the 6-byte descriptor:
                 * @code
                 *   tag = 0x05
                 *   length = 4
                 *   format_identifier = formatIdentifier (big-endian)
                 * @endcode
                 *
                 * Suitable for the @c descriptors parameter of
                 * @ref MpegTsMuxer::addStream when registering a
                 * @ref StreamTypePrivatePes / @ref StreamTypeJpegXs
                 * stream that needs a @c format_identifier on the wire.
                 *
                 * @param formatIdentifier 32-bit four-character format
                 *        identifier (e.g. @ref RegFormatSmpte302M,
                 *        @ref RegFormatOpus, @ref RegFormatAv1,
                 *        @ref RegFormatJpegXs).
                 * @param outBuf Receives the 6-byte descriptor; the
                 *        buffer's existing contents are overwritten.
                 * @return @c Error::Ok on success.
                 */
                static Error buildRegistrationDescriptor(uint32_t formatIdentifier, Buffer &outBuf);

                /**
                 * @brief Scans an @c ES_info / @c program_descriptors
                 *        descriptor list for a @c registration_descriptor.
                 *
                 * @param descriptors Raw descriptor-list bytes (one or
                 *        more concatenated @c [tag, length, payload]
                 *        triplets).  An empty / null view is permitted
                 *        and returns @c Error::NotFound.
                 * @param outFormatIdentifier Receives the 32-bit
                 *        @c format_identifier when found.
                 * @return @c Error::Ok when a registration descriptor
                 *         is found, @c Error::NotFound when the list
                 *         contains no registration descriptor, or
                 *         @c Error::CorruptData if any descriptor's
                 *         @c length runs past @p descriptors.
                 */
                static Error findRegistrationDescriptor(const BufferView &descriptors,
                                                        uint32_t         *outFormatIdentifier);

                /** @brief Descriptor tag @c 0x7F (extension_descriptor). */
                static constexpr uint8_t DescriptorTagExtension = 0x7F;

                /** @brief Descriptor tag @c 0x80 (AV1 @c av1_video_descriptor, per AOMedia AV1-in-MPEG-2-TS). */
                static constexpr uint8_t DescriptorTagAv1Video = 0x80;

                /** @brief Descriptor tag @c 0x32 (@c jxs_video_descriptor, per ISO/IEC 13818-1 AMD 3). */
                static constexpr uint8_t DescriptorTagJxsVideo = 0x32;

                /**
                 * @brief @c extension_descriptor_tag value for the Opus
                 *        audio descriptor, per DVB BlueBook A146.
                 */
                static constexpr uint8_t ExtensionDescTagOpus = 0x80;

                /**
                 * @brief Builds an Opus audio descriptor (DVB BlueBook
                 *        A146 §6.1).
                 *
                 * Wire form is an extension_descriptor (@c tag = 0x7F,
                 * @c length = 2) with @c descriptor_tag_extension =
                 * @c 0x80 (Opus) followed by a single
                 * @c channel_config_code byte.  This implementation
                 * stamps @c channel_config_code = @p channels which
                 * matches ffmpeg's libavformat encoder and is
                 * sufficient for mono / stereo / multichannel feeds
                 * up to 8 channels — finer-grained mapping family
                 * selection is left for the caller to layer on by
                 * building the descriptor by hand.
                 *
                 * @param channels  Number of channels (1..8).
                 * @param outBuf    Receives the 4-byte descriptor.
                 * @return @c Error::Ok on success,
                 *         @c Error::InvalidArgument when @p channels
                 *         is out of range.
                 */
                static Error buildOpusExtensionDescriptor(unsigned channels, Buffer &outBuf);

                /**
                 * @brief Builds an AV1 @c av1_video_descriptor (AOM
                 *        AV1-in-MPEG-2-TS §3).
                 *
                 * Wire form is @c descriptor_tag = 0x80,
                 * @c descriptor_length = 4, then 4 bytes of payload:
                 * @c version (8 bits), @c profile (3) + @c level_idx
                 * (5), @c tier (1) + @c HDR_WCG_idc (2) + @c HDR_dynamic
                 * (1) + @c HDR_static (1) + reserved (1) +
                 * @c seq_force_screen_content_tools (1) +
                 * @c seq_force_integer_mv (1),
                 * @c initial_presentation_delay_present_flag (1) +
                 * @c initial_presentation_delay_minus_one (4) +
                 * reserved (3).
                 *
                 * Default-constructed inputs produce a safe descriptor
                 * (@c version=1, @c profile=0 / Main, @c level=auto,
                 * unknown HDR, presentation delay absent) — sufficient
                 * for pass-through decoders that consume the bitstream
                 * to learn the real values.
                 *
                 * @param outBuf  Receives the 6-byte descriptor.
                 * @return @c Error::Ok.
                 */
                static Error buildAv1VideoDescriptor(Buffer &outBuf);

                /**
                 * @brief Builds a JPEG XS @c jxs_video_descriptor
                 *        (ISO/IEC 13818-1 AMD 3 / SMPTE ST 2049-1).
                 *
                 * Wire form is @c descriptor_tag = 0x32,
                 * @c descriptor_length = 24, then the fields
                 * @c descriptor_version (8), @c horizontal_size (16),
                 * @c vertical_size (16), @c brat (32), @c frat (32),
                 * @c schar (16), @c Ppih (16), @c Plev (16),
                 * @c max_buffer_size (32).  Caller passes the
                 * uncompressed-side @c width / @c height — the rest
                 * are filled with zeros (= unknown / unconstrained),
                 * which is the safe default for downstream consumers
                 * that re-parse the codestream header anyway.
                 *
                 * @param width   Frame width in pixels.
                 * @param height  Frame height in pixels.
                 * @param frameRateNum / frameRateDen  Frame rate
                 *                expressed as a fraction; encoded into
                 *                the 32-bit @c frat field
                 *                (interlace_mode=0 | frame_rate_den=1bit
                 *                | frame_rate_num=24bit).  Pass
                 *                @c (0, 1) to leave @c frat zero.
                 * @param outBuf  Receives the 26-byte descriptor.
                 * @return @c Error::Ok.
                 */
                static Error buildJpegXsVideoDescriptor(uint16_t width, uint16_t height,
                                                        uint32_t frameRateNum, uint32_t frameRateDen,
                                                        Buffer &outBuf);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
