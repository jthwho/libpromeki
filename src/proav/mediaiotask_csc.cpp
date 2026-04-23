/**
 * @file      mediaiotask_csc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask_csc.h>
#include <promeki/cscregistry.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_CSC)

namespace {

// Returns a coarse "chroma resolution rank" for sorting — bigger
// number = more chroma samples per pixel = higher fidelity.  Used to
// detect lossy chroma subsampling between two PixelMemLayouts.
//
// 4:2:0 and 4:1:1 both carry one chroma sample per four luma samples,
// but distribute it differently (4:2:0 halves both axes; 4:1:1 keeps
// full vertical but quarters horizontal).  They get distinct ranks
// so the planner doesn't treat a 4:2:0 → 4:1:1 crossover as free: it
// isn't (the chroma grid is rewritten), and NV12-style 4:2:0 is the
// far more common hardware-friendly layout, so 4:2:0 outranks 4:1:1.
int chromaRank(PixelMemLayout::Sampling s) {
        switch(s) {
                case PixelMemLayout::Sampling444: return 4;
                case PixelMemLayout::Sampling422: return 2;
                case PixelMemLayout::Sampling420: return 1;
                case PixelMemLayout::Sampling411: return 0;
                case PixelMemLayout::SamplingUndefined: default: return 4;
        }
}

// Quality cost for a CSC bridge from @p from to @p to.  Bands follow
// the unitless scale in docs/devplan/proav_pipeline_planner.md and
// FormatDesc::BridgeFunc; smaller = higher quality.
//
// Two effects combine:
//
//   - Penalties for lossy reductions (bit-depth loss, chroma down-
//     sampling) push the cost up so the planner avoids these paths
//     when a higher-quality alternative exists.
//   - A "fast-path bonus" subtracts a fixed amount when a
//     hand-tuned SIMD kernel exists in CSCRegistry, so the planner
//     prefers the conversions that run at full SIMD throughput.
//
// The bonus is capped to never make a lossy conversion cheaper than
// a same-bit-depth one — quality dominates speed by design.
int cscBridgeCost(const PixelFormat &from, const PixelFormat &to) {
        if(from == to) return 0;

        // Base cost for any CSC hop — the conversion is precision-
        // preserving when the target has at least the source's
        // bit-depth and chroma sampling.  Penalties below add to
        // this whenever a lossy reduction is implied.
        int cost = 50;

        // Bit-depth penalty.  Compare component[0] (luma / red) bits;
        // any reduction adds 100 per lost bit (e.g. 10-bit → 8-bit
        // pays 200, 12-bit → 8-bit pays 400).  The penalty is large
        // enough that the planner picks a same-depth path even if
        // the alternative needs a chroma resample.
        const int fromBits = from.memLayout().compCount() > 0
                ? static_cast<int>(from.memLayout().compDesc(0).bits) : 0;
        const int toBits   = to.memLayout().compCount() > 0
                ? static_cast<int>(to.memLayout().compDesc(0).bits)   : 0;
        if(toBits > 0 && fromBits > 0 && toBits < fromBits) {
                cost += 100 * (fromBits - toBits);
        }

        // Chroma-subsampling penalty — going from finer to coarser
        // chroma loses information (e.g. 4:4:4 → 4:2:0 pays 150).
        const int fromChroma = chromaRank(from.memLayout().sampling());
        const int toChroma   = chromaRank(to.memLayout().sampling());
        if(toChroma < fromChroma) {
                cost += 75 * (fromChroma - toChroma);
        }

        // Fast-path bonus — when a hand-tuned SIMD kernel exists for
        // this pair, knock 25 off the cost.  Cap so we never go
        // negative or cancel a quality penalty: same-bit-depth
        // colour-space hop with a fast path lands at 25 (vs 50 for
        // a generic-pipeline hop), but a lossy 10→8 hop with a fast
        // path stays well above any same-depth alternative.
        if(CSCRegistry::lookupFastPath(from, to) != nullptr) {
                cost -= 25;
                if(cost < 1) cost = 1;
        }

        return cost;
}

bool cscBridge(const MediaDesc &from,
               const MediaDesc &to,
               MediaIO::Config *outConfig,
               int *outCost) {
        // CSC bridges only the video side, and only between
        // uncompressed pixel descriptions.  Anything compressed on
        // either end goes to VideoEncoder / VideoDecoder.
        if(from.imageList().isEmpty() || to.imageList().isEmpty()) return false;
        const PixelFormat &fromPd = from.imageList()[0].pixelFormat();
        const PixelFormat &toPd   = to.imageList()[0].pixelFormat();
        if(!fromPd.isValid() || !toPd.isValid())   return false;
        if(fromPd.isCompressed() || toPd.isCompressed()) return false;

        // Source raster must already match the sink raster — CSC
        // does not scale.  Source frame rate must match too — that
        // is FrameSync's job.
        if(from.imageList()[0].size() != to.imageList()[0].size()) return false;
        if(from.frameRate() != to.frameRate()) return false;

        // Audio must already match.  CSC is pixel-only and ignores
        // the audio plane on pass-through; if audio differs the
        // planner needs a different (or compound) bridge, so decline
        // here rather than silently drop the audio mismatch.
        if(from.audioList().size() != to.audioList().size()) return false;
        const size_t audioCount = from.audioList().size();
        for(size_t i = 0; i < audioCount; ++i) {
                const AudioDesc &a = from.audioList()[i];
                const AudioDesc &b = to.audioList()[i];
                if(a.sampleRate() != b.sampleRate()) return false;
                if(a.channels()   != b.channels())   return false;
                if(a.format().id()   != b.format().id())   return false;
        }

        // No work to do — let the planner skip inserting a CSC.
        if(fromPd == toPd) return false;

        if(outConfig != nullptr) {
                *outConfig = MediaIO::defaultConfig("CSC");
                outConfig->set(MediaConfig::OutputPixelFormat, toPd);
        }
        if(outCost != nullptr) {
                *outCost = cscBridgeCost(fromPd, toPd);
        }
        return true;
}

} // namespace

MediaIO::FormatDesc MediaIOTask_CSC::formatDesc() {
        MediaIO::FormatDesc d;
        d.name              = "CSC";
        d.description       = "Color space converter (uncompressed pixel format conversion)";
        d.canBeSource       = false;
        d.canBeSink         = false;
        d.canBeTransform    = true;
        d.create            = []() -> MediaIOTask * { return new MediaIOTask_CSC(); };
        d.configSpecs       = []() -> MediaIO::Config::SpecMap {
                MediaIO::Config::SpecMap specs;
                auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        specs.insert(id, gs ? VariantSpec(*gs).setDefault(def)
                                            : VariantSpec().setDefault(def));
                };
                s(MediaConfig::OutputPixelFormat, PixelFormat());
                s(MediaConfig::Capacity, int32_t(4));
                return specs;
        };
        d.bridge            = cscBridge;
        return d;
}

MediaIOTask_CSC::~MediaIOTask_CSC() = default;

Error MediaIOTask_CSC::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Transform) {
                promekiErr("MediaIOTask_CSC: only InputAndOutput mode is supported");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        _outputPixelFormat = cfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat, PixelFormat());
        _outputPixelFormatSet = _outputPixelFormat.isValid();

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if(_capacity < 1) _capacity = 1;

        MediaDesc outDesc;
        outDesc.setFrameRate(cmd.pendingMediaDesc.frameRate());

        for(const auto &srcImg : cmd.pendingMediaDesc.imageList()) {
                ImageDesc id = srcImg;
                if(_outputPixelFormatSet) id = ImageDesc(srcImg.size(), _outputPixelFormat);
                outDesc.imageList().pushToBack(id);
        }

        for(const auto &srcAudio : cmd.pendingMediaDesc.audioList()) {
                outDesc.audioList().pushToBack(srcAudio);
        }

        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _outputQueue.clear();

        cmd.mediaDesc = outDesc;
        if(!outDesc.audioList().isEmpty()) cmd.audioDesc = outDesc.audioList()[0];
        cmd.metadata = cmd.pendingMetadata;
        cmd.frameRate = outDesc.frameRate();
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.defaultStep = 1;
        cmd.defaultPrefetchDepth = 1;
        cmd.defaultWriteDepth = _capacity;
        return Error::Ok;
}

Error MediaIOTask_CSC::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _outputQueue.clear();
        _outputPixelFormat = PixelFormat();
        _outputPixelFormatSet = false;
        _frameCount = 0;
        _readCount = 0;
        _framesConverted = 0;
        _capacityWarned = false;
        return Error::Ok;
}

Error MediaIOTask_CSC::convertImage(const Image &input, Image &output) {
        if(!input.isValid()) {
                output = Image();
                return Error::Invalid;
        }

        if(!_outputPixelFormatSet) {
                output = input;
                return Error::Ok;
        }

        if(input.pixelFormat() == _outputPixelFormat) {
                output = input;
                return Error::Ok;
        }

        if(input.pixelFormat().isCompressed() || _outputPixelFormat.isCompressed()) {
                promekiErr("MediaIOTask_CSC: %s -> %s is a compression hop; "
                           "use MediaIOTask_VideoEncoder / MediaIOTask_VideoDecoder",
                           input.pixelFormat().name().cstr(),
                           _outputPixelFormat.name().cstr());
                return Error::NotSupported;
        }

        MediaConfig convertConfig;
        Image converted = input.convert(_outputPixelFormat, input.metadata(),
                                        convertConfig);
        if(!converted.isValid()) {
                promekiErr("MediaIOTask_CSC: convert %s -> %s failed",
                           input.pixelFormat().name().cstr(),
                           _outputPixelFormat.name().cstr());
                return Error::ConversionFailed;
        }
        output = std::move(converted);
        return Error::Ok;
}

Error MediaIOTask_CSC::convertFrame(const Frame::Ptr &input, Frame::Ptr &output) {
        if(!input.isValid()) {
                return Error::Invalid;
        }

        Frame::Ptr outFrame = Frame::Ptr::create();
        Frame *outRaw = outFrame.modify();
        outRaw->metadata() = input->metadata();

        for(const auto &srcImgPtr : input->imageList()) {
                if(!srcImgPtr.isValid()) continue;
                const Image &srcImg = *srcImgPtr;
                Image dstImg;
                Error err = convertImage(srcImg, dstImg);
                if(err.isError()) return err;
                outRaw->imageList().pushToBack(Image::Ptr::create(std::move(dstImg)));
        }

        for(const auto &srcAudioPtr : input->audioList()) {
                outRaw->audioList().pushToBack(srcAudioPtr);
        }

        output = std::move(outFrame);
        return Error::Ok;
}

Error MediaIOTask_CSC::executeCmd(MediaIOCommandWrite &cmd) {
        if(!cmd.frame.isValid()) {
                promekiErr("MediaIOTask_CSC: write with null frame");
                return Error::InvalidArgument;
        }
        stampWorkBegin();

        if(static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("MediaIOTask_CSC: output queue exceeded capacity (%d >= %d)",
                        static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame::Ptr outFrame;
        Error err = convertFrame(cmd.frame, outFrame);
        if(err.isError()) { stampWorkEnd(); return err; }

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        _framesConverted++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_CSC::executeCmd(MediaIOCommandRead &cmd) {
        if(_outputQueue.isEmpty()) {
                return Error::TryAgain;
        }

        Frame::Ptr frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error MediaIOTask_CSC::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesConverted, _framesConverted);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int MediaIOTask_CSC::pendingOutput() const {
        return static_cast<int>(_outputQueue.size());
}

// ---- Phase 1 introspection / negotiation overrides ----

Error MediaIOTask_CSC::describe(MediaIODescription *out) const {
        if(out == nullptr) return Error::Invalid;

        // CSC accepts any uncompressed video MediaDesc as input.
        // The acceptable list stays empty as a "no constraint"
        // signal; consumers walk producibleFormats for the
        // configured target pixel desc when one is set.

        if(_outputPixelFormatSet) {
                MediaDesc preferred;
                preferred.imageList().pushToBack(
                        ImageDesc(Size2Du32(0, 0), _outputPixelFormat));
                out->setPreferredFormat(preferred);
                out->producibleFormats().pushToBack(preferred);
        }
        return Error::Ok;
}

Error MediaIOTask_CSC::proposeInput(const MediaDesc &offered,
                                    MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;

        // CSC handles uncompressed video only; reject anything
        // compressed.  The planner will route compressed inputs
        // through a VideoDecoder bridge instead.
        for(const auto &img : offered.imageList()) {
                if(img.pixelFormat().isCompressed()) {
                        return Error::NotSupported;
                }
        }

        // CSC accepts any uncompressed shape as-is — it will run
        // the configured Image::convert on each frame.  The output
        // shape is governed by OutputPixelFormat, not the input.
        *preferred = offered;
        return Error::Ok;
}

Error MediaIOTask_CSC::proposeOutput(const MediaDesc &requested,
                                     MediaDesc *achievable) const {
        if(achievable == nullptr) return Error::Invalid;

        // CSC's output shape is fully determined by the input shape
        // plus its OutputPixelFormat config (and any other Output*
        // overrides the planner may have set).  Whatever the
        // planner asks for, what we'll actually produce is what
        // applyOutputOverrides yields — relay that back.
        const MediaIO *io = mediaIo();
        const MediaConfig &cfg = (io != nullptr) ? io->config() : MediaConfig();
        *achievable = applyOutputOverrides(requested, cfg);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
