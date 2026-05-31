/**
 * @file      mpegts.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mpegts.h>
#include <promeki/crc.h>
#include <promeki/error.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // CRC-32/MPEG-2 parameters per ISO/IEC 13818-1 §2.4.4.10 Annex A.
        // Same polynomial as CRC-32/ISO-HDLC but unreflected and with no
        // final XOR.
        constexpr Crc32::Params kPsiCrc32Params{0x04C11DB7u, 0xFFFFFFFFu, 0x00000000u, false, "CRC-32/MPEG-2"};

        inline void writeBE16(uint8_t *p, uint16_t v) {
                p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
                p[1] = static_cast<uint8_t>(v & 0xFF);
        }

        inline void writeBE32(uint8_t *p, uint32_t v) {
                p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
                p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
                p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
                p[3] = static_cast<uint8_t>(v & 0xFF);
        }

        inline uint16_t readBE16(const uint8_t *p) {
                return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        }

        inline uint32_t readBE32(const uint8_t *p) {
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }

        // PSI section_length is a 12-bit field stored as
        //   reserved(2='11') | private(1='0' for PAT, '0' for PMT) | unused(1) | length(12).
        // The 4 reserved/zero high bits vary by table type — PAT/PMT
        // share the same wire shape: the upper nibble is 0b1011 (PAT,
        // private=0, reserved=0) for both.
        constexpr uint16_t kSectionLengthPrefix = 0xB000;

} // namespace

uint32_t MpegTs::psiCrc32(const void *data, size_t len) {
        return Crc32::compute(kPsiCrc32Params, data, len);
}

uint32_t MpegTs::psiCrc32(const BufferView &v) {
        if (!v.isValid()) return 0;
        // PSI sections live in a single contiguous buffer when we build
        // them; multi-slice inputs are not expected for CRC use.
        return psiCrc32(v.data(), v.size());
}

void MpegTs::encodePesPts(uint64_t value90k, uint8_t prefix, uint8_t out[5]) {
        // ISO/IEC 13818-1 §2.4.3.7.  Layout (MSB first):
        //   byte 0: prefix(4) | TS[32..30](3) | marker(1='1')
        //   byte 1: TS[29..22](8)
        //   byte 2: TS[21..15](7) | marker(1='1')
        //   byte 3: TS[14..7](8)
        //   byte 4: TS[6..0](7) | marker(1='1')
        const uint64_t v = value90k & ((1ull << 33) - 1);
        out[0] = static_cast<uint8_t>(((prefix & 0x0F) << 4) | (((v >> 30) & 0x07) << 1) | 0x01);
        out[1] = static_cast<uint8_t>((v >> 22) & 0xFF);
        out[2] = static_cast<uint8_t>((((v >> 15) & 0x7F) << 1) | 0x01);
        out[3] = static_cast<uint8_t>((v >> 7) & 0xFF);
        out[4] = static_cast<uint8_t>(((v & 0x7F) << 1) | 0x01);
}

uint64_t MpegTs::decodePesPts(const uint8_t in[5]) {
        const uint64_t hi = (static_cast<uint64_t>(in[0]) >> 1) & 0x07ull;
        const uint64_t a = static_cast<uint64_t>(in[1]);
        const uint64_t b = (static_cast<uint64_t>(in[2]) >> 1) & 0x7Full;
        const uint64_t c = static_cast<uint64_t>(in[3]);
        const uint64_t d = (static_cast<uint64_t>(in[4]) >> 1) & 0x7Full;
        return (hi << 30) | (a << 22) | (b << 15) | (c << 7) | d;
}

void MpegTs::encodePcr(uint64_t value27mhz, uint8_t out[6]) {
        // ISO/IEC 13818-1 §2.4.3.5: 33-bit base @ 90 kHz, 6 reserved
        // bits ('111111'), 9-bit extension @ 27 MHz - base * 300.
        const uint64_t base = (value27mhz / 300) & ((1ull << 33) - 1);
        const uint64_t ext = value27mhz % 300;
        out[0] = static_cast<uint8_t>((base >> 25) & 0xFF);
        out[1] = static_cast<uint8_t>((base >> 17) & 0xFF);
        out[2] = static_cast<uint8_t>((base >> 9) & 0xFF);
        out[3] = static_cast<uint8_t>((base >> 1) & 0xFF);
        out[4] = static_cast<uint8_t>(((base & 0x01) << 7) | 0x7E | static_cast<uint8_t>((ext >> 8) & 0x01));
        out[5] = static_cast<uint8_t>(ext & 0xFF);
}

uint64_t MpegTs::decodePcr(const uint8_t in[6]) {
        const uint64_t base = (static_cast<uint64_t>(in[0]) << 25) | (static_cast<uint64_t>(in[1]) << 17) |
                              (static_cast<uint64_t>(in[2]) << 9) | (static_cast<uint64_t>(in[3]) << 1) |
                              (static_cast<uint64_t>(in[4]) >> 7);
        const uint64_t ext = ((static_cast<uint64_t>(in[4]) & 0x01) << 8) | static_cast<uint64_t>(in[5]);
        return base * 300 + ext;
}

Error MpegTs::buildPat(uint16_t transportStreamId, uint16_t programNumber, uint16_t pmtPid, uint8_t versionNumber,
                       Buffer &outBuf) {
        // PSI PAT layout (single program):
        //   table_id(8) = 0x00
        //   section_syntax_indicator(1)='1' | '0' | reserved(2)='11' | section_length(12)
        //   transport_stream_id(16)
        //   reserved(2)='11' | version_number(5) | current_next_indicator(1)='1'
        //   section_number(8)=0
        //   last_section_number(8)=0
        //   per-program loop (4 bytes per entry):
        //     program_number(16)
        //     reserved(3)='111' | program_map_PID(13)
        //   CRC_32(32)
        //
        // section_length counts every byte *after* itself, including
        // CRC32 — for one program that's:
        //   2 (tsid) + 1 (version) + 1 (section_number) + 1 (last_section_number)
        //   + 4 (one program loop entry) + 4 (CRC) = 13 bytes.

        constexpr size_t kProgramEntrySize = 4;
        constexpr size_t kHeaderAfterSectionLength = 5; // tsid + version + section# + last#
        constexpr size_t kCrcSize = 4;

        const size_t sectionLengthValue = kHeaderAfterSectionLength + kProgramEntrySize + kCrcSize;
        const size_t totalSize = 3 /* table_id + section_length-prefix */ + sectionLengthValue;

        outBuf = Buffer(totalSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(totalSize);
        uint8_t *p = static_cast<uint8_t *>(outBuf.data());

        p[0] = 0x00; // table_id = PAT
        const uint16_t sectionLen = static_cast<uint16_t>(kSectionLengthPrefix | (sectionLengthValue & 0x0FFF));
        writeBE16(p + 1, sectionLen);
        writeBE16(p + 3, transportStreamId);
        p[5] = static_cast<uint8_t>(0xC0 /* reserved '11' */ | ((versionNumber & 0x1F) << 1) | 0x01 /* current_next */);
        p[6] = 0x00; // section_number
        p[7] = 0x00; // last_section_number
        writeBE16(p + 8, programNumber);
        writeBE16(p + 10, static_cast<uint16_t>(0xE000 /* reserved '111' */ | (pmtPid & 0x1FFF)));
        // CRC32 is computed over table_id through the last program entry
        // (i.e. everything up to but not including the CRC field).
        const uint32_t crc = psiCrc32(p, totalSize - kCrcSize);
        writeBE32(p + totalSize - kCrcSize, crc);
        return Error::Ok;
}

Error MpegTs::buildPmt(uint16_t programNumber, uint16_t pcrPid, uint8_t versionNumber,
                       const BufferView &programDescriptors, const List<PmtStream> &streams, Buffer &outBuf) {
        // PSI PMT layout:
        //   table_id(8) = 0x02
        //   section_syntax_indicator(1)='1' | '0' | reserved(2) | section_length(12)
        //   program_number(16)
        //   reserved(2) | version_number(5) | current_next_indicator(1)
        //   section_number(8)=0
        //   last_section_number(8)=0
        //   reserved(3)='111' | PCR_PID(13)
        //   reserved(4)='1111' | program_info_length(12)
        //   program-level descriptors (program_info_length bytes)
        //   per-stream loop:
        //     stream_type(8)
        //     reserved(3)='111' | elementary_PID(13)
        //     reserved(4)='1111' | ES_info_length(12)
        //     ES_info descriptors
        //   CRC_32(32)
        //
        // section_length counts every byte after itself, including CRC32.

        const size_t programDescLen = programDescriptors.isValid() ? programDescriptors.size() : 0;
        if (programDescLen > 0x3FF) return Error::InvalidArgument;

        size_t streamLoopLen = 0;
        for (const PmtStream &s : streams) {
                const size_t esInfoLen = s.descriptors.isValid() ? s.descriptors.size() : 0;
                if (esInfoLen > 0x3FF) return Error::InvalidArgument;
                streamLoopLen += 5 + esInfoLen;
        }

        constexpr size_t kHeaderAfterSectionLength = 9; // program_number(2)+version(1)+section#(1)+last#(1)+pcrPid(2)+programInfoLen(2)
        constexpr size_t kCrcSize = 4;

        const size_t sectionLengthValue = kHeaderAfterSectionLength + programDescLen + streamLoopLen + kCrcSize;
        if (sectionLengthValue > 0x3FF) return Error::InvalidArgument; // 12-bit field cap.

        const size_t totalSize = 3 + sectionLengthValue;
        outBuf = Buffer(totalSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(totalSize);
        uint8_t *p = static_cast<uint8_t *>(outBuf.data());

        p[0] = 0x02; // table_id = PMT
        const uint16_t sectionLen = static_cast<uint16_t>(kSectionLengthPrefix | (sectionLengthValue & 0x0FFF));
        writeBE16(p + 1, sectionLen);
        writeBE16(p + 3, programNumber);
        p[5] = static_cast<uint8_t>(0xC0 | ((versionNumber & 0x1F) << 1) | 0x01);
        p[6] = 0x00; // section_number
        p[7] = 0x00; // last_section_number
        writeBE16(p + 8, static_cast<uint16_t>(0xE000 | (pcrPid & 0x1FFF)));
        writeBE16(p + 10, static_cast<uint16_t>(0xF000 | (programDescLen & 0x0FFF)));
        size_t off = 12;
        if (programDescLen > 0) {
                const uint8_t *src = programDescriptors.data();
                for (size_t i = 0; i < programDescLen; ++i) p[off + i] = src[i];
                off += programDescLen;
        }
        for (const PmtStream &s : streams) {
                const size_t esInfoLen = s.descriptors.isValid() ? s.descriptors.size() : 0;
                p[off + 0] = s.streamType;
                writeBE16(p + off + 1, static_cast<uint16_t>(0xE000 | (s.pid & 0x1FFF)));
                writeBE16(p + off + 3, static_cast<uint16_t>(0xF000 | (esInfoLen & 0x0FFF)));
                off += 5;
                if (esInfoLen > 0) {
                        const uint8_t *src = static_cast<const uint8_t *>(s.descriptors.data());
                        for (size_t i = 0; i < esInfoLen; ++i) p[off + i] = src[i];
                        off += esInfoLen;
                }
        }
        const uint32_t crc = psiCrc32(p, totalSize - kCrcSize);
        writeBE32(p + off, crc);
        return Error::Ok;
}

size_t MpegTs::pesHeaderSize(const PesHeader &h) {
        size_t n = 9; // start_code(3)+stream_id(1)+pes_packet_length(2)+flags(3)
        if (h.hasPts) n += 5;
        if (h.hasDts) n += 5;
        return n;
}

void MpegTs::writePesHeader(const PesHeader &h, uint8_t *out) {
        const size_t headerSize = pesHeaderSize(h);
        const size_t pesHeaderDataLength = headerSize - 9; // bytes following the third flag byte.

        // Start code prefix + stream_id.
        out[0] = 0x00;
        out[1] = 0x00;
        out[2] = 0x01;
        out[3] = h.streamId;

        // PES_packet_length — number of bytes of the PES packet
        // following this field.  Always emit verbatim; the caller is
        // responsible for clamping unbounded payloads to 0.
        writeBE16(out + 4, h.pesPacketLength);

        // Flags byte 1: marker(2)='10' | scrambling(2)=0 | priority(1)=0
        //               | data_alignment(1) | copyright(1)=0 | original(1)=0
        out[6] = static_cast<uint8_t>(0x80 | (h.dataAlignmentIndicator ? 0x04 : 0x00));

        // Flags byte 2: PTS_DTS_flags(2) | ESCR(1)=0 | ES_rate(1)=0
        //               | DSM_trick(1)=0 | additional_copy_info(1)=0
        //               | PES_CRC(1)=0 | extension(1)=0
        uint8_t ptsDtsFlags = 0;
        if (h.hasPts && h.hasDts)
                ptsDtsFlags = 0xC0; // '11'
        else if (h.hasPts)
                ptsDtsFlags = 0x80; // '10'
        out[7] = ptsDtsFlags;

        // PES_header_data_length.
        out[8] = static_cast<uint8_t>(pesHeaderDataLength & 0xFF);

        size_t p = 9;
        if (h.hasPts && h.hasDts) {
                encodePesPts(h.pts90k, 0x3, out + p); // PTS half of PTS+DTS = '0011'
                p += 5;
                encodePesPts(h.dts90k, 0x1, out + p); // DTS = '0001'
                p += 5;
        } else if (h.hasPts) {
                encodePesPts(h.pts90k, 0x2, out + p); // PTS-only = '0010'
                p += 5;
        }
}

Error MpegTs::readPesHeader(const uint8_t *in, size_t len, PesHeader *out, size_t *headerSize) {
        if (in == nullptr || out == nullptr || headerSize == nullptr) return Error::InvalidArgument;
        if (len < 9) return Error::InvalidArgument;
        if (in[0] != 0x00 || in[1] != 0x00 || in[2] != 0x01) return Error::CorruptData;
        out->streamId = in[3];
        out->pesPacketLength = readBE16(in + 4);
        const uint8_t flags1 = in[6];
        const uint8_t flags2 = in[7];
        const uint8_t pesHdrDataLen = in[8];
        if (static_cast<size_t>(9 + pesHdrDataLen) > len) return Error::CorruptData;
        out->dataAlignmentIndicator = (flags1 & 0x04) != 0;
        const uint8_t ptsDtsFlags = static_cast<uint8_t>((flags2 >> 6) & 0x03);
        out->hasPts = (ptsDtsFlags & 0x02) != 0;
        out->hasDts = ptsDtsFlags == 0x03;
        out->pts90k = 0;
        out->dts90k = 0;
        size_t pos = 9;
        if (out->hasPts) {
                if (pos + 5 > len) return Error::CorruptData;
                out->pts90k = decodePesPts(in + pos);
                pos += 5;
        }
        if (out->hasDts) {
                if (pos + 5 > len) return Error::CorruptData;
                out->dts90k = decodePesPts(in + pos);
                pos += 5;
        }
        // header runs from byte 0 through the end of PES_header_data
        // (which is exactly 9 + pesHdrDataLen bytes).  Any unread
        // bytes inside the declared header_data region (additional
        // flags we don't decode) are skipped by the caller.
        *headerSize = 9 + pesHdrDataLen;
        return Error::Ok;
}

bool MpegTs::isPsiSectionValid(const uint8_t *section, size_t len) {
        if (section == nullptr || len < 8) return false;
        const uint32_t calc = psiCrc32(section, len - 4);
        const uint32_t wire = readBE32(section + len - 4);
        return calc == wire;
}

Error MpegTs::parsePat(const uint8_t *section, size_t len, ParsedPat *out) {
        // section layout:
        //   table_id(1) section_syntax(2bits/+12 length) tsid(2) version(1)
        //   section#(1) last#(1)
        //   N * (programNumber(2) + reserved(3)+pid(13))
        //   CRC(4)
        if (out == nullptr) return Error::InvalidArgument;
        if (section == nullptr || len < 12) return Error::InvalidArgument;
        if (section[0] != 0x00) return Error::CorruptData;
        const uint16_t sectionLen = static_cast<uint16_t>(((section[1] & 0x0F) << 8) | section[2]);
        if (static_cast<size_t>(sectionLen) + 3 != len) return Error::CorruptData;
        out->transportStreamId = readBE16(section + 3);
        out->versionNumber = static_cast<uint8_t>((section[5] >> 1) & 0x1F);
        out->currentNextIndicator = (section[5] & 0x01) != 0;
        out->entries.clear();
        const size_t loopStart = 8;
        const size_t loopEnd = len - 4;
        if (loopEnd < loopStart) return Error::CorruptData;
        for (size_t i = loopStart; i + 4 <= loopEnd; i += 4) {
                PatEntry e;
                e.programNumber = readBE16(section + i);
                e.pid = static_cast<uint16_t>(readBE16(section + i + 2) & 0x1FFF);
                out->entries.pushToBack(e);
        }
        return Error::Ok;
}

Error MpegTs::parsePmt(const uint8_t *section, size_t len, ParsedPmt *out) {
        if (out == nullptr) return Error::InvalidArgument;
        if (section == nullptr || len < 16) return Error::InvalidArgument;
        if (section[0] != 0x02) return Error::CorruptData;
        const uint16_t sectionLen = static_cast<uint16_t>(((section[1] & 0x0F) << 8) | section[2]);
        if (static_cast<size_t>(sectionLen) + 3 != len) return Error::CorruptData;
        out->programNumber = readBE16(section + 3);
        out->versionNumber = static_cast<uint8_t>((section[5] >> 1) & 0x1F);
        out->currentNextIndicator = (section[5] & 0x01) != 0;
        out->pcrPid = static_cast<uint16_t>(readBE16(section + 8) & 0x1FFF);
        const uint16_t programInfoLen = static_cast<uint16_t>(readBE16(section + 10) & 0x0FFF);
        size_t         off = 12;
        if (off + programInfoLen > len - 4) return Error::CorruptData;
        if (programInfoLen > 0) {
                out->programDescriptors = Buffer(programInfoLen);
                if (!out->programDescriptors.isValid()) return Error::NoMem;
                out->programDescriptors.setSize(programInfoLen);
                std::memcpy(out->programDescriptors.data(), section + off, programInfoLen);
        } else {
                out->programDescriptors = Buffer();
        }
        off += programInfoLen;
        const size_t end = len - 4;
        out->streams.clear();
        while (off + 5 <= end) {
                PmtStream      s;
                s.streamType = section[off];
                s.pid = static_cast<uint16_t>(readBE16(section + off + 1) & 0x1FFF);
                const uint16_t esInfoLen = static_cast<uint16_t>(readBE16(section + off + 3) & 0x0FFF);
                off += 5;
                if (off + esInfoLen > end) return Error::CorruptData;
                if (esInfoLen > 0) {
                        s.descriptors = Buffer(esInfoLen);
                        if (!s.descriptors.isValid()) return Error::NoMem;
                        s.descriptors.setSize(esInfoLen);
                        std::memcpy(s.descriptors.data(), section + off, esInfoLen);
                }
                off += esInfoLen;
                out->streams.pushToBack(std::move(s));
        }
        return Error::Ok;
}

Error MpegTs::buildRegistrationDescriptor(uint32_t formatIdentifier, Buffer &outBuf) {
        constexpr size_t kSize = 6; // tag(1) + length(1) + format_identifier(4)
        outBuf = Buffer(kSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(kSize);
        uint8_t *p = static_cast<uint8_t *>(outBuf.data());
        p[0] = DescriptorTagRegistration;
        p[1] = 4;
        p[2] = static_cast<uint8_t>((formatIdentifier >> 24) & 0xFF);
        p[3] = static_cast<uint8_t>((formatIdentifier >> 16) & 0xFF);
        p[4] = static_cast<uint8_t>((formatIdentifier >> 8) & 0xFF);
        p[5] = static_cast<uint8_t>(formatIdentifier & 0xFF);
        return Error::Ok;
}

Error MpegTs::buildOpusExtensionDescriptor(unsigned channels, Buffer &outBuf) {
        if (channels < 1 || channels > 8) return Error::InvalidArgument;
        constexpr size_t kSize = 4; // tag(1) + length(1) + ext_tag(1) + channel_config_code(1)
        outBuf = Buffer(kSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(kSize);
        uint8_t *p = static_cast<uint8_t *>(outBuf.data());
        p[0] = DescriptorTagExtension;
        p[1] = 2;
        p[2] = ExtensionDescTagOpus;
        p[3] = static_cast<uint8_t>(channels & 0xFF);
        return Error::Ok;
}

Error MpegTs::buildAv1VideoDescriptor(Buffer &outBuf) {
        constexpr size_t kSize = 6; // tag(1) + length(1) + 4 payload
        outBuf = Buffer(kSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(kSize);
        uint8_t *p = static_cast<uint8_t *>(outBuf.data());
        p[0] = DescriptorTagAv1Video;
        p[1] = 4;
        // version = 1.
        p[2] = 0x01;
        // profile (3 bits, =0=Main) | level_idx (5 bits, =0=auto).
        p[3] = 0x00;
        // tier (1) | HDR_WCG_idc (2, =0=unknown) | HDR_dynamic (1)
        // | HDR_static (1) | reserved (1) |
        // seq_force_screen_content_tools (1, =1=auto) |
        // seq_force_integer_mv (1, =1=auto).
        // bits 7..0: tier=0 wcg=00 dyn=0 stat=0 rsv=0 ssct=1 sfim=1
        // → 0b00000011 = 0x03
        p[4] = 0x03;
        // initial_presentation_delay_present_flag (1, =0=absent) |
        // initial_presentation_delay_minus_one (4, =0) | reserved (3).
        p[5] = 0x00;
        return Error::Ok;
}

Error MpegTs::buildJpegXsVideoDescriptor(uint16_t width, uint16_t height, uint32_t frameRateNum,
                                          uint32_t frameRateDen, Buffer &outBuf) {
        constexpr size_t kPayload = 24;
        constexpr size_t kSize = 2 + kPayload;
        outBuf = Buffer(kSize);
        if (!outBuf.isValid()) return Error::NoMem;
        outBuf.setSize(kSize);
        uint8_t *p = static_cast<uint8_t *>(outBuf.data());
        p[0] = DescriptorTagJxsVideo;
        p[1] = static_cast<uint8_t>(kPayload);
        // descriptor_version = 0.
        p[2] = 0x00;
        // horizontal_size, vertical_size (uint16 BE).
        p[3] = static_cast<uint8_t>((width >> 8) & 0xFF);
        p[4] = static_cast<uint8_t>(width & 0xFF);
        p[5] = static_cast<uint8_t>((height >> 8) & 0xFF);
        p[6] = static_cast<uint8_t>(height & 0xFF);
        // brat (uint32 BE) — bit rate; 0 = unknown.
        std::memset(p + 7, 0, 4);
        // frat (uint32 BE) — interlace_mode(2) | reserved(6) |
        // frame_rate_den(8) | frame_rate_num(16).
        // Pack as: byte0 = interlace=0 + reserved (=0)
        //          byte1 = frame_rate_den (clamp to 1 byte)
        //          byte2,3 = frame_rate_num (16-bit clamp)
        uint32_t frat = 0;
        if (frameRateNum > 0 && frameRateDen > 0) {
                const uint32_t den = (frameRateDen > 0xFF) ? 0xFF : frameRateDen;
                const uint32_t num = (frameRateNum > 0xFFFF) ? 0xFFFF : frameRateNum;
                frat = (den << 16) | num;
        }
        p[11] = static_cast<uint8_t>((frat >> 24) & 0xFF);
        p[12] = static_cast<uint8_t>((frat >> 16) & 0xFF);
        p[13] = static_cast<uint8_t>((frat >> 8) & 0xFF);
        p[14] = static_cast<uint8_t>(frat & 0xFF);
        // schar (uint16 BE) — sample characteristic; 0 = unspecified.
        p[15] = 0x00;
        p[16] = 0x00;
        // Ppih (uint16 BE) — profile (Main 422.10 = 0x2400, but 0 = unconstrained).
        p[17] = 0x00;
        p[18] = 0x00;
        // Plev (uint16 BE) — level / sublevel; 0 = unconstrained.
        p[19] = 0x00;
        p[20] = 0x00;
        // max_buffer_size (uint32 BE) — 0 = unconstrained.
        std::memset(p + 21, 0, 4);
        return Error::Ok;
}

Error MpegTs::findRegistrationDescriptor(const BufferView &descriptors, uint32_t *outFormatIdentifier) {
        if (outFormatIdentifier == nullptr) return Error::InvalidArgument;
        if (!descriptors.isValid() || descriptors.size() == 0) return Error::NotFound;
        const uint8_t *p   = static_cast<const uint8_t *>(descriptors.data());
        const size_t   len = descriptors.size();
        size_t         off = 0;
        while (off + 2 <= len) {
                const uint8_t tag    = p[off];
                const uint8_t descLen = p[off + 1];
                if (off + 2 + descLen > len) return Error::CorruptData;
                if (tag == DescriptorTagRegistration && descLen >= 4) {
                        const uint8_t *d = p + off + 2;
                        *outFormatIdentifier = (static_cast<uint32_t>(d[0]) << 24) |
                                               (static_cast<uint32_t>(d[1]) << 16) |
                                               (static_cast<uint32_t>(d[2]) << 8) |
                                               static_cast<uint32_t>(d[3]);
                        return Error::Ok;
                }
                off += 2 + descLen;
        }
        return Error::NotFound;
}

PROMEKI_NAMESPACE_END
