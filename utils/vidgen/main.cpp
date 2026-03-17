/**
 * @file      vidgen/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>

#include <promeki/core/string.h>
#include <promeki/core/timecode.h>
#include <promeki/core/framerate.h>
#include <promeki/core/rational.h>
#include <promeki/core/filepath.h>

#include <promeki/proav/mediapipeline.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/testpatternnode.h>
#include <promeki/proav/framedemuxnode.h>
#include <promeki/proav/timecodeoverlaynode.h>
#include <promeki/proav/jpegencodernode.h>
#include <promeki/proav/videodesc.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/proav/rtpvideosinknode.h>
#include <promeki/proav/rtpaudiosinknode.h>

#include <promeki/network/socketaddress.h>
#include <promeki/network/sdpsession.h>
#include <promeki/network/rtppayload.h>

using namespace promeki;

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
        g_running = false;
}

// --------------------------------------------------------------------
// Option parsing helpers
// --------------------------------------------------------------------

struct Options {
        // Video
        int             width           = 1920;
        int             height          = 1080;
        String          framerateStr    = "29.97";
        String          patternStr      = "colorbars";
        String          pixelFormatStr  = "rgb8";

        // Motion
        double          motion          = 0.0;

        // Audio
        int             audioRate       = 48000;
        int             audioChannels   = 2;
        double          audioTone       = 1000.0;
        double          audioAmplitude  = 0.5;
        bool            audioSilence    = false;
        bool            audioLtc        = false;
        float           ltcLevel        = 0.5f;
        int             ltcChannel      = 0;
        bool            noAudio         = false;

        // Timecode
        String          tcStart         = "01:00:00:00";
        bool            tcDf            = false;
        bool            tcBurn          = false;
        String          tcFont;
        int             tcSize          = 36;
        String          tcPosition      = "bottomcenter";

        // Streaming
        String          dest;
        String          multicast;
        String          transport       = "st2110";
        int             jpegQuality     = 85;
        String          audioDest;
        String          sdpFile;

        // General
        double          duration        = 0.0;
        bool            verbose         = false;
        bool            listPatterns    = false;
        bool            listFormats     = false;
        String          dumpJpeg;
};

static void usage() {
        fprintf(stderr,
                "Usage: vidgen [OPTIONS]\n"
                "\n"
                "Video test pattern generator with RTP streaming output.\n"
                "\n"
                "Video Options:\n"
                "  --width <W>              Frame width (default: 1920)\n"
                "  --height <H>             Frame height (default: 1080)\n"
                "  --framerate <R>          Frame rate (default: 29.97)\n"
                "                           Accepts: 23.976, 24, 25, 29.97, 30, 50, 59.94, 60\n"
                "                           Or fraction: 30000/1001\n"
                "  --pattern <P>            Test pattern (default: colorbars)\n"
                "  --pixel-format <F>       Pixel format (default: rgb8)\n"
                "\n"
                "Motion Options:\n"
                "  --motion <S>             Pattern motion speed (default: 0.0 = static)\n"
                "\n"
                "Audio Options:\n"
                "  --audio-rate <R>         Sample rate in Hz (default: 48000)\n"
                "  --audio-channels <N>     Number of channels (default: 2)\n"
                "  --audio-tone <Hz>        Tone frequency in Hz (default: 1000)\n"
                "  --audio-amplitude <A>    Amplitude 0.0-1.0 (default: 0.5)\n"
                "  --audio-silence          Generate silence instead of tone\n"
                "  --audio-ltc              Generate LTC timecode audio\n"
                "  --ltc-level <L>          LTC output level 0.0-1.0 (default: 0.5)\n"
                "  --ltc-channel <N>        Channel for LTC (default: 0, -1 = all)\n"
                "  --no-audio               Disable audio output entirely\n"
                "\n"
                "Timecode Options:\n"
                "  --tc-start <TC>          Starting timecode (default: 01:00:00:00)\n"
                "  --tc-df                  Use drop-frame timecode (only at 29.97 fps)\n"
                "  --tc-burn                Burn timecode into video\n"
                "  --tc-font <PATH>         Font file for TC burn (required with --tc-burn)\n"
                "  --tc-size <PTS>          Font size in points (default: 36)\n"
                "  --tc-position <POS>      Position: topleft, topcenter, topright,\n"
                "                           bottomleft, bottomcenter, bottomright\n"
                "                           (default: bottomcenter)\n"
                "\n"
                "Streaming Options:\n"
                "  --dest <IP:PORT>         Destination address (required)\n"
                "  --multicast <GROUP:PORT> Multicast group (alternative to --dest)\n"
                "  --transport <T>          Transport: st2110, mjpeg (default: st2110)\n"
                "  --jpeg-quality <Q>       JPEG quality 1-100 for mjpeg (default: 85)\n"
                "  --audio-dest <IP:PORT>   Audio destination (default: video port+2)\n"
                "  --sdp <FILE>             Write SDP file describing the stream\n"
                "\n"
                "Diagnostics:\n"
                "  --dump-jpeg <FILE>       Save first JPEG frame to file (MJPEG only)\n"
                "\n"
                "General:\n"
                "  --duration <SEC>         Run for N seconds (default: unlimited)\n"
                "  --verbose                Print pipeline statistics\n"
                "  --list-patterns          List available test patterns\n"
                "  --list-formats           List available pixel formats\n"
                "  --help                   Show this help\n"
        );
}

static bool parseOptions(int argc, char *argv[], Options &opts) {
        for(int i = 1; i < argc; i++) {
                String arg(argv[i]);
                if(arg == "--help" || arg == "-h")              { usage(); exit(0); }
                else if(arg == "--width" && i + 1 < argc)       opts.width = atoi(argv[++i]);
                else if(arg == "--height" && i + 1 < argc)      opts.height = atoi(argv[++i]);
                else if(arg == "--framerate" && i + 1 < argc)   opts.framerateStr = argv[++i];
                else if(arg == "--pattern" && i + 1 < argc)     opts.patternStr = argv[++i];
                else if(arg == "--pixel-format" && i + 1 < argc) opts.pixelFormatStr = argv[++i];
                else if(arg == "--motion" && i + 1 < argc)      opts.motion = atof(argv[++i]);
                else if(arg == "--audio-rate" && i + 1 < argc)  opts.audioRate = atoi(argv[++i]);
                else if(arg == "--audio-channels" && i + 1 < argc) opts.audioChannels = atoi(argv[++i]);
                else if(arg == "--audio-tone" && i + 1 < argc)  opts.audioTone = atof(argv[++i]);
                else if(arg == "--audio-amplitude" && i + 1 < argc) opts.audioAmplitude = atof(argv[++i]);
                else if(arg == "--audio-silence")                opts.audioSilence = true;
                else if(arg == "--audio-ltc")                    opts.audioLtc = true;
                else if(arg == "--ltc-level" && i + 1 < argc)   opts.ltcLevel = static_cast<float>(atof(argv[++i]));
                else if(arg == "--ltc-channel" && i + 1 < argc) opts.ltcChannel = atoi(argv[++i]);
                else if(arg == "--no-audio")                     opts.noAudio = true;
                else if(arg == "--tc-start" && i + 1 < argc)    opts.tcStart = argv[++i];
                else if(arg == "--tc-df")                        opts.tcDf = true;
                else if(arg == "--tc-burn")                      opts.tcBurn = true;
                else if(arg == "--tc-font" && i + 1 < argc)     opts.tcFont = argv[++i];
                else if(arg == "--tc-size" && i + 1 < argc)     opts.tcSize = atoi(argv[++i]);
                else if(arg == "--tc-position" && i + 1 < argc) opts.tcPosition = argv[++i];
                else if(arg == "--dest" && i + 1 < argc)        opts.dest = argv[++i];
                else if(arg == "--multicast" && i + 1 < argc)   opts.multicast = argv[++i];
                else if(arg == "--transport" && i + 1 < argc)   opts.transport = argv[++i];
                else if(arg == "--jpeg-quality" && i + 1 < argc) opts.jpegQuality = atoi(argv[++i]);
                else if(arg == "--audio-dest" && i + 1 < argc)  opts.audioDest = argv[++i];
                else if(arg == "--sdp" && i + 1 < argc)         opts.sdpFile = argv[++i];
                else if(arg == "--duration" && i + 1 < argc)    opts.duration = atof(argv[++i]);
                else if(arg == "--verbose")                      opts.verbose = true;
                else if(arg == "--list-patterns")                opts.listPatterns = true;
                else if(arg == "--dump-jpeg" && i + 1 < argc)    opts.dumpJpeg = argv[++i];
                else if(arg == "--list-formats")                 opts.listFormats = true;
                else {
                        fprintf(stderr, "Unknown option: %s\n", argv[i]);
                        usage();
                        return false;
                }
        }
        return true;
}

// --------------------------------------------------------------------
// Pattern / format lookup
// --------------------------------------------------------------------

struct PatternEntry {
        const char              *name;
        TestPatternNode::Pattern pattern;
};

static const PatternEntry g_patterns[] = {
        { "colorbars",    TestPatternNode::ColorBars },
        { "colorbars75",  TestPatternNode::ColorBars75 },
        { "ramp",         TestPatternNode::Ramp },
        { "grid",         TestPatternNode::Grid },
        { "crosshatch",   TestPatternNode::Crosshatch },
        { "checkerboard", TestPatternNode::Checkerboard },
        { "black",        TestPatternNode::Black },
        { "white",        TestPatternNode::White },
        { "noise",        TestPatternNode::Noise },
        { "zoneplate",    TestPatternNode::ZonePlate },
        { nullptr,        TestPatternNode::ColorBars }
};

static bool lookupPattern(const String &name, TestPatternNode::Pattern &out) {
        for(const auto *p = g_patterns; p->name; p++) {
                if(name == p->name) { out = p->pattern; return true; }
        }
        return false;
}

struct FormatEntry {
        const char      *name;
        PixelFormat::ID id;
        int             bitsPerPixel;
};

static const FormatEntry g_formats[] = {
        { "rgba8",     PixelFormat::RGBA8,    32 },
        { "rgb8",      PixelFormat::RGB8,     24 },
        { "rgb10",     PixelFormat::RGB10,    30 },
        { "yuv8_422",  PixelFormat::YUV8_422, 16 },
        { nullptr,     PixelFormat::Invalid,   0 }
};

static bool lookupFormat(const String &name, PixelFormat::ID &id, int &bpp) {
        for(const auto *f = g_formats; f->name; f++) {
                if(name == f->name) { id = f->id; bpp = f->bitsPerPixel; return true; }
        }
        return false;
}

// --------------------------------------------------------------------
// Frame rate parsing
// --------------------------------------------------------------------

static bool parseFrameRate(const String &str, FrameRate &out) {
        // Try well-known rates first
        if(str == "23.976" || str == "23.98") { out = FrameRate(FrameRate::FPS_2398); return true; }
        if(str == "24")                       { out = FrameRate(FrameRate::FPS_24);   return true; }
        if(str == "25")                       { out = FrameRate(FrameRate::FPS_25);   return true; }
        if(str == "29.97")                    { out = FrameRate(FrameRate::FPS_2997); return true; }
        if(str == "30")                       { out = FrameRate(FrameRate::FPS_30);   return true; }
        if(str == "50")                       { out = FrameRate(FrameRate::FPS_50);   return true; }
        if(str == "59.94")                    { out = FrameRate(FrameRate::FPS_5994); return true; }
        if(str == "60")                       { out = FrameRate(FrameRate::FPS_60);   return true; }

        // Try fraction form: num/den
        const char *slash = strchr(str.cstr(), '/');
        if(slash) {
                unsigned int num = static_cast<unsigned int>(atoi(str.cstr()));
                unsigned int den = static_cast<unsigned int>(atoi(slash + 1));
                if(num > 0 && den > 0) {
                        out = FrameRate(FrameRate::RationalType(num, den));
                        return true;
                }
        }

        return false;
}

// --------------------------------------------------------------------
// Timecode overlay position
// --------------------------------------------------------------------

static bool parseTcPosition(const String &str, TimecodeOverlayNode::Position &out) {
        if(str == "topleft")        { out = TimecodeOverlayNode::TopLeft;      return true; }
        if(str == "topcenter")      { out = TimecodeOverlayNode::TopCenter;    return true; }
        if(str == "topright")       { out = TimecodeOverlayNode::TopRight;     return true; }
        if(str == "bottomleft")     { out = TimecodeOverlayNode::BottomLeft;   return true; }
        if(str == "bottomcenter")   { out = TimecodeOverlayNode::BottomCenter; return true; }
        if(str == "bottomright")    { out = TimecodeOverlayNode::BottomRight;  return true; }
        return false;
}

// --------------------------------------------------------------------
// Timecode mode from frame rate
// --------------------------------------------------------------------

static Timecode::Mode tcModeFromFrameRate(const FrameRate &fps, bool dropFrame) {
        if(dropFrame && fps.wellKnownRate() == FrameRate::FPS_2997) {
                return Timecode::DF30;
        }
        switch(fps.wellKnownRate()) {
                case FrameRate::FPS_2398: return Timecode::NDF24;
                case FrameRate::FPS_24:   return Timecode::NDF24;
                case FrameRate::FPS_25:   return Timecode::NDF25;
                case FrameRate::FPS_2997: return Timecode::NDF30;
                case FrameRate::FPS_30:   return Timecode::NDF30;
                case FrameRate::FPS_50:   return Timecode::NDF25; // 50fps uses 25 TC base
                case FrameRate::FPS_5994: return Timecode::NDF30;
                case FrameRate::FPS_60:   return Timecode::NDF30;
                default:                  return Timecode::NDF30;
        }
}

// --------------------------------------------------------------------
// SDP file generation
// --------------------------------------------------------------------

static void writeSdpFile(const String &filename, const Options &opts,
                         const SocketAddress &videoDest, const SocketAddress &audioDest,
                         const FrameRate &fps, int bpp, bool isMjpeg) {
        SdpSession sdp;
        sdp.setSessionName("vidgen");
        sdp.setOrigin("-", 1, 1);
        sdp.setConnectionAddress(videoDest.address().toString());

        // Video media description
        SdpMediaDescription videoMd;
        videoMd.setMediaType("video");
        videoMd.setPort(videoDest.port());
        videoMd.setProtocol("RTP/AVP");
        if(isMjpeg) {
                videoMd.addPayloadType(26);
                videoMd.setAttribute("rtpmap", "26 JPEG/90000");
        } else {
                videoMd.addPayloadType(96);
                char rtpmap[128];
                snprintf(rtpmap, sizeof(rtpmap), "96 raw/90000");
                videoMd.setAttribute("rtpmap", rtpmap);
                char fmtp[256];
                snprintf(fmtp, sizeof(fmtp),
                         "96 sampling=RGB; width=%d; height=%d; depth=%d; exactframerate=%u/%u",
                         opts.width, opts.height, bpp,
                         fps.numerator(), fps.denominator());
                videoMd.setAttribute("fmtp", fmtp);
        }
        sdp.addMediaDescription(videoMd);

        // Audio media description
        if(!opts.noAudio) {
                SdpMediaDescription audioMd;
                audioMd.setMediaType("audio");
                audioMd.setPort(audioDest.port());
                audioMd.setProtocol("RTP/AVP");
                audioMd.addPayloadType(97);
                char artpmap[128];
                snprintf(artpmap, sizeof(artpmap), "97 L24/%d/%d",
                         opts.audioRate, opts.audioChannels);
                audioMd.setAttribute("rtpmap", artpmap);
                char ptime[32];
                snprintf(ptime, sizeof(ptime), "1");
                audioMd.setAttribute("ptime", ptime);
                sdp.addMediaDescription(audioMd);
        }

        String sdpText = sdp.toString();
        FILE *f = fopen(filename.cstr(), "w");
        if(!f) {
                fprintf(stderr, "Error: cannot write SDP file: %s\n", filename.cstr());
                return;
        }
        fwrite(sdpText.cstr(), 1, sdpText.size(), f);
        fclose(f);
        fprintf(stdout, "SDP written to %s\n", filename.cstr());
}

// --------------------------------------------------------------------
// Message handler for node messages
// --------------------------------------------------------------------

static void onNodeMessage(const NodeMessage &msg) {
        const char *sevStr = "INFO";
        switch(msg.severity) {
                case Severity::Info:    sevStr = "INFO";    break;
                case Severity::Warning: sevStr = "WARN";    break;
                case Severity::Error:   sevStr = "ERROR";   break;
                case Severity::Fatal:   sevStr = "FATAL";   break;
        }
        fprintf(stderr, "[%s] frame %lu: %s\n", sevStr,
                static_cast<unsigned long>(msg.frameNumber),
                msg.message.cstr());
        if(msg.severity == Severity::Fatal) {
                g_running = false;
        }
}

// --------------------------------------------------------------------
// main
// --------------------------------------------------------------------

int main(int argc, char *argv[]) {
        Options opts;
        if(!parseOptions(argc, argv, opts)) return 1;

        // --list-patterns
        if(opts.listPatterns) {
                fprintf(stdout, "Available test patterns:\n");
                for(const auto *p = g_patterns; p->name; p++) {
                        fprintf(stdout, "  %s\n", p->name);
                }
                return 0;
        }

        // --list-formats
        if(opts.listFormats) {
                fprintf(stdout, "Available pixel formats:\n");
                for(const auto *f = g_formats; f->name; f++) {
                        fprintf(stdout, "  %-12s (%d bpp)\n", f->name, f->bitsPerPixel);
                }
                return 0;
        }

        // Validate required options
        if(opts.dest.isEmpty() && opts.multicast.isEmpty()) {
                fprintf(stderr, "Error: --dest or --multicast is required\n");
                usage();
                return 1;
        }

        // Parse frame rate
        FrameRate fps;
        if(!parseFrameRate(opts.framerateStr, fps)) {
                fprintf(stderr, "Error: invalid frame rate: %s\n", opts.framerateStr.cstr());
                return 1;
        }

        // Parse pattern
        TestPatternNode::Pattern pattern;
        if(!lookupPattern(opts.patternStr, pattern)) {
                fprintf(stderr, "Error: unknown pattern: %s\n", opts.patternStr.cstr());
                fprintf(stderr, "Use --list-patterns to see available patterns\n");
                return 1;
        }

        // Parse pixel format
        PixelFormat::ID pixFmtId;
        int bitsPerPixel;
        if(!lookupFormat(opts.pixelFormatStr, pixFmtId, bitsPerPixel)) {
                fprintf(stderr, "Error: unknown pixel format: %s\n", opts.pixelFormatStr.cstr());
                fprintf(stderr, "Use --list-formats to see available formats\n");
                return 1;
        }

        bool isMjpeg = (opts.transport == "mjpeg");

        // Parse video destination
        SocketAddress videoDest;
        if(!opts.dest.isEmpty()) {
                auto [addr, err] = SocketAddress::fromString(opts.dest);
                if(err.isError()) {
                        fprintf(stderr, "Error: invalid destination address: %s\n", opts.dest.cstr());
                        return 1;
                }
                videoDest = addr;
        } else {
                auto [addr, err] = SocketAddress::fromString(opts.multicast);
                if(err.isError()) {
                        fprintf(stderr, "Error: invalid multicast address: %s\n", opts.multicast.cstr());
                        return 1;
                }
                videoDest = addr;
        }

        // Parse audio destination (default: video port + 2)
        SocketAddress audioDest;
        if(!opts.noAudio) {
                if(!opts.audioDest.isEmpty()) {
                        auto [addr, err] = SocketAddress::fromString(opts.audioDest);
                        if(err.isError()) {
                                fprintf(stderr, "Error: invalid audio destination: %s\n", opts.audioDest.cstr());
                                return 1;
                        }
                        audioDest = addr;
                } else {
                        audioDest = videoDest;
                        uint16_t audioPort = videoDest.port() + 2;
                        if(audioPort < videoDest.port()) {
                                // Wrapped past 65535
                                audioPort = 1024;
                                fprintf(stderr, "Warning: video port+2 exceeds 65535, using audio port %d\n", audioPort);
                        }
                        audioDest.setPort(audioPort);
                }
        }

        // Parse timecode
        Timecode::Mode tcMode = tcModeFromFrameRate(fps, opts.tcDf);
        auto [parsedTc, tcErr] = Timecode::fromString(opts.tcStart);
        Timecode startTc;
        if(tcErr.isOk()) {
                startTc = Timecode(tcMode, parsedTc.hour(), parsedTc.min(),
                                   parsedTc.sec(), parsedTc.frame());
        } else {
                startTc = Timecode(tcMode, 1, 0, 0, 0);
                fprintf(stderr, "Warning: could not parse timecode '%s', using 01:00:00:00\n",
                        opts.tcStart.cstr());
        }

        // Validate TC burn options
        if(opts.tcBurn && opts.tcFont.isEmpty()) {
                // Try bundled font
                opts.tcFont = String(PROMEKI_SOURCE_DIR) + "/etc/fonts/FiraCodeNerdFontMono-Regular.ttf";
        }

        TimecodeOverlayNode::Position tcPos;
        if(!parseTcPosition(opts.tcPosition, tcPos)) {
                fprintf(stderr, "Error: unknown TC position: %s\n", opts.tcPosition.cstr());
                return 1;
        }

        // Install signal handlers
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        // ================================================================
        // Build pipeline
        // ================================================================

        MediaPipeline pipeline;
        MediaGraph *graph = pipeline.graph();

        // --- Source ---
        TestPatternNode *src = new TestPatternNode();
        src->setName("Source");
        src->setPattern(pattern);
        src->setMotion(opts.motion);
        src->setStartTimecode(startTc);
        src->setDropFrame(opts.tcDf && fps.wellKnownRate() == FrameRate::FPS_2997);

        VideoDesc vdesc;
        vdesc.setFrameRate(fps);
        ImageDesc idesc(opts.width, opts.height, pixFmtId);
        vdesc.imageList().pushToBack(idesc);
        src->setVideoDesc(vdesc);

        if(!opts.noAudio) {
                AudioDesc adesc(static_cast<float>(opts.audioRate), opts.audioChannels);
                src->setAudioDesc(adesc);
                src->setAudioEnabled(true);
                if(opts.audioSilence) {
                        src->setAudioMode(TestPatternNode::Silence);
                } else if(opts.audioLtc) {
                        src->setAudioMode(TestPatternNode::LTC);
                        src->setLtcLevel(opts.ltcLevel);
                        src->setLtcChannel(opts.ltcChannel);
                } else {
                        src->setAudioMode(TestPatternNode::Tone);
                        src->setToneFrequency(opts.audioTone);
                        src->setToneAmplitude(opts.audioAmplitude);
                }
        } else {
                src->setAudioEnabled(false);
        }
        graph->addNode(src);

        // --- Demux ---
        FrameDemuxNode *demux = new FrameDemuxNode();
        demux->setName("Demux");
        graph->addNode(demux);
        graph->connect(src, 0, demux, 0);

        // --- TC Overlay (optional) ---
        TimecodeOverlayNode *overlay = nullptr;
        if(opts.tcBurn) {
                overlay = new TimecodeOverlayNode();
                overlay->setName("TCOverlay");
                overlay->setFontPath(FilePath(opts.tcFont));
                overlay->setFontSize(opts.tcSize);
                overlay->setPosition(tcPos);
                overlay->setDrawBackground(true);
                graph->addNode(overlay);
                graph->connect(demux, "image", overlay, "input");
        }

        // --- JPEG Encoder (optional, for MJPEG transport) ---
        JpegEncoderNode *jpegEnc = nullptr;
        if(isMjpeg) {
                jpegEnc = new JpegEncoderNode();
                jpegEnc->setName("JpegEncoder");
                jpegEnc->setQuality(opts.jpegQuality);
                graph->addNode(jpegEnc);
                if(overlay) {
                        graph->connect(overlay, "output", jpegEnc, "input");
                } else {
                        graph->connect(demux, "image", jpegEnc, "input");
                }
        }

        // --- RTP payload handlers (must outlive the pipeline) ---
        RtpPayloadRawVideo rawVideoPayload(opts.width, opts.height, bitsPerPixel);
        RtpPayloadJpeg jpegPayload(opts.width, opts.height, opts.jpegQuality);
        RtpPayloadL24 audioPayload(static_cast<uint32_t>(opts.audioRate), opts.audioChannels);

        // --- Video Sink ---
        RtpVideoSinkNode *videoSink = new RtpVideoSinkNode();
        videoSink->setName("VideoSink");
        videoSink->setDestination(videoDest);
        videoSink->setFrameRate(fps);
        if(isMjpeg) {
                videoSink->setRtpPayload(&jpegPayload);
                videoSink->setPayloadType(26);
        } else {
                videoSink->setRtpPayload(&rawVideoPayload);
                videoSink->setPayloadType(96);
        }
        if(!opts.multicast.isEmpty() && videoDest.isMulticast()) {
                videoSink->setMulticast(videoDest);
        }
        if(!opts.dumpJpeg.isEmpty()) {
                videoSink->setDumpPath(opts.dumpJpeg);
        }
        graph->addNode(videoSink);

        // Connect to video sink
        if(isMjpeg && jpegEnc) {
                graph->connect(jpegEnc, "output", videoSink, "input");
        } else if(overlay) {
                graph->connect(overlay, "output", videoSink, "input");
        } else {
                graph->connect(demux, "image", videoSink, "input");
        }

        // --- Audio Sink ---
        RtpAudioSinkNode *audioSink = nullptr;
        if(!opts.noAudio) {
                audioSink = new RtpAudioSinkNode();
                audioSink->setName("AudioSink");
                audioSink->setDestination(audioDest);
                audioSink->setRtpPayload(&audioPayload);
                audioSink->setClockRate(static_cast<uint32_t>(opts.audioRate));
                audioSink->setOutputFormat(AudioDesc::PCMI_S24BE);
                graph->addNode(audioSink);
                graph->connect(demux, "audio", audioSink, "input");
        }

        // --- Connect message handlers ---
        for(auto *node : graph->nodes()) {
                node->messageEmittedSignal.connect(onNodeMessage);
        }

        // ================================================================
        // Start pipeline
        // ================================================================

        Error err = pipeline.start();
        if(err.isError()) {
                fprintf(stderr, "Error: pipeline start failed\n");
                return 1;
        }

        // Print stream info
        fprintf(stdout, "vidgen streaming %dx%d %s @ %s fps\n",
                opts.width, opts.height,
                isMjpeg ? "MJPEG" : "ST2110",
                fps.toString().cstr());
        fprintf(stdout, "  Video: %s  Pattern: %s\n",
                videoDest.toString().cstr(), opts.patternStr.cstr());
        if(!opts.noAudio) {
                fprintf(stdout, "  Audio: %s  %d Hz %d ch\n",
                        audioDest.toString().cstr(), opts.audioRate, opts.audioChannels);
        }
        if(opts.duration > 0) {
                fprintf(stdout, "  Duration: %.1f seconds\n", opts.duration);
        }
        fprintf(stdout, "Press Ctrl+C to stop.\n");

        // Write SDP file if requested
        if(!opts.sdpFile.isEmpty()) {
                writeSdpFile(opts.sdpFile, opts, videoDest, audioDest, fps, bitsPerPixel, isMjpeg);
        }

        // ================================================================
        // Run loop — drive processing in topological order
        // ================================================================
        // The video sink node is the pacing authority: its process()
        // sleeps to maintain the target frame rate. We drive the graph
        // in topological order on the main thread.

        List<MediaNode *> processingOrder = graph->topologicalSort();

        auto startTime = std::chrono::steady_clock::now();
        auto lastStats = startTime;

        while(g_running) {
                // Drive all nodes in topological order
                for(auto *node : processingOrder) {
                        node->process();
                        // Drain intermediate nodes that may have queued frames
                        while(node->queuedFrameCount() > 0) node->process();
                }

                // Check duration
                if(opts.duration > 0) {
                        auto elapsed = std::chrono::steady_clock::now() - startTime;
                        double secs = std::chrono::duration<double>(elapsed).count();
                        if(secs >= opts.duration) break;
                }

                // Print stats periodically
                if(opts.verbose) {
                        auto now = std::chrono::steady_clock::now();
                        auto sinceStats = std::chrono::duration<double>(now - lastStats).count();
                        if(sinceStats >= 5.0) {
                                lastStats = now;
                                auto elapsed = std::chrono::duration<double>(now - startTime).count();
                                fprintf(stdout, "[%.1fs] frames: %lu",
                                        elapsed, static_cast<unsigned long>(src->frameCount()));

                                auto vstats = videoSink->extendedStats();
                                auto pit = vstats.find("packetsSent");
                                if(pit != vstats.end()) {
                                        fprintf(stdout, "  video pkts: %lu",
                                                static_cast<unsigned long>(pit->second.get<uint64_t>()));
                                }
                                auto bit = vstats.find("bytesSent");
                                if(bit != vstats.end()) {
                                        double mb = static_cast<double>(bit->second.get<uint64_t>()) / (1024.0 * 1024.0);
                                        fprintf(stdout, "  (%.1f MB)", mb);
                                }

                                if(audioSink) {
                                        auto astats = audioSink->extendedStats();
                                        auto apit = astats.find("packetsSent");
                                        if(apit != astats.end()) {
                                                fprintf(stdout, "  audio pkts: %lu",
                                                        static_cast<unsigned long>(apit->second.get<uint64_t>()));
                                        }
                                }
                                fprintf(stdout, "\n");
                        }
                }
        }

        // ================================================================
        // Shutdown
        // ================================================================

        // Capture stats before stop (stop() resets node counters)
        uint64_t totalFrames = src->frameCount();
        auto totalElapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime).count();

        fprintf(stdout, "\nStopping...\n");
        pipeline.stop();

        fprintf(stdout, "Ran for %.1f seconds, %lu frames generated\n",
                totalElapsed, static_cast<unsigned long>(totalFrames));

        return 0;
}
