/**
 * @file      tests/mpegts.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for the MPEG-TS bottom-layer primitives in @ref MpegTs:
 * PSI CRC-32, PES PTS / DTS encoding, PCR encoding, and PAT / PMT
 * builders.
 */

#include <doctest/doctest.h>
#include <promeki/mpegts.h>
#include <cstring>

using namespace promeki;


TEST_CASE("MpegTs::psiCrc32 — Rocksoft '123456789' check") {
        // CRC-32/MPEG-2 catalogue check value (Rocksoft / reveng):
        // ASCII "123456789" → 0x0376E6E7.
        const char *msg = "123456789";
        const uint32_t got = MpegTs::psiCrc32(msg, 9);
        CHECK(got == 0x0376E6E7u);
}

TEST_CASE("MpegTs::psiCrc32 — empty input is init value") {
        // With init=0xFFFFFFFF and no XOR, an empty input returns
        // the bare initial register value.
        CHECK(MpegTs::psiCrc32(nullptr, 0) == 0xFFFFFFFFu);
}

TEST_CASE("MpegTs::encodePesPts / decodePesPts round-trip") {
        SUBCASE("zero") {
                uint8_t buf[5];
                MpegTs::encodePesPts(0, 0x2, buf);
                // Marker bits must be set.
                CHECK((buf[0] & 0x01) == 0x01);
                CHECK((buf[2] & 0x01) == 0x01);
                CHECK((buf[4] & 0x01) == 0x01);
                // Top-nibble prefix.
                CHECK(((buf[0] >> 4) & 0x0F) == 0x2);
                CHECK(MpegTs::decodePesPts(buf) == 0);
        }
        SUBCASE("max 33-bit value") {
                const uint64_t maxVal = (1ull << 33) - 1;
                uint8_t        buf[5];
                MpegTs::encodePesPts(maxVal, 0x3, buf);
                CHECK(MpegTs::decodePesPts(buf) == maxVal);
                // Bits above 33 should be silently masked off.
                MpegTs::encodePesPts(maxVal | (1ull << 40), 0x3, buf);
                CHECK(MpegTs::decodePesPts(buf) == maxVal);
        }
        SUBCASE("sweep of representative values") {
                for (uint64_t v : {1ull, 90000ull, 12345678901ull, (1ull << 30), (1ull << 32) + 17}) {
                        const uint64_t masked = v & ((1ull << 33) - 1);
                        uint8_t        buf[5];
                        MpegTs::encodePesPts(v, 0x2, buf);
                        CHECK(MpegTs::decodePesPts(buf) == masked);
                }
        }
}

TEST_CASE("MpegTs::encodePcr / decodePcr round-trip") {
        SUBCASE("zero") {
                uint8_t buf[6];
                MpegTs::encodePcr(0, buf);
                CHECK(MpegTs::decodePcr(buf) == 0);
        }
        SUBCASE("base only — exact multiples of 300") {
                for (uint64_t base : {1ull, 90000ull, 1234567ull}) {
                        const uint64_t v = base * 300;
                        uint8_t        buf[6];
                        MpegTs::encodePcr(v, buf);
                        CHECK(MpegTs::decodePcr(buf) == v);
                }
        }
        SUBCASE("base + extension") {
                for (uint64_t ext : {1ull, 7ull, 150ull, 299ull}) {
                        const uint64_t v = 90000ull * 300 + ext;
                        uint8_t        buf[6];
                        MpegTs::encodePcr(v, buf);
                        CHECK(MpegTs::decodePcr(buf) == v);
                }
        }
}

TEST_CASE("MpegTs::buildPat — single program PAT") {
        Buffer out;
        REQUIRE(MpegTs::buildPat(/*tsid=*/0x1234, /*pn=*/1, /*pmtPid=*/0x1000, /*version=*/0, out) == Error::Ok);
        REQUIRE(out.isValid());
        REQUIRE(out.size() == 16); // 3 + 13 (computed in code).
        const uint8_t *p = static_cast<const uint8_t *>(out.data());
        CHECK(p[0] == 0x00);             // table_id = PAT
        CHECK((p[1] & 0xB0) == 0xB0);    // section_syntax + reserved
        CHECK(p[3] == 0x12);             // TSID hi
        CHECK(p[4] == 0x34);             // TSID lo
        CHECK((p[5] & 0xC1) == 0xC1);    // reserved + current_next
        CHECK(p[8] == 0x00);             // program_number hi
        CHECK(p[9] == 0x01);             // program_number lo
        CHECK((p[10] & 0xE0) == 0xE0);   // reserved on pmt PID
        CHECK(((p[10] & 0x1F) << 8 | p[11]) == 0x1000);

        // CRC validates: re-running psiCrc32 over the first 12 bytes
        // must equal the on-wire 4-byte trailer.
        const uint32_t calc = MpegTs::psiCrc32(p, out.size() - 4);
        const uint32_t wire = (static_cast<uint32_t>(p[12]) << 24) | (static_cast<uint32_t>(p[13]) << 16) |
                              (static_cast<uint32_t>(p[14]) << 8) | static_cast<uint32_t>(p[15]);
        CHECK(calc == wire);
}

TEST_CASE("MpegTs::buildPmt — single H.264 stream") {
        List<MpegTs::PmtStream> streams;
        MpegTs::PmtStream       s;
        s.streamType = MpegTs::StreamTypeH264;
        s.pid = 0x100;
        streams.pushToBack(s);

        Buffer out;
        REQUIRE(MpegTs::buildPmt(/*pn=*/1, /*pcrPid=*/0x100, /*version=*/0, BufferView(), streams, out) ==
                Error::Ok);
        REQUIRE(out.isValid());
        const uint8_t *p = static_cast<const uint8_t *>(out.data());
        CHECK(p[0] == 0x02); // table_id = PMT
        CHECK(((p[8] & 0x1F) << 8 | p[9]) == 0x100); // PCR_PID
        CHECK(((p[10] & 0x0F) << 8 | p[11]) == 0);  // program_info_length

        // First stream entry begins at offset 12.
        CHECK(p[12] == MpegTs::StreamTypeH264);
        CHECK(((p[13] & 0x1F) << 8 | p[14]) == 0x100); // elementary_PID
        CHECK(((p[15] & 0x0F) << 8 | p[16]) == 0);    // ES_info_length

        // CRC at the end validates.
        const size_t   crcOff = out.size() - 4;
        const uint32_t calc = MpegTs::psiCrc32(p, crcOff);
        const uint32_t wire = (static_cast<uint32_t>(p[crcOff]) << 24) | (static_cast<uint32_t>(p[crcOff + 1]) << 16) |
                              (static_cast<uint32_t>(p[crcOff + 2]) << 8) | static_cast<uint32_t>(p[crcOff + 3]);
        CHECK(calc == wire);
}

TEST_CASE("MpegTs::pesHeaderSize / writePesHeader") {
        MpegTs::PesHeader h;
        h.streamId = MpegTs::PesStreamIdVideoFirst;
        h.dataAlignmentIndicator = true;
        h.hasPts = true;
        h.hasDts = false;
        h.pts90k = 123456;
        h.pesPacketLength = 0;
        CHECK(MpegTs::pesHeaderSize(h) == 9 + 5);

        uint8_t buf[14];
        MpegTs::writePesHeader(h, buf);
        CHECK(buf[0] == 0x00);
        CHECK(buf[1] == 0x00);
        CHECK(buf[2] == 0x01);
        CHECK(buf[3] == MpegTs::PesStreamIdVideoFirst);
        // PES_packet_length == 0.
        CHECK(buf[4] == 0x00);
        CHECK(buf[5] == 0x00);
        // flags1: marker '10' + data_alignment '1' = 0x84.
        CHECK(buf[6] == 0x84);
        // flags2: PTS_only = 0x80.
        CHECK(buf[7] == 0x80);
        // PES_header_data_length = 5.
        CHECK(buf[8] == 0x05);
        // PTS field decodes back to 123456.
        CHECK(MpegTs::decodePesPts(buf + 9) == 123456u);
        // PTS prefix tag = 0010 = 0x2.
        CHECK(((buf[9] >> 4) & 0x0F) == 0x2);

        SUBCASE("PTS+DTS form uses different prefix tags") {
                h.hasDts = true;
                h.dts90k = 123000;
                CHECK(MpegTs::pesHeaderSize(h) == 9 + 10);
                uint8_t buf2[19];
                MpegTs::writePesHeader(h, buf2);
                CHECK(buf2[7] == 0xC0);                          // PTS+DTS flags.
                CHECK(buf2[8] == 0x0A);                          // header_data_length = 10.
                CHECK(((buf2[9] >> 4) & 0x0F) == 0x3);           // PTS half of pair.
                CHECK(((buf2[14] >> 4) & 0x0F) == 0x1);          // DTS half.
                CHECK(MpegTs::decodePesPts(buf2 + 9) == 123456u);
                CHECK(MpegTs::decodePesPts(buf2 + 14) == 123000u);
        }
}

TEST_CASE("MpegTs::buildRegistrationDescriptor — wire layout") {
        Buffer desc;
        REQUIRE(MpegTs::buildRegistrationDescriptor(MpegTs::RegFormatSmpte302M, desc).isOk());
        REQUIRE(desc.size() == 6);
        const uint8_t *p = static_cast<const uint8_t *>(desc.data());
        CHECK(p[0] == MpegTs::DescriptorTagRegistration); // tag = 0x05
        CHECK(p[1] == 4);                                 // length = 4
        // 'B' 'S' 'S' 'D'
        CHECK(p[2] == 'B');
        CHECK(p[3] == 'S');
        CHECK(p[4] == 'S');
        CHECK(p[5] == 'D');

        SUBCASE("Opus format_identifier") {
                Buffer d2;
                REQUIRE(MpegTs::buildRegistrationDescriptor(MpegTs::RegFormatOpus, d2).isOk());
                const uint8_t *p2 = static_cast<const uint8_t *>(d2.data());
                CHECK(p2[2] == 'O');
                CHECK(p2[3] == 'p');
                CHECK(p2[4] == 'u');
                CHECK(p2[5] == 's');
        }

        SUBCASE("AV1 format_identifier") {
                Buffer d2;
                REQUIRE(MpegTs::buildRegistrationDescriptor(MpegTs::RegFormatAv1, d2).isOk());
                const uint8_t *p2 = static_cast<const uint8_t *>(d2.data());
                CHECK(p2[2] == 'A');
                CHECK(p2[3] == 'V');
                CHECK(p2[4] == '0');
                CHECK(p2[5] == '1');
        }

        SUBCASE("JPEG XS format_identifier") {
                Buffer d2;
                REQUIRE(MpegTs::buildRegistrationDescriptor(MpegTs::RegFormatJpegXs, d2).isOk());
                const uint8_t *p2 = static_cast<const uint8_t *>(d2.data());
                CHECK(p2[2] == 'J');
                CHECK(p2[3] == 'X');
                CHECK(p2[4] == 'S');
                CHECK(p2[5] == 'V');
        }
}

TEST_CASE("MpegTs::findRegistrationDescriptor — locate in a descriptor list") {
        // Construct a 3-descriptor list: an arbitrary descriptor
        // (tag 0x52 with length 1), the registration descriptor, and
        // another arbitrary descriptor.  findRegistrationDescriptor
        // must skip the first one and return the format_identifier
        // from the middle entry.
        Buffer reg;
        REQUIRE(MpegTs::buildRegistrationDescriptor(MpegTs::RegFormatOpus, reg).isOk());

        Buffer list(3 + reg.size() + 3);
        list.setSize(3 + reg.size() + 3);
        uint8_t *p = static_cast<uint8_t *>(list.data());
        // Filler descriptor: tag 0x52 (stream_identifier) len 1 value 0x07.
        p[0] = 0x52;
        p[1] = 1;
        p[2] = 0x07;
        std::memcpy(p + 3, reg.data(), reg.size());
        const size_t after = 3 + reg.size();
        p[after + 0] = 0x52;
        p[after + 1] = 1;
        p[after + 2] = 0x08;

        BufferView v(list, 0, list.size());
        uint32_t   fmt = 0;
        REQUIRE(MpegTs::findRegistrationDescriptor(v, &fmt).isOk());
        CHECK(fmt == MpegTs::RegFormatOpus);

        SUBCASE("empty list returns NotFound") {
                BufferView empty;
                uint32_t   f = 0;
                CHECK(MpegTs::findRegistrationDescriptor(empty, &f) == Error::NotFound);
        }

        SUBCASE("list without registration_descriptor returns NotFound") {
                Buffer  noreg(3);
                noreg.setSize(3);
                uint8_t *np = static_cast<uint8_t *>(noreg.data());
                np[0] = 0x52;
                np[1] = 1;
                np[2] = 0x07;
                BufferView vv(noreg, 0, noreg.size());
                uint32_t   f = 0;
                CHECK(MpegTs::findRegistrationDescriptor(vv, &f) == Error::NotFound);
        }

        SUBCASE("malformed length is reported as CorruptData") {
                Buffer bad(3);
                bad.setSize(3);
                uint8_t *bp = static_cast<uint8_t *>(bad.data());
                bp[0] = 0x52;
                bp[1] = 99; // claims 99 payload bytes but we only have 1.
                bp[2] = 0x07;
                BufferView vv(bad, 0, bad.size());
                uint32_t   f = 0;
                CHECK(MpegTs::findRegistrationDescriptor(vv, &f) == Error::CorruptData);
        }
}

TEST_CASE("MpegTs::buildOpusExtensionDescriptor — DVB A146 wire layout") {
        Buffer d;
        REQUIRE(MpegTs::buildOpusExtensionDescriptor(2, d).isOk());
        REQUIRE(d.size() == 4);
        const uint8_t *p = static_cast<const uint8_t *>(d.data());
        CHECK(p[0] == MpegTs::DescriptorTagExtension); // 0x7F
        CHECK(p[1] == 2);                              // length
        CHECK(p[2] == MpegTs::ExtensionDescTagOpus);   // 0x80
        CHECK(p[3] == 2);                              // channel_config_code

        SUBCASE("8 channels") {
                Buffer d2;
                REQUIRE(MpegTs::buildOpusExtensionDescriptor(8, d2).isOk());
                const uint8_t *p2 = static_cast<const uint8_t *>(d2.data());
                CHECK(p2[3] == 8);
        }

        SUBCASE("invalid channel counts rejected") {
                Buffer d2;
                CHECK(MpegTs::buildOpusExtensionDescriptor(0, d2) == Error::InvalidArgument);
                CHECK(MpegTs::buildOpusExtensionDescriptor(9, d2) == Error::InvalidArgument);
        }
}

TEST_CASE("MpegTs::buildAv1VideoDescriptor — AOM wire layout") {
        Buffer d;
        REQUIRE(MpegTs::buildAv1VideoDescriptor(d).isOk());
        REQUIRE(d.size() == 6);
        const uint8_t *p = static_cast<const uint8_t *>(d.data());
        CHECK(p[0] == MpegTs::DescriptorTagAv1Video); // 0x80
        CHECK(p[1] == 4);                             // length
        CHECK(p[2] == 0x01);                          // version = 1
        // p[3] = profile=0 | level_idx=0
        CHECK(p[3] == 0x00);
        // p[4] = tier=0 | wcg=00 | dyn=0 | stat=0 | rsv=0 | ssct=1 | sfim=1
        CHECK(p[4] == 0x03);
        // p[5] = initial_presentation_delay_present_flag=0 | ipd_minus_one=0 | rsv=0
        CHECK(p[5] == 0x00);
}

TEST_CASE("MpegTs::buildJpegXsVideoDescriptor — AMD 3 wire layout") {
        Buffer d;
        REQUIRE(MpegTs::buildJpegXsVideoDescriptor(/*w=*/1920, /*h=*/1080,
                                                    /*frNum=*/30000, /*frDen=*/1001, d)
                        .isOk());
        REQUIRE(d.size() == 26);
        const uint8_t *p = static_cast<const uint8_t *>(d.data());
        CHECK(p[0] == MpegTs::DescriptorTagJxsVideo); // 0x32
        CHECK(p[1] == 24);                            // length
        CHECK(p[2] == 0x00);                          // descriptor_version
        // horizontal_size = 1920 (0x0780)
        CHECK(p[3] == 0x07);
        CHECK(p[4] == 0x80);
        // vertical_size = 1080 (0x0438)
        CHECK(p[5] == 0x04);
        CHECK(p[6] == 0x38);
        // brat = 0
        for (int i = 7; i < 11; ++i) CHECK(p[i] == 0);
        // frat byte 0 = interlace+reserved = 0
        CHECK(p[11] == 0x00);
        // frat byte 1 = frame_rate_den clamped to 1 byte = 0xE9 (1001 % 256 = 233 = 0xE9, then clamped to 0xFF -> 1001 > 255 so clamp to 0xFF)
        CHECK(p[12] == 0xFF);
        // frat bytes 2-3 = frame_rate_num (30000 = 0x7530)
        CHECK(p[13] == 0x75);
        CHECK(p[14] == 0x30);
}
