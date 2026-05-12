/**
 * @file      tpgmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cmath>
#include <promeki/tpgmediaio.h>
#include <promeki/videoformat.h>
#include <promeki/file.h>
#include <promeki/frame.h>
#include <promeki/memspace.h>
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
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea608.h>
#include <promeki/scc.h>
#include <promeki/subrip.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        /**
         * @brief MediaIOAllocator that hands out SystemCow-backed
         *        video planes for cheap per-frame burn-in detach.
         *
         * TPG caches its rendered background once per
         * (descriptor + pattern + solid colour) configuration.  The
         * per-frame burn-in then writes a small text band on top.
         * Without SystemCow, the @c ensureExclusive() that detaches
         * the cached payload before burning copies the entire frame
         * (~8.3 MB at 1080p RGBA8, ~33 MB at 4K) — pure CPU memory
         * traffic with no algorithmic value.
         *
         * With SystemCow, the cached payload is sealed once at the
         * end of populate; per-frame @c ensureExclusive maps a fresh
         * @c MAP_PRIVATE clone of the sealed memfd.  Pages that
         * @c applyBurn doesn't dirty stay shared with the cache;
         * only the burn band's pages CoW.
         *
         * Audio + generic bytes fall through to the base allocator
         * (default heap) — there's no SystemCow win for those paths.
         */
        class TpgAllocator : public MediaIOAllocator {
                public:
                        PROMEKI_SHARED_DERIVED(TpgAllocator)

                        String name() const override { return String("TpgAllocator"); }

                        Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override {
                                const PixelFormat &pf = desc.pixelFormat();
                                if (!pf.isValid() || !desc.size().isValid()) return Buffer();
                                if (planeIndex < 0 || planeIndex >= static_cast<int>(pf.planeCount())) return Buffer();
                                const size_t bytes = pf.planeSize(static_cast<size_t>(planeIndex), desc);
                                if (bytes == 0) return Buffer();
                                Buffer buf(bytes, Buffer::DefaultAlign, MemSpace::SystemCow);
                                if (buf.isValid()) buf.setSize(bytes);
                                return buf;
                        }
        };
}

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
        s(MediaConfig::VideoBurnText, String("{Meta.Timecode:smpte}\n{VideoFormat}"));
        s(MediaConfig::VideoBurnPosition, BurnPosition::BottomCenter);
        s(MediaConfig::VideoBurnTextColor, Color::White);
        s(MediaConfig::VideoBurnBgColor, Color::Black);
        s(MediaConfig::VideoBurnDrawBg, true);
        // Motion band — on by default so the plain TPG stream gives
        // an observer an immediate visual cue for frame stutter / drop
        // / repeat.  Cycle length is computed from the configured
        // FrameRate at open time; height 0 picks the default.
        s(MediaConfig::VideoMotionBandEnabled, true);
        s(MediaConfig::VideoMotionBandHeight, int32_t(0));
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
        // CEA-708 caption ANC — disabled by default so the plain TPG
        // stream stays compatible with consumers that don't expect ANC.
        // Flip on with TpgAncCaptionsEnabled = true and point
        // TpgAncCaptionsFile at a SubRip (`.srt`) file.  The TPG
        // parses it once at open time, runs a Cea608Encoder against
        // the cue timeline, and injects the encoder's per-frame
        // CcDataList into a CEA-708 CDP on every emitted frame.
        s(MediaConfig::TpgAncCaptionsEnabled, false);
        s(MediaConfig::TpgAncCaptionsFile, String());
        s(MediaConfig::TpgAncCaptionsOffset, Duration());
        s(MediaConfig::TpgAncCaptionsLine, int32_t(11));
        s(MediaConfig::TpgAncCaptionsScc, String());
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
                        // Reserve the data-encoder band at the top of
                        // the frame so a Top-* burn doesn't get
                        // overwritten by the encoder pass that runs
                        // after applyBurn.  The encoder stamps two
                        // bands of @c _dataEncoderRepeat scan lines
                        // each, starting at line 0.
                        const int reservedLines =
                                (_dataEncoderEnabled) ? static_cast<int>(2u * _dataEncoderRepeat) : 0;
                        _videoPattern.setBurnTopReserved(reservedLines);
                }

                // Motion band — parallel to burn-in.  Cycle length is
                // rounded num/den so 29.97 → 30 frames, 23.976 → 24,
                // etc., giving the marker a once-per-second wrap that
                // lands on the same pixel each cycle.  Burn-in's
                // bottom-position math automatically respects the
                // band's reservedLines, so nothing else needs to know
                // it's enabled.
                _motionBandEnabled = cfg.getAs<bool>(MediaConfig::VideoMotionBandEnabled, true);
                _videoPattern.motionBand().setEnabled(_motionBandEnabled);
                if (_motionBandEnabled) {
                        const int bandHeight = cfg.getAs<int>(MediaConfig::VideoMotionBandHeight, 0);
                        _videoPattern.motionBand().setHeight(bandHeight);
                        const unsigned int num = _frameRate.numerator();
                        const unsigned int den = _frameRate.denominator();
                        const int          seqLen = (den > 0) ? static_cast<int>((num + den / 2u) / den) : 0;
                        _videoPattern.motionBand().setSequenceLength(seqLen);
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

        // -- ANC (CEA-708 caption injection) --
        _ancCaptionsEnabled = cfg.getAs<bool>(MediaConfig::TpgAncCaptionsEnabled, false);
        if (_ancCaptionsEnabled) {
                _ancCaptionsFile = cfg.getAs<String>(MediaConfig::TpgAncCaptionsFile, String());
                _ancCaptionsOffset = cfg.getAs<Duration>(MediaConfig::TpgAncCaptionsOffset, Duration());
                _ancCaptionsLine = static_cast<uint16_t>(
                        cfg.getAs<int32_t>(MediaConfig::TpgAncCaptionsLine, int32_t(11)));
                _ancSequenceCounter = 0;
                _ancSccBypassActive = false;
                _ancSccByFrame.clear();

                const String sccPath = cfg.getAs<String>(MediaConfig::TpgAncCaptionsScc, String());
                if (!sccPath.isEmpty() && !_ancCaptionsFile.isEmpty()) {
                        promekiErr("TpgMediaIO: TpgAncCaptionsFile and TpgAncCaptionsScc are "
                                   "mutually exclusive; set one or the other (file='%s', scc='%s')",
                                   _ancCaptionsFile.cstr(), sccPath.cstr());
                        return Error::InvalidArgument;
                }
                if (!sccPath.isEmpty()) {
                        // SCC bypass: load + parse the SCC file, then
                        // pre-compute a frame-indexed lookup of byte
                        // pairs.  The first SCC row's TC anchors to TPG
                        // frame 0; subsequent rows shift by the absolute
                        // frame-count delta from the first row.
                        File   sccFile(sccPath);
                        Buffer sccBytes;
                        Error  openErr = sccFile.open(IODevice::ReadOnly);
                        if (openErr.isError()) {
                                promekiErr("TpgMediaIO: failed to open SCC file '%s': %s",
                                           sccPath.cstr(), openErr.name().cstr());
                                return openErr;
                        }
                        Result<int64_t> szR = sccFile.size();
                        if (szR.second().isError()) {
                                sccFile.close();
                                return szR.second();
                        }
                        const int64_t sz = szR.first();
                        if (sz > 0) {
                                sccBytes = Buffer(static_cast<size_t>(sz));
                                sccBytes.setSize(static_cast<size_t>(sz));
                                int64_t got = sccFile.read(sccBytes.data(), sz);
                                if (got != sz) {
                                        sccFile.close();
                                        return Error::IOError;
                                }
                        }
                        sccFile.close();
                        Result<Scc> sccR = Scc::fromBuffer(sccBytes);
                        if (sccR.second().isError()) {
                                promekiErr("TpgMediaIO: SCC parse failed for '%s': %s",
                                           sccPath.cstr(), sccR.second().name().cstr());
                                return sccR.second();
                        }
                        const Scc      &doc = sccR.first();
                        const auto      sccCcType = static_cast<uint8_t>(0); // SCC is field-1 only.
                        FrameNumber     firstAbs = FrameNumber::unknown();
                        for (size_t li = 0; li < doc.size(); ++li) {
                                const Scc::Line &row = doc.lines()[li];
                                FrameNumber      absFrame = row.start.toFrameNumber();
                                if (!absFrame.isValid()) continue;
                                if (!firstAbs.isValid()) firstAbs = absFrame;
                                const int64_t relFrame = absFrame.value() - firstAbs.value();
                                for (size_t bp = 0; bp < row.bytePairs.size(); ++bp) {
                                        const uint16_t v = row.bytePairs[bp];
                                        const uint8_t  b1 = static_cast<uint8_t>((v >> 8) & 0xFF);
                                        const uint8_t  b2 = static_cast<uint8_t>(v & 0xFF);
                                        Cea708Cdp::CcDataList list;
                                        // SCC bytes already carry odd parity (real captioner
                                        // output) so we pass them through verbatim — no
                                        // additional parity stamping.
                                        list.pushToBack(Cea708Cdp::CcData{true, sccCcType, b1, b2});
                                        _ancSccByFrame.insert(relFrame + static_cast<int64_t>(bp), list);
                                }
                        }
                        _ancSccBypassActive = true;
                }

                // Load and shift the SubRip file if one is configured.
                // Empty path = no cues; the TPG still emits per-frame
                // CDPs carrying null caption pairs so the receiver sees
                // a steady stream.
                _ancCaptions = SubtitleList();
                if (!_ancCaptionsFile.isEmpty()) {
                        File   srtFile(_ancCaptionsFile);
                        Buffer srtBytes;
                        Error  openErr = srtFile.open(IODevice::ReadOnly);
                        if (openErr.isError()) {
                                promekiErr("TpgMediaIO: failed to open SubRip file '%s': %s",
                                           _ancCaptionsFile.cstr(), openErr.name().cstr());
                                return openErr;
                        }
                        Result<int64_t> szR = srtFile.size();
                        if (szR.second().isError()) {
                                promekiErr("TpgMediaIO: stat failed on SubRip file '%s': %s",
                                           _ancCaptionsFile.cstr(), szR.second().name().cstr());
                                srtFile.close();
                                return szR.second();
                        }
                        const int64_t sz = szR.first();
                        if (sz > 0) {
                                srtBytes = Buffer(static_cast<size_t>(sz));
                                srtBytes.setSize(static_cast<size_t>(sz));
                                int64_t got = srtFile.read(srtBytes.data(), sz);
                                if (got != sz) {
                                        promekiErr("TpgMediaIO: short read on SubRip file '%s': %lld / %lld bytes",
                                                   _ancCaptionsFile.cstr(), static_cast<long long>(got),
                                                   static_cast<long long>(sz));
                                        srtFile.close();
                                        return Error::IOError;
                                }
                        }
                        srtFile.close();
                        Result<SubtitleList> r = SubRip::parse(srtBytes);
                        if (r.second().isError()) {
                                promekiErr("TpgMediaIO: SubRip parse failed for '%s': %s",
                                           _ancCaptionsFile.cstr(), r.second().name().cstr());
                                return r.second();
                        }
                        _ancCaptions = r.first();
                        // Apply the configured offset to every cue.
                        // Negative offsets that would push a cue's start
                        // below t=0 are detected later by the encoder's
                        // pre-roll check (Error::OutOfRange).
                        if (_ancCaptionsOffset.nanoseconds() != 0) {
                                using ClockDur = TimeStamp::Value::duration;
                                const ClockDur shift = std::chrono::duration_cast<ClockDur>(
                                        std::chrono::nanoseconds(_ancCaptionsOffset.nanoseconds()));
                                SubtitleList shifted;
                                shifted.reserve(_ancCaptions.size());
                                for (size_t i = 0; i < _ancCaptions.size(); ++i) {
                                        Subtitle s = _ancCaptions[i];
                                        s.setStart(TimeStamp(s.start().value() + shift));
                                        s.setEnd(TimeStamp(s.end().value() + shift));
                                        shifted.append(s);
                                }
                                _ancCaptions = shifted;
                        }
                        _ancCaptions.sortByStart();
                        // Snap each cue's start / end to the nearest
                        // frame boundary at the configured rate.
                        // Authoring tools emit ms-precision; the
                        // pipeline is frame-quantised — making the
                        // snap explicit keeps per-frame equality
                        // checks (metadata stamping, encoder schedule)
                        // exact across NTSC fractional rates.
                        _ancCaptions = _ancCaptions.snapToFrames(_frameRate);
                }

                // Instantiate the Cea608Encoder and load the cues.
                // Filter cues that wouldn't fit the encoder's pre-roll
                // / back-to-back constraints first — a SubRip file
                // whose first cue starts at frame 0 (or at < pre-roll
                // frames from t=0) is common and shouldn't make the
                // whole pipeline refuse to open.  Dropped cues are
                // logged as warnings; the rest still encode normally.
                //
                // Skipped entirely when SCC bypass is active — the
                // SCC byte pairs feed the cc_data directly without
                // going through the encoder's state machine.
                if (!_ancSccBypassActive) {
                        Cea608Encoder::Config encCfg;
                        encCfg.frameRate = _frameRate;
                        _ancCaptionEncoder = std::make_unique<Cea608Encoder>(encCfg);
                        SubtitleList dropped;
                        SubtitleList kept = _ancCaptionEncoder->encodableSubset(_ancCaptions, &dropped);
                        for (size_t di = 0; di < dropped.size(); ++di) {
                                const Subtitle &d = dropped[di];
                                promekiWarn(
                                        "TpgMediaIO: dropping caption cue %lld..%lld (\"%s\") — too "
                                        "close to t=0 or overlaps the prior cue's wire stream",
                                        static_cast<long long>(d.start().value().time_since_epoch().count()),
                                        static_cast<long long>(d.end().value().time_since_epoch().count()),
                                        d.text().cstr());
                        }
                        _ancCaptions = kept;
                        Error encErr = _ancCaptionEncoder->setSubtitles(_ancCaptions);
                        if (encErr.isError()) {
                                promekiErr(
                                        "TpgMediaIO: Cea608Encoder::setSubtitles failed: %s "
                                        "(check that cue start times are far enough from t=0 "
                                        "to honour pre-roll, and that consecutive cues don't "
                                        "overlap)",
                                        encErr.name().cstr());
                                return encErr;
                        }
                } else {
                        _ancCaptionEncoder.reset();
                }
                // SMPTE 334-2 §5.1.4 frame-rate codes.  We pick the
                // closest match for the configured FrameRate; unknowns
                // emit code 0 (CDP still round-trips structurally).
                const unsigned int num = _frameRate.numerator();
                const unsigned int den = _frameRate.denominator();
                if (num == 24000 && den == 1001)
                        _ancFrameRateCode = 1; // 23.976
                else if (num == 24 && den == 1)
                        _ancFrameRateCode = 2; // 24
                else if (num == 25 && den == 1)
                        _ancFrameRateCode = 3; // 25
                else if (num == 30000 && den == 1001)
                        _ancFrameRateCode = 4; // 29.97
                else if (num == 30 && den == 1)
                        _ancFrameRateCode = 5; // 30
                else if (num == 50 && den == 1)
                        _ancFrameRateCode = 6; // 50
                else if (num == 60000 && den == 1001)
                        _ancFrameRateCode = 7; // 59.94
                else if (num == 60 && den == 1)
                        _ancFrameRateCode = 8; // 60
                else
                        _ancFrameRateCode = 0;

                // Build the per-stream descriptor advertised through MediaDesc.
                _ancDesc = AncDesc();
                _ancDesc.setSourceRaster(_imageDesc.size());
                _ancDesc.setScanMode(VideoScanMode::Progressive);
                _ancDesc.setFrameRate(_frameRate);
                AncFormat::IDList allowed;
                allowed.pushToBack(AncFormat::Cea708);
                _ancDesc.setAllowedFormats(allowed);
                mediaDesc.ancList().pushToBack(_ancDesc);

                // Stamp the build-line into the held translator config
                // so every per-frame build() emits packets on the
                // configured VANC line.
                AncTranslateConfig ancCfg;
                ancCfg.set(AncTranslateConfig::St291BuildLine, _ancCaptionsLine);
                _ancTranslator.setConfig(ancCfg);
        }

        // Must have at least one component enabled
        if (!_videoEnabled && !_audioEnabled && !_timecodeEnabled && !_ancCaptionsEnabled) {
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

        // Install the SystemCow-routing allocator so the cached video
        // background is sealed once after populate; per-frame burn-in
        // detaches the cached payload via MAP_PRIVATE clone instead of
        // memcpy'ing the full frame.  ROLLBACK POINT: removing the
        // next two lines reverts placement to the default heap-backed
        // allocator without disturbing any of the SystemCow / allocator
        // infrastructure — see devplan/core/systemcow-mediaio-allocator.md.
        auto allocator = MediaIOAllocator::Ptr::takeOwnership(new TpgAllocator());
        setAllocator(allocator);
        _videoPattern.setAllocator(allocator);
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
        _videoPattern.setBurnTopReserved(0);
        _videoPattern.motionBand().setEnabled(false);
        _motionBandEnabled = false;
        _burnEnabled = false;
        _burnTextTemplate = String();
        _ancCaptionsEnabled = false;
        _ancCaptionsFile = String();
        _ancCaptionsOffset = Duration();
        _ancSequenceCounter = 0;
        _ancFrameRateCode = 0;
        _ancDesc = AncDesc();
        _ancTranslator = AncTranslator();
        _ancCaptions = SubtitleList();
        _ancCaptionEncoder.reset();
        _ancSccBypassActive = false;
        _ancSccByFrame.clear();
        return Error::Ok;
}

Error TpgMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        // The TPG ignores rate direction for the timecode generator's run
        // mode; each read processes |step| advances.  Pull the integer
        // per-tick advance out of the group's fractional-rate accumulator
        // so the TPG honours slow-motion (rate=0.5 → alternating 0/1
        // advances) and fast-forward (rate=2 → +2 per tick).
        const int s = (cmd.group != nullptr) ? cmd.group->nextStep() : 1;
        if (_timecodeEnabled) {
                if (s > 0)
                        _tcGen.setRunMode(TimecodeGenerator::Forward);
                else if (s < 0)
                        _tcGen.setRunMode(TimecodeGenerator::Reverse);
                else
                        _tcGen.setRunMode(TimecodeGenerator::Still);
        }

        Frame frame = Frame();

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
                        frame.addPayload(payload);
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
                        frame.addPayload(payload);
                }
        }

        // Frame-level metadata.  Written before the burn template
        // resolves so {Meta.Timecode}, {Meta.FrameRate}, and {VideoFormat}
        // all pick it up.  FrameRate is required for VideoFormat to
        // render its rate component.
        frame.metadata().set(Metadata::FrameRate, _frameRate);
        if (_timecodeEnabled) {
                frame.metadata().set(Metadata::Timecode, tc);
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
        auto lastVideoPayloadSlot = [&](Frame &f) -> MediaPayload::Ptr * {
                MediaPayload::PtrList &list = f.payloadList();
                for (size_t i = list.size(); i > 0; --i) {
                        MediaPayload::Ptr &slot = list[i - 1];
                        if (!slot.isValid()) continue;
                        if (slot->as<UncompressedVideoPayload>()) return &slot;
                }
                return nullptr;
        };

        // Motion band — runs before burn so the burn lands on top of
        // the band (in practice they're spatially disjoint because
        // burn-position math respects MotionBand::reservedLines, but
        // the order is well-defined just in case a caller forces a
        // Bottom* burn into the band region anyway).
        if (_videoEnabled && _motionBandEnabled) {
                if (MediaPayload::Ptr *slot = lastVideoPayloadSlot(frame)) {
                        auto *uvp = static_cast<UncompressedVideoPayload *>(slot->modify());
                        uvp->ensureExclusive();
                        Error mbErr = _videoPattern.applyMotionBand(*uvp, _frameCount.value());
                        if (mbErr.isError()) {
                                promekiWarn("TpgMediaIO: applyMotionBand failed: %s", mbErr.name().cstr());
                        }
                }
        }

        if (_videoEnabled && _burnEnabled && !_burnTextTemplate.isEmpty()) {
                String burnText = VariantLookup<Frame>::format(frame, _burnTextTemplate);
                if (!burnText.isEmpty()) {
                        if (MediaPayload::Ptr *slot = lastVideoPayloadSlot(frame)) {
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
                if (MediaPayload::Ptr *slot = lastVideoPayloadSlot(frame)) {
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

        // -- CEA-708 caption ANC injection --
        if (_ancCaptionsEnabled && (_ancCaptionEncoder != nullptr || _ancSccBypassActive)) {
                // Pull the byte-pair for this frame from the SubRip-
                // driven Cea608Encoder, or from the SCC bypass map if
                // configured.  Empty frames carry the null pair
                // (0x80, 0x80) so the receiver sees a steady stream
                // of cc_data triples — that's what real broadcast
                // captioners do.
                const FrameNumber     fn(_frameCount.value());
                Cea708Cdp::CcDataList triples;
                if (_ancSccBypassActive) {
                        auto it = _ancSccByFrame.find(_frameCount.value());
                        if (it != _ancSccByFrame.end()) {
                                triples = it->second;
                        } else {
                                triples.pushToBack(Cea708Cdp::CcData{
                                        true, 0, Cea608::withOddParity(Cea608::NullB1),
                                        Cea608::withOddParity(Cea608::NullB2)});
                        }
                } else {
                        triples = _ancCaptionEncoder->nextFrame(fn);
                }
                Cea708Cdp cdp(_ancFrameRateCode, triples, _ancSequenceCounter);
                cdp.timeCodePresent = _timecodeEnabled;
                if (_timecodeEnabled) cdp.timeCode = tc;

                Result<AncPacket> r = _ancTranslator.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                                            AncTransport::St291);
                if (r.second().isOk()) {
                        AncPayload::Ptr ancPayload = AncPayload::Ptr::create(_ancDesc);
                        ancPayload.modify()->addPacket(r.first());
                        frame.addPayload(ancPayload);
                } else {
                        promekiWarn("TpgMediaIO: CEA-708 ANC build failed: %s", r.second().name().cstr());
                }
                ++_ancSequenceCounter;

                // Stamp the active Subtitle into the Frame's metadata
                // for *every* frame in the cue's display window.
                // Renderers (SubtitleBurn) and other downstream
                // consumers that don't decode ANC pull the active cue
                // straight off the Frame each tick — they need it on
                // every frame the cue is visible, not just the start.
                // The CoW Subtitle handle makes per-frame re-stamping
                // essentially free (a refcount bump).
                if (!_ancCaptions.isEmpty()) {
                        // Compute this frame's media-relative TimeStamp
                        // (epoch = TPG t=0).  Use FrameRate's exact
                        // rational helper to avoid drift on NTSC rates.
                        using ClockDur = TimeStamp::Value::duration;
                        const int64_t   nsPerSec = 1'000'000'000;
                        const int64_t   tickNs = _frameRate.cumulativeTicks(nsPerSec, _frameCount.value());
                        const ClockDur  frameDur =
                                std::chrono::duration_cast<ClockDur>(std::chrono::nanoseconds(tickNs));
                        const TimeStamp frameTs{TimeStamp::Value(frameDur)};
                        int64_t         idx = _ancCaptions.findActiveAt(frameTs);
                        if (idx >= 0) {
                                const Subtitle &active = _ancCaptions[static_cast<size_t>(idx)];
                                frame.metadata().set(Metadata::Subtitle, Variant(active));
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
        // Report the frame number that was just produced — the in-frame
        // payloads (audio PcmMarker, motion band cycle index, data
        // encoder frameId) all consumed _frameCount before this point,
        // so currentFrame must mirror that pre-increment value to keep
        // every per-frame identifier in lockstep starting at 0.
        cmd.currentFrame = toFrameNumber(_frameCount);
        ++_frameCount;
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

Error TpgMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable, MediaConfig *configDelta) const {
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

        // When the planner asks for a config delta, translate the
        // requested shape into the @ref MediaConfig keys that drive
        // it.  The pipeline planner merges this onto the source
        // stage's config so TPG opens with the negotiated shape and
        // no CSC / resampler bridge is needed downstream.
        if (configDelta != nullptr) {
                if (!requested.imageList().isEmpty()) {
                        const ImageDesc &img = requested.imageList()[0];
                        if (img.pixelFormat().isValid()) {
                                configDelta->set(MediaConfig::VideoPixelFormat, img.pixelFormat());
                        }
                        // Stamp VideoFormat when the requested raster
                        // and frame rate are both valid — the planner
                        // may have asked for a different shape, so we
                        // build a matching VideoFormat from the
                        // ad-hoc components rather than relying on
                        // the well-known table.
                        if (img.size().isValid() && requested.frameRate().isValid()) {
                                VideoFormat vfmt(img.size(), requested.frameRate(), img.videoScanMode());
                                if (vfmt.isValid()) {
                                        configDelta->set(MediaConfig::VideoFormat, vfmt);
                                }
                        }
                }
                if (!requested.audioList().isEmpty()) {
                        const AudioDesc &aud = requested.audioList()[0];
                        if (aud.sampleRate() > 0.0f) {
                                configDelta->set(MediaConfig::AudioRate, aud.sampleRate());
                        }
                        if (aud.channels() > 0) {
                                configDelta->set(MediaConfig::AudioChannels, static_cast<int32_t>(aud.channels()));
                        }
                }
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
