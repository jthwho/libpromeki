/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pixelformat.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>

using namespace promeki;

// ============================================================================
// Default / Invalid
// ============================================================================

TEST_CASE("PixelFormat: default constructs to Invalid") {
        PixelFormat pd;
        CHECK_FALSE(pd.isValid());
        CHECK(pd.id() == PixelFormat::Invalid);
}

// ============================================================================
// RGBA8
// ============================================================================

TEST_CASE("PixelFormat: RGBA8 is valid") {
        PixelFormat pd(PixelFormat::RGBA8_sRGB);
        CHECK(pd.isValid());
        CHECK(pd.memLayout().id() == PixelMemLayout::I_4x8);
        CHECK(pd.hasAlpha());
        CHECK_FALSE(pd.isCompressed());
        CHECK(pd.compCount() == 4);
        CHECK(pd.planeCount() == 1);
}

// ============================================================================
// RGB8
// ============================================================================

TEST_CASE("PixelFormat: RGB8 is valid") {
        PixelFormat pd(PixelFormat::RGB8_sRGB);
        CHECK(pd.isValid());
        CHECK_FALSE(pd.hasAlpha());
        CHECK(pd.compCount() == 3);
}

// ============================================================================
// YUV8_422 (YUYV)
// ============================================================================

TEST_CASE("PixelFormat: YUV8_422 is valid") {
        PixelFormat pd(PixelFormat::YUV8_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK_FALSE(pd.hasAlpha());
}

TEST_CASE("PixelFormat: YUV8_422 YUYV has YUY2 and YUYV FourCCs") {
        PixelFormat pd(PixelFormat::YUV8_422_Rec709);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("YUY2"));
        CHECK(pd.fourccList()[1] == FourCC("YUYV"));
}

// ============================================================================
// JPEG compressed formats
// ============================================================================

TEST_CASE("PixelFormat: JPEG_RGBA8 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_RGBA8_sRGB);
        CHECK(pd.isCompressed());
        CHECK(pd.hasAlpha());
}

TEST_CASE("PixelFormat: JPEG_RGB8 videoCodec resolves to VideoCodec::JPEG") {
        PixelFormat pd(PixelFormat::JPEG_RGB8_sRGB);
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG);
        CHECK(pd.videoCodec().name() == "JPEG");
}

TEST_CASE("PixelFormat: JPEG_RGBA8 encodeSources and decodeTargets") {
        PixelFormat pd(PixelFormat::JPEG_RGBA8_sRGB);
        CHECK(pd.encodeSources().size() == 1);
        CHECK(pd.encodeSources()[0] == PixelFormat::RGBA8_sRGB);
        CHECK(pd.decodeTargets().size() == 1);
        CHECK(pd.decodeTargets()[0] == PixelFormat::RGBA8_sRGB);
}

TEST_CASE("PixelFormat: JPEG_RGB8 encodeSources is strictly RGB8") {
        // Only the natural RGB family is listed — see
        // JpegVideoEncoder which tags the output based on the
        // input component order.  Mixed-family inputs CSC through
        // Image::convert() before hitting the codec.
        PixelFormat pd(PixelFormat::JPEG_RGB8_sRGB);
        REQUIRE(pd.encodeSources().size() == 1);
        CHECK(pd.encodeSources()[0] == PixelFormat::RGB8_sRGB);
}

TEST_CASE("PixelFormat: JPEG_YUV8_422 encodeSources and decodeTargets") {
        PixelFormat pd(PixelFormat::JPEG_YUV8_422_Rec709);
        // Only the natural YUV 4:2:2 family — RGB inputs CSC through
        // Image::convert() before hitting the codec.
        REQUIRE(pd.encodeSources().size() == 3);
        CHECK(pd.encodeSources().contains(PixelFormat::YUV8_422_Rec709));
        CHECK(pd.encodeSources().contains(PixelFormat::YUV8_422_UYVY_Rec709));
        CHECK(pd.encodeSources().contains(PixelFormat::YUV8_422_Planar_Rec709));
        CHECK(pd.decodeTargets().size() == 5);
        CHECK(pd.decodeTargets().contains(PixelFormat::YUV8_422_Rec709));
        CHECK(pd.decodeTargets().contains(PixelFormat::YUV8_422_UYVY_Rec709));
        CHECK(pd.decodeTargets().contains(PixelFormat::YUV8_422_Planar_Rec709));
        CHECK(pd.decodeTargets().contains(PixelFormat::RGB8_sRGB));
        CHECK(pd.decodeTargets().contains(PixelFormat::RGBA8_sRGB));
}

TEST_CASE("PixelFormat: JPEG YCbCr complement — matrix × range grid") {
        // The library offers all 8 combinations of
        // subsampling (4:2:2 / 4:2:0) × matrix (Rec.709 / Rec.601) ×
        // range (limited / full) as first-class JPEG PixelFormats.
        // The unsuffixed names are limited-range (matching the
        // library-wide YCbCr default), and "_Full" is the explicit
        // full-range opt-in.  For strict JFIF interop (ffplay,
        // browsers, libjpeg-turbo's own decode path) use the
        // Rec.601 _Full variants.

        struct Case {
                PixelFormat::ID id;
                bool          limitedRange;
                bool          rec709;
                bool          is420;
        };
        const Case cases[] = {
                // Limited-range entries.
                { PixelFormat::JPEG_YUV8_422_Rec709,       true,  true,  false },
                { PixelFormat::JPEG_YUV8_420_Rec709,       true,  true,  true  },
                { PixelFormat::JPEG_YUV8_422_Rec601,       true,  false, false },
                { PixelFormat::JPEG_YUV8_420_Rec601,       true,  false, true  },
                // Full-range entries.
                { PixelFormat::JPEG_YUV8_422_Rec709_Full,  false, true,  false },
                { PixelFormat::JPEG_YUV8_420_Rec709_Full,  false, true,  true  },
                { PixelFormat::JPEG_YUV8_422_Rec601_Full,  false, false, false },
                { PixelFormat::JPEG_YUV8_420_Rec601_Full,  false, false, true  },
        };
        for(const auto &c : cases) {
                PixelFormat pd(c.id);
                CAPTURE(pd.name());
                REQUIRE(pd.isValid());
                CHECK(pd.isCompressed());
                CHECK(pd.videoCodec().id() == VideoCodec::JPEG);
                const ColorModel::ID expectedModel = c.rec709
                        ? ColorModel::YCbCr_Rec709
                        : ColorModel::YCbCr_Rec601;
                CHECK(pd.colorModel().id() == expectedModel);
                if(c.limitedRange) {
                        CHECK(pd.compSemantic(0).rangeMin == 16);
                        CHECK(pd.compSemantic(0).rangeMax == 235);
                        CHECK(pd.compSemantic(1).rangeMin == 16);
                        CHECK(pd.compSemantic(1).rangeMax == 240);
                        CHECK(pd.compSemantic(2).rangeMin == 16);
                        CHECK(pd.compSemantic(2).rangeMax == 240);
                } else {
                        CHECK(pd.compSemantic(0).rangeMin == 0);
                        CHECK(pd.compSemantic(0).rangeMax == 255);
                        CHECK(pd.compSemantic(1).rangeMin == 0);
                        CHECK(pd.compSemantic(1).rangeMax == 255);
                        CHECK(pd.compSemantic(2).rangeMin == 0);
                        CHECK(pd.compSemantic(2).rangeMax == 255);
                }
        }
}

TEST_CASE("PixelFormat: videoRange auto-derives from compSemantics") {
        // Well-known PixelFormats don't set Data::videoRange explicitly
        // yet, so all of these exercise the auto-derivation path in
        // PixelFormat::registerData.  The shape of the inference is:
        //   rangeMin > 0          → Limited
        //   rangeMin==0 && max==(2^N-1) → Full
        // Anything else stays Unknown.

        SUBCASE("RGB full-range 8-bit") {
                PixelFormat pd(PixelFormat::RGBA8_sRGB);
                CHECK(pd.videoRange() == VideoRange::Full);
        }
        SUBCASE("RGB full-range 10-bit") {
                PixelFormat pd(PixelFormat::RGB10_LE_sRGB);
                CHECK(pd.videoRange() == VideoRange::Full);
        }
        SUBCASE("YCbCr limited 8-bit") {
                PixelFormat pd(PixelFormat::YUV8_422_Rec709);
                CHECK(pd.videoRange() == VideoRange::Limited);
        }
        SUBCASE("YCbCr limited 10-bit") {
                PixelFormat pd(PixelFormat::YUV10_422_Rec709);
                CHECK(pd.videoRange() == VideoRange::Limited);
        }
        SUBCASE("YCbCr full-range intermediate") {
                PixelFormat pd(PixelFormat::YUV8_422_Rec709_Full);
                CHECK(pd.videoRange() == VideoRange::Full);
        }
}

TEST_CASE("PixelFormat: full-range uncompressed YCbCr intermediates") {
        // The encode-source intermediates for the full-range JPEG
        // variants live as first-class PixelFormats so Image::convert
        // can CSC into them before the codec copies bytes verbatim
        // into the JFIF bitstream.
        const PixelFormat::ID ids[] = {
                PixelFormat::YUV8_422_Rec709_Full,
                PixelFormat::YUV8_422_Rec601_Full,
                PixelFormat::YUV8_420_Planar_Rec709_Full,
                PixelFormat::YUV8_420_Planar_Rec601_Full,
        };
        for(PixelFormat::ID id : ids) {
                PixelFormat pd(id);
                CAPTURE(pd.name());
                REQUIRE(pd.isValid());
                CHECK_FALSE(pd.isCompressed());
                // Full-range semantics: every channel spans 0..255.
                CHECK(pd.compSemantic(0).rangeMin == 0);
                CHECK(pd.compSemantic(0).rangeMax == 255);
                CHECK(pd.compSemantic(1).rangeMin == 0);
                CHECK(pd.compSemantic(1).rangeMax == 255);
                CHECK(pd.compSemantic(2).rangeMin == 0);
                CHECK(pd.compSemantic(2).rangeMax == 255);
        }
}

// ============================================================================
// Video codec compressed formats (QuickTime / MP4 family)
// ============================================================================

TEST_CASE("PixelFormat: H264 is compressed with avc1 FourCC") {
        PixelFormat pd(PixelFormat::H264);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::H264);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("avc1"));
        CHECK(pd.fourccList()[1] == FourCC("avc3"));
}

TEST_CASE("PixelFormat: HEVC is compressed with hvc1 FourCC") {
        PixelFormat pd(PixelFormat::HEVC);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::HEVC);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("hvc1"));
        CHECK(pd.fourccList()[1] == FourCC("hev1"));
}

TEST_CASE("PixelFormat: ProRes 422 family has correct FourCCs") {
        // The string "prores" was shared across all six PixelFormat
        // entries before we landed the typed VideoCodec registry;
        // each variant now has its own VideoCodec ID.  The table
        // lists the FourCC + the matching VideoCodec ID per variant
        // and we verify each one resolves through PixelFormat::videoCodec().
        struct Entry {
                PixelFormat::ID  pdId;
                const char    *fourcc;
                VideoCodec::ID vc;
        };
        Entry entries[] = {
                { PixelFormat::ProRes_422_Proxy, "apco", VideoCodec::ProRes_422_Proxy },
                { PixelFormat::ProRes_422_LT,    "apcs", VideoCodec::ProRes_422_LT    },
                { PixelFormat::ProRes_422,       "apcn", VideoCodec::ProRes_422       },
                { PixelFormat::ProRes_422_HQ,    "apch", VideoCodec::ProRes_422_HQ    }
        };
        for(const auto &e : entries) {
                PixelFormat pd(e.pdId);
                CHECK(pd.isValid());
                CHECK(pd.isCompressed());
                CHECK(pd.videoCodec().id() == e.vc);
                CHECK_FALSE(pd.hasAlpha());
                REQUIRE(pd.fourccList().size() == 1);
                CHECK(pd.fourccList()[0] == FourCC(e.fourcc[0], e.fourcc[1], e.fourcc[2], e.fourcc[3]));
                CHECK(pd.memLayout().id() == PixelMemLayout::P_422_3x10_LE);
        }
}

TEST_CASE("PixelFormat: ProRes 4444 has alpha and 10-bit") {
        PixelFormat pd(PixelFormat::ProRes_4444);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::ProRes_4444);
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
        REQUIRE(pd.fourccList().size() == 1);
        CHECK(pd.fourccList()[0] == FourCC("ap4h"));
        CHECK(pd.memLayout().id() == PixelMemLayout::I_4x10_LE);
}

TEST_CASE("PixelFormat: ProRes 4444 XQ has alpha and 12-bit") {
        PixelFormat pd(PixelFormat::ProRes_4444_XQ);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::ProRes_4444_XQ);
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
        REQUIRE(pd.fourccList().size() == 1);
        CHECK(pd.fourccList()[0] == FourCC("ap4x"));
        CHECK(pd.memLayout().id() == PixelMemLayout::I_4x12_LE);
}

TEST_CASE("PixelFormat: new codec entries are each uniquely lookupable by name") {
        const char *names[] = {
                "H264", "HEVC",
                "ProRes_422_Proxy", "ProRes_422_LT", "ProRes_422", "ProRes_422_HQ",
                "ProRes_4444", "ProRes_4444_XQ"
        };
        for(const char *name : names) {
                PixelFormat pd = PixelFormat::lookup(name);
                CHECK(pd.isValid());
                CHECK(pd.name() == name);
        }
}

// ============================================================================
// lineStride and planeSize delegation
// ============================================================================

TEST_CASE("PixelFormat: RGBA8 lineStride via ImageDesc") {
        ImageDesc desc(1920, 1080, PixelFormat::RGBA8_sRGB);
        PixelFormat pd(PixelFormat::RGBA8_sRGB);
        CHECK(pd.lineStride(0, desc) == 1920 * 4);
}

TEST_CASE("PixelFormat: JPEG planeSize reads CompressedSize from metadata") {
        ImageDesc desc(640, 480, PixelFormat::JPEG_RGB8_sRGB);
        desc.metadata().set(Metadata::CompressedSize, 12345);
        PixelFormat pd(PixelFormat::JPEG_RGB8_sRGB);
        CHECK(pd.planeSize(0, desc) == 12345);
}

// ============================================================================
// Lookup and equality
// ============================================================================

TEST_CASE("PixelFormat: lookup by name") {
        PixelFormat pd(PixelFormat::RGBA8_sRGB);
        PixelFormat found = PixelFormat::lookup(pd.name());
        CHECK(found == pd);
}

TEST_CASE("PixelFormat: equality") {
        PixelFormat a(PixelFormat::RGBA8_sRGB);
        PixelFormat b(PixelFormat::RGBA8_sRGB);
        PixelFormat c(PixelFormat::RGB8_sRGB);
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// UYVY 8-bit
// ============================================================================

TEST_CASE("PixelFormat: YUV8_422_UYVY is valid") {
        PixelFormat pd(PixelFormat::YUV8_422_UYVY_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.memLayout().id() == PixelMemLayout::I_422_UYVY_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("PixelFormat: YUV8_422_UYVY has both 2vuy and UYVY FourCCs") {
        PixelFormat pd(PixelFormat::YUV8_422_UYVY_Rec709);
        // QuickTime canonical name comes first (the writer preference).
        REQUIRE(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("2vuy"));
        CHECK(pd.fourccList()[1] == FourCC("UYVY"));
}

TEST_CASE("PixelFormat: YUV8_422_UYVY limited range") {
        PixelFormat pd(PixelFormat::YUV8_422_UYVY_Rec709);
        CHECK(pd.compSemantic(0).rangeMin == 16);
        CHECK(pd.compSemantic(0).rangeMax == 235);
}

// ============================================================================
// 10/12-bit UYVY and v210
// ============================================================================

TEST_CASE("PixelFormat: YUV10_422_UYVY_LE is valid") {
        PixelFormat pd(PixelFormat::YUV10_422_UYVY_LE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compSemantic(0).rangeMax == 940);
}

TEST_CASE("PixelFormat: YUV12_422_UYVY_LE is valid") {
        PixelFormat pd(PixelFormat::YUV12_422_UYVY_LE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compSemantic(0).rangeMax == 3760);
}

TEST_CASE("PixelFormat: YUV10_422_v210 has v210 FourCC") {
        PixelFormat pd(PixelFormat::YUV10_422_v210_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("v210"));
}

// ============================================================================
// Planar 4:2:2 PixelFormats
// ============================================================================

TEST_CASE("PixelFormat: YUV8_422_Planar is valid") {
        PixelFormat pd(PixelFormat::YUV8_422_Planar_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.memLayout().id() == PixelMemLayout::P_422_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("PixelFormat: YUV8_422_Planar has I422 FourCC") {
        PixelFormat pd(PixelFormat::YUV8_422_Planar_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("I422"));
}

// ============================================================================
// Planar 4:2:0 PixelFormats
// ============================================================================

TEST_CASE("PixelFormat: YUV8_420_Planar is valid") {
        PixelFormat pd(PixelFormat::YUV8_420_Planar_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.memLayout().id() == PixelMemLayout::P_420_3x8);
}

TEST_CASE("PixelFormat: YUV8_420_Planar has I420 FourCC") {
        PixelFormat pd(PixelFormat::YUV8_420_Planar_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("I420"));
}

TEST_CASE("PixelFormat: YUV10_420_Planar_LE is valid") {
        PixelFormat pd(PixelFormat::YUV10_420_Planar_LE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.memLayout().id() == PixelMemLayout::P_420_3x10_LE);
}

TEST_CASE("PixelFormat: YUV12_420_Planar_BE is valid") {
        PixelFormat pd(PixelFormat::YUV12_420_Planar_BE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.memLayout().id() == PixelMemLayout::P_420_3x12_BE);
}

// ============================================================================
// Semi-planar 4:2:0 PixelFormats
// ============================================================================

TEST_CASE("PixelFormat: YUV8_420_SemiPlanar is valid") {
        PixelFormat pd(PixelFormat::YUV8_420_SemiPlanar_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 2);
        CHECK(pd.memLayout().id() == PixelMemLayout::SP_420_8);
}

TEST_CASE("PixelFormat: YUV8_420_SemiPlanar has NV12 FourCC") {
        PixelFormat pd(PixelFormat::YUV8_420_SemiPlanar_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("NV12"));
}

// ============================================================================
// registeredIDs
// ============================================================================

TEST_CASE("PixelFormat: registeredIDs includes all formats") {
        auto ids = PixelFormat::registeredIDs();
        CHECK(ids.size() >= 132);
        CHECK(ids.contains(PixelFormat::RGBA8_sRGB));
        CHECK(ids.contains(PixelFormat::YUV8_422_UYVY_Rec709));
        CHECK(ids.contains(PixelFormat::YUV10_422_v210_Rec709));
        CHECK(ids.contains(PixelFormat::YUV8_422_Planar_Rec709));
        CHECK(ids.contains(PixelFormat::YUV8_420_Planar_Rec709));
        CHECK(ids.contains(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        CHECK(ids.contains(PixelFormat::YUV12_420_SemiPlanar_BE_Rec709));
}

// ============================================================================
// Automatic iteration over ALL registered PixelFormats
// ============================================================================

TEST_CASE("PixelFormat: all registered descs have valid properties") {
        auto ids = PixelFormat::registeredIDs();
        for(auto id : ids) {
                PixelFormat pd(id);
                CHECK(pd.isValid());
                CHECK_FALSE(pd.name().isEmpty());
                CHECK(pd.memLayout().isValid());
                CHECK(pd.colorModel().isValid());
                CHECK(PixelFormat::lookup(pd.name()) == pd);
        }
}

// ============================================================================
// BGRA
// ============================================================================

TEST_CASE("PixelFormat: BGRA8_sRGB") {
        SUBCASE("component order and alpha") {
                PixelFormat pd(PixelFormat::BGRA8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "B");
                CHECK(pd.compSemantic(2).abbrev == "R");
                CHECK(pd.hasAlpha());
                CHECK(pd.alphaCompIndex() == 3);
        }
}

// ============================================================================
// BGR
// ============================================================================

TEST_CASE("PixelFormat: BGR8_sRGB") {
        SUBCASE("component order and no alpha") {
                PixelFormat pd(PixelFormat::BGR8_sRGB);
                CHECK(pd.isValid());
                CHECK_FALSE(pd.hasAlpha());
                CHECK(pd.compSemantic(0).abbrev == "B");
        }
}

// ============================================================================
// ARGB
// ============================================================================

TEST_CASE("PixelFormat: ARGB8_sRGB") {
        SUBCASE("alpha-first component order") {
                PixelFormat pd(PixelFormat::ARGB8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "A");
                CHECK(pd.hasAlpha());
                CHECK(pd.alphaCompIndex() == 0);
        }
}

// ============================================================================
// ABGR
// ============================================================================

TEST_CASE("PixelFormat: ABGR8_sRGB") {
        SUBCASE("alpha-first blue-first component order") {
                PixelFormat pd(PixelFormat::ABGR8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "A");
                CHECK(pd.compSemantic(1).abbrev == "B");
                CHECK(pd.alphaCompIndex() == 0);
        }
}

// ============================================================================
// Monochrome
// ============================================================================

TEST_CASE("PixelFormat: Mono8_sRGB") {
        SUBCASE("single luminance component") {
                PixelFormat pd(PixelFormat::Mono8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 1);
                CHECK(pd.compSemantic(0).name == "Luminance");
        }
}

// ============================================================================
// Float RGBA
// ============================================================================

TEST_CASE("PixelFormat: RGBAF16_LE_LinearRec709") {
        SUBCASE("float range and alpha") {
                PixelFormat pd(PixelFormat::RGBAF16_LE_LinearRec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1.0));
                CHECK(pd.hasAlpha());
                CHECK(pd.colorModel().id() == ColorModel::LinearRec709);
        }
}

// ============================================================================
// Float Mono
// ============================================================================

TEST_CASE("PixelFormat: MonoF32_LE_LinearRec709") {
        SUBCASE("float mono properties") {
                PixelFormat pd(PixelFormat::MonoF32_LE_LinearRec709);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 1);
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1.0));
        }
}

// ============================================================================
// RGB10A2
// ============================================================================

TEST_CASE("PixelFormat: RGB10A2_LE_sRGB") {
        SUBCASE("10-bit RGB with 2-bit alpha") {
                PixelFormat pd(PixelFormat::RGB10A2_LE_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1023));
                CHECK(pd.compSemantic(3).rangeMax == doctest::Approx(3));
                CHECK(pd.hasAlpha());
        }
}

// ============================================================================
// YCbCr 4:4:4
// ============================================================================

TEST_CASE("PixelFormat: YUV8_Rec709 (4:4:4)") {
        SUBCASE("4:4:4 YCbCr properties") {
                PixelFormat pd(PixelFormat::YUV8_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "Y");
                CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
        }
}

// ============================================================================
// Rec.2020
// ============================================================================

TEST_CASE("PixelFormat: YUV10_422_UYVY_LE_Rec2020") {
        SUBCASE("Rec.2020 color model") {
                PixelFormat pd(PixelFormat::YUV10_422_UYVY_LE_Rec2020);
                CHECK(pd.isValid());
                CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec2020);
        }
}

// ============================================================================
// Rec.601
// ============================================================================

TEST_CASE("PixelFormat: YUV8_422_Rec601") {
        SUBCASE("Rec.601 color model") {
                PixelFormat pd(PixelFormat::YUV8_422_Rec601);
                CHECK(pd.isValid());
                CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec601);
        }
}

// ============================================================================
// NV21
// ============================================================================

TEST_CASE("PixelFormat: YUV8_420_NV21_Rec709") {
        SUBCASE("NV21 validity and semantics") {
                PixelFormat pd(PixelFormat::YUV8_420_NV21_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 3);
                CHECK(pd.planeCount() == 2);
                CHECK(pd.compSemantic(0).abbrev == "Y");
        }
}

// ============================================================================
// NV16 (semi-planar 4:2:2)
// ============================================================================

TEST_CASE("PixelFormat: YUV8_422_SemiPlanar_Rec709") {
        SUBCASE("NV16 validity") {
                PixelFormat pd(PixelFormat::YUV8_422_SemiPlanar_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 3);
                CHECK(pd.planeCount() == 2);
        }
}

// ============================================================================
// 4:1:1 planar
// ============================================================================

TEST_CASE("PixelFormat: YUV8_411_Planar_Rec709") {
        SUBCASE("4:1:1 planar validity") {
                PixelFormat pd(PixelFormat::YUV8_411_Planar_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "Y");
        }
}

// ============================================================================
// 16-bit YCbCr
// ============================================================================

TEST_CASE("PixelFormat: YUV16_LE_Rec709") {
        SUBCASE("16-bit limited range") {
                PixelFormat pd(PixelFormat::YUV16_LE_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).rangeMin == doctest::Approx(4096));
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(60160));
        }
}

// ============================================================================
// DPX additional packed formats
// ============================================================================

TEST_CASE("PixelFormat: RGB10_DPX_LE_sRGB") {
        SUBCASE("validity and pixel format") {
                PixelFormat pd(PixelFormat::RGB10_DPX_LE_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.id() == PixelFormat::RGB10_DPX_LE_sRGB);
                CHECK(pd.name() == "RGB10_DPX_LE_sRGB");
                CHECK(pd.memLayout().id() == PixelMemLayout::I_3x10_DPX);
        }
        SUBCASE("component range is 10-bit full") {
                PixelFormat pd(PixelFormat::RGB10_DPX_LE_sRGB);
                CHECK(pd.compSemantic(0).rangeMin == doctest::Approx(0));
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1023));
                CHECK(pd.compSemantic(0).abbrev == "R");
                CHECK(pd.compSemantic(1).abbrev == "G");
                CHECK(pd.compSemantic(2).abbrev == "B");
        }
}

TEST_CASE("PixelFormat: YUV10_DPX_B_Rec709") {
        SUBCASE("validity and pixel format") {
                PixelFormat pd(PixelFormat::YUV10_DPX_B_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.id() == PixelFormat::YUV10_DPX_B_Rec709);
                CHECK(pd.name() == "YUV10_DPX_B_Rec709");
                CHECK(pd.memLayout().id() == PixelMemLayout::I_3x10_DPX_B);
        }
        SUBCASE("component semantics are YCbCr") {
                PixelFormat pd(PixelFormat::YUV10_DPX_B_Rec709);
                CHECK(pd.compSemantic(0).abbrev == "Y");
                CHECK(pd.compSemantic(1).abbrev == "Cb");
                CHECK(pd.compSemantic(2).abbrev == "Cr");
        }
}

// ============================================================================
// hasPaintEngine()
// ============================================================================

TEST_CASE("PixelFormat: hasPaintEngine") {
        SUBCASE("default (invalid) PixelFormat returns false") {
                PixelFormat pd;
                CHECK_FALSE(pd.hasPaintEngine());
        }

        SUBCASE("RGB formats with a registered paint engine return true") {
                // The library ships a PaintEngine for interleaved 8-bit RGB/RGBA sRGB formats.
                CHECK(PixelFormat(PixelFormat::RGBA8_sRGB).hasPaintEngine());
                CHECK(PixelFormat(PixelFormat::RGB8_sRGB).hasPaintEngine());
        }

        SUBCASE("YUV formats have registered paint engines") {
                CHECK(PixelFormat(PixelFormat::YUV8_422_Rec709).hasPaintEngine());
                CHECK(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709).hasPaintEngine());
                CHECK(PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709).hasPaintEngine());
                CHECK(PixelFormat(PixelFormat::YUV8_420_Planar_Rec709).hasPaintEngine());
                CHECK(PixelFormat(PixelFormat::YUV10_422_Rec709).hasPaintEngine());
        }

        SUBCASE("compressed formats have no paint engine") {
                CHECK_FALSE(PixelFormat(PixelFormat::H264).hasPaintEngine());
                CHECK_FALSE(PixelFormat(PixelFormat::HEVC).hasPaintEngine());
                CHECK_FALSE(PixelFormat(PixelFormat::JPEG_RGB8_sRGB).hasPaintEngine());
        }
}

// ============================================================================
// JPEG XS compressed formats (ISO/IEC 21122)
// ============================================================================

TEST_CASE("PixelFormat: JPEG_XS_YUV8_422_Rec709 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_YUV8_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelFormat: JPEG_XS_YUV10_422_Rec709 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_YUV10_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelFormat: JPEG_XS_YUV12_422_Rec709 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_YUV12_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelFormat: JPEG_XS_YUV8_420_Rec709 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_YUV8_420_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelFormat: JPEG_XS_YUV10_420_Rec709 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_YUV10_420_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelFormat: JPEG_XS_YUV12_420_Rec709 is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_YUV12_420_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelFormat: JPEG_XS_RGB8_sRGB is compressed") {
        PixelFormat pd(PixelFormat::JPEG_XS_RGB8_sRGB);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelFormat: JPEG XS YCbCr entries have correct encode/decode targets") {
        // Each JPEG XS YCbCr variant must have exactly one encode
        // source (the matching planar uncompressed format) and at
        // least one decode target.
        struct Case {
                PixelFormat::ID jpegXs;
                PixelFormat::ID uncompressed;
        };
        const Case cases[] = {
                { PixelFormat::JPEG_XS_YUV8_422_Rec709,  PixelFormat::YUV8_422_Planar_Rec709 },
                { PixelFormat::JPEG_XS_YUV10_422_Rec709, PixelFormat::YUV10_422_Planar_LE_Rec709 },
                { PixelFormat::JPEG_XS_YUV12_422_Rec709, PixelFormat::YUV12_422_Planar_LE_Rec709 },
                { PixelFormat::JPEG_XS_YUV8_420_Rec709,  PixelFormat::YUV8_420_Planar_Rec709 },
                { PixelFormat::JPEG_XS_YUV10_420_Rec709, PixelFormat::YUV10_420_Planar_LE_Rec709 },
                { PixelFormat::JPEG_XS_YUV12_420_Rec709, PixelFormat::YUV12_420_Planar_LE_Rec709 },
        };
        for(const auto &c : cases) {
                PixelFormat pd(c.jpegXs);
                REQUIRE(pd.isValid());
                // Encode source must include the uncompressed match.
                bool foundSource = false;
                for(const auto &src : pd.encodeSources()) {
                        if(src == c.uncompressed) { foundSource = true; break; }
                }
                CHECK_MESSAGE(foundSource, "encode source missing for ", (int)c.jpegXs);
                // Decode target must include the uncompressed match.
                bool foundTarget = false;
                for(const auto &tgt : pd.decodeTargets()) {
                        if(tgt == c.uncompressed) { foundTarget = true; break; }
                }
                CHECK_MESSAGE(foundTarget, "decode target missing for ", (int)c.jpegXs);
        }
}

TEST_CASE("PixelFormat: JPEG_XS_RGB8_sRGB encode/decode targets") {
        PixelFormat pd(PixelFormat::JPEG_XS_RGB8_sRGB);
        REQUIRE(pd.isValid());
        bool foundPlanar = false;
        for(const auto &src : pd.encodeSources()) {
                if(src == PixelFormat::RGB8_Planar_sRGB) { foundPlanar = true; break; }
        }
        CHECK(foundPlanar);
        bool foundTarget = false;
        for(const auto &tgt : pd.decodeTargets()) {
                if(tgt == PixelFormat::RGB8_Planar_sRGB) { foundTarget = true; break; }
        }
        CHECK(foundTarget);
}

TEST_CASE("PixelFormat: JPEG XS fourcc is jxsm") {
        // ISO/IEC 21122-3 ISOBMFF sample entry is "jxsm".
        PixelFormat pd(PixelFormat::JPEG_XS_YUV10_422_Rec709);
        REQUIRE(pd.isValid());
        bool foundFourcc = false;
        for(const auto &cc : pd.fourccList()) {
                if(cc == "jxsm") { foundFourcc = true; break; }
        }
        CHECK(foundFourcc);
}

TEST_CASE("PixelFormat: JPEG XS string name round-trip via registeredIDs") {
        // All JPEG XS entries must appear in the registeredIDs list
        // and must have non-empty string names that start with
        // "JPEG_XS_".
        const PixelFormat::ID jxsIds[] = {
                PixelFormat::JPEG_XS_YUV8_422_Rec709,
                PixelFormat::JPEG_XS_YUV10_422_Rec709,
                PixelFormat::JPEG_XS_YUV12_422_Rec709,
                PixelFormat::JPEG_XS_YUV8_420_Rec709,
                PixelFormat::JPEG_XS_YUV10_420_Rec709,
                PixelFormat::JPEG_XS_YUV12_420_Rec709,
                PixelFormat::JPEG_XS_RGB8_sRGB,
        };
        auto allIds = PixelFormat::registeredIDs();
        for(PixelFormat::ID id : jxsIds) {
                bool foundInRegistry = false;
                for(const auto &regId : allIds) {
                        if(regId == id) { foundInRegistry = true; break; }
                }
                CHECK_MESSAGE(foundInRegistry,
                              "JPEG XS ID ", (int)id,
                              " not in registeredIDs");
                PixelFormat pd(id);
                REQUIRE(pd.isValid());
                CHECK(pd.name().startsWith("JPEG_XS_"));
        }
}

TEST_CASE("PixelFormat: JPEG XS component semantics have expected ranges") {
        // The 10-bit 4:2:2 entry should have Rec.709 limited-range
        // component semantics: Y 64-940, Cb/Cr 64-960.
        PixelFormat pd(PixelFormat::JPEG_XS_YUV10_422_Rec709);
        REQUIRE(pd.isValid());
        REQUIRE(pd.compCount() >= 3);
        CHECK(pd.compSemantic(0).name == "Luma");
        CHECK(pd.compSemantic(0).rangeMin == 64);
        CHECK(pd.compSemantic(0).rangeMax == 940);
        CHECK(pd.compSemantic(1).name == "Chroma Blue");
        CHECK(pd.compSemantic(1).rangeMin == 64);
        CHECK(pd.compSemantic(1).rangeMax == 960);
        CHECK(pd.compSemantic(2).name == "Chroma Red");
        CHECK(pd.compSemantic(2).rangeMin == 64);
        CHECK(pd.compSemantic(2).rangeMax == 960);
}

// ============================================================================
// data() accessor and Invalid fallback
// ============================================================================

TEST_CASE("PixelFormat: data() returns non-null for valid formats") {
        PixelFormat pd(PixelFormat::RGBA8_sRGB);
        CHECK(pd.data() != nullptr);
        CHECK(pd.data()->id == PixelFormat::RGBA8_sRGB);
        CHECK(pd.data()->name == "RGBA8_sRGB");
}

TEST_CASE("PixelFormat: default-constructed PixelFormat still has a data record") {
        // lookupData() falls back to the Invalid entry for unknown/default IDs,
        // so data() is non-null even for default construction.
        PixelFormat pd;
        CHECK(pd.data() != nullptr);
        CHECK(pd.data()->id == PixelFormat::Invalid);
}

TEST_CASE("PixelFormat: unknown non-registered ID falls back to Invalid") {
        PixelFormat pd(static_cast<PixelFormat::ID>(900));
        CHECK_FALSE(pd.isValid());
        CHECK(pd.id() == PixelFormat::Invalid);
}

// ============================================================================
// alphaCompIndex for formats without alpha
// ============================================================================

TEST_CASE("PixelFormat: alphaCompIndex is -1 for non-alpha RGB/YUV formats") {
        CHECK(PixelFormat(PixelFormat::RGB8_sRGB).alphaCompIndex() == -1);
        CHECK(PixelFormat(PixelFormat::BGR8_sRGB).alphaCompIndex() == -1);
        CHECK(PixelFormat(PixelFormat::YUV8_422_Rec709).alphaCompIndex() == -1);
        CHECK(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709).alphaCompIndex() == -1);
        CHECK(PixelFormat(PixelFormat::Mono8_sRGB).alphaCompIndex() == -1);
}

// ============================================================================
// videoCodec() / encodeSources() / decodeTargets() for uncompressed formats
// ============================================================================

TEST_CASE("PixelFormat: videoCodec() is Invalid for uncompressed formats") {
        CHECK(PixelFormat(PixelFormat::RGBA8_sRGB).videoCodec().id() == VideoCodec::Invalid);
        CHECK(PixelFormat(PixelFormat::YUV8_422_Rec709).videoCodec().id() == VideoCodec::Invalid);
        CHECK(PixelFormat(PixelFormat::Mono8_sRGB).videoCodec().id() == VideoCodec::Invalid);
}

TEST_CASE("PixelFormat: encodeSources/decodeTargets empty for uncompressed formats") {
        PixelFormat rgba(PixelFormat::RGBA8_sRGB);
        CHECK(rgba.encodeSources().isEmpty());
        CHECK(rgba.decodeTargets().isEmpty());
        PixelFormat yuv(PixelFormat::YUV8_422_Rec709);
        CHECK(yuv.encodeSources().isEmpty());
        CHECK(yuv.decodeTargets().isEmpty());
}

// ============================================================================
// lineStride / planeSize: compressed vs uncompressed behaviour
// ============================================================================

TEST_CASE("PixelFormat: lineStride returns 0 for compressed formats") {
        ImageDesc desc(1920, 1080, PixelFormat::JPEG_RGB8_sRGB);
        PixelFormat pd(PixelFormat::JPEG_RGB8_sRGB);
        CHECK(pd.lineStride(0, desc) == 0);
}

TEST_CASE("PixelFormat: planeSize returns 0 for compressed without CompressedSize") {
        // planeSize on a compressed format without Metadata::CompressedSize
        // must return 0 — callers are expected to set that metadata before
        // asking.
        ImageDesc desc(1920, 1080, PixelFormat::JPEG_RGB8_sRGB);
        PixelFormat pd(PixelFormat::JPEG_RGB8_sRGB);
        CHECK(pd.planeSize(0, desc) == 0);
}

TEST_CASE("PixelFormat: planeSize non-compressed delegates to memLayout") {
        ImageDesc desc(1920, 1080, PixelFormat::RGBA8_sRGB);
        PixelFormat pd(PixelFormat::RGBA8_sRGB);
        CHECK(pd.planeSize(0, desc) == 1920 * 1080 * 4);
}

TEST_CASE("PixelFormat: lineStride respects ImageDesc linePad/lineAlign") {
        ImageDesc desc(1920, 1080, PixelFormat::RGBA8_sRGB);
        desc.setLinePad(8);
        desc.setLineAlign(64);
        PixelFormat pd(PixelFormat::RGBA8_sRGB);
        size_t stride = pd.lineStride(0, desc);
        CHECK(stride % 64 == 0);
        CHECK(stride >= 1920 * 4 + 8);
}

TEST_CASE("PixelFormat: planar YUV planeSize matches memLayout expectations") {
        ImageDesc desc(1920, 1080, PixelFormat::YUV8_420_Planar_Rec709);
        PixelFormat pd(PixelFormat::YUV8_420_Planar_Rec709);
        CHECK(pd.planeSize(0, desc) == 1920 * 1080);
        CHECK(pd.planeSize(1, desc) == 960 * 540);
        CHECK(pd.planeSize(2, desc) == 960 * 540);
}

// ============================================================================
// videoRange auto-derivation — exhaustive shape coverage
// ============================================================================

TEST_CASE("PixelFormat: videoRange auto-derives Full for 12-bit full-range RGB") {
        PixelFormat pd(PixelFormat::RGB12_LE_sRGB);
        CHECK(pd.videoRange() == VideoRange::Full);
}

TEST_CASE("PixelFormat: videoRange auto-derives Full for 16-bit full-range RGB") {
        PixelFormat pd(PixelFormat::RGB16_LE_sRGB);
        CHECK(pd.videoRange() == VideoRange::Full);
}

TEST_CASE("PixelFormat: videoRange auto-derives Limited for 12-bit limited YCbCr") {
        PixelFormat pd(PixelFormat::YUV12_422_UYVY_LE_Rec709);
        CHECK(pd.videoRange() == VideoRange::Limited);
}

TEST_CASE("PixelFormat: videoRange auto-derives Limited for 16-bit limited YCbCr") {
        PixelFormat pd(PixelFormat::YUV16_LE_Rec709);
        CHECK(pd.videoRange() == VideoRange::Limited);
}

TEST_CASE("PixelFormat: videoRange via registerData — user-defined Unknown path") {
        // A user format whose compSemantic[0].rangeMax is <= 0 leaves
        // videoRange as Unknown.  The auto-derive code returns Unknown
        // immediately for max == 0.
        PixelFormat::ID id = PixelFormat::registerType();
        PixelFormat::Data d;
        d.id             = id;
        d.name           = "TestUserUnknownRange";
        d.desc           = "User-registered format with empty semantics";
        d.memLayout      = PixelMemLayout(PixelMemLayout::I_4x8);
        d.colorModel     = ColorModel(ColorModel::sRGB);
        // compSemantics left default: rangeMin=0, rangeMax=0.
        PixelFormat::registerData(std::move(d));
        PixelFormat pd(id);
        REQUIRE(pd.isValid());
        CHECK(pd.videoRange() == VideoRange::Unknown);
}

TEST_CASE("PixelFormat: videoRange via registerData — exotic range stays Unknown") {
        // A full-looking range with a non-standard maximum (e.g. 200) is
        // neither a full-range bit-depth maximum nor limited (min==0), so
        // auto-derive yields Unknown.
        PixelFormat::ID id = PixelFormat::registerType();
        PixelFormat::Data d;
        d.id             = id;
        d.name           = "TestUserExoticRange";
        d.desc           = "User-registered format with odd max";
        d.memLayout      = PixelMemLayout(PixelMemLayout::I_4x8);
        d.colorModel     = ColorModel(ColorModel::sRGB);
        d.compSemantics[0] = { "Red",   "R", 0.0f, 200.0f };
        d.compSemantics[1] = { "Green", "G", 0.0f, 200.0f };
        d.compSemantics[2] = { "Blue",  "B", 0.0f, 200.0f };
        d.compSemantics[3] = { "Alpha", "A", 0.0f, 200.0f };
        PixelFormat::registerData(std::move(d));
        PixelFormat pd(id);
        CHECK(pd.videoRange() == VideoRange::Unknown);
}

TEST_CASE("PixelFormat: videoRange via registerData — explicit value not overridden") {
        // Factories may pre-set Data::videoRange; auto-derive must not
        // overwrite a non-Unknown value.
        PixelFormat::ID id = PixelFormat::registerType();
        PixelFormat::Data d;
        d.id             = id;
        d.name           = "TestUserExplicitFull";
        d.memLayout      = PixelMemLayout(PixelMemLayout::I_4x8);
        d.colorModel     = ColorModel(ColorModel::sRGB);
        d.videoRange     = VideoRange::Full;
        // Give the auto-derive a shape that would otherwise yield Limited
        // so we can prove the explicit value wins.
        d.compSemantics[0] = { "Y", "Y", 16.0f, 235.0f };
        PixelFormat::registerData(std::move(d));
        PixelFormat pd(id);
        CHECK(pd.videoRange() == VideoRange::Full);
}

// ============================================================================
// registerType / registerData (user-defined)
// ============================================================================

TEST_CASE("PixelFormat: registerType returns unique monotonic IDs >= UserDefined") {
        auto a = PixelFormat::registerType();
        auto b = PixelFormat::registerType();
        auto c = PixelFormat::registerType();
        CHECK(static_cast<int>(a) >= static_cast<int>(PixelFormat::UserDefined));
        CHECK(static_cast<int>(b) == static_cast<int>(a) + 1);
        CHECK(static_cast<int>(c) == static_cast<int>(b) + 1);
}

TEST_CASE("PixelFormat: registerData makes a user-defined format lookupable") {
        PixelFormat::ID id = PixelFormat::registerType();
        PixelFormat::Data d;
        d.id             = id;
        d.name           = "TestUserRGBA8Copy";
        d.desc           = "User-registered RGBA8 clone";
        d.memLayout      = PixelMemLayout(PixelMemLayout::I_4x8);
        d.colorModel     = ColorModel(ColorModel::sRGB);
        d.hasAlpha       = true;
        d.alphaCompIndex = 3;
        d.compSemantics[0] = { "Red",   "R", 0, 255 };
        d.compSemantics[1] = { "Green", "G", 0, 255 };
        d.compSemantics[2] = { "Blue",  "B", 0, 255 };
        d.compSemantics[3] = { "Alpha", "A", 0, 255 };
        PixelFormat::registerData(std::move(d));

        PixelFormat pd(id);
        REQUIRE(pd.isValid());
        CHECK(pd.id() == id);
        CHECK(pd.name() == "TestUserRGBA8Copy");
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
        // Auto-derive should have filled in Full from the 0..255 semantics.
        CHECK(pd.videoRange() == VideoRange::Full);

        PixelFormat found = PixelFormat::lookup("TestUserRGBA8Copy");
        CHECK(found == pd);

        auto ids = PixelFormat::registeredIDs();
        CHECK(ids.contains(id));
}

TEST_CASE("PixelFormat: lookup of unknown name returns Invalid") {
        PixelFormat pd = PixelFormat::lookup("NoSuchFormat_XYZ");
        CHECK_FALSE(pd.isValid());
}

// ============================================================================
// Newly-added well-known formats: YUV8/10 444 Planar, RGB8 Planar, AV1
// ============================================================================

TEST_CASE("PixelFormat: YUV8_444_Planar_Rec709 basic properties") {
        PixelFormat pd(PixelFormat::YUV8_444_Planar_Rec709);
        REQUIRE(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.memLayout().id() == PixelMemLayout::P_444_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
        CHECK(pd.compSemantic(0).abbrev == "Y");
        CHECK(pd.compSemantic(1).abbrev == "Cb");
        CHECK(pd.compSemantic(2).abbrev == "Cr");
        CHECK(pd.compSemantic(0).rangeMin == 16);
        CHECK(pd.compSemantic(0).rangeMax == 235);
        CHECK_FALSE(pd.isCompressed());
        // NOTE: videoRange stays Unknown for this format.  Well-known
        // factories are added via PixelFormatRegistry::add() which bypasses
        // PixelFormat::registerData(), so autoDeriveVideoRange never runs
        // for YUV 4:4:4 planar (which also lacks a paint-engine patch that
        // would otherwise call registerData() a second time and trigger
        // auto-derive).  See docs for more context.
        CHECK(pd.videoRange() == VideoRange::Unknown);
}

TEST_CASE("PixelFormat: YUV10_444_Planar_LE_Rec709 basic properties") {
        PixelFormat pd(PixelFormat::YUV10_444_Planar_LE_Rec709);
        REQUIRE(pd.isValid());
        CHECK(pd.memLayout().id() == PixelMemLayout::P_444_3x10_LE);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
        CHECK(pd.compSemantic(0).rangeMin == 64);
        CHECK(pd.compSemantic(0).rangeMax == 940);
        // See note on YUV8_444_Planar_Rec709: lacks paint-engine patch,
        // so videoRange auto-derive never runs.
        CHECK(pd.videoRange() == VideoRange::Unknown);
}

TEST_CASE("PixelFormat: RGB8_Planar_sRGB basic properties") {
        PixelFormat pd(PixelFormat::RGB8_Planar_sRGB);
        REQUIRE(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.memLayout().id() == PixelMemLayout::P_444_3x8);
        CHECK(pd.colorModel().id() == ColorModel::sRGB);
        CHECK(pd.compSemantic(0).abbrev == "R");
        CHECK(pd.compSemantic(1).abbrev == "G");
        CHECK(pd.compSemantic(2).abbrev == "B");
        CHECK(pd.videoRange() == VideoRange::Full);
        CHECK_FALSE(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == -1);
}

TEST_CASE("PixelFormat: AV1 is compressed with av01 FourCC") {
        PixelFormat pd(PixelFormat::AV1);
        REQUIRE(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::AV1);
        REQUIRE(pd.fourccList().size() == 1);
        CHECK(pd.fourccList()[0] == FourCC("av01"));
        // AV1 ships 10-bit 4:2:0 planar layout (Rec.709).
        CHECK(pd.memLayout().id() == PixelMemLayout::P_420_3x10_LE);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

// ============================================================================
// YUV 4:4:4 DPX (originally missing from explicit coverage)
// ============================================================================

TEST_CASE("PixelFormat: YUV10_DPX_Rec709 basic properties") {
        PixelFormat pd(PixelFormat::YUV10_DPX_Rec709);
        REQUIRE(pd.isValid());
        CHECK(pd.memLayout().id() == PixelMemLayout::I_3x10_DPX);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
        CHECK(pd.compSemantic(0).abbrev == "Y");
        CHECK(pd.compSemantic(1).abbrev == "Cb");
        CHECK(pd.compSemantic(2).abbrev == "Cr");
        CHECK(pd.videoRange() == VideoRange::Limited);
}

// ============================================================================
// hasPaintEngine — compressed AV1 / JPEG_XS / ProRes have no engine
// ============================================================================

TEST_CASE("PixelFormat: compressed codec formats have no paint engine") {
        CHECK_FALSE(PixelFormat(PixelFormat::AV1).hasPaintEngine());
        CHECK_FALSE(PixelFormat(PixelFormat::ProRes_422).hasPaintEngine());
        CHECK_FALSE(PixelFormat(PixelFormat::ProRes_4444).hasPaintEngine());
        CHECK_FALSE(PixelFormat(PixelFormat::ProRes_4444_XQ).hasPaintEngine());
        CHECK_FALSE(PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709).hasPaintEngine());
        CHECK_FALSE(PixelFormat(PixelFormat::JPEG_XS_RGB8_sRGB).hasPaintEngine());
}

TEST_CASE("PixelFormat: RGB8_Planar_sRGB has a paint engine") {
        // Planar RGB (4:4:4) ships with a MultiPlane8 paint-engine patch.
        CHECK(PixelFormat(PixelFormat::RGB8_Planar_sRGB).hasPaintEngine());
}

TEST_CASE("PixelFormat: 4:4:4 planar YCbCr has no registered paint engine") {
        // YUV 4:4:4 planar formats are present in the registry but no
        // paint-engine factory is wired up for them yet — callers either
        // convert to RGB first or operate on the raw planes directly.
        CHECK_FALSE(PixelFormat(PixelFormat::YUV8_444_Planar_Rec709).hasPaintEngine());
        CHECK_FALSE(PixelFormat(PixelFormat::YUV10_444_Planar_LE_Rec709).hasPaintEngine());
}

// ============================================================================
// Float formats — BE and 3-component variants not yet covered
// ============================================================================

TEST_CASE("PixelFormat: RGBF16_LE_LinearRec709 basic properties") {
        PixelFormat pd(PixelFormat::RGBF16_LE_LinearRec709);
        REQUIRE(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK_FALSE(pd.hasAlpha());
        CHECK(pd.colorModel().id() == ColorModel::LinearRec709);
        CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1.0));
}

TEST_CASE("PixelFormat: RGBAF32_LE_LinearRec709 basic properties") {
        PixelFormat pd(PixelFormat::RGBAF32_LE_LinearRec709);
        REQUIRE(pd.isValid());
        CHECK(pd.compCount() == 4);
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
        CHECK(pd.colorModel().id() == ColorModel::LinearRec709);
}

TEST_CASE("PixelFormat: MonoF16_LE_LinearRec709 basic properties") {
        PixelFormat pd(PixelFormat::MonoF16_LE_LinearRec709);
        REQUIRE(pd.isValid());
        CHECK(pd.compCount() == 1);
        CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1.0));
}

// ============================================================================
// Non-alpha PixelFormat alphaCompIndex
// ============================================================================

TEST_CASE("PixelFormat: RGB10A2 alpha component index") {
        PixelFormat pd(PixelFormat::RGB10A2_LE_sRGB);
        REQUIRE(pd.isValid());
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
}

TEST_CASE("PixelFormat: BGR10A2 alpha component index") {
        PixelFormat pd(PixelFormat::BGR10A2_LE_sRGB);
        REQUIRE(pd.isValid());
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
}

// ============================================================================
// videoCodec() FourCC association
// ============================================================================

TEST_CASE("PixelFormat: videoCodec accessor name matches for H264/HEVC") {
        CHECK(PixelFormat(PixelFormat::H264).videoCodec().name() == "H264");
        CHECK(PixelFormat(PixelFormat::HEVC).videoCodec().name() == "HEVC");
        CHECK(PixelFormat(PixelFormat::AV1).videoCodec().name() == "AV1");
}

// ============================================================================
// Round-trip all registeredIDs through lookup(name())
// ============================================================================

TEST_CASE("PixelFormat: every registered ID round-trips through lookup by name") {
        auto ids = PixelFormat::registeredIDs();
        for(auto id : ids) {
                PixelFormat pd(id);
                REQUIRE(pd.isValid());
                PixelFormat found = PixelFormat::lookup(pd.name());
                CAPTURE(pd.name());
                CHECK(found == pd);
        }
}

// ============================================================================
// Every compressed format declares a VideoCodec
// ============================================================================

TEST_CASE("PixelFormat: every compressed format has a valid VideoCodec") {
        auto ids = PixelFormat::registeredIDs();
        for(auto id : ids) {
                PixelFormat pd(id);
                if(!pd.isCompressed()) continue;
                CAPTURE(pd.name());
                CHECK(pd.videoCodec().id() != VideoCodec::Invalid);
        }
}
