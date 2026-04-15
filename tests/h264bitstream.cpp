/**
 * @file      tests/h264bitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/h264bitstream.h>
#include <cstring>
#include <vector>

using namespace promeki;

namespace {

/** @brief Builds a Buffer::Ptr containing the given bytes. */
Buffer::Ptr makeBuffer(const std::vector<uint8_t> &bytes) {
        auto buf = Buffer::Ptr::create(bytes.size());
        if(!bytes.empty()) std::memcpy(buf->data(), bytes.data(), bytes.size());
        buf->setSize(bytes.size());
        return buf;
}

/** @brief Wraps a Buffer::Ptr in a BufferView spanning its content. */
BufferView viewOf(const Buffer::Ptr &buf) {
        return BufferView(buf, 0, buf ? buf->size() : 0);
}

/** @brief Returns a vector copy of the bytes in @p buf, up to buf->size(). */
std::vector<uint8_t> bytesOf(const Buffer::Ptr &buf) {
        std::vector<uint8_t> v;
        if(!buf) return v;
        v.resize(buf->size());
        if(!v.empty()) std::memcpy(v.data(), buf->data(), buf->size());
        return v;
}

} // namespace

TEST_CASE("H264Bitstream — Annex-B NAL iteration") {

        SUBCASE("empty input returns Ok with no invocations") {
                BufferView empty;
                int calls = 0;
                Error err = H264Bitstream::forEachAnnexBNal(empty,
                        [&](const H264Bitstream::NalUnit &) { ++calls; return Error::Ok; });
                CHECK(err == Error::Ok);
                CHECK(calls == 0);
        }

        SUBCASE("non-empty input with no start code returns CorruptData") {
                auto buf = makeBuffer({ 0x11, 0x22, 0x33, 0x44 });
                Error err = H264Bitstream::forEachAnnexBNal(viewOf(buf),
                        [&](const H264Bitstream::NalUnit &) { return Error::Ok; });
                CHECK(err == Error::CorruptData);
        }

        SUBCASE("3-byte start codes split payloads correctly") {
                // 00 00 01 | AA BB | 00 00 01 | CC DD EE
                auto buf = makeBuffer({
                        0x00, 0x00, 0x01, 0xaa, 0xbb,
                        0x00, 0x00, 0x01, 0xcc, 0xdd, 0xee
                });
                std::vector<std::vector<uint8_t>> captured;
                Error err = H264Bitstream::forEachAnnexBNal(viewOf(buf),
                        [&](const H264Bitstream::NalUnit &nal) {
                                std::vector<uint8_t> v(nal.view.data(),
                                                       nal.view.data() + nal.view.size());
                                captured.push_back(std::move(v));
                                return Error::Ok;
                        });
                CHECK(err == Error::Ok);
                REQUIRE(captured.size() == 2);
                CHECK(captured[0] == std::vector<uint8_t>({ 0xaa, 0xbb }));
                CHECK(captured[1] == std::vector<uint8_t>({ 0xcc, 0xdd, 0xee }));
        }

        SUBCASE("4-byte start codes split payloads correctly") {
                // 00 00 00 01 | 67 42 C0 | 00 00 00 01 | 68 CE | 00 00 00 01 | 65 88
                auto buf = makeBuffer({
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x01, 0x68, 0xce,
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88
                });
                std::vector<std::vector<uint8_t>> captured;
                std::vector<uint8_t> headers;
                Error err = H264Bitstream::forEachAnnexBNal(viewOf(buf),
                        [&](const H264Bitstream::NalUnit &nal) {
                                std::vector<uint8_t> v(nal.view.data(),
                                                       nal.view.data() + nal.view.size());
                                captured.push_back(std::move(v));
                                headers.push_back(nal.header0);
                                return Error::Ok;
                        });
                CHECK(err == Error::Ok);
                REQUIRE(captured.size() == 3);
                CHECK(captured[0] == std::vector<uint8_t>({ 0x67, 0x42, 0xc0 }));
                CHECK(captured[1] == std::vector<uint8_t>({ 0x68, 0xce }));
                CHECK(captured[2] == std::vector<uint8_t>({ 0x65, 0x88 }));
                CHECK(headers[0] == 0x67);
                CHECK((headers[0] & 0x1f) == 7);  // H.264 SPS
                CHECK((headers[1] & 0x1f) == 8);  // H.264 PPS
                CHECK((headers[2] & 0x1f) == 5);  // H.264 IDR slice
        }

        SUBCASE("mixed 3- and 4-byte start codes both parsed") {
                // 00 00 01 | AA | 00 00 00 01 | BB CC | 00 00 01 | DD
                auto buf = makeBuffer({
                        0x00, 0x00, 0x01, 0xaa,
                        0x00, 0x00, 0x00, 0x01, 0xbb, 0xcc,
                        0x00, 0x00, 0x01, 0xdd
                });
                std::vector<std::vector<uint8_t>> captured;
                Error err = H264Bitstream::forEachAnnexBNal(viewOf(buf),
                        [&](const H264Bitstream::NalUnit &nal) {
                                std::vector<uint8_t> v(nal.view.data(),
                                                       nal.view.data() + nal.view.size());
                                captured.push_back(std::move(v));
                                return Error::Ok;
                        });
                CHECK(err == Error::Ok);
                REQUIRE(captured.size() == 3);
                CHECK(captured[0] == std::vector<uint8_t>({ 0xaa }));
                CHECK(captured[1] == std::vector<uint8_t>({ 0xbb, 0xcc }));
                CHECK(captured[2] == std::vector<uint8_t>({ 0xdd }));
        }

        SUBCASE("visitor can abort iteration by returning non-Ok") {
                auto buf = makeBuffer({
                        0x00, 0x00, 0x01, 0xaa,
                        0x00, 0x00, 0x01, 0xbb,
                        0x00, 0x00, 0x01, 0xcc
                });
                int calls = 0;
                Error err = H264Bitstream::forEachAnnexBNal(viewOf(buf),
                        [&](const H264Bitstream::NalUnit &) {
                                ++calls;
                                if(calls == 2) return Error(Error::Cancelled);
                                return Error(Error::Ok);
                        });
                CHECK(err == Error::Cancelled);
                CHECK(calls == 2);
        }
}

TEST_CASE("H264Bitstream — AVCC NAL iteration") {

        SUBCASE("rejects invalid lenSize") {
                auto buf = makeBuffer({ 0x00 });
                Error err = H264Bitstream::forEachAvccNal(viewOf(buf), 3,
                        [](const H264Bitstream::NalUnit &) { return Error::Ok; });
                CHECK(err == Error::InvalidArgument);
        }

        SUBCASE("walks 4-byte length prefix") {
                // len=2 | AA BB | len=3 | CC DD EE
                auto buf = makeBuffer({
                        0x00, 0x00, 0x00, 0x02, 0xaa, 0xbb,
                        0x00, 0x00, 0x00, 0x03, 0xcc, 0xdd, 0xee
                });
                std::vector<std::vector<uint8_t>> captured;
                Error err = H264Bitstream::forEachAvccNal(viewOf(buf), 4,
                        [&](const H264Bitstream::NalUnit &nal) {
                                std::vector<uint8_t> v(nal.view.data(),
                                                       nal.view.data() + nal.view.size());
                                captured.push_back(std::move(v));
                                return Error::Ok;
                        });
                CHECK(err == Error::Ok);
                REQUIRE(captured.size() == 2);
                CHECK(captured[0] == std::vector<uint8_t>({ 0xaa, 0xbb }));
                CHECK(captured[1] == std::vector<uint8_t>({ 0xcc, 0xdd, 0xee }));
        }

        SUBCASE("walks 2-byte length prefix") {
                auto buf = makeBuffer({ 0x00, 0x03, 0x11, 0x22, 0x33 });
                std::vector<uint8_t> captured;
                Error err = H264Bitstream::forEachAvccNal(viewOf(buf), 2,
                        [&](const H264Bitstream::NalUnit &nal) {
                                captured.assign(nal.view.data(),
                                                nal.view.data() + nal.view.size());
                                return Error::Ok;
                        });
                CHECK(err == Error::Ok);
                CHECK(captured == std::vector<uint8_t>({ 0x11, 0x22, 0x33 }));
        }

        SUBCASE("truncated payload returns CorruptData") {
                // Declares length=10 but only 3 bytes of payload follow.
                auto buf = makeBuffer({ 0x00, 0x00, 0x00, 0x0a, 0x11, 0x22, 0x33 });
                Error err = H264Bitstream::forEachAvccNal(viewOf(buf), 4,
                        [](const H264Bitstream::NalUnit &) { return Error::Ok; });
                CHECK(err == Error::CorruptData);
        }

        SUBCASE("truncated length prefix returns CorruptData") {
                // Only 2 bytes where a 4-byte length prefix is expected.
                auto buf = makeBuffer({ 0x00, 0x00 });
                Error err = H264Bitstream::forEachAvccNal(viewOf(buf), 4,
                        [](const H264Bitstream::NalUnit &) { return Error::Ok; });
                CHECK(err == Error::CorruptData);
        }
}

TEST_CASE("H264Bitstream — Annex-B ↔ AVCC round trip") {

        SUBCASE("Annex-B → AVCC with 4-byte length") {
                auto in = makeBuffer({
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x01, 0x68, 0xce,
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88
                });
                Buffer::Ptr out;
                Error err = H264Bitstream::annexBToAvcc(viewOf(in), 4, out);
                CHECK(err == Error::Ok);
                REQUIRE(out);
                std::vector<uint8_t> expected = {
                        0x00, 0x00, 0x00, 0x03, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x02, 0x68, 0xce,
                        0x00, 0x00, 0x00, 0x02, 0x65, 0x88
                };
                CHECK(bytesOf(out) == expected);
        }

        SUBCASE("AVCC → Annex-B emits 4-byte start codes") {
                auto in = makeBuffer({
                        0x00, 0x00, 0x00, 0x02, 0xaa, 0xbb,
                        0x00, 0x00, 0x00, 0x03, 0xcc, 0xdd, 0xee
                });
                Buffer::Ptr out;
                Error err = H264Bitstream::avccToAnnexB(viewOf(in), 4, out);
                CHECK(err == Error::Ok);
                REQUIRE(out);
                std::vector<uint8_t> expected = {
                        0x00, 0x00, 0x00, 0x01, 0xaa, 0xbb,
                        0x00, 0x00, 0x00, 0x01, 0xcc, 0xdd, 0xee
                };
                CHECK(bytesOf(out) == expected);
        }

        SUBCASE("Annex-B → AVCC → Annex-B is byte-identical (normalized to 4-byte)") {
                // Start with a canonical 4-byte-start-code Annex-B stream so
                // the round trip is bit-exact; the converter normalizes
                // 3-byte start codes to 4-byte on the way back.
                auto original = makeBuffer({
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x01,
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x11, 0x22
                });
                Buffer::Ptr avcc;
                Error err = H264Bitstream::annexBToAvcc(viewOf(original), 4, avcc);
                REQUIRE(err == Error::Ok);
                Buffer::Ptr back;
                err = H264Bitstream::avccToAnnexB(viewOf(avcc), 4, back);
                REQUIRE(err == Error::Ok);
                CHECK(bytesOf(back) == bytesOf(original));
        }

        SUBCASE("emulation-prevention bytes pass through unchanged") {
                // A NAL payload containing the byte triple 00 00 03 — the
                // framing converter must copy it verbatim; it's the higher
                // layer's job to insert/remove those bytes if needed.
                auto in = makeBuffer({
                        0x00, 0x00, 0x00, 0x01,
                        0x67, 0x00, 0x00, 0x03, 0x01, 0x42
                });
                Buffer::Ptr avcc;
                Error err = H264Bitstream::annexBToAvcc(viewOf(in), 4, avcc);
                REQUIRE(err == Error::Ok);
                std::vector<uint8_t> expected = {
                        0x00, 0x00, 0x00, 0x06,
                        0x67, 0x00, 0x00, 0x03, 0x01, 0x42
                };
                CHECK(bytesOf(avcc) == expected);
        }

        SUBCASE("annexBToAvcc rejects NAL larger than lenSize can encode") {
                // A 300-byte NAL does not fit in a 1-byte length prefix.
                std::vector<uint8_t> bytes(4 + 300, 0xaa);
                bytes[0] = 0x00; bytes[1] = 0x00; bytes[2] = 0x00; bytes[3] = 0x01;
                auto in = makeBuffer(bytes);
                Buffer::Ptr out;
                Error err = H264Bitstream::annexBToAvcc(viewOf(in), 1, out);
                CHECK(err == Error::CorruptData);
        }
}

TEST_CASE("AvcDecoderConfig — extract from Annex-B access unit") {

        // Canned SPS: NAL header 0x67 (type=7), profile_idc=0x42 (baseline),
        // profile_compatibility=0xe0 (constraint_set0/1/2), level_idc=0x1e.
        // The bytes after the level are arbitrary payload — the fromAnnexB
        // scanner only reads the three profile bytes directly.
        const std::vector<uint8_t> spsNal = { 0x67, 0x42, 0xe0, 0x1e, 0xaa, 0xbb };
        const std::vector<uint8_t> ppsNal = { 0x68, 0xce, 0x3c, 0x80 };
        const std::vector<uint8_t> idrNal = { 0x65, 0x88, 0x11, 0x22 };

        SUBCASE("parameter sets and profile bytes captured correctly") {
                std::vector<uint8_t> au;
                au.insert(au.end(), { 0x00, 0x00, 0x00, 0x01 });
                au.insert(au.end(), spsNal.begin(), spsNal.end());
                au.insert(au.end(), { 0x00, 0x00, 0x00, 0x01 });
                au.insert(au.end(), ppsNal.begin(), ppsNal.end());
                au.insert(au.end(), { 0x00, 0x00, 0x00, 0x01 });
                au.insert(au.end(), idrNal.begin(), idrNal.end());
                auto buf = makeBuffer(au);

                AvcDecoderConfig cfg;
                Error err = AvcDecoderConfig::fromAnnexB(viewOf(buf), cfg);
                CHECK(err == Error::Ok);
                CHECK(cfg.configurationVersion == 1);
                CHECK(cfg.avcProfileIndication == 0x42);
                CHECK(cfg.profileCompatibility == 0xe0);
                CHECK(cfg.avcLevelIndication   == 0x1e);
                CHECK(cfg.lengthSizeMinusOne   == 3);
                REQUIRE(cfg.sps.size() == 1);
                REQUIRE(cfg.pps.size() == 1);
                CHECK(bytesOf(cfg.sps[0]) == spsNal);
                CHECK(bytesOf(cfg.pps[0]) == ppsNal);
        }

        SUBCASE("no SPS in input returns InvalidArgument") {
                std::vector<uint8_t> au = {
                        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80,
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x11, 0x22
                };
                auto buf = makeBuffer(au);
                AvcDecoderConfig cfg;
                Error err = AvcDecoderConfig::fromAnnexB(viewOf(buf), cfg);
                CHECK(err == Error::InvalidArgument);
        }

        SUBCASE("non-Annex-B input returns CorruptData") {
                auto buf = makeBuffer({ 0x11, 0x22, 0x33 });
                AvcDecoderConfig cfg;
                Error err = AvcDecoderConfig::fromAnnexB(viewOf(buf), cfg);
                CHECK(err == Error::CorruptData);
        }
}

TEST_CASE("AvcDecoderConfig — serialize / parse round trip") {

        SUBCASE("single SPS + single PPS round-trips byte-exact") {
                AvcDecoderConfig cfg;
                cfg.avcProfileIndication = 0x64;  // high profile
                cfg.profileCompatibility = 0x00;
                cfg.avcLevelIndication   = 0x28;  // level 4.0
                cfg.lengthSizeMinusOne   = 3;
                cfg.sps.pushToBack(makeBuffer({ 0x67, 0x64, 0x00, 0x28, 0xac }));
                cfg.pps.pushToBack(makeBuffer({ 0x68, 0xee, 0x3c, 0x80 }));

                Buffer::Ptr payload;
                CHECK(cfg.serialize(payload) == Error::Ok);
                REQUIRE(payload);

                AvcDecoderConfig parsed;
                CHECK(AvcDecoderConfig::parse(viewOf(payload), parsed) == Error::Ok);
                CHECK(parsed.configurationVersion == 1);
                CHECK(parsed.avcProfileIndication == 0x64);
                CHECK(parsed.profileCompatibility == 0x00);
                CHECK(parsed.avcLevelIndication   == 0x28);
                CHECK(parsed.lengthSizeMinusOne   == 3);
                REQUIRE(parsed.sps.size() == 1);
                REQUIRE(parsed.pps.size() == 1);
                CHECK(bytesOf(parsed.sps[0]) == bytesOf(cfg.sps[0]));
                CHECK(bytesOf(parsed.pps[0]) == bytesOf(cfg.pps[0]));
        }

        SUBCASE("multi-SPS / multi-PPS round-trips byte-exact") {
                AvcDecoderConfig cfg;
                cfg.avcProfileIndication = 0x42;
                cfg.profileCompatibility = 0xc0;
                cfg.avcLevelIndication   = 0x1f;
                cfg.sps.pushToBack(makeBuffer({ 0x67, 0x42, 0xc0, 0x1f, 0x01 }));
                cfg.sps.pushToBack(makeBuffer({ 0x67, 0x42, 0xc0, 0x1f, 0x02 }));
                cfg.pps.pushToBack(makeBuffer({ 0x68, 0xce, 0xa1 }));
                cfg.pps.pushToBack(makeBuffer({ 0x68, 0xce, 0xa2 }));
                cfg.pps.pushToBack(makeBuffer({ 0x68, 0xce, 0xa3 }));

                Buffer::Ptr payload;
                CHECK(cfg.serialize(payload) == Error::Ok);

                AvcDecoderConfig parsed;
                CHECK(AvcDecoderConfig::parse(viewOf(payload), parsed) == Error::Ok);
                REQUIRE(parsed.sps.size() == 2);
                REQUIRE(parsed.pps.size() == 3);
                CHECK(bytesOf(parsed.sps[1]) == bytesOf(cfg.sps[1]));
                CHECK(bytesOf(parsed.pps[2]) == bytesOf(cfg.pps[2]));
        }

        SUBCASE("reserved bits in length-size byte set to 1") {
                AvcDecoderConfig cfg;
                cfg.sps.pushToBack(makeBuffer({ 0x67, 0x42, 0xc0, 0x1f }));
                Buffer::Ptr payload;
                CHECK(cfg.serialize(payload) == Error::Ok);
                // Byte 4: top 6 bits reserved = 111111, low 2 bits =
                // lengthSizeMinusOne (3 → 0xff).
                CHECK(bytesOf(payload)[4] == 0xff);
                // Byte 5: top 3 bits reserved = 111, low 5 bits =
                // numOfSequenceParameterSets (1 → 0xe1).
                CHECK(bytesOf(payload)[5] == 0xe1);
        }

        SUBCASE("truncated payload returns CorruptData") {
                auto buf = makeBuffer({ 0x01, 0x42, 0xe0 }); // <6 bytes
                AvcDecoderConfig cfg;
                CHECK(AvcDecoderConfig::parse(viewOf(buf), cfg) == Error::CorruptData);
        }

        SUBCASE("SPS length overrun returns CorruptData") {
                // Declares a 100-byte SPS but provides only a few bytes.
                auto buf = makeBuffer({
                        0x01, 0x42, 0xe0, 0x1e, 0xff, 0xe1,
                        0x00, 0x64,          // spsLen = 100
                        0x67, 0x42, 0xc0     // only 3 bytes actually present
                });
                AvcDecoderConfig cfg;
                CHECK(AvcDecoderConfig::parse(viewOf(buf), cfg) == Error::CorruptData);
        }
}

TEST_CASE("AvcDecoderConfig::toAnnexB") {
        AvcDecoderConfig cfg;
        cfg.sps.pushToBack(makeBuffer({ 0x67, 0x42, 0xe0, 0x1e }));
        cfg.pps.pushToBack(makeBuffer({ 0x68, 0xce, 0x3c, 0x80 }));
        Buffer::Ptr annexB;
        CHECK(cfg.toAnnexB(annexB) == Error::Ok);
        std::vector<uint8_t> expected = {
                0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xe0, 0x1e,
                0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x3c, 0x80
        };
        CHECK(bytesOf(annexB) == expected);
}

TEST_CASE("H264Bitstream::annexBToAvccFiltered") {

        SUBCASE("filter removes SPS and PPS, keeps IDR") {
                // SPS (type 7), PPS (type 8), IDR (type 5) — filter
                // removes parameter sets; only the IDR should survive.
                auto in = makeBuffer({
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, // SPS
                        0x00, 0x00, 0x00, 0x01, 0x68, 0xce,        // PPS
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x11   // IDR
                });
                Buffer::Ptr out;
                auto filter = [](const H264Bitstream::NalUnit &nal) {
                        uint8_t t = nal.header0 & 0x1f;
                        return t != 7 && t != 8; // drop SPS + PPS
                };
                Error err = H264Bitstream::annexBToAvccFiltered(
                        BufferView(in, 0, in->size()), 4, filter, out);
                CHECK(err == Error::Ok);
                REQUIRE(out);
                // Only IDR NAL survives: 4-byte length(3) + 65 88 11
                std::vector<uint8_t> expected = {
                        0x00, 0x00, 0x00, 0x03, 0x65, 0x88, 0x11
                };
                CHECK(bytesOf(out) == expected);
        }

        SUBCASE("keep-all filter is equivalent to annexBToAvcc") {
                auto in = makeBuffer({
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88
                });
                Buffer::Ptr viaFilter;
                Error err = H264Bitstream::annexBToAvccFiltered(
                        BufferView(in, 0, in->size()), 4,
                        [](const H264Bitstream::NalUnit &) { return true; },
                        viaFilter);
                CHECK(err == Error::Ok);

                Buffer::Ptr viaPlain;
                CHECK(H264Bitstream::annexBToAvcc(
                        BufferView(in, 0, in->size()), 4, viaPlain) == Error::Ok);

                CHECK(bytesOf(viaFilter) == bytesOf(viaPlain));
        }

        SUBCASE("drop-all filter produces empty buffer") {
                auto in = makeBuffer({
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x01, 0x65, 0x88
                });
                Buffer::Ptr out;
                Error err = H264Bitstream::annexBToAvccFiltered(
                        BufferView(in, 0, in->size()), 4,
                        [](const H264Bitstream::NalUnit &) { return false; },
                        out);
                CHECK(err == Error::Ok);
                REQUIRE(out);
                CHECK(out->size() == 0);
        }
}

TEST_CASE("H264Bitstream::wrapNalsAsAnnexB") {

        SUBCASE("empty input yields empty output") {
                List<BufferView> empty;
                Buffer::Ptr out;
                Error err = H264Bitstream::wrapNalsAsAnnexB(empty, out);
                CHECK(err == Error::Ok);
                REQUIRE(out);
                CHECK(out->size() == 0);
        }

        SUBCASE("concatenates NALs with 4-byte start codes") {
                auto buf1 = makeBuffer({ 0x67, 0x42, 0xc0 });
                auto buf2 = makeBuffer({ 0x68, 0xce });
                List<BufferView> nals;
                nals.pushToBack(BufferView(buf1, 0, buf1->size()));
                nals.pushToBack(BufferView(buf2, 0, buf2->size()));
                Buffer::Ptr out;
                Error err = H264Bitstream::wrapNalsAsAnnexB(nals, out);
                CHECK(err == Error::Ok);
                std::vector<uint8_t> expected = {
                        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0,
                        0x00, 0x00, 0x00, 0x01, 0x68, 0xce
                };
                CHECK(bytesOf(out) == expected);
        }
}
