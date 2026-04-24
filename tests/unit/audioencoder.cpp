/**
 * @file      tests/audioencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the AudioEncoder / AudioDecoder contract with an
 * in-test "PassthroughAudio" codec that simply moves the PCM buffer
 * into a @ref CompressedAudioPayload and back out.  The intent is to prove the
 * push/pull plumbing, the typed (codec, backend) factory resolution
 * (including the @ref MediaConfig::CodecBackend override), the
 * @ref AudioCodec::canEncode / @ref AudioCodec::canDecode probes,
 * and the flush/EOS handling all work end-to-end before any real
 * audio codec backend is plugged in.
 */

#include <doctest/doctest.h>
#include <deque>
#include <promeki/audiobuffer.h>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/buffer.h>
#include <promeki/mediaconfig.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>

using namespace promeki;

namespace {

// Slots for the Passthrough codec's typed AudioCodec::ID and two
// backend handles — populated at static-init time by
// PassthroughRegistrar and read by every test that needs to resolve
// them.  The "Vendored" backend registers at the Vendored weight, the
// "User" backend at the User weight, so the default-selector tests can
// prove higher weight wins and override tests can pin the lower-weight
// entry explicitly.
static AudioCodec::ID           gPassthroughCodecId;
static AudioCodec::Backend      gVendoredBackend;
static AudioCodec::Backend      gUserBackend;

// Slot for the compressed @ref AudioFormat that mirrors
// gPassthroughCodecId.  The payload-native output path needs a
// compressed AudioFormat whose @ref AudioFormat::audioCodec matches
// the session's codec, so @ref CompressedAudioPayload::isValid passes
// and downstream consumers can recover the payload's codec identity.
static AudioFormat gPassthroughCompressedFormat;

class PassthroughAudioEncoder : public AudioEncoder {
        public:
                void configure(const MediaConfig &config) override {
                        _cfgBitrate = config.getAs<int32_t>(MediaConfig::BitrateKbps);
                }

                Error submitPayload(const PcmAudioPayload::Ptr &payload) override {
                        clearError();
                        if(!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                setError(Error::Invalid, "invalid audio payload");
                                return _lastError;
                        }
                        // Passthrough: wrap the payload's plane-0 view
                        // in a CompressedAudioPayload that shares the
                        // same Buffer.  The descriptor uses the
                        // registered compressed AudioFormat for this
                        // test codec so CompressedAudioPayload::isValid
                        // passes and any legacy bridge recovers the
                        // right @ref AudioCodec identity.
                        AudioDesc desc(gPassthroughCompressedFormat,
                                       payload->desc().sampleRate(),
                                       payload->desc().channels());
                        auto pv = payload->plane(0);
                        auto cvp = CompressedAudioPayload::Ptr::create(
                                desc, BufferView(pv.buffer(), pv.offset(), pv.size()));
                        cvp.modify()->setPts(payload->pts());
                        _queue.push_back(cvp);
                        return Error::Ok;
                }

                CompressedAudioPayload::Ptr receiveCompressedPayload() override {
                        if(_queue.empty()) {
                                if(_flushed && !_eosEmitted) {
                                        _eosEmitted = true;
                                        AudioDesc eosDesc(gPassthroughCompressedFormat, 0, 0);
                                        auto eos = CompressedAudioPayload::Ptr::create(eosDesc);
                                        eos.modify()->markEndOfStream();
                                        return eos;
                                }
                                return CompressedAudioPayload::Ptr();
                        }
                        auto pkt = _queue.front();
                        _queue.pop_front();
                        return pkt;
                }

                Error flush() override { _flushed = true; return Error::Ok; }
                Error reset() override {
                        _queue.clear();
                        _flushed    = false;
                        _eosEmitted = false;
                        return Error::Ok;
                }

                int32_t configuredBitrate() const { return _cfgBitrate; }

        private:
                std::deque<CompressedAudioPayload::Ptr> _queue;
                int32_t                      _cfgBitrate = 0;
                bool                         _flushed    = false;
                bool                         _eosEmitted = false;
};

class PassthroughAudioDecoder : public AudioDecoder {
        public:
                void configure(const MediaConfig &cfg) override {
                        float sr = cfg.getAs<float>(MediaConfig::AudioRate);
                        if(sr > 0.0f) _outDesc.setSampleRate(sr);
                        int32_t ch = cfg.getAs<int32_t>(MediaConfig::AudioChannels);
                        if(ch > 0) _outDesc.setChannels(static_cast<unsigned int>(ch));
                }

                Error submitPayload(const CompressedAudioPayload::Ptr &payload) override {
                        clearError();
                        if(!payload.isValid() || !payload->isValid()) {
                                setError(Error::Invalid, "invalid payload");
                                return _lastError;
                        }
                        _pending.push_back(payload);
                        return Error::Ok;
                }

                PcmAudioPayload::Ptr receiveAudioPayload() override {
                        if(_pending.empty()) return PcmAudioPayload::Ptr();
                        CompressedAudioPayload::Ptr in = std::move(_pending.front());
                        _pending.pop_front();
                        if(in->planeCount() == 0) return PcmAudioPayload::Ptr();
                        // Compute sample count from bytes and the
                        // configured output format.
                        auto view = in->plane(0);
                        size_t sampleBytes = _outDesc.bytesPerSample() * _outDesc.channels();
                        size_t samples = sampleBytes ? view.size() / sampleBytes : 0;
                        BufferView planes(view.buffer(), view.offset(), view.size());
                        auto uap = PcmAudioPayload::Ptr::create(_outDesc, samples, planes);
                        uap.modify()->setPts(in->pts());
                        return uap;
                }

                Error flush() override { return Error::Ok; }
                Error reset() override { _pending.clear(); return Error::Ok; }

                void setOutputDesc(const AudioDesc &d) { _outDesc = d; }

        private:
                std::deque<CompressedAudioPayload::Ptr> _pending;
                AudioDesc               _outDesc;
};

// Registers the passthrough codec exactly once per process so tests
// can call AudioCodec::createEncoder / createDecoder.  Two encoder
// backends are registered against the same codec ID (one Vendored,
// one User) to exercise the weighted selection + override flow.
struct PassthroughRegistrar {
        PassthroughRegistrar() {
                gPassthroughCodecId = AudioCodec::registerType();
                AudioCodec::Data d;
                d.id   = gPassthroughCodecId;
                d.name = "PassthroughAudio";
                d.desc = "Passthrough (test) audio codec";
                d.supportedSampleFormats = {
                        static_cast<int>(AudioFormat::PCMI_S16LE),
                };
                AudioCodec::registerData(std::move(d));

                // Register a compressed AudioFormat for this codec so
                // the Passthrough encoder's output payloads can mark
                // themselves compressed via their descriptor.
                AudioFormat::ID cfmtId = AudioFormat::registerType();
                AudioFormat::Data cf;
                cf.id         = cfmtId;
                cf.name       = "PassthroughAudioCompressed";
                cf.desc       = "Passthrough (test) compressed format";
                cf.compressed = true;
                cf.audioCodec = AudioCodec(gPassthroughCodecId);
                AudioFormat::registerData(std::move(cf));
                gPassthroughCompressedFormat = AudioFormat(cfmtId);

                auto vb = AudioCodec::registerBackend("PassthroughVendored");
                if(error(vb).isError()) return;
                gVendoredBackend = value(vb);

                auto ub = AudioCodec::registerBackend("PassthroughUser");
                if(error(ub).isError()) return;
                gUserBackend = value(ub);

                AudioEncoder::registerBackend({
                        .codecId         = gPassthroughCodecId,
                        .backend         = gVendoredBackend,
                        .weight          = BackendWeight::Vendored,
                        .supportedInputs = {
                                static_cast<int>(AudioFormat::PCMI_S16LE),
                        },
                        .factory         = []() -> AudioEncoder * {
                                return new PassthroughAudioEncoder();
                        },
                });
                // Higher-weight User backend lives at the same codec
                // ID with a different backend handle so override tests
                // have something to pin against.
                AudioEncoder::registerBackend({
                        .codecId         = gPassthroughCodecId,
                        .backend         = gUserBackend,
                        .weight          = BackendWeight::User,
                        .supportedInputs = {
                                static_cast<int>(AudioFormat::PCMI_S16LE),
                        },
                        .factory         = []() -> AudioEncoder * {
                                return new PassthroughAudioEncoder();
                        },
                });

                AudioDecoder::registerBackend({
                        .codecId          = gPassthroughCodecId,
                        .backend          = gVendoredBackend,
                        .weight           = BackendWeight::Vendored,
                        .supportedOutputs = {
                                static_cast<int>(AudioFormat::PCMI_S16LE),
                        },
                        .factory          = []() -> AudioDecoder * {
                                return new PassthroughAudioDecoder();
                        },
                });
        }
};
static PassthroughRegistrar _passthroughRegistrar;

// Helper: build a small PCM audio payload with deterministic bytes.
PcmAudioPayload::Ptr makePcmPayload(size_t samples, uint8_t fill,
                                             const AudioDesc &desc = AudioDesc(AudioFormat::PCMI_S16LE,
                                                                               48000.0f, 2)) {
        const size_t bytes = desc.bufferSize(samples);
        auto buf = Buffer::Ptr::create(bytes);
        buf.modify()->fill(static_cast<char>(fill));
        buf.modify()->setSize(bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        return PcmAudioPayload::Ptr::create(desc, samples, planes);
}

AudioEncoder *makePassthroughEncoder(const MediaConfig *cfg = nullptr) {
        AudioCodec codec(gPassthroughCodecId);
        auto res = codec.createEncoder(cfg);
        return isOk(res) ? value(res) : nullptr;
}

AudioDecoder *makePassthroughDecoder(const MediaConfig *cfg = nullptr) {
        AudioCodec codec(gPassthroughCodecId);
        auto res = codec.createDecoder(cfg);
        return isOk(res) ? value(res) : nullptr;
}

} // namespace

TEST_CASE("AudioCodec: createEncoder resolves through the backend registry") {
        AudioCodec codec(gPassthroughCodecId);
        REQUIRE(codec.isValid());
        CHECK(codec.canEncode());

        AudioEncoder *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);
        CHECK(enc->codec().name() == "PassthroughAudio");
        CHECK(enc->codec().id() == gPassthroughCodecId);
        delete enc;
}

TEST_CASE("AudioCodec: createDecoder resolves through the backend registry") {
        AudioCodec codec(gPassthroughCodecId);
        REQUIRE(codec.canDecode());

        AudioDecoder *dec = makePassthroughDecoder();
        REQUIRE(dec != nullptr);
        CHECK(dec->codec().name() == "PassthroughAudio");
        CHECK(dec->codec().id() == gPassthroughCodecId);
        delete dec;
}

TEST_CASE("AudioCodec: canEncode / canDecode return false for a codec without a backend") {
        // AAC has no backend registered in the unit-test build.
        AudioCodec aac(AudioCodec::AAC);
        REQUIRE(aac.isValid());
        CHECK_FALSE(aac.canEncode());
        CHECK_FALSE(aac.canDecode());

        // Invalid codec returns Error / false.
        AudioCodec bad;
        CHECK_FALSE(bad.canEncode());
        auto r = bad.createEncoder();
        CHECK(error(r).isError());
}

TEST_CASE("AudioCodec: availableEncoderBackends lists every registered backend") {
        AudioCodec codec(gPassthroughCodecId);
        auto backends = codec.availableEncoderBackends();
        REQUIRE(backends.size() == 2);
        // Highest weight first: PassthroughUser before PassthroughVendored.
        CHECK(backends[0] == gUserBackend);
        CHECK(backends[1] == gVendoredBackend);
}

TEST_CASE("AudioCodec: CodecBackend override pins the requested backend") {
        AudioCodec codec(gPassthroughCodecId);

        // No override -> highest weight (User) wins; we can confirm by
        // inspecting the codec attached to the encoder.
        AudioEncoder *defaultEnc = makePassthroughEncoder();
        REQUIRE(defaultEnc != nullptr);
        CHECK(defaultEnc->codec().backend() == gUserBackend);
        delete defaultEnc;

        // Override pins the lower-weight Vendored entry.
        MediaConfig cfg;
        cfg.set(MediaConfig::CodecBackend, String("PassthroughVendored"));
        AudioEncoder *pinned = makePassthroughEncoder(&cfg);
        REQUIRE(pinned != nullptr);
        CHECK(pinned->codec().backend() == gVendoredBackend);
        delete pinned;

        // Override naming a registered-but-not-attached backend -> error.
        // Register the backend handle directly without attaching any
        // factory, then ask for it by name through the override.
        auto dummy = AudioCodec::registerBackend("PassthroughDummy");
        REQUIRE(isOk(dummy));
        MediaConfig bogus;
        bogus.set(MediaConfig::CodecBackend, String("PassthroughDummy"));
        auto r = codec.createEncoder(&bogus);
        CHECK(error(r).isError());
}

TEST_CASE("AudioCodec: pinned backend via AudioCodec wrapper resolves directly") {
        AudioCodec codec(gPassthroughCodecId, gVendoredBackend);
        AudioEncoder *enc = makePassthroughEncoder();
        // (the helper ignores the wrapper's backend; re-create through
        // the pinned AudioCodec here instead.)
        delete enc;

        auto res = codec.createEncoder();
        REQUIRE(isOk(res));
        AudioEncoder *pinnedEnc = value(res);
        CHECK(pinnedEnc->codec().backend() == gVendoredBackend);
        delete pinnedEnc;
}

TEST_CASE("AudioEncoder: configure forwards well-known keys") {
        auto *raw = makePassthroughEncoder();
        REQUIRE(raw != nullptr);
        auto *enc = static_cast<PassthroughAudioEncoder *>(raw);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(64));
        enc->configure(cfg);
        CHECK(enc->configuredBitrate() == 64);
        delete enc;
}

TEST_CASE("AudioCodec::createEncoder: configure() runs automatically when config is supplied") {
        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(96));
        cfg.set(MediaConfig::CodecBackend, String("PassthroughVendored"));

        auto *raw = makePassthroughEncoder(&cfg);
        REQUIRE(raw != nullptr);
        auto *enc = static_cast<PassthroughAudioEncoder *>(raw);
        CHECK(enc->configuredBitrate() == 96);
        delete enc;
}

TEST_CASE("AudioEncoder: flush emits an EndOfStream packet") {
        AudioEncoder *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);

        auto frame = makePcmPayload(64, 0x42);
        REQUIRE(enc->submitPayload(frame) == Error::Ok);
        CHECK(enc->receiveCompressedPayload());

        CHECK(enc->flush() == Error::Ok);
        auto eos = enc->receiveCompressedPayload();
        REQUIRE(eos);
        CHECK(eos->isEndOfStream());
        CHECK_FALSE(enc->receiveCompressedPayload());
        delete enc;
}

TEST_CASE("AudioEncoder: submitPayload rejects invalid input") {
        AudioEncoder *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);

        PcmAudioPayload::Ptr empty;
        Error err = enc->submitPayload(empty);
        CHECK(err == Error::Invalid);
        CHECK(enc->lastError() == Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Audio codec: encoder -> packet -> decoder round-trip") {
        AudioEncoder *enc = makePassthroughEncoder();
        AudioDecoder *dec = makePassthroughDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        static_cast<PassthroughAudioDecoder *>(dec)->setOutputDesc(desc);

        auto src = makePcmPayload(128, 0x37, desc);

        CHECK(enc->submitPayload(src) == Error::Ok);
        auto pkt = enc->receiveCompressedPayload();
        REQUIRE(pkt);
        CHECK(pkt->desc().format().audioCodec().id() == gPassthroughCodecId);

        CHECK(dec->submitPayload(pkt) == Error::Ok);
        PcmAudioPayload::Ptr out = dec->receiveAudioPayload();
        REQUIRE(out.isValid());

        auto srcPlane = src->plane(0);
        auto outPlane = out->plane(0);
        CHECK(srcPlane.size() == outPlane.size());
        const uint8_t *srcBytes = srcPlane.data();
        const uint8_t *outBytes = outPlane.data();
        for(size_t i = 0; i < srcPlane.size(); ++i) {
                CHECK(srcBytes[i] == outBytes[i]);
        }

        delete enc;
        delete dec;
}

// ---------------------------------------------------------------------------
// AudioEncoder::registerBackend / AudioDecoder::registerBackend — error paths
// ---------------------------------------------------------------------------

TEST_CASE("AudioEncoder::registerBackend rejects malformed records") {
        // Missing factory.
        AudioEncoder::BackendRecord r;
        r.codecId = gPassthroughCodecId;
        r.backend = gVendoredBackend;
        // r.factory left empty.
        CHECK(AudioEncoder::registerBackend(r) == Error::Invalid);

        // Invalid backend handle.
        r.factory = []() -> AudioEncoder * { return nullptr; };
        r.backend = AudioCodec::Backend();
        CHECK(AudioEncoder::registerBackend(r) == Error::Invalid);

        // Invalid codec ID.
        r.backend = gVendoredBackend;
        r.codecId = AudioCodec::Invalid;
        CHECK(AudioEncoder::registerBackend(r) == Error::Invalid);
}

TEST_CASE("AudioDecoder::registerBackend rejects malformed records") {
        AudioDecoder::BackendRecord r;
        r.codecId = gPassthroughCodecId;
        r.backend = gVendoredBackend;
        // No factory.
        CHECK(AudioDecoder::registerBackend(r) == Error::Invalid);

        r.factory = []() -> AudioDecoder * { return nullptr; };
        r.backend = AudioCodec::Backend();
        CHECK(AudioDecoder::registerBackend(r) == Error::Invalid);

        r.backend = gVendoredBackend;
        r.codecId = AudioCodec::Invalid;
        CHECK(AudioDecoder::registerBackend(r) == Error::Invalid);
}

// ---------------------------------------------------------------------------
// supportedInputs / supportedOutputs union & pinned variants
// ---------------------------------------------------------------------------

TEST_CASE("AudioEncoder::supportedInputsFor: union vs pinned-backend") {
        // Unpinned (invalid handle) returns the union across both
        // Passthrough backends — both registered PCMI_S16LE so the
        // union has a single entry.
        auto unionList = AudioEncoder::supportedInputsFor(gPassthroughCodecId,
                                                          AudioCodec::Backend());
        CHECK(unionList.contains(static_cast<int>(AudioFormat::PCMI_S16LE)));

        // Pinned to a registered backend returns just that backend's list.
        auto pinned = AudioEncoder::supportedInputsFor(gPassthroughCodecId,
                                                       gVendoredBackend);
        CHECK(pinned.contains(static_cast<int>(AudioFormat::PCMI_S16LE)));

        // Pinned to a registered-but-not-attached backend returns empty.
        auto bk = AudioCodec::registerBackend("AudioEncoderTest_UnattachedBackend");
        REQUIRE(isOk(bk));
        auto missing = AudioEncoder::supportedInputsFor(gPassthroughCodecId,
                                                        value(bk));
        CHECK(missing.isEmpty());

        // Unknown codec ID returns empty regardless of backend pin.
        auto bogus = AudioEncoder::supportedInputsFor(
                static_cast<AudioCodec::ID>(0xDEADBEEF), AudioCodec::Backend());
        CHECK(bogus.isEmpty());
}

TEST_CASE("AudioDecoder::supportedOutputsFor: union vs pinned-backend") {
        auto unionList = AudioDecoder::supportedOutputsFor(gPassthroughCodecId,
                                                           AudioCodec::Backend());
        CHECK(unionList.contains(static_cast<int>(AudioFormat::PCMI_S16LE)));

        auto pinned = AudioDecoder::supportedOutputsFor(gPassthroughCodecId,
                                                        gVendoredBackend);
        CHECK(pinned.contains(static_cast<int>(AudioFormat::PCMI_S16LE)));

        // Unknown codec returns empty.
        auto bogus = AudioDecoder::supportedOutputsFor(
                static_cast<AudioCodec::ID>(0xDEADBEEF), AudioCodec::Backend());
        CHECK(bogus.isEmpty());
}

TEST_CASE("AudioCodec::canEncode pinned to a registered-but-not-attached backend is false") {
        auto bk = AudioCodec::registerBackend("AudioEncoderTest_PinNotAttached");
        REQUIRE(isOk(bk));
        AudioCodec codec(gPassthroughCodecId, value(bk));
        CHECK_FALSE(codec.canEncode());
        CHECK_FALSE(codec.canDecode());
}

TEST_CASE("AudioEncoder::availableBackends for unknown codec returns empty list") {
        auto out = AudioEncoder::availableBackends(static_cast<AudioCodec::ID>(0xBAADBEEF));
        CHECK(out.isEmpty());
}

TEST_CASE("AudioDecoder::availableBackends for unknown codec returns empty list") {
        auto out = AudioDecoder::availableBackends(static_cast<AudioCodec::ID>(0xBAADBEEF));
        CHECK(out.isEmpty());
}

// ---------------------------------------------------------------------------
// AudioCodec::registeredBackends() — union over both registries
// ---------------------------------------------------------------------------

TEST_CASE("AudioCodec::registeredBackends includes the Passthrough backends") {
        auto backends = AudioCodec::registeredBackends();
        CHECK_FALSE(backends.isEmpty());

        bool sawVendored = false;
        bool sawUser     = false;
        for(const auto &b : backends) {
                if(b.name() == "PassthroughVendored") sawVendored = true;
                if(b.name() == "PassthroughUser")     sawUser     = true;
        }
        CHECK(sawVendored);
        CHECK(sawUser);
}

// ---------------------------------------------------------------------------
// re-registering a (codec, backend) pair replaces the prior record
// ---------------------------------------------------------------------------

TEST_CASE("AudioEncoder::registerBackend replaces the prior record for the same (codec,backend)") {
        // Re-register the passthrough Vendored backend with a noticeably
        // different weight; the registry must still report exactly one
        // entry for that backend, with the new weight in effect (proven
        // by sortByWeight: the new heavier record should outrank the
        // previously-registered User record).
        AudioEncoder::BackendRecord r;
        r.codecId = gPassthroughCodecId;
        r.backend = gVendoredBackend;
        r.weight  = BackendWeight::User + 100;   // outrank the User entry
        r.factory = []() -> AudioEncoder * { return new PassthroughAudioEncoder; };
        r.supportedInputs = { static_cast<int>(AudioFormat::PCMI_S16LE) };
        REQUIRE(AudioEncoder::registerBackend(r) == Error::Ok);

        AudioCodec codec(gPassthroughCodecId);
        auto backends = codec.availableEncoderBackends();
        // Highest weight first now: Vendored (just bumped) before User.
        REQUIRE(backends.size() == 2);
        CHECK(backends[0] == gVendoredBackend);
        CHECK(backends[1] == gUserBackend);

        // Restore the original Vendored weight so other tests see
        // the User backend as the highest-weight default again.
        AudioEncoder::BackendRecord restore;
        restore.codecId = gPassthroughCodecId;
        restore.backend = gVendoredBackend;
        restore.weight  = BackendWeight::Vendored;
        restore.factory = []() -> AudioEncoder * { return new PassthroughAudioEncoder; };
        restore.supportedInputs = { static_cast<int>(AudioFormat::PCMI_S16LE) };
        REQUIRE(AudioEncoder::registerBackend(restore) == Error::Ok);

        auto restored = codec.availableEncoderBackends();
        REQUIRE(restored.size() == 2);
        CHECK(restored[0] == gUserBackend);
        CHECK(restored[1] == gVendoredBackend);
}

// ---------------------------------------------------------------------------
// AudioEncoder::create / AudioDecoder::create error paths
// ---------------------------------------------------------------------------

TEST_CASE("AudioEncoder::create returns IdNotFound for unknown codec") {
        auto res = AudioEncoder::create(static_cast<AudioCodec::ID>(0xDEAD),
                                         AudioCodec::Backend(), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("AudioDecoder::create returns IdNotFound for unknown codec") {
        auto res = AudioDecoder::create(static_cast<AudioCodec::ID>(0xDEAD),
                                         AudioCodec::Backend(), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("AudioEncoder::create with pin to unregistered backend returns IdNotFound") {
        auto bk = AudioCodec::registerBackend("AudioEncoderTest_UnattachedPin");
        REQUIRE(isOk(bk));
        auto res = AudioEncoder::create(gPassthroughCodecId, value(bk), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("AudioDecoder::create with pin to unregistered backend returns IdNotFound") {
        auto bk = AudioCodec::registerBackend("AudioDecoderTest_UnattachedPin");
        REQUIRE(isOk(bk));
        auto res = AudioDecoder::create(gPassthroughCodecId, value(bk), nullptr);
        CHECK(error(res) == Error::IdNotFound);
}

// ---------------------------------------------------------------------------
// AudioCodec::createDecoder error gating
// ---------------------------------------------------------------------------

TEST_CASE("AudioCodec::createDecoder on invalid codec returns Error::Invalid") {
        AudioCodec invalid;
        auto res = invalid.createDecoder();
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("AudioCodec::createEncoder on invalid codec returns Error::Invalid") {
        AudioCodec invalid;
        auto res = invalid.createEncoder();
        CHECK(error(res) == Error::Invalid);
}

// ---------------------------------------------------------------------------
// Decoder configure + round-trip for completeness
// ---------------------------------------------------------------------------

TEST_CASE("AudioDecoder: configure forwards AudioRate / AudioChannels") {
        auto *dec = makePassthroughDecoder();
        REQUIRE(dec != nullptr);

        // Seed a complete output desc so configure()'s sample-rate /
        // channel updates land on a desc that already carries an
        // AudioFormat — without that the produced payload is degenerate
        // and we can't observe the rate/channel changes downstream.
        AudioDesc seed(AudioFormat::PCMI_S16LE, 16000.0f, 1);
        static_cast<PassthroughAudioDecoder *>(dec)->setOutputDesc(seed);

        MediaConfig cfg;
        cfg.set(MediaConfig::AudioRate,     48000.0f);
        cfg.set(MediaConfig::AudioChannels, int32_t(2));
        dec->configure(cfg);

        auto frame = makePcmPayload(64, 0x66,
                AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2));
        AudioDesc cdesc(gPassthroughCompressedFormat, 48000.0f, 2);
        auto fEntry = frame->plane(0); auto pkt = CompressedAudioPayload::Ptr::create(cdesc, BufferView(fEntry.buffer(), fEntry.offset(), fEntry.size()));
        CHECK(dec->submitPayload(pkt) == Error::Ok);
        PcmAudioPayload::Ptr out = dec->receiveAudioPayload();
        REQUIRE(out.isValid());
        CHECK(out->desc().sampleRate() == 48000.0f);
        CHECK(out->desc().channels() == 2u);
        delete dec;
}

TEST_CASE("AudioDecoder: reset clears any pending state") {
        auto *dec = makePassthroughDecoder();
        REQUIRE(dec != nullptr);

        auto frame = makePcmPayload(32, 0x77,
                AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2));
        AudioDesc cdesc(gPassthroughCompressedFormat, 48000.0f, 2);
        auto fEntry = frame->plane(0); auto pkt = CompressedAudioPayload::Ptr::create(cdesc, BufferView(fEntry.buffer(), fEntry.offset(), fEntry.size()));
        // Submit but don't drain — reset() should drop the packet.
        REQUIRE(dec->submitPayload(pkt) == Error::Ok);
        REQUIRE(dec->reset() == Error::Ok);
        CHECK_FALSE(dec->receiveAudioPayload().isValid());
        delete dec;
}

TEST_CASE("AudioEncoder: reset clears the pending queue + restores flush state") {
        auto *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);

        auto frame = makePcmPayload(64, 0x44);
        REQUIRE(enc->submitPayload(frame) == Error::Ok);
        REQUIRE(enc->reset() == Error::Ok);
        CHECK_FALSE(enc->receiveCompressedPayload());

        // After reset the encoder must accept new frames cleanly.
        REQUIRE(enc->submitPayload(frame) == Error::Ok);
        auto pkt = enc->receiveCompressedPayload();
        CHECK(pkt);
        delete enc;
}

TEST_CASE("AudioEncoder: requestKeyframe default is a safe no-op") {
        auto *enc = makePassthroughEncoder();
        REQUIRE(enc != nullptr);
        // Passthrough encoder doesn't override requestKeyframe(), so
        // the call must hit the AudioEncoder base default.  No
        // observable behaviour change — this is purely a "doesn't
        // crash, doesn't set lastError" probe.
        enc->requestKeyframe();
        CHECK(enc->lastError() == Error::Ok);
        delete enc;
}

// ---------------------------------------------------------------------------
// Decoder rejects invalid input
// ---------------------------------------------------------------------------

TEST_CASE("AudioDecoder: submitPayload rejects null Ptr") {
        auto *dec = makePassthroughDecoder();
        REQUIRE(dec != nullptr);
        Error err = dec->submitPayload(CompressedAudioPayload::Ptr());
        CHECK(err == Error::Invalid);
        CHECK_FALSE(dec->lastErrorMessage().isEmpty());
        delete dec;
}
