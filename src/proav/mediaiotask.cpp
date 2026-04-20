/**
 * @file      mediaiotask.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiotask.h>

#include <promeki/audiodesc.h>
#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/enums.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/pixeldesc.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOTask::~MediaIOTask() = default;

Error MediaIOTask::executeCmd(MediaIOCommandOpen &cmd) {
        return Error::NotImplemented;
}

Error MediaIOTask::executeCmd(MediaIOCommandClose &cmd) {
        return Error::Ok;
}

Error MediaIOTask::executeCmd(MediaIOCommandRead &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandWrite &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandSeek &cmd) {
        return Error::IllegalSeek;
}

Error MediaIOTask::executeCmd(MediaIOCommandParams &cmd) {
        return Error::NotSupported;
}

Error MediaIOTask::executeCmd(MediaIOCommandStats &cmd) {
        return Error::Ok;
}

void MediaIOTask::configChanged(const MediaConfig &delta) {
        (void)delta;
}

void MediaIOTask::cancelBlockingWork() {
        // No-op for backends whose executeCmd is strictly
        // non-blocking.  Backends that may block inside executeCmd
        // override this to signal their wait loops to unwind.
}

int MediaIOTask::pendingOutput() const {
        return 0;
}

Error MediaIOTask::describe(MediaIODescription *out) const {
        // Default: leave the description as MediaIO populated it.
        // Backends override to fill in producibleFormats /
        // acceptableFormats / preferredFormat / capability fields
        // probed from the underlying resource.
        (void)out;
        return Error::Ok;
}

Error MediaIOTask::proposeInput(const MediaDesc &offered,
                                MediaDesc *preferred) const {
        // Default: accept whatever is offered (transparent
        // passthrough).  Sinks and transforms with format constraints
        // override to either narrow or refuse.
        if(preferred != nullptr) *preferred = offered;
        return Error::Ok;
}

Error MediaIOTask::proposeOutput(const MediaDesc &requested,
                                 MediaDesc *achievable) const {
        // Default: most sources produce what they produce — flag
        // NotSupported and let the planner bridge from the current
        // output.  Configurable sources (TPG, V4L2) override.
        (void)requested;
        if(achievable != nullptr) *achievable = MediaDesc();
        return Error::NotSupported;
}

PixelDesc MediaIOTask::defaultUncompressedPixelDesc(const PixelDesc &source) {
        // Both fallbacks are registered in the PixelDesc well-known
        // table and carry paint engines, so the planner can splice a
        // cheap one-hop CSC between the source and us.  A YCbCr source
        // stays in the YUV family to minimise that CSC cost.
        const bool isYuv = source.isValid()
                && source.colorModel().type() == ColorModel::TypeYCbCr;
        return isYuv
                ? PixelDesc(PixelDesc::YUV8_422_Rec709)
                : PixelDesc(PixelDesc::RGBA8_sRGB);
}

MediaDesc MediaIOTask::applyOutputOverrides(const MediaDesc &input,
                                            const MediaConfig &config) {
        MediaDesc out = input;

        // ---- Video: OutputPixelDesc ----
        // A valid PixelDesc replaces the pixel format on every image
        // layer.  An invalid (default-constructed) PixelDesc means
        // "inherit from input" — leave the per-image pixelDesc alone.
        if(config.contains(MediaConfig::OutputPixelDesc)) {
                const PixelDesc target = config.getAs<PixelDesc>(MediaConfig::OutputPixelDesc);
                if(target.isValid()) {
                        ImageDesc::List &imgs = out.imageList();
                        for(size_t i = 0; i < imgs.size(); ++i) {
                                imgs[i].setPixelDesc(target);
                        }
                }
        }

        // ---- Video: OutputFrameRate ----
        if(config.contains(MediaConfig::OutputFrameRate)) {
                const FrameRate fr = config.getAs<FrameRate>(MediaConfig::OutputFrameRate);
                if(fr.isValid()) out.setFrameRate(fr);
        }

        // ---- Audio: OutputAudioRate (Hz) ----
        // Zero (default) means "inherit from input".
        if(config.contains(MediaConfig::OutputAudioRate)) {
                const float hz = config.getAs<float>(MediaConfig::OutputAudioRate);
                if(hz > 0.0f) {
                        AudioDesc::List &auds = out.audioList();
                        for(size_t i = 0; i < auds.size(); ++i) {
                                auds[i].setSampleRate(hz);
                        }
                }
        }

        // ---- Audio: OutputAudioChannels ----
        // Zero (default) means "inherit from input".
        if(config.contains(MediaConfig::OutputAudioChannels)) {
                const int ch = config.getAs<int>(MediaConfig::OutputAudioChannels);
                if(ch > 0) {
                        AudioDesc::List &auds = out.audioList();
                        for(size_t i = 0; i < auds.size(); ++i) {
                                auds[i].setChannels(static_cast<unsigned int>(ch));
                        }
                }
        }

        // ---- Audio: OutputAudioDataType ----
        // Invalid (default) means "inherit from input".  The key is
        // typed as a TypeEnum bound to AudioDataType::Type so the
        // value lives as an Enum and we project it back through the
        // AudioDataType wrapper to get the corresponding
        // AudioDesc::DataType.
        if(config.contains(MediaConfig::OutputAudioDataType)) {
                Error enumErr;
                Enum adtEnum = config.get(MediaConfig::OutputAudioDataType)
                        .asEnum(AudioDataType::Type, &enumErr);
                if(enumErr.isOk()) {
                        const auto dt = static_cast<AudioDesc::DataType>(adtEnum.value());
                        if(dt != AudioDesc::Invalid) {
                                AudioDesc::List &auds = out.audioList();
                                for(size_t i = 0; i < auds.size(); ++i) {
                                        auds[i].setDataType(dt);
                                }
                        }
                }
        }

        return out;
}

// ---- Live-telemetry helper forwarders ----
//
// All three forward into the owning MediaIO's per-instance counters.
// MediaIOTask is a friend of MediaIO (declared in mediaio.h), so the
// private atomic fields are accessible here.  Each helper guards
// against a null owner so tasks constructed in isolation (e.g. unit
// tests) don't crash if they invoke the helpers before being adopted.

void MediaIOTask::noteFrameDropped() {
        if(_owner == nullptr) return;
        _owner->_framesDroppedTotal.fetchAndAdd(1);
}

void MediaIOTask::noteFrameRepeated() {
        if(_owner == nullptr) return;
        _owner->_framesRepeatedTotal.fetchAndAdd(1);
}

void MediaIOTask::noteFrameLate() {
        if(_owner == nullptr) return;
        _owner->_framesLateTotal.fetchAndAdd(1);
}

// ---- Benchmark stamp helpers ----
//
// These let backends bracket the real per-frame processing work
// inside executeCmd(Read/Write) so the framework can report
// processing time separately from end-to-end latency (which
// includes queue wait and pacing).  The infrastructure sets
// _activeBenchmark before calling executeCmd and clears it after,
// so the helpers work identically for both reads and writes.  The
// stamp IDs live on the owning MediaIO; friendship gives us access.

void MediaIOTask::stampWorkBegin() {
        if(_activeBenchmark == nullptr) return;
        if(_owner == nullptr || !_owner->_benchmarkEnabled) return;
        _activeBenchmark->stamp(_owner->_idStampWorkBegin);
}

void MediaIOTask::stampWorkEnd() {
        if(_activeBenchmark == nullptr) return;
        if(_owner == nullptr || !_owner->_benchmarkEnabled) return;
        _activeBenchmark->stamp(_owner->_idStampWorkEnd);
}

PROMEKI_NAMESPACE_END
