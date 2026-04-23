/**
 * @file      mediaiotask_inspector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <promeki/mediaiotask_inspector.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/dir.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/ltcdecoder.h>
#include <promeki/pixelformat.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/timestamp.h>
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

// Real wall-clock (system_clock) nanoseconds since the UNIX epoch.
// Separate from TimeStamp::now() because that uses steady_clock — its
// epoch is "some time in the past" (boot on Linux), which makes no
// sense when rendering ISO-8601.
int64_t wallClockEpochNs() {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(
                        system_clock::now().time_since_epoch()).count();
}

// ISO-8601 UTC rendering of an epoch nanosecond value, with
// millisecond precision — wire format that every downstream log tool
// handles without additional parsing code.  Buffer must be at least
// @ref IsoBufLen bytes; the extra headroom over the fixed 24-char
// output keeps gcc's -Wformat-truncation analysis happy for
// hypothetically-huge tm_year values.
static constexpr size_t IsoBufLen = 64;
void formatIsoUtc(int64_t epochNs, char *out, size_t outLen) {
        if(epochNs <= 0) {
                if(outLen > 0) out[0] = '\0';
                return;
        }
        const time_t secs = static_cast<time_t>(epochNs / 1000000000LL);
        const int msec    = static_cast<int>((epochNs / 1000000LL) % 1000LL);
        struct tm tm;
        gmtime_r(&secs, &tm);
        std::snprintf(out, outLen,
                "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
}

// Sums every plane's buffer into one byte count — gives the "on the
// wire" frame size, which is what a stats-file reader most likely
// wants.  Returns 0 if the image has no planes.
size_t imageByteSize(const Image &img) {
        size_t total = 0;
        const Buffer::PtrList &planes = img.planes();
        for(size_t i = 0; i < planes.size(); ++i) {
                if(planes[i].isValid()) total += planes[i]->size();
        }
        return total;
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
                false,  // canBeSource
                true,   // canBeSink
                false,  // canBeTransform
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
                        // Inspector defaults run every *in-memory* check
                        // out of the box.  CaptureStats is deliberately
                        // excluded from the default list because it
                        // opens an output file — making it opt-in keeps
                        // a default-configured inspector side-effect
                        // free.
                        EnumList allTests = EnumList::forType<InspectorTest>();
                        allTests.append(InspectorTest::ImageData);
                        allTests.append(InspectorTest::Ltc);
                        allTests.append(InspectorTest::TcSync);
                        allTests.append(InspectorTest::Continuity);
                        allTests.append(InspectorTest::Timestamp);
                        allTests.append(InspectorTest::AudioSamples);
                        s(MediaConfig::InspectorDropFrames,           true);
                        s(MediaConfig::InspectorTests,                allTests);
                        s(MediaConfig::InspectorImageDataRepeatLines, int32_t(16));
                        s(MediaConfig::InspectorLtcChannel,           int32_t(0));
                        s(MediaConfig::InspectorSyncOffsetToleranceSamples, int32_t(0));
                        s(MediaConfig::InspectorLogIntervalSec,       1.0);
                        s(MediaConfig::InspectorStatsFile,            String());
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
        _hasPreviousVideoTimestamp = false;
        _hasPreviousAudioTimestamp = false;
        _previousVideoTimestampNs  = 0;
        _previousAudioTimestampNs  = 0;
        _audioSamplesAnchorSet       = false;
        _audioSamplesAnchorNs        = 0;
        _audioSamplesPreviousStampNs = 0;
        _audioSamplesCumulative      = 0;
        _frameIndex          = 0;
        _decodersInitialized = false;
        _imageDataDecoder    = ImageDataDecoder();
        _ltcDecoder.reset();
        _ltcCumulativeSamples = 0;
        _lastLogWallSec      = monotonicWallSeconds();
        _framesSinceLastLog  = 0;
}

Error MediaIOTask_Inspector::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Sink) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        _dropFrames           = cfg.getAs<bool>(MediaConfig::InspectorDropFrames,        true);
        _imageDataRepeatLines = cfg.getAs<int>(MediaConfig::InspectorImageDataRepeatLines, 16);
        _ltcChannel           = cfg.getAs<int>(MediaConfig::InspectorLtcChannel,         0);
        _syncOffsetToleranceSamples =
                cfg.getAs<int>(MediaConfig::InspectorSyncOffsetToleranceSamples, 0);
        _logIntervalSec       = cfg.getAs<double>(MediaConfig::InspectorLogIntervalSec,  1.0);

        // Resolve the test selection.  The configured list drives
        // which tests run: each entry turns one test on, anything
        // absent stays off.  Test dependencies (TcSync → ImageData
        // + Ltc, Continuity → ImageData) are auto-added after the
        // explicit list has been interpreted so callers never have
        // to know about them.  The default config (set in
        // formatDesc) carries every test, so a default-configured
        // inspector runs the full suite.
        _decodeImageData    = false;
        _decodeLtc          = false;
        _checkTcSync        = false;
        _checkContinuity    = false;
        _checkTimestamp     = false;
        _checkAudioSamples  = false;
        _checkCaptureStats  = false;
        const EnumList testsCfg = cfg.get(MediaConfig::InspectorTests).get<EnumList>();
        for(size_t i = 0; i < testsCfg.size(); ++i) {
                const int v = testsCfg.at(i).value();
                if(v == InspectorTest::ImageData.value())         _decodeImageData    = true;
                else if(v == InspectorTest::Ltc.value())          _decodeLtc          = true;
                else if(v == InspectorTest::TcSync.value())       _checkTcSync        = true;
                else if(v == InspectorTest::Continuity.value())   _checkContinuity    = true;
                else if(v == InspectorTest::Timestamp.value())    _checkTimestamp     = true;
                else if(v == InspectorTest::AudioSamples.value()) _checkAudioSamples  = true;
                else if(v == InspectorTest::CaptureStats.value()) _checkCaptureStats  = true;
        }
        if(_checkTcSync) {
                _decodeImageData = true;
                _decodeLtc       = true;
        }
        if(_checkContinuity) {
                _decodeImageData = true;
        }

        resetState();

        // Open the CaptureStats output file after resetState so any
        // leftover state from a previous run is cleared first.  A file
        // open error aborts open() so the caller sees the failure
        // immediately rather than discovering later that rows were
        // silently dropped.
        if(_checkCaptureStats) {
                String configured = cfg.getAs<String>(
                        MediaConfig::InspectorStatsFile, String());
                Error ferr = openCaptureStatsFile(configured);
                if(ferr.isError()) return ferr;
        }

        _isOpen = true;

        // Dump the resolved configuration into the log so any later
        // periodic reports can be interpreted in context — especially
        // useful when post-mortem'ing a recorded log file.  Each line
        // shares the "config:" prefix so the whole block is scannable.
        promekiInfo("Image data decode     = %s", _decodeImageData ? "enabled" : "disabled");
        promekiInfo("LTC decode            = %s", _decodeLtc ? "enabled" : "disabled");
        promekiInfo("LTC channel           = %d", _ltcChannel);
        promekiInfo("A/V sync check        = %s", _checkTcSync ? "enabled" : "disabled");
        promekiInfo("A/V sync jitter tol   = %d samples", _syncOffsetToleranceSamples);
        promekiInfo("Continuity check      = %s", _checkContinuity ? "enabled" : "disabled");
        promekiInfo("Timestamp check       = %s", _checkTimestamp ? "enabled" : "disabled");
        promekiInfo("Audio samples check   = %s", _checkAudioSamples ? "enabled" : "disabled");
        promekiInfo("Capture stats check   = %s%s%s",
                    _checkCaptureStats ? "enabled (writing " : "disabled",
                    _checkCaptureStats ? _statsFilePath.cstr() : "",
                    _checkCaptureStats ? ")" : "");
        promekiInfo("Image data band       = %d scan lines per item", _imageDataRepeatLines);
        promekiInfo("Drop frames           = %s", _dropFrames ? "yes" : "no");
        promekiInfo("Log interval          = %.2f seconds", _logIntervalSec);

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
        closeCaptureStatsFile();

        // Emit a final summary so the log contains a complete record
        // of the inspector's lifetime.  We snapshot under the mutex
        // once and work from the copy — same pattern as the periodic
        // report.
        InspectorSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }

        promekiInfo("=== INSPECTOR FINAL REPORT ===");
        promekiInfo("  Total frames processed: %lld",
                    static_cast<long long>(snap.framesProcessed.value()));

        if(_decodeImageData) {
                const double pct = snap.framesProcessed.value() > 0
                        ? 100.0 * snap.framesWithPictureData.value() / snap.framesProcessed.value()
                        : 0.0;
                if(snap.framesWithPictureData == snap.framesProcessed) {
                        promekiInfo("  Image data band: decoded %lld / %lld frames (%.1f%%)",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    pct);
                } else {
                        promekiWarn("  Image data band: decoded %lld / %lld frames (%.1f%%) — "
                                    "%lld frames failed to decode",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    pct,
                                    static_cast<long long>((snap.framesProcessed - snap.framesWithPictureData).value()));
                }
                if(snap.hasLastEvent && snap.lastEvent.pictureDecoded) {
                        promekiInfo("  Last picture: streamID 0x%08X, frameNo %u, TC %s",
                                    snap.lastEvent.pictureStreamId,
                                    snap.lastEvent.pictureFrameNumber,
                                    renderTc(snap.lastEvent.pictureTimecode).cstr());
                }
        }

        if(_decodeLtc) {
                const double pct = snap.framesProcessed.value() > 0
                        ? 100.0 * snap.framesWithLtc.value() / snap.framesProcessed.value()
                        : 0.0;
                if(snap.framesWithLtc == snap.framesProcessed) {
                        promekiInfo("  LTC: decoded %lld / %lld frames (%.1f%%)",
                                    static_cast<long long>(snap.framesWithLtc.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    pct);
                } else {
                        promekiWarn("  LTC: decoded %lld / %lld frames (%.1f%%) — "
                                    "%lld frames failed to decode",
                                    static_cast<long long>(snap.framesWithLtc.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    pct,
                                    static_cast<long long>((snap.framesProcessed - snap.framesWithLtc).value()));
                }
                if(snap.hasLastEvent && snap.lastEvent.ltcDecoded) {
                        promekiInfo("  Last LTC: TC %s",
                                    renderTc(snap.lastEvent.ltcTimecode).cstr());
                }
        }

        if(_checkTcSync && snap.hasLastEvent && snap.lastEvent.avSyncValid) {
                const int64_t s = snap.lastEvent.avSyncOffsetSamples;
                if(s == 0) {
                        promekiInfo("  A/V Sync: audio and video locked (0 samples)");
                } else {
                        const int64_t absSamples = s < 0 ? -s : s;
                        const double  frames     = _samplesPerFrame > 0.0
                                ? static_cast<double>(absSamples) / _samplesPerFrame
                                : 0.0;
                        const char *direction = (s > 0)
                                ? "Video leads audio"
                                : "Audio leads video";
                        promekiInfo("  A/V Sync: %s by %lld samples, %.4f frames",
                                    direction,
                                    static_cast<long long>(absSamples),
                                    frames);
                }
        }

        if(_checkTimestamp) {
                if(snap.framesWithVideoTimestamp == snap.framesProcessed) {
                        promekiInfo("  Video timestamps: all %lld frames stamped",
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else {
                        promekiWarn("  Video timestamps: %lld / %lld frames stamped "
                                    "(%lld missing)",
                                    static_cast<long long>(snap.framesWithVideoTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    static_cast<long long>((snap.framesProcessed -
                                                          snap.framesWithVideoTimestamp).value()));
                }
                if(snap.videoDeltaSamples > 0) {
                        promekiInfo("  Video delta: min %.3f ms / avg %.3f ms / max %.3f ms  "
                                    "(actual %.4f fps over %lld samples)",
                                    snap.videoDeltaMinNs / 1.0e6,
                                    snap.videoDeltaAvgNs / 1.0e6,
                                    snap.videoDeltaMaxNs / 1.0e6,
                                    snap.actualFps,
                                    static_cast<long long>(snap.videoDeltaSamples));
                }
                if(snap.framesWithAudioTimestamp == snap.framesProcessed) {
                        promekiInfo("  Audio timestamps: all %lld frames stamped",
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else if(snap.framesWithAudioTimestamp > 0) {
                        promekiWarn("  Audio timestamps: %lld / %lld frames stamped "
                                    "(%lld missing)",
                                    static_cast<long long>(snap.framesWithAudioTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    static_cast<long long>((snap.framesProcessed -
                                                          snap.framesWithAudioTimestamp).value()));
                }
                if(snap.audioDeltaSamples > 0) {
                        promekiInfo("  Audio delta: min %.3f ms / avg %.3f ms / max %.3f ms  "
                                    "(%lld samples)",
                                    snap.audioDeltaMinNs / 1.0e6,
                                    snap.audioDeltaAvgNs / 1.0e6,
                                    snap.audioDeltaMaxNs / 1.0e6,
                                    static_cast<long long>(snap.audioDeltaSamples));
                }
        }

        if(_checkAudioSamples) {
                if(snap.audioSamplesFrames == 0) {
                        promekiWarn("  Audio samples: no audio frames observed");
                } else {
                        promekiInfo("  Audio samples: per-frame min/avg/max = "
                                    "%lld / %.2f / %lld  (%lld frames)",
                                    static_cast<long long>(snap.audioSamplesMin),
                                    snap.audioSamplesAvg,
                                    static_cast<long long>(snap.audioSamplesMax),
                                    static_cast<long long>(snap.audioSamplesFrames));
                        if(snap.measuredAudioSampleRate > 0.0) {
                                promekiInfo("  Audio samples: measured rate %.2f Hz "
                                            "(%lld samples over %.3f s)",
                                            snap.measuredAudioSampleRate,
                                            static_cast<long long>(snap.audioSamplesTotal),
                                            snap.audioSamplesSpanNs / 1.0e9);
                        }
                }
        }

        if(_checkContinuity) {
                if(snap.totalDiscontinuities == 0) {
                        promekiInfo("  Continuity: clean — no discontinuities detected");
                } else {
                        promekiWarn("  Continuity: %lld total discontinuities detected",
                                    static_cast<long long>(snap.totalDiscontinuities));
                }
        }

        promekiInfo("=== END OF FINAL REPORT ===");

        // Stats survive close so callers can retrieve final counts;
        // resetState happens on the next open.
        return Error::Ok;
}

void MediaIOTask_Inspector::decompressImages(Frame &frame) {
        // Walk every image in the frame; any that carries a
        // compressed PixelFormat gets replaced in-place with the
        // decompressed output of Image::convert().  The target
        // format is the first entry in the codec's decodeTargets
        // list — guaranteed to be a native decode format so
        // convert() skips the post-decode CSC stage.  If the codec
        // advertises no decode targets (shouldn't happen for a
        // working codec), we fall back to RGBA8_sRGB which every
        // codec can reach via CSC.
        Image::PtrList &images = frame.imageList();
        for(size_t i = 0; i < images.size(); ++i) {
                Image::Ptr &imgPtr = images[i];
                if(!imgPtr.isValid() || !imgPtr->isCompressed()) continue;

                const PixelFormat &srcPd = imgPtr->desc().pixelFormat();
                PixelFormat targetPd;
                if(!srcPd.decodeTargets().isEmpty()) {
                        targetPd = PixelFormat(srcPd.decodeTargets()[0]);
                } else {
                        targetPd = PixelFormat(PixelFormat::RGBA8_sRGB);
                }

                Image decoded = imgPtr->convert(targetPd, imgPtr->metadata());
                if(!decoded.isValid()) {
                        promekiWarn("MediaIOTask_Inspector: failed to decompress "
                                    "%s image on frame %lld — downstream checks "
                                    "may report failures",
                                    srcPd.name().cstr(),
                                    static_cast<long long>(_frameIndex.value()));
                        continue;
                }
                imgPtr = Image::Ptr::create(std::move(decoded));
        }
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
                                        _ltcDecoder = LtcDecoder::UPtr::create(rate);
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

        FrameNumber picFrames = pictureTc.toFrameNumber();
        FrameNumber ltcFrames = event.ltcTimecode.toFrameNumber();
        if(!picFrames.isValid() || !ltcFrames.isValid()) return;

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
        const int64_t tcOffsetFrames = picFrames.value() - ltcFrames.value();
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
                        d.previousValue = String::sprintf("%+lld",
                                      static_cast<long long>(_previousSyncOffset));
                        d.currentValue  = String::sprintf("%+lld",
                                      static_cast<long long>(event.avSyncOffsetSamples));
                        d.description = String::sprintf(
                                      "A/V sync offset moved: was %+lld samples, "
                                      "now %+lld samples (delta %+lld, tolerance %d)",
                                      static_cast<long long>(_previousSyncOffset),
                                      static_cast<long long>(event.avSyncOffsetSamples),
                                      static_cast<long long>(delta),
                                      _syncOffsetToleranceSamples);
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
                        d.previousValue = String::sprintf("0x%08X", _previousStreamId);
                        d.currentValue  = String::sprintf("0x%08X", event.pictureStreamId);
                        d.description   = String::sprintf("Stream ID changed: was %s, now %s", d.previousValue.cstr(), d.currentValue.cstr());
                        event.discontinuities.pushToBack(d);
                }

                const uint32_t expectedNext = _previousFrameNumber + 1;
                if(event.pictureFrameNumber != expectedNext) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::FrameNumberJump;
                        d.previousValue = String::number(_previousFrameNumber);
                        d.currentValue = String::number(event.pictureFrameNumber);
                        const long long deltaFromExpected =
                                static_cast<long long>(event.pictureFrameNumber) -
                                static_cast<long long>(expectedNext);
                        d.description = String::sprintf("Frame number jumped: was %u (expected %u next), got %u "
                                      "(%+lld frame%s relative to expected)",
                                      _previousFrameNumber, expectedNext, event.pictureFrameNumber,
                                      deltaFromExpected,
                                      deltaFromExpected == 1 || deltaFromExpected == -1 ? "" : "s");
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
                                d.description = String::sprintf(
                                              "Picture TC jumped: was %s (expected %s next), got %s",
                                              d.previousValue.cstr(),
                                              renderTc(expectedTc).cstr(),
                                              d.currentValue.cstr());
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
                                d.description = String::sprintf(
                                              "LTC jumped: was %s (expected %s next), got %s",
                                              d.previousValue.cstr(),
                                              renderTc(expectedLtc).cstr(),
                                              d.currentValue.cstr());
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

void MediaIOTask_Inspector::runTimestampCheck(const Frame &frame, InspectorEvent &event) {
        event.timestampTestEnabled = true;

        // Pull the per-essence MediaTimeStamps.  MediaIO is
        // responsible for ensuring every essence carries one (backends
        // with hardware timestamps set them directly; MediaIO fills in
        // a Synthetic fallback otherwise), so "missing" here is a real
        // fault — we surface it as a warning and a discontinuity.
        if(!frame.imageList().isEmpty()) {
                const Image::Ptr &imgPtr = frame.imageList()[0];
                if(imgPtr.isValid()) {
                        MediaTimeStamp mts = imgPtr->metadata()
                                .get(Metadata::MediaTimeStamp)
                                .get<MediaTimeStamp>();
                        if(mts.isValid()) {
                                event.videoTimestampValid = true;
                                event.videoTimestampNs =
                                        mts.timeStamp().nanoseconds() +
                                        mts.offset().nanoseconds();
                        }
                }
        }
        if(!frame.audioList().isEmpty()) {
                const Audio::Ptr &audPtr = frame.audioList()[0];
                if(audPtr.isValid()) {
                        MediaTimeStamp mts = audPtr->metadata()
                                .get(Metadata::MediaTimeStamp)
                                .get<MediaTimeStamp>();
                        if(mts.isValid()) {
                                event.audioTimestampValid = true;
                                event.audioTimestampNs =
                                        mts.timeStamp().nanoseconds() +
                                        mts.offset().nanoseconds();
                        }
                }
        }

        // Compute frame-to-frame deltas only when both the current and
        // the previous frame carried a valid timestamp — a gap resets
        // the anchor so one missing frame doesn't poison the next
        // measurement.
        if(event.videoTimestampValid && _hasPreviousVideoTimestamp) {
                event.videoTimestampDeltaNs =
                        event.videoTimestampNs - _previousVideoTimestampNs;
                event.videoTimestampDeltaValid = true;
        }
        if(event.audioTimestampValid && _hasPreviousAudioTimestamp) {
                event.audioTimestampDeltaNs =
                        event.audioTimestampNs - _previousAudioTimestampNs;
                event.audioTimestampDeltaValid = true;
        }

        // Surface a missing timestamp as a discontinuity so it lands in
        // the same warning channel as the other continuity faults.  We
        // fire unconditionally rather than "only after a previously
        // valid frame" because MediaIO guarantees one on every essence
        // — the very first missing stamp is already a real failure.
        if(!frame.imageList().isEmpty() && !event.videoTimestampValid) {
                InspectorDiscontinuity d;
                d.kind          = InspectorDiscontinuity::MissingVideoTimestamp;
                d.previousValue = String("valid");
                d.currentValue  = String("missing");
                d.description   = String("Video MediaTimeStamp missing on frame");
                event.discontinuities.pushToBack(d);
        }
        if(!frame.audioList().isEmpty() && !event.audioTimestampValid) {
                InspectorDiscontinuity d;
                d.kind          = InspectorDiscontinuity::MissingAudioTimestamp;
                d.previousValue = String("valid");
                d.currentValue  = String("missing");
                d.description   = String("Audio MediaTimeStamp missing on frame");
                event.discontinuities.pushToBack(d);
        }

        // Advance the per-essence anchors.  On a gap we reset rather
        // than carry forward, so the next valid timestamp starts a
        // fresh delta chain.
        if(event.videoTimestampValid) {
                _previousVideoTimestampNs  = event.videoTimestampNs;
                _hasPreviousVideoTimestamp = true;
        } else {
                _hasPreviousVideoTimestamp = false;
        }
        if(event.audioTimestampValid) {
                _previousAudioTimestampNs  = event.audioTimestampNs;
                _hasPreviousAudioTimestamp = true;
        } else {
                _hasPreviousAudioTimestamp = false;
        }
}

void MediaIOTask_Inspector::runAudioSamplesCheck(const Frame &frame, InspectorEvent &event) {
        event.audioSamplesTestEnabled = true;

        if(frame.audioList().isEmpty()) return;
        const Audio::Ptr &audPtr = frame.audioList()[0];
        if(!audPtr.isValid()) return;

        const int64_t n = static_cast<int64_t>(audPtr->samples());
        event.audioSamplesValid     = true;
        event.audioSamplesThisFrame = n;

        // Derive the measured sample rate from cumulative samples and
        // the elapsed audio MediaTimeStamp span.  The *first* anchored
        // frame establishes the origin; samples from that frame are
        // deliberately excluded because the measurement window they
        // mark is the time from anchor to the *next* frame, not the
        // samples that precede the anchor.  Any timestamp gap resets
        // the anchor so the next valid chunk starts a fresh window.
        MediaTimeStamp audMts = audPtr->metadata()
                .get(Metadata::MediaTimeStamp)
                .get<MediaTimeStamp>();
        if(!audMts.isValid()) {
                _audioSamplesAnchorSet = false;
                return;
        }
        const int64_t stampNs = audMts.timeStamp().nanoseconds() +
                                audMts.offset().nanoseconds();
        if(!_audioSamplesAnchorSet) {
                _audioSamplesAnchorSet       = true;
                _audioSamplesAnchorNs        = stampNs;
                _audioSamplesPreviousStampNs = stampNs;
                _audioSamplesCumulative      = 0;
                return;
        }
        // Timestamps must advance monotonically for the rate
        // derivation to be meaningful; a non-monotone stamp points at
        // a gap or a backend-level fault, so we reset the anchor
        // rather than feeding a nonsensical denominator.
        if(stampNs <= _audioSamplesPreviousStampNs) {
                _audioSamplesAnchorSet       = true;
                _audioSamplesAnchorNs        = stampNs;
                _audioSamplesPreviousStampNs = stampNs;
                _audioSamplesCumulative      = 0;
                return;
        }
        _audioSamplesCumulative      += n;
        _audioSamplesPreviousStampNs  = stampNs;
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

        promekiInfo("[INSPECTOR REPORT] Frame %lld: %lld frames (%.2f s wall) since last report - %lld total",
            static_cast<long long>(snap.framesProcessed.value()),
            static_cast<long long>(_framesSinceLastLog.value()),
            elapsed,
            static_cast<long long>(snap.framesProcessed.value()));

        if(_decodeImageData) {
                if(snap.lastEvent.pictureDecoded) {
                        promekiInfo("  Image data band: decoded %lld / %lld frames "
                                    "(%.1f%%) — most recent: streamID 0x%08X, "
                                    "frameNo %u, TC %s",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    snap.framesProcessed.value() > 0
                                            ? 100.0 * snap.framesWithPictureData.value() / snap.framesProcessed.value()
                                            : 0.0,
                                    snap.lastEvent.pictureStreamId,
                                    snap.lastEvent.pictureFrameNumber,
                                    renderTc(snap.lastEvent.pictureTimecode).cstr());
                } else {
                        promekiWarn("  Image data band: NOT DECODED in latest frame "
                                    "(decoded %lld / %lld frames since open)",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                }
        }

        if(_decodeLtc) {
                if(snap.lastEvent.ltcDecoded) {
                        promekiInfo("  LTC: decoded %lld / %lld frames (%.1f%%) — "
                                    "most recent: TC %s, sync word at sample %lld "
                                    "within chunk",
                                    static_cast<long long>(snap.framesWithLtc.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    snap.framesProcessed.value() > 0
                                            ? 100.0 * snap.framesWithLtc.value() / snap.framesProcessed.value()
                                            : 0.0,
                                    renderTc(snap.lastEvent.ltcTimecode).cstr(),
                                    static_cast<long long>(snap.lastEvent.ltcSampleStart));
                } else {
                        promekiWarn("  LTC: NOT DECODED in latest frame "
                                    "(decoded %lld / %lld frames since open)",
                                    static_cast<long long>(snap.framesWithLtc.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                }
        }

        if(_checkTcSync) {
                if(snap.lastEvent.avSyncValid) {
                        const int64_t s = snap.lastEvent.avSyncOffsetSamples;
                        if(s == 0) {
                                promekiInfo("  A/V Sync: audio and video locked (0 samples)");
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
                                promekiInfo("  A/V Sync: %s by %lld samples, "
                                            "%.4f frames",
                                            direction,
                                            static_cast<long long>(absSamples),
                                            frames);
                        }
                }
        }

        if(_checkTimestamp) {
                if(snap.videoDeltaSamples > 0) {
                        promekiInfo("  Video timestamps: delta min/avg/max = "
                                    "%.3f / %.3f / %.3f ms  (actual %.4f fps, "
                                    "%lld / %lld frames stamped)",
                                    snap.videoDeltaMinNs / 1.0e6,
                                    snap.videoDeltaAvgNs / 1.0e6,
                                    snap.videoDeltaMaxNs / 1.0e6,
                                    snap.actualFps,
                                    static_cast<long long>(snap.framesWithVideoTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else if(snap.framesWithVideoTimestamp > 0) {
                        promekiInfo("  Video timestamps: %lld / %lld frames stamped "
                                    "(no delta yet)",
                                    static_cast<long long>(snap.framesWithVideoTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else {
                        promekiWarn("  Video timestamps: no stamped frames yet");
                }
                if(snap.audioDeltaSamples > 0) {
                        promekiInfo("  Audio timestamps: delta min/avg/max = "
                                    "%.3f / %.3f / %.3f ms  (%lld / %lld frames stamped)",
                                    snap.audioDeltaMinNs / 1.0e6,
                                    snap.audioDeltaAvgNs / 1.0e6,
                                    snap.audioDeltaMaxNs / 1.0e6,
                                    static_cast<long long>(snap.framesWithAudioTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else if(snap.framesWithAudioTimestamp > 0) {
                        promekiInfo("  Audio timestamps: %lld / %lld frames stamped "
                                    "(no delta yet)",
                                    static_cast<long long>(snap.framesWithAudioTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                }
        }

        if(_checkAudioSamples && snap.audioSamplesFrames > 0) {
                if(snap.measuredAudioSampleRate > 0.0) {
                        promekiInfo("  Audio samples: per-frame min/avg/max = "
                                    "%lld / %.2f / %lld  (measured rate %.2f Hz "
                                    "over %lld samples / %.3f s)",
                                    static_cast<long long>(snap.audioSamplesMin),
                                    snap.audioSamplesAvg,
                                    static_cast<long long>(snap.audioSamplesMax),
                                    snap.measuredAudioSampleRate,
                                    static_cast<long long>(snap.audioSamplesTotal),
                                    snap.audioSamplesSpanNs / 1.0e9);
                } else {
                        promekiInfo("  Audio samples: per-frame min/avg/max = "
                                    "%lld / %.2f / %lld  (rate not yet measurable)",
                                    static_cast<long long>(snap.audioSamplesMin),
                                    snap.audioSamplesAvg,
                                    static_cast<long long>(snap.audioSamplesMax));
                }
        }

        // Continuity is reported only when there's something to say —
        // a clean stream stays silent.  When discontinuities have
        // accumulated we emit a warning summary so the line stands
        // out from the routine info-level traffic.
        if(_checkContinuity && snap.totalDiscontinuities > 0) {
                promekiWarn("  Continuity: %lld total discontinuities detected",
                            static_cast<long long>(snap.totalDiscontinuities));
        }

        promekiInfo("=== END OF REPORT ===");
        _lastLogWallSec     = now;
        _framesSinceLastLog = 0;
}

// ---------------------------------------------------------------------------
// CaptureStats output file
// ---------------------------------------------------------------------------

Error MediaIOTask_Inspector::openCaptureStatsFile(const String &configured) {
        closeCaptureStatsFile();

        if(configured.isEmpty()) {
                // Synthesize a unique path under Dir::temp().  PID +
                // steady-clock nanoseconds is effectively collision
                // free on any machine that isn't spawning thousands of
                // inspectors per nanosecond.
                const int64_t ns = TimeStamp::now().nanoseconds();
                FilePath p = Dir::temp().path() /
                        String::sprintf("promeki_inspector_stats_%d_%lld.tsv",
                                        static_cast<int>(getpid()),
                                        static_cast<long long>(ns));
                _statsFilePath = p.toString();
        } else {
                _statsFilePath = configured;
        }

        _statsFile = std::fopen(_statsFilePath.cstr(), "w");
        if(_statsFile == nullptr) {
                promekiErr("MediaIOTask_Inspector: failed to open CaptureStats "
                           "file '%s': %s",
                           _statsFilePath.cstr(), std::strerror(errno));
                _statsFilePath = String();
                return Error::OpenFailed;
        }

        // Line-buffer so a crash or kill -9 still leaves each
        // completed row on disk.  Stats files are append-only and
        // small enough that the per-line flush cost is irrelevant.
        std::setvbuf(_statsFile, nullptr, _IOLBF, 0);

        // Header: comment lines describe the provenance + column
        // semantics, followed by a single tab-separated column line.
        // Every data row has the same column count and order so tools
        // like awk / cut / column -t work out of the box.
        char iso[64];
        formatIsoUtc(wallClockEpochNs(), iso, sizeof(iso));
        std::fprintf(_statsFile,
                "# libpromeki Inspector CaptureStats\n"
                "# generated=%s  pid=%d\n"
                "# One record per frame.  Columns are tab-separated.\n"
                "# Missing / inapplicable values are written as '-'.\n"
                "# wall_* columns are real (system_clock) time.\n"
                "# video_ts_ns / audio_ts_ns are raw nanoseconds in the\n"
                "# clock domain named by the adjacent *_clock column —\n"
                "# NOT necessarily the UNIX epoch.\n"
                "# *_delta_* columns are frame-to-frame differences.\n",
                iso, static_cast<int>(getpid()));
        std::fprintf(_statsFile,
                "frame\t"
                "wall_ns\twall_iso\t"
                "video_ts_ns\tvideo_clock\tvideo_delta_ns\tvideo_delta_ms\t"
                "image_width\timage_height\tpixel_format\timage_bytes\t"
                "audio_ts_ns\taudio_clock\taudio_delta_ns\taudio_delta_ms\t"
                "audio_samples\taudio_format\taudio_rate_hz\taudio_channels\taudio_bytes\n");

        promekiInfo("MediaIOTask_Inspector: CaptureStats writing to %s",
                    _statsFilePath.cstr());
        _statsWriteError = false;
        return Error::Ok;
}

void MediaIOTask_Inspector::closeCaptureStatsFile() {
        if(_statsFile != nullptr) {
                std::fclose(_statsFile);
                _statsFile = nullptr;
        }
        _statsFilePath = String();
        _statsWriteError = false;
}

void MediaIOTask_Inspector::runCaptureStats(const Frame &frame,
                                            const InspectorEvent &event) {
        if(_statsFile == nullptr || _statsWriteError) return;

        // Wall time = when we're writing the row.  system_clock (not
        // steady_clock) so the ISO rendering is meaningful.
        const int64_t wallNs = wallClockEpochNs();
        char wallIso[64];
        formatIsoUtc(wallNs, wallIso, sizeof(wallIso));

        // -- Video columns --
        String videoTsNs      = String("-");
        String videoClockName = String("-");
        String videoDeltaNs   = String("-");
        String videoDeltaMs   = String("-");
        String imgWidth       = String("-");
        String imgHeight      = String("-");
        String pixelFormat      = String("-");
        String imageBytes     = String("-");
        if(!frame.imageList().isEmpty()) {
                const Image::Ptr &imgPtr = frame.imageList()[0];
                if(imgPtr.isValid()) {
                        imgWidth    = String::number(imgPtr->size().width());
                        imgHeight   = String::number(imgPtr->size().height());
                        pixelFormat   = PixelFormat(imgPtr->desc().pixelFormat()).name();
                        imageBytes  = String::number(static_cast<int64_t>(
                                imageByteSize(*imgPtr)));

                        MediaTimeStamp mts = imgPtr->metadata()
                                .get(Metadata::MediaTimeStamp)
                                .get<MediaTimeStamp>();
                        if(mts.isValid()) {
                                const int64_t ns =
                                        mts.timeStamp().nanoseconds() +
                                        mts.offset().nanoseconds();
                                videoTsNs = String::number(ns);
                                videoClockName = mts.domain().name();
                        }
                }
        }
        if(event.videoTimestampDeltaValid) {
                const int64_t d = event.videoTimestampDeltaNs;
                videoDeltaNs = String::number(d);
                videoDeltaMs = String::sprintf("%.6f", d / 1.0e6);
        }

        // -- Audio columns --
        String audioTsNs      = String("-");
        String audioClockName = String("-");
        String audioDeltaNs   = String("-");
        String audioDeltaMs   = String("-");
        String audioSamples   = String("-");
        String audioFormat    = String("-");
        String audioRateHz    = String("-");
        String audioChannels  = String("-");
        String audioBytes     = String("-");
        if(!frame.audioList().isEmpty()) {
                const Audio::Ptr &audPtr = frame.audioList()[0];
                if(audPtr.isValid()) {
                        const AudioDesc &ad = audPtr->desc();
                        audioSamples  = String::number(static_cast<int64_t>(
                                audPtr->samples()));
                        audioFormat   = ad.format().name();
                        audioRateHz   = String::sprintf("%.2f", ad.sampleRate());
                        audioChannels = String::number(static_cast<int64_t>(
                                ad.channels()));
                        audioBytes    = String::number(static_cast<int64_t>(
                                ad.bytesPerSample() * ad.channels() *
                                audPtr->samples()));

                        MediaTimeStamp mts = audPtr->metadata()
                                .get(Metadata::MediaTimeStamp)
                                .get<MediaTimeStamp>();
                        if(mts.isValid()) {
                                const int64_t ns =
                                        mts.timeStamp().nanoseconds() +
                                        mts.offset().nanoseconds();
                                audioTsNs = String::number(ns);
                                audioClockName = mts.domain().name();
                        }
                }
        }
        if(event.audioTimestampDeltaValid) {
                const int64_t d = event.audioTimestampDeltaNs;
                audioDeltaNs = String::number(d);
                audioDeltaMs = String::sprintf("%.6f", d / 1.0e6);
        }

        const int rc = std::fprintf(_statsFile,
                "%lld\t"
                "%lld\t%s\t"
                "%s\t%s\t%s\t%s\t"
                "%s\t%s\t%s\t%s\t"
                "%s\t%s\t%s\t%s\t"
                "%s\t%s\t%s\t%s\t%s\n",
                static_cast<long long>(event.frameIndex.value()),
                static_cast<long long>(wallNs), wallIso,
                videoTsNs.cstr(), videoClockName.cstr(),
                videoDeltaNs.cstr(), videoDeltaMs.cstr(),
                imgWidth.cstr(), imgHeight.cstr(),
                pixelFormat.cstr(), imageBytes.cstr(),
                audioTsNs.cstr(), audioClockName.cstr(),
                audioDeltaNs.cstr(), audioDeltaMs.cstr(),
                audioSamples.cstr(), audioFormat.cstr(),
                audioRateHz.cstr(), audioChannels.cstr(), audioBytes.cstr());
        if(rc < 0) {
                promekiErr("MediaIOTask_Inspector: CaptureStats write "
                           "failed on frame %lld (%s) — further rows "
                           "will be dropped",
                           static_cast<long long>(event.frameIndex.value()),
                           std::strerror(errno));
                _statsWriteError = true;
        }
}

Error MediaIOTask_Inspector::executeCmd(MediaIOCommandWrite &cmd) {
        if(!_isOpen) return Error::NotOpen;
        if(!cmd.frame.isValid()) return Error::InvalidArgument;
        stampWorkBegin();

        // Decompress any compressed images into a local Frame copy
        // before running the checks.  The inspector is a sink so we
        // never emit the decompressed frame downstream — this local
        // work is purely to give the picture-side checks (ImageData
        // decode, continuity) readable pixels.  Frame's shared-plane
        // model means the copy is cheap when no decompression is
        // needed; when it is, the compressed entries get replaced
        // in-place with freshly-allocated decoded images.
        Frame work = *cmd.frame;
        decompressImages(work);

        initDecoders(work);

        InspectorEvent event;
        event.frameIndex = _frameIndex;

        if(_decodeImageData) runImageDataCheck(work, event);
        if(_decodeLtc)       runLtcCheck(work, event);
        runAvSyncCheck(work, event);
        runContinuityCheck(event);
        if(_checkTimestamp)  runTimestampCheck(work, event);
        if(_checkAudioSamples) runAudioSamplesCheck(work, event);
        if(_checkCaptureStats) runCaptureStats(work, event);

        // Update stats accumulator under the mutex.
        {
                Mutex::Locker lk(_stateMutex);
                _stats.framesProcessed++;
                if(event.pictureDecoded) _stats.framesWithPictureData++;
                if(event.ltcDecoded)     _stats.framesWithLtc++;
                if(event.videoTimestampValid) _stats.framesWithVideoTimestamp++;
                if(event.audioTimestampValid) _stats.framesWithAudioTimestamp++;
                if(event.videoTimestampDeltaValid) {
                        // Running min/max/avg.  We keep avg as a
                        // double: numerically safer than summing int64
                        // deltas for a long capture, and the fractional
                        // precision is useful when reporting actual
                        // FPS.
                        const int64_t d = event.videoTimestampDeltaNs;
                        if(_stats.videoDeltaSamples == 0) {
                                _stats.videoDeltaMinNs = d;
                                _stats.videoDeltaMaxNs = d;
                                _stats.videoDeltaAvgNs = static_cast<double>(d);
                        } else {
                                if(d < _stats.videoDeltaMinNs) _stats.videoDeltaMinNs = d;
                                if(d > _stats.videoDeltaMaxNs) _stats.videoDeltaMaxNs = d;
                                const double n = static_cast<double>(_stats.videoDeltaSamples + 1);
                                _stats.videoDeltaAvgNs +=
                                        (static_cast<double>(d) - _stats.videoDeltaAvgNs) / n;
                        }
                        _stats.videoDeltaSamples++;
                        _stats.actualFps = _stats.videoDeltaAvgNs > 0.0
                                ? 1.0e9 / _stats.videoDeltaAvgNs
                                : 0.0;
                }
                if(event.audioTimestampDeltaValid) {
                        const int64_t d = event.audioTimestampDeltaNs;
                        if(_stats.audioDeltaSamples == 0) {
                                _stats.audioDeltaMinNs = d;
                                _stats.audioDeltaMaxNs = d;
                                _stats.audioDeltaAvgNs = static_cast<double>(d);
                        } else {
                                if(d < _stats.audioDeltaMinNs) _stats.audioDeltaMinNs = d;
                                if(d > _stats.audioDeltaMaxNs) _stats.audioDeltaMaxNs = d;
                                const double n = static_cast<double>(_stats.audioDeltaSamples + 1);
                                _stats.audioDeltaAvgNs +=
                                        (static_cast<double>(d) - _stats.audioDeltaAvgNs) / n;
                        }
                        _stats.audioDeltaSamples++;
                }
                if(event.audioSamplesValid) {
                        const int64_t n = event.audioSamplesThisFrame;
                        if(_stats.audioSamplesFrames == 0) {
                                _stats.audioSamplesMin = n;
                                _stats.audioSamplesMax = n;
                                _stats.audioSamplesAvg = static_cast<double>(n);
                        } else {
                                if(n < _stats.audioSamplesMin) _stats.audioSamplesMin = n;
                                if(n > _stats.audioSamplesMax) _stats.audioSamplesMax = n;
                                const double k = static_cast<double>(_stats.audioSamplesFrames + 1);
                                _stats.audioSamplesAvg +=
                                        (static_cast<double>(n) - _stats.audioSamplesAvg) / k;
                        }
                        _stats.audioSamplesFrames++;
                        // Expose the anchor-based cumulative totals so
                        // snapshot consumers and the periodic log can
                        // report measured rate.  These mirror the
                        // inspector-private anchor fields.
                        _stats.audioSamplesTotal  = _audioSamplesCumulative;
                        _stats.audioSamplesSpanNs = _audioSamplesAnchorSet
                                ? (_audioSamplesPreviousStampNs - _audioSamplesAnchorNs)
                                : 0;
                        _stats.measuredAudioSampleRate = _stats.audioSamplesSpanNs > 0
                                ? (static_cast<double>(_stats.audioSamplesTotal) * 1.0e9 /
                                   static_cast<double>(_stats.audioSamplesSpanNs))
                                : 0.0;
                }
                _stats.totalDiscontinuities += event.discontinuities.size();
                _stats.hasLastEvent = true;
                _stats.lastEvent    = event;
        }

        // Log every per-frame problem immediately as a warning so it
        // lands in the log at the right point in wall time.  All lines
        // share the "Frame N:" prefix so log readers can correlate
        // them with the periodic report.

        // Decode failures — the continuity check only catches failures
        // that follow a previously-successful decode.  These warnings
        // fire unconditionally so the first failure (and failures when
        // continuity is disabled) are never silent.
        if(_decodeImageData && !event.pictureDecoded) {
                promekiWarn("Frame %lld: image data band decode failed",
                            static_cast<long long>(event.frameIndex.value()));
        }
        if(_decodeLtc && !event.ltcDecoded) {
                promekiWarn("Frame %lld: LTC decode failed",
                            static_cast<long long>(event.frameIndex.value()));
        }

        // Discontinuities have pre-rendered descriptions courtesy of
        // runContinuityCheck / runAvSyncCheck, so we relay them here.
        for(const auto &d : event.discontinuities) {
                promekiWarn("Frame %lld: discontinuity: %s",
                            static_cast<long long>(event.frameIndex.value()),
                            d.description.cstr());
        }

        // Fire the per-frame callback (worker thread context — caller
        // is responsible for thread safety).
        if(_callback) {
                _callback(event);
        }

        ++_frameIndex;
        ++_framesSinceLastLog;
        cmd.currentFrame = _frameIndex;
        cmd.frameCount   = toFrameCount(_frameIndex);

        emitPeriodicLogIfDue();
        // _dropFrames is currently a no-op — the inspector is a sink so
        // there is no downstream consumer.  The flag exists for future
        // tee-style wrappers; for now we always "drop" by simply
        // returning Ok and letting cmd.frame's reference count expire.
        (void)_dropFrames;
        stampWorkEnd();
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
