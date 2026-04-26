/**
 * @file      videoformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <format>
#include <promeki/videoformat.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("VideoFormat: default construction is invalid") {
        VideoFormat vf;
        CHECK_FALSE(vf.isValid());
        CHECK_FALSE(vf.isWellKnownRaster());
        CHECK(vf.toString() == String());
}

TEST_CASE("VideoFormat: construct from well-known raster + rate") {
        VideoFormat vf(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced);
        CHECK(vf.isValid());
        CHECK(vf.raster() == Size2Du32(1920, 1080));
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(vf.videoScanMode() == VideoScanMode::Interlaced);
        CHECK(vf.isWellKnownRaster());
        CHECK(vf.wellKnownRaster() == VideoFormat::Raster_HD);
}

TEST_CASE("VideoFormat: construct from explicit raster") {
        VideoFormat vf(Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_30));
        CHECK(vf.isValid());
        CHECK(vf.videoScanMode() == VideoScanMode::Progressive);
        CHECK(vf.wellKnownRaster() == VideoFormat::Raster_HD);
}

TEST_CASE("VideoFormat: invalid when raster or rate missing") {
        CHECK_FALSE(VideoFormat(Size2Du32(0, 0), FrameRate(FrameRate::FPS_30)).isValid());
        CHECK_FALSE(VideoFormat(Size2Du32(1920, 1080), FrameRate()).isValid());
        CHECK_FALSE(VideoFormat(VideoFormat::Raster_Invalid, FrameRate(FrameRate::FPS_30)).isValid());
}

TEST_CASE("VideoFormat: toString uses SMPTE height form for broadcast rasters") {
        VideoFormat p1080_2997(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97));
        CHECK(p1080_2997.toString() == "1080p29.97");

        VideoFormat i1080_2997(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced);
        CHECK(i1080_2997.toString() == "1080i59.94");

        VideoFormat p720_5994(VideoFormat::Raster_HD720, FrameRate(FrameRate::FPS_59_94));
        CHECK(p720_5994.toString() == "720p59.94");

        VideoFormat uhd60(VideoFormat::Raster_UHD, FrameRate(FrameRate::FPS_60));
        CHECK(uhd60.toString() == "2160p60");

        VideoFormat i1080_25(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_25), VideoScanMode::InterlacedEvenFirst);
        CHECK(i1080_25.toString() == "1080i50");

        VideoFormat ntscI(VideoFormat::Raster_SD525, FrameRate(FrameRate::FPS_29_97),
                          VideoScanMode::InterlacedOddFirst);
        CHECK(ntscI.toString() == "486i59.94");
}

TEST_CASE("VideoFormat: toString uses PsF suffix") {
        VideoFormat psf24(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_24), VideoScanMode::PsF);
        CHECK(psf24.toString() == "1080psf24");

        VideoFormat psf2398(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_23_98), VideoScanMode::PsF);
        CHECK(psf2398.toString() == "1080psf23.98");
}

TEST_CASE("VideoFormat: toString falls back to WxH for non-broadcast rasters") {
        VideoFormat dci2k(VideoFormat::Raster_2K, FrameRate(FrameRate::FPS_24));
        CHECK(dci2k.toString() == "2048x1080p24");

        VideoFormat dci4k(VideoFormat::Raster_4K, FrameRate(FrameRate::FPS_24));
        CHECK(dci4k.toString() == "4096x2160p24");

        VideoFormat odd(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK(odd.toString() == "405x314p30");
}

TEST_CASE("VideoFormat: toString uses named raster when requested") {
        VideoFormat::StringOptions opts;
        opts.useNamedRaster = true;

        VideoFormat hd(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97));
        CHECK(hd.toString(opts) == "HDp29.97");

        VideoFormat uhd(VideoFormat::Raster_UHD, FrameRate(FrameRate::FPS_60));
        CHECK(uhd.toString(opts) == "UHDp60");

        VideoFormat dci2k(VideoFormat::Raster_2K, FrameRate(FrameRate::FPS_24));
        CHECK(dci2k.toString(opts) == "2Kp24");

        // Non-well-known raster still falls back to WxH.
        VideoFormat odd(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK(odd.toString(opts) == "405x314p30");
}

TEST_CASE("VideoFormat: rasterString and frameRateString accessors") {
        VideoFormat vf(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced);
        CHECK(vf.rasterString() == "1080i");
        CHECK(vf.frameRateString() == "59.94");

        VideoFormat p60(VideoFormat::Raster_HD720, FrameRate(FrameRate::FPS_60));
        CHECK(p60.rasterString() == "720p");
        CHECK(p60.frameRateString() == "60");

        VideoFormat::StringOptions opts;
        opts.useNamedRaster = true;
        CHECK(vf.rasterString(opts) == "HDi");
}

TEST_CASE("VideoFormat: fromString parses SMPTE height forms") {
        auto [vf, err] = VideoFormat::fromString("1080i59.94");
        REQUIRE(err.isOk());
        CHECK(vf.isValid());
        CHECK(vf.raster() == Size2Du32(1920, 1080));
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(vf.videoScanMode() == VideoScanMode::Interlaced);

        auto r2 = VideoFormat::fromString("720p59.94");
        REQUIRE(r2.second().isOk());
        CHECK(r2.first().raster() == Size2Du32(1280, 720));
        CHECK(r2.first().frameRate() == FrameRate(FrameRate::FPS_59_94));
        CHECK(r2.first().videoScanMode() == VideoScanMode::Progressive);

        auto r3 = VideoFormat::fromString("2160p60");
        REQUIRE(r3.second().isOk());
        CHECK(r3.first().raster() == Size2Du32(3840, 2160));

        auto r4 = VideoFormat::fromString("1080i50");
        REQUIRE(r4.second().isOk());
        CHECK(r4.first().frameRate() == FrameRate(FrameRate::FPS_25));
}

TEST_CASE("VideoFormat: fromString parses PsF") {
        auto [vf, err] = VideoFormat::fromString("1080psf23.98");
        REQUIRE(err.isOk());
        CHECK(vf.videoScanMode() == VideoScanMode::PsF);
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_23_98));

        auto r2 = VideoFormat::fromString("1080psf24");
        REQUIRE(r2.second().isOk());
        CHECK(r2.first().videoScanMode() == VideoScanMode::PsF);
        CHECK(r2.first().frameRate() == FrameRate(FrameRate::FPS_24));
}

TEST_CASE("VideoFormat: fromString accepts explicit WxH") {
        auto [vf, err] = VideoFormat::fromString("1920x1080p29.97");
        REQUIRE(err.isOk());
        CHECK(vf.raster() == Size2Du32(1920, 1080));
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_29_97));

        auto r2 = VideoFormat::fromString("405x314p30");
        REQUIRE(r2.second().isOk());
        CHECK(r2.first().raster() == Size2Du32(405, 314));
        CHECK(r2.first().videoScanMode() == VideoScanMode::Progressive);
}

TEST_CASE("VideoFormat: fromString accepts well-known names") {
        auto hd = VideoFormat::fromString("HDp29.97");
        REQUIRE(hd.second().isOk());
        CHECK(hd.first().raster() == Size2Du32(1920, 1080));

        auto uhd = VideoFormat::fromString("UHDp60");
        REQUIRE(uhd.second().isOk());
        CHECK(uhd.first().raster() == Size2Du32(3840, 2160));

        auto k2 = VideoFormat::fromString("2Kp24");
        REQUIRE(k2.second().isOk());
        CHECK(k2.first().raster() == Size2Du32(2048, 1080));

        auto ntsc = VideoFormat::fromString("NTSCi59.94");
        REQUIRE(ntsc.second().isOk());
        CHECK(ntsc.first().raster() == Size2Du32(720, 486));
        CHECK(ntsc.first().frameRate() == FrameRate(FrameRate::FPS_29_97));

        auto pal = VideoFormat::fromString("PALi50");
        REQUIRE(pal.second().isOk());
        CHECK(pal.first().raster() == Size2Du32(720, 576));
        CHECK(pal.first().frameRate() == FrameRate(FrameRate::FPS_25));
}

TEST_CASE("VideoFormat: fromString is case-insensitive") {
        auto lower = VideoFormat::fromString("1080i59.94");
        auto upper = VideoFormat::fromString("1080I59.94");
        auto mixed = VideoFormat::fromString("hdP29.97");
        auto psfUp = VideoFormat::fromString("1080PSF24");

        REQUIRE(lower.second().isOk());
        REQUIRE(upper.second().isOk());
        REQUIRE(mixed.second().isOk());
        REQUIRE(psfUp.second().isOk());

        CHECK(lower.first() == upper.first());
        CHECK(mixed.first().raster() == Size2Du32(1920, 1080));
        CHECK(mixed.first().videoScanMode() == VideoScanMode::Progressive);
        CHECK(psfUp.first().videoScanMode() == VideoScanMode::PsF);
}

TEST_CASE("VideoFormat: strict mode always halves interlaced rate") {
        // "1080i29.97" in strict mode → halved 15000/1001, not a well-known rate.
        auto [vf, err] = VideoFormat::fromString("1080i29.97");
        REQUIRE(err.isOk());
        CHECK(vf.videoScanMode() == VideoScanMode::Interlaced);
        CHECK(vf.frameRate().numerator() == 15000);
        CHECK(vf.frameRate().denominator() == 1001);
}

TEST_CASE("VideoFormat: loose mode tolerates interlaced rate as frame rate") {
        VideoFormat::ParseOptions opts;
        opts.strictInterlacedFieldRate = false;

        auto r = VideoFormat::fromString("1080i29.97", opts);
        REQUIRE(r.second().isOk());
        CHECK(r.first().frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(r.first().videoScanMode() == VideoScanMode::Interlaced);

        // Strict SMPTE field rate still parses the same way in loose mode
        // because the halved rate is well-known.
        auto r2 = VideoFormat::fromString("1080i59.94", opts);
        REQUIRE(r2.second().isOk());
        CHECK(r2.first().frameRate() == FrameRate(FrameRate::FPS_29_97));
}

TEST_CASE("VideoFormat: fromString tolerates whitespace and separators") {
        const struct {
                        const char   *input;
                        Size2Du32     raster;
                        FrameRate     rate;
                        VideoScanMode mode;
        } cases[] = {
                {"1920x1080 @ 29.97", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97),
                 VideoScanMode::Progressive},
                {"1080p @ 29.97", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Progressive},
                {"1080 i 59.94", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced},
                {"HD, 29.97", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Progressive},
                {"  720p   60  ", Size2Du32(1280, 720), FrameRate(FrameRate::FPS_60), VideoScanMode::Progressive},
                {"UHD @ 60", Size2Du32(3840, 2160), FrameRate(FrameRate::FPS_60), VideoScanMode::Progressive},
                {"1080\tpsf\t24", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_24), VideoScanMode::PsF},
                {"2048x1080 p 24", Size2Du32(2048, 1080), FrameRate(FrameRate::FPS_24), VideoScanMode::Progressive},
                {"1080 29.97", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Progressive},
                {"1080 23.98p", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_23_98), VideoScanMode::Progressive},
                {"1080 59.94i", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced},
                {"1080 24psf", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_24), VideoScanMode::PsF},
                {"1080 29.97 p", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Progressive},
                {"1080 50 i", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_25), VideoScanMode::Interlaced},
                {"1080 24 psf", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_24), VideoScanMode::PsF},
                {"HD @ 23.98p", Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_23_98), VideoScanMode::Progressive},
        };
        for (const auto &c : cases) {
                CAPTURE(c.input);
                auto [vf, err] = VideoFormat::fromString(c.input);
                REQUIRE(err.isOk());
                CHECK(vf.raster() == c.raster);
                CHECK(vf.frameRate() == c.rate);
                CHECK(vf.videoScanMode() == c.mode);
        }
}

TEST_CASE("VideoFormat: fromString rejects malformed input") {
        CHECK(VideoFormat::fromString("").second().isError());
        CHECK(VideoFormat::fromString("nonsense").second().isError());
        CHECK(VideoFormat::fromString("1080").second().isError());
        CHECK(VideoFormat::fromString("p29.97").second().isError());
        CHECK(VideoFormat::fromString("1080x").second().isError());
        CHECK(VideoFormat::fromString("1080p").second().isError());
}

TEST_CASE("VideoFormat: round-trip preserves canonical forms") {
        const char *inputs[] = {
                "1080i59.94", "1080p29.97", "720p59.94",    "2160p60",      "1080psf23.98",
                "486i59.94",  "576i50",     "2048x1080p24", "4096x2160p24",
        };
        for (const char *s : inputs) {
                auto [vf, err] = VideoFormat::fromString(s);
                CAPTURE(s);
                REQUIRE(err.isOk());
                CHECK(vf.toString() == s);
        }
}

TEST_CASE("VideoFormat: equality") {
        VideoFormat a(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced);
        VideoFormat b(Size2Du32(1920, 1080), FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced);
        VideoFormat c(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::Progressive);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("VideoFormat: construct from WellKnownFormat") {
        VideoFormat vf(VideoFormat::Smpte1080p29_97);
        CHECK(vf.isValid());
        CHECK(vf.raster() == Size2Du32(1920, 1080));
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(vf.videoScanMode() == VideoScanMode::Progressive);
        CHECK(vf.toString() == "1080p29.97");

        VideoFormat i1080(VideoFormat::Smpte1080i59_94);
        CHECK(i1080.videoScanMode() == VideoScanMode::Interlaced);
        CHECK(i1080.frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(i1080.toString() == "1080i59.94");

        VideoFormat psf(VideoFormat::Smpte1080psf23_98);
        CHECK(psf.videoScanMode() == VideoScanMode::PsF);
        CHECK(psf.toString() == "1080psf23.98");

        VideoFormat dci(VideoFormat::Dci4Kp24);
        CHECK(dci.raster() == Size2Du32(4096, 2160));
        CHECK(dci.frameRate() == FrameRate(FrameRate::FPS_24));
}

TEST_CASE("VideoFormat: WellKnownFormat sentinels construct invalid VideoFormat") {
        CHECK_FALSE(VideoFormat(VideoFormat::Invalid).isValid());
        CHECK_FALSE(VideoFormat(VideoFormat::NotWellKnown).isValid());
}

TEST_CASE("VideoFormat: wellKnownFormat round-trip") {
        VideoFormat a(VideoFormat::Smpte1080p29_97);
        CHECK(a.wellKnownFormat() == VideoFormat::Smpte1080p29_97);
        CHECK(a.isWellKnownFormat());

        VideoFormat b(VideoFormat::Smpte2160p60);
        CHECK(b.wellKnownFormat() == VideoFormat::Smpte2160p60);

        VideoFormat c(VideoFormat::Dci2Kp24);
        CHECK(c.wellKnownFormat() == VideoFormat::Dci2Kp24);

        // Default-constructed → Invalid.
        VideoFormat d;
        CHECK(d.wellKnownFormat() == VideoFormat::Invalid);
        CHECK_FALSE(d.isWellKnownFormat());

        // Valid but unknown raster/rate combo → NotWellKnown.
        VideoFormat e(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK(e.wellKnownFormat() == VideoFormat::NotWellKnown);
        CHECK_FALSE(e.isWellKnownFormat());

        // Well-known raster + non-well-known rate → NotWellKnown.
        VideoFormat f(VideoFormat::Raster_HD, FrameRate(FrameRate::RationalType(15, 1)));
        CHECK(f.wellKnownFormat() == VideoFormat::NotWellKnown);
}

TEST_CASE("VideoFormat: wellKnownFormat treats interlaced variants as equivalent") {
        VideoFormat even(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::InterlacedEvenFirst);
        VideoFormat odd(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::InterlacedOddFirst);
        CHECK(even.wellKnownFormat() == VideoFormat::Smpte1080i59_94);
        CHECK(odd.wellKnownFormat() == VideoFormat::Smpte1080i59_94);
}

TEST_CASE("VideoFormat: comparison with WellKnownFormat enum") {
        VideoFormat hd1080p2997(VideoFormat::Smpte1080p29_97);
        CHECK(hd1080p2997 == VideoFormat::Smpte1080p29_97);
        CHECK(VideoFormat::Smpte1080p29_97 == hd1080p2997);
        CHECK(hd1080p2997 != VideoFormat::Smpte1080p30);
        CHECK(VideoFormat::Smpte1080p30 != hd1080p2997);

        VideoFormat invalid;
        CHECK(invalid == VideoFormat::Invalid);
        CHECK(invalid != VideoFormat::Smpte1080p29_97);

        VideoFormat oddball(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK(oddball == VideoFormat::NotWellKnown);
}

TEST_CASE("VideoFormat: format flag queries") {
        VideoFormat ntscSD(VideoFormat::Smpte486i59_94);
        CHECK(ntscSD.isSmpteFormat());
        CHECK(ntscSD.isNtscFormat());
        CHECK(ntscSD.isSdFormat());
        CHECK_FALSE(ntscSD.isPalFormat());
        CHECK_FALSE(ntscSD.isHdFormat());
        CHECK_FALSE(ntscSD.isUhdFormat());
        CHECK_FALSE(ntscSD.isDciFormat());

        VideoFormat palSD(VideoFormat::Smpte576i50);
        CHECK(palSD.isSmpteFormat());
        CHECK(palSD.isPalFormat());
        CHECK(palSD.isSdFormat());
        CHECK_FALSE(palSD.isNtscFormat());

        VideoFormat hd60(VideoFormat::Smpte1080p60);
        CHECK(hd60.isSmpteFormat());
        CHECK(hd60.isHdFormat());
        CHECK_FALSE(hd60.isNtscFormat());
        CHECK_FALSE(hd60.isPalFormat());

        VideoFormat uhd2997(VideoFormat::Smpte2160p29_97);
        CHECK(uhd2997.isSmpteFormat());
        CHECK(uhd2997.isNtscFormat());
        CHECK(uhd2997.isUhdFormat());
        CHECK_FALSE(uhd2997.isHdFormat());
        CHECK_FALSE(uhd2997.isUhd8kFormat());

        VideoFormat eightK(VideoFormat::Smpte4320p60);
        CHECK(eightK.isSmpteFormat());
        CHECK(eightK.isUhd8kFormat());
        CHECK_FALSE(eightK.isUhdFormat());

        VideoFormat dci(VideoFormat::Dci4Kp24);
        CHECK(dci.isDciFormat());
        CHECK_FALSE(dci.isSmpteFormat());

        // Not-well-known → all flag queries false.
        VideoFormat oddball(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK(oddball.wellKnownFormatFlags() == 0u);
        CHECK_FALSE(oddball.isSmpteFormat());
        CHECK_FALSE(oddball.isHdFormat());

        // Invalid → all flag queries false.
        VideoFormat invalid;
        CHECK(invalid.wellKnownFormatFlags() == 0u);
}

TEST_CASE("VideoFormat: formatFlags static accessor") {
        const uint32_t hdFlags = VideoFormat::formatFlags(VideoFormat::Smpte1080p29_97);
        CHECK((hdFlags & VideoFormat::FormatFlag_Smpte) != 0u);
        CHECK((hdFlags & VideoFormat::FormatFlag_Ntsc) != 0u);
        CHECK((hdFlags & VideoFormat::FormatFlag_Hd) != 0u);
        CHECK((hdFlags & VideoFormat::FormatFlag_Pal) == 0u);

        CHECK(VideoFormat::formatFlags(VideoFormat::Invalid) == 0u);
        CHECK(VideoFormat::formatFlags(VideoFormat::NotWellKnown) == 0u);
}

TEST_CASE("VideoFormat: std::format default style") {
        VideoFormat vf(VideoFormat::Smpte1080p29_97);
        CHECK(std::format("{}", vf) == "1080p29.97");
        CHECK(std::format("{:smpte}", vf) == "1080p29.97");

        VideoFormat i1080(VideoFormat::Smpte1080i59_94);
        CHECK(std::format("{}", i1080) == "1080i59.94");

        VideoFormat dci(VideoFormat::Dci2Kp24);
        CHECK(std::format("{}", dci) == "2048x1080p24");
}

TEST_CASE("VideoFormat: std::format named-raster style") {
        VideoFormat hd(VideoFormat::Smpte1080p29_97);
        CHECK(std::format("{:named}", hd) == "HDp29.97");

        VideoFormat uhd(VideoFormat::Smpte2160p60);
        CHECK(std::format("{:named}", uhd) == "UHDp60");

        VideoFormat dci(VideoFormat::Dci2Kp24);
        CHECK(std::format("{:named}", dci) == "2Kp24");

        // Non-well-known raster still falls back to WxH.
        VideoFormat odd(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK(std::format("{:named}", odd) == "405x314p30");
}

TEST_CASE("VideoFormat: std::format style plus width/alignment") {
        VideoFormat hd(VideoFormat::Smpte1080p29_97);
        CHECK(std::format("{:>16}", hd) == "      1080p29.97");
        CHECK(std::format("{:smpte:>16}", hd) == "      1080p29.97");
        CHECK(std::format("{:named:>16}", hd) == "        HDp29.97");
        CHECK(std::format("{:named:*<12}", hd) == "HDp29.97****");
}

TEST_CASE("VideoFormat: fromString accepts WellKnownFormat identifier") {
        auto r = VideoFormat::fromString("Smpte1080p29_97");
        REQUIRE(r.second().isOk());
        CHECK(r.first() == VideoFormat::Smpte1080p29_97);

        // Case-insensitive, matches other enum names.
        auto r2 = VideoFormat::fromString("smpte1080i59_94");
        REQUIRE(r2.second().isOk());
        CHECK(r2.first() == VideoFormat::Smpte1080i59_94);

        auto r3 = VideoFormat::fromString("DCI4KP24");
        REQUIRE(r3.second().isOk());
        CHECK(r3.first() == VideoFormat::Dci4Kp24);

        auto r4 = VideoFormat::fromString("Smpte1080psf23_98");
        REQUIRE(r4.second().isOk());
        CHECK(r4.first().videoScanMode() == VideoScanMode::PsF);
        CHECK(r4.first() == VideoFormat::Smpte1080psf23_98);
}

TEST_CASE("VideoFormat: allWellKnownFormats returns every entry and filters by flags") {
        VideoFormat::WellKnownFormatList all = VideoFormat::allWellKnownFormats();
        CHECK(all.size() == 49);
        CHECK(all.front() == VideoFormat::Smpte486i59_94);
        CHECK(all.back() == VideoFormat::Dci4Kp60);

        VideoFormat::WellKnownFormatList dci = VideoFormat::allWellKnownFormats(VideoFormat::FormatFlag_Dci);
        CHECK(dci.size() == 12);
        for (VideoFormat::WellKnownFormat f : dci) {
                VideoFormat vf(f);
                CHECK(vf.isDciFormat());
                CHECK_FALSE(vf.isSmpteFormat());
        }

        VideoFormat::WellKnownFormatList palHd =
                VideoFormat::allWellKnownFormats(VideoFormat::FormatFlag_Pal | VideoFormat::FormatFlag_Hd);
        CHECK_FALSE(palHd.isEmpty());
        for (VideoFormat::WellKnownFormat f : palHd) {
                VideoFormat vf(f);
                CHECK(vf.isPalFormat());
                CHECK(vf.isHdFormat());
        }

        VideoFormat::WellKnownFormatList uhd8k = VideoFormat::allWellKnownFormats(VideoFormat::FormatFlag_Uhd8k);
        CHECK(uhd8k.size() == 8);
}

TEST_CASE("VideoFormat: broadcast / cinema / integer-cadence queries") {
        VideoFormat hd(VideoFormat::Smpte1080p29_97);
        CHECK(hd.isBroadcastFormat());
        CHECK_FALSE(hd.isCinemaFormat());
        CHECK_FALSE(hd.isIntegerCadence());

        VideoFormat uhd60(VideoFormat::Smpte2160p60);
        CHECK(uhd60.isBroadcastFormat());
        CHECK(uhd60.isIntegerCadence());

        VideoFormat dci(VideoFormat::Dci4Kp24);
        CHECK(dci.isCinemaFormat());
        CHECK_FALSE(dci.isBroadcastFormat());
        CHECK(dci.isIntegerCadence());

        // Non-well-known but valid — broadcast/cinema both false, but
        // isIntegerCadence still works on the raw rate.
        VideoFormat oddInt(Size2Du32(405, 314), FrameRate(FrameRate::FPS_30));
        CHECK_FALSE(oddInt.isBroadcastFormat());
        CHECK_FALSE(oddInt.isCinemaFormat());
        CHECK(oddInt.isIntegerCadence());

        VideoFormat oddFrac(Size2Du32(405, 314), FrameRate(FrameRate::FPS_29_97));
        CHECK_FALSE(oddFrac.isIntegerCadence());

        // Default-constructed → all false.
        VideoFormat invalid;
        CHECK_FALSE(invalid.isBroadcastFormat());
        CHECK_FALSE(invalid.isCinemaFormat());
        CHECK_FALSE(invalid.isIntegerCadence());
}

TEST_CASE("VideoFormat: Variant round-trip via String") {
        VideoFormat vf(VideoFormat::Raster_HD, FrameRate(FrameRate::FPS_29_97), VideoScanMode::Interlaced);
        Variant     v(vf);
        CHECK(v.type() == Variant::TypeVideoFormat);
        CHECK(v.get<String>() == "1080i59.94");

        Variant     s(String("720p59.94"));
        Error       err;
        VideoFormat parsed = s.get<VideoFormat>(&err);
        CHECK(err.isOk());
        CHECK(parsed.raster() == Size2Du32(1280, 720));
        CHECK(parsed.frameRate() == FrameRate(FrameRate::FPS_59_94));
}
