/**
 * @file      pixeldesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pixeldesc.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>

using namespace promeki;

// ============================================================================
// Default / Invalid
// ============================================================================

TEST_CASE("PixelDesc: default constructs to Invalid") {
        PixelDesc pd;
        CHECK_FALSE(pd.isValid());
        CHECK(pd.id() == PixelDesc::Invalid);
}

// ============================================================================
// RGBA8
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 is valid") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::I_4x8);
        CHECK(pd.hasAlpha());
        CHECK_FALSE(pd.isCompressed());
        CHECK(pd.compCount() == 4);
        CHECK(pd.planeCount() == 1);
}

// ============================================================================
// RGB8
// ============================================================================

TEST_CASE("PixelDesc: RGB8 is valid") {
        PixelDesc pd(PixelDesc::RGB8_sRGB);
        CHECK(pd.isValid());
        CHECK_FALSE(pd.hasAlpha());
        CHECK(pd.compCount() == 3);
}

// ============================================================================
// YUV8_422 (YUYV)
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422 is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK_FALSE(pd.hasAlpha());
}

TEST_CASE("PixelDesc: YUV8_422 YUYV has YUY2 and YUYV FourCCs") {
        PixelDesc pd(PixelDesc::YUV8_422_Rec709);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("YUY2"));
        CHECK(pd.fourccList()[1] == FourCC("YUYV"));
}

// ============================================================================
// JPEG compressed formats
// ============================================================================

TEST_CASE("PixelDesc: JPEG_RGBA8 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_RGBA8_sRGB);
        CHECK(pd.isCompressed());
        CHECK(pd.hasAlpha());
}

TEST_CASE("PixelDesc: JPEG_RGB8 videoCodec resolves to VideoCodec::JPEG") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB);
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG);
        CHECK(pd.videoCodec().name() == "JPEG");
}

TEST_CASE("PixelDesc: JPEG_RGBA8 encodeSources and decodeTargets") {
        PixelDesc pd(PixelDesc::JPEG_RGBA8_sRGB);
        CHECK(pd.encodeSources().size() == 1);
        CHECK(pd.encodeSources()[0] == PixelDesc::RGBA8_sRGB);
        CHECK(pd.decodeTargets().size() == 1);
        CHECK(pd.decodeTargets()[0] == PixelDesc::RGBA8_sRGB);
}

TEST_CASE("PixelDesc: JPEG_RGB8 encodeSources is strictly RGB8") {
        // Only the natural RGB family is listed — see
        // JpegImageCodec::encode() which tags the output based on the
        // input component order.  Mixed-family inputs CSC through
        // Image::convert() before hitting the codec.
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB);
        REQUIRE(pd.encodeSources().size() == 1);
        CHECK(pd.encodeSources()[0] == PixelDesc::RGB8_sRGB);
}

TEST_CASE("PixelDesc: JPEG_YUV8_422 encodeSources and decodeTargets") {
        PixelDesc pd(PixelDesc::JPEG_YUV8_422_Rec709);
        // Only the natural YUV 4:2:2 family — RGB inputs CSC through
        // Image::convert() before hitting the codec.
        REQUIRE(pd.encodeSources().size() == 3);
        CHECK(pd.encodeSources().contains(PixelDesc::YUV8_422_Rec709));
        CHECK(pd.encodeSources().contains(PixelDesc::YUV8_422_UYVY_Rec709));
        CHECK(pd.encodeSources().contains(PixelDesc::YUV8_422_Planar_Rec709));
        CHECK(pd.decodeTargets().size() == 5);
        CHECK(pd.decodeTargets().contains(PixelDesc::YUV8_422_Rec709));
        CHECK(pd.decodeTargets().contains(PixelDesc::YUV8_422_UYVY_Rec709));
        CHECK(pd.decodeTargets().contains(PixelDesc::YUV8_422_Planar_Rec709));
        CHECK(pd.decodeTargets().contains(PixelDesc::RGB8_sRGB));
        CHECK(pd.decodeTargets().contains(PixelDesc::RGBA8_sRGB));
}

TEST_CASE("PixelDesc: JPEG YCbCr complement — matrix × range grid") {
        // The library offers all 8 combinations of
        // subsampling (4:2:2 / 4:2:0) × matrix (Rec.709 / Rec.601) ×
        // range (limited / full) as first-class JPEG PixelDescs.
        // The unsuffixed names are limited-range (matching the
        // library-wide YCbCr default), and "_Full" is the explicit
        // full-range opt-in.  For strict JFIF interop (ffplay,
        // browsers, libjpeg-turbo's own decode path) use the
        // Rec.601 _Full variants.

        struct Case {
                PixelDesc::ID id;
                bool          limitedRange;
                bool          rec709;
                bool          is420;
        };
        const Case cases[] = {
                // Limited-range entries.
                { PixelDesc::JPEG_YUV8_422_Rec709,       true,  true,  false },
                { PixelDesc::JPEG_YUV8_420_Rec709,       true,  true,  true  },
                { PixelDesc::JPEG_YUV8_422_Rec601,       true,  false, false },
                { PixelDesc::JPEG_YUV8_420_Rec601,       true,  false, true  },
                // Full-range entries.
                { PixelDesc::JPEG_YUV8_422_Rec709_Full,  false, true,  false },
                { PixelDesc::JPEG_YUV8_420_Rec709_Full,  false, true,  true  },
                { PixelDesc::JPEG_YUV8_422_Rec601_Full,  false, false, false },
                { PixelDesc::JPEG_YUV8_420_Rec601_Full,  false, false, true  },
        };
        for(const auto &c : cases) {
                PixelDesc pd(c.id);
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

TEST_CASE("PixelDesc: videoRange auto-derives from compSemantics") {
        // Well-known PixelDescs don't set Data::videoRange explicitly
        // yet, so all of these exercise the auto-derivation path in
        // PixelDesc::registerData.  The shape of the inference is:
        //   rangeMin > 0          → Limited
        //   rangeMin==0 && max==(2^N-1) → Full
        // Anything else stays Unknown.

        SUBCASE("RGB full-range 8-bit") {
                PixelDesc pd(PixelDesc::RGBA8_sRGB);
                CHECK(pd.videoRange() == VideoRange::Full);
        }
        SUBCASE("RGB full-range 10-bit") {
                PixelDesc pd(PixelDesc::RGB10_LE_sRGB);
                CHECK(pd.videoRange() == VideoRange::Full);
        }
        SUBCASE("YCbCr limited 8-bit") {
                PixelDesc pd(PixelDesc::YUV8_422_Rec709);
                CHECK(pd.videoRange() == VideoRange::Limited);
        }
        SUBCASE("YCbCr limited 10-bit") {
                PixelDesc pd(PixelDesc::YUV10_422_Rec709);
                CHECK(pd.videoRange() == VideoRange::Limited);
        }
        SUBCASE("YCbCr full-range intermediate") {
                PixelDesc pd(PixelDesc::YUV8_422_Rec709_Full);
                CHECK(pd.videoRange() == VideoRange::Full);
        }
}

TEST_CASE("PixelDesc: full-range uncompressed YCbCr intermediates") {
        // The encode-source intermediates for the full-range JPEG
        // variants live as first-class PixelDescs so Image::convert
        // can CSC into them before the codec copies bytes verbatim
        // into the JFIF bitstream.
        const PixelDesc::ID ids[] = {
                PixelDesc::YUV8_422_Rec709_Full,
                PixelDesc::YUV8_422_Rec601_Full,
                PixelDesc::YUV8_420_Planar_Rec709_Full,
                PixelDesc::YUV8_420_Planar_Rec601_Full,
        };
        for(PixelDesc::ID id : ids) {
                PixelDesc pd(id);
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

TEST_CASE("PixelDesc: H264 is compressed with avc1 FourCC") {
        PixelDesc pd(PixelDesc::H264);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::H264);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("avc1"));
        CHECK(pd.fourccList()[1] == FourCC("avc3"));
}

TEST_CASE("PixelDesc: HEVC is compressed with hvc1 FourCC") {
        PixelDesc pd(PixelDesc::HEVC);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::HEVC);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("hvc1"));
        CHECK(pd.fourccList()[1] == FourCC("hev1"));
}

TEST_CASE("PixelDesc: ProRes 422 family has correct FourCCs") {
        // The string "prores" was shared across all six PixelDesc
        // entries before we landed the typed VideoCodec registry;
        // each variant now has its own VideoCodec ID.  The table
        // lists the FourCC + the matching VideoCodec ID per variant
        // and we verify each one resolves through PixelDesc::videoCodec().
        struct Entry {
                PixelDesc::ID  pdId;
                const char    *fourcc;
                VideoCodec::ID vc;
        };
        Entry entries[] = {
                { PixelDesc::ProRes_422_Proxy, "apco", VideoCodec::ProRes_422_Proxy },
                { PixelDesc::ProRes_422_LT,    "apcs", VideoCodec::ProRes_422_LT    },
                { PixelDesc::ProRes_422,       "apcn", VideoCodec::ProRes_422       },
                { PixelDesc::ProRes_422_HQ,    "apch", VideoCodec::ProRes_422_HQ    }
        };
        for(const auto &e : entries) {
                PixelDesc pd(e.pdId);
                CHECK(pd.isValid());
                CHECK(pd.isCompressed());
                CHECK(pd.videoCodec().id() == e.vc);
                CHECK_FALSE(pd.hasAlpha());
                REQUIRE(pd.fourccList().size() == 1);
                CHECK(pd.fourccList()[0] == FourCC(e.fourcc[0], e.fourcc[1], e.fourcc[2], e.fourcc[3]));
                CHECK(pd.pixelFormat().id() == PixelFormat::P_422_3x10_LE);
        }
}

TEST_CASE("PixelDesc: ProRes 4444 has alpha and 10-bit") {
        PixelDesc pd(PixelDesc::ProRes_4444);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::ProRes_4444);
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
        REQUIRE(pd.fourccList().size() == 1);
        CHECK(pd.fourccList()[0] == FourCC("ap4h"));
        CHECK(pd.pixelFormat().id() == PixelFormat::I_4x10_LE);
}

TEST_CASE("PixelDesc: ProRes 4444 XQ has alpha and 12-bit") {
        PixelDesc pd(PixelDesc::ProRes_4444_XQ);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::ProRes_4444_XQ);
        CHECK(pd.hasAlpha());
        CHECK(pd.alphaCompIndex() == 3);
        REQUIRE(pd.fourccList().size() == 1);
        CHECK(pd.fourccList()[0] == FourCC("ap4x"));
        CHECK(pd.pixelFormat().id() == PixelFormat::I_4x12_LE);
}

TEST_CASE("PixelDesc: new codec entries are each uniquely lookupable by name") {
        const char *names[] = {
                "H264", "HEVC",
                "ProRes_422_Proxy", "ProRes_422_LT", "ProRes_422", "ProRes_422_HQ",
                "ProRes_4444", "ProRes_4444_XQ"
        };
        for(const char *name : names) {
                PixelDesc pd = PixelDesc::lookup(name);
                CHECK(pd.isValid());
                CHECK(pd.name() == name);
        }
}

// ============================================================================
// lineStride and planeSize delegation
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 lineStride via ImageDesc") {
        ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
        PixelDesc pd(PixelDesc::RGBA8_sRGB);
        CHECK(pd.lineStride(0, desc) == 1920 * 4);
}

TEST_CASE("PixelDesc: JPEG planeSize reads CompressedSize from metadata") {
        ImageDesc desc(640, 480, PixelDesc::JPEG_RGB8_sRGB);
        desc.metadata().set(Metadata::CompressedSize, 12345);
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB);
        CHECK(pd.planeSize(0, desc) == 12345);
}

// ============================================================================
// Lookup and equality
// ============================================================================

TEST_CASE("PixelDesc: lookup by name") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB);
        PixelDesc found = PixelDesc::lookup(pd.name());
        CHECK(found == pd);
}

TEST_CASE("PixelDesc: equality") {
        PixelDesc a(PixelDesc::RGBA8_sRGB);
        PixelDesc b(PixelDesc::RGBA8_sRGB);
        PixelDesc c(PixelDesc::RGB8_sRGB);
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// UYVY 8-bit
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422_UYVY is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_UYVY_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::I_422_UYVY_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("PixelDesc: YUV8_422_UYVY has both 2vuy and UYVY FourCCs") {
        PixelDesc pd(PixelDesc::YUV8_422_UYVY_Rec709);
        // QuickTime canonical name comes first (the writer preference).
        REQUIRE(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("2vuy"));
        CHECK(pd.fourccList()[1] == FourCC("UYVY"));
}

TEST_CASE("PixelDesc: YUV8_422_UYVY limited range") {
        PixelDesc pd(PixelDesc::YUV8_422_UYVY_Rec709);
        CHECK(pd.compSemantic(0).rangeMin == 16);
        CHECK(pd.compSemantic(0).rangeMax == 235);
}

// ============================================================================
// 10/12-bit UYVY and v210
// ============================================================================

TEST_CASE("PixelDesc: YUV10_422_UYVY_LE is valid") {
        PixelDesc pd(PixelDesc::YUV10_422_UYVY_LE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compSemantic(0).rangeMax == 940);
}

TEST_CASE("PixelDesc: YUV12_422_UYVY_LE is valid") {
        PixelDesc pd(PixelDesc::YUV12_422_UYVY_LE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compSemantic(0).rangeMax == 3760);
}

TEST_CASE("PixelDesc: YUV10_422_v210 has v210 FourCC") {
        PixelDesc pd(PixelDesc::YUV10_422_v210_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("v210"));
}

// ============================================================================
// Planar 4:2:2 PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422_Planar is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_Planar_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.pixelFormat().id() == PixelFormat::P_422_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("PixelDesc: YUV8_422_Planar has I422 FourCC") {
        PixelDesc pd(PixelDesc::YUV8_422_Planar_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("I422"));
}

// ============================================================================
// Planar 4:2:0 PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: YUV8_420_Planar is valid") {
        PixelDesc pd(PixelDesc::YUV8_420_Planar_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.pixelFormat().id() == PixelFormat::P_420_3x8);
}

TEST_CASE("PixelDesc: YUV8_420_Planar has I420 FourCC") {
        PixelDesc pd(PixelDesc::YUV8_420_Planar_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("I420"));
}

TEST_CASE("PixelDesc: YUV10_420_Planar_LE is valid") {
        PixelDesc pd(PixelDesc::YUV10_420_Planar_LE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::P_420_3x10_LE);
}

TEST_CASE("PixelDesc: YUV12_420_Planar_BE is valid") {
        PixelDesc pd(PixelDesc::YUV12_420_Planar_BE_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::P_420_3x12_BE);
}

// ============================================================================
// Semi-planar 4:2:0 PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: YUV8_420_SemiPlanar is valid") {
        PixelDesc pd(PixelDesc::YUV8_420_SemiPlanar_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 2);
        CHECK(pd.pixelFormat().id() == PixelFormat::SP_420_8);
}

TEST_CASE("PixelDesc: YUV8_420_SemiPlanar has NV12 FourCC") {
        PixelDesc pd(PixelDesc::YUV8_420_SemiPlanar_Rec709);
        CHECK(pd.fourccList()[0] == FourCC("NV12"));
}

// ============================================================================
// registeredIDs
// ============================================================================

TEST_CASE("PixelDesc: registeredIDs includes all formats") {
        auto ids = PixelDesc::registeredIDs();
        CHECK(ids.size() >= 132);
        CHECK(ids.contains(PixelDesc::RGBA8_sRGB));
        CHECK(ids.contains(PixelDesc::YUV8_422_UYVY_Rec709));
        CHECK(ids.contains(PixelDesc::YUV10_422_v210_Rec709));
        CHECK(ids.contains(PixelDesc::YUV8_422_Planar_Rec709));
        CHECK(ids.contains(PixelDesc::YUV8_420_Planar_Rec709));
        CHECK(ids.contains(PixelDesc::YUV8_420_SemiPlanar_Rec709));
        CHECK(ids.contains(PixelDesc::YUV12_420_SemiPlanar_BE_Rec709));
}

// ============================================================================
// Automatic iteration over ALL registered PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: all registered descs have valid properties") {
        auto ids = PixelDesc::registeredIDs();
        for(auto id : ids) {
                PixelDesc pd(id);
                CHECK(pd.isValid());
                CHECK_FALSE(pd.name().isEmpty());
                CHECK(pd.pixelFormat().isValid());
                CHECK(pd.colorModel().isValid());
                CHECK(PixelDesc::lookup(pd.name()) == pd);
        }
}

// ============================================================================
// BGRA
// ============================================================================

TEST_CASE("PixelDesc: BGRA8_sRGB") {
        SUBCASE("component order and alpha") {
                PixelDesc pd(PixelDesc::BGRA8_sRGB);
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

TEST_CASE("PixelDesc: BGR8_sRGB") {
        SUBCASE("component order and no alpha") {
                PixelDesc pd(PixelDesc::BGR8_sRGB);
                CHECK(pd.isValid());
                CHECK_FALSE(pd.hasAlpha());
                CHECK(pd.compSemantic(0).abbrev == "B");
        }
}

// ============================================================================
// ARGB
// ============================================================================

TEST_CASE("PixelDesc: ARGB8_sRGB") {
        SUBCASE("alpha-first component order") {
                PixelDesc pd(PixelDesc::ARGB8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "A");
                CHECK(pd.hasAlpha());
                CHECK(pd.alphaCompIndex() == 0);
        }
}

// ============================================================================
// ABGR
// ============================================================================

TEST_CASE("PixelDesc: ABGR8_sRGB") {
        SUBCASE("alpha-first blue-first component order") {
                PixelDesc pd(PixelDesc::ABGR8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "A");
                CHECK(pd.compSemantic(1).abbrev == "B");
                CHECK(pd.alphaCompIndex() == 0);
        }
}

// ============================================================================
// Monochrome
// ============================================================================

TEST_CASE("PixelDesc: Mono8_sRGB") {
        SUBCASE("single luminance component") {
                PixelDesc pd(PixelDesc::Mono8_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 1);
                CHECK(pd.compSemantic(0).name == "Luminance");
        }
}

// ============================================================================
// Float RGBA
// ============================================================================

TEST_CASE("PixelDesc: RGBAF16_LE_LinearRec709") {
        SUBCASE("float range and alpha") {
                PixelDesc pd(PixelDesc::RGBAF16_LE_LinearRec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1.0));
                CHECK(pd.hasAlpha());
                CHECK(pd.colorModel().id() == ColorModel::LinearRec709);
        }
}

// ============================================================================
// Float Mono
// ============================================================================

TEST_CASE("PixelDesc: MonoF32_LE_LinearRec709") {
        SUBCASE("float mono properties") {
                PixelDesc pd(PixelDesc::MonoF32_LE_LinearRec709);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 1);
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1.0));
        }
}

// ============================================================================
// RGB10A2
// ============================================================================

TEST_CASE("PixelDesc: RGB10A2_LE_sRGB") {
        SUBCASE("10-bit RGB with 2-bit alpha") {
                PixelDesc pd(PixelDesc::RGB10A2_LE_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1023));
                CHECK(pd.compSemantic(3).rangeMax == doctest::Approx(3));
                CHECK(pd.hasAlpha());
        }
}

// ============================================================================
// YCbCr 4:4:4
// ============================================================================

TEST_CASE("PixelDesc: YUV8_Rec709 (4:4:4)") {
        SUBCASE("4:4:4 YCbCr properties") {
                PixelDesc pd(PixelDesc::YUV8_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "Y");
                CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
        }
}

// ============================================================================
// Rec.2020
// ============================================================================

TEST_CASE("PixelDesc: YUV10_422_UYVY_LE_Rec2020") {
        SUBCASE("Rec.2020 color model") {
                PixelDesc pd(PixelDesc::YUV10_422_UYVY_LE_Rec2020);
                CHECK(pd.isValid());
                CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec2020);
        }
}

// ============================================================================
// Rec.601
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422_Rec601") {
        SUBCASE("Rec.601 color model") {
                PixelDesc pd(PixelDesc::YUV8_422_Rec601);
                CHECK(pd.isValid());
                CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec601);
        }
}

// ============================================================================
// NV21
// ============================================================================

TEST_CASE("PixelDesc: YUV8_420_NV21_Rec709") {
        SUBCASE("NV21 validity and semantics") {
                PixelDesc pd(PixelDesc::YUV8_420_NV21_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 3);
                CHECK(pd.planeCount() == 2);
                CHECK(pd.compSemantic(0).abbrev == "Y");
        }
}

// ============================================================================
// NV16 (semi-planar 4:2:2)
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422_SemiPlanar_Rec709") {
        SUBCASE("NV16 validity") {
                PixelDesc pd(PixelDesc::YUV8_422_SemiPlanar_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compCount() == 3);
                CHECK(pd.planeCount() == 2);
        }
}

// ============================================================================
// 4:1:1 planar
// ============================================================================

TEST_CASE("PixelDesc: YUV8_411_Planar_Rec709") {
        SUBCASE("4:1:1 planar validity") {
                PixelDesc pd(PixelDesc::YUV8_411_Planar_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).abbrev == "Y");
        }
}

// ============================================================================
// 16-bit YCbCr
// ============================================================================

TEST_CASE("PixelDesc: YUV16_LE_Rec709") {
        SUBCASE("16-bit limited range") {
                PixelDesc pd(PixelDesc::YUV16_LE_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.compSemantic(0).rangeMin == doctest::Approx(4096));
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(60160));
        }
}

// ============================================================================
// DPX additional packed formats
// ============================================================================

TEST_CASE("PixelDesc: RGB10_DPX_LE_sRGB") {
        SUBCASE("validity and pixel format") {
                PixelDesc pd(PixelDesc::RGB10_DPX_LE_sRGB);
                CHECK(pd.isValid());
                CHECK(pd.id() == PixelDesc::RGB10_DPX_LE_sRGB);
                CHECK(pd.name() == "RGB10_DPX_LE_sRGB");
                CHECK(pd.pixelFormat().id() == PixelFormat::I_3x10_DPX);
        }
        SUBCASE("component range is 10-bit full") {
                PixelDesc pd(PixelDesc::RGB10_DPX_LE_sRGB);
                CHECK(pd.compSemantic(0).rangeMin == doctest::Approx(0));
                CHECK(pd.compSemantic(0).rangeMax == doctest::Approx(1023));
                CHECK(pd.compSemantic(0).abbrev == "R");
                CHECK(pd.compSemantic(1).abbrev == "G");
                CHECK(pd.compSemantic(2).abbrev == "B");
        }
}

TEST_CASE("PixelDesc: YUV10_DPX_B_Rec709") {
        SUBCASE("validity and pixel format") {
                PixelDesc pd(PixelDesc::YUV10_DPX_B_Rec709);
                CHECK(pd.isValid());
                CHECK(pd.id() == PixelDesc::YUV10_DPX_B_Rec709);
                CHECK(pd.name() == "YUV10_DPX_B_Rec709");
                CHECK(pd.pixelFormat().id() == PixelFormat::I_3x10_DPX_B);
        }
        SUBCASE("component semantics are YCbCr") {
                PixelDesc pd(PixelDesc::YUV10_DPX_B_Rec709);
                CHECK(pd.compSemantic(0).abbrev == "Y");
                CHECK(pd.compSemantic(1).abbrev == "Cb");
                CHECK(pd.compSemantic(2).abbrev == "Cr");
        }
}

// ============================================================================
// hasPaintEngine()
// ============================================================================

TEST_CASE("PixelDesc: hasPaintEngine") {
        SUBCASE("default (invalid) PixelDesc returns false") {
                PixelDesc pd;
                CHECK_FALSE(pd.hasPaintEngine());
        }

        SUBCASE("RGB formats with a registered paint engine return true") {
                // The library ships a PaintEngine for interleaved 8-bit RGB/RGBA sRGB formats.
                CHECK(PixelDesc(PixelDesc::RGBA8_sRGB).hasPaintEngine());
                CHECK(PixelDesc(PixelDesc::RGB8_sRGB).hasPaintEngine());
        }

        SUBCASE("YUV formats have registered paint engines") {
                CHECK(PixelDesc(PixelDesc::YUV8_422_Rec709).hasPaintEngine());
                CHECK(PixelDesc(PixelDesc::YUV8_422_UYVY_Rec709).hasPaintEngine());
                CHECK(PixelDesc(PixelDesc::YUV8_420_SemiPlanar_Rec709).hasPaintEngine());
                CHECK(PixelDesc(PixelDesc::YUV8_420_Planar_Rec709).hasPaintEngine());
                CHECK(PixelDesc(PixelDesc::YUV10_422_Rec709).hasPaintEngine());
        }

        SUBCASE("compressed formats have no paint engine") {
                CHECK_FALSE(PixelDesc(PixelDesc::H264).hasPaintEngine());
                CHECK_FALSE(PixelDesc(PixelDesc::HEVC).hasPaintEngine());
                CHECK_FALSE(PixelDesc(PixelDesc::JPEG_RGB8_sRGB).hasPaintEngine());
        }
}

// ============================================================================
// JPEG XS compressed formats (ISO/IEC 21122)
// ============================================================================

TEST_CASE("PixelDesc: JPEG_XS_YUV8_422_Rec709 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_YUV8_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelDesc: JPEG_XS_YUV10_422_Rec709 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_YUV10_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelDesc: JPEG_XS_YUV12_422_Rec709 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_YUV12_422_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelDesc: JPEG_XS_YUV8_420_Rec709 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_YUV8_420_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelDesc: JPEG_XS_YUV10_420_Rec709 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_YUV10_420_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelDesc: JPEG_XS_YUV12_420_Rec709 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_YUV12_420_Rec709);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
}

TEST_CASE("PixelDesc: JPEG_XS_RGB8_sRGB is compressed") {
        PixelDesc pd(PixelDesc::JPEG_XS_RGB8_sRGB);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.videoCodec().id() == VideoCodec::JPEG_XS);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelDesc: JPEG XS YCbCr entries have correct encode/decode targets") {
        // Each JPEG XS YCbCr variant must have exactly one encode
        // source (the matching planar uncompressed format) and at
        // least one decode target.
        struct Case {
                PixelDesc::ID jpegXs;
                PixelDesc::ID uncompressed;
        };
        const Case cases[] = {
                { PixelDesc::JPEG_XS_YUV8_422_Rec709,  PixelDesc::YUV8_422_Planar_Rec709 },
                { PixelDesc::JPEG_XS_YUV10_422_Rec709, PixelDesc::YUV10_422_Planar_LE_Rec709 },
                { PixelDesc::JPEG_XS_YUV12_422_Rec709, PixelDesc::YUV12_422_Planar_LE_Rec709 },
                { PixelDesc::JPEG_XS_YUV8_420_Rec709,  PixelDesc::YUV8_420_Planar_Rec709 },
                { PixelDesc::JPEG_XS_YUV10_420_Rec709, PixelDesc::YUV10_420_Planar_LE_Rec709 },
                { PixelDesc::JPEG_XS_YUV12_420_Rec709, PixelDesc::YUV12_420_Planar_LE_Rec709 },
        };
        for(const auto &c : cases) {
                PixelDesc pd(c.jpegXs);
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

TEST_CASE("PixelDesc: JPEG_XS_RGB8_sRGB encode/decode targets") {
        PixelDesc pd(PixelDesc::JPEG_XS_RGB8_sRGB);
        REQUIRE(pd.isValid());
        bool foundPlanar = false;
        for(const auto &src : pd.encodeSources()) {
                if(src == PixelDesc::RGB8_Planar_sRGB) { foundPlanar = true; break; }
        }
        CHECK(foundPlanar);
        bool foundTarget = false;
        for(const auto &tgt : pd.decodeTargets()) {
                if(tgt == PixelDesc::RGB8_Planar_sRGB) { foundTarget = true; break; }
        }
        CHECK(foundTarget);
}

TEST_CASE("PixelDesc: JPEG XS fourcc is jxsm") {
        // ISO/IEC 21122-3 ISOBMFF sample entry is "jxsm".
        PixelDesc pd(PixelDesc::JPEG_XS_YUV10_422_Rec709);
        REQUIRE(pd.isValid());
        bool foundFourcc = false;
        for(const auto &cc : pd.fourccList()) {
                if(cc == "jxsm") { foundFourcc = true; break; }
        }
        CHECK(foundFourcc);
}

TEST_CASE("PixelDesc: JPEG XS string name round-trip via registeredIDs") {
        // All JPEG XS entries must appear in the registeredIDs list
        // and must have non-empty string names that start with
        // "JPEG_XS_".
        const PixelDesc::ID jxsIds[] = {
                PixelDesc::JPEG_XS_YUV8_422_Rec709,
                PixelDesc::JPEG_XS_YUV10_422_Rec709,
                PixelDesc::JPEG_XS_YUV12_422_Rec709,
                PixelDesc::JPEG_XS_YUV8_420_Rec709,
                PixelDesc::JPEG_XS_YUV10_420_Rec709,
                PixelDesc::JPEG_XS_YUV12_420_Rec709,
                PixelDesc::JPEG_XS_RGB8_sRGB,
        };
        auto allIds = PixelDesc::registeredIDs();
        for(PixelDesc::ID id : jxsIds) {
                bool foundInRegistry = false;
                for(const auto &regId : allIds) {
                        if(regId == id) { foundInRegistry = true; break; }
                }
                CHECK_MESSAGE(foundInRegistry,
                              "JPEG XS ID ", (int)id,
                              " not in registeredIDs");
                PixelDesc pd(id);
                REQUIRE(pd.isValid());
                CHECK(pd.name().startsWith("JPEG_XS_"));
        }
}

TEST_CASE("PixelDesc: JPEG XS component semantics have expected ranges") {
        // The 10-bit 4:2:2 entry should have Rec.709 limited-range
        // component semantics: Y 64-940, Cb/Cr 64-960.
        PixelDesc pd(PixelDesc::JPEG_XS_YUV10_422_Rec709);
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
