/**
 * @file      tests/videocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the @ref VideoEncoder / @ref VideoDecoder contract with an
 * in-test "Passthrough" codec: the encoder simply moves the first
 * image plane's buffer into a @ref MediaPacket and the decoder puts it
 * back.  No actual compression — the point is to prove the push/pull
 * plumbing, registry, flush/EOS handling, and @ref MediaConfig
 * forwarding all work before any real codec (NVENC, x264, etc.) is
 * plugged in.  Also registers the "Passthrough" factory names so the
 * generic MediaIOTask_VideoEncoder / MediaIOTask_VideoDecoder tests
 * (and anyone else who wants a GPU-free smoke codec) can reach it.
 */

#include <doctest/doctest.h>
#include <deque>
#include <promeki/codec.h>
#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/size2d.h>

using namespace promeki;

namespace {

class PassthroughVideoEncoder : public VideoEncoder {
        public:
                String name() const override { return "Passthrough"; }
                String description() const override { return "Passthrough (test) video encoder"; }
                PixelDesc outputPixelDesc() const override {
                        return PixelDesc(PixelDesc::H264);
                }
                List<int> supportedInputs() const override { return {}; }

                void configure(const MediaConfig &config) override {
                        _cfgBitrate = config.getAs<int32_t>(MediaConfig::BitrateKbps);
                        _cfgGop     = config.getAs<int32_t>(MediaConfig::GopLength);
                }

                Error submitFrame(const Image &frame, const MediaTimeStamp &pts) override {
                        clearError();
                        if(!frame.isValid() || frame.planes().isEmpty()) {
                                setError(Error::Invalid, "invalid frame");
                                return _lastError;
                        }
                        auto pkt = MediaPacket::Ptr::create();
                        pkt.modify()->setBuffer(frame.plane(0));
                        pkt.modify()->setPixelDesc(outputPixelDesc());
                        pkt.modify()->setPts(pts);
                        if(_forceKey || _frameIdx == 0) {
                                pkt.modify()->addFlag(MediaPacket::Keyframe);
                                _forceKey = false;
                        }
                        _queue.push_back(pkt);
                        ++_frameIdx;
                        return Error::Ok;
                }

                MediaPacket::Ptr receivePacket() override {
                        if(_queue.empty()) {
                                if(_flushed && !_eosEmitted) {
                                        _eosEmitted = true;
                                        auto eos = MediaPacket::Ptr::create();
                                        eos.modify()->setPixelDesc(outputPixelDesc());
                                        eos.modify()->addFlag(MediaPacket::EndOfStream);
                                        return eos;
                                }
                                return MediaPacket::Ptr();
                        }
                        auto pkt = _queue.front();
                        _queue.pop_front();
                        return pkt;
                }

                Error flush() override {
                        _flushed = true;
                        return Error::Ok;
                }

                Error reset() override {
                        _queue.clear();
                        _frameIdx   = 0;
                        _forceKey   = false;
                        _flushed    = false;
                        _eosEmitted = false;
                        return Error::Ok;
                }

                void requestKeyframe() override { _forceKey = true; }

                int32_t configuredBitrate() const { return _cfgBitrate; }
                int32_t configuredGop()     const { return _cfgGop; }

        private:
                std::deque<MediaPacket::Ptr> _queue;
                int32_t   _cfgBitrate  = 0;
                int32_t   _cfgGop      = 0;
                uint64_t  _frameIdx    = 0;
                bool      _forceKey    = false;
                bool      _flushed     = false;
                bool      _eosEmitted  = false;
};

class PassthroughVideoDecoder : public VideoDecoder {
        public:
                String name() const override { return "Passthrough"; }
                String description() const override { return "Passthrough (test) video decoder"; }
                PixelDesc inputPixelDesc() const override { return PixelDesc(PixelDesc::H264); }
                List<int> supportedOutputs() const override { return {}; }

                // Reads the output geometry + format from MediaConfig —
                // the MediaIOTask_VideoDecoder pathway forwards the
                // whole MediaIO config to configure(), so the generic
                // backend tests can drive this decoder without having
                // to reach in and call setOutput() themselves.
                void configure(const MediaConfig &cfg) override {
                        Size2Du32 sz = cfg.getAs<Size2Du32>(MediaConfig::VideoSize);
                        if(sz.isValid()) _outSize = sz;
                        PixelDesc pd = cfg.getAs<PixelDesc>(MediaConfig::OutputPixelDesc);
                        if(pd.isValid()) _outDesc = pd;
                }

                Error submitPacket(const MediaPacket &packet) override {
                        clearError();
                        if(!packet.isValid()) {
                                setError(Error::Invalid, "invalid packet");
                                return _lastError;
                        }
                        _pending.push_back(packet);
                        return Error::Ok;
                }

                Image receiveFrame() override {
                        if(_pending.empty()) return Image();
                        MediaPacket pkt = _pending.front();
                        _pending.pop_front();
                        if(!pkt.buffer()) return Image();
                        return Image::fromBuffer(pkt.buffer(),
                                _outSize.width(), _outSize.height(), _outDesc);
                }

                Error flush() override { return Error::Ok; }

                Error reset() override {
                        _pending.clear();
                        return Error::Ok;
                }

                void setOutput(const Size2Du32 &size, const PixelDesc &pd) {
                        _outSize = size;
                        _outDesc = pd;
                }

        private:
                std::deque<MediaPacket>  _pending;
                Size2Du32                _outSize;
                PixelDesc                _outDesc;
};

// Registers the passthrough codec exactly once per process so the
// registry lookup tests see it without the production library
// carrying test-only code.
struct PassthroughRegistrar {
        PassthroughRegistrar() {
                auto encFactory = []() -> VideoEncoder * {
                        return new PassthroughVideoEncoder();
                };
                auto decFactory = []() -> VideoDecoder * {
                        return new PassthroughVideoDecoder();
                };
                VideoEncoder::registerEncoder("Passthrough", encFactory);
                VideoDecoder::registerDecoder("Passthrough", decFactory);

                // Register against the typed VideoCodec registry too
                // so MediaConfig::VideoCodec (now TypeVideoCodec) can
                // resolve "Passthrough" through VideoCodec::lookup.
                // Allocates a fresh user-defined ID per process.
                VideoCodec::Data d;
                d.id            = VideoCodec::registerType();
                d.name          = "Passthrough";
                d.desc          = "Passthrough (test) codec";
                d.createEncoder = encFactory;
                d.createDecoder = decFactory;
                VideoCodec::registerData(std::move(d));
        }
};
static PassthroughRegistrar _passthroughRegistrar;

// Helper: build a solid-grey RGB8 frame for round-trip checks.  Uses
// Image::fromBuffer so we keep full control over the backing buffer's
// logical size (the default Image ctor leaves it at 0).
Image makeTestFrame(int width, int height, uint8_t fill) {
        PixelDesc pd(PixelDesc::RGB8_sRGB);
        const size_t bytes = pd.pixelFormat().planeSize(0, width, height);
        auto buf = Buffer::Ptr::create(bytes);
        buf.modify()->fill(static_cast<char>(fill));
        buf.modify()->setSize(bytes);
        return Image::fromBuffer(buf, width, height, pd);
}

} // namespace

TEST_CASE("VideoEncoder: registry round-trip") {
        auto names = VideoEncoder::registeredEncoders();
        CHECK(names.contains("Passthrough"));

        VideoEncoder *enc = VideoEncoder::createEncoder("Passthrough");
        REQUIRE(enc != nullptr);
        CHECK(enc->name() == "Passthrough");
        CHECK(enc->outputPixelDesc().id() == PixelDesc::H264);
        delete enc;

        CHECK(VideoEncoder::createEncoder("nonexistent") == nullptr);
}

TEST_CASE("VideoDecoder: registry round-trip") {
        auto names = VideoDecoder::registeredDecoders();
        CHECK(names.contains("Passthrough"));

        VideoDecoder *dec = VideoDecoder::createDecoder("Passthrough");
        REQUIRE(dec != nullptr);
        CHECK(dec->name() == "Passthrough");
        CHECK(dec->inputPixelDesc().id() == PixelDesc::H264);
        delete dec;

        CHECK(VideoDecoder::createDecoder("nonexistent") == nullptr);
}

TEST_CASE("VideoEncoder: configure forwards well-known keys") {
        auto *enc = static_cast<PassthroughVideoEncoder *>(
                VideoEncoder::createEncoder("Passthrough"));
        REQUIRE(enc != nullptr);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(8000));
        cfg.set(MediaConfig::GopLength,   int32_t(48));
        enc->configure(cfg);

        CHECK(enc->configuredBitrate() == 8000);
        CHECK(enc->configuredGop() == 48);
        delete enc;
}

TEST_CASE("VideoEncoder: first frame is keyframe; requestKeyframe forces next") {
        auto *enc = VideoEncoder::createEncoder("Passthrough");
        REQUIRE(enc != nullptr);

        Image f0 = makeTestFrame(4, 4, 0xAA);
        Image f1 = makeTestFrame(4, 4, 0xBB);
        Image f2 = makeTestFrame(4, 4, 0xCC);

        CHECK(enc->submitFrame(f0) == Error::Ok);
        auto p0 = enc->receivePacket();
        REQUIRE(p0);
        CHECK(p0->isKeyframe());

        CHECK(enc->submitFrame(f1) == Error::Ok);
        auto p1 = enc->receivePacket();
        REQUIRE(p1);
        CHECK_FALSE(p1->isKeyframe());

        enc->requestKeyframe();
        CHECK(enc->submitFrame(f2) == Error::Ok);
        auto p2 = enc->receivePacket();
        REQUIRE(p2);
        CHECK(p2->isKeyframe());

        delete enc;
}

TEST_CASE("VideoEncoder: flush emits EndOfStream packet") {
        auto *enc = VideoEncoder::createEncoder("Passthrough");
        REQUIRE(enc != nullptr);

        Image f = makeTestFrame(2, 2, 0x42);
        REQUIRE(enc->submitFrame(f) == Error::Ok);
        CHECK(enc->receivePacket());

        CHECK(enc->flush() == Error::Ok);
        auto eos = enc->receivePacket();
        REQUIRE(eos);
        CHECK(eos->isEndOfStream());

        CHECK_FALSE(enc->receivePacket());
        delete enc;
}

TEST_CASE("VideoEncoder: submitFrame rejects invalid input") {
        auto *enc = VideoEncoder::createEncoder("Passthrough");
        REQUIRE(enc != nullptr);

        Image empty;
        Error err = enc->submitFrame(empty);
        CHECK(err == Error::Invalid);
        CHECK(enc->lastError() == Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Video codec: encoder -> packet -> decoder round-trip") {
        auto *enc = VideoEncoder::createEncoder("Passthrough");
        auto *dec = static_cast<PassthroughVideoDecoder *>(
                VideoDecoder::createDecoder("Passthrough"));
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        const int w = 8, h = 4;
        dec->setOutput(Size2Du32(w, h), PixelDesc(PixelDesc::RGB8_sRGB));

        Image src = makeTestFrame(w, h, 0x37);

        CHECK(enc->submitFrame(src) == Error::Ok);
        auto pkt = enc->receivePacket();
        REQUIRE(pkt);

        CHECK(dec->submitPacket(*pkt) == Error::Ok);
        Image out = dec->receiveFrame();
        REQUIRE(out.isValid());

        REQUIRE(src.plane(0));
        REQUIRE(out.plane(0));
        CHECK(src.plane(0)->size() == out.plane(0)->size());
        const auto *srcBytes = static_cast<const uint8_t *>(src.plane(0)->data());
        const auto *outBytes = static_cast<const uint8_t *>(out.plane(0)->data());
        for(size_t i = 0; i < src.plane(0)->size(); ++i) {
                CHECK(srcBytes[i] == outBytes[i]);
        }

        delete enc;
        delete dec;
}
