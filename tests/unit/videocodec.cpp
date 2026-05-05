/**
 * @file      tests/videocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the @ref VideoEncoder / @ref VideoDecoder contract with an
 * in-test "Passthrough" codec: the encoder simply moves the first
 * image plane's buffer into a @ref CompressedVideoPayload and the decoder
 * puts it back.  No actual compression — the point is to prove the push/pull
 * plumbing, registry, flush/EOS handling, and @ref MediaConfig
 * forwarding all work before any real codec (NVENC, x264, etc.) is
 * plugged in.  Also registers the "Passthrough" codec + backend so the
 * generic VideoEncoderMediaIO / VideoDecoderMediaIO tests
 * (and anyone else who wants a GPU-free smoke codec) can reach it.
 */

#include <doctest/doctest.h>
#include <deque>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/mediaconfig.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/buffer.h>
#include <promeki/size2d.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

        // Slot for the Passthrough codec's typed VideoCodec::ID — populated at
        // static-init time by PassthroughRegistrar and read by every test that
        // needs to refer to the codec after registration.
        static VideoCodec::ID gPassthroughCodecId = VideoCodec::Invalid;

        class PassthroughVideoEncoder : public VideoEncoder {
                public:
                        void configure(const MediaConfig &config) override {
                                _cfgBitrate = config.getAs<int32_t>(MediaConfig::BitrateKbps);
                                _cfgGop = config.getAs<int32_t>(MediaConfig::GopLength);
                        }

                        Error submitPayload(const UncompressedVideoPayload::Ptr &payload) override {
                                clearError();
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        setError(Error::Invalid, "invalid payload");
                                        return _lastError;
                                }
                                // Build a CompressedVideoPayload that shares
                                // plane 0's buffer with the input — passthrough
                                // does no compression, it just restamps the
                                // bytes with an H.264 compressed PixelFormat so
                                // downstream code treats the output as a real
                                // compressed access unit.
                                ImageDesc desc(payload->desc().size(), PixelFormat(PixelFormat::H264));
                                auto      pv = payload->plane(0);
                                auto      cvp = CompressedVideoPayload::Ptr::create(
                                        desc, BufferView(pv.buffer(), pv.offset(), pv.size()));
                                cvp.modify()->setPts(payload->pts());
                                cvp.modify()->setDts(payload->pts());
                                if (_forceKey || _frameIdx == 0) {
                                        cvp.modify()->addFlag(MediaPayload::Keyframe);
                                        _forceKey = false;
                                }
                                _queue.push_back(cvp);
                                ++_frameIdx;
                                return Error::Ok;
                        }

                        CompressedVideoPayload::Ptr receiveCompressedPayload() override {
                                if (_queue.empty()) {
                                        if (_flushed && !_eosEmitted) {
                                                _eosEmitted = true;
                                                ImageDesc desc(Size2Du32(0, 0), PixelFormat(PixelFormat::H264));
                                                auto      eos = CompressedVideoPayload::Ptr::create(desc);
                                                eos.modify()->markEndOfStream();
                                                return eos;
                                        }
                                        return CompressedVideoPayload::Ptr();
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
                                _frameIdx = 0;
                                _forceKey = false;
                                _flushed = false;
                                _eosEmitted = false;
                                return Error::Ok;
                        }

                        void requestKeyframe() override { _forceKey = true; }

                        int32_t configuredBitrate() const { return _cfgBitrate; }
                        int32_t configuredGop() const { return _cfgGop; }

                private:
                        std::deque<CompressedVideoPayload::Ptr> _queue;
                        int32_t                                 _cfgBitrate = 0;
                        int32_t                                 _cfgGop = 0;
                        uint64_t                                _frameIdx = 0;
                        bool                                    _forceKey = false;
                        bool                                    _flushed = false;
                        bool                                    _eosEmitted = false;
        };

        class PassthroughVideoDecoder : public VideoDecoder {
                public:
                        // Reads the output geometry + format from MediaConfig —
                        // the VideoDecoderMediaIO pathway forwards the
                        // whole MediaIO config to configure(), so the generic
                        // backend tests can drive this decoder without having
                        // to reach in and call setOutput() themselves.
                        void configure(const MediaConfig &cfg) override {
                                Size2Du32 sz = cfg.getAs<Size2Du32>(MediaConfig::VideoSize);
                                if (sz.isValid()) _outSize = sz;
                                PixelFormat pd = cfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat);
                                if (pd.isValid()) _outDesc = pd;
                        }

                        Error submitPayload(const CompressedVideoPayload::Ptr &payload) override {
                                clearError();
                                if (!payload.isValid() || !payload->isValid()) {
                                        setError(Error::Invalid, "invalid payload");
                                        return _lastError;
                                }
                                _pending.push_back(payload);
                                return Error::Ok;
                        }

                        UncompressedVideoPayload::Ptr receiveVideoPayload() override {
                                if (_pending.empty()) return UncompressedVideoPayload::Ptr();
                                CompressedVideoPayload::Ptr in = std::move(_pending.front());
                                _pending.pop_front();
                                if (in->planeCount() == 0) return UncompressedVideoPayload::Ptr();
                                ImageDesc outDesc(_outSize, _outDesc);
                                auto      out = UncompressedVideoPayload::Ptr::create(outDesc);
                                auto      entry = in->plane(0);
                                out.modify()->data().pushToBack(entry.buffer(), entry.offset(), entry.size());
                                out.modify()->setPts(in->pts());
                                return out;
                        }

                        Error flush() override { return Error::Ok; }

                        Error reset() override {
                                _pending.clear();
                                return Error::Ok;
                        }

                        void setOutput(const Size2Du32 &size, const PixelFormat &pd) {
                                _outSize = size;
                                _outDesc = pd;
                        }

                private:
                        std::deque<CompressedVideoPayload::Ptr> _pending;
                        Size2Du32                               _outSize;
                        PixelFormat                             _outDesc;
        };

        // Registers the passthrough codec exactly once per process so the
        // registry lookup tests see it without the production library
        // carrying test-only code.  Uses the full typed registration path:
        // a fresh VideoCodec::ID allocated via registerType(), a registered
        // "Passthrough" backend, and encoder/decoder BackendRecords against
        // the (codec, backend) pair.
        struct PassthroughRegistrar {
                        PassthroughRegistrar() {
                                gPassthroughCodecId = VideoCodec::registerType();
                                VideoCodec::Data d;
                                d.id = gPassthroughCodecId;
                                d.name = "Passthrough";
                                d.desc = "Passthrough (test) codec";
                                // The Passthrough encoder stamps every CompressedVideoPayload
                                // with PixelFormat::H264 so tests can round-trip through
                                // the generic VideoEncoderMediaIO output-desc path.
                                d.compressedPixelFormats = {
                                        static_cast<int>(PixelFormat::H264),
                                };
                                VideoCodec::registerData(std::move(d));

                                auto bk = VideoCodec::registerBackend("Passthrough");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                VideoEncoder::registerBackend({
                                        .codecId = gPassthroughCodecId,
                                        .backend = backend,
                                        .weight = BackendWeight::User,
                                        .supportedInputs = {},
                                        .factory = []() -> VideoEncoder * { return new PassthroughVideoEncoder(); },
                                });
                                VideoDecoder::registerBackend({
                                        .codecId = gPassthroughCodecId,
                                        .backend = backend,
                                        .weight = BackendWeight::User,
                                        .supportedOutputs = {},
                                        .factory = []() -> VideoDecoder * { return new PassthroughVideoDecoder(); },
                                });
                        }
        };
        static PassthroughRegistrar _passthroughRegistrar;

        // Helper: build a solid-grey RGB8 payload for round-trip checks.
        UncompressedVideoPayload::Ptr makeTestPayload(int width, int height, uint8_t fill) {
                PixelFormat  pd(PixelFormat::RGB8_sRGB);
                const size_t bytes = pd.memLayout().planeSize(0, width, height);
                auto         buf = Buffer(bytes);
                buf.fill(static_cast<char>(fill));
                buf.setSize(bytes);
                ImageDesc desc(Size2Du32(width, height), pd);
                auto      payload = UncompressedVideoPayload::Ptr::create(desc);
                payload.modify()->data().pushToBack(buf, 0, bytes);
                return payload;
        }

        VideoEncoder *makePassthroughEncoder() {
                VideoCodec vc(gPassthroughCodecId);
                auto       res = vc.createEncoder();
                return isOk(res) ? value(res) : nullptr;
        }

        VideoDecoder *makePassthroughDecoder() {
                VideoCodec vc(gPassthroughCodecId);
                auto       res = vc.createDecoder();
                return isOk(res) ? value(res) : nullptr;
        }

} // namespace

TEST_CASE("VideoEncoder: registry round-trip") {
        VideoCodec vc(gPassthroughCodecId);
        REQUIRE(vc.isValid());
        CHECK(vc.canEncode());

        VideoEncoder *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);
        CHECK(enc->codec().name() == "Passthrough");
        // First compressed format on the Passthrough codec's list is
        // empty, so the encoder's stamped codec simply reports the
        // codec's registered name.
        delete enc;

        // Unknown codec ID → invalid VideoCodec → createEncoder returns
        // Error::Invalid.
        VideoCodec bogus(static_cast<VideoCodec::ID>(0xDEADBEEF));
        auto       r = bogus.createEncoder();
        CHECK(error(r).isError());
}

TEST_CASE("VideoDecoder: registry round-trip") {
        VideoCodec vc(gPassthroughCodecId);
        REQUIRE(vc.isValid());
        CHECK(vc.canDecode());

        VideoDecoder *dec = makePassthroughDecoder();
        REQUIRE(dec != nullptr);
        CHECK(dec->codec().name() == "Passthrough");
        delete dec;
}

TEST_CASE("VideoEncoder: configure forwards well-known keys") {
        auto *enc = static_cast<PassthroughVideoEncoder *>(makePassthroughEncoder());
        REQUIRE(enc != nullptr);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(8000));
        cfg.set(MediaConfig::GopLength, int32_t(48));
        enc->configure(cfg);

        CHECK(enc->configuredBitrate() == 8000);
        CHECK(enc->configuredGop() == 48);
        delete enc;
}

TEST_CASE("VideoEncoder: first frame is keyframe; requestKeyframe forces next") {
        auto *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);

        auto f0 = makeTestPayload(4, 4, 0xAA);
        auto f1 = makeTestPayload(4, 4, 0xBB);
        auto f2 = makeTestPayload(4, 4, 0xCC);

        CHECK(enc->submitPayload(f0) == Error::Ok);
        auto p0 = enc->receiveCompressedPayload();
        REQUIRE(p0);
        CHECK(p0->isKeyframe());

        CHECK(enc->submitPayload(f1) == Error::Ok);
        auto p1 = enc->receiveCompressedPayload();
        REQUIRE(p1);
        CHECK_FALSE(p1->isKeyframe());

        enc->requestKeyframe();
        CHECK(enc->submitPayload(f2) == Error::Ok);
        auto p2 = enc->receiveCompressedPayload();
        REQUIRE(p2);
        CHECK(p2->isKeyframe());

        delete enc;
}

TEST_CASE("VideoEncoder: flush emits EndOfStream packet") {
        auto *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);

        auto f = makeTestPayload(2, 2, 0x42);
        REQUIRE(enc->submitPayload(f) == Error::Ok);
        CHECK(enc->receiveCompressedPayload());

        CHECK(enc->flush() == Error::Ok);
        auto eos = enc->receiveCompressedPayload();
        REQUIRE(eos);
        CHECK(eos->isEndOfStream());

        CHECK_FALSE(enc->receiveCompressedPayload());
        delete enc;
}

TEST_CASE("VideoEncoder: submitPayload rejects invalid input") {
        auto *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);

        UncompressedVideoPayload::Ptr empty;
        Error                         err = enc->submitPayload(empty);
        CHECK(err == Error::Invalid);
        CHECK(enc->lastError() == Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Video codec: encoder -> packet -> decoder round-trip") {
        auto *enc = makePassthroughEncoder();
        auto *dec = static_cast<PassthroughVideoDecoder *>(makePassthroughDecoder());
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        const int w = 8, h = 4;
        dec->setOutput(Size2Du32(w, h), PixelFormat(PixelFormat::RGB8_sRGB));

        auto src = makeTestPayload(w, h, 0x37);

        CHECK(enc->submitPayload(src) == Error::Ok);
        auto pkt = enc->receiveCompressedPayload();
        REQUIRE(pkt);

        CHECK(dec->submitPayload(pkt) == Error::Ok);
        UncompressedVideoPayload::Ptr out = dec->receiveVideoPayload();
        REQUIRE(out.isValid());

        auto srcPlane = src->plane(0);
        auto outPlane = out->plane(0);
        CHECK(srcPlane.size() == outPlane.size());
        const uint8_t *srcBytes = srcPlane.data();
        const uint8_t *outBytes = outPlane.data();
        for (size_t i = 0; i < srcPlane.size(); ++i) {
                CHECK(srcBytes[i] == outBytes[i]);
        }

        delete enc;
        delete dec;
}

// ---------------------------------------------------------------------------
// VideoEncoder::registerBackend / VideoDecoder::registerBackend — error paths
// ---------------------------------------------------------------------------

TEST_CASE("VideoEncoder::registerBackend rejects malformed records") {
        // Fetch a known-good backend handle from the passthrough wiring.
        VideoCodec passthrough(gPassthroughCodecId);
        auto       backends = passthrough.availableEncoderBackends();
        REQUIRE_FALSE(backends.isEmpty());
        const auto goodBackend = backends.front();

        VideoEncoder::BackendRecord r;
        r.codecId = gPassthroughCodecId;
        r.backend = goodBackend;
        // r.factory left empty.
        CHECK(VideoEncoder::registerBackend(r) == Error::Invalid);

        // Invalid backend handle.
        r.factory = []() -> VideoEncoder * {
                return nullptr;
        };
        r.backend = VideoCodec::Backend();
        CHECK(VideoEncoder::registerBackend(r) == Error::Invalid);

        // Invalid codec ID.
        r.backend = goodBackend;
        r.codecId = VideoCodec::Invalid;
        CHECK(VideoEncoder::registerBackend(r) == Error::Invalid);
}

TEST_CASE("VideoDecoder::registerBackend rejects malformed records") {
        VideoCodec passthrough(gPassthroughCodecId);
        auto       backends = passthrough.availableDecoderBackends();
        REQUIRE_FALSE(backends.isEmpty());
        const auto goodBackend = backends.front();

        VideoDecoder::BackendRecord r;
        r.codecId = gPassthroughCodecId;
        r.backend = goodBackend;
        CHECK(VideoDecoder::registerBackend(r) == Error::Invalid);

        r.factory = []() -> VideoDecoder * {
                return nullptr;
        };
        r.backend = VideoCodec::Backend();
        CHECK(VideoDecoder::registerBackend(r) == Error::Invalid);

        r.backend = goodBackend;
        r.codecId = VideoCodec::Invalid;
        CHECK(VideoDecoder::registerBackend(r) == Error::Invalid);
}

// ---------------------------------------------------------------------------
// supportedInputs / supportedOutputs union & pinned variants
// ---------------------------------------------------------------------------

TEST_CASE("VideoEncoder::supportedInputsFor: empty list for unknown codec") {
        auto out = VideoEncoder::supportedInputsFor(static_cast<VideoCodec::ID>(0xBAADBEEF), VideoCodec::Backend());
        CHECK(out.isEmpty());
}

TEST_CASE("VideoDecoder::supportedOutputsFor: empty list for unknown codec") {
        auto out = VideoDecoder::supportedOutputsFor(static_cast<VideoCodec::ID>(0xBAADBEEF), VideoCodec::Backend());
        CHECK(out.isEmpty());
}

TEST_CASE("VideoEncoder::supportedInputsFor: pin to unattached backend returns empty") {
        auto bk = VideoCodec::registerBackend("VideoCodecTest_UnattachedPin");
        REQUIRE(isOk(bk));
        auto out = VideoEncoder::supportedInputsFor(gPassthroughCodecId, value(bk));
        CHECK(out.isEmpty());
}

TEST_CASE("VideoDecoder::supportedOutputsFor: pin to unattached backend returns empty") {
        auto bk = VideoCodec::registerBackend("VideoCodecTest_UnattachedPinDec");
        REQUIRE(isOk(bk));
        auto out = VideoDecoder::supportedOutputsFor(gPassthroughCodecId, value(bk));
        CHECK(out.isEmpty());
}

TEST_CASE("VideoCodec: canEncode / canDecode pinned to an unattached backend is false") {
        auto bk = VideoCodec::registerBackend("VideoCodecTest_PinUnattachedCheck");
        REQUIRE(isOk(bk));
        VideoCodec codec(gPassthroughCodecId, value(bk));
        CHECK_FALSE(codec.canEncode());
        CHECK_FALSE(codec.canDecode());
}

// ---------------------------------------------------------------------------
// availableBackends — unknown codec
// ---------------------------------------------------------------------------

TEST_CASE("VideoEncoder::availableBackends for unknown codec returns empty list") {
        auto out = VideoEncoder::availableBackends(static_cast<VideoCodec::ID>(0xBAADBEEF));
        CHECK(out.isEmpty());
}

TEST_CASE("VideoDecoder::availableBackends for unknown codec returns empty list") {
        auto out = VideoDecoder::availableBackends(static_cast<VideoCodec::ID>(0xBAADBEEF));
        CHECK(out.isEmpty());
}

// ---------------------------------------------------------------------------
// VideoEncoder::create / VideoDecoder::create error paths
// ---------------------------------------------------------------------------

TEST_CASE("VideoEncoder::create returns IdNotFound for unknown codec") {
        auto res = VideoEncoder::create(static_cast<VideoCodec::ID>(0xDEAD), VideoCodec::Backend(), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("VideoDecoder::create returns IdNotFound for unknown codec") {
        auto res = VideoDecoder::create(static_cast<VideoCodec::ID>(0xDEAD), VideoCodec::Backend(), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("VideoEncoder::create with pin to unregistered backend returns IdNotFound") {
        auto bk = VideoCodec::registerBackend("VideoEncoderTest_UnregisteredPin");
        REQUIRE(isOk(bk));
        auto res = VideoEncoder::create(gPassthroughCodecId, value(bk), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("VideoDecoder::create with pin to unregistered backend returns IdNotFound") {
        auto bk = VideoCodec::registerBackend("VideoDecoderTest_UnregisteredPin");
        REQUIRE(isOk(bk));
        auto res = VideoDecoder::create(gPassthroughCodecId, value(bk), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

// ---------------------------------------------------------------------------
// CodecBackend MediaConfig override path
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec::createEncoder: CodecBackend override pins the requested backend") {
        // Register a second backend for the passthrough codec so the
        // override branch has a real choice between two attached records.
        auto bk = VideoCodec::registerBackend("VideoCodecTest_OverrideBackend");
        REQUIRE(isOk(bk));
        VideoCodec::Backend overrideBackend = value(bk);

        VideoEncoder::BackendRecord rec;
        rec.codecId = gPassthroughCodecId;
        rec.backend = overrideBackend;
        rec.weight = BackendWeight::Vendored; // lower than the default User
        rec.factory = []() -> VideoEncoder * {
                return new PassthroughVideoEncoder();
        };
        REQUIRE(VideoEncoder::registerBackend(rec) == Error::Ok);

        VideoCodec codec(gPassthroughCodecId);

        // Default selector picks the higher-weight User backend.
        auto defResult = codec.createEncoder(nullptr);
        REQUIRE(isOk(defResult));
        CHECK(value(defResult)->codec().backend().name() == "Passthrough");
        delete value(defResult);

        // Override pins the lower-weight Vendored entry.
        MediaConfig cfg;
        cfg.set(MediaConfig::CodecBackend, String("VideoCodecTest_OverrideBackend"));
        auto pinResult = codec.createEncoder(&cfg);
        REQUIRE(isOk(pinResult));
        CHECK(value(pinResult)->codec().backend() == overrideBackend);
        delete value(pinResult);
}

TEST_CASE("VideoCodec::createEncoder: CodecBackend override naming an unattached backend errors") {
        // Register a backend handle without attaching any factory.
        auto bk = VideoCodec::registerBackend("VideoCodecTest_OverrideUnattached");
        REQUIRE(isOk(bk));
        MediaConfig cfg;
        cfg.set(MediaConfig::CodecBackend, String("VideoCodecTest_OverrideUnattached"));
        VideoCodec codec(gPassthroughCodecId);
        auto       res = codec.createEncoder(&cfg);
        CHECK(error(res).isError());
}

TEST_CASE("VideoCodec::createDecoder: CodecBackend override naming an unattached backend errors") {
        auto bk = VideoCodec::registerBackend("VideoCodecTest_DecOverrideUnattached");
        REQUIRE(isOk(bk));
        MediaConfig cfg;
        cfg.set(MediaConfig::CodecBackend, String("VideoCodecTest_DecOverrideUnattached"));
        VideoCodec codec(gPassthroughCodecId);
        auto       res = codec.createDecoder(&cfg);
        CHECK(error(res).isError());
}

// ---------------------------------------------------------------------------
// VideoDecoder rejects invalid input + reset
// ---------------------------------------------------------------------------

TEST_CASE("VideoDecoder: submitPayload rejects null Ptr") {
        auto *dec = makePassthroughDecoder();
        REQUIRE(dec != nullptr);
        Error err = dec->submitPayload(CompressedVideoPayload::Ptr());
        CHECK(err == Error::Invalid);
        CHECK_FALSE(dec->lastErrorMessage().isEmpty());
        delete dec;
}

TEST_CASE("VideoEncoder: reset clears pending packets and restores flush state") {
        auto *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);
        auto f = makeTestPayload(2, 2, 0x55);
        REQUIRE(enc->submitPayload(f) == Error::Ok);
        REQUIRE(enc->reset() == Error::Ok);
        // After reset there should be nothing queued.
        CHECK_FALSE(enc->receiveCompressedPayload());

        // Reset's behaviour also implies the next submit's first frame
        // is once again marked Keyframe (frameIdx == 0).
        REQUIRE(enc->submitPayload(f) == Error::Ok);
        auto p = enc->receiveCompressedPayload();
        REQUIRE(p);
        CHECK(p->isKeyframe());
        delete enc;
}

TEST_CASE("VideoDecoder: reset clears pending packets") {
        auto *dec = static_cast<PassthroughVideoDecoder *>(makePassthroughDecoder());
        REQUIRE(dec != nullptr);
        dec->setOutput(Size2Du32(2, 2), PixelFormat(PixelFormat::RGB8_sRGB));
        auto f = makeTestPayload(2, 2, 0x66);
        // Wrap the source plane as a compressed payload and submit.
        ImageDesc cdesc(Size2Du32(2, 2), PixelFormat(PixelFormat::H264));
        auto      fPlane = f->plane(0);
        auto      pkt =
                CompressedVideoPayload::Ptr::create(cdesc, BufferView(fPlane.buffer(), fPlane.offset(), fPlane.size()));
        REQUIRE(dec->submitPayload(pkt) == Error::Ok);
        REQUIRE(dec->reset() == Error::Ok);
        CHECK_FALSE(dec->receiveVideoPayload().isValid());
        delete dec;
}

// ---------------------------------------------------------------------------
// Re-registering the same (codec, backend) pair replaces the prior record
// ---------------------------------------------------------------------------

TEST_CASE("VideoEncoder::registerBackend replaces the prior record for the same (codec,backend)") {
        VideoCodec passthrough(gPassthroughCodecId);
        auto       backends = passthrough.availableEncoderBackends();
        REQUIRE_FALSE(backends.isEmpty());
        const auto target = backends.front();

        // Remember the count of registered backends before the
        // re-registration so we can prove it hasn't grown.
        const size_t before = backends.size();

        VideoEncoder::BackendRecord r;
        r.codecId = gPassthroughCodecId;
        r.backend = target;
        r.weight = BackendWeight::User + 200;
        r.factory = []() -> VideoEncoder * {
                return new PassthroughVideoEncoder();
        };
        REQUIRE(VideoEncoder::registerBackend(r) == Error::Ok);

        auto after = passthrough.availableEncoderBackends();
        CHECK(after.size() == before);
        CHECK(after.contains(target));
}

// ---------------------------------------------------------------------------
// VideoEncoder::create with default config (no MediaConfig) succeeds
// ---------------------------------------------------------------------------

TEST_CASE("VideoEncoder::create with null config succeeds and skips configure") {
        auto res = VideoEncoder::create(gPassthroughCodecId, VideoCodec::Backend(), nullptr);
        REQUIRE(isOk(res));
        VideoEncoder *enc = value(res);
        REQUIRE(enc != nullptr);
        // Passthrough's configure() reads BitrateKbps; without a config
        // it must remain at its default zero.
        auto *typed = static_cast<PassthroughVideoEncoder *>(enc);
        CHECK(typed->configuredBitrate() == 0);
        delete enc;
}

// ===========================================================================
// Invalid sentinel name round-trips losslessly
// ===========================================================================
//
// VideoCodec() is a legitimate value used as a "no codec" sentinel.
// Because Variant serializes it via toString() and parses back via
// fromString → lookup, the "Invalid" sentinel name must be registered
// so the round-trip is lossless and JSON-serialised defaults survive
// a parse-back.

TEST_CASE("VideoCodec: Invalid sentinel name round-trips through lookup") {
        VideoCodec inv;
        REQUIRE(!inv.isValid());
        REQUIRE(inv.id() == VideoCodec::Invalid);
        REQUIRE(inv.name() == "Invalid");

        auto r = VideoCodec::lookup("Invalid");
        CHECK(error(r).isOk());
        CHECK(value(r).id() == VideoCodec::Invalid);

        // A name that's genuinely missing still reports IdNotFound.
        auto miss = VideoCodec::lookup("DefinitelyNotARealCodec");
        CHECK(error(miss) == Error::IdNotFound);
}

TEST_CASE("VideoCodec: every registered ID round-trips through lookup by name") {
        for (auto id : VideoCodec::registeredIDs()) {
                VideoCodec vc(id);
                CAPTURE(vc.name());
                auto r = VideoCodec::lookup(vc.name());
                CHECK(error(r).isOk());
                CHECK(value(r).id() == id);
        }
        // Plus the Invalid sentinel.
        VideoCodec inv;
        auto       r = VideoCodec::lookup(inv.name());
        CHECK(error(r).isOk());
        CHECK(value(r).id() == VideoCodec::Invalid);
}
