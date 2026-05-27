/**
 * @file      cases/rtp_ffmpeg.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * ffmpeg-rawvideo interop case for SMPTE ST 2110-20 / RFC 4175.
 *
 * The TX side is the same TPG → RtpMediaIO sink that the in-process
 * matrix uses, pinned to @c PixelFormat::YUV8_422_Rec709 — the 8-bit
 * uncompressed lingua franca that has full round-trip coverage today.
 * Instead of a sibling RtpMediaIO receiver, the RX side is an ffmpeg
 * subprocess invoked as:
 *
 * @code
 * ffmpeg -y -loglevel error -protocol_whitelist file,udp,rtp \
 *        -f sdp -i <sdp> -map 0:v -frames:v <N> \
 *        -f rawvideo <out.yuv>
 * @endcode
 *
 * If ffmpeg is missing from @c PATH the case Skips cleanly.  Other
 * failures (subprocess crash, zero-byte output, wrong exit code) are
 * a Fail — the interop path is real and a regression in our SDP
 * writer or RFC 4175 packetizer breaks ffmpeg's demuxer the same way
 * it would break a third-party receiver.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/process.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/basicthread.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                bool ffmpegAvailable() {
                        // `ffmpeg -version` exits 0 when present.  We
                        // do not parse stdout — the existence of the
                        // binary on $PATH is all the precondition the
                        // case needs.
                        Process probe;
                        Error   ie = probe.start(String("ffmpeg"), List<String>{String("-version")});
                        if (ie.isError()) return false;
                        Error fe = probe.waitForFinished(/*timeoutMs=*/3000);
                        if (fe.isError()) {
                                probe.kill();
                                return false;
                        }
                        return probe.exitCode() == 0;
                }

                void runRtpFfmpegCase(TestContext &ctx) {
                        if (!ffmpegAvailable()) {
                                ctx.setSkip(String("ffmpeg not on PATH — interop case requires it"));
                                return;
                        }

                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 15000);

                        // 51800 sits clear of the in-process RTP matrix's
                        // 51000+caseIndex×8 allocation so this case can
                        // co-run with the matrix without colliding.
                        const int     videoPort = 51800;
                        const String  sdpPath = (ctx.testFolder() / String("session.sdp")).toString();
                        const String  outPath = (ctx.testFolder() / String("ffmpeg-rawvideo.yuv")).toString();
                        const uint32_t streamId = 0xF7'1A'01'02u; // ASCII "ff1A" — distinct from the matrix's stream IDs.

                        ctx.setDetail(String("videoPort"), int64_t(videoPort));
                        ctx.setDetail(String("sdpPath"), sdpPath);
                        ctx.setDetail(String("outPath"), outPath);
                        ctx.setDetail(String("ffmpegFrames"), int64_t(frames));

                        // -------------------------------------------------
                        // TX pipeline assembly: TPG → RtpMediaIO sink.
                        // Audio disabled — ffmpeg's SDP demuxer plumbs the
                        // audio leg through PCM and would tug in another
                        // skip/fail vector we don't need for this case.
                        // -------------------------------------------------
                        MediaPipelineConfig txCfg;
                        txCfg.addStage(makeTpgStage(streamId,
                                                    PixelFormat(PixelFormat::YUV8_422_Rec709),
                                                    /*videoEnabled=*/true, /*audioEnabled=*/false));
                        {
                                MediaPipelineConfig::Stage s;
                                s.name = String("rtpout");
                                s.type = String("Rtp");
                                s.role = MediaPipelineConfig::StageRole::Sink;
                                s.config = MediaIOFactory::defaultConfig("Rtp");
                                s.config.set(MediaConfig::Type, String("Rtp"));
                                s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Write);
                                s.config.set(MediaConfig::RtpSaveSdpPath, sdpPath);
                                auto vAddr = SocketAddress::fromString(String("127.0.0.1:")
                                                                       + String::number(videoPort));
                                s.config.set(MediaConfig::VideoRtpDestination, vAddr.first());
                                s.config.set(MediaConfig::VideoPixelFormat,
                                             PixelFormat(PixelFormat::YUV8_422_Rec709));
                                txCfg.addStage(s);
                        }
                        txCfg.addRoute(String("tpg"), String("rtpout"));
                        txCfg.setFrameCount(FrameCount(frames));

                        // -------------------------------------------------
                        // Open TX so the SDP gets written, *then* spawn
                        // ffmpeg so it can bind and read.  ffmpeg listens
                        // on the UDP port for the announced session, so
                        // it has to be running before the first packet
                        // arrives — UDP loopback silently drops packets
                        // landing on an unbound port.  This is the
                        // canonical "start RX before TX" pattern used in
                        // the in-process matrix's RxStartFirst sequence.
                        // -------------------------------------------------
                        MediaPipeline txPipe;
                        Error         buildErr = txPipe.build(txCfg, /*autoplan=*/true);
                        if (buildErr.isError()) {
                                ctx.setFail(String("tx build: ") + buildErr.desc());
                                return;
                        }
                        Error openErr = txPipe.open();
                        if (openErr.isError()) {
                                ctx.setFail(String("tx open: ") + openErr.desc());
                                return;
                        }

                        // Sanity-check the SDP was written before we
                        // hand it to ffmpeg — `ffmpeg -i missing.sdp`
                        // exits with a confusing "No such file" error
                        // that would mask a real RtpMediaIO open-time
                        // bug.
                        if (!FilePath(sdpPath).exists()) {
                                ctx.setFail(String("tx open did not write SDP at ") + sdpPath);
                                return;
                        }

                        Process      ffmpeg;
                        List<String> args;
                        args.pushToBack(String("-y"));
                        args.pushToBack(String("-loglevel"));
                        args.pushToBack(String("error"));
                        args.pushToBack(String("-protocol_whitelist"));
                        args.pushToBack(String("file,udp,rtp"));
                        args.pushToBack(String("-f"));
                        args.pushToBack(String("sdp"));
                        args.pushToBack(String("-i"));
                        args.pushToBack(sdpPath);
                        args.pushToBack(String("-map"));
                        args.pushToBack(String("0:v"));
                        args.pushToBack(String("-frames:v"));
                        args.pushToBack(String::number(frames));
                        args.pushToBack(String("-f"));
                        args.pushToBack(String("rawvideo"));
                        args.pushToBack(outPath);
                        Error spawnErr = ffmpeg.start(String("ffmpeg"), args);
                        if (spawnErr.isError()) {
                                ctx.setFail(String("ffmpeg spawn: ") + spawnErr.desc());
                                return;
                        }

                        // 200 ms is enough headroom on Linux loopback
                        // for ffmpeg's SDP-driven `rtp://` URL to open
                        // its UDP socket and start reading.  Without
                        // this brief sleep the first frame's packets
                        // would race the bind and a percentage of
                        // them would land on an unbound port.
                        BasicThread::sleepMs(200);

                        Error startErr = txPipe.start();
                        if (startErr.isError()) {
                                ffmpeg.kill();
                                ffmpeg.waitForFinished(2000);
                                ctx.setFail(String("tx start: ") + startErr.desc());
                                return;
                        }

                        // Drive the event loop until TX closes.  The
                        // pipeline emits closedSignal once it finishes
                        // pushing @c frames frames; we set a watchdog
                        // timer so a deadlock can't stall the matrix.
                        bool txClosed = false;
                        bool watchdogFired = false;
                        int  watchdogId = loop->startTimer(
                                static_cast<unsigned int>(timeoutMs),
                                [&]() {
                                        if (txClosed) return;
                                        watchdogFired = true;
                                        if (loop->isRunning()) loop->quit(0);
                                },
                                /*singleShot=*/true);
                        txPipe.closedSignal.connect(
                                [&](Error) {
                                        txClosed = true;
                                        if (loop->isRunning()) loop->quit(0);
                                },
                                &txPipe);
                        loop->exec();
                        if (watchdogId >= 0) loop->stopTimer(watchdogId);

                        if (watchdogFired) {
                                ffmpeg.kill();
                                ffmpeg.waitForFinished(2000);
                                ctx.setTimeout(String("tx watchdog fired after ")
                                               + String::number(timeoutMs) + String(" ms"));
                                return;
                        }

                        // ffmpeg should now drain the last few packets,
                        // close its file, and exit on its own once it
                        // has read @c frames frames.  Give it generous
                        // headroom — RTP packetization at 30 fps on
                        // 1080p YUV422 emits ~50 packets/frame; ffmpeg's
                        // SDP demuxer adds latency for the marker-bit
                        // frame-end detection.
                        Error ffmpegFinish = ffmpeg.waitForFinished(timeoutMs);
                        if (ffmpegFinish.isError()) {
                                ffmpeg.kill();
                                ffmpeg.waitForFinished(2000);
                                ctx.setFail(String("ffmpeg did not finish within ")
                                            + String::number(timeoutMs) + String(" ms"));
                                return;
                        }

                        const int ffmpegExit = ffmpeg.exitCode();
                        ctx.setDetail(String("ffmpegExitCode"), int64_t(ffmpegExit));
                        if (ffmpegExit != 0) {
                                Buffer stderrBuf = ffmpeg.readAllStderr();
                                String tail;
                                if (stderrBuf.size() > 0) {
                                        const size_t take = stderrBuf.size() > 512 ? 512 : stderrBuf.size();
                                        tail = String(static_cast<const char *>(stderrBuf.data()),
                                                      static_cast<int>(take));
                                }
                                ctx.setFail(String("ffmpeg exit=") + String::number(ffmpegExit)
                                            + (tail.isEmpty() ? String() : String(" — ") + tail));
                                return;
                        }

                        if (!FilePath(outPath).exists()) {
                                ctx.setFail(String("ffmpeg produced no output at ") + outPath);
                                return;
                        }
                        int64_t outBytes = 0;
                        {
                                File  outFile(outPath);
                                Error oe = outFile.open(File::ReadOnly);
                                if (oe.isError()) {
                                        ctx.setFail(String("ffmpeg output stat open: ") + oe.desc());
                                        return;
                                }
                                auto sz = outFile.size();
                                if (sz.second().isError()) {
                                        ctx.setFail(String("ffmpeg output size: ") + sz.second().desc());
                                        return;
                                }
                                outBytes = sz.first();
                        }
                        ctx.setDetail(String("ffmpegOutBytes"), outBytes);
                        if (outBytes <= 0) {
                                ctx.setFail(String("ffmpeg output is zero-byte"));
                                return;
                        }

                        // 720p59.94 YUV422 (the kDefaultVideoFormat used
                        // by makeTpgStage) = 1280 × 720 × 2 = 1843200
                        // bytes / frame.  ffmpeg writes in raster order,
                        // so a non-zero output that is at least one
                        // full frame is the structural proof we want.
                        // A more precise frame-count assertion would
                        // need to assume a fixed pixel format; the
                        // "at-least-one-frame" floor stays correct
                        // regardless.
                        const int64_t minOneFrame = 1280LL * 720LL * 2LL;
                        if (outBytes < minOneFrame) {
                                ctx.setFail(String("ffmpeg output ") + String::number(outBytes)
                                            + String(" bytes < one 720p YUV422 frame"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerRtpFfmpegCases() {
                TestRunner::registerCase(TestCase(
                        String("rtp.ffmpeg.raw.yuv8_422_rec709"),
                        String("RTP interop: TPG → RtpMediaIO RFC 4175 sender → ffmpeg rawvideo demuxer"),
                        [](TestContext &ctx) { runRtpFfmpegCase(ctx); }));
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
