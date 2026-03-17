/**
 * @file      testrender/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/proav/testpatternnode.h>
#include <promeki/proav/framedemuxnode.h>
#include <promeki/proav/timecodeoverlaynode.h>
#include <promeki/proav/mediagraph.h>
#include <promeki/proav/imagefile.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/timecode.h>
#include <promeki/core/framerate.h>

using namespace promeki;

// Simple sink that captures the last image frame
class ImageCaptureSink : public MediaNode {
        PROMEKI_OBJECT(ImageCaptureSink, MediaNode)
        public:
                ImageCaptureSink() : MediaNode() {
                        setName("Capture");
                        addInputPort(MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Image));
                }
                void process() override {
                        Frame::Ptr f = dequeueInput();
                        if(f.isValid()) _last = f;
                        return;
                }
                void drain() { while(queuedFrameCount() > 0) process(); return; }
                Frame::Ptr last() const { return _last; }
        private:
                Frame::Ptr _last;
};

// Audio discard sink
class AudioDiscardSink : public MediaNode {
        PROMEKI_OBJECT(AudioDiscardSink, MediaNode)
        public:
                AudioDiscardSink() : MediaNode() {
                        setName("AudioDiscard");
                        addInputPort(MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Audio));
                }
                void process() override { dequeueInput(); return; }
                void drain() { while(queuedFrameCount() > 0) process(); return; }
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

static TestPatternNode::Pattern parsePattern(const char *name) {
        String s(name);
        if(s == "colorbars")    return TestPatternNode::ColorBars;
        if(s == "colorbars75")  return TestPatternNode::ColorBars75;
        if(s == "ramp")         return TestPatternNode::Ramp;
        if(s == "grid")         return TestPatternNode::Grid;
        if(s == "crosshatch")   return TestPatternNode::Crosshatch;
        if(s == "checkerboard") return TestPatternNode::Checkerboard;
        if(s == "black")        return TestPatternNode::Black;
        if(s == "white")        return TestPatternNode::White;
        if(s == "noise")        return TestPatternNode::Noise;
        if(s == "zoneplate")    return TestPatternNode::ZonePlate;
        fprintf(stderr, "Unknown pattern: %s\n", name);
        return TestPatternNode::ColorBars;
}

int main(int argc, char *argv[]) {
        int width = 1920;
        int height = 1080;
        TestPatternNode::Pattern pattern = TestPatternNode::ColorBars;
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
                else if(arg == "--pattern" && i + 1 < argc) pattern = parsePattern(argv[++i]);
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

        // Parse timecode — fromString parses digits but doesn't infer a format,
        // so we construct with NDF24 mode and the parsed digit values.
        auto [parsedTc, tcErr] = Timecode::fromString(tcStr);
        Timecode tc;
        if(tcErr.isOk()) {
                tc = Timecode(Timecode::NDF24, parsedTc.hour(), parsedTc.min(),
                              parsedTc.sec(), parsedTc.frame());
        } else {
                tc = Timecode(Timecode::NDF24, 1, 0, 0, 0);
        }

        // Build pipeline
        MediaGraph graph;

        // Source
        TestPatternNode *src = new TestPatternNode();
        src->setPattern(pattern);
        VideoDesc vdesc;
        vdesc.setFrameRate(FrameRate(FrameRate::FPS_24));
        ImageDesc idesc(width, height, PixelFormat::RGBA8);
        vdesc.imageList().pushToBack(idesc);
        src->setVideoDesc(vdesc);
        src->setStartTimecode(tc);
        src->setAudioEnabled(true);
        graph.addNode(src);

        // Demux
        FrameDemuxNode *demux = new FrameDemuxNode();
        graph.addNode(demux);
        graph.connect(src, 0, demux, 0);

        // TC overlay (optional)
        TimecodeOverlayNode *overlay = nullptr;
        if(burnTc) {
                overlay = new TimecodeOverlayNode();
                overlay->setFontPath(FilePath(fontPath));
                overlay->setFontSize(fontSize);
                overlay->setPosition(TimecodeOverlayNode::BottomCenter);
                overlay->setDrawBackground(true);
                if(!customText.isEmpty()) overlay->setCustomText(customText);
                graph.addNode(overlay);
                graph.connect(demux, "image", overlay, "input");
        }

        // Image capture sink
        ImageCaptureSink *sink = new ImageCaptureSink();
        graph.addNode(sink);
        if(overlay) {
                graph.connect(overlay, "output", sink, "input");
        } else {
                graph.connect(demux, "image", sink, "input");
        }

        // Audio discard sink
        AudioDiscardSink *audSink = new AudioDiscardSink();
        graph.addNode(audSink);
        graph.connect(demux, "audio", audSink, "input");

        // Configure all nodes
        Error err = src->configure();
        if(err.isError()) { fprintf(stderr, "TestPatternNode configure failed\n"); return 1; }
        err = demux->configure();
        if(err.isError()) { fprintf(stderr, "FrameDemuxNode configure failed\n"); return 1; }
        if(overlay) {
                err = overlay->configure();
                if(err.isError()) { fprintf(stderr, "TimecodeOverlayNode configure failed\n"); return 1; }
        }

        // Start source and generate one frame
        src->start();
        src->process();

        // Push through demux
        while(demux->queuedFrameCount() > 0) demux->process();

        // Push through overlay
        if(overlay) {
                while(overlay->queuedFrameCount() > 0) overlay->process();
        }

        // Drain sink
        sink->drain();
        audSink->drain();

        // Save result
        Frame::Ptr result = sink->last();
        if(!result.isValid() || result->imageList().isEmpty()) {
                fprintf(stderr, "No image produced\n");
                return 1;
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
