/**
 * @file      tests/v4l2m2mcodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Coverage for the V4L2 mem2mem codec engine and the V4l2VideoEncoder
 * backend.  Two tiers:
 *
 *   1. Host-safe policy / discovery / graceful-degradation cases that
 *      run anywhere (no codec node required):
 *        - the NV12 input-format advertisement,
 *        - device discovery returning empty for a format no node has,
 *        - open() failing cleanly on a bad path,
 *        - the encoder rejecting non-NV12 input before it ever touches a
 *          device, and erroring (not crashing) when no node exists.
 *
 *   2. An integration harness that drives the full @ref V4l2M2mCodec
 *      encode state machine (raw in → coded out → flush → EOS) against
 *      the kernel `vicodec` FWHT test driver.  It auto-detects the node
 *      and is a no-op when vicodec is not loaded, so the suite stays
 *      green on hosts without it.  To run it:
 *          sudo modprobe vicodec
 *          ./unittest-promeki -tc='*Vicodec*'
 *      This proves the queue setup / QBUF-DQBUF / pts passthrough /
 *      drain logic before real Xilinx VCU / Raspberry Pi hardware is
 *      available.  (FWHT is a different codec than H.264, so this
 *      validates the V4L2 plumbing, not bitstream conformance.)
 */

#include <doctest/doctest.h>
#include "codectesthelpers.h"

#include <promeki/v4l2m2mcodec.h>
#include <promeki/v4l2rawformat.h>
#include <promeki/v4l2codecparams.h>
#include <promeki/v4l2videoencoder.h>
#include <promeki/v4l2videodecoder.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/ciepoint.h>
#include <promeki/v4l2captionsei.h>
#include <promeki/h264bitstream.h>
#include <promeki/videoencodersei.h>
#include <promeki/anctranslator.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/enums_anc.h>
#include <promeki/cea708cdp.h>
#include <promeki/variant.h>
#include <promeki/videocodec.h>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/bufferview.h>

#include <promeki/dmaheap.h>
#include <promeki/memspace.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <cstring>
#include <unistd.h>

using namespace promeki;

// ---------------------------------------------------------------------------
// Input format advertisement
// ---------------------------------------------------------------------------

TEST_CASE("V4l2VideoEncoder_SupportedInputFormats") {
        // NV12 (4:2:0 8b), NV16 (4:2:2 8b), P010 (4:2:0 10b).
        List<int> inputs = V4l2VideoEncoder::supportedInputList();
        CHECK(inputs.contains(static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec709)));
        CHECK(inputs.contains(static_cast<int>(PixelFormat::YUV8_422_SemiPlanar_Rec709)));
        CHECK(inputs.contains(static_cast<int>(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709)));
}

// ---------------------------------------------------------------------------
// Device discovery
// ---------------------------------------------------------------------------

TEST_CASE("V4l2M2mCodec_FindDeviceBogusFormatIsEmpty") {
        // A made-up FourCC pair can never be enumerated by any node, so the
        // scan must come back empty regardless of what is plugged in.
        const uint32_t bogus = v4l2_fourcc('Z', 'Z', 'Z', 'Z');
        String         dev = V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, bogus, bogus);
        CHECK(dev.isEmpty());
}

// ---------------------------------------------------------------------------
// open() on a bad path fails cleanly
// ---------------------------------------------------------------------------

TEST_CASE("V4l2M2mCodec_OpenBadPathFails") {
        V4l2M2mCodec             codec;
        V4l2M2mCodec::OpenParams op;
        op.role = V4l2M2mCodec::Role::Encoder;
        op.devPath = String("/dev/promeki-no-such-video-node");
        op.outputFourcc = V4L2_PIX_FMT_NV12;
        op.captureFourcc = V4L2_PIX_FMT_H264;
        op.size = Size2Du32(320, 240);
        CHECK(codec.open(op).isError());
        CHECK_FALSE(codec.isOpen());
}

// ---------------------------------------------------------------------------
// Encoder rejects non-NV12 input before touching any device
// ---------------------------------------------------------------------------

TEST_CASE("V4l2VideoEncoder_RejectsNonNV12Input") {
        V4l2VideoEncoder enc(VideoCodec::H264);
        MediaConfig      cfg;
        enc.configure(cfg);

        // RGB is not an accepted input; the format check happens in
        // ensureSession ahead of any open(), so this is deterministic on
        // every host.
        auto rgb = UncompressedVideoPayload::allocate(ImageDesc(64, 64, PixelFormat(PixelFormat::RGB8_sRGB)));
        REQUIRE(rgb.isValid());
        Error err = enc.submitFrame(tests::frameWith(rgb));
        CHECK(err.isError());

        // A default-constructed Frame carries no video payload.
        CHECK(enc.submitFrame(Frame()).isError());
}

// ---------------------------------------------------------------------------
// Encoder degrades gracefully when no hardware codec node is present
// ---------------------------------------------------------------------------

TEST_CASE("V4l2VideoEncoder_NoDeviceSubmitErrors") {
        // Only meaningful on a host with no H.264 mem2mem encoder.  When a
        // real node exists (VCU / Pi) this submit may legitimately succeed,
        // so the assertion is gated on the discovery result.
        const bool haveEncoder =
                !V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_H264)
                         .isEmpty();
        if (haveEncoder) return;

        V4l2VideoEncoder enc(VideoCodec::H264);
        MediaConfig      cfg;
        enc.configure(cfg);

        auto nv12 = UncompressedVideoPayload::allocate(
                ImageDesc(320, 240, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709)));
        REQUIRE(nv12.isValid());
        Error err = enc.submitFrame(tests::frameWith(nv12));
        CHECK(err.isError()); // DeviceNotFound — no node to open.
}

// ---------------------------------------------------------------------------
// Backend registry consistency
// ---------------------------------------------------------------------------

TEST_CASE("V4l2VideoEncoder_RegisteredOnlyWhenDevicePresent") {
        // The "V4L2" backend self-registers for H.264 only when a matching
        // encoder node was discovered at static-init time.  Whatever the
        // host, the registry view must agree with a fresh discovery scan.
        const bool haveEncoder =
                !V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_H264)
                         .isEmpty();

        VideoCodec              h264(VideoCodec::H264);
        VideoCodec::BackendList backends = h264.availableEncoderBackends();
        bool                    v4l2Listed = false;
        for (const auto &b : backends) {
                if (VideoCodec(VideoCodec::H264, b).toString().contains("V4L2")) v4l2Listed = true;
        }
        CHECK(v4l2Listed == haveEncoder);
}

// ---------------------------------------------------------------------------
// Decoder: host-safe policy / discovery / graceful-degradation
// ---------------------------------------------------------------------------

TEST_CASE("V4l2VideoDecoder_SupportedOutputFormats") {
        List<int> outputs = V4l2VideoDecoder::supportedOutputList();
        CHECK(outputs.contains(static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec709)));
        CHECK(outputs.contains(static_cast<int>(PixelFormat::YUV8_422_SemiPlanar_Rec709)));
        CHECK(outputs.contains(static_cast<int>(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709)));
}

TEST_CASE("V4l2VideoDecoder_NoDeviceSubmitErrors") {
        // Only meaningful with no H.264 mem2mem decoder present.
        const bool haveDecoder =
                !V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Decoder, V4L2_PIX_FMT_H264, V4L2_PIX_FMT_NV12)
                         .isEmpty();
        if (haveDecoder) return;

        V4l2VideoDecoder dec(VideoCodec::H264);
        MediaConfig      cfg;
        dec.configure(cfg);

        // A small dummy H.264 access unit — the decoder must fail at open()
        // (no node), not crash, before it ever interprets the bytes.
        Buffer b(16);
        b.setSize(16);
        BufferView v(b, 0, 16);
        ImageDesc  cd(Size2Du32(640, 480), PixelFormat(PixelFormat::H264));
        auto       pkt = CompressedVideoPayload::Ptr::create(cd, v);
        Frame      f;
        f.addPayload(pkt);
        CHECK(dec.submitFrame(f).isError());

        // No compressed payload at all → rejected.
        CHECK(dec.submitFrame(Frame()).isError());
}

TEST_CASE("V4l2VideoDecoder_RegisteredOnlyWhenDevicePresent") {
        const bool haveDecoder =
                !V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Decoder, V4L2_PIX_FMT_H264, V4L2_PIX_FMT_NV12)
                         .isEmpty();

        VideoCodec              h264(VideoCodec::H264);
        VideoCodec::BackendList backends = h264.availableDecoderBackends();
        bool                    v4l2Listed = false;
        for (const auto &b : backends) {
                if (VideoCodec(VideoCodec::H264, b).toString().contains("V4L2")) v4l2Listed = true;
        }
        CHECK(v4l2Listed == haveDecoder);
}

// ---------------------------------------------------------------------------
// Raw-format pack/unpack math — host-safe (no device).  Validates the
// semi-planar byte copy and the P010 10-bit MSB-align shift by round-tripping
// a known raster through pack → (simulated V4L2 buffer) → unpack and checking
// it survives bit-exact.  Covers NV12 (4:2:0 8b), NV16 (4:2:2 8b), P010
// (4:2:0 10b) — the 4:2:2 and 10-bit pro formats this change adds.
// ---------------------------------------------------------------------------

TEST_CASE("V4l2RawFormat_PackUnpackRoundtrip") {
        const uint32_t fourccs[] = {V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_NV16, V4L2_PIX_FMT_P010};
        const uint32_t W = 64;
        const uint32_t H = 32;

        for (uint32_t fourcc : fourccs) {
                const V4l2RawFormat *rf = v4l2RawFormatForFourcc(fourcc);
                REQUIRE(rf != nullptr);
                CAPTURE(rf->name);
                const int      mask = (rf->bytesPerSample == 1) ? 0xFF : 0x3FF;
                const uint32_t cRows = H / rf->chromaVDiv;

                auto setSample = [&](uint8_t *base, size_t stride, uint32_t x, uint32_t y, int val) {
                        if (rf->bytesPerSample == 1) {
                                base[y * stride + x] = static_cast<uint8_t>(val & 0xFF);
                        } else {
                                auto *w16 = reinterpret_cast<uint16_t *>(base + y * stride);
                                w16[x] = static_cast<uint16_t>(val & 0x3FF); // promeki: LSB-aligned
                        }
                };

                // --- Build a source payload with a deterministic raster ---
                auto src = UncompressedVideoPayload::allocate(ImageDesc(W, H, PixelFormat(rf->pixelFormatId)));
                REQUIRE(src.isValid());
                REQUIRE(src->planeCount() == 2);
                UncompressedVideoPayload *s = src.modify();
                const PixelMemLayout     &ml = s->desc().pixelFormat().memLayout();
                for (uint32_t y = 0; y < H; ++y)
                        for (uint32_t x = 0; x < W; ++x)
                                setSample(s->data()[0].data(), ml.lineStride(0, W), x, y, (x * 3 + y * 5) & mask);
                for (uint32_t y = 0; y < cRows; ++y)
                        for (uint32_t x = 0; x < W; ++x)
                                setSample(s->data()[1].data(), ml.lineStride(1, W), x, y, (x * 7 + y * 11) & mask);

                // --- Pack into a simulated single-plane V4L2 OUTPUT buffer ---
                const size_t stride = static_cast<size_t>(W) * rf->bytesPerSample;
                const size_t total = stride * H + stride * cRows;
                Buffer       v4l2buf(total);
                v4l2buf.setSize(total);
                V4l2M2mCodec::OutPlane op;
                op.data = static_cast<uint8_t *>(v4l2buf.data());
                op.capacity = total;
                op.stride = stride;
                List<V4l2M2mCodec::OutPlane> outPlanes;
                outPlanes.pushToBack(op);
                List<size_t> used;
                v4l2PackSemiPlanar(*s, *rf, outPlanes, used);
                REQUIRE(used.size() == 1);
                CHECK(used[0] == total);

                // 10-bit must be MSB-aligned in the V4L2 buffer (low 6 bits zero).
                if (rf->bytesPerSample == 2) {
                        const auto *w16 = reinterpret_cast<const uint16_t *>(v4l2buf.data());
                        CHECK((w16[0] & 0x3F) == 0);
                        CHECK((w16[1] & 0x3F) == 0);
                }

                // --- Unpack back into a fresh payload and compare bit-exact ---
                auto dst = UncompressedVideoPayload::allocate(ImageDesc(W, H, PixelFormat(rf->pixelFormatId)));
                REQUIRE(dst.isValid());
                UncompressedVideoPayload        *d = dst.modify();
                V4l2M2mCodec::CapturePlane       cp;
                cp.data = static_cast<const uint8_t *>(v4l2buf.data());
                cp.size = total;
                cp.stride = stride;
                List<V4l2M2mCodec::CapturePlane> capPlanes;
                capPlanes.pushToBack(cp);
                v4l2UnpackSemiPlanar(capPlanes, *rf, d);

                auto planeEqual = [&](int plane, uint32_t rows) {
                        const uint8_t *a = s->data()[plane].data();
                        const uint8_t *b = d->data()[plane].data();
                        const size_t   st = ml.lineStride(plane, W);
                        const size_t   rb = static_cast<size_t>(W) * rf->bytesPerSample;
                        for (uint32_t y = 0; y < rows; ++y)
                                if (std::memcmp(a + y * st, b + y * st, rb) != 0) return false;
                        return true;
                };
                CHECK(planeEqual(0, H));      // luma
                CHECK(planeEqual(1, cRows));  // interleaved CbCr
        }
}

// ---------------------------------------------------------------------------
// Codec-parameter mappings — host-safe (pure functions).  Profile/level
// controls and VUI colour only take effect on a real H.264/HEVC node, but the
// H.273 → V4L2 colorimetry table and the profile/level enum tables are pure
// and validated here.
// ---------------------------------------------------------------------------

TEST_CASE("V4l2CodecParams_ColorimetryFromH273") {
        // BT.709 SDR, limited range.
        {
                V4l2Colorimetry c = v4l2ColorimetryFromH273(/*prim*/ 1, /*trans*/ 1, /*matrix*/ 1, false);
                CHECK(c.colorspace == V4L2_COLORSPACE_REC709);
                CHECK(c.ycbcrEnc == V4L2_YCBCR_ENC_709);
                CHECK(c.xferFunc == V4L2_XFER_FUNC_709);
                CHECK(c.quantization == V4L2_QUANTIZATION_LIM_RANGE);
        }
        // BT.2020 + PQ (HDR10), limited range.
        {
                V4l2Colorimetry c = v4l2ColorimetryFromH273(/*prim*/ 9, /*trans*/ 16, /*matrix*/ 9, false);
                CHECK(c.colorspace == V4L2_COLORSPACE_BT2020);
                CHECK(c.ycbcrEnc == V4L2_YCBCR_ENC_BT2020);
                CHECK(c.xferFunc == V4L2_XFER_FUNC_SMPTE2084);
        }
        // BT.601 525 (SMPTE 170M), full range.
        {
                V4l2Colorimetry c = v4l2ColorimetryFromH273(/*prim*/ 6, /*trans*/ 1, /*matrix*/ 6, true);
                CHECK(c.colorspace == V4L2_COLORSPACE_SMPTE170M);
                CHECK(c.ycbcrEnc == V4L2_YCBCR_ENC_601);
                CHECK(c.quantization == V4L2_QUANTIZATION_FULL_RANGE);
        }
        // HLG (ARIB STD-B67, H.273 code 18) has no V4L2 transfer function → DEFAULT.
        {
                V4l2Colorimetry c = v4l2ColorimetryFromH273(/*prim*/ 9, /*trans*/ 18, /*matrix*/ 9, false);
                CHECK(c.xferFunc == V4L2_XFER_FUNC_DEFAULT);
        }
        // Unspecified (code 2) → DEFAULT.
        {
                V4l2Colorimetry c = v4l2ColorimetryFromH273(2, 2, 2, false);
                CHECK(c.colorspace == V4L2_COLORSPACE_DEFAULT);
                CHECK(c.ycbcrEnc == V4L2_YCBCR_ENC_DEFAULT);
                CHECK(c.xferFunc == V4L2_XFER_FUNC_DEFAULT);
        }
}

TEST_CASE("V4l2CodecParams_ProfileLevel") {
        // H.264 profiles (H264Profile values: 3=High 4=High10 5=High422 6=High444).
        CHECK(v4l2H264Profile(3) == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
        CHECK(v4l2H264Profile(4) == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10);
        CHECK(v4l2H264Profile(5) == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422);
        CHECK(v4l2H264Profile(6) == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE);
        CHECK(v4l2H264Profile(0) == -1); // Auto → unset

        // H.264 levels (level_idc = level × 10).
        CHECK(v4l2H264Level(41) == V4L2_MPEG_VIDEO_H264_LEVEL_4_1);
        CHECK(v4l2H264Level(51) == V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
        CHECK(v4l2H264Level(0) == -1);
        CHECK(v4l2H264Level(99) == -1); // unmappable

        // HEVC profiles (wire tokens, case-insensitive).
        CHECK(v4l2HevcProfile("main") == V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
        CHECK(v4l2HevcProfile("Main10") == V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10);
        CHECK(v4l2HevcProfile("main422-10") == -1); // no mainline V4L2 enum
        CHECK(v4l2HevcProfile("") == -1);

        // HEVC levels.
        CHECK(v4l2HevcLevel(51) == V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
        CHECK(v4l2HevcLevel(40) == V4L2_MPEG_VIDEO_HEVC_LEVEL_4);
        CHECK(v4l2HevcLevel(0) == -1);
}

TEST_CASE("V4l2CodecParams_Hdr10Structs") {
        // BT.2020 primaries + D65 white, 0.005–1000 cd/m² — a typical HDR10
        // mastering display.
        const MasteringDisplay md(CIEPoint(0.708, 0.292),   // red
                                  CIEPoint(0.170, 0.797),   // green
                                  CIEPoint(0.131, 0.046),   // blue
                                  CIEPoint(0.3127, 0.3290), // D65 white
                                  /*minLum*/ 0.005, /*maxLum*/ 1000.0);
        REQUIRE(md.isValid());
        struct v4l2_ctrl_hdr10_mastering_display m = v4l2MakeMasteringDisplay(md);

        // SEI primary order is green, blue, red; chromaticity in 0.00002 units.
        CHECK(m.display_primaries_x[0] == 8500);  // green x: 0.170 × 50000
        CHECK(m.display_primaries_y[0] == 39850); // green y: 0.797 × 50000
        CHECK(m.display_primaries_x[1] == 6550);  // blue x:  0.131 × 50000
        CHECK(m.display_primaries_y[1] == 2300);  // blue y:  0.046 × 50000
        CHECK(m.display_primaries_x[2] == 35400); // red x:   0.708 × 50000
        CHECK(m.display_primaries_y[2] == 14600); // red y:   0.292 × 50000
        CHECK(m.white_point_x == 15635);          // 0.3127 × 50000
        CHECK(m.white_point_y == 16450);          // 0.3290 × 50000
        // Luminance in 0.0001 cd/m² units.
        CHECK(m.max_display_mastering_luminance == 10000000u); // 1000 × 10000
        CHECK(m.min_display_mastering_luminance == 50u);       // 0.005 × 10000

        // Content light level — 1 cd/m² units, copied through.
        struct v4l2_ctrl_hdr10_cll_info c = v4l2MakeCllInfo(ContentLightLevel(1000, 400));
        CHECK(c.max_content_light_level == 1000);
        CHECK(c.max_pic_average_light_level == 400);

        // 16-bit field clamp.
        struct v4l2_ctrl_hdr10_cll_info cc = v4l2MakeCllInfo(ContentLightLevel(70000, 70000));
        CHECK(cc.max_content_light_level == 65535);
        CHECK(cc.max_pic_average_light_level == 65535);
}

// ---------------------------------------------------------------------------
// Caption SEI bitstream surgery — host-safe (pure bitstream ops, no device).
// ---------------------------------------------------------------------------

namespace {
        // Builds an Annex-B access unit from raw NAL byte sequences.
        Buffer makeAccessUnit(const List<List<uint8_t>> &nals) {
                List<Buffer>     bufs;
                List<BufferView> views;
                for (const List<uint8_t> &nb : nals) {
                        Buffer b(nb.size());
                        std::memcpy(b.data(), nb.data(), nb.size());
                        b.setSize(nb.size());
                        bufs.pushToBack(b);
                }
                for (const Buffer &b : bufs) views.pushToBack(BufferView(b, 0, b.size()));
                Buffer out;
                H264Bitstream::wrapNalsAsAnnexB(views, out);
                return out;
        }
        bool buffersEqual(const Buffer &a, const Buffer &b) {
                if (a.size() != b.size()) return false;
                return std::memcmp(a.data(), b.data(), a.size()) == 0;
        }
} // namespace

TEST_CASE("V4l2CaptionSei_BuildExtractRoundtrip") {
        // A payload body deliberately full of byte sequences that require
        // emulation-prevention (00 00 00 / 00 00 01 / 00 00 03), so the
        // escape-on-build / unescape-on-extract path is exercised.
        const uint8_t bodyBytes[] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x03,
                                     0xAB, 0xFF, 0x00, 0x00, 0x02, 0x55};
        Buffer        body(sizeof(bodyBytes));
        std::memcpy(body.data(), bodyBytes, sizeof(bodyBytes));
        body.setSize(sizeof(bodyBytes));

        for (bool hevc : {false, true}) {
                CAPTURE(hevc);
                Buffer nal = v4l2BuildSeiNal(VideoEncoderSei::TypeUserDataRegistered,
                                             BufferView(body, 0, body.size()), hevc);
                REQUIRE(nal.size() > body.size()); // header + FF-coding + EP

                // The built NAL must contain no raw start-code emulation.
                const uint8_t *p = static_cast<const uint8_t *>(nal.data());
                for (size_t i = 0; i + 2 < nal.size(); ++i) {
                        const bool emul = p[i] == 0 && p[i + 1] == 0 && p[i + 2] <= 0x01;
                        CHECK_FALSE(emul);
                }

                // Wrap as an Annex-B AU and extract the body back.
                List<Buffer> nals;
                nals.pushToBack(nal);
                List<BufferView> views;
                views.pushToBack(BufferView(nal, 0, nal.size()));
                Buffer au;
                H264Bitstream::wrapNalsAsAnnexB(views, au);

                List<Buffer> got = v4l2ExtractSeiPayloads(BufferView(au, 0, au.size()),
                                                          VideoEncoderSei::TypeUserDataRegistered, hevc);
                REQUIRE(got.size() == 1);
                CHECK(buffersEqual(got[0], body));

                // A different payload type is not returned.
                CHECK(v4l2ExtractSeiPayloads(BufferView(au, 0, au.size()), 137, hevc).isEmpty());
        }
}

TEST_CASE("V4l2CaptionSei_InjectBeforeFirstVcl") {
        // Synthetic H.264 AU: SPS(7), PPS(8), IDR slice(5).
        Buffer au = makeAccessUnit({
                {0x67, 0x42, 0x00, 0x0a}, // SPS
                {0x68, 0xce, 0x3c, 0x80}, // PPS
                {0x65, 0x88, 0x80, 0x10}, // IDR slice (VCL)
        });

        const uint8_t capBody[] = {0xB5, 0x00, 0x31, 'G', 'A', '9', '4', 0x03};
        Buffer        body(sizeof(capBody));
        std::memcpy(body.data(), capBody, sizeof(capBody));
        body.setSize(sizeof(capBody));
        Buffer sei =
                v4l2BuildSeiNal(VideoEncoderSei::TypeUserDataRegistered, BufferView(body, 0, body.size()), false);

        List<Buffer> seis;
        seis.pushToBack(sei);
        Buffer out;
        REQUIRE_FALSE(v4l2InjectSeiNals(BufferView(au, 0, au.size()), seis, false, out).isError());

        // Walk the result: the SEI(6) must appear before the IDR(5), after PPS(8).
        List<int> order;
        H264Bitstream::forEachAnnexBNal(BufferView(out, 0, out.size()),
                                        [&](const H264Bitstream::NalUnit &n) -> Error {
                                                order.pushToBack(n.header0 & 0x1f);
                                                return Error::Ok;
                                        });
        REQUIRE(order.size() == 4);
        // Find SEI (6) and first VCL (5) positions.
        int seiPos = -1, vclPos = -1;
        for (int i = 0; i < static_cast<int>(order.size()); ++i) {
                if (order[i] == 6 && seiPos < 0) seiPos = i;
                if (order[i] == 5 && vclPos < 0) vclPos = i;
        }
        REQUIRE(seiPos >= 0);
        REQUIRE(vclPos >= 0);
        CHECK(seiPos < vclPos);
}

TEST_CASE("V4l2CaptionSei_FullCaptionPipeline") {
        // End-to-end, no device: CEA-708 ANC → VideoEncoderSei::captions →
        // build/inject SEI NAL → extract → AncTranslator parse back to CEA-708.
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        triples.pushToBack({true, 0, 'h' | 0x80, 'i' | 0x80});
        triples.pushToBack({true, 0, '!' | 0x80, 0x80});
        Cea708Cdp     cdp(4, triples, 7);
        AncTranslator t;
        AncTranslator::PacketsResult built =
                t.build(Variant(cdp), AncFormat(AncFormat::Cea708), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncPayload::Ptr anc = AncPayload::Ptr::create(AncDesc(), built.first());
        REQUIRE(anc.isValid());

        Frame source;
        source.addPayload(anc);

        // Encode side: build the caption SEI payloads, wrap + inject into a
        // synthetic coded AU.
        List<VideoEncoderSei::SeiPayload> seis = VideoEncoderSei::captions(source, 0, t);
        REQUIRE(seis.size() >= 1);
        CHECK(seis[0].type == VideoEncoderSei::TypeUserDataRegistered);

        List<Buffer> nals;
        for (const VideoEncoderSei::SeiPayload &sp : seis) {
                nals.pushToBack(v4l2BuildSeiNal(sp.type, BufferView(sp.bytes, 0, sp.bytes.size()), false));
        }
        Buffer codedAu = makeAccessUnit({{0x65, 0x88, 0x80, 0x10}}); // a lone IDR slice
        Buffer injected;
        REQUIRE_FALSE(v4l2InjectSeiNals(BufferView(codedAu, 0, codedAu.size()), nals, false, injected).isError());

        // Decode side: extract the caption SEI and parse it back to CEA-708.
        List<Buffer> bodies = v4l2ExtractSeiPayloads(BufferView(injected, 0, injected.size()),
                                                     VideoEncoderSei::TypeUserDataRegistered, false);
        REQUIRE(bodies.size() == 1);
        CHECK(buffersEqual(bodies[0], seis[0].bytes)); // survived the bitstream round-trip

        AncPacket                    hls(AncFormat(AncFormat::Cea708), AncTransport::HlsSei, bodies[0]);
        AncTranslator::PacketsResult back = t.translate(hls, AncTransport::St291);
        REQUIRE(back.second().isOk());
        CHECK(back.first().size() >= 1); // recovered a CEA-708 packet
}

// ===========================================================================
// Integration harness — drives the real engine against the `vicodec` FWHT
// test driver.  No-op unless `sudo modprobe vicodec` has been run.
// ===========================================================================

namespace {

        // Writes a deterministic raster into the OUTPUT buffer's planes for the
        // chosen raw FourCC and returns the per-plane bytesused.  Content is
        // arbitrary (FWHT does not care) but varies with `seed` so successive
        // frames differ.  Each row's leading bytes are filled; stride padding is
        // left untouched.
        List<size_t> fillRaw(uint32_t fourcc, const List<V4l2M2mCodec::OutPlane> &planes, uint32_t w, uint32_t h,
                             int seed) {
                List<size_t>  used;
                const uint8_t luma = static_cast<uint8_t>((0x40 + seed * 7) & 0xff);
                if (fourcc == V4L2_PIX_FMT_YUYV) {
                        // Packed 4:2:2, single plane, bytesperline = w*2.
                        const V4l2M2mCodec::OutPlane &p = planes[0];
                        const size_t stride = p.stride ? p.stride : static_cast<size_t>(w) * 2;
                        for (uint32_t r = 0; r < h; ++r) std::memset(p.data + stride * r, luma, w * 2);
                        used.pushToBack(stride * h);
                        return used;
                }

                // Semi-planar (NV12 4:2:0 / NV16 4:2:2): Y plane (w x h) then
                // interleaved CbCr (w x h/chromaVDiv), either as two separate
                // planes or one contiguous plane.
                const V4l2RawFormat *rf = v4l2RawFormatForFourcc(fourcc);
                const uint32_t       cRows = h / (rf ? rf->chromaVDiv : 2);
                if (planes.size() >= 2) {
                        const size_t yStride = planes[0].stride ? planes[0].stride : w;
                        const size_t cStride = planes[1].stride ? planes[1].stride : w;
                        for (uint32_t r = 0; r < h; ++r) std::memset(planes[0].data + yStride * r, luma, w);
                        for (uint32_t r = 0; r < cRows; ++r) std::memset(planes[1].data + cStride * r, 128, w);
                        used.pushToBack(yStride * h);
                        used.pushToBack(cStride * cRows);
                } else {
                        const size_t stride = planes[0].stride ? planes[0].stride : w;
                        uint8_t     *base = planes[0].data;
                        for (uint32_t r = 0; r < h; ++r) std::memset(base + stride * r, luma, w);
                        uint8_t *cbase = base + stride * h;
                        for (uint32_t r = 0; r < cRows; ++r) std::memset(cbase + stride * r, 128, w);
                        used.pushToBack(stride * h + stride * cRows);
                }
                return used;
        }

} // namespace

TEST_CASE("V4l2M2mCodec_VicodecEncodeStateMachine") {
        // Locate a vicodec FWHT encoder node accepting a raw format we can
        // fill.  vicodec's FWHT table includes NV12; YUYV is a fallback.
        struct Cand {
                        uint32_t    fourcc;
                        const char *name;
        };
        const Cand cands[] = {{V4L2_PIX_FMT_NV12, "NV12"}, {V4L2_PIX_FMT_YUYV, "YUYV"}};
        uint32_t   rawFourcc = 0;
        const char *rawName = nullptr;
        String     dev;
        for (const Cand &c : cands) {
                String d = V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, c.fourcc, V4L2_PIX_FMT_FWHT);
                if (!d.isEmpty()) {
                        rawFourcc = c.fourcc;
                        rawName = c.name;
                        dev = d;
                        break;
                }
        }
        if (rawFourcc == 0) {
                MESSAGE("vicodec FWHT encoder node not present (run: sudo modprobe vicodec); "
                        "skipping integration harness");
                return;
        }
        MESSAGE("vicodec encoder at " << doctest::String(dev.cstr()) << " raw=" << doctest::String(rawName));

        // 640x480 is at/above vicodec's 640x360 minimum and is universally
        // supported by the real targets (VCU / Pi) too.
        const uint32_t W = 640;
        const uint32_t H = 480;
        const int      N = 8;

        V4l2M2mCodec             codec;
        V4l2M2mCodec::OpenParams op;
        op.role = V4l2M2mCodec::Role::Encoder;
        op.outputFourcc = rawFourcc;
        op.captureFourcc = V4L2_PIX_FMT_FWHT;
        op.size = Size2Du32(W, H);
        REQUIRE_FALSE(codec.open(op).isError());
        REQUIRE_FALSE(codec.start().isError());

        // The driver may align the requested geometry up (the engine's
        // documented contract); it must never shrink below what we asked for.
        const uint32_t negW = codec.negotiatedWidth();
        const uint32_t negH = codec.negotiatedHeight();
        CHECK(negW >= W);
        CHECK(negH >= H);

        List<int64_t> gotPts;
        int           produced = 0;
        bool          sawEos = false;

        // Non-blocking drain of every ready coded buffer.
        auto pump = [&]() {
                for (;;) {
                        Buffer  coded;
                        int64_t pts = 0;
                        bool    kf = false;
                        bool    eos = false;
                        Error   e = codec.dequeueCapture(coded, pts, kf, eos);
                        if (eos) sawEos = true;
                        if (e == Error::Ok) {
                                CHECK(coded.size() > 0);
                                ++produced;
                                gotPts.pushToBack(pts);
                                if (eos) break;
                                continue;
                        }
                        break; // NotReady or a device error.
                }
        };

        // Feed N raw frames, using the input index as the pts tag.
        for (int i = 0; i < N; ++i) {
                int                          idx = -1;
                List<V4l2M2mCodec::OutPlane> planes;
                Error                        a = codec.acquireOutput(idx, planes);
                for (int spins = 0; a != Error::Ok && spins < 100; ++spins) {
                        pump();
                        bool ow = false, cr = false;
                        codec.poll(ow, cr, 200);
                        a = codec.acquireOutput(idx, planes);
                }
                REQUIRE(a == Error::Ok);
                REQUIRE(idx >= 0);
                REQUIRE_FALSE(planes.isEmpty());

                List<size_t> used = fillRaw(rawFourcc, planes, negW, negH, i);
                REQUIRE_FALSE(codec.submitOutput(idx, used, static_cast<int64_t>(i)).isError());
                pump();
        }

        // Drain: STOP, then pump until the LAST sentinel.
        codec.sendStop();
        for (int spins = 0; spins < 200 && !sawEos; ++spins) {
                pump();
                if (sawEos) break;
                bool  ow = false, cr = false;
                Error pe = codec.poll(ow, cr, 200);
                if (pe == Error::Timeout) {
                        pump();
                        break;
                }
        }

        // FWHT is intra-only: one coded frame per submitted raw frame.
        CHECK(produced == N);
        CHECK(sawEos);

        // The OUTPUT buffer timestamp (our input counter) must thread through
        // to the matching CAPTURE buffer, in order.
        REQUIRE(gotPts.size() == static_cast<size_t>(produced));
        bool ordered = true;
        for (int i = 0; i < static_cast<int>(gotPts.size()); ++i) {
                if (gotPts[i] != static_cast<int64_t>(i)) ordered = false;
        }
        CHECK(ordered);

        codec.close();
        CHECK_FALSE(codec.isOpen());
}

TEST_CASE("V4l2M2mCodec_VicodecColorimetry") {
        // The engine must apply the requested VUI colorimetry to the raw OUTPUT
        // format so the codec writes a matching bitstream VUI.  vicodec is a
        // standard vb2 driver that preserves the colorimetry fields, so we can
        // verify they round-trip through S_FMT.
        const String dev =
                V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_FWHT);
        if (dev.isEmpty()) {
                MESSAGE("vicodec FWHT encoder node not present; skipping colorimetry check");
                return;
        }

        V4l2M2mCodec             codec;
        V4l2M2mCodec::OpenParams op;
        op.role = V4l2M2mCodec::Role::Encoder;
        op.outputFourcc = V4L2_PIX_FMT_NV12;
        op.captureFourcc = V4L2_PIX_FMT_FWHT;
        op.size = Size2Du32(640, 480);
        // BT.2020 + PQ (HDR10), limited range — what the encoder would derive
        // from a Rec.2020 PQ source via v4l2ColorimetryFromH273.
        op.colorspace = V4L2_COLORSPACE_BT2020;
        op.ycbcrEnc = V4L2_YCBCR_ENC_BT2020;
        op.xferFunc = V4L2_XFER_FUNC_SMPTE2084;
        op.quantization = V4L2_QUANTIZATION_LIM_RANGE;
        REQUIRE_FALSE(codec.open(op).isError());

        CHECK(codec.negotiatedColorspace() == V4L2_COLORSPACE_BT2020);
        CHECK(codec.negotiatedYcbcrEnc() == V4L2_YCBCR_ENC_BT2020);
        CHECK(codec.negotiatedXferFunc() == V4L2_XFER_FUNC_SMPTE2084);
        CHECK(codec.negotiatedQuantization() == V4L2_QUANTIZATION_LIM_RANGE);

        // The compound-control path (used for the HDR10 mastering-display /
        // CLL controls) must form a valid S_EXT_CTRLS and degrade gracefully
        // when the driver lacks the control (vicodec has no HDR10 controls):
        // optional → Ok, no error.
        struct v4l2_ctrl_hdr10_cll_info cll = v4l2MakeCllInfo(ContentLightLevel(1000, 400));
        CHECK_FALSE(codec.setControlCompound(V4L2_CID_COLORIMETRY_HDR10_CLL_INFO, &cll, sizeof(cll)).isError());
        codec.close();
}

TEST_CASE("V4l2M2mCodec_VicodecDmabufImport") {
        // Zero-copy encode input: dma-heap NV12 frames imported straight into
        // the codec's OUTPUT queue via V4L2_MEMORY_DMABUF (no memcpy).  Needs
        // both a vicodec encoder node and a usable dma-heap.
        const String dev =
                V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_FWHT);
        if (dev.isEmpty()) {
                MESSAGE("vicodec encoder node not present; skipping dma-buf import");
                return;
        }
        if (!DmaHeap::isAvailable()) {
                MESSAGE("no usable dma-heap; skipping dma-buf import");
                return;
        }

        const uint32_t W = 640;
        const uint32_t H = 480;
        const int      N = 8;
        const size_t   frameSize = static_cast<size_t>(W) * H * 3 / 2; // NV12

        // One dma-buf per frame, kept alive for the whole session (the codec
        // reads them until DQBUF reclaims each).
        List<Buffer> frames;
        for (int i = 0; i < N; ++i) {
                Buffer b(frameSize, Buffer::DefaultAlign, MemSpace::Dmabuf);
                REQUIRE(b.isValid());
                REQUIRE(b.dmabufFd() >= 0);
                frames.pushToBack(b);
        }

        V4l2M2mCodec             codec;
        V4l2M2mCodec::OpenParams op;
        op.role = V4l2M2mCodec::Role::Encoder;
        op.outputFourcc = V4L2_PIX_FMT_NV12;
        op.captureFourcc = V4L2_PIX_FMT_FWHT;
        op.size = Size2Du32(W, H);
        op.outputMemory = V4l2M2mCodec::Memory::Dmabuf; // import raw frames
        op.captureMemory = V4l2M2mCodec::Memory::Mmap;  // coded output copied out
        REQUIRE_FALSE(codec.open(op).isError());
        REQUIRE_FALSE(codec.start().isError());

        int  produced = 0;
        bool sawEos = false;
        auto pump = [&]() {
                for (;;) {
                        Buffer  c;
                        int64_t p = 0;
                        bool    kf = false, e = false;
                        Error   r = codec.dequeueCapture(c, p, kf, e);
                        if (e) sawEos = true;
                        if (r == Error::Ok) {
                                CHECK(c.size() > 0);
                                ++produced;
                                if (e) break;
                                continue;
                        }
                        break;
                }
        };

        for (int i = 0; i < N; ++i) {
                bool  queued = false;
                Error q = codec.queueOutputDmabuf(frames[i].dmabufFd(), frameSize, i, queued);
                for (int s = 0; (q.isError() || !queued) && s < 100; ++s) {
                        pump();
                        bool ow = false, cr = false;
                        codec.poll(ow, cr, 200);
                        q = codec.queueOutputDmabuf(frames[i].dmabufFd(), frameSize, i, queued);
                }
                REQUIRE_FALSE(q.isError());
                REQUIRE(queued);
                pump();
        }

        codec.sendStop();
        for (int s = 0; s < 200 && !sawEos; ++s) {
                pump();
                if (sawEos) break;
                bool ow = false, cr = false;
                if (codec.poll(ow, cr, 200) == Error::Timeout) {
                        pump();
                        break;
                }
        }

        CHECK(produced == N); // one coded frame per imported dma-buf
        CHECK(sawEos);
        codec.close();
}

TEST_CASE("V4l2M2mCodec_VicodecExpbuf") {
        // Export a codec buffer as a dma-buf fd for a downstream consumer.
        const String dev =
                V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_FWHT);
        if (dev.isEmpty()) {
                MESSAGE("vicodec encoder node not present; skipping EXPBUF");
                return;
        }

        V4l2M2mCodec             codec;
        V4l2M2mCodec::OpenParams op;
        op.role = V4l2M2mCodec::Role::Encoder;
        op.outputFourcc = V4L2_PIX_FMT_NV12;
        op.captureFourcc = V4L2_PIX_FMT_FWHT;
        op.size = Size2Du32(640, 480);
        REQUIRE_FALSE(codec.open(op).isError());
        REQUIRE_FALSE(codec.start().isError());

        Result<int> fd = codec.exportBuffer(/*capture=*/true, 0);
        REQUIRE_FALSE(error(fd).isError());
        CHECK(value(fd) >= 0);
        if (value(fd) >= 0) ::close(value(fd));
        codec.close();
}

TEST_CASE("V4l2M2mCodec_DmabufApiGuards") {
        // Host-safe: dma-buf queue ops fail cleanly on a closed / wrong-memory codec.
        V4l2M2mCodec codec;
        bool         queued = true;
        CHECK(codec.queueOutputDmabuf(7, 1024, 0, queued).isError()); // not open
        CHECK_FALSE(queued);
        CHECK(error(codec.exportBuffer(false, 0)).isError()); // not open
}

// ===========================================================================
// Decoder integration harness — full round-trip through both engine roles:
// raw → FWHT (encoder node) → raw (decoder node), exercising the staged
// stateful-decoder bring-up (OUTPUT-only stream → SOURCE_CHANGE event →
// setupCapture → drain).  No-op unless vicodec is loaded.
// ===========================================================================

// Full raw→FWHT→raw round-trip through both engine roles for a given raw
// FourCC.  Parameterised so the same harness validates NV12 (4:2:0) and NV16
// (4:2:2).  No-op (early MESSAGE) when vicodec is not loaded.
static void vicodecRoundtrip(uint32_t rawFourcc, const char *rawName) {
        const String encDev =
                V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder, rawFourcc, V4L2_PIX_FMT_FWHT);
        const String decDev =
                V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Decoder, V4L2_PIX_FMT_FWHT, rawFourcc);
        if (encDev.isEmpty() || decDev.isEmpty()) {
                MESSAGE("vicodec encoder+decoder nodes not present for "
                        << doctest::String(rawName) << " (run: sudo modprobe vicodec); skipping");
                return;
        }

        const uint32_t W = 640;
        const uint32_t H = 480;
        const int      N = 8;

        // ---- Phase 1: encode N raw frames to FWHT ----
        List<Buffer> codedFrames;
        uint32_t     encW = 0, encH = 0;
        {
                V4l2M2mCodec             enc;
                V4l2M2mCodec::OpenParams op;
                op.role = V4l2M2mCodec::Role::Encoder;
                op.outputFourcc = rawFourcc;
                op.captureFourcc = V4L2_PIX_FMT_FWHT;
                op.size = Size2Du32(W, H);
                REQUIRE_FALSE(enc.open(op).isError());
                REQUIRE_FALSE(enc.start().isError());
                encW = enc.negotiatedWidth();
                encH = enc.negotiatedHeight();

                bool encEos = false;
                auto pumpEnc = [&]() {
                        for (;;) {
                                Buffer  c;
                                int64_t p = 0;
                                bool    kf = false, e = false;
                                Error   r = enc.dequeueCapture(c, p, kf, e);
                                if (e) encEos = true;
                                if (r == Error::Ok) {
                                        codedFrames.pushToBack(c);
                                        if (e) break;
                                        continue;
                                }
                                break;
                        }
                };
                for (int i = 0; i < N; ++i) {
                        int                          idx = -1;
                        List<V4l2M2mCodec::OutPlane> planes;
                        Error                        a = enc.acquireOutput(idx, planes);
                        for (int s = 0; a != Error::Ok && s < 100; ++s) {
                                pumpEnc();
                                bool ow = false, cr = false;
                                enc.poll(ow, cr, 200);
                                a = enc.acquireOutput(idx, planes);
                        }
                        REQUIRE(a == Error::Ok);
                        List<size_t> used = fillRaw(rawFourcc, planes, encW, encH, i);
                        REQUIRE_FALSE(enc.submitOutput(idx, used, static_cast<int64_t>(i)).isError());
                        pumpEnc();
                }
                enc.sendStop();
                for (int s = 0; s < 200 && !encEos; ++s) {
                        pumpEnc();
                        if (encEos) break;
                        bool ow = false, cr = false;
                        if (enc.poll(ow, cr, 200) == Error::Timeout) {
                                pumpEnc();
                                break;
                        }
                }
                enc.close();
        }
        REQUIRE(codedFrames.size() == static_cast<size_t>(N));

        // ---- Phase 2: decode FWHT back to raw ----
        V4l2M2mCodec             dec;
        V4l2M2mCodec::OpenParams dop;
        dop.role = V4l2M2mCodec::Role::Decoder;
        dop.devPath = decDev;
        dop.outputFourcc = V4L2_PIX_FMT_FWHT; // coded in
        dop.captureFourcc = rawFourcc;        // raw out
        dop.size = Size2Du32(encW, encH);
        REQUIRE_FALSE(dec.open(dop).isError());
        REQUIRE_FALSE(dec.startOutput().isError());

        int           decoded = 0;
        bool          decEos = false;
        bool          setupFailed = false;
        List<int64_t> decPts;
        auto          pumpDec = [&]() {
                for (;;) {
                        bool  sc = false, e = false;
                        Error r = dec.dequeueEvent(sc, e);
                        if (r != Error::Ok) break;
                        if (sc && !dec.captureConfigured()) {
                                if (dec.setupCapture().isError()) setupFailed = true;
                        }
                        if (e) decEos = true;
                }
                if (!dec.captureConfigured()) return;
                for (;;) {
                        List<V4l2M2mCodec::CapturePlane> planes;
                        int                              idx = -1;
                        int64_t                          p = 0;
                        bool                             e = false;
                        Error                            r = dec.dequeueRawFrame(planes, idx, p, e);
                        if (e) decEos = true;
                        if (r == Error::Ok) {
                                ++decoded;
                                decPts.pushToBack(p);
                                if (!e) dec.requeueRawFrame(idx);
                                if (e) break;
                                continue;
                        }
                        break;
                }
        };

        for (int i = 0; i < static_cast<int>(codedFrames.size()); ++i) {
                int                          idx = -1;
                List<V4l2M2mCodec::OutPlane> planes;
                Error                        a = dec.acquireOutput(idx, planes);
                for (int s = 0; a != Error::Ok && s < 100; ++s) {
                        bool ow = false, cr = false, ev = false;
                        dec.pollEvents(ow, cr, ev, 100);
                        pumpDec();
                        a = dec.acquireOutput(idx, planes);
                }
                REQUIRE(a == Error::Ok);
                REQUIRE_FALSE(planes.isEmpty());
                const Buffer &cf = codedFrames[i];
                const size_t  n = (cf.size() < planes[0].capacity) ? cf.size() : planes[0].capacity;
                std::memcpy(planes[0].data, cf.data(), n);
                List<size_t> used;
                used.pushToBack(n);
                REQUIRE_FALSE(dec.submitOutput(idx, used, static_cast<int64_t>(i)).isError());
                bool ow = false, cr = false, ev = false;
                dec.pollEvents(ow, cr, ev, 100);
                pumpDec();
        }

        dec.sendStop();
        for (int s = 0; s < 200 && !decEos; ++s) {
                bool ow = false, cr = false, ev = false;
                dec.pollEvents(ow, cr, ev, 200);
                pumpDec();
                if (decEos) break;
        }

        CHECK_FALSE(setupFailed);
        CHECK(dec.captureConfigured());
        // The decoder must recover the encoded resolution from the bitstream.
        CHECK(dec.captureWidth() == encW);
        CHECK(dec.captureHeight() == encH);
        // …and emit the requested raw chroma format.
        CHECK(dec.captureFourcc() == rawFourcc);
        // FWHT is intra-only: one decoded frame per coded frame.
        CHECK(decoded == N);
        CHECK(decEos);
        // pts threaded OUTPUT→CAPTURE through the decoder, in order.
        REQUIRE(decPts.size() == static_cast<size_t>(decoded));
        bool ordered = true;
        for (int i = 0; i < static_cast<int>(decPts.size()); ++i) {
                if (decPts[i] != static_cast<int64_t>(i)) ordered = false;
        }
        CHECK(ordered);

        dec.close();
}

TEST_CASE("V4l2M2mCodec_VicodecDecodeRoundtrip_NV12") {
        vicodecRoundtrip(V4L2_PIX_FMT_NV12, "NV12"); // 8-bit 4:2:0
}

TEST_CASE("V4l2M2mCodec_VicodecDecodeRoundtrip_NV16") {
        vicodecRoundtrip(V4L2_PIX_FMT_NV16, "NV16"); // 8-bit 4:2:2 — pro 4:2:2 path
}
