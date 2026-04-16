/**
 * @file      mediaiotask_tpg.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/videoformat.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/audiolevel.h>
#include <promeki/enums.h>
#include <promeki/imagedataencoder.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_TPG)

MediaIO::FormatDesc MediaIOTask_TPG::formatDesc() {
        return {
                "TPG",
                "Video/audio/timecode test pattern generator",
                {},     // No file extensions — this is a generator
                true,   // canOutput
                false,  // canInput
                false,  // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_TPG();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        // General — VideoFormat provides both the
                        // raster (1920×1080) and frame rate (29.97)
                        // in a single key, replacing the former
                        // separate FrameRate + VideoSize pair.
                        s(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
                        // Video — enabled by default so an unconfigured
                        // TPG produces a usable 1080p29.97 colour-bars
                        // stream out of the box.
                        s(MediaConfig::VideoEnabled, true);
                        s(MediaConfig::VideoPattern, VideoPattern::ColorBars);
                        s(MediaConfig::VideoPixelFormat, PixelDesc(PixelDesc::RGB8_sRGB));
                        s(MediaConfig::VideoSolidColor, Color::Black);
                        s(MediaConfig::VideoMotion, 0.0);
                        // Video burn-in — on by default so the plain
                        // TPG stream shows timecode out of the box.
                        // Font size 0 means "auto": VideoTestPattern
                        // scales from image height (36px at 1080p).
                        s(MediaConfig::VideoBurnEnabled, true);
                        s(MediaConfig::VideoBurnFontPath, String());
                        s(MediaConfig::VideoBurnFontSize, int32_t(0));
                        s(MediaConfig::VideoBurnText, String("{Timecode:smpte}"));
                        s(MediaConfig::VideoBurnPosition, BurnPosition::BottomCenter);
                        s(MediaConfig::VideoBurnTextColor, Color::White);
                        s(MediaConfig::VideoBurnBgColor, Color::Black);
                        s(MediaConfig::VideoBurnDrawBg, true);
                        // Audio — enabled by default, defaulting to
                        // LTC on ch0 and an AvSync click marker on
                        // ch1 so the plain TPG stream emits both a
                        // decodable timecode reference and a
                        // per-second click that pairs with the video
                        // AvSync pattern and the burn.  The channel
                        // list is interpreted positionally: extra
                        // channels beyond the list are silenced.
                        EnumList defaultAudioModes = EnumList::forType<AudioPattern>();
                        defaultAudioModes.append(AudioPattern::LTC);
                        defaultAudioModes.append(AudioPattern::AvSync);
                        s(MediaConfig::AudioEnabled, true);
                        s(MediaConfig::AudioChannelModes, defaultAudioModes);
                        s(MediaConfig::AudioRate, 48000.0f);
                        s(MediaConfig::AudioChannels, int32_t(2));
                        s(MediaConfig::AudioToneFrequency, 1000.0);
                        s(MediaConfig::AudioToneLevel, -20.0);
                        s(MediaConfig::AudioLtcLevel, -20.0);
                        s(MediaConfig::AudioChannelIdBaseFreq, 1000.0);
                        s(MediaConfig::AudioChannelIdStepFreq, 100.0);
                        s(MediaConfig::AudioChirpStartFreq,    20.0);
                        s(MediaConfig::AudioChirpEndFreq,      20000.0);
                        s(MediaConfig::AudioChirpDurationSec,  1.0);
                        s(MediaConfig::AudioDualToneFreq1,     60.0);
                        s(MediaConfig::AudioDualToneFreq2,     7000.0);
                        s(MediaConfig::AudioDualToneRatio,     0.25);
                        s(MediaConfig::AudioNoiseBufferSec,    10.0);
                        s(MediaConfig::AudioNoiseSeed,         uint32_t(0x505244A4u));
                        // Timecode
                        s(MediaConfig::TimecodeEnabled, true);
                        s(MediaConfig::TimecodeStart, String("01:00:00:00"));
                        s(MediaConfig::TimecodeDropFrame, false);
                        // VITC-style binary data encoder pass — on by
                        // default so an out-of-the-box TPG stream
                        // carries machine-readable frame and timecode
                        // identifiers in the top of every frame.
                        s(MediaConfig::TpgDataEncoderEnabled, true);
                        s(MediaConfig::TpgDataEncoderRepeatLines, int32_t(16));
                        s(MediaConfig::StreamID, uint32_t(0));
                        return specs;
                },
                []() -> Metadata {
                        // TPG is a pure generator — it does not
                        // consume container-level metadata.  The only
                        // metadata key it *produces* on every frame is
                        // Timecode; everything else a caller stamps
                        // via setMetadata() flows through unchanged to
                        // downstream consumers (the SDL player ignores
                        // it; a file sink picks it up).  Return the
                        // Timecode key so the schema dump shows it as
                        // the one well-known slot.
                        Metadata m;
                        m.set(Metadata::Timecode, Timecode());
                        return m;
                }
        };
}

MediaIOTask_TPG::~MediaIOTask_TPG() {
        delete _audioPattern;
}

Error MediaIOTask_TPG::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Output) return Error::NotSupported;

        const MediaIO::Config &cfg = cmd.config;

        // -- VideoFormat (required) --
        VideoFormat vfmt = cfg.getAs<VideoFormat>(MediaConfig::VideoFormat);
        if(!vfmt.isValid()) {
                promekiErr("MediaIOTask_TPG: invalid or missing VideoFormat");
                return Error::InvalidArgument;
        }
        _frameRate = vfmt.frameRate();

        MediaDesc mediaDesc;
        mediaDesc.setFrameRate(_frameRate);

        // -- Video --
        _videoEnabled = cfg.getAs<bool>(MediaConfig::VideoEnabled, false);
        if(_videoEnabled) {
                Error patErr;
                Enum patEnum = cfg.get(MediaConfig::VideoPattern).asEnum(VideoPattern::Type, &patErr);
                if(patErr.isError() || !patEnum.hasListedValue()) {
                        promekiErr("MediaIOTask_TPG: unknown pattern '%s'",
                                   cfg.get(MediaConfig::VideoPattern).get<String>().cstr());
                        return Error::InvalidArgument;
                }
                _videoPattern.setPattern(
                        static_cast<VideoTestPattern::Pattern>(patEnum.value()));

                Size2Du32 size = vfmt.raster();
                PixelDesc pd = cfg.getAs<PixelDesc>(MediaConfig::VideoPixelFormat, PixelDesc(PixelDesc::RGB8_sRGB));
                if(!size.isValid()) {
                        promekiErr("MediaIOTask_TPG: invalid raster dimensions %s",
                                   size.toString().cstr());
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
                _dataEncoderRepeat  = static_cast<uint32_t>(
                        cfg.getAs<int>(MediaConfig::TpgDataEncoderRepeatLines, 16));
                _streamId           = cfg.getAs<uint32_t>(MediaConfig::StreamID, uint32_t(0));
                if(_dataEncoderEnabled) {
                        _dataEncoder = ImageDataEncoder(_imageDesc);
                        if(!_dataEncoder.isValid()) {
                                promekiWarn("MediaIOTask_TPG: image %s too narrow "
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
                // @ref _burnTextTemplate via @ref Frame::makeString
                // after the frame's metadata has been populated.
                _burnEnabled = cfg.getAs<bool>(MediaConfig::VideoBurnEnabled, true);
                _videoPattern.setBurnEnabled(_burnEnabled);
                if(_burnEnabled) {
                        _videoPattern.setBurnFontFilename(
                                cfg.getAs<String>(MediaConfig::VideoBurnFontPath, String()));
                        _videoPattern.setBurnFontSize(
                                cfg.getAs<int>(MediaConfig::VideoBurnFontSize, 0));
                        _burnTextTemplate =
                                cfg.getAs<String>(MediaConfig::VideoBurnText, String());
                        _videoPattern.setBurnTextColor(
                                cfg.getAs<Color>(MediaConfig::VideoBurnTextColor, Color::White));
                        _videoPattern.setBurnBackgroundColor(
                                cfg.getAs<Color>(MediaConfig::VideoBurnBgColor, Color::Black));
                        _videoPattern.setBurnDrawBackground(
                                cfg.getAs<bool>(MediaConfig::VideoBurnDrawBg, true));
                        Error posErr;
                        Enum posEnum = cfg.get(MediaConfig::VideoBurnPosition)
                                           .asEnum(BurnPosition::Type, &posErr);
                        if(posErr.isError() || !posEnum.hasListedValue()) {
                                promekiErr("MediaIOTask_TPG: unknown burn position '%s'",
                                           cfg.get(MediaConfig::VideoBurnPosition)
                                                   .get<String>().cstr());
                                return Error::InvalidArgument;
                        }
                        _videoPattern.setBurnPosition(
                                static_cast<VideoTestPattern::BurnPosition>(posEnum.value()));
                }
        }

        // -- Audio --
        _audioEnabled = cfg.getAs<bool>(MediaConfig::AudioEnabled, false);
        if(_audioEnabled) {
                float audioRate = cfg.getAs<float>(MediaConfig::AudioRate, 48000.0f);
                int audioChannels = cfg.getAs<int>(MediaConfig::AudioChannels, 2);
                _audioDesc = AudioDesc(audioRate, audioChannels);
                if(!_audioDesc.isValid()) {
                        promekiErr("MediaIOTask_TPG: invalid audio desc");
                        return Error::InvalidArgument;
                }

                mediaDesc.audioList().pushToBack(_audioDesc);

                delete _audioPattern;
                _audioPattern = new AudioTestPattern(_audioDesc);

                // Pull the per-channel mode list.  Fall back to a
                // silent list when the config is missing so at least
                // open() succeeds — the user gets an all-silent audio
                // stream rather than a hard failure.
                EnumList channelModes =
                        cfg.get(MediaConfig::AudioChannelModes).get<EnumList>();
                if(!channelModes.isValid()) {
                        channelModes = EnumList::forType<AudioPattern>();
                }
                if(channelModes.elementType() != AudioPattern::Type) {
                        promekiErr("MediaIOTask_TPG: AudioChannelModes has wrong "
                                "element type");
                        delete _audioPattern;
                        _audioPattern = nullptr;
                        return Error::InvalidArgument;
                }
                _audioPattern->setChannelModes(channelModes);

                double toneFreq   = cfg.getAs<double>(MediaConfig::AudioToneFrequency, 1000.0);
                double toneLevel  = cfg.getAs<double>(MediaConfig::AudioToneLevel, -20.0);
                double ltcLevel   = cfg.getAs<double>(MediaConfig::AudioLtcLevel, -20.0);
                double chanBase   = cfg.getAs<double>(MediaConfig::AudioChannelIdBaseFreq, 1000.0);
                double chanStep   = cfg.getAs<double>(MediaConfig::AudioChannelIdStepFreq, 100.0);
                double chirpStart = cfg.getAs<double>(MediaConfig::AudioChirpStartFreq, 20.0);
                double chirpEnd   = cfg.getAs<double>(MediaConfig::AudioChirpEndFreq, 20000.0);
                double chirpDur   = cfg.getAs<double>(MediaConfig::AudioChirpDurationSec, 1.0);
                double dt1        = cfg.getAs<double>(MediaConfig::AudioDualToneFreq1, 60.0);
                double dt2        = cfg.getAs<double>(MediaConfig::AudioDualToneFreq2, 7000.0);
                double dtRatio    = cfg.getAs<double>(MediaConfig::AudioDualToneRatio, 0.25);
                double noiseSec   = cfg.getAs<double>(MediaConfig::AudioNoiseBufferSec, 1.0);
                uint32_t noiseSeed =
                        cfg.getAs<uint32_t>(MediaConfig::AudioNoiseSeed, uint32_t(0x505244A4u));

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
                _audioPattern->configure();
        }

        // -- Timecode --
        _timecodeEnabled = cfg.getAs<bool>(MediaConfig::TimecodeEnabled, false);
        if(_timecodeEnabled) {
                _tcGen = TimecodeGenerator();

                Variant tcVar = cfg.get(MediaConfig::TimecodeValue);
                if(tcVar.isValid()) {
                        _tcGen.setTimecode(tcVar.get<Timecode>());
                } else {
                        String tcStr = cfg.getAs<String>(MediaConfig::TimecodeStart, "00:00:00:00");
                        if(!tcStr.isEmpty()) {
                                auto [tc, tcErr] = Timecode::fromString(tcStr);
                                if(tcErr.isOk()) {
                                        _tcGen.setTimecode(tc);
                                }
                        }
                }

                bool dropFrame = cfg.getAs<bool>(MediaConfig::TimecodeDropFrame, false);
                _tcGen.setDropFrame(dropFrame);
                _tcGen.setFrameRate(_frameRate);
        }

        // Must have at least one component enabled
        if(!_videoEnabled && !_audioEnabled && !_timecodeEnabled) {
                promekiErr("MediaIOTask_TPG: no components enabled");
                return Error::InvalidArgument;
        }

        _frameCount = 0;

        // Fill output fields
        cmd.mediaDesc = mediaDesc;
        if(_audioEnabled) cmd.audioDesc = _audioDesc;
        cmd.frameRate = _frameRate;
        cmd.canSeek = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_TPG::executeCmd(MediaIOCommandClose &cmd) {
        delete _audioPattern;
        _audioPattern = nullptr;
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

Error MediaIOTask_TPG::executeCmd(MediaIOCommandRead &cmd) {
        stampWorkBegin();
        int s = cmd.step;

        // The TPG ignores step direction for the timecode generator's run mode;
        // each read processes |step| advances.  (Previously the TPG cached this
        // via setStep on the base class — now it's per-read so the task is
        // stateless from MediaIO's perspective.)
        if(_timecodeEnabled) {
                if(s > 0)      _tcGen.setRunMode(TimecodeGenerator::Forward);
                else if(s < 0) _tcGen.setRunMode(TimecodeGenerator::Reverse);
                else           _tcGen.setRunMode(TimecodeGenerator::Still);
        }

        Frame::Ptr frame = Frame::Ptr::create();

        // Advance timecode by |step| frames (or hold at step=0).
        Timecode tc;
        if(_timecodeEnabled) {
                int advances = (s >= 0) ? s : -s;
                if(advances == 0) {
                        tc = _tcGen.timecode();
                } else {
                        tc = _tcGen.advance();
                        for(int i = 1; i < advances; i++) _tcGen.advance();
                }
        }

        // Audio.  Compute the per-frame sample count via the rational
        // cadence in FrameRate so fractional NTSC rates (29.97, 59.94,
        // ...) emit alternating sample counts whose cumulative total
        // matches wall-clock time exactly.  The tc may be invalid
        // (timecode generation disabled) — the audio pattern handles
        // that gracefully (LTC and AvSync degrade to silence).
        if(_audioEnabled && _audioPattern != nullptr) {
                size_t samples = _frameRate.samplesPerFrame(
                        static_cast<int64_t>(_audioDesc.sampleRate()),
                        _frameCount);
                Audio audio = _audioPattern->create(samples, tc);
                if(audio.isValid()) {
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                }
        }

        // Video background — pure pattern, no burn yet.  Pushed to the
        // frame so the per-frame burn template (resolved below) can see
        // it via {Image[0].*} and {@VideoFormat}.
        if(_videoEnabled) {
                Image img = _videoPattern.create(_imageDesc, _motionOffset,
                                                 _timecodeEnabled ? tc : Timecode());
                if(_timecodeEnabled) {
                        img.metadata().set(Metadata::Timecode, tc);
                }
                frame.modify()->imageList().pushToBack(
                        Image::Ptr::create(std::move(img)));
        }

        // Frame-level metadata.  Written before the burn template
        // resolves so {Timecode}, {FrameRate}, and {@VideoFormat} all
        // pick it up.  FrameRate is required for VideoFormat to
        // render its rate component.
        frame.modify()->metadata().set(Metadata::FrameRate, _frameRate);
        if(_timecodeEnabled) {
                frame.modify()->metadata().set(Metadata::Timecode, tc);
        }

        // Resolve and apply the burn against the now-populated frame.
        // All mutations go through the Image::Ptr already inside the
        // frame's imageList so the frame always reflects the final
        // state.  modify() on the Ptr detaches from the cached
        // background plane; ensureExclusive() detaches the pixel
        // buffers so painting is safe.
        if(_videoEnabled && _burnEnabled && !_burnTextTemplate.isEmpty()) {
                String burnText = frame->makeString(_burnTextTemplate);
                if(!burnText.isEmpty()) {
                        Image *imgMut = frame.modify()->imageList().back().modify();
                        imgMut->ensureExclusive();
                        Error burnErr = _videoPattern.applyBurn(*imgMut, burnText);
                        if(burnErr.isError()) {
                                promekiWarn("MediaIOTask_TPG: applyBurn failed: %s",
                                            burnErr.name().cstr());
                        }
                }
        }

        // Binary data encoder pass — runs last so the stamped band
        // sits on top of both the pattern and any burn-in.  We emit
        // two items per frame:
        //   item 0: (streamID << 32) | frameNumber  (frame ID)
        //   item 1: BCD timecode word (Timecode::toBcd64)
        if(_videoEnabled && _dataEncoderEnabled && _dataEncoder.isValid()) {
                const uint64_t frameId =
                        (static_cast<uint64_t>(_streamId) << 32) |
                        static_cast<uint32_t>(_frameCount & 0xffffffffu);
                const uint64_t tcBcd =
                        (_timecodeEnabled && tc.isValid())
                                ? tc.toBcd64()
                                : uint64_t(0);
                List<ImageDataEncoder::Item> items;
                items.pushToBack({ 0, _dataEncoderRepeat, frameId });
                items.pushToBack({ _dataEncoderRepeat, _dataEncoderRepeat, tcBcd });
                Image *imgMut = frame.modify()->imageList().back().modify();
                imgMut->ensureExclusive();
                Error encErr = _dataEncoder.encode(*imgMut, items);
                if(encErr.isError()) {
                        promekiWarn("MediaIOTask_TPG: data encoder pass "
                                    "failed: %s", encErr.name().cstr());
                }
        }

        // Advance motion by step (negative step reverses direction)
        if(_videoEnabled && _motion != 0.0 && s != 0) {
                double fpsVal = _frameRate.toDouble();
                if(fpsVal > 0.0) {
                        _motionOffset += _motion * (double)s * (double)_imageDesc.size().width() / fpsVal;
                        double period = (double)_imageDesc.size().width();
                        if(period > 0.0) {
                                while(_motionOffset >= period) _motionOffset -= period;
                                while(_motionOffset < 0.0) _motionOffset += period;
                        }
                }
        }

        _frameCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _frameCount;
        stampWorkEnd();
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
