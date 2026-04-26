/**
 * @file      tests/hevcbitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/hevcbitstream.h>
#include <cstring>
#include <vector>

using namespace promeki;

namespace {

        Buffer::Ptr makeBuffer(const std::vector<uint8_t> &bytes) {
                auto buf = Buffer::Ptr::create(bytes.size());
                if (!bytes.empty()) std::memcpy(buf->data(), bytes.data(), bytes.size());
                buf->setSize(bytes.size());
                return buf;
        }

        BufferView viewOf(const Buffer::Ptr &buf) {
                return BufferView(buf, 0, buf ? buf->size() : 0);
        }

        std::vector<uint8_t> bytesOf(const Buffer::Ptr &buf) {
                std::vector<uint8_t> v;
                if (!buf) return v;
                v.resize(buf->size());
                if (!v.empty()) std::memcpy(v.data(), buf->data(), buf->size());
                return v;
        }

        /**
 * @brief Build a canned HEVC SPS NAL with known profile/level bytes.
 *
 * Layout (15 minimum bytes):
 *   0: NAL header byte 0 = (33 << 1) = 0x42 (SPS)
 *   1: NAL header byte 1 = 0x01 (nuh_temporal_id_plus1 = 1)
 *   2: sps_vps_id=0, sps_max_sub_layers_minus1=0, temporal_id_nesting_flag=1
 *      → (0 << 4) | (0 << 1) | 1 = 0x01
 *   3: profile_space=0, tier_flag=0, profile_idc=1 (Main) → 0x01
 *   4-7: profile_compatibility_flags = 0x60000000 (Main + Main10 compat)
 *   8-13: constraint_indicator_flags = 0x900000000000
 *   14: level_idc = 93 (Level 3.1 = 93)
 */
        std::vector<uint8_t> cannedHevcSps() {
                return {
                        0x42, 0x01,                         // NAL header
                        0x01,                               // sps_vps_id byte
                        0x01,                               // ptl byte 0
                        0x60, 0x00, 0x00, 0x00,             // compat_flags
                        0x90, 0x00, 0x00, 0x00, 0x00, 0x00, // constraint_flags
                        0x5d,                               // level_idc (93)
                        0xaa, 0xbb                          // trailing RBSP (ignored)
                };
        }

        std::vector<uint8_t> cannedHevcVps() {
                // NAL type 32 = 0x40 in bits 1-6 → (32 << 1) = 0x40.
                return {0x40, 0x01, 0x11, 0x22, 0x33};
        }

        std::vector<uint8_t> cannedHevcPps() {
                // NAL type 34 = 0x44.
                return {0x44, 0x01, 0xc1, 0x62};
        }

} // namespace

TEST_CASE("HevcDecoderConfig — extract from Annex-B access unit") {

        SUBCASE("VPS/SPS/PPS captured, profile fields populated from SPS") {
                auto vps = cannedHevcVps();
                auto sps = cannedHevcSps();
                auto pps = cannedHevcPps();

                std::vector<uint8_t> au;
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), vps.begin(), vps.end());
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), sps.begin(), sps.end());
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), pps.begin(), pps.end());
                auto buf = makeBuffer(au);

                HevcDecoderConfig cfg;
                Error             err = HevcDecoderConfig::fromAnnexB(viewOf(buf), cfg);
                CHECK(err == Error::Ok);
                CHECK(cfg.configurationVersion == 1);
                CHECK(cfg.generalProfileSpace == 0);
                CHECK(cfg.generalTierFlag == 0);
                CHECK(cfg.generalProfileIdc == 1);
                CHECK(cfg.generalProfileCompatibilityFlags == 0x60000000u);
                CHECK(cfg.generalConstraintIndicatorFlags == 0x900000000000ull);
                CHECK(cfg.generalLevelIdc == 93);
                CHECK(cfg.numTemporalLayers == 1);
                CHECK(cfg.temporalIdNested == 1);
                REQUIRE(cfg.vps.size() == 1);
                REQUIRE(cfg.sps.size() == 1);
                REQUIRE(cfg.pps.size() == 1);
                CHECK(bytesOf(cfg.vps[0]) == vps);
                CHECK(bytesOf(cfg.sps[0]) == sps);
                CHECK(bytesOf(cfg.pps[0]) == pps);
        }

        SUBCASE("missing SPS returns InvalidArgument") {
                auto                 vps = cannedHevcVps();
                std::vector<uint8_t> au;
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), vps.begin(), vps.end());
                auto              buf = makeBuffer(au);
                HevcDecoderConfig cfg;
                CHECK(HevcDecoderConfig::fromAnnexB(viewOf(buf), cfg) == Error::InvalidArgument);
        }

        SUBCASE("SPS too short for profile fields returns InvalidArgument") {
                // Well-formed SPS NAL header but only 5 bytes of payload —
                // not enough to contain profile_tier_level fixed fields.
                std::vector<uint8_t> au = {0x00, 0x00, 0x00, 0x01, 0x42, 0x01, 0x01, 0x01, 0x00};
                auto                 buf = makeBuffer(au);
                HevcDecoderConfig    cfg;
                CHECK(HevcDecoderConfig::fromAnnexB(viewOf(buf), cfg) == Error::InvalidArgument);
        }
}

TEST_CASE("HevcDecoderConfig — serialize / parse round trip") {

        SUBCASE("VPS+SPS+PPS round-trips and reserved bits are set") {
                HevcDecoderConfig cfg;
                cfg.generalProfileSpace = 0;
                cfg.generalTierFlag = 1;
                cfg.generalProfileIdc = 2; // Main10
                cfg.generalProfileCompatibilityFlags = 0x40000000;
                cfg.generalConstraintIndicatorFlags = 0x800000000000ull;
                cfg.generalLevelIdc = 120; // Level 4.0
                cfg.parallelismType = 0;
                cfg.chromaFormat = 1;
                cfg.bitDepthLumaMinus8 = 2; // 10-bit
                cfg.bitDepthChromaMinus8 = 2;
                cfg.numTemporalLayers = 1;
                cfg.temporalIdNested = 1;
                cfg.lengthSizeMinusOne = 3;
                cfg.vps.pushToBack(makeBuffer({0x40, 0x01, 0xaa, 0xbb}));
                cfg.sps.pushToBack(makeBuffer(cannedHevcSps()));
                cfg.pps.pushToBack(makeBuffer({0x44, 0x01, 0xc1, 0x62}));

                Buffer::Ptr payload;
                CHECK(cfg.serialize(payload) == Error::Ok);
                REQUIRE(payload);

                // The profile byte should combine space(0<<6) | tier(1<<5) |
                // idc(2) → 0x22.
                CHECK(bytesOf(payload)[1] == 0x22);
                // Reserved top bits: byte 13 high nibble = 0xf0, byte 15
                // top 6 bits = 0xfc, byte 16 top 6 = 0xfc, byte 17 top 5 =
                // 0xf8, byte 18 top 5 = 0xf8.
                CHECK((bytesOf(payload)[13] & 0xf0) == 0xf0);
                CHECK((bytesOf(payload)[15] & 0xfc) == 0xfc);
                CHECK((bytesOf(payload)[16] & 0xfc) == 0xfc);
                CHECK((bytesOf(payload)[17] & 0xf8) == 0xf8);
                CHECK((bytesOf(payload)[18] & 0xf8) == 0xf8);

                HevcDecoderConfig parsed;
                CHECK(HevcDecoderConfig::parse(viewOf(payload), parsed) == Error::Ok);
                CHECK(parsed.generalProfileSpace == cfg.generalProfileSpace);
                CHECK(parsed.generalTierFlag == cfg.generalTierFlag);
                CHECK(parsed.generalProfileIdc == cfg.generalProfileIdc);
                CHECK(parsed.generalProfileCompatibilityFlags == cfg.generalProfileCompatibilityFlags);
                CHECK(parsed.generalConstraintIndicatorFlags == cfg.generalConstraintIndicatorFlags);
                CHECK(parsed.generalLevelIdc == cfg.generalLevelIdc);
                CHECK(parsed.chromaFormat == cfg.chromaFormat);
                CHECK(parsed.bitDepthLumaMinus8 == cfg.bitDepthLumaMinus8);
                CHECK(parsed.lengthSizeMinusOne == cfg.lengthSizeMinusOne);
                CHECK(parsed.numTemporalLayers == cfg.numTemporalLayers);
                CHECK(parsed.temporalIdNested == cfg.temporalIdNested);
                REQUIRE(parsed.vps.size() == 1);
                REQUIRE(parsed.sps.size() == 1);
                REQUIRE(parsed.pps.size() == 1);
                CHECK(bytesOf(parsed.vps[0]) == bytesOf(cfg.vps[0]));
                CHECK(bytesOf(parsed.sps[0]) == bytesOf(cfg.sps[0]));
                CHECK(bytesOf(parsed.pps[0]) == bytesOf(cfg.pps[0]));
        }

        SUBCASE("missing-VPS serialize emits only SPS+PPS arrays") {
                HevcDecoderConfig cfg;
                cfg.sps.pushToBack(makeBuffer({0x42, 0x01, 0xaa}));
                cfg.pps.pushToBack(makeBuffer({0x44, 0x01, 0xbb}));
                Buffer::Ptr payload;
                CHECK(cfg.serialize(payload) == Error::Ok);
                // numOfArrays byte (index 22) should be 2.
                CHECK(bytesOf(payload)[22] == 2);

                HevcDecoderConfig parsed;
                CHECK(HevcDecoderConfig::parse(viewOf(payload), parsed) == Error::Ok);
                CHECK(parsed.vps.isEmpty());
                CHECK(parsed.sps.size() == 1);
                CHECK(parsed.pps.size() == 1);
        }

        SUBCASE("truncated payload returns CorruptData") {
                auto              buf = makeBuffer({0x01, 0x22, 0x00});
                HevcDecoderConfig cfg;
                CHECK(HevcDecoderConfig::parse(viewOf(buf), cfg) == Error::CorruptData);
        }

        SUBCASE("truncated NAL in an array returns CorruptData") {
                // Valid fixed header, one array with one NAL declared length
                // 100 but only 5 payload bytes follow.
                std::vector<uint8_t> payload(23, 0);
                payload[0] = 0x01;  // configVersion
                payload[21] = 0x0b; // numTemporalLayers=1, lengthSizeMinusOne=3
                payload[22] = 1;    // numOfArrays
                // Array header: type=33 (SPS), numNalus=1.
                payload.push_back(0x80 | 33);
                payload.push_back(0x00);
                payload.push_back(0x01);
                // NAL length=100, but only 5 bytes of payload.
                payload.push_back(0x00);
                payload.push_back(0x64);
                for (int i = 0; i < 5; ++i) payload.push_back(static_cast<uint8_t>(i));

                auto              buf = makeBuffer(payload);
                HevcDecoderConfig cfg;
                CHECK(HevcDecoderConfig::parse(viewOf(buf), cfg) == Error::CorruptData);
        }
}

TEST_CASE("HevcDecoderConfig::toAnnexB") {
        HevcDecoderConfig cfg;
        cfg.vps.pushToBack(makeBuffer({0x40, 0x01, 0xaa}));
        cfg.sps.pushToBack(makeBuffer({0x42, 0x01, 0xbb}));
        cfg.pps.pushToBack(makeBuffer({0x44, 0x01, 0xcc}));
        Buffer::Ptr annexB;
        CHECK(cfg.toAnnexB(annexB) == Error::Ok);
        std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0xaa, 0x00, 0x00, 0x00, 0x01,
                                         0x42, 0x01, 0xbb, 0x00, 0x00, 0x00, 0x01, 0x44, 0x01, 0xcc};
        CHECK(bytesOf(annexB) == expected);
}
