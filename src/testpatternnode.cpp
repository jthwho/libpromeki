/**
 * @file      testpatternnode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/proav/testpatternnode.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/image.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/frame.h>
#include <promeki/core/metadata.h>
#include <promeki/core/random.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(TestPatternNode)

TestPatternNode::TestPatternNode(ObjectBase *parent) : MediaNode(parent) {
        setName("TestPatternNode");
        auto port = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Frame);
        addOutputPort(port);
}

TestPatternNode::~TestPatternNode() {
        delete _audioGen;
        delete _ltcEncoder;
}

void TestPatternNode::setChannelConfig(size_t chan, AudioGen::Config config) {
        while(_channelConfigs.size() <= chan) {
                AudioGen::Config def = { AudioGen::Silence, 0.0f, 0.0f, 0.0f, 0.5f };
                _channelConfigs.pushToBack(def);
        }
        _channelConfigs[chan] = config;
        return;
}

Error TestPatternNode::configure() {
        if(state() != Idle) return Error(Error::Invalid);

        // Validate video description
        if(!_videoDesc.isValid()) {
                emitError("VideoDesc is not valid");
                return Error(Error::Invalid);
        }
        if(_videoDesc.imageList().isEmpty()) {
                emitError("VideoDesc has no image layers");
                return Error(Error::Invalid);
        }

        _imageDesc = _videoDesc.imageList()[0];
        if(!_imageDesc.isValid()) {
                emitError("ImageDesc is not valid");
                return Error(Error::Invalid);
        }

        // Set up timecode generator from frame rate
        _tcGen.setFrameRate(_videoDesc.frameRate());

        // Set output port descriptors
        MediaPort::Ptr port = outputPort(0);
        port.modify()->setVideoDesc(_videoDesc);

        // Set up audio
        if(_audioEnabled) {
                if(!_audioDesc.isValid()) {
                        // Default audio: 48kHz stereo native float
                        _audioDesc = AudioDesc(48000.0f, 2);
                }
                port.modify()->setAudioDesc(_audioDesc);

                // Compute samples per frame from video frame rate and audio sample rate
                double fps = _videoDesc.frameRate().toDouble();
                if(fps > 0.0) {
                        _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fps);
                } else {
                        _samplesPerFrame = 1600; // fallback
                }

                // Create audio generator or LTC encoder
                delete _audioGen;
                _audioGen = nullptr;
                delete _ltcEncoder;
                _ltcEncoder = nullptr;

                if(_audioMode == LTC) {
                        _ltcEncoder = new LtcEncoder((int)_audioDesc.sampleRate(), _ltcLevel);
                } else {
                        _audioGen = new AudioGen(_audioDesc);
                        for(size_t ch = 0; ch < _audioDesc.channels(); ch++) {
                                if(ch < _channelConfigs.size()) {
                                        _audioGen->setConfig(ch, _channelConfigs[ch]);
                                } else if(_audioMode == Tone) {
                                        AudioGen::Config cfg;
                                        cfg.type = AudioGen::Sine;
                                        cfg.freq = (float)_toneFreq;
                                        cfg.amplitude = (float)_toneAmplitude;
                                        cfg.phase = 0.0f;
                                        cfg.dutyCycle = 0.5f;
                                        _audioGen->setConfig(ch, cfg);
                                } else {
                                        AudioGen::Config cfg;
                                        cfg.type = AudioGen::Silence;
                                        cfg.freq = 0.0f;
                                        cfg.amplitude = 0.0f;
                                        cfg.phase = 0.0f;
                                        cfg.dutyCycle = 0.5f;
                                        _audioGen->setConfig(ch, cfg);
                                }
                        }
                }
        }

        _motionOffset = 0.0;
        _frameCount = 0;

        setState(Configured);
        return Error(Error::Ok);
}

Error TestPatternNode::start() {
        if(state() != Configured) return Error(Error::Invalid);
        setState(Running);
        return Error(Error::Ok);
}

void TestPatternNode::process() {
        // Advance timecode — returns the value for the current frame
        Timecode tc = _tcGen.advance();

        // Create image
        Image img(_imageDesc);
        renderPattern(img, _motionOffset);

        // Set metadata on the image
        img.metadata().set(Metadata::Timecode, tc);

        // Create frame
        Frame::Ptr frame = Frame::Ptr::create();
        Image::Ptr imgPtr = Image::Ptr::create(img);
        frame.modify()->imageList().pushToBack(imgPtr);

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

        // Deliver to output
        deliverOutput(frame);

        // Advance motion
        if(_motion != 0.0) {
                double fps = _videoDesc.frameRate().toDouble();
                if(fps > 0.0) {
                        _motionOffset += _motion * (double)_imageDesc.size().width() / fps;
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
        delete _audioGen;
        _audioGen = nullptr;
        delete _ltcEncoder;
        _ltcEncoder = nullptr;
        _frameCount = 0;
        _motionOffset = 0.0;
        _tcGen.reset();
        setState(Idle);
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

        // SMPTE color bars: White, Yellow, Cyan, Green, Magenta, Red, Blue, Black
        // 100% values
        struct BarColor { uint16_t r, g, b; };
        BarColor bars100[] = {
                {65535, 65535, 65535}, // White
                {65535, 65535, 0},     // Yellow
                {0, 65535, 65535},     // Cyan
                {0, 65535, 0},         // Green
                {65535, 0, 65535},     // Magenta
                {65535, 0, 0},         // Red
                {0, 0, 65535},         // Blue
                {0, 0, 0}             // Black
        };
        // 75% values
        BarColor bars75[] = {
                {49151, 49151, 49151}, // White 75%
                {49151, 49151, 0},     // Yellow 75%
                {0, 49151, 49151},     // Cyan 75%
                {0, 49151, 0},         // Green 75%
                {49151, 0, 49151},     // Magenta 75%
                {49151, 0, 0},         // Red 75%
                {0, 0, 49151},         // Blue 75%
                {0, 0, 0}             // Black
        };

        BarColor *bars = full ? bars100 : bars75;
        int intOffset = (int)std::fmod(offset, (double)w);
        if(intOffset < 0) intOffset += w;

        for(int i = 0; i < 8; i++) {
                auto pixel = pe.createPixel(bars[i].r, bars[i].g, bars[i].b);
                int x0 = (i * barWidth + intOffset) % w;
                // Handle wrap-around
                if(x0 + barWidth <= w) {
                        pe.fillRect(pixel, Rect<int32_t>(x0, 0, barWidth, h));
                } else {
                        int firstPart = w - x0;
                        pe.fillRect(pixel, Rect<int32_t>(x0, 0, firstPart, h));
                        pe.fillRect(pixel, Rect<int32_t>(0, 0, barWidth - firstPart, h));
                }
        }
        // Fill remaining pixels if w is not exactly 8*barWidth
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

        // Black background
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        // White grid lines
        auto white = pe.createPixel(65535, 65535, 65535);
        int spacing = 128;
        int intOffset = (int)std::fmod(offset, (double)spacing);
        if(intOffset < 0) intOffset += spacing;

        // Vertical lines
        for(int x = intOffset; x < w; x += spacing) {
                pe.fillRect(white, Rect<int32_t>(x, 0, 1, h));
        }
        // Horizontal lines
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

        // Diagonal lines (top-left to bottom-right)
        for(int d = -h + intOffset; d < w + h; d += spacing) {
                pe.drawLine(white, d, 0, d + h, h);
        }
        // Diagonal lines (top-right to bottom-left)
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
