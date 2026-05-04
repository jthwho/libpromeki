/**
 * @file      tpgmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/tpgmediaio.h>
#include <promeki/videoformat.h>
#include <promeki/frame.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/audiolevel.h>
#include <promeki/enums.h>
#include <promeki/imagedataencoder.h>
#include <promeki/logger.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(TpgFactory)

MediaIOFactory::Config::SpecMap TpgFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // General — VideoFormat provides both the raster (1920×1080)
        // and frame rate (29.97) in a single key, replacing the former
        // separate FrameRate + VideoSize pair.
        s(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        // Video — enabled by default so an unconfigured TPG produces
        // a usable 1080p29.97 colour-bars stream out of the box.
        s(MediaConfig::VideoEnabled, true);
        s(MediaConfig::VideoPattern, VideoPattern::ColorBars);
        s(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGB8_sRGB));
        s(MediaConfig::VideoSolidColor, Color::Black);
        s(MediaConfig::VideoMotion, 0.0);
        // Video burn-in — on by default so the plain TPG stream shows
        // timecode out of the box.  Font size 0 means "auto":
        // VideoTestPattern scales from image height (36px at 1080p).
        s(MediaConfig::VideoBurnEnabled, true);
        s(MediaConfig::VideoBurnFontPath, String());
        s(MediaConfig::VideoBurnFontSize, int32_t(0));
        s(MediaConfig::VideoBurnText, String("{Meta.Timecode:smpte}"));
        s(MediaConfig::VideoBurnPosition, BurnPosition::BottomCenter);
        s(MediaConfig::VideoBurnTextColor, Color::White);
        s(MediaConfig::VideoBurnBgColor, Color::Black);
        s(MediaConfig::VideoBurnDrawBg, true);
        // Audio — enabled by default with a @c PcmMarker on every
        // channel so the plain TPG stream stamps each channel with
        // its own @c [stream:8][channel:8][frame:48] codeword.  The
        // marker is sample-accurate, decodable on any PCM format, and
        // robust through ordinary sample-rate conversion — see
        // @ref AudioDataEncoder.  Extra channels beyond the list are
        // silenced; the user remains free to pin specific channels to
        // @c LTC, @c AvSync, @c Tone, etc. via the
        // @ref MediaConfig::AudioChannelModes config key.
        EnumList defaultAudioModes = EnumList::forType<AudioPattern>();
        for (int i = 0; i < 16; ++i) defaultAudioModes.append(AudioPattern::PcmMarker);
        s(MediaConfig::AudioEnabled, true);
        s(MediaConfig::AudioChannelModes, defaultAudioModes);
        s(MediaConfig::AudioRate, 48000.0f);
        s(MediaConfig::AudioChannels, int32_t(2));
        s(MediaConfig::AudioToneFrequency, 1000.0);
        s(MediaConfig::AudioToneLevel, -20.0);
        s(MediaConfig::AudioLtcLevel, -20.0);
        s(MediaConfig::AudioChannelIdBaseFreq, 1000.0);
        s(MediaConfig::AudioChannelIdStepFreq, 100.0);
        s(MediaConfig::AudioChirpStartFreq, 20.0);
        s(MediaConfig::AudioChirpEndFreq, 20000.0);
        s(MediaConfig::AudioChirpDurationSec, 1.0);
        s(MediaConfig::AudioDualToneFreq1, 60.0);
        s(MediaConfig::AudioDualToneFreq2, 7000.0);
        s(MediaConfig::AudioDualToneRatio, 0.25);
        s(MediaConfig::AudioNoiseBufferSec, 10.0);
        s(MediaConfig::AudioNoiseSeed, uint32_t(0x505244A4u));
        // Timecode
        s(MediaConfig::TimecodeEnabled, true);
        s(MediaConfig::TimecodeStart, String("01:00:00:00"));
        s(MediaConfig::TimecodeDropFrame, false);
        // VITC-style binary data encoder pass — on by default so an
        // out-of-the-box TPG stream carries machine-readable frame and
        // timecode identifiers in the top of every frame.
        s(MediaConfig::TpgDataEncoderEnabled, true);
        s(MediaConfig::TpgDataEncoderRepeatLines, int32_t(16));
        s(MediaConfig::StreamID, uint32_t(0));
        return specs;
}

Metadata TpgFactory::defaultMetadata() const {
        // TPG is a pure generator — it does not consume container-
        // level metadata.  The only metadata key it *produces* on
        // every frame is Timecode; everything else a caller stamps
        // via setMetadata() flows through unchanged to downstream
        // consumers (the SDL player ignores it; a file sink picks
        // it up).  Return the Timecode key so the schema dump shows
        // it as the one well-known slot.
        Metadata m;
        m.set(Metadata::Timecode, Timecode());
        return m;
}

MediaIO *TpgFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new TpgMediaIO(parent);
        io->setConfig(config);
        return io;
}

TpgMediaIO::TpgMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

TpgMediaIO::~TpgMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error TpgMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        // -- VideoFormat (required) --
        VideoFormat vfmt = cfg.getAs<VideoFormat>(MediaConfig::VideoFormat);
        if (!vfmt.isValid()) {
                promekiErr("TpgMediaIO: invalid or missing VideoFormat");
                return Error::InvalidArgument;
        }
        _frameRate = vfmt.frameRate();

        MediaDesc mediaDesc;
        mediaDesc.setFrameRate(_frameRate);

        // -- Video --
        _videoEnabled = cfg.getAs<bool>(MediaConfig::VideoEnabled, false);
        if (_videoEnabled) {
                Error patErr;
                Enum  patEnum = cfg.get(MediaConfig::VideoPattern).asEnum(VideoPattern::Type, &patErr);
                if (patErr.isError() || !patEnum.hasListedValue()) {
                        promekiErr("TpgMediaIO: unknown pattern '%s'",
                                   cfg.get(MediaConfig::VideoPattern).get<String>().cstr());
                        return Error::InvalidArgument;
                }
                _videoPattern.setPattern(VideoPattern(patEnum.value()));

                Size2Du32   size = vfmt.raster();
                PixelFormat pd =
                        cfg.getAs<PixelFormat>(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGB8_sRGB));
                if (!size.isValid()) {
                        promekiErr("TpgMediaIO: invalid raster dimensions %s", size.toString().cstr());
                        return Error::InvalidDimension;
                }

                _imageDesc = ImageDesc(size.width(), size.height(), pd.id());
                mediaDesc.imageList().pushToBack(_imageDesc);

                // Build the binary data encoder up-front so the
                // per-frame hot path is just memcpy and a single CRC
                // pass.  If the user has disabled it (or the image is
                // too narrow for any cell width) we silently skip the
                // pass on each frame.
                _dataEncoderEnabled = cfg.getAs<bool>(MediaConfig::TpgDataEncoderEnabled, true);
                _dataEncoderRepeat = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::TpgDataEncoderRepeatLines, 16));
                // Stream ID lives in 8 bits — the TPG-shared 64-bit
                // codeword carries the layout
                // @c [stream:8][channel:8][frame:48], so anything above
                // @c 0xff is folded down via masking rather than
                // rejected.  Audio markers reuse the same stream value.
                _streamId = cfg.getAs<uint32_t>(MediaConfig::StreamID, uint32_t(0)) & 0xffu;
                if (_dataEncoderEnabled) {
                        _dataEncoder = ImageDataEncoder(_imageDesc);
                        if (!_dataEncoder.isValid()) {
                                promekiWarn("TpgMediaIO: image %s too narrow "
                                            "for binary data encoder; skipping pass",
                                            size.toString().cstr());
                                _dataEncoderEnabled = false;
                        }
                }

                Color solidColor = cfg.getAs<Color>(MediaConfig::VideoSolidColor, Color::Black);
                _videoPattern.setSolidColor(solidColor);

                _motion = cfg.getAs<double>(MediaConfig::VideoMotion, 0.0);
                _motionOffset = 0.0;

                // Burn-in configuration.  VideoTestPattern owns the
                // cached background and applies the burn on a copy, so
                // there's no pre-render pass needed here.  The actual
                // burn text is computed per-frame from
                // @ref _burnTextTemplate via @ref VariantLookup<Frame>::format
                // after the frame's metadata has been populated.
                _burnEnabled = cfg.getAs<bool>(MediaConfig::VideoBurnEnabled, true);
                _videoPattern.setBurnEnabled(_burnEnabled);
                if (_burnEnabled) {
                        _videoPattern.setBurnFontFilename(cfg.getAs<String>(MediaConfig::VideoBurnFontPath, String()));
                        _videoPattern.setBurnFontSize(cfg.getAs<int>(MediaConfig::VideoBurnFontSize, 0));
                        _burnTextTemplate = cfg.getAs<String>(MediaConfig::VideoBurnText, String());
                        _videoPattern.setBurnTextColor(cfg.getAs<Color>(MediaConfig::VideoBurnTextColor, Color::White));
                        _videoPattern.setBurnBackgroundColor(
                                cfg.getAs<Color>(MediaConfig::VideoBurnBgColor, Color::Black));
                        _videoPattern.setBurnDrawBackground(cfg.getAs<bool>(MediaConfig::VideoBurnDrawBg, true));
                        Error posErr;
                        Enum  posEnum = cfg.get(MediaConfig::VideoBurnPosition).asEnum(BurnPosition::Type, &posErr);
                        if (posErr.isError() || !posEnum.hasListedValue()) {
                                promekiErr("TpgMediaIO: unknown burn position '%s'",
                                           cfg.get(MediaConfig::VideoBurnPosition).get<String>().cstr());
                                return Error::InvalidArgument;
                        }
                        _videoPattern.setBurnPosition(BurnPosition(posEnum.value()));
                }
        }

        // -- Audio --
        _audioEnabled = cfg.getAs<bool>(MediaConfig::AudioEnabled, false);
        if (_audioEnabled) {
                float audioRate = cfg.getAs<float>(MediaConfig::AudioRate, 48000.0f);
                int   audioChannels = cfg.getAs<int>(MediaConfig::AudioChannels, 2);
                _audioDesc = AudioDesc(audioRate, audioChannels);
                if (!_audioDesc.isValid()) {
                        promekiErr("TpgMediaIO: invalid audio desc");
                        return Error::InvalidArgument;
                }

                mediaDesc.audioList().pushToBack(_audioDesc);

                _audioPattern = AudioTestPattern::UPtr::create(_audioDesc);

                // Pull the per-channel mode list.  Fall back to a
                // silent list when the config is missing so at least
                // open() succeeds — the user gets an all-silent audio
                // stream rather than a hard failure.
                EnumList channelModes = cfg.get(MediaConfig::AudioChannelModes).get<EnumList>();
                if (!channelModes.isValid()) {
                        channelModes = EnumList::forType<AudioPattern>();
                }
                if (channelModes.elementType() != AudioPattern::Type) {
                        promekiErr("TpgMediaIO: AudioChannelModes has wrong "
                                   "element type");
                        _audioPattern.clear();
                        return Error::InvalidArgument;
                }
                _audioPattern->setChannelModes(channelModes);

                double   toneFreq = cfg.getAs<double>(MediaConfig::AudioToneFrequency, 1000.0);
                double   toneLevel = cfg.getAs<double>(MediaConfig::AudioToneLevel, -20.0);
                double   ltcLevel = cfg.getAs<double>(MediaConfig::AudioLtcLevel, -20.0);
                double   chanBase = cfg.getAs<double>(MediaConfig::AudioChannelIdBaseFreq, 1000.0);
                double   chanStep = cfg.getAs<double>(MediaConfig::AudioChannelIdStepFreq, 100.0);
                double   chirpStart = cfg.getAs<double>(MediaConfig::AudioChirpStartFreq, 20.0);
                double   chirpEnd = cfg.getAs<double>(MediaConfig::AudioChirpEndFreq, 20000.0);
                double   chirpDur = cfg.getAs<double>(MediaConfig::AudioChirpDurationSec, 1.0);
                double   dt1 = cfg.getAs<double>(MediaConfig::AudioDualToneFreq1, 60.0);
                double   dt2 = cfg.getAs<double>(MediaConfig::AudioDualToneFreq2, 7000.0);
                double   dtRatio = cfg.getAs<double>(MediaConfig::AudioDualToneRatio, 0.25);
                double   noiseSec = cfg.getAs<double>(MediaConfig::AudioNoiseBufferSec, 1.0);
                uint32_t noiseSeed = cfg.getAs<uint32_t>(MediaConfig::AudioNoiseSeed, uint32_t(0x505244A4u));

                _audioPattern->setToneFrequency(toneFreq);
                _audioPattern->setToneLevel(AudioLevel::fromDbfs(toneLevel));
                _audioPattern->setLtcLevel(AudioLevel::fromDbfs(ltcLevel));
                _audioPattern->setChannelIdBaseFreq(chanBase);
                _audioPattern->setChannelIdStepFreq(chanStep);
                _audioPattern->setChirpStartFreq(chirpStart);
                _audioPattern->setChirpEndFreq(chirpEnd);
                _audioPattern->setChirpDurationSec(chirpDur);
                _audioPattern->setDualToneFreq1(dt1);
                _audioPattern->setDualToneFreq2(dt2);
                _audioPattern->setDualToneRatio(dtRatio);
                _audioPattern->setNoiseBufferSeconds(noiseSec);
                _audioPattern->setNoiseSeed(noiseSeed);
                _audioPattern->setPcmMarkerStreamId(static_cast<uint8_t>(_streamId & 0xffu));
                _audioPattern->configure();
        }

        // -- Timecode --
        _timecodeEnabled = cfg.getAs<bool>(MediaConfig::TimecodeEnabled, false);
        if (_timecodeEnabled) {
                _tcGen = TimecodeGenerator();

                Variant tcVar = cfg.get(MediaConfig::TimecodeValue);
                if (tcVar.isValid()) {
                        _tcGen.setTimecode(tcVar.get<Timecode>());
                } else {
                        String tcStr = cfg.getAs<String>(MediaConfig::TimecodeStart, "00:00:00:00");
                        if (!tcStr.isEmpty()) {
                                auto [tc, tcErr] = Timecode::fromString(tcStr);
                                if (tcErr.isOk()) {
                                        _tcGen.setTimecode(tc);
                                }
                        }
                }

                bool dropFrame = cfg.getAs<bool>(MediaConfig::TimecodeDropFrame, false);
                _tcGen.setDropFrame(dropFrame);
                _tcGen.setFrameRate(_frameRate);
        }

        // Must have at least one component enabled
        if (!_videoEnabled && !_audioEnabled && !_timecodeEnabled) {
                promekiErr("TpgMediaIO: no components enabled");
                return Error::InvalidArgument;
        }

        _frameCount = 0;

        // Declare the port shape: TPG is a generator with one source
        // in a single-port group, so we build one group + one source.
        // The default-clock overload of addPortGroup synthesizes a
        // MediaIOClock for the group, since TPG has no hardware
        // timing reference of its own.
        MediaIOPortGroup *group = addPortGroup("av");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(_frameRate);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);

        if (addSource(group, mediaDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error TpgMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        _audioPattern.clear();
        _imageDesc = ImageDesc();
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();
        _tcGen.reset();
        _frameCount = 0;
        _motionOffset = 0.0;
        _videoEnabled = false;
        _audioEnabled = false;
        _timecodeEnabled = false;
        _dataEncoder = ImageDataEncoder();
        _dataEncoderEnabled = false;
        _videoPattern.setBurnEnabled(false);
        _burnEnabled = false;
        _burnTextTemplate = String();
        return Error::Ok;
}

Error TpgMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        int s = cmd.step;

        // The TPG ignores step direction for the timecode generator's run mode;
        // each read processes |step| advances.  (Previously the TPG cached this
        // via setStep on the base class — now it's per-read so the task is
        // stateless from MediaIO's perspective.)
        if (_timecodeEnabled) {
                if (s > 0)
                        _tcGen.setRunMode(TimecodeGenerator::Forward);
                else if (s < 0)
                        _tcGen.setRunMode(TimecodeGenerator::Reverse);
                else
                        _tcGen.setRunMode(TimecodeGenerator::Still);
        }

        Frame::Ptr frame = Frame::Ptr::create();

        // Advance timecode by |step| frames (or hold at step=0).
        Timecode tc;
        if (_timecodeEnabled) {
                int advances = (s >= 0) ? s : -s;
                if (advances == 0) {
                        tc = _tcGen.timecode();
                } else {
                        tc = _tcGen.advance();
                        for (int i = 1; i < advances; i++) _tcGen.advance();
                }
        }

        // Audio.  Compute the per-frame sample count via the rational
        // cadence in FrameRate so fractional NTSC rates (29.97, 59.94,
        // ...) emit alternating sample counts whose cumulative total
        // matches wall-clock time exactly.  The tc may be invalid
        // (timecode generation disabled) — the audio pattern handles
        // that gracefully (LTC and AvSync degrade to silence).
        if (_audioEnabled && _audioPattern.isValid()) {
                size_t samples =
                        _frameRate.samplesPerFrame(static_cast<int64_t>(_audioDesc.sampleRate()), _frameCount.value());
                // Lock the audio PcmMarker frame number to the same
                // counter the video data band carries so a downstream
                // consumer can match audio markers to their video
                // frame by exact equality of the low 48 bits.
                _audioPattern->setPcmMarkerFrameNumber(_frameCount.value());
                auto payload = _audioPattern->createPayload(samples, tc);
                if (payload.isValid()) {
                        frame.modify()->addPayload(payload);
                }
        }

        // Video background — pure pattern, no burn yet.  Pushed to the
        // frame so the per-frame burn template (resolved below) can see
        // it via {Video[0].*} and {VideoFormat}.
        if (_videoEnabled) {
                auto payload =
                        _videoPattern.createPayload(_imageDesc, _motionOffset, _timecodeEnabled ? tc : Timecode());
                if (payload.isValid()) {
                        if (_timecodeEnabled) {
                                // Stamp on the descriptor's metadata —
                                // that's what toImage() surfaces on
                                // the materialised Image's metadata()
                                // and what payload-native readers see
                                // via VideoPayload::desc().metadata().
                                payload.modify()->desc().metadata().set(Metadata::Timecode, tc);
                        }
                        frame.modify()->addPayload(payload);
                }
        }

        // Frame-level metadata.  Written before the burn template
        // resolves so {Meta.Timecode}, {Meta.FrameRate}, and {VideoFormat}
        // all pick it up.  FrameRate is required for VideoFormat to
        // render its rate component.
        frame.modify()->metadata().set(Metadata::FrameRate, _frameRate);
        if (_timecodeEnabled) {
                frame.modify()->metadata().set(Metadata::Timecode, tc);
        }

        // Resolve and apply the burn / data-encoder against the
        // now-populated frame.  Mutations go through the last video
        // payload @ref Frame::addImage pushed onto
        // @ref payloadList — downstream pipeline stages consume the
        // payload list, and the paint / data-encoder entries take an
        // @ref UncompressedVideoPayload directly.
        //
        // To keep the mutation in the list's slot (rather than on a
        // CoW-clone the caller never sees) we call @c modify() on
        // the @ref MediaPayload::Ptr stored in the list.  CoW clones
        // the payload when it's shared — the virtual clone hook
        // preserves the @c UncompressedVideoPayload type — so the
        // subsequent @c static_cast is safe.
        auto lastVideoPayloadSlot = [&](Frame *f) -> MediaPayload::Ptr * {
                MediaPayload::PtrList &list = f->payloadList();
                for (size_t i = list.size(); i > 0; --i) {
                        MediaPayload::Ptr &slot = list[i - 1];
                        if (!slot.isValid()) continue;
                        if (slot->as<UncompressedVideoPayload>()) return &slot;
                }
                return nullptr;
        };

        if (_videoEnabled && _burnEnabled && !_burnTextTemplate.isEmpty()) {
                String burnText = VariantLookup<Frame>::format(*frame, _burnTextTemplate);
                if (!burnText.isEmpty()) {
                        if (MediaPayload::Ptr *slot = lastVideoPayloadSlot(frame.modify())) {
                                auto *uvp = static_cast<UncompressedVideoPayload *>(slot->modify());
                                uvp->ensureExclusive();
                                Error burnErr = _videoPattern.applyBurn(*uvp, burnText);
                                if (burnErr.isError()) {
                                        promekiWarn("TpgMediaIO: applyBurn failed: %s", burnErr.name().cstr());
                                }
                        }
                }
        }

        // Binary data encoder pass — runs last so the stamped band
        // sits on top of both the pattern and any burn-in.  We emit
        // two items per frame:
        //   item 0: TPG-shared 64-bit codeword
        //           @c [stream:8][channel:8][frame:48].  Video uses
        //           @c channel=0; the audio PcmMarker pattern reuses
        //           the same layout with the channel index in bits
        //           48..55, so a downstream consumer can correlate
        //           audio and video by stream + frame.
        //   item 1: BCD timecode word (Timecode::toBcd64).
        if (_videoEnabled && _dataEncoderEnabled && _dataEncoder.isValid()) {
                constexpr uint64_t kFrameMask = 0x0000ffffffffffffULL;
                const uint64_t     frameId = (static_cast<uint64_t>(_streamId & 0xffu) << 56) |
                                         (static_cast<uint64_t>(0u) << 48) | (_frameCount.value() & kFrameMask);
                const uint64_t               tcBcd = (_timecodeEnabled && tc.isValid()) ? tc.toBcd64() : uint64_t(0);
                List<ImageDataEncoder::Item> items;
                items.pushToBack({0, _dataEncoderRepeat, frameId});
                items.pushToBack({_dataEncoderRepeat, _dataEncoderRepeat, tcBcd});
                if (MediaPayload::Ptr *slot = lastVideoPayloadSlot(frame.modify())) {
                        auto *uvp = static_cast<UncompressedVideoPayload *>(slot->modify());
                        uvp->ensureExclusive();
                        Error encErr = _dataEncoder.encode(*uvp, items);
                        if (encErr.isError()) {
                                promekiWarn("TpgMediaIO: data encoder pass "
                                            "failed: %s",
                                            encErr.name().cstr());
                        }
                }
        }

        // Advance motion by step (negative step reverses direction)
        if (_videoEnabled && _motion != 0.0 && s != 0) {
                double fpsVal = _frameRate.toDouble();
                if (fpsVal > 0.0) {
                        _motionOffset += _motion * (double)s * (double)_imageDesc.size().width() / fpsVal;
                        double period = (double)_imageDesc.size().width();
                        if (period > 0.0) {
                                while (_motionOffset >= period) _motionOffset -= period;
                                while (_motionOffset < 0.0) _motionOffset += period;
                        }
                }
        }

        cmd.frame = std::move(frame);
        ++_frameCount;
        cmd.currentFrame = toFrameNumber(_frameCount);
        return Error::Ok;
}

// ---- Phase 3 introspection / negotiation overrides ----

MediaDesc TpgMediaIO::producedFromConfig(const MediaIO::Config &cfg) const {
        // Layer the user's config on top of TPG's spec defaults so
        // describe() / proposeOutput report a meaningful shape even
        // when a caller built the MediaIO with just `Type=TPG` and
        // no other keys.  The factory does not auto-merge defaults,
        // so we do it here.
        MediaIO::Config merged = MediaIOFactory::defaultConfig("TPG");
        cfg.forEach([&merged](MediaConfig::ID id, const Variant &val) { merged.set(id, val); });

        MediaDesc         md;
        const VideoFormat vfmt = merged.getAs<VideoFormat>(MediaConfig::VideoFormat);
        if (vfmt.isValid()) {
                md.setFrameRate(vfmt.frameRate());
                if (merged.getAs<bool>(MediaConfig::VideoEnabled, true)) {
                        const PixelFormat pd = merged.getAs<PixelFormat>(MediaConfig::VideoPixelFormat,
                                                                         PixelFormat(PixelFormat::RGB8_sRGB));
                        ImageDesc         img(vfmt.raster().width(), vfmt.raster().height(), pd.id());
                        img.setVideoScanMode(vfmt.videoScanMode());
                        md.imageList().pushToBack(img);
                }
        }
        if (merged.getAs<bool>(MediaConfig::AudioEnabled, true)) {
                AudioDesc ad;
                ad.setSampleRate(merged.getAs<float>(MediaConfig::AudioRate, 48000.0f));
                ad.setChannels(static_cast<unsigned int>(merged.getAs<int>(MediaConfig::AudioChannels, 2)));
                ad.setFormat(AudioFormat::PCMI_Float32LE);
                md.audioList().pushToBack(ad);
        }
        return md;
}

Error TpgMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        // Let the base populate identity / role flags / instance
        // identity / per-port snapshots first.  We supplement with
        // backend-specific @c preferredFormat + @c producibleFormats.
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;
        const MediaDesc preferred = producedFromConfig(config());
        if (preferred.isValid()) {
                out->setPreferredFormat(preferred);
                out->producibleFormats().pushToBack(preferred);
        }
        out->setFrameCount(MediaIODescription::FrameCountInfinite);
        return Error::Ok;
}

Error TpgMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const {
        // TPG can synthesise at any reasonable shape.  The planner
        // can ask for an alternative pixel format / raster / frame
        // rate via @p requested; we accept any uncompressed
        // PixelFormat and any valid frame rate.  (When the planner
        // doesn't ask for anything specific it falls back to
        // describe().preferredFormat.)
        if (achievable == nullptr) return Error::Invalid;
        if (!requested.isValid()) return Error::NotSupported;
        for (const auto &img : requested.imageList()) {
                if (img.pixelFormat().isCompressed()) {
                        // TPG produces uncompressed frames only.
                        return Error::NotSupported;
                }
        }
        *achievable = requested;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
