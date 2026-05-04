/**
 * @file      inspectormediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <promeki/inspectormediaio.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/dir.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/videocodec.h>
#include <promeki/videodecoder.h>
#include <promeki/mediaconfig.h>
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

PROMEKI_REGISTER_MEDIAIO_FACTORY(InspectorFactory)

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
                return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        }

        // ISO-8601 UTC rendering of an epoch nanosecond value, with
        // millisecond precision — wire format that every downstream log tool
        // handles without additional parsing code.  Buffer must be at least
        // @ref IsoBufLen bytes; the extra headroom over the fixed 24-char
        // output keeps gcc's -Wformat-truncation analysis happy for
        // hypothetically-huge tm_year values.
        static constexpr size_t IsoBufLen = 64;
        void                    formatIsoUtc(int64_t epochNs, char *out, size_t outLen) {
                if (epochNs <= 0) {
                        if (outLen > 0) out[0] = '\0';
                        return;
                }
                const time_t secs = static_cast<time_t>(epochNs / 1000000000LL);
                const int    msec = static_cast<int>((epochNs / 1000000LL) % 1000LL);
                struct tm    tm;
                gmtime_r(&secs, &tm);
                std::snprintf(out, outLen, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                                                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
        }

        // Sums every plane's bytes into one count — gives the "on the wire"
        // frame size, which is what a stats-file reader most likely wants.
        // Returns 0 if the payload has no planes.  MediaPayload::size()
        // already performs the sum, so this is now a thin wrapper kept for
        // call-site clarity at the CSV-row assembly point.
        size_t payloadByteSize(const VideoPayload &vp) {
                return vp.size();
        }

} // namespace

// ---------------------------------------------------------------------------
// InspectorFactory
// ---------------------------------------------------------------------------

MediaIOFactory::Config::SpecMap InspectorFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // Inspector defaults run every *in-memory* check out of the
        // box that the TPG default produces signal for.  Excluded
        // from the default list:
        //   - @c CaptureStats — opens a file, so opt-in keeps a
        //     default-configured inspector side-effect free.
        //   - @c Ltc — the TPG default carries @c PcmMarker on
        //     every channel rather than LTC, so a default inspector
        //     against a default TPG would just count zeros for the
        //     LTC test.  Callers that pin a channel to
        //     @c AudioPattern::LTC re-enable the LTC test via the
        //     @ref MediaConfig::InspectorTests config key.
        //
        // @c AvSync IS in the default — its dependencies are
        // @c ImageData + @c AudioData, both of which the TPG
        // default produces.
        EnumList allTests = EnumList::forType<InspectorTest>();
        allTests.append(InspectorTest::ImageData);
        allTests.append(InspectorTest::AudioData);
        allTests.append(InspectorTest::AvSync);
        allTests.append(InspectorTest::Continuity);
        allTests.append(InspectorTest::Timestamp);
        allTests.append(InspectorTest::AudioSamples);
        s(MediaConfig::InspectorDropFrames, true);
        s(MediaConfig::InspectorTests, allTests);
        s(MediaConfig::InspectorImageDataRepeatLines, int32_t(16));
        s(MediaConfig::InspectorLtcChannel, int32_t(0));
        s(MediaConfig::InspectorSyncOffsetToleranceSamples, int32_t(0));
        s(MediaConfig::InspectorLogIntervalSec, 1.0);
        s(MediaConfig::InspectorStatsFile, String());
        return specs;
}

MediaIO *InspectorFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new InspectorMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ---------------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------------

InspectorMediaIO::InspectorMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

InspectorMediaIO::~InspectorMediaIO() {
        if (isOpen()) (void)close().wait();
}

void InspectorMediaIO::setEventCallback(EventCallback cb) {
        // No locking — must be called before open() per the API
        // contract.  Documenting that contract is cheaper than the
        // mutex contention on every frame.
        _callback = std::move(cb);
}

InspectorSnapshot InspectorMediaIO::snapshot() const {
        Mutex::Locker lk(_stateMutex);
        return _stats;
}

void InspectorMediaIO::resetState() {
        Mutex::Locker lk(_stateMutex);
        _stats = InspectorSnapshot{};
        _hasPreviousPicture = false;
        _hasPreviousLtc = false;
        _previousFrameNumber = 0;
        _previousStreamId = 0;
        _previousPictureTc = Timecode();
        _previousLtcTc = Timecode();
        _inferredPictureMode = Timecode::Mode();
        _hasPreviousSyncOffset = false;
        _previousSyncOffset = 0;
        _avSyncBaselineSet = false;
        _avSyncBaselinePhase = 0;
        _samplesPerFrame = 0.0;
        _hasPreviousVideoTimestamp = false;
        _hasPreviousAudioTimestamp = false;
        _previousVideoTimestampNs = 0;
        _previousAudioTimestampNs = 0;
        _frameIndex = 0;
        _decodersInitialized = false;
        _imageDataDecoder = ImageDataDecoder();
        _audioDataDecoder = AudioDataDecoder();
        _audioDataStreamStates.clear();
        _audioDataChannelActive.clear();
        _videoFrameHistory.clear();
        _ltcDecoder.reset();
        _audioStream = AudioBuffer();
        _audioStreamReady = false;
        _audioSampleRate = 0.0;
        _audioStreamAnchored = false;
        _audioStreamStartNs = 0;
        _audioCumulativeIn = 0;
        _audioCumulativeAnalyzed = 0;
        _videoPtsAnchored = false;
        _videoPtsAnchorNs = 0;
        _videoPtsAnchorFrame = FrameNumber{0};
        _frameRateConfirmed = false;
        _avBaselineSet = false;
        _avBaselineOffsetNs = 0;
        _hasLastAudioPtsForAv = false;
        _lastAudioPtsForAvNs = 0;
        _lastLogWallSec = monotonicWallSeconds();
        _framesSinceLastLog = 0;
}

Error InspectorMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        _dropFrames = cfg.getAs<bool>(MediaConfig::InspectorDropFrames, true);
        _imageDataRepeatLines = cfg.getAs<int>(MediaConfig::InspectorImageDataRepeatLines, 16);
        _ltcChannel = cfg.getAs<int>(MediaConfig::InspectorLtcChannel, 0);
        _syncOffsetToleranceSamples = cfg.getAs<int>(MediaConfig::InspectorSyncOffsetToleranceSamples, 0);
        _audioPtsToleranceNs =
                cfg.getAs<int64_t>(MediaConfig::InspectorAudioPtsToleranceNs, int64_t(5'000'000));
        _videoPtsToleranceNs =
                cfg.getAs<int64_t>(MediaConfig::InspectorVideoPtsToleranceNs, int64_t(5'000'000));
        _logIntervalSec = cfg.getAs<double>(MediaConfig::InspectorLogIntervalSec, 1.0);

        // Cache the upstream frame rate for both the per-essence PTS
        // prediction and the marker-based A/V sync's cadence math —
        // rational rates (29.97 NTSC, 59.94, 23.98) need the exact
        // num/den pair to predict the audio sample cadence
        // (1601/1602/1601/1602/1602 at 48k/29.97), which the wobble-
        // free offset computation in @ref runAvSyncCheck subtracts
        // out so a clean stream reports 0 regardless of rate.
        _frameRate = cmd.pendingMediaDesc.frameRate();
        if (_frameRate.isValid()) {
                const Duration d = _frameRate.frameDuration();
                _videoFrameDurationNs = static_cast<double>(d.nanoseconds());
        } else {
                _videoFrameDurationNs = 0.0;
        }

        // Resolve the test selection.  The configured list drives
        // which tests run: each entry turns one test on, anything
        // absent stays off.  Test dependencies (AvSync → ImageData
        // + Ltc, Continuity → ImageData) are auto-added after the
        // explicit list has been interpreted so callers never have
        // to know about them.  The default config (set in
        // formatDesc) carries every test, so a default-configured
        // inspector runs the full suite.
        _decodeImageData = false;
        _decodeAudioData = false;
        _decodeLtc = false;
        _checkAvSync = false;
        _checkContinuity = false;
        _checkTimestamp = false;
        _checkAudioSamples = false;
        _checkCaptureStats = false;
        const EnumList testsCfg = cfg.get(MediaConfig::InspectorTests).get<EnumList>();
        for (size_t i = 0; i < testsCfg.size(); ++i) {
                const int v = testsCfg.at(i).value();
                if (v == InspectorTest::ImageData.value())
                        _decodeImageData = true;
                else if (v == InspectorTest::Ltc.value())
                        _decodeLtc = true;
                else if (v == InspectorTest::AvSync.value())
                        _checkAvSync = true;
                else if (v == InspectorTest::Continuity.value())
                        _checkContinuity = true;
                else if (v == InspectorTest::Timestamp.value())
                        _checkTimestamp = true;
                else if (v == InspectorTest::AudioSamples.value())
                        _checkAudioSamples = true;
                else if (v == InspectorTest::CaptureStats.value())
                        _checkCaptureStats = true;
                else if (v == InspectorTest::AudioData.value())
                        _decodeAudioData = true;
        }
        if (_checkAvSync) {
                // Marker-based A/V sync: compares the picture data
                // band's frame number against the AudioDataEncoder
                // codeword's frame number and uses the video
                // MediaTimeStamp + audio stream sample anchor for the
                // wall-clock conversion.  Both decoders are required;
                // LTC is no longer involved.
                _decodeImageData = true;
                _decodeAudioData = true;
        }
        if (_checkContinuity) {
                _decodeImageData = true;
        }

        resetState();

        // Open the CaptureStats output file after resetState so any
        // leftover state from a previous run is cleared first.  A file
        // open error aborts open() so the caller sees the failure
        // immediately rather than discovering later that rows were
        // silently dropped.
        if (_checkCaptureStats) {
                String configured = cfg.getAs<String>(MediaConfig::InspectorStatsFile, String());
                Error  ferr = openCaptureStatsFile(configured);
                if (ferr.isError()) return ferr;
        }

        _isOpen = true;

        // Dump the resolved configuration into the log so any later
        // periodic reports can be interpreted in context — especially
        // useful when post-mortem'ing a recorded log file.  Each line
        // shares the "config:" prefix so the whole block is scannable.
        promekiInfo("Image data decode     = %s", _decodeImageData ? "enabled" : "disabled");
        promekiInfo("Audio data decode     = %s", _decodeAudioData ? "enabled" : "disabled");
        promekiInfo("LTC decode            = %s", _decodeLtc ? "enabled" : "disabled");
        promekiInfo("LTC channel           = %d", _ltcChannel);
        promekiInfo("A/V sync check        = %s", _checkAvSync ? "enabled" : "disabled");
        promekiInfo("A/V sync jitter tol   = %d samples", _syncOffsetToleranceSamples);
        promekiInfo("Continuity check      = %s", _checkContinuity ? "enabled" : "disabled");
        promekiInfo("Timestamp check       = %s", _checkTimestamp ? "enabled" : "disabled");
        promekiInfo("Audio samples check   = %s", _checkAudioSamples ? "enabled" : "disabled");
        promekiInfo("Capture stats check   = %s%s%s", _checkCaptureStats ? "enabled (writing " : "disabled",
                    _checkCaptureStats ? _statsFilePath.cstr() : "", _checkCaptureStats ? ")" : "");
        promekiInfo("Image data band       = %d scan lines per item", _imageDataRepeatLines);
        promekiInfo("Audio PTS tolerance   = %lld ns (%.3f ms)",
                    static_cast<long long>(_audioPtsToleranceNs),
                    _audioPtsToleranceNs / 1.0e6);
        promekiInfo("Video PTS tolerance   = %lld ns (%.3f ms)",
                    static_cast<long long>(_videoPtsToleranceNs),
                    _videoPtsToleranceNs / 1.0e6);
        promekiInfo("Drop frames           = %s", _dropFrames ? "yes" : "no");
        promekiInfo("Log interval          = %.2f seconds", _logIntervalSec);

        // Inspector is a passive sink — register one in a single-port
        // group with the upstream-supplied desc.  The group reports
        // unbounded length and no seek capability.
        MediaIOPortGroup *group = addPortGroup("inspector");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(cmd.pendingMediaDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error InspectorMediaIO::executeCmd(MediaIOCommandClose &cmd) {
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
        promekiInfo("  Total frames processed: %lld", static_cast<long long>(snap.framesProcessed.value()));

        if (_decodeImageData) {
                const double pct = snap.framesProcessed.value() > 0
                                           ? 100.0 * snap.framesWithPictureData.value() / snap.framesProcessed.value()
                                           : 0.0;
                if (snap.framesWithPictureData == snap.framesProcessed) {
                        promekiInfo("  Image data band: decoded %lld / %lld frames (%.1f%%)",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()), pct);
                } else {
                        promekiWarn(
                                "  Image data band: decoded %lld / %lld frames (%.1f%%) — "
                                "%lld frames failed to decode",
                                static_cast<long long>(snap.framesWithPictureData.value()),
                                static_cast<long long>(snap.framesProcessed.value()), pct,
                                static_cast<long long>((snap.framesProcessed - snap.framesWithPictureData).value()));
                }
                if (snap.hasLastEvent && snap.lastEvent.pictureDecoded) {
                        promekiInfo("  Last picture: streamID 0x%08X, frameNo %u, TC %s",
                                    snap.lastEvent.pictureStreamId, snap.lastEvent.pictureFrameNumber,
                                    renderTc(snap.lastEvent.pictureTimecode).cstr());
                }
        }

        if (_decodeLtc) {
                const double pct = snap.framesProcessed.value() > 0
                                           ? 100.0 * snap.framesWithLtc.value() / snap.framesProcessed.value()
                                           : 0.0;
                if (snap.framesWithLtc == snap.framesProcessed) {
                        promekiInfo("  LTC: decoded %lld / %lld frames (%.1f%%)",
                                    static_cast<long long>(snap.framesWithLtc.value()),
                                    static_cast<long long>(snap.framesProcessed.value()), pct);
                } else {
                        promekiWarn("  LTC: decoded %lld / %lld frames (%.1f%%) — "
                                    "%lld frames failed to decode",
                                    static_cast<long long>(snap.framesWithLtc.value()),
                                    static_cast<long long>(snap.framesProcessed.value()), pct,
                                    static_cast<long long>((snap.framesProcessed - snap.framesWithLtc).value()));
                }
                if (snap.hasLastEvent && snap.lastEvent.ltcDecoded) {
                        promekiInfo("  Last LTC: TC %s", renderTc(snap.lastEvent.ltcTimecode).cstr());
                }
        }

        if (_checkAvSync && snap.hasLastEvent && snap.lastEvent.avSyncValid) {
                const int64_t s = snap.lastEvent.avSyncOffsetSamples;
                if (s == 0) {
                        promekiInfo("  A/V Sync: audio and video locked (0 samples)");
                } else {
                        const int64_t absSamples = s < 0 ? -s : s;
                        const double  frames =
                                _samplesPerFrame > 0.0 ? static_cast<double>(absSamples) / _samplesPerFrame : 0.0;
                        const char *direction = (s > 0) ? "Video leads audio" : "Audio leads video";
                        promekiInfo("  A/V Sync: %s by %lld samples, %.4f frames", direction,
                                    static_cast<long long>(absSamples), frames);
                }
        }

        if (_checkTimestamp) {
                if (snap.framesWithVideoTimestamp == snap.framesProcessed) {
                        promekiInfo("  Video timestamps: all %lld frames stamped",
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else {
                        promekiWarn(
                                "  Video timestamps: %lld / %lld frames stamped "
                                "(%lld missing)",
                                static_cast<long long>(snap.framesWithVideoTimestamp.value()),
                                static_cast<long long>(snap.framesProcessed.value()),
                                static_cast<long long>((snap.framesProcessed - snap.framesWithVideoTimestamp).value()));
                }
                if (snap.videoDeltaSamples > 0) {
                        promekiInfo("  Video delta: min %.3f ms / avg %.3f ms / max %.3f ms  "
                                    "(actual %.4f fps over %lld samples)",
                                    snap.videoDeltaMinNs / 1.0e6, snap.videoDeltaAvgNs / 1.0e6,
                                    snap.videoDeltaMaxNs / 1.0e6, snap.actualFps,
                                    static_cast<long long>(snap.videoDeltaSamples));
                }
                if (snap.framesWithAudioTimestamp == snap.framesProcessed) {
                        promekiInfo("  Audio timestamps: all %lld frames stamped",
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else if (snap.framesWithAudioTimestamp > 0) {
                        promekiWarn(
                                "  Audio timestamps: %lld / %lld frames stamped "
                                "(%lld missing)",
                                static_cast<long long>(snap.framesWithAudioTimestamp.value()),
                                static_cast<long long>(snap.framesProcessed.value()),
                                static_cast<long long>((snap.framesProcessed - snap.framesWithAudioTimestamp).value()));
                }
                if (snap.audioDeltaSamples > 0) {
                        promekiInfo("  Audio delta: min %.3f ms / avg %.3f ms / max %.3f ms  "
                                    "(%lld samples)",
                                    snap.audioDeltaMinNs / 1.0e6, snap.audioDeltaAvgNs / 1.0e6,
                                    snap.audioDeltaMaxNs / 1.0e6, static_cast<long long>(snap.audioDeltaSamples));
                }
                if (snap.audioPtsJitterSamples > 0) {
                        promekiInfo("  Audio PTS jitter: min %+.3f ms / avg %+.3f ms / max %+.3f ms  "
                                    "(over %lld chunks, %lld re-anchors)",
                                    snap.audioPtsJitterMinNs / 1.0e6, snap.audioPtsJitterAvgNs / 1.0e6,
                                    snap.audioPtsJitterMaxNs / 1.0e6,
                                    static_cast<long long>(snap.audioPtsJitterSamples),
                                    static_cast<long long>(snap.audioReanchorCount));
                }
                if (snap.videoPtsJitterSamples > 0) {
                        promekiInfo("  Video PTS jitter: min %+.3f ms / avg %+.3f ms / max %+.3f ms  "
                                    "(over %lld frames, %lld re-anchors)",
                                    snap.videoPtsJitterMinNs / 1.0e6, snap.videoPtsJitterAvgNs / 1.0e6,
                                    snap.videoPtsJitterMaxNs / 1.0e6,
                                    static_cast<long long>(snap.videoPtsJitterSamples),
                                    static_cast<long long>(snap.videoReanchorCount));
                }
                if (snap.avPtsDriftSamples > 0) {
                        promekiInfo("  A/V PTS drift: min %+.3f ms / avg %+.3f ms / max %+.3f ms  "
                                    "(baseline videoPts-audioPts %+.3f ms, %lld samples)",
                                    snap.avPtsDriftMinNs / 1.0e6, snap.avPtsDriftAvgNs / 1.0e6,
                                    snap.avPtsDriftMaxNs / 1.0e6, snap.avBaselineOffsetNs / 1.0e6,
                                    static_cast<long long>(snap.avPtsDriftSamples));
                }
        }

        if (_checkAudioSamples) {
                if (snap.audioSamplesFrames == 0) {
                        promekiWarn("  Audio samples: no audio frames observed");
                } else {
                        promekiInfo("  Audio samples: per-frame min/avg/max = "
                                    "%lld / %.2f / %lld  (%lld frames)",
                                    static_cast<long long>(snap.audioSamplesMin), snap.audioSamplesAvg,
                                    static_cast<long long>(snap.audioSamplesMax),
                                    static_cast<long long>(snap.audioSamplesFrames));
                        if (snap.measuredAudioSampleRate > 0.0) {
                                promekiInfo("  Audio samples: measured rate %.2f Hz "
                                            "(%lld samples over %.3f s)",
                                            snap.measuredAudioSampleRate,
                                            static_cast<long long>(snap.audioSamplesTotal),
                                            snap.audioSamplesSpanNs / 1.0e9);
                        }
                }
        }

        if (_checkContinuity) {
                if (snap.totalDiscontinuities == 0) {
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

void InspectorMediaIO::decompressImages(Frame &frame) {
        // Walk every video payload in the frame.  Any that carries
        // a compressed PixelFormat is replaced in-place with an
        // uncompressed @ref UncompressedVideoPayload produced by a
        // short-lived @ref VideoDecoder session.  The target format
        // is the first entry in the codec's decodeTargets list —
        // guaranteed to be a native decode format so the decoder
        // can skip any post-decode CSC.  When the codec advertises
        // no decode targets, fall back to @c RGBA8_sRGB and let the
        // payload-native CSC land it via
        // @ref UncompressedVideoPayload::convert.
        for (MediaPayload::Ptr &payloadPtr : frame.payloadList()) {
                if (!payloadPtr.isValid()) continue;
                const auto *cvpConst = payloadPtr->as<CompressedVideoPayload>();
                if (cvpConst == nullptr) continue;

                const PixelFormat &srcPd = cvpConst->desc().pixelFormat();
                const VideoCodec   codec = srcPd.videoCodec();
                if (!codec.isValid() || !codec.canDecode()) {
                        promekiWarn("InspectorMediaIO: no decoder for "
                                    "%s — skipping decompress on frame %lld",
                                    srcPd.name().cstr(), static_cast<long long>(_frameIndex.value()));
                        continue;
                }

                PixelFormat targetPd;
                if (!srcPd.decodeTargets().isEmpty()) {
                        targetPd = PixelFormat(srcPd.decodeTargets()[0]);
                } else {
                        targetPd = PixelFormat(PixelFormat::RGBA8_sRGB);
                }

                MediaConfig cfg;
                cfg.set(MediaConfig::VideoSize, cvpConst->desc().size());
                // Ask the decoder for a native output first; we CSC to
                // targetPd below if the decoder's emitted format differs.
                const auto supportedOut = codec.decoderSupportedOutputs();
                bool       targetOk = supportedOut.isEmpty();
                for (const PixelFormat &s : supportedOut) {
                        if (s == targetPd) {
                                targetOk = true;
                                break;
                        }
                }
                if (targetOk) cfg.set(MediaConfig::OutputPixelFormat, targetPd);

                auto decRes = codec.createDecoder(&cfg);
                if (error(decRes).isError()) {
                        promekiWarn("InspectorMediaIO: createDecoder "
                                    "failed for %s on frame %lld",
                                    srcPd.name().cstr(), static_cast<long long>(_frameIndex.value()));
                        continue;
                }
                VideoDecoder *dec = value(decRes);

                // Re-cast to mutable Ptr for the decoder's const-Ptr
                // parameter.  The decoder doesn't mutate the payload;
                // sharedPointerCast keeps ownership shared.
                auto cvpPtr = sharedPointerCast<CompressedVideoPayload>(payloadPtr);
                if (!cvpPtr.isValid()) {
                        delete dec;
                        continue;
                }

                Error                         err = dec->submitPayload(cvpPtr);
                UncompressedVideoPayload::Ptr out;
                if (err.isOk()) out = dec->receiveVideoPayload();
                delete dec;

                if (!out.isValid()) {
                        promekiWarn("InspectorMediaIO: failed to decompress "
                                    "%s payload on frame %lld — downstream checks "
                                    "may report failures",
                                    srcPd.name().cstr(), static_cast<long long>(_frameIndex.value()));
                        continue;
                }

                // Finish the hop to targetPd via payload-native CSC
                // when the decoder didn't emit it directly.
                if (out->desc().pixelFormat() != targetPd) {
                        auto csc = out->convert(targetPd, out->metadata());
                        if (csc.isValid()) out = csc;
                }

                // Carry the PTS from the compressed source onto the
                // decoded payload.  The outer MediaIO already stamped
                // a synthetic PTS on the compressed payload in
                // writeFrame, but replacing the Ptr here drops it —
                // and the timestamp continuity check would then
                // report every decoded frame as "missing PTS".
                const MediaTimeStamp &srcPts = cvpConst->pts();
                if (srcPts.isValid()) {
                        out.modify()->setPts(srcPts);
                }

                payloadPtr = out;
        }
}

void InspectorMediaIO::initDecoders(const Frame &frame) {
        // Frame-rate latch / re-latch from the per-frame
        // @ref Metadata::FrameRate.  The open-time value pulled from
        // @c pendingMediaDesc is speculative — backends like the NDI
        // receiver only learn the real source rate after the first
        // SDK frame arrives, so they publish a placeholder (30/1) at
        // open time which would otherwise drive the cadence math off
        // for the entire run on a 29.97 source.  The first authoritative
        // metadata-published rate that disagrees silently relatches;
        // any later mid-run change warns and resets the A/V sync
        // baseline so the @ref FrameRate::cumulativeTicks math runs
        // against the new rate.
        {
                const FrameRate fr = frame.mediaDesc().frameRate();
                if (fr.isValid() && fr != _frameRate) {
                        if (_frameRateConfirmed) {
                                promekiWarn("InspectorMediaIO: upstream frame rate changed "
                                            "mid-run: %s -> %s — resetting A/V sync baseline",
                                            _frameRate.toString().cstr(), fr.toString().cstr());
                                _avSyncBaselineSet = false;
                                _avSyncBaselinePhase = 0;
                                _hasPreviousSyncOffset = false;
                                _previousSyncOffset = 0;
                        }
                        _frameRate = fr;
                        _videoFrameDurationNs =
                                static_cast<double>(fr.frameDuration().nanoseconds());
                }
                if (fr.isValid()) _frameRateConfirmed = true;
        }

        // Picture decoder needs a valid image to learn the descriptor.
        // It can latch on the first frame with video; once latched we
        // skip this check on subsequent calls.
        if (_decodeImageData && !_imageDataDecoder.isValid()) {
                auto vids = frame.videoPayloads();
                if (!vids.isEmpty() && vids[0].isValid()) {
                        const ImageDesc &d = vids[0]->desc();
                        _imageDataDecoder = ImageDataDecoder(d);
                        if (!_imageDataDecoder.isValid()) {
                                promekiWarn("InspectorMediaIO: image data decoder "
                                            "could not be initialised for %s",
                                            d.toString().cstr());
                        }
                }
        }

        // LTC decoder + audio stream both need the first audio chunk
        // to learn the sample rate.  Network-fed inspectors may see
        // several video frames before the first audio arrives, so we
        // try every call until one of them latches.
        if (!_audioStreamReady) {
                auto auds = frame.audioPayloads();
                if (!auds.isEmpty() && auds[0].isValid()) {
                        const AudioDesc &srcDesc = auds[0]->desc();
                        const int        rate = static_cast<int>(srcDesc.sampleRate());
                        const unsigned   ch = srcDesc.channels();
                        if (rate > 0 && ch > 0) {
                                // Stream storage: native float interleaved
                                // at the source's rate / channel count.
                                // 1 s of headroom is plenty — analyses
                                // drain on every writeFrame call so the
                                // ring rarely holds more than one video
                                // frame's worth.
                                AudioDesc bufDesc(AudioFormat(AudioFormat::NativeFloat),
                                                  static_cast<float>(rate), ch);
                                _audioStream = AudioBuffer(bufDesc, static_cast<size_t>(rate));
                                _audioStreamReady = true;
                                _audioSampleRate = static_cast<double>(rate);
                                if (_decodeLtc && !_ltcDecoder) {
                                        _ltcDecoder = LtcDecoder::UPtr::create(rate);
                                }
                                if (_decodeAudioData && !_audioDataDecoder.isValid()) {
                                        // Build the data decoder against
                                        // the source descriptor — that
                                        // way every per-channel decode
                                        // path runs on the chunk's
                                        // native format without an
                                        // intermediate conversion.
                                        _audioDataDecoder = AudioDataDecoder(srcDesc);
                                        if (!_audioDataDecoder.isValid()) {
                                                promekiWarn("InspectorMediaIO: audio data "
                                                            "decoder could not be initialised "
                                                            "for %s",
                                                            srcDesc.toString().cstr());
                                        }
                                }
                        }
                }
        }

        _decodersInitialized = true;
}

// ---------------------------------------------------------------------------
// Audio stream ingest — converts each frame's audio payload(s) into a
// continuous stream view via @ref _audioStream, decoupling per-frame
// chunk shape from analyses that want to see audio as a stream.
// Also runs the audio PTS jitter / re-anchor check.
// ---------------------------------------------------------------------------

void InspectorMediaIO::ingestAudio(const Frame &frame, InspectorEvent &event) {
        if (!_audioStreamReady) return; // initDecoders hasn't latched yet

        auto auds = frame.audioPayloads();
        if (auds.isEmpty()) return;
        const AudioPayload::Ptr &ap = auds[0];
        if (!ap.isValid()) return;
        const auto *uap = ap->as<PcmAudioPayload>();
        if (uap == nullptr) return; // streaming only meaningful for PCM

        const AudioDesc &srcDesc = uap->desc();
        if (srcDesc.format().isPlanar()) {
                // Separately-allocated planar payloads need a transpose
                // step we don't yet implement here; warn once and skip.
                // NDI's interleaved-on-drain layout and the TPG's
                // PCMI_Float32LE output don't trigger this path.
                promekiWarn("InspectorMediaIO: planar PCM audio not yet "
                            "supported in audio stream — skipping ingest");
                return;
        }

        const size_t samples = uap->sampleCount();

        // PTS handling first, before pushing samples — the prediction
        // for "the first sample of this chunk" uses the cumulative-in
        // count BEFORE the push.
        const MediaTimeStamp &mts = uap->pts();
        if (mts.isValid()) {
                const int64_t actualPts =
                        mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
                if (!_audioStreamAnchored) {
                        // First valid PTS — anchor stream sample 0 here.
                        _audioStreamStartNs = actualPts;
                        _audioStreamAnchored = true;
                } else if (_audioSampleRate > 0.0) {
                        const double  samplesBefore = static_cast<double>(_audioCumulativeIn);
                        const int64_t predictedPts =
                                _audioStreamStartNs +
                                static_cast<int64_t>(samplesBefore * 1.0e9 / _audioSampleRate);
                        const int64_t jitter = actualPts - predictedPts;
                        event.audioPtsJitterValid = true;
                        event.audioPtsJitterNs = jitter;

                        const int64_t absJitter = jitter < 0 ? -jitter : jitter;
                        if (absJitter > _audioPtsToleranceNs) {
                                // Re-anchor: trust the new PTS as the
                                // wall-clock time of the chunk's first
                                // sample, and back-compute a new stream
                                // start so subsequent predictions stay
                                // consistent with the cumulative count.
                                _audioStreamStartNs =
                                        actualPts - static_cast<int64_t>(samplesBefore * 1.0e9 /
                                                                         _audioSampleRate);

                                InspectorDiscontinuity d;
                                d.kind = InspectorDiscontinuity::AudioTimestampReanchor;
                                d.previousValue = String::sprintf(
                                        "%lld", static_cast<long long>(predictedPts));
                                d.currentValue = String::sprintf(
                                        "%lld", static_cast<long long>(actualPts));
                                d.description = String::sprintf(
                                        "Audio PTS diverged from prediction by %+.3f ms "
                                        "(tolerance %.3f ms) — re-anchoring stream",
                                        jitter / 1.0e6, _audioPtsToleranceNs / 1.0e6);
                                event.discontinuities.pushToBack(d);
                        }
                }
                _hasLastAudioPtsForAv = true;
                _lastAudioPtsForAvNs = actualPts;
        }

        if (samples == 0) return;

        // The cumulative-in counter drives downstream PTS-jitter
        // predictions, so it advances on every chunk that arrived
        // regardless of which decoders are wired up.
        _audioCumulativeIn += static_cast<int64_t>(samples);

        // The @ref _audioStream FIFO is only drained by
        // @ref runLtcCheck; pushing samples in when no consumer is
        // wired up would fill the ring until @c push returns
        // @c NoSpace and frames start dropping.  Skip the push when
        // LTC is off — the audio data check uses its own per-channel
        // accumulators (see @ref runAudioDataCheck).
        if (!_decodeLtc) return;
        if (uap->planeCount() == 0) return;
        BufferView::Entry plane = uap->plane(0);
        if (!plane.isValid()) return;

        Error err = _audioStream.push(plane.data(), samples, srcDesc);
        if (err.isError()) {
                promekiWarn("InspectorMediaIO: audio stream push failed (%s) — "
                            "%zu samples dropped on frame %lld",
                            err.name().cstr(), samples,
                            static_cast<long long>(_frameIndex.value()));
        }
}

// ---------------------------------------------------------------------------
// Per-frame check execution
// ---------------------------------------------------------------------------

void InspectorMediaIO::runImageDataCheck(const Frame &frame, InspectorEvent &event) {
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

        if (!_imageDataDecoder.isValid()) return;
        auto vids = frame.videoPayloads();
        if (vids.isEmpty()) return;
        const VideoPayload::Ptr &vp = vids[0];
        if (!vp.isValid()) return;
        const auto *uvp = vp->as<UncompressedVideoPayload>();
        if (uvp == nullptr) return; // picture-data band decoder runs on raster only

        // Two TPG-convention bands: frame ID at lines [0, N), timecode
        // at lines [N, 2N), where N = _imageDataRepeatLines.
        List<ImageDataDecoder::Band> bands;
        bands.pushToBack({0, static_cast<uint32_t>(_imageDataRepeatLines)});
        bands.pushToBack({static_cast<uint32_t>(_imageDataRepeatLines), static_cast<uint32_t>(_imageDataRepeatLines)});

        ImageDataDecoder::DecodedList items;
        Error                         err = _imageDataDecoder.decode(*uvp, bands, items);
        if (err.isError() || items.size() != 2) return;
        if (items[0].error.isError() || items[1].error.isError()) return;

        // The picture timecode mode is unknown to the decoder (the
        // wire format only carries digits + flags), so we unpack with
        // the inspector-side default Vitc mode and an unknown Mode —
        // the resulting Timecode will report 29.97 DF if the DF flag
        // was set, or carry digits-only otherwise.  A failure here
        // (which would be unusual — the BCD unpack only fails on a
        // DF/rate inconsistency) drops us into the same all-or-nothing
        // bail-out path so partial fields don't leak out.
        auto rt = Timecode::fromBcd64(items[1].payload);
        if (rt.second().isError()) return;

        // Wire format: [stream:8][channel:8][frame:48].  Video data
        // bands always carry @c channel=0; the audio @c PcmMarker
        // pattern reuses the same word with the channel index in
        // bits 48..55.  We extract @c stream and @c frame here; the
        // channel byte is checked by the per-channel audio data
        // decode pass downstream.
        const uint64_t frameId = items[0].payload;
        event.pictureFrameNumber = static_cast<uint32_t>(frameId & 0xffffffffu);
        event.pictureStreamId = static_cast<uint32_t>((frameId >> 56) & 0xffu);
        event.pictureTimecode = rt.first();
        event.pictureDecoded = true;

        // Record this frame in the marker-based A/V sync history.
        // The audio-side codeword decode (which may lag by a few
        // frames under bursty audio) looks the matching video frame
        // up by the same 48-bit frame field + stream byte.  We push
        // even when the video MediaTimeStamp is missing — the entry
        // is still useful for *finding* a match later, and the sync
        // computation downstream skips the offset calculation when
        // the wall-clock anchor isn't present.
        if (_checkAvSync) {
                VideoFrameRecord rec;
                rec.frame48 = frameId & 0x0000ffffffffffffULL;
                rec.streamId = static_cast<uint8_t>((frameId >> 56) & 0xffu);
                const MediaTimeStamp &vMts = vp->pts();
                rec.videoWallNs =
                        vMts.isValid() ? vMts.timeStamp().nanoseconds() + vMts.offset().nanoseconds() : 0;
                _videoFrameHistory.pushToBack(rec);
                while (_videoFrameHistory.size() > kVideoFrameHistoryMax) {
                        _videoFrameHistory.remove(0);
                }
        }
}

void InspectorMediaIO::runAudioDataCheck(const Frame &frame, InspectorEvent &event) {
        event.audioDataDecoderEnabled = true;
        if (!_audioDataDecoder.isValid()) return;

        const auto auds = frame.audioPayloads();
        if (auds.isEmpty()) return;
        const auto &ap = auds[0];
        if (!ap.isValid()) return;
        const auto *pcm = ap->as<PcmAudioPayload>();
        if (pcm == nullptr) return;

        const AudioDesc &desc = pcm->desc();
        if (desc.format() != _audioDataDecoder.desc().format() || desc.sampleRate() != _audioDataDecoder.desc().sampleRate() ||
            desc.channels() != _audioDataDecoder.desc().channels()) {
                // Source descriptor changed mid-stream — rebuild the
                // decoder against the new shape on the next frame and
                // drop any partial stream states since they were sized
                // for the old shape.
                _audioDataDecoder = AudioDataDecoder();
                _audioDataStreamStates.clear();
                _audioDataChannelActive.clear();
                return;
        }

        const size_t channels = desc.channels();
        if (channels == 0) return;
        const size_t samples = pcm->sampleCount();
        const size_t bps = desc.format().bytesPerSample();
        if (bps == 0) return;
        const size_t bufferSamples = pcm->sampleCount();
        const size_t stride = desc.bytesPerSampleStride();
        if (pcm->planeCount() < 1) return;
        const uint8_t *base = pcm->data()[0].data();
        if (base == nullptr) return;

        // Lazy-grow stream states to match the channel count.  Stays
        // pinned to the source channel count for the lifetime of this
        // decoder instance — a channel-count change rebuilds the
        // decoder above and drops the states with it.
        if (_audioDataStreamStates.size() != channels) {
                _audioDataStreamStates.clear();
                _audioDataStreamStates.resize(channels);
        }
        if (_audioDataChannelActive.size() != channels) {
                _audioDataChannelActive.clear();
                _audioDataChannelActive.resize(channels, 0);
        }

        std::vector<uint8_t> rawScratch;
        std::vector<float>   floatScratch;

        event.audioChannelMarkers.clear();
        event.audioChannelMarkers.reserve(channels);
        bool anyDecoded = false;

        for (uint32_t ch = 0; ch < channels; ++ch) {
                // Extract this frame's chunk for the channel into a
                // contiguous float scratch buffer.  Empty when the
                // frame has no audio (bursty delivery) — decodeAll
                // will then run a pure trim cycle on the existing
                // accumulator without appending.
                floatScratch.clear();
                if (samples > 0) {
                        floatScratch.resize(samples);
                        const uint8_t *channelBase = base + desc.channelBufferOffset(ch, bufferSamples);
                        if (stride == bps) {
                                desc.format().samplesToFloat(floatScratch.data(), channelBase, samples);
                        } else {
                                rawScratch.resize(samples * bps);
                                for (size_t i = 0; i < samples; ++i) {
                                        std::memcpy(rawScratch.data() + i * bps, channelBase + i * stride, bps);
                                }
                                desc.format().samplesToFloat(floatScratch.data(), rawScratch.data(), samples);
                        }
                }

                AudioDataDecoder::DecodedList items;
                _audioDataDecoder.decodeAll(_audioDataStreamStates[ch], floatScratch.data(),
                                            floatScratch.size(), items);

                // Per-codeword anomaly check.  The
                // @ref AudioDataLengthAnomaly tolerance is set to
                // ±20 % of the expected packet sample count — wide
                // enough to absorb any normal SRC ratio (48 ↔ 44.1
                // is ~9 % one-way and round-trips to ~0 %), tight
                // enough to flag genuine rate distortion that the
                // ±50 % bandwidth gate inside the decoder lets pass.
                const int64_t expectedPacketSamples =
                        static_cast<int64_t>(_audioDataDecoder.expectedSamplesPerBit()) *
                        static_cast<int64_t>(AudioDataDecoder::BitsPerPacket);
                const int64_t lengthTolerance = expectedPacketSamples / 5;

                InspectorEvent::AudioChannelMarker m;
                m.packetsDecoded = static_cast<uint32_t>(items.size());
                for (const auto &r : items) {
                        if (r.error.isOk()) {
                                m.decoded = true;
                                m.streamId = static_cast<uint8_t>((r.payload >> 56) & 0xffu);
                                m.encodedChannel = static_cast<uint8_t>((r.payload >> 48) & 0xffu);
                                m.frameNumber = r.payload & 0x0000ffffffffffffULL;
                                m.streamSampleStart = r.streamSampleStart;
                                m.channelMatches = (m.encodedChannel == static_cast<uint8_t>(ch));
                                // Latch "this channel carries
                                // codewords" so subsequent failures
                                // on it surface as anomalies.
                                _audioDataChannelActive[ch] = 1;
                                if (!m.channelMatches) {
                                        InspectorDiscontinuity d;
                                        d.kind = InspectorDiscontinuity::AudioChannelMismatch;
                                        d.previousValue = String::sprintf("ch=%u", ch);
                                        d.currentValue =
                                                String::sprintf("encodedCh=%u", m.encodedChannel);
                                        d.description = String::sprintf(
                                                "Audio data marker channel "
                                                "mismatch on ch=%u: encoded "
                                                "channel byte was %u",
                                                ch, m.encodedChannel);
                                        event.discontinuities.pushToBack(d);
                                }
                                anyDecoded = true;
                        } else if (r.error == Error::CorruptData) {
                                ++m.packetsCorrupt;
                                // Sync-byte or CRC failure on a
                                // codeword that otherwise looked
                                // real (passed findSync + the
                                // bandwidth gate).  Surface as a
                                // decode-failure discontinuity, but
                                // only after we've previously seen
                                // a clean codeword on this channel
                                // — without that latch a
                                // non-PcmMarker channel (LTC,
                                // continuous tone) would emit
                                // anomalies whenever findSync's
                                // 4-transition search happens to
                                // produce an in-band samplesPerBit.
                                // Bandwidth-out-of-range items show
                                // up as Error::OutOfRange and are
                                // skipped on the @c else above.
                                if (!_audioDataChannelActive[ch]) continue;
                                InspectorDiscontinuity d;
                                d.kind = InspectorDiscontinuity::AudioDataDecodeFailure;
                                d.previousValue = String::sprintf("ch=%u", ch);
                                d.currentValue = String::sprintf(
                                        "sync=0x%X decodedCrc=0x%02X expectedCrc=0x%02X",
                                        r.decodedSync, r.decodedCrc, r.expectedCrc);
                                d.description = String::sprintf(
                                        "Audio data codeword failed validation on ch=%u: "
                                        "sync=0x%X decodedCrc=0x%02X expectedCrc=0x%02X",
                                        ch, r.decodedSync, r.decodedCrc, r.expectedCrc);
                                event.discontinuities.pushToBack(d);
                        }

                        // Length-anomaly check fires only on channels
                        // we've previously confirmed carry codewords
                        // (same active-latch logic as the decode
                        // failure case above) and only for items
                        // whose pitch landed inside the bandwidth
                        // gate.  A packet that decoded inside the
                        // gate but at a wildly different sample
                        // length signals something downstream is
                        // rate-shifting the audio in a way that
                        // could eventually cause decode failures.
                        if (_audioDataChannelActive[ch] && r.error != Error::OutOfRange &&
                            r.packetSampleCount > 0) {
                                const int64_t deviation = r.packetSampleCount - expectedPacketSamples;
                                const int64_t absDev = deviation < 0 ? -deviation : deviation;
                                if (absDev > lengthTolerance) {
                                        InspectorDiscontinuity d;
                                        d.kind = InspectorDiscontinuity::AudioDataLengthAnomaly;
                                        d.previousValue = String::sprintf(
                                                "expected=%lld",
                                                static_cast<long long>(expectedPacketSamples));
                                        d.currentValue = String::sprintf(
                                                "actual=%lld",
                                                static_cast<long long>(r.packetSampleCount));
                                        d.description = String::sprintf(
                                                "Audio data codeword length on ch=%u "
                                                "deviated from expected by %+lld samples "
                                                "(expected %lld, got %lld, tolerance ±%lld)",
                                                ch, static_cast<long long>(deviation),
                                                static_cast<long long>(expectedPacketSamples),
                                                static_cast<long long>(r.packetSampleCount),
                                                static_cast<long long>(lengthTolerance));
                                        event.discontinuities.pushToBack(d);
                                }
                        }
                }
                event.audioChannelMarkers.pushToBack(m);
        }
        event.audioDataDecoded = anyDecoded;
}

void InspectorMediaIO::runLtcCheck(InspectorEvent &event) {
        event.ltcDecoderEnabled = true;
        if (!_ltcDecoder || !_audioStreamReady) return;

        // Drain everything currently buffered into a scratch and feed
        // it to the LTC decoder as a single contiguous chunk.  Because
        // ingestAudio runs first and pushes the just-arrived chunk
        // before us, the drain captures both any backlog from previous
        // frames (bursty audio) and whatever just landed.  The LTC
        // decoder is itself stream-stateful, so feeding it accumulated
        // bursts is equivalent to feeding it a steady drip.
        const size_t available = _audioStream.available();
        if (available == 0) return;
        const AudioDesc bufDesc = _audioStream.format();
        if (!bufDesc.isValid()) return;
        const size_t bytes = bufDesc.bufferSize(available);
        if (!_audioDrainScratch.isValid() || _audioDrainScratch->size() < bytes) {
                _audioDrainScratch = Buffer::Ptr::create(bytes);
                if (!_audioDrainScratch.isValid()) return;
        }
        auto [got, popErr] = _audioStream.pop(_audioDrainScratch.modify()->data(), available);
        if (popErr.isError() || got == 0) return;

        // The LTC decoder reports sampleStart in *its* internal counter,
        // which equals our cumulative-analyzed counter because we feed
        // it cumulatively from sample 0.  Capture the pre-decode value
        // so we can rebase the result if needed (currently a no-op —
        // the decoder's counter and ours stay locked).
        const int64_t analyzedBefore = _audioCumulativeAnalyzed;
        // Wrap the popped bytes in a temporary PcmAudioPayload so the
        // existing format-agnostic LtcDecoder::decode entrypoint pulls
        // the right channel out for us.
        BufferView              scratchBv(_audioDrainScratch, 0, bufDesc.bufferSize(got));
        PcmAudioPayload         tempPayload(bufDesc, got, scratchBv);
        LtcDecoder::DecodedList results = _ltcDecoder->decode(tempPayload, _ltcChannel);
        _audioCumulativeAnalyzed = analyzedBefore + static_cast<int64_t>(got);

        if (results.isEmpty()) return;

        // Multiple LTC frames may decode in one drain when burst sizes
        // straddle LTC frame boundaries — we keep the last one for the
        // per-frame report.
        const auto &last = results[results.size() - 1];
        event.ltcDecoded = true;
        event.ltcTimecode = last.timecode;
        // Absolute stream-sample position of the LTC sync word.  Sample
        // 0 is the first sample the inspector ever ingested; the
        // wall-clock at this position is _audioStreamStartNs +
        // sampleStart * 1e9 / _audioSampleRate.
        event.ltcSampleStart = last.sampleStart;

        // Latch the LTC's frame-rate mode the first time we see one,
        // so the picture-TC continuity check has a rate to work with.
        // The picture data band only carries digits + the DF flag and
        // therefore arrives without a Mode, which makes Timecode's
        // operator++ a no-op — without an attached mode the "expected
        // next TC" stays equal to the previous, and every frame would
        // false-positive a discontinuity.
        if (!_inferredPictureMode.hasFormat() && last.timecode.mode().vtcFormat() != nullptr) {
                _inferredPictureMode = last.timecode.mode();
        }
}

void InspectorMediaIO::runAvSyncCheck(const Frame &frame, InspectorEvent &event) {
        (void)frame;
        if (!_checkAvSync) return;
        if (!_audioStreamAnchored || _audioSampleRate <= 0.0) return;
        if (event.audioChannelMarkers.isEmpty()) return;
        if (_videoFrameHistory.isEmpty()) return;
        if (!_frameRate.isValid()) return;

        // Marker-based A/V sync, cadence-aware and phase-anchored.
        //
        // Both the @ref ImageDataEncoder and the @ref AudioDataEncoder
        // stamp the same @c [stream:8][channel:8][frame:48] codeword,
        // so the 48-bit frame field uniquely identifies a frame
        // across the two streams.  For each successful audio
        // codeword we compute the raw phase between its actual
        // stream sample position and the rational-rate-predicted
        // ideal, then report the deviation from a baseline phase
        // captured on the first match:
        //
        //   raw_phase = streamSampleStart -
        //               _frameRate.cumulativeTicks(sampleRate, frame)
        //   baseline  = raw_phase at first match (latched once)
        //   offset    = raw_phase - baseline           (in samples)
        //
        // The baseline absorbs any constant phase the producer
        // happened to start with — a mid-stream join, a non-zero
        // first frame number, an SRC's constant group delay, or any
        // other one-time offset between the producer's audio
        // cadence and the rational ideal.  All of those are stable
        // properties of the stream and not real A/V sync errors.
        // A clean cadenced run therefore reports offset = 0 across
        // every frame regardless of rate, starting frame, or
        // pipeline shape.
        //
        // Real changes in the codeword position relative to its
        // expected rational position (codeword moved within a
        // chunk, audio sample dropped/inserted, audio path
        // resampling drift) still show up: they change raw_phase
        // away from the latched baseline, the per-frame
        // change-detection (see below) fires
        // @ref InspectorDiscontinuity::SyncOffsetChange.
        //
        // Sign convention: positive = audio later than predicted →
        // video leads audio.  Negative = audio earlier than
        // predicted → audio leads video.
        //
        // We still gate on the matching @c VideoFrameRecord so the
        // per-frame event reports an offset only for codewords
        // whose video frame the inspector has actually seen.
        bool    haveMatch = false;
        int64_t latestMatchedSamplePos = -1;
        int64_t matchedOffsetSamples = 0;
        for (size_t i = 0; i < event.audioChannelMarkers.size(); ++i) {
                const auto &m = event.audioChannelMarkers[i];
                if (!m.decoded) continue;
                if (m.streamSampleStart < 0) continue;

                // Sanity-gate against the video frame history.
                // Search from the back: the most recent record is
                // the cheapest match under steady-state.
                const VideoFrameRecord *match = nullptr;
                for (size_t j = _videoFrameHistory.size(); j > 0; --j) {
                        const VideoFrameRecord &rec = _videoFrameHistory[j - 1];
                        if (rec.frame48 == m.frameNumber && rec.streamId == m.streamId) {
                                match = &rec;
                                break;
                        }
                }
                if (match == nullptr) continue;

                if (m.streamSampleStart > latestMatchedSamplePos) {
                        const int64_t expectedSamplePos = _frameRate.cumulativeTicks(
                                static_cast<int64_t>(_audioSampleRate),
                                static_cast<int64_t>(m.frameNumber));
                        const int64_t rawPhase = m.streamSampleStart - expectedSamplePos;
                        if (!_avSyncBaselineSet) {
                                _avSyncBaselinePhase = rawPhase;
                                _avSyncBaselineSet = true;
                        }
                        matchedOffsetSamples = rawPhase - _avSyncBaselinePhase;
                        latestMatchedSamplePos = m.streamSampleStart;
                        haveMatch = true;
                }
        }

        if (!haveMatch) return;

        // Cache the average per-frame sample count for downstream
        // reporting (periodic log + final report).  Exact rational
        // average pulled from the upstream MediaDesc, which makes
        // 29.97 and friends report correctly.
        if (_videoFrameDurationNs > 0.0) {
                _samplesPerFrame = _audioSampleRate * _videoFrameDurationNs / 1.0e9;
        }

        event.avSyncOffsetSamples = matchedOffsetSamples;
        event.avSyncValid = true;

        // In professional video the audio and video are locked to the
        // same reference, so once the A/V sync offset has settled it
        // must not move from one frame to the next.  Any movement
        // beyond the configured tolerance is a real fault — even one
        // sample — and we surface it as a discontinuity so the
        // standard immediate-warning path picks it up.  The tolerance
        // defaults to 0 to enforce strict lock; QA workflows that
        // know their pipeline has bounded jitter can raise it.
        if (_hasPreviousSyncOffset) {
                const int64_t delta = event.avSyncOffsetSamples - _previousSyncOffset;
                const int64_t absDelta = delta < 0 ? -delta : delta;
                if (absDelta > _syncOffsetToleranceSamples) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::SyncOffsetChange;
                        d.previousValue = String::sprintf("%+lld", static_cast<long long>(_previousSyncOffset));
                        d.currentValue = String::sprintf("%+lld", static_cast<long long>(event.avSyncOffsetSamples));
                        d.description = String::sprintf("A/V sync offset moved: was %+lld samples, "
                                                        "now %+lld samples (delta %+lld, tolerance %d)",
                                                        static_cast<long long>(_previousSyncOffset),
                                                        static_cast<long long>(event.avSyncOffsetSamples),
                                                        static_cast<long long>(delta), _syncOffsetToleranceSamples);
                        event.discontinuities.pushToBack(d);
                }
        }
        _previousSyncOffset = event.avSyncOffsetSamples;
        _hasPreviousSyncOffset = true;
}

void InspectorMediaIO::runContinuityCheck(InspectorEvent &event) {
        if (!_checkContinuity) return;
        if (!event.pictureDecoded) {
                if (_hasPreviousPicture) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::ImageDataDecodeFailure;
                        d.previousValue = String("decoded");
                        d.currentValue = String("undecoded");
                        d.description = String("Picture data band decode failed after a previously-successful frame");
                        event.discontinuities.pushToBack(d);
                }
                return;
        }

        if (_hasPreviousPicture) {
                if (event.pictureStreamId != _previousStreamId) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::StreamIdChange;
                        d.previousValue = String::sprintf("0x%08X", _previousStreamId);
                        d.currentValue = String::sprintf("0x%08X", event.pictureStreamId);
                        d.description = String::sprintf("Stream ID changed: was %s, now %s", d.previousValue.cstr(),
                                                        d.currentValue.cstr());
                        event.discontinuities.pushToBack(d);
                }

                const uint32_t expectedNext = _previousFrameNumber + 1;
                if (event.pictureFrameNumber != expectedNext) {
                        InspectorDiscontinuity d;
                        d.kind = InspectorDiscontinuity::FrameNumberJump;
                        d.previousValue = String::number(_previousFrameNumber);
                        d.currentValue = String::number(event.pictureFrameNumber);
                        const long long deltaFromExpected =
                                static_cast<long long>(event.pictureFrameNumber) - static_cast<long long>(expectedNext);
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
                if (event.pictureTimecode.isValid() && _previousPictureTc.isValid() &&
                    _inferredPictureMode.hasFormat()) {
                        Timecode prevWithMode = _previousPictureTc;
                        Timecode currWithMode = event.pictureTimecode;
                        prevWithMode.setMode(_inferredPictureMode);
                        currWithMode.setMode(_inferredPictureMode);
                        Timecode expectedTc = prevWithMode;
                        ++expectedTc;
                        if (currWithMode != expectedTc) {
                                InspectorDiscontinuity d;
                                d.kind = InspectorDiscontinuity::PictureTcJump;
                                d.previousValue = renderTc(prevWithMode);
                                d.currentValue = renderTc(currWithMode);
                                d.description = String::sprintf("Picture TC jumped: was %s (expected %s next), got %s",
                                                                d.previousValue.cstr(), renderTc(expectedTc).cstr(),
                                                                d.currentValue.cstr());
                                event.discontinuities.pushToBack(d);
                        }
                }
        }

        _hasPreviousPicture = true;
        _previousFrameNumber = event.pictureFrameNumber;
        _previousStreamId = event.pictureStreamId;
        _previousPictureTc = event.pictureTimecode;

        // LTC continuity — only meaningful if both decoders are running.
        if (event.ltcDecoded && event.ltcTimecode.isValid()) {
                if (_hasPreviousLtc && _previousLtcTc.isValid()) {
                        Timecode expectedLtc = _previousLtcTc;
                        ++expectedLtc;
                        if (event.ltcTimecode != expectedLtc) {
                                InspectorDiscontinuity d;
                                d.kind = InspectorDiscontinuity::LtcTcJump;
                                d.previousValue = renderTc(_previousLtcTc);
                                d.currentValue = renderTc(event.ltcTimecode);
                                d.description = String::sprintf("LTC jumped: was %s (expected %s next), got %s",
                                                                d.previousValue.cstr(), renderTc(expectedLtc).cstr(),
                                                                d.currentValue.cstr());
                                event.discontinuities.pushToBack(d);
                        }
                }
                _hasPreviousLtc = true;
                _previousLtcTc = event.ltcTimecode;
        } else if (_hasPreviousLtc) {
                InspectorDiscontinuity d;
                d.kind = InspectorDiscontinuity::LtcDecodeFailure;
                d.previousValue = renderTc(_previousLtcTc);
                d.currentValue = String("--:--:--:--");
                d.description = String("LTC decode failed after a previously-successful frame");
                event.discontinuities.pushToBack(d);
        }
}

void InspectorMediaIO::runTimestampCheck(const Frame &frame, InspectorEvent &event) {
        event.timestampTestEnabled = true;

        // Pull the per-essence PTS from each payload.  MediaIO is
        // responsible for ensuring every essence carries one (backends
        // with hardware timestamps set them directly; MediaIO fills in
        // a Synthetic fallback otherwise), so "missing" here is a real
        // fault — we surface it as a warning and a discontinuity.
        auto vids = frame.videoPayloads();
        if (!vids.isEmpty() && vids[0].isValid()) {
                const MediaTimeStamp &mts = vids[0]->pts();
                if (mts.isValid()) {
                        event.videoTimestampValid = true;
                        event.videoTimestampNs = mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
                }
        }
        auto auds = frame.audioPayloads();
        if (!auds.isEmpty() && auds[0].isValid()) {
                const MediaTimeStamp &mts = auds[0]->pts();
                if (mts.isValid()) {
                        event.audioTimestampValid = true;
                        event.audioTimestampNs = mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
                }
        }

        // Compute frame-to-frame deltas only when both the current and
        // the previous frame carried a valid timestamp — a gap resets
        // the anchor so one missing frame doesn't poison the next
        // measurement.
        if (event.videoTimestampValid && _hasPreviousVideoTimestamp) {
                event.videoTimestampDeltaNs = event.videoTimestampNs - _previousVideoTimestampNs;
                event.videoTimestampDeltaValid = true;
        }
        if (event.audioTimestampValid && _hasPreviousAudioTimestamp) {
                event.audioTimestampDeltaNs = event.audioTimestampNs - _previousAudioTimestampNs;
                event.audioTimestampDeltaValid = true;
        }

        // Video PTS prediction & re-anchor.  Anchored on the first
        // valid PTS at frame N0; the predicted PTS for any subsequent
        // frame N is anchorNs + (N - N0) * frameDurationNs.  The jitter
        // is reported per-frame; if it exceeds the configured tolerance
        // we re-anchor on the new PTS and emit a discontinuity.
        //
        // The frame duration normally comes from the upstream MediaDesc
        // captured at open() time, but standalone test rigs and
        // backends that don't carry a frame rate end up with zero.  In
        // that case we derive it from the first observed PTS delta and
        // lock it in for the rest of the run.
        if (event.videoTimestampValid) {
                if (!_videoPtsAnchored) {
                        _videoPtsAnchored = true;
                        _videoPtsAnchorNs = event.videoTimestampNs;
                        _videoPtsAnchorFrame = event.frameIndex;
                } else {
                        if (_videoFrameDurationNs <= 0.0 && _hasPreviousVideoTimestamp) {
                                const int64_t observed =
                                        event.videoTimestampNs - _previousVideoTimestampNs;
                                if (observed > 0) {
                                        _videoFrameDurationNs = static_cast<double>(observed);
                                }
                        }
                        if (_videoFrameDurationNs > 0.0) {
                                const int64_t framesSince =
                                        event.frameIndex.value() - _videoPtsAnchorFrame.value();
                                const int64_t predictedNs =
                                        _videoPtsAnchorNs +
                                        static_cast<int64_t>(static_cast<double>(framesSince) *
                                                             _videoFrameDurationNs);
                                const int64_t jitter = event.videoTimestampNs - predictedNs;
                                event.videoPtsJitterValid = true;
                                event.videoPtsJitterNs = jitter;
                                const int64_t absJ = jitter < 0 ? -jitter : jitter;
                                if (absJ > _videoPtsToleranceNs) {
                                        InspectorDiscontinuity d;
                                        d.kind = InspectorDiscontinuity::VideoTimestampReanchor;
                                        d.previousValue = String::sprintf(
                                                "%lld", static_cast<long long>(predictedNs));
                                        d.currentValue = String::sprintf(
                                                "%lld", static_cast<long long>(event.videoTimestampNs));
                                        d.description = String::sprintf(
                                                "Video PTS diverged from prediction by %+.3f ms "
                                                "(tolerance %.3f ms) — re-anchoring",
                                                jitter / 1.0e6, _videoPtsToleranceNs / 1.0e6);
                                        event.discontinuities.pushToBack(d);
                                        _videoPtsAnchorNs = event.videoTimestampNs;
                                        _videoPtsAnchorFrame = event.frameIndex;
                                }
                        }
                }
        }

        // A/V cross-essence drift: how does (videoPts - audioPts)
        // move over time?  Use the latest known audio PTS — bursty
        // delivery may mean this frame had no audio chunk, but a
        // recent one is still meaningful for the cross-clock check.
        if (event.videoTimestampValid && _hasLastAudioPtsForAv) {
                const int64_t offsetNs = event.videoTimestampNs - _lastAudioPtsForAvNs;
                if (!_avBaselineSet) {
                        _avBaselineSet = true;
                        _avBaselineOffsetNs = offsetNs;
                }
                event.avPtsDriftValid = true;
                event.avPtsDriftNs = offsetNs - _avBaselineOffsetNs;
        }

        // Surface a missing timestamp as a discontinuity so it lands in
        // the same warning channel as the other continuity faults.  We
        // fire unconditionally rather than "only after a previously
        // valid frame" because MediaIO guarantees one on every essence
        // — the very first missing stamp is already a real failure.
        if (!vids.isEmpty() && !event.videoTimestampValid) {
                InspectorDiscontinuity d;
                d.kind = InspectorDiscontinuity::MissingVideoTimestamp;
                d.previousValue = String("valid");
                d.currentValue = String("missing");
                d.description = String("Video MediaTimeStamp missing on frame");
                event.discontinuities.pushToBack(d);
        }
        if (!auds.isEmpty() && !event.audioTimestampValid) {
                InspectorDiscontinuity d;
                d.kind = InspectorDiscontinuity::MissingAudioTimestamp;
                d.previousValue = String("valid");
                d.currentValue = String("missing");
                d.description = String("Audio MediaTimeStamp missing on frame");
                event.discontinuities.pushToBack(d);
        }

        // Advance the per-essence anchors.  On a gap we reset rather
        // than carry forward, so the next valid timestamp starts a
        // fresh delta chain.
        if (event.videoTimestampValid) {
                _previousVideoTimestampNs = event.videoTimestampNs;
                _hasPreviousVideoTimestamp = true;
        } else {
                _hasPreviousVideoTimestamp = false;
        }
        if (event.audioTimestampValid) {
                _previousAudioTimestampNs = event.audioTimestampNs;
                _hasPreviousAudioTimestamp = true;
        } else {
                _hasPreviousAudioTimestamp = false;
        }
}

void InspectorMediaIO::runAudioSamplesCheck(const Frame &frame, InspectorEvent &event) {
        event.audioSamplesTestEnabled = true;

        auto auds = frame.audioPayloads();
        if (auds.isEmpty()) return;
        const AudioPayload::Ptr &ap = auds[0];
        if (!ap.isValid()) return;
        // Sample counts only apply to uncompressed PCM payloads.
        const auto *uap = ap->as<PcmAudioPayload>();
        if (uap == nullptr) return;

        const int64_t n = static_cast<int64_t>(uap->sampleCount());
        event.audioSamplesValid = true;
        event.audioSamplesThisFrame = n;

        // Stream-level cadence (cumulative samples vs. wall-clock span)
        // is reported from the audio stream anchors directly — see the
        // stats accumulator block in @ref executeCmd.  ingestAudio is
        // the single owner of those anchors and handles re-anchor on
        // PTS jumps, so this per-frame check has nothing to add to the
        // running totals beyond the per-frame delivery-shape stat
        // above.
}

void InspectorMediaIO::emitPeriodicLogIfDue() {
        if (_logIntervalSec <= 0.0) return;

        const double now = monotonicWallSeconds();
        const double elapsed = now - _lastLogWallSec;
        if (elapsed < _logIntervalSec) return;

        InspectorSnapshot snap;
        {
                Mutex::Locker lk(_stateMutex);
                snap = _stats;
        }

        promekiInfo("[INSPECTOR REPORT] Frame %lld: %lld frames (%.2f s wall) since last report - %lld total",
                    static_cast<long long>(snap.framesProcessed.value()),
                    static_cast<long long>(_framesSinceLastLog.value()), elapsed,
                    static_cast<long long>(snap.framesProcessed.value()));

        if (_decodeImageData) {
                if (snap.lastEvent.pictureDecoded) {
                        promekiInfo("  Image data band: decoded %lld / %lld frames "
                                    "(%.1f%%) — most recent: streamID 0x%08X, "
                                    "frameNo %u, TC %s",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()),
                                    snap.framesProcessed.value() > 0
                                            ? 100.0 * snap.framesWithPictureData.value() / snap.framesProcessed.value()
                                            : 0.0,
                                    snap.lastEvent.pictureStreamId, snap.lastEvent.pictureFrameNumber,
                                    renderTc(snap.lastEvent.pictureTimecode).cstr());
                } else {
                        promekiWarn("  Image data band: NOT DECODED in latest frame "
                                    "(decoded %lld / %lld frames since open)",
                                    static_cast<long long>(snap.framesWithPictureData.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                }
        }

        if (_decodeLtc) {
                if (snap.lastEvent.ltcDecoded) {
                        promekiInfo("  LTC: decoded %lld / %lld frames (%.1f%%) — "
                                    "most recent: TC %s, sync word at stream "
                                    "sample %lld",
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

        if (_checkAvSync) {
                if (snap.lastEvent.avSyncValid) {
                        const int64_t s = snap.lastEvent.avSyncOffsetSamples;
                        if (s == 0) {
                                promekiInfo("  A/V Sync: audio and video locked (0 samples)");
                        } else {
                                // Render direction in plain language
                                // and report both the raw sample count
                                // and the fractional-frame equivalent
                                // so a QA reader can interpret the
                                // value at whichever scale is more
                                // useful.  Frames are computed from
                                // the upstream MediaDesc's rational
                                // frame rate so 29.97 and friends
                                // report correctly.
                                const int64_t absSamples = s < 0 ? -s : s;
                                const double  frames = _samplesPerFrame > 0.0
                                                               ? static_cast<double>(absSamples) / _samplesPerFrame
                                                               : 0.0;
                                const char   *direction = (s > 0) ? "Video leads audio" : "Audio leads video";
                                promekiInfo("  A/V Sync: %s by %lld samples, "
                                            "%.4f frames",
                                            direction, static_cast<long long>(absSamples), frames);
                        }
                }
        }

        if (_checkTimestamp) {
                if (snap.videoDeltaSamples > 0) {
                        promekiInfo("  Video timestamps: delta min/avg/max = "
                                    "%.3f / %.3f / %.3f ms  (actual %.4f fps, "
                                    "%lld / %lld frames stamped)",
                                    snap.videoDeltaMinNs / 1.0e6, snap.videoDeltaAvgNs / 1.0e6,
                                    snap.videoDeltaMaxNs / 1.0e6, snap.actualFps,
                                    static_cast<long long>(snap.framesWithVideoTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else if (snap.framesWithVideoTimestamp > 0) {
                        promekiInfo("  Video timestamps: %lld / %lld frames stamped "
                                    "(no delta yet)",
                                    static_cast<long long>(snap.framesWithVideoTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else {
                        promekiWarn("  Video timestamps: no stamped frames yet");
                }
                if (snap.audioDeltaSamples > 0) {
                        promekiInfo("  Audio timestamps: delta min/avg/max = "
                                    "%.3f / %.3f / %.3f ms  (%lld / %lld frames stamped)",
                                    snap.audioDeltaMinNs / 1.0e6, snap.audioDeltaAvgNs / 1.0e6,
                                    snap.audioDeltaMaxNs / 1.0e6,
                                    static_cast<long long>(snap.framesWithAudioTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                } else if (snap.framesWithAudioTimestamp > 0) {
                        promekiInfo("  Audio timestamps: %lld / %lld frames stamped "
                                    "(no delta yet)",
                                    static_cast<long long>(snap.framesWithAudioTimestamp.value()),
                                    static_cast<long long>(snap.framesProcessed.value()));
                }
        }

        if (_checkAudioSamples && snap.audioSamplesFrames > 0) {
                if (snap.measuredAudioSampleRate > 0.0) {
                        promekiInfo("  Audio samples: per-frame min/avg/max = "
                                    "%lld / %.2f / %lld  (measured rate %.2f Hz "
                                    "over %lld samples / %.3f s)",
                                    static_cast<long long>(snap.audioSamplesMin), snap.audioSamplesAvg,
                                    static_cast<long long>(snap.audioSamplesMax), snap.measuredAudioSampleRate,
                                    static_cast<long long>(snap.audioSamplesTotal), snap.audioSamplesSpanNs / 1.0e9);
                } else {
                        promekiInfo("  Audio samples: per-frame min/avg/max = "
                                    "%lld / %.2f / %lld  (rate not yet measurable)",
                                    static_cast<long long>(snap.audioSamplesMin), snap.audioSamplesAvg,
                                    static_cast<long long>(snap.audioSamplesMax));
                }
        }

        if (_checkTimestamp) {
                if (snap.audioPtsJitterSamples > 0) {
                        promekiInfo("  Audio PTS jitter: min/avg/max = %+.3f / %+.3f / %+.3f ms "
                                    "(over %lld chunks, %lld re-anchors)",
                                    snap.audioPtsJitterMinNs / 1.0e6, snap.audioPtsJitterAvgNs / 1.0e6,
                                    snap.audioPtsJitterMaxNs / 1.0e6,
                                    static_cast<long long>(snap.audioPtsJitterSamples),
                                    static_cast<long long>(snap.audioReanchorCount));
                }
                if (snap.videoPtsJitterSamples > 0) {
                        promekiInfo("  Video PTS jitter: min/avg/max = %+.3f / %+.3f / %+.3f ms "
                                    "(over %lld frames, %lld re-anchors)",
                                    snap.videoPtsJitterMinNs / 1.0e6, snap.videoPtsJitterAvgNs / 1.0e6,
                                    snap.videoPtsJitterMaxNs / 1.0e6,
                                    static_cast<long long>(snap.videoPtsJitterSamples),
                                    static_cast<long long>(snap.videoReanchorCount));
                }
                if (snap.avPtsDriftSamples > 0) {
                        promekiInfo("  A/V PTS drift: min/avg/max = %+.3f / %+.3f / %+.3f ms "
                                    "(baseline videoPts-audioPts %+.3f ms, %lld samples)",
                                    snap.avPtsDriftMinNs / 1.0e6, snap.avPtsDriftAvgNs / 1.0e6,
                                    snap.avPtsDriftMaxNs / 1.0e6, snap.avBaselineOffsetNs / 1.0e6,
                                    static_cast<long long>(snap.avPtsDriftSamples));
                }
        }

        // Continuity is reported only when there's something to say —
        // a clean stream stays silent.  When discontinuities have
        // accumulated we emit a warning summary so the line stands
        // out from the routine info-level traffic.
        if (_checkContinuity && snap.totalDiscontinuities > 0) {
                promekiWarn("  Continuity: %lld total discontinuities detected",
                            static_cast<long long>(snap.totalDiscontinuities));
        }

        promekiInfo("=== END OF REPORT ===");
        _lastLogWallSec = now;
        _framesSinceLastLog = 0;
}

// ---------------------------------------------------------------------------
// CaptureStats output file
// ---------------------------------------------------------------------------

Error InspectorMediaIO::openCaptureStatsFile(const String &configured) {
        closeCaptureStatsFile();

        if (configured.isEmpty()) {
                // Synthesize a unique path under Dir::temp().  PID +
                // steady-clock nanoseconds is effectively collision
                // free on any machine that isn't spawning thousands of
                // inspectors per nanosecond.
                const int64_t ns = TimeStamp::now().nanoseconds();
                FilePath      p =
                        Dir::temp().path() / String::sprintf("promeki_inspector_stats_%d_%lld.tsv",
                                                             static_cast<int>(getpid()), static_cast<long long>(ns));
                _statsFilePath = p.toString();
        } else {
                _statsFilePath = configured;
        }

        _statsFile = std::fopen(_statsFilePath.cstr(), "w");
        if (_statsFile == nullptr) {
                promekiErr("InspectorMediaIO: failed to open CaptureStats "
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
        std::fprintf(_statsFile, "frame\t"
                                 "wall_ns\twall_iso\t"
                                 "video_ts_ns\tvideo_clock\tvideo_delta_ns\tvideo_delta_ms\t"
                                 "video_pts_jitter_ns\t"
                                 "image_width\timage_height\tpixel_format\timage_bytes\t"
                                 "audio_ts_ns\taudio_clock\taudio_delta_ns\taudio_delta_ms\t"
                                 "audio_pts_jitter_ns\t"
                                 "audio_samples\taudio_format\taudio_rate_hz\taudio_channels\taudio_bytes\t"
                                 "av_pts_drift_ns\n");

        promekiInfo("InspectorMediaIO: CaptureStats writing to %s", _statsFilePath.cstr());
        _statsWriteError = false;
        return Error::Ok;
}

void InspectorMediaIO::closeCaptureStatsFile() {
        if (_statsFile != nullptr) {
                std::fclose(_statsFile);
                _statsFile = nullptr;
        }
        _statsFilePath = String();
        _statsWriteError = false;
}

void InspectorMediaIO::runCaptureStats(const Frame &frame, const InspectorEvent &event) {
        if (_statsFile == nullptr || _statsWriteError) return;

        // Wall time = when we're writing the row.  system_clock (not
        // steady_clock) so the ISO rendering is meaningful.
        const int64_t wallNs = wallClockEpochNs();
        char          wallIso[64];
        formatIsoUtc(wallNs, wallIso, sizeof(wallIso));

        // -- Video columns --
        String videoTsNs = String("-");
        String videoClockName = String("-");
        String videoDeltaNs = String("-");
        String videoDeltaMs = String("-");
        String imgWidth = String("-");
        String imgHeight = String("-");
        String pixelFormat = String("-");
        String imageBytes = String("-");
        {
                auto vids = frame.videoPayloads();
                if (!vids.isEmpty() && vids[0].isValid()) {
                        const VideoPayload &vp = *vids[0];
                        const ImageDesc    &id = vp.desc();
                        imgWidth = String::number(id.size().width());
                        imgHeight = String::number(id.size().height());
                        pixelFormat = id.pixelFormat().name();
                        imageBytes = String::number(static_cast<int64_t>(payloadByteSize(vp)));

                        const MediaTimeStamp &mts = vp.pts();
                        if (mts.isValid()) {
                                const int64_t ns = mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
                                videoTsNs = String::number(ns);
                                videoClockName = mts.domain().name();
                        }
                }
        }
        if (event.videoTimestampDeltaValid) {
                const int64_t d = event.videoTimestampDeltaNs;
                videoDeltaNs = String::number(d);
                videoDeltaMs = String::sprintf("%.6f", d / 1.0e6);
        }
        String videoPtsJitterNs = String("-");
        if (event.videoPtsJitterValid) {
                videoPtsJitterNs = String::number(event.videoPtsJitterNs);
        }

        // -- Audio columns --
        String audioTsNs = String("-");
        String audioClockName = String("-");
        String audioDeltaNs = String("-");
        String audioDeltaMs = String("-");
        String audioPtsJitterNs = String("-");
        String audioSamples = String("-");
        String audioFormat = String("-");
        String audioRateHz = String("-");
        String audioChannels = String("-");
        String audioBytes = String("-");
        String avPtsDriftNs = String("-");
        {
                auto auds = frame.audioPayloads();
                if (!auds.isEmpty() && auds[0].isValid()) {
                        const AudioPayload &ap = *auds[0];
                        const AudioDesc    &ad = ap.desc();
                        // Sample count is only meaningful for PCM
                        // payloads; compressed audio reports zero here.
                        const auto  *uap = ap.as<PcmAudioPayload>();
                        const size_t sampleCount = uap != nullptr ? uap->sampleCount() : size_t(0);
                        audioSamples = String::number(static_cast<int64_t>(sampleCount));
                        audioFormat = ad.format().name();
                        audioRateHz = String::sprintf("%.2f", ad.sampleRate());
                        audioChannels = String::number(static_cast<int64_t>(ad.channels()));
                        audioBytes =
                                String::number(static_cast<int64_t>(ad.bytesPerSample() * ad.channels() * sampleCount));

                        const MediaTimeStamp &mts = ap.pts();
                        if (mts.isValid()) {
                                const int64_t ns = mts.timeStamp().nanoseconds() + mts.offset().nanoseconds();
                                audioTsNs = String::number(ns);
                                audioClockName = mts.domain().name();
                        }
                }
        }
        if (event.audioTimestampDeltaValid) {
                const int64_t d = event.audioTimestampDeltaNs;
                audioDeltaNs = String::number(d);
                audioDeltaMs = String::sprintf("%.6f", d / 1.0e6);
        }
        if (event.audioPtsJitterValid) {
                audioPtsJitterNs = String::number(event.audioPtsJitterNs);
        }
        if (event.avPtsDriftValid) {
                avPtsDriftNs = String::number(event.avPtsDriftNs);
        }

        const int rc =
                std::fprintf(_statsFile,
                             "%lld\t"
                             "%lld\t%s\t"
                             "%s\t%s\t%s\t%s\t%s\t"
                             "%s\t%s\t%s\t%s\t"
                             "%s\t%s\t%s\t%s\t%s\t"
                             "%s\t%s\t%s\t%s\t%s\t"
                             "%s\n",
                             static_cast<long long>(event.frameIndex.value()), static_cast<long long>(wallNs), wallIso,
                             videoTsNs.cstr(), videoClockName.cstr(), videoDeltaNs.cstr(), videoDeltaMs.cstr(),
                             videoPtsJitterNs.cstr(),
                             imgWidth.cstr(), imgHeight.cstr(), pixelFormat.cstr(), imageBytes.cstr(),
                             audioTsNs.cstr(), audioClockName.cstr(), audioDeltaNs.cstr(), audioDeltaMs.cstr(),
                             audioPtsJitterNs.cstr(),
                             audioSamples.cstr(), audioFormat.cstr(), audioRateHz.cstr(), audioChannels.cstr(),
                             audioBytes.cstr(),
                             avPtsDriftNs.cstr());
        if (rc < 0) {
                promekiErr("InspectorMediaIO: CaptureStats write "
                           "failed on frame %lld (%s) — further rows "
                           "will be dropped",
                           static_cast<long long>(event.frameIndex.value()), std::strerror(errno));
                _statsWriteError = true;
        }
}

Error InspectorMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!_isOpen) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;

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

        // Audio ingest runs before any audio-side check so the LTC
        // drain sees this frame's audio chunk in the stream.  It also
        // emits AudioTimestampReanchor discontinuities into @c event,
        // which the periodic warning loop below relays.
        ingestAudio(work, event);

        if (_decodeImageData) runImageDataCheck(work, event);
        if (_decodeAudioData) runAudioDataCheck(work, event);
        if (_decodeLtc) runLtcCheck(event);
        runAvSyncCheck(work, event);
        runContinuityCheck(event);
        if (_checkTimestamp) runTimestampCheck(work, event);
        if (_checkAudioSamples) runAudioSamplesCheck(work, event);
        if (_checkCaptureStats) runCaptureStats(work, event);

        // Update stats accumulator under the mutex.
        {
                Mutex::Locker lk(_stateMutex);
                _stats.framesProcessed++;
                if (event.pictureDecoded) _stats.framesWithPictureData++;
                if (event.ltcDecoded) _stats.framesWithLtc++;
                if (event.videoTimestampValid) _stats.framesWithVideoTimestamp++;
                if (event.audioTimestampValid) _stats.framesWithAudioTimestamp++;
                if (event.videoTimestampDeltaValid) {
                        // Running min/max/avg.  We keep avg as a
                        // double: numerically safer than summing int64
                        // deltas for a long capture, and the fractional
                        // precision is useful when reporting actual
                        // FPS.
                        const int64_t d = event.videoTimestampDeltaNs;
                        if (_stats.videoDeltaSamples == 0) {
                                _stats.videoDeltaMinNs = d;
                                _stats.videoDeltaMaxNs = d;
                                _stats.videoDeltaAvgNs = static_cast<double>(d);
                        } else {
                                if (d < _stats.videoDeltaMinNs) _stats.videoDeltaMinNs = d;
                                if (d > _stats.videoDeltaMaxNs) _stats.videoDeltaMaxNs = d;
                                const double n = static_cast<double>(_stats.videoDeltaSamples + 1);
                                _stats.videoDeltaAvgNs += (static_cast<double>(d) - _stats.videoDeltaAvgNs) / n;
                        }
                        _stats.videoDeltaSamples++;
                        _stats.actualFps = _stats.videoDeltaAvgNs > 0.0 ? 1.0e9 / _stats.videoDeltaAvgNs : 0.0;
                }
                if (event.audioTimestampDeltaValid) {
                        const int64_t d = event.audioTimestampDeltaNs;
                        if (_stats.audioDeltaSamples == 0) {
                                _stats.audioDeltaMinNs = d;
                                _stats.audioDeltaMaxNs = d;
                                _stats.audioDeltaAvgNs = static_cast<double>(d);
                        } else {
                                if (d < _stats.audioDeltaMinNs) _stats.audioDeltaMinNs = d;
                                if (d > _stats.audioDeltaMaxNs) _stats.audioDeltaMaxNs = d;
                                const double n = static_cast<double>(_stats.audioDeltaSamples + 1);
                                _stats.audioDeltaAvgNs += (static_cast<double>(d) - _stats.audioDeltaAvgNs) / n;
                        }
                        _stats.audioDeltaSamples++;
                }
                if (event.audioSamplesValid) {
                        const int64_t n = event.audioSamplesThisFrame;
                        if (_stats.audioSamplesFrames == 0) {
                                _stats.audioSamplesMin = n;
                                _stats.audioSamplesMax = n;
                                _stats.audioSamplesAvg = static_cast<double>(n);
                        } else {
                                if (n < _stats.audioSamplesMin) _stats.audioSamplesMin = n;
                                if (n > _stats.audioSamplesMax) _stats.audioSamplesMax = n;
                                const double k = static_cast<double>(_stats.audioSamplesFrames + 1);
                                _stats.audioSamplesAvg += (static_cast<double>(n) - _stats.audioSamplesAvg) / k;
                        }
                        _stats.audioSamplesFrames++;
                        // Stream-level cumulative totals: cumulative
                        // samples / span between the first ingested
                        // chunk's PTS and the most recent ingested
                        // chunk's PTS.  This is the long-run
                        // measured rate; the per-frame min/max/avg
                        // above is the per-delivery shape.  When
                        // audio is bursty, span and cumulative are
                        // both still meaningful — they just measure
                        // the *stream* rather than per-frame.
                        _stats.audioSamplesTotal = _audioCumulativeIn;
                        _stats.audioSamplesSpanNs = (_audioStreamAnchored && _hasLastAudioPtsForAv)
                                                            ? (_lastAudioPtsForAvNs - _audioStreamStartNs)
                                                            : 0;
                        _stats.measuredAudioSampleRate =
                                _stats.audioSamplesSpanNs > 0
                                        ? (static_cast<double>(_stats.audioSamplesTotal) * 1.0e9 /
                                           static_cast<double>(_stats.audioSamplesSpanNs))
                                        : 0.0;
                }
                if (event.audioPtsJitterValid) {
                        const int64_t j = event.audioPtsJitterNs;
                        if (_stats.audioPtsJitterSamples == 0) {
                                _stats.audioPtsJitterMinNs = j;
                                _stats.audioPtsJitterMaxNs = j;
                                _stats.audioPtsJitterAvgNs = static_cast<double>(j);
                        } else {
                                if (j < _stats.audioPtsJitterMinNs) _stats.audioPtsJitterMinNs = j;
                                if (j > _stats.audioPtsJitterMaxNs) _stats.audioPtsJitterMaxNs = j;
                                const double n =
                                        static_cast<double>(_stats.audioPtsJitterSamples + 1);
                                _stats.audioPtsJitterAvgNs +=
                                        (static_cast<double>(j) - _stats.audioPtsJitterAvgNs) / n;
                        }
                        _stats.audioPtsJitterSamples++;
                }
                if (event.videoPtsJitterValid) {
                        const int64_t j = event.videoPtsJitterNs;
                        if (_stats.videoPtsJitterSamples == 0) {
                                _stats.videoPtsJitterMinNs = j;
                                _stats.videoPtsJitterMaxNs = j;
                                _stats.videoPtsJitterAvgNs = static_cast<double>(j);
                        } else {
                                if (j < _stats.videoPtsJitterMinNs) _stats.videoPtsJitterMinNs = j;
                                if (j > _stats.videoPtsJitterMaxNs) _stats.videoPtsJitterMaxNs = j;
                                const double n =
                                        static_cast<double>(_stats.videoPtsJitterSamples + 1);
                                _stats.videoPtsJitterAvgNs +=
                                        (static_cast<double>(j) - _stats.videoPtsJitterAvgNs) / n;
                        }
                        _stats.videoPtsJitterSamples++;
                }
                if (event.avPtsDriftValid) {
                        const int64_t d = event.avPtsDriftNs;
                        if (_stats.avPtsDriftSamples == 0) {
                                _stats.avPtsDriftMinNs = d;
                                _stats.avPtsDriftMaxNs = d;
                                _stats.avPtsDriftAvgNs = static_cast<double>(d);
                        } else {
                                if (d < _stats.avPtsDriftMinNs) _stats.avPtsDriftMinNs = d;
                                if (d > _stats.avPtsDriftMaxNs) _stats.avPtsDriftMaxNs = d;
                                const double n = static_cast<double>(_stats.avPtsDriftSamples + 1);
                                _stats.avPtsDriftAvgNs +=
                                        (static_cast<double>(d) - _stats.avPtsDriftAvgNs) / n;
                        }
                        _stats.avPtsDriftSamples++;
                }
                _stats.avBaselineSet = _avBaselineSet;
                _stats.avBaselineOffsetNs = _avBaselineOffsetNs;

                // Re-anchor counters mirror the per-frame discontinuity
                // events so consumers can read the totals from the
                // snapshot without scanning every event.
                for (const auto &d : event.discontinuities) {
                        if (d.kind == InspectorDiscontinuity::AudioTimestampReanchor) {
                                _stats.audioReanchorCount++;
                        } else if (d.kind == InspectorDiscontinuity::VideoTimestampReanchor) {
                                _stats.videoReanchorCount++;
                        }
                }

                _stats.totalDiscontinuities += event.discontinuities.size();
                _stats.hasLastEvent = true;
                _stats.lastEvent = event;
        }

        // Log every per-frame problem immediately as a warning so it
        // lands in the log at the right point in wall time.  All lines
        // share the "Frame N:" prefix so log readers can correlate
        // them with the periodic report.

        // Decode failures — the continuity check only catches failures
        // that follow a previously-successful decode.  These warnings
        // fire unconditionally so the first failure (and failures when
        // continuity is disabled) are never silent.
        if (_decodeImageData && !event.pictureDecoded) {
                promekiWarn("Frame %lld: image data band decode failed",
                            static_cast<long long>(event.frameIndex.value()));
        }
        if (_decodeLtc && !event.ltcDecoded) {
                promekiWarn("Frame %lld: LTC decode failed", static_cast<long long>(event.frameIndex.value()));
        }

        // Discontinuities have pre-rendered descriptions courtesy of
        // runContinuityCheck / runAvSyncCheck, so we relay them here.
        for (const auto &d : event.discontinuities) {
                promekiWarn("Frame %lld: discontinuity: %s", static_cast<long long>(event.frameIndex.value()),
                            d.description.cstr());
        }

        // Fire the per-frame callback (worker thread context — caller
        // is responsible for thread safety).
        if (_callback) {
                _callback(event);
        }

        ++_frameIndex;
        ++_framesSinceLastLog;
        cmd.currentFrame = _frameIndex;
        cmd.frameCount = toFrameCount(_frameIndex);

        emitPeriodicLogIfDue();
        // _dropFrames is currently a no-op — the inspector is a sink so
        // there is no downstream consumer.  The flag exists for future
        // tee-style wrappers; for now we always "drop" by simply
        // returning Ok and letting cmd.frame's reference count expire.
        (void)_dropFrames;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
