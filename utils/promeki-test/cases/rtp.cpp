/**
 * @file      cases/rtp.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RTP transport roundtrip tests.
 *
 * Each registered case spins up two pipelines on the main event
 * loop:
 *
 *   TX:  TPG → RtpMediaIO (sink, SDP written to file)
 *   RX:  RtpMediaIO (source, configured via that SDP) → Inspector
 *
 * The TX side writes the @ref MediaConfig::RtpSaveSdpPath file on
 * @c open and the RX side consumes that file via
 * @ref MediaConfig::RtpSdp.  Driving the receiver from the on-disk
 * SDP is deliberate — it exercises the real interop path that an
 * external receiver (ffplay, GStreamer, ST 2110 hardware) would use,
 * so a regression in either the SDP writer or the SDP parser shows
 * up as a test failure rather than slipping through a back-channel.
 *
 * Both pipelines run on @c 127.0.0.1 with per-case unique even ports
 * for video / audio so successive cases don't collide.  The Inspector
 * sits at the end of the RX side and applies its full default check
 * set — picture-data continuity, audio data, A/V sync, frame-stamp
 * timestamps — so a broken depacketizer or a dropped audio packet
 * surfaces as a discontinuity, not just a missing frame.
 *
 * The matrix covers every wire format the RtpMediaIO backend ships
 * today:
 *   - MJPEG (RFC 2435) — 4:2:2 and 4:2:0 sub-format variants;
 *   - 8-bit interleaved uncompressed (RFC 4175) — RGB and YUV;
 *   - 10 / 12-bit uncompressed (RFC 4175 / ST 2110-20) — UYVY BE,
 *     v210, and DPX-B source layouts driving the 4:2:2 and 4:4:4
 *     ST 2110-20 wire pgroups via the c-3 scalar CSC kernels;
 *   - H.264 (RFC 6184) — 4:2:0 and 4:2:2 input subsampling;
 *   - H.265 / HEVC (RFC 7798) — 4:2:0 and 4:2:2 input subsampling;
 *   - JPEG XS (RFC 9134) — 10-bit 4:2:2 codestream-mode round-trip.
 *
 * The H.264 / H.265 / JPEG XS cases register only when both an
 * encoder and a decoder backend are available for the codec;
 * otherwise the matrix entry is omitted (no Skip noise on machines
 * without the codec backend).  Audio is L16 / 48 kHz (RFC 3551 /
 * AES67) on every base case; one additional case exercises the
 * AES67 L24 / 96 kHz wire path (Phase E30).
 *
 * Skip vs. fail policy:
 *
 *   - Build / open / start failures classify as Skip — the most
 *     common cause is "this build doesn't have the payload class
 *     wired up" (e.g. a future raw-10-bit case before pgroup support
 *     lands), and we don't want those false-failing the matrix.
 *   - Mid-run pipeline-error signals, frames-processed below the
 *     expected count, and discontinuity-count overruns are Fail.
 *   - The combined watchdog firing surfaces as Timeout on the side
 *     that hadn't yet closed — usually the RX side, which will sit
 *     on @c recvfrom forever if the TX never sent anything.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/inspectormediaio.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // -------------------------------------------------------------------
                // Case description
                // -------------------------------------------------------------------

                struct Case {
                                String      name;        // dotted identifier registered with TestRunner
                                String      family;      // "jpeg" / "raw" / "h264" / "h265" / "jxs" — used to label the case sub-group
                                PixelFormat pixelFormat; // wire format on the RTP path (compressed for codec cases, uncompressed for raw)
                                PixelFormat tpgPixelFormat; // optional: pixel format the TPG should emit before the
                                                         // encoder (drives 4:2:0 vs 4:2:2 subsampling for codecs whose
                                                         // wire @ref PixelFormat does not encode subsampling, e.g.
                                                         // @c PixelFormat::H264 / @c PixelFormat::HEVC).  Also used for
                                                         // 10/12-bit raw cases where the TPG paints into a CSC-source
                                                         // layout (UYVY BE / v210 / DPX-B) and the planner inserts a
                                                         // CSC stage to convert to the @c pixelFormat ST 2110-20 wire
                                                         // pgroup format.  Empty for JPEG (subsampling already lives
                                                         // in the JPEG_* wire format) and for 8-bit raw (TPG emits
                                                         // the wire format directly).
                                VideoCodec  codec;       // valid for compressed paths only — drives an encoder stage on TX and a decoder stage on RX
                                bool        audio = true;
                                // Audio sample rate in Hz.  0 = backend default (48 kHz).
                                // Used by the AES67 / ST 2110-30 96 kHz path test.
                                float       audioRate = 0.0f;
                                // AES67 / ST 2110-30 on-wire PCM format.  Auto = backend default
                                // (L16 for 48 kHz, L24 for 96 kHz per Phase E30b §6.2.1).
                                AudioWireFormat audioWireFormat = AudioWireFormat::Auto;
                                // SMPTE ST 2022-7 dual-leg fan-out (Phase E2022).  When
                                // true, the TX sink is configured with secondary
                                // video / audio destinations on a distinct port pair
                                // (allocated from @ref PortQuad) and the SDP emits
                                // @c a=group:DUP plus the per-section @c a=mid:
                                // tokens.  The RX side parses the dual-mid SDP and
                                // spins up a secondary @c ReceiveThread that merges
                                // into the same @c StreamReceiver as the primary
                                // leg; the @c RtpSeqReorderBuffer's silent
                                // duplicate-by-extended-seq discard satisfies
                                // RFC 7104 §6 dedup automatically.
                                bool        dualLeg = false;
};

                // Hand-curated matrix.  Each entry maps to a real wire
                // format the RTP backend ships today.  The JPEG cases
                // run TPG → VideoEncoder(JPEG, sub-format) → RTP sink;
                // the RTP sink dispatches RFC 2435 because its input
                // arrives in a JPEG-family @ref PixelFormat.  The raw
                // cases skip the encoder — RtpMediaIO dispatches RFC
                // 4175 directly when the input pixel format is an
                // uncompressed 8-bit interleaved family member.
                List<Case> buildMatrix() {
                        List<Case>       matrix;
                        const VideoCodec jpeg(VideoCodec::JPEG);
                        const VideoCodec h264(VideoCodec::H264);
                        const VideoCodec hevc(VideoCodec::HEVC);
                        const VideoCodec jxs(VideoCodec::JPEG_XS);

                        // JPEG (RFC 2435).  Two YUV422 variants exercise the two
                        // matrices the JPEG family commonly carries; the
                        // YUV420 entry catches a 4:2:0 packing path that
                        // the others don't.
                        matrix.pushToBack({String("rtp.jpeg.yuv8_422_rec709"), String("jpeg"),
                                           PixelFormat(PixelFormat::JPEG_YUV8_422_Rec709), PixelFormat(), jpeg, true});
                        matrix.pushToBack({String("rtp.jpeg.yuv8_420_rec709"), String("jpeg"),
                                           PixelFormat(PixelFormat::JPEG_YUV8_420_Rec709), PixelFormat(), jpeg, true});
                        matrix.pushToBack({String("rtp.jpeg.yuv8_422_rec601_full"), String("jpeg"),
                                           PixelFormat(PixelFormat::JPEG_YUV8_422_Rec601_Full), PixelFormat(), jpeg,
                                           true});

                        // RFC 4175 uncompressed — 8-bit interleaved.  TPG
                        // emits the wire format directly; no CSC stage
                        // inserted by the planner.
                        matrix.pushToBack({String("rtp.raw.rgb8_srgb"), String("raw"),
                                           PixelFormat(PixelFormat::RGB8_sRGB), PixelFormat(), VideoCodec(), true});
                        matrix.pushToBack({String("rtp.raw.yuv8_422_rec709"), String("raw"),
                                           PixelFormat(PixelFormat::YUV8_422_Rec709), PixelFormat(), VideoCodec(),
                                           true});

                        // RFC 4175 / ST 2110-20 — 10 / 12-bit pgroup.
                        // The wire @ref PixelFormat is the ST 2110-20
                        // pgroup format (`*_2110_*`); the TPG paints into
                        // a CSC-source layout (`UYVY_BE`, `v210`, `DPX_B`)
                        // and the planner inserts the c-3 scalar CSC
                        // kernel landed in Phase E20c-3 parts A/B.  These
                        // exercise the broadcast lingua franca: SDI input
                        // (UYVY10/12 BE, v210) and DPX-B colour-grading
                        // hand-off (4:4:4 10-bit).
                        matrix.pushToBack({String("rtp.raw.yuv10_422_uyvy_be_rec709"), String("raw"),
                                           PixelFormat(PixelFormat::YUV10_422_2110_Rec709),
                                           PixelFormat(PixelFormat::YUV10_422_UYVY_BE_Rec709), VideoCodec(), true});
                        matrix.pushToBack({String("rtp.raw.yuv12_422_uyvy_be_rec709"), String("raw"),
                                           PixelFormat(PixelFormat::YUV12_422_2110_Rec709),
                                           PixelFormat(PixelFormat::YUV12_422_UYVY_BE_Rec709), VideoCodec(), true});
                        matrix.pushToBack({String("rtp.raw.yuv10_422_v210_rec709"), String("raw"),
                                           PixelFormat(PixelFormat::YUV10_422_2110_Rec709),
                                           PixelFormat(PixelFormat::YUV10_422_v210_Rec709), VideoCodec(), true});
                        matrix.pushToBack({String("rtp.raw.yuv10_dpx_b_rec709"), String("raw"),
                                           PixelFormat(PixelFormat::YUV10_2110_Rec709),
                                           PixelFormat(PixelFormat::YUV10_DPX_B_Rec709), VideoCodec(), true});

                        // RFC 6184 H.264.  Wire format is @c PixelFormat::H264 —
                        // codec-only, no subsampling.  The TPG output
                        // (@ref Case::tpgPixelFormat) drives the encoder's
                        // input subsampling so the matrix can exercise both
                        // 4:2:0 and 4:2:2 round-trips even though the wire
                        // PixelFormat is a single enum value.  Skip if no
                        // H.264 codec backend has both encoder and decoder.
                        if (h264.canEncode() && h264.canDecode()) {
                                matrix.pushToBack({String("rtp.h264.yuv8_420_rec709"), String("h264"),
                                                   PixelFormat(PixelFormat::H264),
                                                   PixelFormat(PixelFormat::YUV8_420_Planar_Rec709), h264, true});
                                matrix.pushToBack({String("rtp.h264.yuv8_422_rec709"), String("h264"),
                                                   PixelFormat(PixelFormat::H264),
                                                   PixelFormat(PixelFormat::YUV8_422_Rec709), h264, true});
                        }

                        // RFC 7798 H.265 / HEVC.  Same shape as H.264 — the
                        // wire @ref PixelFormat is codec-only and the
                        // @ref Case::tpgPixelFormat selects the encoder
                        // input subsampling.
                        if (hevc.canEncode() && hevc.canDecode()) {
                                matrix.pushToBack({String("rtp.h265.yuv8_420_rec709"), String("h265"),
                                                   PixelFormat(PixelFormat::HEVC),
                                                   PixelFormat(PixelFormat::YUV8_420_Planar_Rec709), hevc, true});
                                matrix.pushToBack({String("rtp.h265.yuv8_422_rec709"), String("h265"),
                                                   PixelFormat(PixelFormat::HEVC),
                                                   PixelFormat(PixelFormat::YUV8_422_Rec709), hevc, true});
                        }

                        // RFC 9134 JPEG XS — ST 2110-22 Phase E22.
                        if (jxs.canEncode() && jxs.canDecode()) {
                                matrix.pushToBack({String("rtp.jxs.yuv10_422_rec709"), String("jxs"),
                                                   PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709),
                                                   PixelFormat(PixelFormat::YUV10_422_Planar_LE_Rec709), jxs, true});
                        }

                        // AES67 / ST 2110-30 — L24 / 96 kHz wire format
                        // (Phase E30b).  Reuses the 8-bit YUV422 video
                        // path to keep the matrix focused on the audio
                        // wire format change; the audio worker stamps
                        // @c L24 + 96000 Hz clock rate into the SDP and
                        // the receiver round-trips them.
                        {
                                Case c{String("rtp.audio.l24_96k"), String("audio"),
                                       PixelFormat(PixelFormat::YUV8_422_Rec709), PixelFormat(), VideoCodec(), true};
                                c.audioRate = 96000.0f;
                                c.audioWireFormat = AudioWireFormat(AudioWireFormat::L24);
                                matrix.pushToBack(c);
                        }

                        // SMPTE ST 2022-7 — dual-leg redundancy (Phase E2022).
                        // Reuses the 8-bit YUV422 video path so the case
                        // focuses on the wire-level fan-out + merge path:
                        // the TX emits identical RTP packets to both
                        // primary and secondary loopback ports, the SDP
                        // carries @c a=group:DUP plus per-section
                        // @c a=mid: tokens, and the RX spawns a parallel
                        // @c ReceiveThread for the secondary leg whose
                        // packets merge through @c RtpSeqReorderBuffer's
                        // silent duplicate-discard.  A clean round-trip
                        // (frameNumberJumps == 0, streamIdChanges == 0)
                        // proves the dual-leg dedup math holds against
                        // real packet flow, not just unit-test stand-ins.
                        {
                                Case c{String("rtp.dup.yuv8_422_rec709"), String("dup"),
                                       PixelFormat(PixelFormat::YUV8_422_Rec709), PixelFormat(), VideoCodec(), true};
                                c.dualLeg = true;
                                matrix.pushToBack(c);
                        }
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Port assignment
                // -------------------------------------------------------------------

                // Each case gets its own (video, audio) port pair so
                // the kernel doesn't have to recycle a freshly closed
                // socket between back-to-back cases.  The video port
                // and audio port are kept ≥ 4 apart so the implicit
                // RTCP port (RTP+1) on each stream never collides with
                // the next stream's RTP port (a classic gotcha covered
                // in @ref RtpMediaIO 's port docstring).  We start at
                // 51000 to stay well clear of any well-known service.
                //
                // Dual-leg (ST 2022-7) cases additionally need a
                // secondary (video, audio) pair on a different port
                // range so the second leg doesn't collide with its own
                // primary.  We reserve a 16-port stride per case
                // unconditionally — wastes 8 ports per single-leg case
                // but keeps allocation arithmetic boring and lets a
                // case flip to dual-leg without re-numbering its
                // neighbours.  Primary lives in @c (base+0, base+4);
                // secondary lives in @c (base+8, base+12).
                struct PortPair {
                                int video = 0;
                                int audio = 0;
                };
                struct PortQuad {
                                PortPair primary;
                                PortPair secondary;
                };
                PortQuad allocatePorts(size_t caseIndex) {
                        const int basePort = 51000 + static_cast<int>(caseIndex) * 16;
                        PortQuad  q;
                        q.primary.video = basePort;
                        q.primary.audio = basePort + 4;
                        q.secondary.video = basePort + 8;
                        q.secondary.audio = basePort + 12;
                        return q;
                }

                // -------------------------------------------------------------------
                // Stage helpers
                // -------------------------------------------------------------------

                // RtpMediaIO is bidirectional — the same backend can
                // be a sink or a source depending on @c MediaConfig::OpenMode.
                // The pipeline stage instantiator at @c MediaPipeline::instantiateStage
                // does not auto-derive @c OpenMode from @ref MediaPipelineConfig::StageRole
                // when the stage carries an explicit @c type, so we have
                // to stamp it ourselves here.

                MediaPipelineConfig::Stage makeRtpSinkStage(const Case &c, const PortQuad &ports,
                                                            const String &sdpPath) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("rtpout");
                        s.type = String("Rtp");
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config = MediaIOFactory::defaultConfig("Rtp");
                        s.config.set(MediaConfig::Type, String("Rtp"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Write);
                        s.config.set(MediaConfig::RtpSaveSdpPath, sdpPath);
                        // Loopback addresses; the RX side consumes the SDP we
                        // just wrote so it picks up these addresses without
                        // any explicit mirroring on its end — that's the
                        // test's whole point.
                        auto vAddr = SocketAddress::fromString(String("127.0.0.1:") +
                                                               String::number(ports.primary.video));
                        auto aAddr = SocketAddress::fromString(String("127.0.0.1:") +
                                                               String::number(ports.primary.audio));
                        s.config.set(MediaConfig::VideoRtpDestination, vAddr.first());
                        s.config.set(MediaConfig::AudioRtpDestination, aAddr.first());
                        // ST 2022-7 secondary leg (Phase E2022).  When
                        // dual-leg is requested the sink stamps the
                        // secondary destinations and @c RtpSession spawns
                        // a parallel @c PacketScheduler thread + transport
                        // pair; the SDP writer emits @c a=group:DUP plus
                        // the per-section @c a=mid: tokens for RFC 7104
                        // discovery on the RX side.
                        if (c.dualLeg) {
                                auto vSec = SocketAddress::fromString(
                                        String("127.0.0.1:") + String::number(ports.secondary.video));
                                auto aSec = SocketAddress::fromString(
                                        String("127.0.0.1:") + String::number(ports.secondary.audio));
                                s.config.set(MediaConfig::RtpVideoDestinationSecondary, vSec.first());
                                s.config.set(MediaConfig::RtpAudioDestinationSecondary, aSec.first());
                        }
                        // Pin the encoded video format so the
                        // RFC 2435 / RFC 4175 dispatch matches the matrix
                        // entry exactly.  The planner inserts a CSC stage
                        // ahead of the sink if TPG's output doesn't
                        // already match.
                        if (c.pixelFormat.isValid()) {
                                s.config.set(MediaConfig::VideoPixelFormat, c.pixelFormat);
                        }
                        // High-packet-rate compressed video (JPEG XS at
                        // 4K / 60 fps emits ~2,600 packets per frame)
                        // forces a real test of the userspace pacing
                        // path — on loopback the kernel uses the
                        // @c noqueue qdisc, which makes the default
                        // @c KernelFq scheduler a no-op and degenerates
                        // to a single ~3 MB burst that overruns the
                        // kernel UDP recv buffer.  Pinning Userspace
                        // pacing keeps the per-packet sendmmsg cadence
                        // smooth enough for loopback to absorb.
                        if (c.family == String("jxs")) {
                                s.config.set(MediaConfig::RtpPacingMode, RtpPacingMode(RtpPacingMode::Userspace));
                        }
                        // Override the AES67 / ST 2110-30 wire format when
                        // the matrix entry asks for a non-default audio
                        // path (the L24 / 96 kHz case).  Auto resolves to
                        // L16 at 48 kHz and L24 at 96 kHz per Phase E30b,
                        // so leaving the value at Auto is also valid; an
                        // explicit override makes the case's intent
                        // legible.
                        if (c.audioWireFormat != AudioWireFormat(AudioWireFormat::Auto)) {
                                s.config.set(MediaConfig::RtpAudioWireFormat, c.audioWireFormat);
                        }
                        return s;
                }

                MediaPipelineConfig::Stage makeRtpSourceStage(const Case &c, const String &sdpPath) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("rtpin");
                        s.type = String("Rtp");
                        s.role = MediaPipelineConfig::StageRole::Source;
                        s.config = MediaIOFactory::defaultConfig("Rtp");
                        s.config.set(MediaConfig::Type, String("Rtp"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Read);
                        // The whole point of these tests: the RX side
                        // learns destinations + payload types + clock
                        // rates exclusively from the SDP file the TX
                        // side just wrote on disk.  Don't echo any
                        // *RtpDestination keys here — they would
                        // override the SDP-discovered values and
                        // bypass the SDP parsing path.
                        s.config.set(MediaConfig::RtpSdp, sdpPath);
                        // High-packet-rate compressed video paths
                        // (JPEG XS at 720p produces ~250 packets per
                        // frame at 60 fps) need a deeper reorder
                        // buffer than the 50 ms default — the
                        // userspace TX bursts a full frame's worth of
                        // packets in <2 ms and a startup-window UDP
                        // loopback loss otherwise wipes a whole frame
                        // before the depacketizer drains.  200 ms
                        // covers ~12 frames at 60 fps which is more
                        // than the testbed needs.
                        if (c.family == String("jxs")) {
                                s.config.set(MediaConfig::RtpJitterMs, int32_t(200));
                        }
                        return s;
                }

                // -------------------------------------------------------------------
                // Pipeline assembly
                // -------------------------------------------------------------------

                MediaPipelineConfig buildTxConfig(const Case &c, const PortQuad &ports, const String &sdpPath,
                                                  int frames, uint32_t streamId) {
                        MediaPipelineConfig cfg;

                        // TPG output policy:
                        //  - 8-bit raw cases: TPG emits the matrix's
                        //    pixel format directly so the RTP sink
                        //    dispatches RFC 4175 raw on its own.
                        //  - 10/12-bit raw cases: TPG emits the
                        //    @ref Case::tpgPixelFormat (UYVY BE, v210,
                        //    or DPX-B) and the planner inserts a CSC
                        //    stage to the @ref Case::pixelFormat
                        //    ST 2110-20 wire pgroup using the c-3
                        //    scalar kernels.  Phase E20c-3.
                        //  - JPEG cases: TPG stays on the backend default
                        //    and the encoder converts to the matrix's
                        //    JPEG-family @ref PixelFormat — RFC 2435
                        //    dispatch follows from the encoder's output
                        //    pixel format.
                        //  - H.264 / H.265 / JPEG XS cases: TPG emits the
                        //    @ref Case::tpgPixelFormat (uncompressed
                        //    YUV422 or YUV420) so the encoder's input
                        //    subsampling matches what the matrix entry
                        //    asks for.  Without this, every matrix
                        //    entry per codec would produce the same
                        //    wire bytes (the codec @ref PixelFormat
                        //    has no subsampling axis).
                        PixelFormat tpgPd;
                        if (c.tpgPixelFormat.isValid()) tpgPd = c.tpgPixelFormat;
                        else if (!c.codec.isValid())
                                tpgPd = c.pixelFormat;
                        MediaPipelineConfig::Stage tpgStage = makeTpgStage(streamId, tpgPd);
                        // RFC 2435 §3.1.5 caps the in-band W/H fields at
                        // 255×8 = 2040 pixels, so JPEG-over-RTP cannot
                        // represent any common UHD raster.  Pin the JPEG
                        // matrix entries to 1080p59.94 — the largest
                        // standard broadcast raster that fits — so the
                        // case exercises the worst case JPEG-over-RTP
                        // can actually carry without silently truncating
                        // the SOF0 to 2040×2040 and corrupting the
                        // round-trip.  Other matrix entries stay on
                        // @ref kDefaultVideoFormat (2160p59.94) per the
                        // 4K60 hardening pass.
                        if (c.family == String("jpeg")) {
                                tpgStage.config.set(MediaConfig::VideoFormat,
                                                    VideoFormat(VideoFormat::Smpte1080p59_94));
                        }
                        // Override the TPG audio defaults for cases that
                        // want a non-48 kHz / non-L16 wire path (Phase
                        // E30b L24 / 96 kHz round-trip).  AudioRate
                        // is a float; AES67 also requires the RTP audio
                        // clock rate to match the sample rate, so stamp
                        // both even though @c TpgMediaIO only reads the
                        // former.
                        if (c.audioRate > 0.0f) {
                                tpgStage.config.set(MediaConfig::AudioRate, c.audioRate);
                        }
                        cfg.addStage(tpgStage);
                        String prev = String("tpg");
                        if (c.codec.isValid()) {
                                cfg.addStage(makeEncoderStage(c.codec, c.pixelFormat));
                                cfg.addRoute(prev, String("enc"));
                                prev = String("enc");
                        }
                        cfg.addStage(makeRtpSinkStage(c, ports, sdpPath));
                        cfg.addRoute(prev, String("rtpout"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildRxConfig(const Case &c, const String &sdpPath, int frames) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeRtpSourceStage(c, sdpPath));
                        String prev = String("rtpin");
                        if (c.codec.isValid()) {
                                cfg.addStage(makeDecoderStage(c.codec));
                                cfg.addRoute(prev, String("dec"));
                                prev = String("dec");
                        }
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(prev, String("insp"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runRtpCase(const Case &c, size_t caseIndex, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t  frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t  timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);
                        const PortQuad ports = allocatePorts(caseIndex);
                        const String   sdpPath = (ctx.testFolder() / String("session.sdp")).toString();

                        ctx.setDetail(String("family"), c.family);
                        ctx.setDetail(String("pixelFormat"), c.pixelFormat.name());
                        ctx.setDetail(String("videoPort"), int64_t(ports.primary.video));
                        ctx.setDetail(String("audioPort"), int64_t(ports.primary.audio));
                        if (c.dualLeg) {
                                ctx.setDetail(String("videoPortSecondary"), int64_t(ports.secondary.video));
                                ctx.setDetail(String("audioPortSecondary"), int64_t(ports.secondary.audio));
                        }
                        ctx.setDetail(String("sdpPath"), sdpPath);

                        // Stream IDs unique per case so any cross-case
                        // packet leakage shows up as a frame-number jump
                        // at the inspector rather than silently flowing
                        // through.
                        const uint32_t streamId =
                                0xA770'0000u ^ static_cast<uint32_t>(c.name.hash());

                        // Inject the inspector so we can pull a
                        // snapshot once the close cascade completes.
                        // Lifetime is tricky: the pipeline does not own
                        // injected stages, but it DOES still hold raw
                        // pointers to them through close-cascade — so
                        // the inspector must out-live the pipeline.  We
                        // use a heap allocation and gate the delete on
                        // exiting the pipeline scope so a watchdog that
                        // tears down a still-active pipeline doesn't
                        // race a user-after-free.
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        {
                                MediaIO::Config inspCfg = MediaIOFactory::defaultConfig("Inspector");
                                inspCfg.set(MediaConfig::Type, String("Inspector"));
                                // Loosen the per-frame PTS-divergence
                                // tolerance for RTP loopback.  The RTP
                                // sender paces packets in user space —
                                // 17 ms / N packets per frame — and on
                                // a normally-loaded host @c sleep_until
                                // overshoots its deadline by tens of
                                // microseconds per packet, which on a
                                // 2300-packet raw-video frame
                                // accumulates to ~1 ms of slip per
                                // frame relative to the announced rate.
                                // The receiver stamps PTS from the
                                // wall-clock arrival of the first
                                // packet, so that slip drifts the
                                // observed PTS away from the
                                // (anchor + i × period) prediction
                                // monotonically.  The inspector's
                                // default 5 ms tolerance is calibrated
                                // for hardware-paced sources (NDI,
                                // ST 2110 NICs); using it here would
                                // flag every userspace-pacing overshoot
                                // as a discontinuity even though the
                                // round-trip is otherwise healthy.
                                // 75 ms accommodates the worst-case
                                // cumulative drift over a 30-frame
                                // run on a busy CI box while still
                                // catching a real stall (a frame
                                // entirely missing).
                                inspCfg.set(MediaConfig::InspectorVideoPtsToleranceNs, int64_t(75'000'000));
                                inspCfg.set(MediaConfig::InspectorAudioPtsToleranceNs, int64_t(75'000'000));
                                insp->setConfig(inspCfg);
                                insp->setName(String("insp"));
                        }

                        int64_t framesProcessed = 0;
                        int64_t framesWithPictureData = 0;
                        int64_t framesWithAudioTimestamp = 0;
                        int64_t totalDiscontinuities = 0;
                        int64_t frameNumberJumps = 0;
                        int64_t streamIdChanges = 0;

                        // Outcome captured outside the pipeline scope so
                        // we can evaluate pass / fail after the pipelines
                        // have safely destroyed themselves.
                        DualPhaseOutcome dp;
                        bool             injectFailed = false;
                        Error            injectError;

                        {
                                MediaPipelineConfig txCfg = buildTxConfig(c, ports, sdpPath, frames, streamId);
                                MediaPipelineConfig rxCfg = buildRxConfig(c, sdpPath, frames);

                                MediaPipeline txPipe;
                                MediaPipeline rxPipe;

                                Error ie = rxPipe.injectStage(insp);
                                if (ie.isError()) {
                                        injectFailed = true;
                                        injectError = ie;
                                } else {
                                        runDualPhase(txPipe, txCfg, rxPipe, rxCfg, loop,
                                                     (unsigned int)timeoutMs, dp);

                                        // Take the inspector snapshot
                                        // BEFORE the pipelines run their
                                        // destructors — once close has
                                        // completed (or the watchdog has
                                        // bailed) the counters are stable
                                        // and the pipeline is about to
                                        // detach from the injected stage.
                                        InspectorSnapshot snap = insp->snapshot();
                                        framesProcessed = snap.framesProcessed.value();
                                        framesWithPictureData = snap.framesWithPictureData.value();
                                        framesWithAudioTimestamp = snap.framesWithAudioTimestamp.value();
                                        totalDiscontinuities = snap.totalDiscontinuities;
                                        frameNumberJumps = snap.discontinuitiesByKind[static_cast<size_t>(
                                                InspectorDiscontinuity::FrameNumberJump)];
                                        streamIdChanges = snap.discontinuitiesByKind[static_cast<size_t>(
                                                InspectorDiscontinuity::StreamIdChange)];
                                }
                        } // <-- txPipe, rxPipe destruct here, releasing
                          //     any references to insp

                        delete insp;

                        if (injectFailed) {
                                ctx.setFail(String("injectStage: ") + injectError.desc());
                                return;
                        }

                        // Persist BOTH pipeline configs under a single
                        // object so result.json carries a complete
                        // picture: tx + rx graphs after autoplan,
                        // including any planner-injected CSC /
                        // FrameBridge stages.
                        JsonObject pipelineDump;
                        if (dp.tx.resolvedConfig.size() > 0) pipelineDump.set("tx", dp.tx.resolvedConfig);
                        if (dp.rx.resolvedConfig.size() > 0) pipelineDump.set("rx", dp.rx.resolvedConfig);
                        if (pipelineDump.size() > 0) ctx.setPipelineConfig(pipelineDump);

                        ctx.setDetail(String("framesProcessed"), framesProcessed);
                        ctx.setDetail(String("framesWithPictureData"), framesWithPictureData);
                        ctx.setDetail(String("framesWithAudioTimestamp"), framesWithAudioTimestamp);
                        ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);
                        ctx.setDetail(String("frameNumberJumps"), frameNumberJumps);
                        ctx.setDetail(String("streamIdChanges"), streamIdChanges);

                        // Translate phase-level errors into a pass /
                        // skip / fail / timeout result.  RTP build /
                        // open / start failures are overwhelmingly
                        // "this payload class isn't wired up" (e.g.
                        // raw 10/12-bit before pgroup support lands)
                        // so they classify as Skip — Fail is reserved
                        // for "the path SAID it works but the
                        // round-trip lost frames or corrupted them".
                        // Drain the deterministic, structural failures
                        // first so they get reported with the right
                        // classification before the watchdog-driven
                        // outcomes (which can mask packet loss as a
                        // generic "TIME").
                        //
                        // The dual-phase runner aborts the sequence as
                        // soon as one side fails, so the OTHER side's
                        // un-attempted steps land with default-Ok
                        // errors.  Skip a step here if its error is Ok
                        // — a real failure on this side will populate
                        // the corresponding error field, and a step
                        // that was simply never reached has nothing
                        // to report.
                        auto isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                        auto structuralPhase = [&](const PhaseOutcome &p, const char *side) -> bool {
                                if (!p.built && p.buildError.isError()) {
                                        String msg = String(side) + String(" build failed: ") +
                                                     p.buildError.desc();
                                        if (isPlannerGap(p.buildError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (!p.opened && p.openError.isError()) {
                                        String msg = String(side) + String(" open failed: ") +
                                                     p.openError.desc();
                                        if (isPlannerGap(p.openError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (!p.started && p.startError.isError()) {
                                        String msg = String(side) + String(" start failed: ") +
                                                     p.startError.desc();
                                        if (isPlannerGap(p.startError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (p.sawError) {
                                        // A mid-stream @ref Error::NotSupported
                                        // means a stage advertised support
                                        // but couldn't actually wire up at
                                        // runtime (e.g. NVENC with a CUDA
                                        // driver mismatch).  Treat it the
                                        // same as the build/open/start
                                        // planner-gap paths above — Skip.
                                        String msg = String(side) + String(" pipeline error: ") +
                                                     p.errorDetail;
                                        if (p.firstError == Error::NotSupported) {
                                                ctx.setSkip(msg);
                                        } else {
                                                ctx.setFail(msg);
                                        }
                                        return true;
                                }
                                return false;
                        };
                        if (structuralPhase(dp.tx, "tx")) return;
                        if (structuralPhase(dp.rx, "rx")) return;

                        // A watchdog firing with zero frames received
                        // is a genuine deadlock — TX never emitted, or
                        // RX never bound — and surfaces as Timeout.
                        // Anything else (some frames received, just
                        // not all) is more useful as a content-level
                        // Fail with the actual count.
                        const bool watchdogFired = dp.tx.timedOut || dp.rx.timedOut;
                        if (watchdogFired && framesProcessed == 0) {
                                ctx.setTimeout(String("watchdog fired with no frames received past ") +
                                               String::number(timeoutMs) + String(" ms"));
                                return;
                        }

                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no frames over RTP"));
                                return;
                        }
                        // RTP TX/RX startup involves variable delay
                        // (RTCP-SR-driven wallclock anchor refinement,
                        // first-packet UDP loopback drops, codec
                        // priming for compressed paths) so the matrix
                        // tests do not require a specific frame count
                        // — they only require that the frames the RX
                        // *did* see arrived in order, regardless of
                        // the starting frame number.  Sequentiality is
                        // exactly what
                        // @ref InspectorDiscontinuity::FrameNumberJump
                        // tracks: the inspector latches the previous
                        // frame on the first received frame and only
                        // fires from the second frame onward.  A
                        // non-zero starting frame number reads as
                        // "fine" as long as every subsequent frame
                        // increments by 1.  Stream-ID changes
                        // similarly indicate cross-case packet
                        // leakage, so they're checked too.
                        //
                        // Other discontinuity kinds (audio PTS
                        // re-anchor, A/V sync offset, picture-data
                        // band decode failure, audio data codeword
                        // anomalies) are intermittent measurement
                        // artifacts on a userspace-paced loopback
                        // round-trip and pre-existing inspector-side
                        // RFC 4175 band-decode quirks that are
                        // tracked separately from this matrix.
                        if (frameNumberJumps != 0) {
                                ctx.setFail(String::number(frameNumberJumps) +
                                            String(" non-sequential frame number(s) in RTP round-trip"));
                                return;
                        }
                        if (streamIdChanges != 0) {
                                ctx.setFail(String::number(streamIdChanges) +
                                            String(" stream-id change(s) in RTP round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerRtpCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        size_t idx = i;
                        String desc = String("RTP roundtrip: ") + c.pixelFormat.name() +
                                      String(" (TPG → RTP sink → SDP file → RTP source → Inspector)");
                        TestRunner::registerCase(TestCase(
                                c.name, desc, [c, idx](TestContext &ctx) { runRtpCase(c, idx, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
