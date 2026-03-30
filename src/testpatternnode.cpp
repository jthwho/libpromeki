/**
 * @file      testpatternnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstring>
#include <promeki/proav/testpatternnode.h>
#include <promeki/proav/medianodeconfig.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/image.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/metadata.h>
#include <promeki/core/random.h>
#include <promeki/core/timecode.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TestPatternNode)

TestPatternNode::TestPatternNode(ObjectBase *parent) : MediaNode(parent) {
        setName("TestPatternNode");
        auto output = MediaSource::Ptr::create("output", ContentHint(ContentVideo | ContentAudio));
        addSource(output);
}

TestPatternNode::~TestPatternNode() {
        delete _audioGen;
        delete _ltcEncoder;
}

bool TestPatternNode::parsePattern(const String &name, Pattern &out) {
        if(name == "colorbars")    { out = ColorBars;    return true; }
        if(name == "colorbars75")  { out = ColorBars75;  return true; }
        if(name == "ramp")         { out = Ramp;         return true; }
        if(name == "grid")         { out = Grid;         return true; }
        if(name == "crosshatch")   { out = Crosshatch;   return true; }
        if(name == "checkerboard") { out = Checkerboard; return true; }
        if(name == "solidcolor")   { out = SolidColor;   return true; }
        if(name == "white")        { out = White;        return true; }
        if(name == "black")        { out = Black;        return true; }
        if(name == "noise")        { out = Noise;        return true; }
        if(name == "zoneplate")    { out = ZonePlate;    return true; }
        return false;
}

bool TestPatternNode::parseAudioMode(const String &name, AudioMode &out) {
        if(name == "tone")    { out = Tone;    return true; }
        if(name == "silence") { out = Silence; return true; }
        if(name == "ltc")     { out = LTC;     return true; }
        return false;
}

bool TestPatternNode::parseFrameRate(const String &str, FrameRate &out) {
        if(str == "23.976" || str == "23.98") { out = FrameRate(FrameRate::FPS_2398); return true; }
        if(str == "24")                       { out = FrameRate(FrameRate::FPS_24);   return true; }
        if(str == "25")                       { out = FrameRate(FrameRate::FPS_25);   return true; }
        if(str == "29.97")                    { out = FrameRate(FrameRate::FPS_2997); return true; }
        if(str == "30")                       { out = FrameRate(FrameRate::FPS_30);   return true; }
        if(str == "50")                       { out = FrameRate(FrameRate::FPS_50);   return true; }
        if(str == "59.94")                    { out = FrameRate(FrameRate::FPS_5994); return true; }
        if(str == "60")                       { out = FrameRate(FrameRate::FPS_60);   return true; }

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

BuildResult TestPatternNode::build(const MediaNodeConfig &config) {
        BuildResult result;
        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Parse pattern
        String patStr = config.get("pattern", Variant(String("colorbars"))).get<String>();
        if(!patStr.isEmpty() && !parsePattern(patStr, _pattern)) {
                result.addError("Unknown pattern: " + patStr);
                return result;
        }

        // Parse video config
        uint32_t width = config.get("width", Variant(uint32_t(0))).get<uint32_t>();
        uint32_t height = config.get("height", Variant(uint32_t(0))).get<uint32_t>();
        int pixFmt = config.get("pixelFormat", Variant(PixelFormat::RGB8)).get<int>();

        String fpsStr = config.get("frameRate", Variant(String())).get<String>();
        FrameRate fps;
        if(!fpsStr.isEmpty() && !parseFrameRate(fpsStr, fps)) {
                result.addError("Invalid frame rate: " + fpsStr);
                return result;
        }

        if(width > 0 && height > 0 && fps.isValid()) {
                _videoDesc = VideoDesc();
                _videoDesc.setFrameRate(fps);
                _videoDesc.imageList().pushToBack(ImageDesc(width, height, pixFmt));
        }

        // Solid color
        _solidR = config.get("solidColorR", Variant(uint16_t(0))).get<uint16_t>();
        _solidG = config.get("solidColorG", Variant(uint16_t(0))).get<uint16_t>();
        _solidB = config.get("solidColorB", Variant(uint16_t(0))).get<uint16_t>();

        // Motion
        _motion = config.get("motion", Variant(0.0)).get<double>();

        // Audio config
        _audioEnabled = config.get("audioEnabled", Variant(true)).get<bool>();
        String audioModeStr = config.get("audioMode", Variant(String("tone"))).get<String>();
        if(!audioModeStr.isEmpty() && !parseAudioMode(audioModeStr, _audioMode)) {
                result.addError("Unknown audio mode: " + audioModeStr);
                return result;
        }

        float audioRate = config.get("audioRate", Variant(48000.0f)).get<float>();
        int audioChannels = config.get("audioChannels", Variant(2)).get<int>();

        if(_audioEnabled) {
                _audioDesc = AudioDesc(audioRate, audioChannels);
        }

        _toneFreq = config.get("toneFrequency", Variant(1000.0)).get<double>();
        double toneLevelDbfs = config.get("toneLevel", Variant(-20.0)).get<double>();
        _toneLevel = AudioLevel::fromDbfs(toneLevelDbfs);
        double ltcLevelDbfs = config.get("ltcLevel", Variant(-20.0)).get<double>();
        _ltcLevel = AudioLevel::fromDbfs(ltcLevelDbfs);
        _ltcChannel = config.get("ltcChannel", Variant(0)).get<int>();

        // Timecode
        String tcStr = config.get("startTimecode", Variant(String())).get<String>();
        if(!tcStr.isEmpty()) {
                auto [tc, err] = Timecode::fromString(tcStr);
                if(err.isOk()) {
                        _tcGen.setTimecode(tc);
                }
        }
        // Accept a pre-built Timecode via Variant
        Variant tcVar = config.get("timecode");
        if(tcVar.isValid()) {
                _tcGen.setTimecode(tcVar.get<Timecode>());
        }
        bool dropFrame = config.get("dropFrame", Variant(false)).get<bool>();
        _tcGen.setDropFrame(dropFrame);

        // ---- Validate and configure (formerly configure()) ----

        if(!_videoDesc.isValid()) {
                result.addError("VideoDesc is not valid");
                return result;
        }
        if(_videoDesc.imageList().isEmpty()) {
                result.addError("VideoDesc has no image layers");
                return result;
        }

        _imageDesc = _videoDesc.imageList()[0];
        if(!_imageDesc.isValid()) {
                result.addError("ImageDesc is not valid");
                return result;
        }

        // Set up timecode generator from frame rate
        _tcGen.setFrameRate(_videoDesc.frameRate());

        // Set up audio
        if(_audioEnabled) {
                if(!_audioDesc.isValid()) {
                        // Default audio: 48kHz stereo native float
                        _audioDesc = AudioDesc(48000.0f, 2);
                }

                // Compute samples per frame from video frame rate and audio sample rate
                double fpsVal = _videoDesc.frameRate().toDouble();
                if(fpsVal > 0.0) {
                        _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fpsVal);
                } else {
                        _samplesPerFrame = 1600; // fallback
                }

                // Create audio generator or LTC encoder
                delete _audioGen;
                _audioGen = nullptr;
                delete _ltcEncoder;
                _ltcEncoder = nullptr;

                if(_audioMode == LTC) {
                        _ltcEncoder = new LtcEncoder((int)_audioDesc.sampleRate(), _ltcLevel.toLinearFloat());
                } else {
                        _audioGen = new AudioGen(_audioDesc);
                        for(size_t ch = 0; ch < _audioDesc.channels(); ch++) {
                                if(ch < _channelConfigs.size()) {
                                        _audioGen->setConfig(ch, _channelConfigs[ch]);
                                } else if(_audioMode == Tone) {
                                        AudioGen::Config cfg;
                                        cfg.type = AudioGen::Sine;
                                        cfg.freq = (float)_toneFreq;
                                        cfg.level = _toneLevel;
                                        cfg.phase = 0.0f;
                                        cfg.dutyCycle = 0.0f;
                                        _audioGen->setConfig(ch, cfg);
                                } else {
                                        AudioGen::Config cfg;
                                        cfg.type = AudioGen::Silence;
                                        cfg.freq = 0.0f;
                                        cfg.level = AudioLevel();
                                        cfg.phase = 0.0f;
                                        cfg.dutyCycle = 0.0f;
                                        _audioGen->setConfig(ch, cfg);
                                }
                        }
                }
        }

        _motionOffset = 0.0;
        _frameCount = 0;

        setState(Configured);
        return result;
}

Error TestPatternNode::start() {
        return MediaNode::start();
}

void TestPatternNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;
        (void)deliveries;

        // Advance timecode — returns the value for the current frame
        Timecode tc = _tcGen.advance();

        // Create image
        Image img(_imageDesc);
        renderPattern(img, _motionOffset);

        // Set metadata on the image
        img.metadata().set(Metadata::Timecode, tc);

        // Create frame
        frame = Frame::Ptr::create();
        Image::Ptr imgPtr = Image::Ptr::create(img);
        frame.modify()->imageList().pushToBack(imgPtr);

        // Attach benchmark if enabled
        if(currentBenchmark().isValid()) {
                frame.modify()->setBenchmark(currentBenchmark());
        }

        // Generate audio
        if(_audioEnabled) {
                Audio::Ptr audioPtr;
                if(_audioMode == LTC && _ltcEncoder != nullptr) {
                        Audio ltcAudio = _ltcEncoder->encode(tc);
                        if(ltcAudio.isValid()) {
                                // If multi-channel and specific LTC channel, embed in silent multi-ch audio
                                if(_audioDesc.channels() > 1 && _ltcChannel >= 0) {
                                        Audio audio(_audioDesc, ltcAudio.samples());
                                        audio.zero();
                                        // Copy LTC into the target channel
                                        int8_t *ltcData = ltcAudio.data<int8_t>();
                                        float *outData = audio.data<float>();
                                        size_t channels = _audioDesc.channels();
                                        for(size_t s = 0; s < ltcAudio.samples(); s++) {
                                                float val = (float)ltcData[s] / 127.0f;
                                                outData[s * channels + (size_t)_ltcChannel] = val;
                                        }
                                        audioPtr = Audio::Ptr::create(audio);
                                } else {
                                        audioPtr = Audio::Ptr::create(ltcAudio);
                                }
                        }
                } else if(_audioGen != nullptr) {
                        Audio audio = _audioGen->generate(_samplesPerFrame);
                        audioPtr = Audio::Ptr::create(audio);
                }
                if(audioPtr.isValid()) {
                        frame.modify()->audioList().pushToBack(audioPtr);
                }
        }

        // Set frame metadata
        frame.modify()->metadata().set(Metadata::Timecode, tc);

        // Advance motion
        if(_motion != 0.0) {
                double fpsVal = _videoDesc.frameRate().toDouble();
                if(fpsVal > 0.0) {
                        _motionOffset += _motion * (double)_imageDesc.size().width() / fpsVal;
                        // Wrap at pattern period (use image width)
                        double period = (double)_imageDesc.size().width();
                        if(period > 0.0) {
                                while(_motionOffset >= period) _motionOffset -= period;
                                while(_motionOffset < 0.0) _motionOffset += period;
                        }
                }
        }

        _frameCount++;
        return;
}

void TestPatternNode::stop() {
        MediaNode::stop();
        delete _audioGen;
        _audioGen = nullptr;
        delete _ltcEncoder;
        _ltcEncoder = nullptr;
        _frameCount = 0;
        _motionOffset = 0.0;
        _tcGen.reset();
        return;
}

Map<String, Variant> TestPatternNode::extendedStats() const {
        Map<String, Variant> ret;
        ret.insert("framesGenerated", Variant((unsigned long)_frameCount));
        ret.insert("currentTimecode", Variant(_tcGen.timecode().toString().first));
        return ret;
}

// ============================================================================
// Pattern rendering
// ============================================================================

void TestPatternNode::renderPattern(Image &img, double motionOffset) {
        switch(_pattern) {
                case ColorBars:      renderColorBars(img, motionOffset, true); break;
                case ColorBars75:    renderColorBars(img, motionOffset, false); break;
                case Ramp:           renderRamp(img, motionOffset); break;
                case Grid:           renderGrid(img, motionOffset); break;
                case Crosshatch:     renderCrosshatch(img, motionOffset); break;
                case Checkerboard:   renderCheckerboard(img, motionOffset); break;
                case SolidColor:     renderSolid(img, _solidR, _solidG, _solidB); break;
                case White:          renderSolid(img, 65535, 65535, 65535); break;
                case Black:          renderSolid(img, 0, 0, 0); break;
                case Noise:          renderNoise(img); break;
                case ZonePlate:      renderZonePlate(img, motionOffset); break;
        }
        return;
}

void TestPatternNode::renderColorBars(Image &img, double offset, bool full) {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();
        int barWidth = w / 8;
        if(barWidth < 1) barWidth = 1;

        struct BarColor { uint16_t r, g, b; };
        BarColor bars100[] = {
                {65535, 65535, 65535}, {65535, 65535, 0},
                {0, 65535, 65535}, {0, 65535, 0},
                {65535, 0, 65535}, {65535, 0, 0},
                {0, 0, 65535}, {0, 0, 0}
        };
        BarColor bars75[] = {
                {49151, 49151, 49151}, {49151, 49151, 0},
                {0, 49151, 49151}, {0, 49151, 0},
                {49151, 0, 49151}, {49151, 0, 0},
                {0, 0, 49151}, {0, 0, 0}
        };

        BarColor *bars = full ? bars100 : bars75;
        int intOffset = (int)std::fmod(offset, (double)w);
        if(intOffset < 0) intOffset += w;

        for(int i = 0; i < 8; i++) {
                auto pixel = pe.createPixel(bars[i].r, bars[i].g, bars[i].b);
                int x0 = (i * barWidth + intOffset) % w;
                if(x0 + barWidth <= w) {
                        pe.fillRect(pixel, Rect<int32_t>(x0, 0, barWidth, h));
                } else {
                        int firstPart = w - x0;
                        pe.fillRect(pixel, Rect<int32_t>(x0, 0, firstPart, h));
                        pe.fillRect(pixel, Rect<int32_t>(0, 0, barWidth - firstPart, h));
                }
        }
        if(barWidth * 8 < w) {
                auto black = pe.createPixel(0, 0, 0);
                int remaining = w - barWidth * 8;
                int x0 = (barWidth * 8 + intOffset) % w;
                pe.fillRect(black, Rect<int32_t>(x0, 0, remaining, h));
        }
        return;
}

void TestPatternNode::renderRamp(Image &img, double offset) {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();
        int intOffset = (int)std::fmod(offset, (double)w);
        if(intOffset < 0) intOffset += w;

        for(int x = 0; x < w; x++) {
                int srcX = (x + intOffset) % w;
                uint16_t lum = (uint16_t)((double)srcX / (double)(w - 1) * 65535.0);
                auto pixel = pe.createPixel(lum, lum, lum);
                pe.fillRect(pixel, Rect<int32_t>(x, 0, 1, h));
        }
        return;
}

void TestPatternNode::renderGrid(Image &img, double offset) {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto white = pe.createPixel(65535, 65535, 65535);
        int spacing = 128;
        int intOffset = (int)std::fmod(offset, (double)spacing);
        if(intOffset < 0) intOffset += spacing;

        for(int x = intOffset; x < w; x += spacing) {
                pe.fillRect(white, Rect<int32_t>(x, 0, 1, h));
        }
        for(int y = intOffset; y < h; y += spacing) {
                pe.fillRect(white, Rect<int32_t>(0, y, w, 1));
        }
        return;
}

void TestPatternNode::renderCrosshatch(Image &img, double offset) {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto white = pe.createPixel(65535, 65535, 65535);
        int spacing = 96;
        int intOffset = (int)std::fmod(offset, (double)spacing);
        if(intOffset < 0) intOffset += spacing;

        for(int d = -h + intOffset; d < w + h; d += spacing) {
                pe.drawLine(white, d, 0, d + h, h);
        }
        for(int d = -h + intOffset; d < w + h; d += spacing) {
                pe.drawLine(white, w - d, 0, w - d - h, h);
        }
        return;
}

void TestPatternNode::renderCheckerboard(Image &img, double offset) {
        PaintEngine pe = img.createPaintEngine();
        int w = (int)img.width();
        int h = (int)img.height();

        auto black = pe.createPixel(0, 0, 0);
        auto white = pe.createPixel(65535, 65535, 65535);
        pe.fill(black);

        int squareSize = 64;
        int intOffset = (int)std::fmod(offset, (double)(squareSize * 2));
        if(intOffset < 0) intOffset += squareSize * 2;

        for(int y = 0; y < h; y += squareSize) {
                for(int x = 0; x < w; x += squareSize) {
                        int adjX = x + intOffset;
                        int adjY = y + intOffset;
                        bool isWhite = ((adjX / squareSize) + (adjY / squareSize)) % 2 == 0;
                        if(isWhite) {
                                int rw = (x + squareSize > w) ? w - x : squareSize;
                                int rh = (y + squareSize > h) ? h - y : squareSize;
                                pe.fillRect(white, Rect<int32_t>(x, y, rw, rh));
                        }
                }
        }
        return;
}

void TestPatternNode::renderZonePlate(Image &img, double phase) {
        int w = (int)img.width();
        int h = (int)img.height();

        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        int bpp = _imageDesc.pixelFormat()->bytesPerBlock();
        int components = _imageDesc.pixelFormat()->compCount();
        double cx = w / 2.0;
        double cy = h / 2.0;
        double scale = 0.001;

        for(int y = 0; y < h; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < w; x++) {
                        double dx = x - cx;
                        double dy = y - cy;
                        double r2 = dx * dx + dy * dy;
                        double val = (std::sin(r2 * scale + phase * 0.1) + 1.0) * 0.5;
                        uint8_t lum = (uint8_t)(val * 255.0);
                        uint8_t *p = row + x * bpp;
                        for(int c = 0; c < components && c < 3; c++) {
                                p[c] = lum;
                        }
                        if(components >= 4) p[3] = 255;
                }
        }
        return;
}

void TestPatternNode::renderNoise(Image &img) {
        int w = (int)img.width();
        int h = (int)img.height();

        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        int bpp = _imageDesc.pixelFormat()->bytesPerBlock();
        int components = _imageDesc.pixelFormat()->compCount();

        Random rng;
        for(int y = 0; y < h; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < w; x++) {
                        uint8_t *p = row + x * bpp;
                        for(int c = 0; c < components && c < 3; c++) {
                                p[c] = (uint8_t)rng.randomInt(0, 255);
                        }
                        if(components >= 4) p[3] = 255;
                }
        }
        return;
}

void TestPatternNode::renderSolid(Image &img, uint16_t r, uint16_t g, uint16_t b) {
        PaintEngine pe = img.createPaintEngine();
        auto pixel = pe.createPixel(r, g, b);
        pe.fill(pixel);
        return;
}

PROMEKI_NAMESPACE_END
