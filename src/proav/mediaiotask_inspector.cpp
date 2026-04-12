/**
 * @file      mediaiotask_inspector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <promeki/mediaiotask_inspector.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/ltcdecoder.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_Inspector)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

double monotonicWallSeconds() {
        using namespace std::chrono;
        const auto now = steady_clock::now().time_since_epoch();
        return duration_cast<duration<double>>(now).count();
}

String renderTc(const Timecode &tc) {
        // Timecode::toString handles every case the inspector cares
        // about: invalid → "--:--:--:--", valid digits without a frame
        // rate → "HH:MM:SS:FF" digits-only, valid digits with a rate →
        // libvtc-formatted output.  No fallback path needed.
        return tc.toString().first();
}

}  // namespace

// ---------------------------------------------------------------------------
// MediaIO factory descriptor
// ---------------------------------------------------------------------------

MediaIO::FormatDesc MediaIOTask_Inspector::formatDesc() {
        return {
                "Inspector",
                "Inspect / validate frames flowing through a pipeline (sink-only).",
                {},     // No file extensions — pure sink.
                false,  // canOutput
                true,   // canInput
                false,  // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_Inspector();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def)
                                                    : VariantSpec().setDefault(def));
                        };
                        // Inspector defaults run *full* checks out of
                        // the box: image data decode, LTC decode, A/V
                        // sync offset, and continuity all on.  This
                        // pairs with the TPG's default AvSync audio
                        // mode (which now embeds LTC alongside the
                        // click marker) so an inspector + default-
                        // configured TPG end-to-end pair validates
                        // everything the TPG produces with no extra
                        // configuration.
                        s(MediaConfig::InspectorDropFrames,           true);
                        s(MediaConfig::InspectorDecodeImageData,      true);
                        s(MediaConfig::InspectorDecodeLtc,            true);
                        s(MediaConfig::InspectorCheckTcSync,          true);
                        s(MediaConfig::InspectorCheckContinuity,      true);
                        s(MediaConfig::InspectorImageDataRepeatLines, int32_t(16));
                        s(MediaConfig::InspectorLtcChannel,           int32_t(0));
                        s(MediaConfig::InspectorSyncOffsetToleranceSamples, int32_t(0));
                        s(MediaConfig::InspectorLogIntervalSec,       1.0);
                        return specs;
                },
                []() -> Metadata { return Metadata(); }
        };
}

// ---------------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------------

MediaIOTask_Inspector::MediaIOTask_Inspector() = default;
MediaIOTask_Inspector::~MediaIOTask_Inspector() = default;

void MediaIOTask_Inspector::setEventCallback(EventCallback cb) {
        // No locking — must be called before open() per the API
        // contract.  Documenting that contract is cheaper than the
        // mutex contention on every frame.
        _callback = std::move(cb);
}

InspectorSnapshot MediaIOTask_Inspector::snapshot() const {
        Mutex::Locker lk(_stateMutex);
        return _stats;
}

void MediaIOTask_Inspector::resetState() {
        Mutex::Locker lk(_stateMutex);
        _stats = InspectorSnapshot{};
        _hasPreviousPicture = false;
        _hasPreviousLtc     = false;
        _previousFrameNumber = 0;
        _previousStreamId    = 0;
        _previousPictureTc   = Timecode();
        _previousLtcTc       = Timecode();
        _inferredPictureMode = Timecode::Mode();
        _hasPreviousSyncOffset = false;
        _previousSyncOffset    = 0;
        _samplesPerFrame       = 0.0;
        _frameIndex          = 0;
        _decodersInitialized = false;
        _imageDataDecoder    = ImageDataDecoder();
        _ltcDecoder.reset();
        _ltcCumulativeSamples = 0;
        _lastLogWallSec      = monotonicWallSeconds();
        _framesSinceLastLog  = 0;
}

Error MediaIOTask_Inspector::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Input) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        _decodeImageData      = cfg.getAs<bool>(MediaConfig::InspectorDecodeImageData,  false);
        _decodeLtc            = cfg.getAs<bool>(MediaConfig::InspectorDecodeLtc,         false);
        _checkTcSync          = cfg.getAs<bool>(MediaConfig::InspectorCheckTcSync,       false);
        _checkContinuity      = cfg.getAs<bool>(MediaConfig::InspectorCheckContinuity,   false);
        _dropFrames           = cfg.getAs<bool>(MediaConfig::InspectorDropFrames,        true);
        _imageDataRepeatLines = cfg.getAs<int>(MediaConfig::InspectorImageDataRepeatLines, 16);
        _ltcChannel           = cfg.getAs<int>(MediaConfig::InspectorLtcChannel,         0);
        _syncOffsetToleranceSamples =
                cfg.getAs<int>(MediaConfig::InspectorSyncOffsetToleranceSamples, 0);
        _logIntervalSec       = cfg.getAs<double>(MediaConfig::InspectorLogIntervalSec,  1.0);

        // Auto-resolve dependencies.  TC sync needs both decoders;
        // continuity needs the picture decoder.
        if(_checkTcSync) {
                _decodeImageData = true;
                _decodeLtc       = true;
        }
        if(_checkContinuity) {
                _decodeImageData = true;
        }

        resetState();
        _isOpen = true;

        // Dump the resolved configuration into the log so any later
        // periodic reports can be interpreted in context — especially
        // useful when post-mortem'ing a recorded log file.  Each line
        // shares the "config:" prefix so the whole block is scannable.
        promekiInfo("config: image data decode  = %s",
                    _decodeImageData ? "enabled" : "disabled");
        promekiInfo("config: LTC decode         = %s%s",
                    _decodeLtc ? "enabled" : "disabled",
                    _decodeLtc ? "" : " (no audio LTC checks will run)");
        if(_decodeLtc) {
                promekiInfo("config: LTC channel        = %d", _ltcChannel);
        }
        promekiInfo("config: A/V sync check     = %s%s",
                    _checkTcSync ? "enabled" : "disabled",
                    _checkTcSync ? " (auto-enables image data + LTC decode)" : "");
        if(_checkTcSync) {
                promekiInfo("config: A/V sync jitter tolerance = %d sample%s "
                            "(any frame-to-frame change beyond this fires a "
                            "discontinuity warning; %s)",
                            _syncOffsetToleranceSamples,
                            _syncOffsetToleranceSamples == 1 ? "" : "s",
                            _syncOffsetToleranceSamples == 0
                                    ? "default = report any change at all"
                                    : "set to 0 to report any change");
        }
        promekiInfo("config: continuity check    = %s%s",
                    _checkContinuity ? "enabled" : "disabled",
                    _checkContinuity ? " (auto-enables image data decode)" : "");
        if(_decodeImageData) {
                promekiInfo("config: image data band    = %d scan lines per item, "
                            "TPG-convention 2 items at top of frame",
                            _imageDataRepeatLines);
        }
        promekiInfo("config: drop frames         = %s",
                    _dropFrames ? "yes (sink behaviour)" : "no");
        if(_logIntervalSec > 0.0) {
                promekiInfo("config: periodic log every  = %.2f seconds (wall time)",
                            _logIntervalSec);
        } else {
                promekiInfo("config: periodic log        = disabled");
        }

        // We don't fill in mediaDesc / audioDesc / frameRate /
        // frameCount — the inspector is a sink, the upstream stage
        // owns the descriptor.  We do, however, advertise no seek
        // and an unbounded frame count.
        cmd.canSeek    = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_Inspector::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _isOpen = false;
        // Stats survive close so callers can retrieve final counts;
        // resetState happens on the next open.
        return Error::Ok;
}

void MediaIOTask_Inspector::initDecoders(const Frame &frame) {
        if(_decodersInitialized) return;

        // Picture decoder needs a valid image to learn the descriptor.
        if(_decodeImageData) {
                if(!frame.imageList().isEmpty()) {
                        const Image::Ptr &imgPtr = frame.imageList()[0];
                        if(imgPtr.isValid()) {
                                _imageDataDecoder = ImageDataDecoder(imgPtr->desc());
                                if(!_imageDataDecoder.isValid()) {
                                        promekiWarn("MediaIOTask_Inspector: image data decoder "
                                                    "could not be initialised for %s",
                                                    imgPtr->desc().toString().cstr());
                                }
                        }
                }
        }

        // LTC decoder needs the audio sample rate.
        if(_decodeLtc) {
                if(!frame.audioList().isEmpty()) {
                        const Audio::Ptr &aud = frame.audioList()[0];
                        if(aud.isValid()) {
                                const int rate = static_cast<int>(aud->desc().sampleRate());
                                if(rate > 0) {
                                        _ltcDecoder = std::make_unique<LtcDecoder>(rate);
                                }
                        }
                }
        }

        _decodersInitialized = true;
}

// ---------------------------------------------------------------------------
// Per-frame check execution
// ---------------------------------------------------------------------------

void MediaIOTask_Inspector::runImageDataCheck(const Frame &frame, InspectorEvent &event) {
        event.pictureDecoderEnabled = true;

        // The function maintains an all-or-nothing contract for the
        // picture-side fields: when @ref InspectorEvent::pictureDecoded
        // is @c false, @c pictureFrameNumber / @c pictureStreamId /
        // @c pictureTimecode are guaranteed to be at their default
        // values.  This prevents a callback consumer who forgets to
        // check @c pictureDecoded from confusing a stale or
        // partially-decoded reading with a real "frame 0, stream 0,
        // TC 00:00:00:00" frame.  We achieve that by only populating
        // the fields after both bands have successfully decoded —
        // any early return below leaves the event's defaults intact.

        if(!_imageDataDecoder.isValid()) return;
        if(frame.imageList().isEmpty()) return;
        const Image::Ptr &imgPtr = frame.imageList()[0];
        if(!imgPtr.isValid()) return;

        // Two TPG-convention bands: frame ID at lines [0, N), timecode
        // at lines [N, 2N), where N = _imageDataRepeatLines.
        List<ImageDataDecoder::Band> bands;
        bands.pushToBack({ 0,
                           static_cast<uint32_t>(_imageDataRepeatLines) });
        bands.pushToBack({ static_cast<uint32_t>(_imageDataRepeatLines),
                           static_cast<uint32_t>(_imageDataRepeatLines) });

        ImageDataDecoder::DecodedList items;
        Error err = _imageDataDecoder.decode(*imgPtr, bands, items);
        if(err.isError() || items.size() != 2) return;
        if(items[0].error.isError() || items[1].error.isError()) return;

        // The picture timecode mode is unknown to the decoder (the
        // wire format only carries digits + flags), so we unpack with
        // the inspector-side default Vitc mode and an unknown Mode —
        // the resulting Timecode will report 29.97 DF if the DF flag
        // was set, or carry digits-only otherwise.  A failure here
        // (which would be unusual — the BCD unpack only fails on a
        // DF/rate inconsistency) drops us into the same all-or-nothing
        // bail-out path so partial fields don't leak out.
        auto rt = Timecode::fromBcd64(items[1].payload);
        if(rt.second().isError()) return;

        const uint64_t frameId = items[0].payload;
        event.pictureFrameNumber = static_cast<uint32_t>(frameId & 0xffffffffu);
        event.pictureStreamId    = static_cast<uint32_t>(frameId >> 32);
        event.pictureTimecode    = rt.first();
        event.pictureDecoded     = true;
}

void MediaIOTask_Inspector::runLtcCheck(const Frame &frame, InspectorEvent &event) {
        event.ltcDecoderEnabled = true;
        if(!_ltcDecoder) return;
        if(frame.audioList().isEmpty()) return;
        const Audio::Ptr &aud = frame.audioList()[0];
        if(!aud.isValid()) return;

        // Capture the cumulative-sample anchor that marks the start of
        // *this* audio chunk before we feed it to the decoder, so we
        // can convert the decoder's absolute sampleStart values into
        // within-chunk offsets afterwards.  LtcDecoder accepts the
        // raw Audio in any format and pulls the named channel itself,
        // so the inspector no longer has to do its own demux + int8
        // conversion.
        const int64_t chunkStartAbs = _ltcCumulativeSamples;
        auto results = _ltcDecoder->decode(*aud, _ltcChannel);
        _ltcCumulativeSamples += static_cast<int64_t>(aud->samples());

        if(results.isEmpty()) return;

        // Multiple LTC frames may decode within one audio chunk if the
        // upstream FPS straddles LTC frame boundaries — we keep the
        // last one for the per-frame report.  The discontinuity check
        // (which fires elsewhere) only sees the most recent.
        const auto &last = results[results.size() - 1];
        event.ltcDecoded     = true;
        event.ltcTimecode    = last.timecode;
        // Convert the decoder's absolute sample position into a
        // within-chunk offset.  May be negative if libvtc's state
        // machine emitted an LTC frame whose sync word was buffered
        // from a previous chunk — that's fine, the drift formula
        // handles signed offsets.
        event.ltcSampleStart = last.sampleStart - chunkStartAbs;

        // Latch the LTC's frame-rate mode the first time we see one,
        // so the picture-TC continuity check has a rate to work with.
        // The picture data band only carries digits + the DF flag and
        // therefore arrives without a Mode, which makes Timecode's
        // operator++ a no-op — without an attached mode the "expected
        // next TC" stays equal to the previous, and every frame would
        // false-positive a discontinuity.
        if(!_inferredPictureMode.hasFormat() &&
           last.timecode.mode().vtcFormat() != nullptr) {
                _inferredPictureMode = last.timecode.mode();
        }
}

void MediaIOTask_Inspector::runAvSyncCheck(const Frame &frame, InspectorEvent &event) {
        if(!_checkTcSync) return;
        if(!event.pictureDecoded || !event.ltcDecoded) return;
        if(!event.ltcTimecode.isValid()) return;
        if(frame.audioList().isEmpty()) return;
        const Audio::Ptr &aud = frame.audioList()[0];
        if(!aud.isValid()) return;
        const int64_t sampleRate = static_cast<int64_t>(aud->desc().sampleRate());
        if(sampleRate <= 0) return;

        // The picture-side BCD wire format only carries digits + the
        // DF flag, so the picture timecode comes back from the decoder
        // with no frame rate information.  The LTC side, on the other
        // hand, was decoded by libvtc with a real format pointer (or
        // 29.97 inferred from the DF flag).  Borrow LTC's mode for
        // the picture so both can be converted to absolute frame
        // numbers in the same coordinate system.
        Timecode pictureTc = event.pictureTimecode;
        const VtcFormat *ltcVtc = event.ltcTimecode.mode().vtcFormat();
        if(ltcVtc != nullptr) {
                pictureTc.setMode(event.ltcTimecode.mode());
        }
        if(ltcVtc == nullptr) return;

        auto picFrames = pictureTc.toFrameNumber();
        auto ltcFrames = event.ltcTimecode.toFrameNumber();
        if(picFrames.second().isError() || ltcFrames.second().isError()) return;

        // Average samples-per-frame from the LTC format's exact
        // rational rate.  For integer rates (24, 25, 30, 60, …) this
        // is an exact integer; for NTSC fractional rates (29.97,
        // 59.94, 23.98, …) it's a non-integer average — the LTC
        // encoder alternates between adjacent integer per-frame
        // sample counts (e.g. 1601 / 1602 / 1601 / 1602 / 1602 …)
        // to track the rational rate, but the long-run average is
        // exact at sampleRate * den / num.  Doubles give us plenty
        // of precision for converting samples ↔ fractional frames
        // in the periodic log.
        if(ltcVtc->rate.num <= 0 || ltcVtc->rate.den <= 0) return;
        const double spf = static_cast<double>(sampleRate) *
                           static_cast<double>(ltcVtc->rate.den) /
                           static_cast<double>(ltcVtc->rate.num);
        if(spf <= 0.0) return;
        _samplesPerFrame = spf;

        // Sign convention: positive = video leads audio (the
        // picture's TC anchor lands at an earlier wall-clock audio
        // sample than the LTC's TC anchor).
        //
        // Derivation: picture's wall-clock time at TC X is
        //   tWall_pic(X) = chunkStart + (X - picTc) * spf
        // LTC's wall-clock time at TC X is
        //   tWall_ltc(X) = ltcSyncAbs + (X - ltcTc) * spf
        // offset = tWall_ltc(X) - tWall_pic(X)
        //        = (ltcSyncAbs - chunkStart) + (picTc - ltcTc) * spf
        //        = ltcSampleStart           + (picFrames - ltcFrames) * spf
        //
        // ltcSampleStart is the within-chunk offset captured in
        // runLtcCheck above.  This is an *instantaneous* offset
        // measurement on the current frame — it is not a delta
        // against a previous frame, so a constant non-zero value
        // indicates a fixed phase relationship rather than drift.
        const int64_t tcOffsetFrames =
                static_cast<int64_t>(picFrames.first()) -
                static_cast<int64_t>(ltcFrames.first());
        const double offsetSamples =
                static_cast<double>(event.ltcSampleStart) +
                static_cast<double>(tcOffsetFrames) * spf;
        event.avSyncOffsetSamples = static_cast<int64_t>(std::llround(offsetSamples));
        event.avSyncValid         = true;

        // In professional video the audio and video are locked to the
        // same reference, so once the A/V sync offset has settled it
        // must not move from one frame to the next.  Any movement
        // beyond the configured tolerance is a real fault — even one
        // sample — and we surface it as a discontinuity so the
        // standard immediate-warning path picks it up.  The tolerance
        // defaults to 0 to enforce strict lock; QA workflows that
        // know their pipeline has bounded jitter can raise it.
        if(_hasPreviousSyncOffset) {
                const int64_t delta = event.avSyncOffsetSamples - _previousSyncOffset;
                const int64_t absDelta = delta < 0 ? -delta : delta;
                if(absDelta > _syncOffsetToleranceSamples) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::SyncOffsetChange;
                        char prev[32];
                        char curr[32];
                        std::snprintf(prev, sizeof(prev), "%+lld",
                                      static_cast<long long>(_previousSyncOffset));
                        std::snprintf(curr, sizeof(curr), "%+lld",
                                      static_cast<long long>(event.avSyncOffsetSamples));
                        d.previousValue = String(prev);
                        d.currentValue  = String(curr);
                        char desc[240];
                        std::snprintf(desc, sizeof(desc),
                                      "A/V sync offset moved: was %+lld samples, "
                                      "now %+lld samples (delta %+lld, tolerance %d) — "
                                      "audio and video are no longer locked",
                                      static_cast<long long>(_previousSyncOffset),
                                      static_cast<long long>(event.avSyncOffsetSamples),
                                      static_cast<long long>(delta),
                                      _syncOffsetToleranceSamples);
                        d.description = String(desc);
                        event.discontinuities.pushToBack(d);
                }
        }
        _previousSyncOffset    = event.avSyncOffsetSamples;
        _hasPreviousSyncOffset = true;
}

void MediaIOTask_Inspector::runContinuityCheck(InspectorEvent &event) {
        if(!_checkContinuity) return;
        if(!event.pictureDecoded) {
                if(_hasPreviousPicture) {
                        InspectorDiscontinuity d;
                        d.kind          = InspectorDiscontinuity::ImageDataDecodeFailure;
                        d.previousValue = String("decoded");
                        d.currentValue  = String("undecoded");
                        d.description   = String("Picture data band decode failed after a previously-successful frame");
                        event.discontinuities.pushToBack(d);
                }
                return;
        }

        if(_hasPreviousPicture) {
                if(event.pictureStreamId != _previousStreamId) {
                        InspectorDiscontinuity d;
                        d.kind          = InspectorDiscontinuity::StreamIdChange;
                        char prev[32];
                        char curr[32];
                        std::snprintf(prev, sizeof(prev), "0x%08x", _previousStreamId);
                        std::snprintf(curr, sizeof(curr), "0x%08x", event.pictureStreamId);
                        d.previousValue = String(prev);
                        d.currentValue  = String(curr);
                        char desc[128];
                        std::snprintf(desc, sizeof(desc),
                                      "Stream ID changed: was %s, now %s", prev, curr);
                        d.description = String(desc);
                        event.discontinuities.pushToBack(d);
                }

                const uint32_t expectedNext = _previousFrameNumber + 1;
                if(event.pictureFrameNumber != expectedNext) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::FrameNumberJump;
                        char prev[32];
                        char curr[32];
                        std::snprintf(prev, sizeof(prev), "%u", _previousFrameNumber);
                        std::snprintf(curr, sizeof(curr), "%u", event.pictureFrameNumber);
                        d.previousValue = String(prev);
                        d.currentValue  = String(curr);
                        const long long deltaFromExpected =
                                static_cast<long long>(event.pictureFrameNumber) -
                                static_cast<long long>(expectedNext);
                        char desc[200];
                        std::snprintf(desc, sizeof(desc),
                                      "Frame number jumped: was %u (expected %u next), got %u "
                                      "(%+lld frame%s relative to expected)",
                                      _previousFrameNumber, expectedNext, event.pictureFrameNumber,
                                      deltaFromExpected,
                                      deltaFromExpected == 1 || deltaFromExpected == -1 ? "" : "s");
                        d.description = String(desc);
                        event.discontinuities.pushToBack(d);
                }

                // Picture TC continuity needs a frame rate to compute
                // "expected next TC" — without one Timecode::operator++
                // is a no-op and every frame would false-positive.
                // We splice in the inferred mode (latched from LTC if
                // available) on both sides so the comparison runs in
                // a single coordinate system, and skip the check
                // entirely until we have a mode.
                if(event.pictureTimecode.isValid() && _previousPictureTc.isValid() &&
                   _inferredPictureMode.hasFormat()) {
                        Timecode prevWithMode = _previousPictureTc;
                        Timecode currWithMode = event.pictureTimecode;
                        prevWithMode.setMode(_inferredPictureMode);
                        currWithMode.setMode(_inferredPictureMode);
                        Timecode expectedTc = prevWithMode;
                        ++expectedTc;
                        if(currWithMode != expectedTc) {
                                InspectorDiscontinuity d;
                                d.kind          = InspectorDiscontinuity::PictureTcJump;
                                d.previousValue = renderTc(prevWithMode);
                                d.currentValue  = renderTc(currWithMode);
                                char desc[200];
                                std::snprintf(desc, sizeof(desc),
                                              "Picture TC jumped: was %s (expected %s next), got %s",
                                              d.previousValue.cstr(),
                                              renderTc(expectedTc).cstr(),
                                              d.currentValue.cstr());
                                d.description = String(desc);
                                event.discontinuities.pushToBack(d);
                        }
                }
        }

        _hasPreviousPicture  = true;
        _previousFrameNumber = event.pictureFrameNumber;
        _previousStreamId    = event.pictureStreamId;
        _previousPictureTc   = event.pictureTimecode;

        // LTC continuity — only meaningful if both decoders are running.
        if(event.ltcDecoded && event.ltcTimecode.isValid()) {
                if(_hasPreviousLtc && _previousLtcTc.isValid()) {
                        Timecode expectedLtc = _previousLtcTc;
                        ++expectedLtc;
                        if(event.ltcTimecode != expectedLtc) {
                                InspectorDiscontinuity d;
                                d.kind          = InspectorDiscontinuity::LtcTcJump;
                                d.previousValue = renderTc(_previousLtcTc);
                                d.currentValue  = renderTc(event.ltcTimecode);
                                char desc[200];
                                std::snprintf(desc, sizeof(desc),
                                              "LTC jumped: was %s (expected %s next), got %s",
                                              d.previousValue.cstr(),
                                              renderTc(expectedLtc).cstr(),
                                              d.currentValue.cstr());
                                d.description = String(desc);
                                event.discontinuities.pushToBack(d);
                        }
                }
                _hasPreviousLtc = true;
                _previousLtcTc  = event.ltcTimecode;
        } else if(_hasPreviousLtc) {
                InspectorDiscontinuity d;
                d.kind          = InspectorDiscontinuity::LtcDecodeFailure;
                d.previousValue = renderTc(_previousLtcTc);
                d.currentValue  = String("--:--:--:--");
                d.description   = String("LTC decode failed after a previously-successful frame");
                event.discontinuities.pushToBack(d);
        }
}

void MediaIOTask_Inspector::emitPeriodicLogIfDue() {
        if(_logIntervalSec <= 0.0) return;

        const double now = monotonicWallSeconds();
        const double elapsed = now - _lastLogWallSec;
        if(elapsed < _logIntervalSec) return;

        InspectorSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }

        // Multi-line periodic report.  Every line shares the same
        // "Frame N:" prefix where N is the inspector's monotonic
        // frame index at report time, so a log reader can group all
        // lines from the same report by string match.  Lines for
        // disabled checks are elided so the log doesn't carry stale
        // "n/a" placeholders.
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "Frame %lld:",
                      static_cast<long long>(snap.framesProcessed));

        promekiInfo("%s report after %lld frames (%.2f s wall) — "
                    "%lld total since open",
                    prefix,
                    static_cast<long long>(_framesSinceLastLog),
                    elapsed,
                    static_cast<long long>(snap.framesProcessed));

        if(_decodeImageData) {
                if(snap.lastEvent.pictureDecoded) {
                        promekiInfo("%s picture data band: decoded %lld / %lld frames "
                                    "(%.1f%%) — most recent: streamID 0x%08x, "
                                    "frameNo %u, TC %s",
                                    prefix,
                                    static_cast<long long>(snap.framesWithPictureData),
                                    static_cast<long long>(snap.framesProcessed),
                                    snap.framesProcessed > 0
                                            ? 100.0 * snap.framesWithPictureData / snap.framesProcessed
                                            : 0.0,
                                    snap.lastEvent.pictureStreamId,
                                    snap.lastEvent.pictureFrameNumber,
                                    renderTc(snap.lastEvent.pictureTimecode).cstr());
                } else {
                        promekiWarn("%s picture data band: NOT DECODED in latest frame "
                                    "(decoded %lld / %lld frames since open)",
                                    prefix,
                                    static_cast<long long>(snap.framesWithPictureData),
                                    static_cast<long long>(snap.framesProcessed));
                }
        }

        if(_decodeLtc) {
                if(snap.lastEvent.ltcDecoded) {
                        promekiInfo("%s audio LTC: decoded %lld / %lld frames (%.1f%%) — "
                                    "most recent: TC %s, sync word at sample %lld "
                                    "within chunk",
                                    prefix,
                                    static_cast<long long>(snap.framesWithLtc),
                                    static_cast<long long>(snap.framesProcessed),
                                    snap.framesProcessed > 0
                                            ? 100.0 * snap.framesWithLtc / snap.framesProcessed
                                            : 0.0,
                                    renderTc(snap.lastEvent.ltcTimecode).cstr(),
                                    static_cast<long long>(snap.lastEvent.ltcSampleStart));
                } else {
                        promekiWarn("%s audio LTC: NOT DECODED in latest frame "
                                    "(decoded %lld / %lld frames since open)",
                                    prefix,
                                    static_cast<long long>(snap.framesWithLtc),
                                    static_cast<long long>(snap.framesProcessed));
                }
        }

        if(_checkTcSync) {
                if(snap.lastEvent.avSyncValid) {
                        const int64_t s = snap.lastEvent.avSyncOffsetSamples;
                        if(s == 0) {
                                promekiInfo("%s A/V Sync: audio and video locked (0 samples)",
                                            prefix);
                        } else {
                                // Render direction in plain language
                                // and report both the raw sample count
                                // and the fractional-frame equivalent
                                // so a QA reader can interpret the
                                // value at whichever scale is more
                                // useful.  Frames are computed from
                                // the LTC's rational rate so 29.97
                                // and friends report correctly.
                                const int64_t absSamples = s < 0 ? -s : s;
                                const double  frames    = _samplesPerFrame > 0.0
                                                                ? static_cast<double>(absSamples) /
                                                                  _samplesPerFrame
                                                                : 0.0;
                                const char *direction = (s > 0)
                                        ? "Video leads audio"
                                        : "Audio leads video";
                                promekiInfo("%s A/V Sync: %s by %lld samples, "
                                            "%.4f frames",
                                            prefix,
                                            direction,
                                            static_cast<long long>(absSamples),
                                            frames);
                        }
                } else {
                        promekiInfo("%s A/V Sync: not measurable in latest frame "
                                    "(needs both picture data and LTC decoded with a "
                                    "valid frame rate)",
                                    prefix);
                }
        }

        // Continuity is reported only when there's something to say —
        // a clean stream stays silent.  When discontinuities have
        // accumulated we emit a warning summary so the line stands
        // out from the routine info-level traffic.
        if(_checkContinuity && snap.totalDiscontinuities > 0) {
                promekiWarn("%s continuity: %lld discontinuities detected "
                            "since open (see earlier warning lines for "
                            "details on each)",
                            prefix,
                            static_cast<long long>(snap.totalDiscontinuities));
        }

        _lastLogWallSec     = now;
        _framesSinceLastLog = 0;
}

Error MediaIOTask_Inspector::executeCmd(MediaIOCommandWrite &cmd) {
        if(!_isOpen) return Error::NotOpen;
        if(!cmd.frame.isValid()) return Error::InvalidArgument;

        initDecoders(*cmd.frame);

        InspectorEvent event;
        event.frameIndex = _frameIndex;

        if(_decodeImageData) runImageDataCheck(*cmd.frame, event);
        if(_decodeLtc)       runLtcCheck(*cmd.frame, event);
        runAvSyncCheck(*cmd.frame, event);
        runContinuityCheck(event);

        // Update stats accumulator under the mutex.
        {
                Mutex::Locker lk(_stateMutex);
                _stats.framesProcessed++;
                if(event.pictureDecoded) _stats.framesWithPictureData++;
                if(event.ltcDecoded)     _stats.framesWithLtc++;
                _stats.totalDiscontinuities += event.discontinuities.size();
                _stats.hasLastEvent = true;
                _stats.lastEvent    = event;
        }

        // Anything that needs human attention is logged immediately as
        // a warning so it lands in the log file at the right point in
        // wall time, with the same "Frame N:" prefix the periodic
        // report uses so log readers can tie them together.  The
        // discontinuities have pre-rendered descriptions courtesy of
        // runContinuityCheck, so we just relay them here.
        if(!event.discontinuities.isEmpty()) {
                char prefix[32];
                std::snprintf(prefix, sizeof(prefix), "Frame %lld:",
                              static_cast<long long>(event.frameIndex));
                for(const auto &d : event.discontinuities) {
                        promekiWarn("%s discontinuity: %s",
                                    prefix, d.description.cstr());
                }
        }

        // Fire the per-frame callback (worker thread context — caller
        // is responsible for thread safety).
        if(_callback) {
                _callback(event);
        }

        _frameIndex++;
        _framesSinceLastLog++;
        cmd.currentFrame = _frameIndex;
        cmd.frameCount   = _frameIndex;

        emitPeriodicLogIfDue();
        // _dropFrames is currently a no-op — the inspector is a sink so
        // there is no downstream consumer.  The flag exists for future
        // tee-style wrappers; for now we always "drop" by simply
        // returning Ok and letting cmd.frame's reference count expire.
        (void)_dropFrames;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
