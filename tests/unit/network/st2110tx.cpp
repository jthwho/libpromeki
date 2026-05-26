/**
 * @file      st2110tx.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/duration.h>
#include <promeki/enums_rtp.h>
#include <promeki/framerate.h>
#include <promeki/st2110tx.h>

using namespace promeki;

TEST_CASE("St2110Tx::activeRatio: HD 1080/1125 = 0.96") {
        CHECK(St2110Tx::activeRatio(1080, 1125) == doctest::Approx(0.96).epsilon(1e-6));
        CHECK(St2110Tx::activeRatio(720, 750) == doctest::Approx(0.96).epsilon(1e-6));
        CHECK(St2110Tx::activeRatio(486, 525) == doctest::Approx(0.9257143).epsilon(1e-4));
        CHECK(St2110Tx::activeRatio(2160, 2250) == doctest::Approx(0.96).epsilon(1e-6));
        CHECK(St2110Tx::activeRatio(0, 1125) == 0.0);
        CHECK(St2110Tx::activeRatio(1080, 0) == 0.0);
        CHECK(St2110Tx::activeRatio(-1, 1125) == 0.0);
}

TEST_CASE("St2110Tx::trsGapped: 1080p60 narrow PRS interval") {
        // T_FRAME at 60 fps = 16'666'666 ns.  R_ACTIVE = 1080/1125
        // = 0.96.  N_PACKETS for HD-4175 RFC 4175 4:2:2 10-bit ≈
        // 4320 packets/frame at 1200-byte payloads.  T_RS_g =
        // T_FRAME × 0.96 / 4320 ≈ 3.7 µs.
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const double   rActive = 1080.0 / 1125.0;
        Duration       trs = St2110Tx::trsGapped(4320, frame, rActive);
        CHECK(trs.nanoseconds() > 3500);
        CHECK(trs.nanoseconds() < 4000);
}

TEST_CASE("St2110Tx::trsLinear: 1080p60 wide PRS interval") {
        // T_RS_l = T_FRAME / N_PACKETS.  4320 packets / 16.667 ms
        // = 3.86 µs.
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        Duration       trs = St2110Tx::trsLinear(4320, frame);
        CHECK(trs.nanoseconds() > 3700);
        CHECK(trs.nanoseconds() < 4000);
        // Linear is wider than gapped — packets spread across the
        // entire frame including blanking interval.
        const double rActive = 1080.0 / 1125.0;
        CHECK(St2110Tx::trsLinear(4320, frame).nanoseconds() >
              St2110Tx::trsGapped(4320, frame, rActive).nanoseconds());
}

TEST_CASE("St2110Tx::trsGapped: invalid inputs return zero") {
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        CHECK(St2110Tx::trsGapped(0, frame, 0.96) == Duration::zero());
        CHECK(St2110Tx::trsGapped(-1, frame, 0.96) == Duration::zero());
        CHECK(St2110Tx::trsGapped(4320, frame, 0.0) == Duration::zero());
        CHECK(St2110Tx::trsGapped(4320, Duration(), 0.96) == Duration::zero());
}

TEST_CASE("St2110Tx::tvdUtcNs: frame N anchor = anchor + N × T_FRAME") {
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const int64_t  anchor = 1'000'000'000'000LL;
        CHECK(St2110Tx::tvdUtcNs(anchor, 0, frame) == anchor);
        CHECK(St2110Tx::tvdUtcNs(anchor, 1, frame) == anchor + 16'666'667LL);
        CHECK(St2110Tx::tvdUtcNs(anchor, 60, frame) == anchor + 60LL * 16'666'667LL);
}

TEST_CASE("St2110Tx::tvdUtcNs: TR_OFFSET shifts the result") {
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const Duration trOffset = Duration::fromMicroseconds(125);
        const int64_t  anchor = 1'000'000'000'000LL;
        CHECK(St2110Tx::tvdUtcNs(anchor, 0, frame, trOffset) == anchor + 125'000LL);
        CHECK(St2110Tx::tvdUtcNs(anchor, 1, frame, trOffset) ==
              anchor + 16'666'667LL + 125'000LL);
}

TEST_CASE("St2110Tx::tvdUtcNs: invalid anchor returns zero") {
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        CHECK(St2110Tx::tvdUtcNs(0, 0, frame) == 0);
        CHECK(St2110Tx::tvdUtcNs(-1, 0, frame) == 0);
        CHECK(St2110Tx::tvdUtcNs(1'000'000'000'000LL, 0, Duration()) == 0);
}

TEST_CASE("St2110Tx::tprJUtcNs: packet j release = T_VD + j × T_RS") {
        const int64_t  tvd = 1'000'000'000'000LL;
        const Duration trs = Duration::fromNanoseconds(3700);
        CHECK(St2110Tx::tprJUtcNs(tvd, 0, trs) == tvd);
        CHECK(St2110Tx::tprJUtcNs(tvd, 1, trs) == tvd + 3700);
        CHECK(St2110Tx::tprJUtcNs(tvd, 4319, trs) == tvd + 3700LL * 4319);
}

TEST_CASE("St2110Tx::tprJUtcNs: defensive cases") {
        const Duration trs = Duration::fromNanoseconds(3700);
        CHECK(St2110Tx::tprJUtcNs(0, 0, trs) == 0);
        CHECK(St2110Tx::tprJUtcNs(-1, 0, trs) == 0);
        CHECK(St2110Tx::tprJUtcNs(1'000'000'000'000LL, -1, trs) == 0);
        CHECK(St2110Tx::tprJUtcNs(1'000'000'000'000LL, 0, Duration()) == 0);
}

TEST_CASE("St2110Tx::vrxFullNarrowBytes: 1080p60 HD") {
        // ST 2110-21 §7.1 narrow VRX:
        //   VRX_FULL = max(1500 × 8 / MAXUDP,
        //                   N_PACKETS / (27000 × T_FRAME))
        // 1080p60 / 4:2:2 10-bit / MAXUDP = 1460:
        //   Term1 = ceil(1500 × 8 / 1460) = ceil(8.22) = 9
        //   Term2 = 4320 / (27000 × 0.01667) ≈ 9.6 → 9
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const int64_t  vrx = St2110Tx::vrxFullNarrowBytes(4320, 1460, frame);
        CHECK(vrx >= 9);
        CHECK(vrx <= 12);
}

TEST_CASE("St2110Tx::vrxFullWideBytes: 1080p60 HD") {
        // ST 2110-21 §7.1 wide VRX:
        //   VRX_FULL = max(1500 × 720 / MAXUDP,
        //                   N_PACKETS / (300 × T_FRAME))
        // 1080p60 / MAXUDP = 1460:
        //   Term1 = ceil(1500 × 720 / 1460) = ceil(739.7) = 740
        //   Term2 = 4320 / (300 × 0.01667) ≈ 864.0
        // Result is dominated by term2 here.
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const int64_t  vrx = St2110Tx::vrxFullWideBytes(4320, 1460, frame);
        CHECK(vrx >= 740);
        CHECK(vrx <= 870);
}

TEST_CASE("St2110Tx: wide VRX is significantly larger than narrow VRX") {
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        // The wide bound is 90× larger by formula (1500×720 vs
        // 1500×8 in the MAXUDP-bounded term).
        const int64_t narrow = St2110Tx::vrxFullNarrowBytes(4320, 1460, frame);
        const int64_t wide = St2110Tx::vrxFullWideBytes(4320, 1460, frame);
        CHECK(wide > narrow * 50);
}

TEST_CASE("St2110Tx::cmaxNarrowPackets: floors at 4") {
        // CMAX_narrow = max(4, N / (43200 × R_ACTIVE × T_FRAME)).
        // For 1080p60 with N=4320 packets:
        //   rhs = 4320 / (43200 × 0.96 × 0.01667) ≈ 6.25
        // → CMAX = 6.
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const double   r = 1080.0 / 1125.0;
        const int      cmax = St2110Tx::cmaxNarrowPackets(4320, r, frame);
        CHECK(cmax >= 4);
        CHECK(cmax <= 8);
        // Very low packet count → floor of 4 kicks in.
        CHECK(St2110Tx::cmaxNarrowPackets(10, r, frame) == 4);
}

TEST_CASE("St2110Tx::cmaxWidePackets: floors at 16") {
        // CMAX_wide = max(16, N / (21600 × T_FRAME)).
        // 1080p60 N=4320:
        //   rhs = 4320 / (21600 × 0.01667) ≈ 12 → floor of 16 wins.
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const int      cmax = St2110Tx::cmaxWidePackets(4320, frame);
        CHECK(cmax == 16);
        // Very dense stream — N=43200 packets at 1080p60:
        //   rhs = 43200 / (21600 × 0.01667) ≈ 120 → 120 > 16.
        CHECK(St2110Tx::cmaxWidePackets(43200, frame) >= 100);
}

TEST_CASE("St2110Tx::resolveSenderType: pacing mode → sender type") {
        CHECK(St2110Tx::resolveSenderType(RtpPacingMode::TxTime) == RtpSenderType::TypeNL);
        CHECK(St2110Tx::resolveSenderType(RtpPacingMode::KernelFq) == RtpSenderType::TypeW);
        CHECK(St2110Tx::resolveSenderType(RtpPacingMode::Userspace) == RtpSenderType::TypeW);
        CHECK(St2110Tx::resolveSenderType(RtpPacingMode::Auto) == RtpSenderType::TypeW);
        CHECK(St2110Tx::resolveSenderType(RtpPacingMode::None) == RtpSenderType::Unknown);
}

TEST_CASE("St2110Tx::troDefault: narrow and wide forms produce sane values") {
        const Duration frame = Duration::fromNanoseconds(16'666'667);
        const double   r = 1080.0 / 1125.0;
        Duration tNarrow = St2110Tx::troDefault(RtpSenderType::TypeN, 4320, frame, r, 1460);
        Duration tNL = St2110Tx::troDefault(RtpSenderType::TypeNL, 4320, frame, r, 1460);
        Duration tWide = St2110Tx::troDefault(RtpSenderType::TypeW, 4320, frame, r, 1460);
        // Narrow forms agree (both use the §7.4 narrow formula).
        CHECK(tNarrow == tNL);
        // Sub-µs to a few hundred µs is the spec's order of magnitude.
        CHECK(tNarrow.nanoseconds() > 0);
        CHECK(tNarrow.microseconds() < 1000);
        CHECK(tWide.nanoseconds() > 0);
        CHECK(tWide.microseconds() < 5000);
        // Unknown / Auto → zero.
        CHECK(St2110Tx::troDefault(RtpSenderType::Unknown, 4320, frame, r, 1460) ==
              Duration::zero());
        CHECK(St2110Tx::troDefault(RtpSenderType::Auto, 4320, frame, r, 1460) ==
              Duration::zero());
}

TEST_CASE("St2110Tx::tpFmtpValue: enum to wire spelling") {
        CHECK(St2110Tx::tpFmtpValue(RtpSenderType::TypeN) == String("2110TPN"));
        CHECK(St2110Tx::tpFmtpValue(RtpSenderType::TypeNL) == String("2110TPNL"));
        CHECK(St2110Tx::tpFmtpValue(RtpSenderType::TypeW) == String("2110TPW"));
        CHECK(St2110Tx::tpFmtpValue(RtpSenderType::Unknown) == String());
        CHECK(St2110Tx::tpFmtpValue(RtpSenderType::Auto) == String());
}

TEST_CASE("St2110Tx::senderTypeFromTp: wire spelling to enum") {
        CHECK(St2110Tx::senderTypeFromTp(String("2110TPN")) == RtpSenderType::TypeN);
        CHECK(St2110Tx::senderTypeFromTp(String("2110TPNL")) == RtpSenderType::TypeNL);
        CHECK(St2110Tx::senderTypeFromTp(String("2110TPW")) == RtpSenderType::TypeW);
        // Case insensitive — receivers tolerate variant case.
        CHECK(St2110Tx::senderTypeFromTp(String("2110tpn")) == RtpSenderType::TypeN);
        CHECK(St2110Tx::senderTypeFromTp(String("2110tpnl")) == RtpSenderType::TypeNL);
        CHECK(St2110Tx::senderTypeFromTp(String("2110tpw")) == RtpSenderType::TypeW);
        // Unknown / garbage → Unknown.
        CHECK(St2110Tx::senderTypeFromTp(String("")) == RtpSenderType::Unknown);
        CHECK(St2110Tx::senderTypeFromTp(String("2110TPX")) == RtpSenderType::Unknown);
        CHECK(St2110Tx::senderTypeFromTp(String("garbage")) == RtpSenderType::Unknown);
}

TEST_CASE("RtpSenderType: enum value identity and default") {
        CHECK(RtpSenderType::Auto.value() == 0);
        CHECK(RtpSenderType::Unknown.value() == 1);
        CHECK(RtpSenderType::TypeN.value() == 2);
        CHECK(RtpSenderType::TypeNL.value() == 3);
        CHECK(RtpSenderType::TypeW.value() == 4);
        RtpSenderType def;
        CHECK(def == RtpSenderType::Auto);
}

TEST_CASE("RtpSenderType: string round-trip via TypedEnum constructor") {
        const char *names[] = {"Auto", "Unknown", "TypeN", "TypeNL", "TypeW"};
        for (const char *n : names) {
                CAPTURE(n);
                RtpSenderType v{String(n)};
                CHECK(v.hasListedValue());
                CHECK(v.valueName() == String(n));
        }
}
