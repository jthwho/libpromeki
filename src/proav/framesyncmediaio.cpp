/**
 * @file      framesyncmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framesyncmediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/colormodel.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/audiodesc.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/logger.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(FrameSyncFactory)

namespace {

        // Bridge: FrameSync resyncs frame rate (and optionally audio rate /
        // channels / sample format).  Inserted by the planner when the only
        // gap between source and sink is temporal cadence.
        bool framesyncBridge(const MediaDesc &from, const MediaDesc &to, MediaIO::Config *outConfig, int *outCost) {
                // Pixel descriptions must already match — pixel conversion
                // is CSC's job; FrameSync only re-times.
                if (!from.imageList().isEmpty() && !to.imageList().isEmpty()) {
                        if (from.imageList()[0].pixelFormat() != to.imageList()[0].pixelFormat()) return false;
                        if (from.imageList()[0].size() != to.imageList()[0].size()) return false;
                }

                const bool rateDiffers = from.frameRate() != to.frameRate();

                bool audioRateDiffers = false;
                bool audioChannelsDiffer = false;
                bool audioDataTypeDiffers = false;
                if (!from.audioList().isEmpty() && !to.audioList().isEmpty()) {
                        const AudioDesc &a = from.audioList()[0];
                        const AudioDesc &b = to.audioList()[0];
                        audioRateDiffers = a.sampleRate() != b.sampleRate();
                        audioChannelsDiffer = a.channels() != b.channels();
                        audioDataTypeDiffers = a.format().id() != b.format().id();
                }

                // Nothing to do — let the planner skip insertion.
                if (!rateDiffers && !audioRateDiffers && !audioChannelsDiffer && !audioDataTypeDiffers) {
                        return false;
                }

                if (outConfig != nullptr) {
                        *outConfig = MediaIOFactory::defaultConfig("FrameSync");
                        if (rateDiffers && to.frameRate().isValid()) {
                                outConfig->set(MediaConfig::OutputFrameRate, to.frameRate());
                        }
                        if (!to.audioList().isEmpty()) {
                                const AudioDesc &dst = to.audioList()[0];
                                if (audioRateDiffers && dst.sampleRate() > 0.0f) {
                                        outConfig->set(MediaConfig::OutputAudioRate, dst.sampleRate());
                                }
                                if (audioChannelsDiffer && dst.channels() > 0) {
                                        outConfig->set(MediaConfig::OutputAudioChannels,
                                                       static_cast<int>(dst.channels()));
                                }
                                if (audioDataTypeDiffers && dst.format().id() != AudioFormat::Invalid) {
                                        outConfig->set(MediaConfig::OutputAudioDataType,
                                                       AudioDataType(dst.format().id()));
                                }
                        }
                }
                if (outCost != nullptr) {
                        // Frame-rate change is the dominant cost (drop /
                        // repeat / interpolate).  Audio resampling adds a
                        // smaller penalty.  Both fit in the "bounded-error
                        // lossy" band.
                        int cost = 0;
                        if (rateDiffers) cost += 100;
                        if (audioRateDiffers || audioChannelsDiffer || audioDataTypeDiffers) cost += 75;
                        *outCost = cost;
                }
                return true;
        }

} // namespace

MediaIOFactory::Config::SpecMap FrameSyncFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::OutputFrameRate, FrameRate());
        s(MediaConfig::OutputAudioRate, 0.0f);
        s(MediaConfig::OutputAudioChannels, int32_t(0));
        s(MediaConfig::OutputAudioDataType, AudioDataType::Invalid);
        s(MediaConfig::InputQueueCapacity, int32_t(8));
        return specs;
}

bool FrameSyncFactory::bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig, int *outCost) const {
        return framesyncBridge(from, to, outConfig, outCost);
}

MediaIO *FrameSyncFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new FrameSyncMediaIO(parent);
        io->setConfig(config);
        return io;
}

FrameSyncMediaIO::FrameSyncMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

FrameSyncMediaIO::~FrameSyncMediaIO() {
        if (isOpen()) (void)close().wait();
}

void FrameSyncMediaIO::setClock(const Clock::Ptr &clock) {
        _externalClock = clock;
}

Error FrameSyncMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        const MediaDesc       &mdesc = cmd.pendingMediaDesc;

        FrameRate srcFps = mdesc.frameRate();
        if (!srcFps.isValid()) {
                promekiErr("FrameSyncMediaIO: pendingMediaDesc has no valid frame rate");
                return Error::InvalidArgument;
        }

        FrameRate outFps = cfg.getAs<FrameRate>(MediaConfig::OutputFrameRate, FrameRate());
        if (!outFps.isValid()) outFps = srcFps;

        int queueCap = cfg.getAs<int>(MediaConfig::InputQueueCapacity, 8);
        if (queueCap < 1) queueCap = 1;

        AudioDesc srcAdesc;
        if (!mdesc.audioList().isEmpty()) srcAdesc = mdesc.audioList()[0];

        AudioDesc adesc = srcAdesc;
        if (adesc.isValid()) {
                float           outRate = cfg.getAs<float>(MediaConfig::OutputAudioRate, 0.0f);
                int             outChannels = cfg.getAs<int>(MediaConfig::OutputAudioChannels, 0);
                AudioFormat::ID outDt = adesc.format().id();

                Error adtErr;
                Enum  adtEnum = cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &adtErr);
                if (adtErr.isOk() && adtEnum.hasListedValue() &&
                    static_cast<AudioFormat::ID>(adtEnum.value()) != AudioFormat::Invalid) {
                        outDt = static_cast<AudioFormat::ID>(adtEnum.value());
                }

                adesc = AudioDesc(outDt, (outRate > 0.0f) ? outRate : srcAdesc.sampleRate(),
                                  (outChannels > 0) ? static_cast<unsigned int>(outChannels) : srcAdesc.channels());
                adesc.metadata() = srcAdesc.metadata();
        }

        Clock::Ptr clock = _externalClock;
        if (clock.isNull()) {
                _ownedClock = Clock::Ptr::takeOwnership(new SyntheticClock());
                clock = _ownedClock;
        }

        _sync.setName(String("FrameSync"));
        _sync.setTargetFrameRate(outFps);
        _sync.setTargetAudioDesc(adesc);
        _sync.setClock(clock);
        _sync.setInputQueueCapacity(queueCap);
        _sync.reset();

        _framesPushed = 0;
        _framesPulled = 0;

        MediaDesc outDesc;
        outDesc.setFrameRate(outFps);
        for (const auto &img : mdesc.imageList()) {
                outDesc.imageList().pushToBack(img);
        }
        if (adesc.isValid()) {
                outDesc.audioList().pushToBack(adesc);
        }

        MediaIOPortGroup *group = addPortGroup("framesync");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(outFps);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        if (addSource(group, outDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error FrameSyncMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _sync.pushEndOfStream();
        _sync.interrupt();
        _sync.reset();
        _sync.setClock(Clock::Ptr());
        _framesPushed = 0;
        _framesPulled = 0;
        return Error::Ok;
}

Error FrameSyncMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("FrameSyncMediaIO: write with null frame");
                return Error::InvalidArgument;
        }
        Error err = _sync.pushFrame(cmd.frame);
        ++_framesPushed;
        cmd.currentFrame = toFrameNumber(_framesPushed);
        cmd.frameCount = _framesPushed;
        return err;
}

Error FrameSyncMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        // Non-blocking pull: the MediaIO strand runs both pushes and
        // pulls, so blocking here would deadlock against the very
        // write that would wake us.  The write-side of MediaIO emits
        // frameReadySignal on every successful push, which re-arms
        // this prefetch as soon as the producer queues a frame.
        auto result = _sync.pullFrame(/*blockOnEmpty=*/false);
        if (result.second().isError()) {
                return result.second();
        }

        const FrameSync::PullResult &pr = result.first();
        if (!pr.frame.isValid()) {
                return Error::EndOfFile;
        }

        if (pr.framesRepeated.value() > 0) noteFrameRepeated(portGroup(0));
        if (pr.framesDropped.value() > 0) noteFrameDropped(portGroup(0));

        ++_framesPulled;
        cmd.frame = pr.frame;
        cmd.currentFrame = toFrameNumber(_framesPulled);
        return Error::Ok;
}

Error FrameSyncMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesPushed, _framesPushed);
        cmd.stats.set(StatsFramesPulled, _framesPulled);
        cmd.stats.set(MediaIOStats::FramesRepeated, _sync.framesRepeated());
        cmd.stats.set(MediaIOStats::FramesDropped, _sync.framesDropped());
        return Error::Ok;
}

// ---- Phase 1 introspection / negotiation overrides ----

Error FrameSyncMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / port snapshots.
        // FrameSync accepts any MediaDesc and produces what
        // applyOutputOverrides yields against its current config.
        return MediaIO::describe(out);
}

Error FrameSyncMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) {
                // Audio-only frame: no pixel constraint, pass through.
                *preferred = offered;
                return Error::Ok;
        }
        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (!pd.isValid() || !pd.isCompressed()) {
                // Uncompressed (or unknown) — FrameSync has no opinion
                // on the shape; it just re-times whatever it receives.
                *preferred = offered;
                return Error::Ok;
        }
        // Compressed input: reject so the planner puts the decoder
        // ahead of us.  Repeating a bitstream forces the downstream
        // decoder to re-decode every held frame, and intra-only codecs
        // would outright break on any non-keyframe repeat.  Ask for a
        // same-family uncompressed substitute via the shared helper;
        // the planner's VideoDecoder bridge closes the gap in one hop.
        const PixelFormat target = MediaIO::defaultUncompressedPixelFormat(pd);
        MediaDesc         want = offered;
        ImageDesc::List  &imgs = want.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelFormat(target);
        }
        *preferred = want;
        return Error::Ok;
}

Error FrameSyncMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const {
        if (achievable == nullptr) return Error::Invalid;
        *achievable = MediaIO::applyOutputOverrides(requested, config());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
