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
#include <promeki/core/audiolevel.h>

#include <promeki/proav/mediapipeline.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/medianodeconfig.h>
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
        double          audioLevel      = -30.0;
        bool            audioSilence    = false;
        bool            audioLtc        = false;
        double          ltcLevel        = -20.0;
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
                "  --audio-level <dBFS>     Tone level in dBFS (default: -30)\n"
                "  --audio-silence          Generate silence instead of tone\n"
                "  --audio-ltc              Generate LTC timecode audio\n"
                "  --ltc-level <dBFS>       LTC output level in dBFS (default: -20)\n"
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
                else if(arg == "--audio-level" && i + 1 < argc) opts.audioLevel = atof(argv[++i]);
                else if(arg == "--audio-silence")                opts.audioSilence = true;
                else if(arg == "--audio-ltc")                    opts.audioLtc = true;
                else if(arg == "--ltc-level" && i + 1 < argc)   opts.ltcLevel = atof(argv[++i]);
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

static bool lookupPattern(const String &name, String &out) {
        for(const auto *p = g_patterns; p->name; p++) {
                if(name == p->name) { out = name; return true; }
        }
        return false;
}

struct FormatEntry {
        const char      *name;
        PixelFormat::ID id;
        int             bitsPerPixel;
        int             bitsPerComponent;
        const char      *sampling;     ///< RFC 4175 sampling parameter.
};

static const FormatEntry g_formats[] = {
        { "rgba8",     PixelFormat::RGBA8,    32, 8,  "RGB" },
        { "rgb8",      PixelFormat::RGB8,     24, 8,  "RGB" },
        { "rgb10",     PixelFormat::RGB10,    30, 10, "RGB" },
        { "yuv8_422",  PixelFormat::YUV8_422, 16, 8,  "YCbCr-4:2:2" },
        { nullptr,     PixelFormat::Invalid,   0, 0,  nullptr }
};

static const FormatEntry *lookupFormat(const String &name) {
        for(const auto *f = g_formats; f->name; f++) {
                if(name == f->name) return f;
        }
        return nullptr;
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
// SDP file generation
// --------------------------------------------------------------------

static void writeSdpFile(const String &filename, const Options &opts,
                         const SocketAddress &videoDest, const SocketAddress &audioDest,
                         const FrameRate &fps, const FormatEntry &fmt, bool isMjpeg) {
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
                         "96 sampling=%s; width=%d; height=%d; depth=%d; exactframerate=%u/%u",
                         fmt.sampling, opts.width, opts.height, fmt.bitsPerComponent,
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
                snprintf(ptime, sizeof(ptime), "4");
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

        // Validate pattern name
        String patternStr;
        if(!lookupPattern(opts.patternStr, patternStr)) {
                fprintf(stderr, "Error: unknown pattern: %s\n", opts.patternStr.cstr());
                fprintf(stderr, "Use --list-patterns to see available patterns\n");
                return 1;
        }

        // Parse pixel format
        const FormatEntry *pixFmt = lookupFormat(opts.pixelFormatStr);
        if(pixFmt == nullptr) {
                fprintf(stderr, "Error: unknown pixel format: %s\n", opts.pixelFormatStr.cstr());
                fprintf(stderr, "Use --list-formats to see available formats\n");
                return 1;
        }
        PixelFormat::ID pixFmtId = pixFmt->id;
        int bitsPerPixel = pixFmt->bitsPerPixel;

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

        // Validate TC burn options
        if(opts.tcBurn && opts.tcFont.isEmpty()) {
                // Try bundled font
                opts.tcFont = String(PROMEKI_SOURCE_DIR) + "/etc/fonts/FiraCodeNerdFontMono-Regular.ttf";
        }

        // Install signal handlers
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        // ================================================================
        // Build pipeline
        // ================================================================

        MediaPipeline pipeline;

        // --- RTP payload handlers (must outlive the pipeline) ---
        RtpPayloadRawVideo rawVideoPayload(opts.width, opts.height, bitsPerPixel);
        RtpPayloadJpeg jpegPayload(opts.width, opts.height, opts.jpegQuality);
        RtpPayloadL24 audioPayload(static_cast<uint32_t>(opts.audioRate), opts.audioChannels);

        // --- Source ---
        TestPatternNode *src = new TestPatternNode();
        {
                MediaNodeConfig cfg("TestPatternNode", "Source");
                cfg.set("Pattern", patternStr);
                cfg.set("Motion", opts.motion);
                cfg.set("StartTimecode", opts.tcStart);
                cfg.set("DropFrame", opts.tcDf && fps.wellKnownRate() == FrameRate::FPS_2997);
                cfg.set("Width", uint32_t(opts.width));
                cfg.set("Height", uint32_t(opts.height));
                cfg.set("PixelFormat", int(pixFmtId));
                cfg.set("FrameRate", opts.framerateStr);
                if(!opts.noAudio) {
                        cfg.set("AudioEnabled", true);
                        cfg.set("AudioRate", float(opts.audioRate));
                        cfg.set("AudioChannels", opts.audioChannels);
                        if(opts.audioSilence) {
                                cfg.set("AudioMode", "silence");
                        } else if(opts.audioLtc) {
                                cfg.set("AudioMode", "ltc");
                                cfg.set("LtcLevel", opts.ltcLevel);
                                cfg.set("LtcChannel", opts.ltcChannel);
                        } else {
                                cfg.set("AudioMode", "tone");
                                cfg.set("ToneFrequency", opts.audioTone);
                                cfg.set("ToneLevel", opts.audioLevel);
                        }
                } else {
                        cfg.set("AudioEnabled", false);
                }
                BuildResult br = src->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Error: source node build failed\n");
                        return 1;
                }
        }
        pipeline.addNode(src);

        // --- Demux ---
        FrameDemuxNode *demux = new FrameDemuxNode();
        {
                MediaNodeConfig cfg("FrameDemuxNode", "Demux");
                BuildResult br = demux->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Error: demux node build failed\n");
                        return 1;
                }
        }
        pipeline.addNode(demux);
        pipeline.connect(src, 0, demux, 0);

        // --- TC Overlay (optional) ---
        TimecodeOverlayNode *overlay = nullptr;
        if(opts.tcBurn) {
                overlay = new TimecodeOverlayNode();
                MediaNodeConfig cfg("TimecodeOverlayNode", "TCOverlay");
                cfg.set("FontPath", opts.tcFont);
                cfg.set("FontSize", opts.tcSize);
                cfg.set("Position", opts.tcPosition);
                cfg.set("DrawBackground", true);
                BuildResult br = overlay->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Error: TC overlay node build failed\n");
                        return 1;
                }
                pipeline.addNode(overlay);
                pipeline.connect(demux, "image", overlay, "input");
        }

        // --- JPEG Encoder (optional, for MJPEG transport) ---
        JpegEncoderNode *jpegEnc = nullptr;
        if(isMjpeg) {
                jpegEnc = new JpegEncoderNode();
                MediaNodeConfig cfg("JpegEncoderNode", "JpegEncoder");
                cfg.set("Quality", opts.jpegQuality);
                BuildResult br = jpegEnc->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Error: JPEG encoder node build failed\n");
                        return 1;
                }
                pipeline.addNode(jpegEnc);
                if(overlay) {
                        pipeline.connect(overlay, "output", jpegEnc, "input");
                } else {
                        pipeline.connect(demux, "image", jpegEnc, "input");
                }
        }

        // --- Video Sink ---
        RtpVideoSinkNode *videoSink = new RtpVideoSinkNode();
        {
                MediaNodeConfig cfg("RtpVideoSinkNode", "VideoSink");
                cfg.set("Destination", videoDest.toString());
                cfg.set("FrameRate", opts.framerateStr);
                if(isMjpeg) {
                        cfg.set("RtpPayload", reinterpret_cast<uint64_t>(&jpegPayload));
                        cfg.set("PayloadType", uint8_t(26));
                } else {
                        cfg.set("RtpPayload", reinterpret_cast<uint64_t>(&rawVideoPayload));
                        cfg.set("PayloadType", uint8_t(96));
                }
                if(!opts.multicast.isEmpty() && videoDest.isMulticast()) {
                        cfg.set("Multicast", videoDest.toString());
                }
                if(!opts.dumpJpeg.isEmpty()) {
                        cfg.set("DumpPath", opts.dumpJpeg);
                }
                BuildResult br = videoSink->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Error: video sink node build failed\n");
                        return 1;
                }
        }
        pipeline.addNode(videoSink);

        // Connect to video sink
        if(isMjpeg && jpegEnc) {
                pipeline.connect(jpegEnc, "output", videoSink, "input");
        } else if(overlay) {
                pipeline.connect(overlay, "output", videoSink, "input");
        } else {
                pipeline.connect(demux, "image", videoSink, "input");
        }

        // --- Audio Sink ---
        RtpAudioSinkNode *audioSink = nullptr;
        if(!opts.noAudio) {
                audioSink = new RtpAudioSinkNode();
                MediaNodeConfig cfg("RtpAudioSinkNode", "AudioSink");
                cfg.set("Destination", audioDest.toString());
                cfg.set("RtpPayload", reinterpret_cast<uint64_t>(&audioPayload));
                cfg.set("ClockRate", uint32_t(opts.audioRate));
                cfg.set("OutputFormat", int(AudioDesc::PCMI_S24BE));
                BuildResult br = audioSink->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Error: audio sink node build failed\n");
                        return 1;
                }
                pipeline.addNode(audioSink);
                pipeline.connect(demux, "audio", audioSink, "input");
        }

        // --- Connect message handlers ---
        for(auto *node : pipeline.nodes()) {
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
                fprintf(stdout, "  Audio: %s  %d Hz %d ch  %.0f dBFS\n",
                        audioDest.toString().cstr(), opts.audioRate, opts.audioChannels,
                        opts.audioLevel);
        }
        if(opts.duration > 0) {
                fprintf(stdout, "  Duration: %.1f seconds\n", opts.duration);
        }
        fprintf(stdout, "Press Ctrl+C to stop.\n");

        // Write SDP file if requested
        if(!opts.sdpFile.isEmpty()) {
                writeSdpFile(opts.sdpFile, opts, videoDest, audioDest, fps, *pixFmt, isMjpeg);
        }

        // ================================================================
        // Run loop — pipeline nodes run on their own threads
        // ================================================================

        auto startTime = std::chrono::steady_clock::now();
        auto lastStats = startTime;

        while(g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
                                auto srcStats = src->extendedStats();
                                auto fgIt = srcStats.find("FramesGenerated");
                                unsigned long frameCount = 0;
                                if(fgIt != srcStats.end()) {
                                        frameCount = fgIt->second.get<unsigned long>();
                                }
                                fprintf(stdout, "[%.1fs] frames: %lu",
                                        elapsed, frameCount);

                                auto vstats = videoSink->extendedStats();
                                auto pit = vstats.find("PacketsSent");
                                if(pit != vstats.end()) {
                                        fprintf(stdout, "  video pkts: %lu",
                                                static_cast<unsigned long>(pit->second.get<uint64_t>()));
                                }
                                auto bit = vstats.find("BytesSent");
                                if(bit != vstats.end()) {
                                        double mb = static_cast<double>(bit->second.get<uint64_t>()) / (1024.0 * 1024.0);
                                        fprintf(stdout, "  (%.1f MB)", mb);
                                }

                                if(audioSink) {
                                        auto astats = audioSink->extendedStats();
                                        auto apit = astats.find("PacketsSent");
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
        auto srcStats = src->extendedStats();
        auto fgIt = srcStats.find("FramesGenerated");
        unsigned long totalFrames = 0;
        if(fgIt != srcStats.end()) {
                totalFrames = fgIt->second.get<unsigned long>();
        }
        auto totalElapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - startTime).count();

        fprintf(stdout, "\nStopping...\n");
        pipeline.stop();

        fprintf(stdout, "Ran for %.1f seconds, %lu frames generated\n",
                totalElapsed, totalFrames);

        return 0;
}
