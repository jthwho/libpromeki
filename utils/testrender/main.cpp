/**
 * @file      testrender/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/testpatternnode.h>
#include <promeki/framedemuxnode.h>
#include <promeki/timecodeoverlaynode.h>
#include <promeki/mediapipeline.h>
#include <promeki/medianodeconfig.h>
#include <promeki/imagefile.h>
#include <promeki/pixeldesc.h>
#include <promeki/timecode.h>
#include <promeki/framerate.h>

using namespace promeki;

// Simple sink that captures the last image frame.
// Provides non-threaded start for synchronous use.
class ImageCaptureSink : public MediaNode {
        PROMEKI_OBJECT(ImageCaptureSink, MediaNode)
        public:
                ImageCaptureSink() : MediaNode() {
                        setName("Capture");
                        addSink(MediaSink::Ptr::create("input", ContentVideo));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)inputIndex; (void)deliveries;
                        if(frame.isValid()) _last = frame;
                        return;
                }
                void drain() { while(sink(0)->queueSize() > 0) process(); return; }
                Frame::Ptr last() const { return _last; }
        private:
                Frame::Ptr _last;
};

// Audio discard sink (same pattern).
class AudioDiscardSink : public MediaNode {
        PROMEKI_OBJECT(AudioDiscardSink, MediaNode)
        public:
                AudioDiscardSink() : MediaNode() {
                        setName("AudioDiscard");
                        addSink(MediaSink::Ptr::create("input", ContentAudio));
                }
                BuildResult build(const MediaNodeConfig &) override {
                        setState(Configured);
                        return BuildResult();
                }
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override {
                        (void)frame; (void)inputIndex; (void)deliveries;
                        return;
                }
                void drain() { while(sink(0)->queueSize() > 0) process(); return; }
};

// Thin wrappers that expose process() publicly for synchronous pumping.
class PumpableSource : public TestPatternNode {
        public:
                using TestPatternNode::process;
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
};

class PumpableDemux : public FrameDemuxNode {
        public:
                using FrameDemuxNode::process;
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
};

class PumpableOverlay : public TimecodeOverlayNode {
        public:
                using TimecodeOverlayNode::process;
                Error start() override {
                        if(state() != Configured) return Error(Error::Invalid);
                        setState(Running);
                        return Error(Error::Ok);
                }
                void stop() override { setState(Idle); return; }
};

static void usage() {
        fprintf(stderr,
                "Usage: testrender [OPTIONS] <output.png>\n"
                "\n"
                "Renders a test pattern with timecode overlay and saves to PNG.\n"
                "\n"
                "Options:\n"
                "  --width <W>       Frame width (default: 1920)\n"
                "  --height <H>      Frame height (default: 1080)\n"
                "  --pattern <P>     Pattern: colorbars, colorbars75, ramp, grid,\n"
                "                    crosshatch, checkerboard, black, white, noise,\n"
                "                    zoneplate (default: colorbars)\n"
                "  --tc <HH:MM:SS:FF>  Timecode to burn (default: 01:00:00:00)\n"
                "  --font <PATH>     Font file (default: bundled FiraCode)\n"
                "  --fontsize <PTS>  Font size (default: 48)\n"
                "  --no-tc           Don't burn timecode\n"
                "  --text <TEXT>     Custom text to render\n"
                "  --help            Show this help\n"
        );
}

static String parsePattern(const char *name) {
        String s(name);
        // Validate against known patterns (CamelCase, matching VideoPattern).
        const char *valid[] = {
                "ColorBars", "ColorBars75", "Ramp", "Grid", "Crosshatch",
                "Checkerboard", "Black", "White", "Noise", "ZonePlate", nullptr
        };
        for(const char **p = valid; *p; p++) {
                if(s == *p) return s;
        }
        fprintf(stderr, "Unknown pattern: %s\n", name);
        return "ColorBars";
}

int main(int argc, char *argv[]) {
        int width = 1920;
        int height = 1080;
        String patternStr = "ColorBars";
        String tcStr = "01:00:00:00";
        String fontPath = String(PROMEKI_SOURCE_DIR) + "/etc/fonts/FiraCodeNerdFontMono-Regular.ttf";
        int fontSize = 48;
        bool burnTc = true;
        String customText;
        const char *outputFile = nullptr;

        for(int i = 1; i < argc; i++) {
                String arg(argv[i]);
                if(arg == "--help" || arg == "-h") { usage(); return 0; }
                else if(arg == "--width" && i + 1 < argc)   width = atoi(argv[++i]);
                else if(arg == "--height" && i + 1 < argc)  height = atoi(argv[++i]);
                else if(arg == "--pattern" && i + 1 < argc) patternStr = parsePattern(argv[++i]);
                else if(arg == "--tc" && i + 1 < argc)      tcStr = argv[++i];
                else if(arg == "--font" && i + 1 < argc)    fontPath = argv[++i];
                else if(arg == "--fontsize" && i + 1 < argc) fontSize = atoi(argv[++i]);
                else if(arg == "--no-tc")                    burnTc = false;
                else if(arg == "--text" && i + 1 < argc)    customText = argv[++i];
                else if(argv[i][0] != '-')                   outputFile = argv[i];
                else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(); return 1; }
        }

        if(!outputFile) {
                fprintf(stderr, "Error: output file required\n");
                usage();
                return 1;
        }

        // Build nodes

        // Source
        PumpableSource *src = new PumpableSource();
        {
                MediaNodeConfig cfg("TestPatternNode", "Source");
                cfg.set("Pattern", patternStr);
                cfg.set("Width", uint32_t(width));
                cfg.set("Height", uint32_t(height));
                cfg.set("PixelFormat", int(PixelDesc::RGBA8_sRGB));
                cfg.set("FrameRate", "24");
                cfg.set("StartTimecode", tcStr);
                cfg.set("AudioEnabled", true);
                BuildResult br = src->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Source build failed\n");
                        return 1;
                }
        }

        // Demux
        PumpableDemux *demux = new PumpableDemux();
        {
                MediaNodeConfig cfg("FrameDemuxNode", "Demux");
                BuildResult br = demux->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Demux build failed\n");
                        return 1;
                }
        }

        // TC overlay (optional)
        PumpableOverlay *overlay = nullptr;
        if(burnTc) {
                overlay = new PumpableOverlay();
                MediaNodeConfig cfg("TimecodeOverlayNode", "Overlay");
                cfg.set("FontPath", fontPath);
                cfg.set("FontSize", fontSize);
                cfg.set("Position", "bottomcenter");
                cfg.set("DrawBackground", true);
                if(!customText.isEmpty()) cfg.set("CustomText", customText);
                BuildResult br = overlay->build(cfg);
                if(br.isError()) {
                        fprintf(stderr, "Overlay build failed\n");
                        return 1;
                }
        }

        // Image capture sink
        ImageCaptureSink *sink = new ImageCaptureSink();
        { MediaNodeConfig cfg; sink->build(cfg); }

        // Audio discard sink
        AudioDiscardSink *audSink = new AudioDiscardSink();
        { MediaNodeConfig cfg; audSink->build(cfg); }

        // Build pipeline topology
        MediaPipeline pipeline;
        pipeline.addNode(src);
        pipeline.addNode(demux);
        pipeline.connect(src, 0, demux, 0);

        if(overlay) {
                pipeline.addNode(overlay);
                pipeline.connect(demux, "image", overlay, "input");
        }

        pipeline.addNode(sink);
        if(overlay) {
                pipeline.connect(overlay, "output", sink, "input");
        } else {
                pipeline.connect(demux, "image", sink, "input");
        }

        pipeline.addNode(audSink);
        pipeline.connect(demux, "audio", audSink, "input");

        // Start nodes (non-threaded via our overrides)
        Error err = pipeline.start();
        if(err.isError()) { fprintf(stderr, "Pipeline start failed\n"); return 1; }

        // Generate one frame synchronously
        src->process();

        // Push through demux
        while(demux->sink(0)->queueSize() > 0) demux->process();

        // Push through overlay
        if(overlay) {
                while(overlay->sink(0)->queueSize() > 0) overlay->process();
        }

        // Drain sinks
        sink->drain();
        audSink->drain();

        pipeline.stop();

        // Save result
        Frame::Ptr result = sink->last();
        if(!result.isValid() || result->imageList().isEmpty()) {
                fprintf(stderr, "No image produced\n");
                return 1;
        }

        // Parse TC for display
        auto [parsedTc, tcErr] = Timecode::fromString(tcStr);
        Timecode tc;
        if(tcErr.isOk()) {
                tc = Timecode(Timecode::NDF24, parsedTc.hour(), parsedTc.min(),
                              parsedTc.sec(), parsedTc.frame());
        } else {
                tc = Timecode(Timecode::NDF24, 1, 0, 0, 0);
        }

        Image outImg = *result->imageList()[0];
        ImageFile png(ImageFile::PNG);
        png.setFilename(outputFile);
        png.setImage(outImg);
        err = png.save();
        if(err.isError()) {
                fprintf(stderr, "Failed to save PNG: %s\n", outputFile);
                return 1;
        }

        auto [tcOut, tcOutErr] = tc.toString();
        fprintf(stdout, "Saved %dx%d %s to %s (TC: %s)\n",
                width, height,
                burnTc ? "with TC overlay" : "no TC",
                outputFile, tcOut.cstr());
        return 0;
}
