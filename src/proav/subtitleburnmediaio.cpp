/**
 * @file      subtitleburnmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/cea608decoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/colormodel.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/imagedesc.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/subtitle.h>
#include <promeki/subtitleburnmediaio.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(SubtitleBurnFactory)

MediaIOFactory::Config::SpecMap SubtitleBurnFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::VideoSubtitleBurnEnabled, true);
        s(MediaConfig::VideoSubtitleBurnFontPath, String());
        s(MediaConfig::VideoSubtitleBurnFontSize, int32_t(0));
        s(MediaConfig::VideoSubtitleBurnTextColor, Color::White);
        s(MediaConfig::VideoSubtitleBurnBgColor, Color::Black);
        s(MediaConfig::VideoSubtitleBurnDrawBg, true);
        s(MediaConfig::VideoSubtitleBurnAnchor, SubtitleAnchor::Default);
        {
                EnumList defSources = EnumList::forType<SubtitleSource>();
                defSources.append(SubtitleSource::Metadata);
                s(MediaConfig::VideoSubtitleBurnSources, defSources);
        }
        s(MediaConfig::Capacity, int32_t(4));
        return specs;
}

MediaIO *SubtitleBurnFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new SubtitleBurnMediaIO(parent);
        io->setConfig(config);
        return io;
}

SubtitleBurnMediaIO::SubtitleBurnMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

SubtitleBurnMediaIO::~SubtitleBurnMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error SubtitleBurnMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        _enabled = cfg.getAs<bool>(MediaConfig::VideoSubtitleBurnEnabled, true);

        // Resolve the ordered source preference list.  Default is
        // [Metadata] — the cheapest path that works for any upstream
        // that stamps Metadata::Subtitle (TPG, future subtitle
        // sources).  Adding @c Cea608Anc enables the in-band ANC
        // decode fallback (or priority, depending on order).
        _sources = EnumList::forType<SubtitleSource>();
        const Variant &srcVar = cfg.get(MediaConfig::VideoSubtitleBurnSources);
        if (srcVar.type() == Variant::TypeEnumList) {
                EnumList parsed = srcVar.get<EnumList>();
                if (parsed.elementType() == SubtitleSource::Type) {
                        _sources = parsed;
                }
        }
        if (_sources.isEmpty()) {
                // Empty list = no rendering.  Reflect this by clearing
                // @c _enabled so the per-frame fast path bypasses
                // every cue lookup.
                _enabled = false;
        }

        _renderer.setFontFilename(cfg.getAs<String>(MediaConfig::VideoSubtitleBurnFontPath, String()));
        _renderer.setFontSize(cfg.getAs<int>(MediaConfig::VideoSubtitleBurnFontSize, 0));
        _renderer.setDefaultForeground(cfg.getAs<Color>(MediaConfig::VideoSubtitleBurnTextColor, Color::White));
        _renderer.setDefaultBackground(cfg.getAs<Color>(MediaConfig::VideoSubtitleBurnBgColor, Color::Black));
        _renderer.setDrawBackground(cfg.getAs<bool>(MediaConfig::VideoSubtitleBurnDrawBg, true));

        Error posErr;
        Enum  anchorEnum =
                cfg.get(MediaConfig::VideoSubtitleBurnAnchor).asEnum(SubtitleAnchor::Type, &posErr);
        if (posErr.isError() || !anchorEnum.hasListedValue()) {
                promekiErr("SubtitleBurnMediaIO: unknown subtitle anchor '%s'",
                           cfg.get(MediaConfig::VideoSubtitleBurnAnchor).get<String>().cstr());
                return Error::InvalidArgument;
        }
        _renderer.setAnchorOverride(SubtitleAnchor(anchorEnum.value()));

        _capacity = cfg.getAs<int>(MediaConfig::Capacity, 4);
        if (_capacity < 1) _capacity = 1;

        _frameCount = 0;
        _readCount = 0;
        _framesPainted = 0;
        _outputQueue.clear();

        // Lazy-initialise the CEA-608 decoder when the ANC source is
        // listed in the preferences.  The decoder is stateful across
        // frames, so we hold one instance for the lifetime of this
        // open() session.
        if (sourceEnabled(SubtitleSource::Cea608Anc)) {
                _ancDecoder = UniquePtr<Cea608Decoder>::create();
                _ancTranslator = AncTranslator();
        } else {
                _ancDecoder = UniquePtr<Cea608Decoder>();
                _ancTranslator = AncTranslator();
        }

        MediaIOPortGroup *group = addPortGroup("subtitleburn");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(cmd.pendingMediaDesc.frameRate());
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        // SubtitleBurn passes the input shape through unchanged — the
        // overlay rides on top of the existing pixels.
        if (addSource(group, cmd.pendingMediaDesc) == nullptr) return Error::Invalid;
        return Error::Ok;
}

Error SubtitleBurnMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        _outputQueue.clear();
        _enabled = false;
        _sources = EnumList();
        _frameCount = 0;
        _readCount = 0;
        _framesPainted = 0;
        _capacityWarned = false;
        _notPaintableWarned = false;
        _ancDecoder = UniquePtr<Cea608Decoder>();
        _ancTranslator = AncTranslator();
        return Error::Ok;
}

bool SubtitleBurnMediaIO::sourceEnabled(const SubtitleSource &src) const {
        if (!_sources.isValid()) return false;
        const auto &values = _sources.values();
        for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] == src.value()) return true;
        }
        return false;
}

Subtitle SubtitleBurnMediaIO::tryMetadataSource(const Frame &input) {
        if (!input.metadata().contains(Metadata::Subtitle)) return Subtitle();
        Variant v = input.metadata().get(Metadata::Subtitle);
        if (v.type() != Variant::TypeSubtitle) return Subtitle();
        Subtitle s = v.get<Subtitle>();
        if (s.isEmpty()) return Subtitle();
        return s;
}

Subtitle SubtitleBurnMediaIO::tryCea608AncSource(const Frame &input) {
        if (_ancDecoder.isNull()) return Subtitle();
        const AncPayload::PtrList ancList = input.ancPayloads();
        if (ancList.isEmpty()) return Subtitle();

        FrameNumber frameNumber = toFrameNumber(_frameCount);
        // The decoder uses ts as the cue start / end stamp.  Frame
        // carries a captureTime (TimeStamp + ClockDomain) — pull the
        // bare TimeStamp out; an unset captureTime falls back to a
        // default-constructed TimeStamp which the decoder treats as
        // "t=0 of this session" — still strictly monotonic with the
        // frame ordering, which is the only invariant the decoder
        // depends on for cue ordering.
        TimeStamp ts = input.captureTime().isValid() ? input.captureTime().timeStamp() : TimeStamp();
        for (size_t i = 0; i < ancList.size(); ++i) {
                if (!ancList[i].isValid()) continue;
                const AncPacket::List &pkts = ancList[i]->packets();
                for (size_t j = 0; j < pkts.size(); ++j) {
                        const AncPacket &pkt = pkts[j];
                        if (pkt.format().id() != AncFormat::Cea708) continue;
                        Result<Variant> parsed = _ancTranslator.parse(pkt);
                        if (!parsed.second().isOk()) continue;
                        if (parsed.first().type() != Variant::TypeCea708Cdp) continue;
                        const Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                        if (cdp.ccData.isEmpty()) continue;
                        _ancDecoder->pushFrame(frameNumber, ts, cdp.ccData);
                }
        }
        // Pull the styled cue (spans + anchor recovered from PAC +
        // mid-row codes), not just the flat displayedText() — the
        // renderer wants the SubtitleSpan list with bold/italic/
        // underline/colour and the SubtitleAnchor so the wire-
        // carried attributes actually paint.
        return _ancDecoder->displayedCue();
}

Subtitle SubtitleBurnMediaIO::pickCue(const Frame &input) {
        // Walk the configured preference list in order; return the
        // first source that produces a non-empty cue.  Sources not
        // listed are skipped entirely (and, in the case of
        // Cea608Anc, the per-frame decoder is never even constructed
        // — see executeCmd(Open)).
        //
        // Note: for the ANC source we still want to *push* every
        // incoming frame's cc_data into the decoder even when an
        // earlier-priority source already returned a cue — otherwise
        // a later switch from Metadata→Cea608Anc would see a stale
        // decoder.  The current behaviour leaves that aside since
        // the source list is fixed at open() time; a follow-up can
        // tee the ANC feed if it becomes a real need.
        if (!_sources.isValid()) return Subtitle();
        const auto &values = _sources.values();
        for (size_t i = 0; i < values.size(); ++i) {
                Subtitle cue;
                if (values[i] == SubtitleSource::Metadata.value()) {
                        cue = tryMetadataSource(input);
                } else if (values[i] == SubtitleSource::Cea608Anc.value()) {
                        cue = tryCea608AncSource(input);
                }
                if (!cue.isEmpty()) return cue;
        }
        return Subtitle();
}

Error SubtitleBurnMediaIO::burnFrame(const Frame &input, Frame &output) {
        if (!input.isValid()) return Error::Invalid;

        Frame outFrame = Frame();
        outFrame.metadata() = input.metadata();
        for (const MediaPayload::Ptr &srcP : input.payloadList()) {
                if (srcP.isValid()) outFrame.addPayload(srcP);
        }

        if (!_enabled) {
                output = std::move(outFrame);
                return Error::Ok;
        }

        Subtitle cue = pickCue(outFrame);
        if (cue.isEmpty()) {
                output = std::move(outFrame);
                return Error::Ok;
        }

        bool painted = false;
        for (MediaPayload::Ptr &payloadPtr : outFrame.payloadList()) {
                if (!payloadPtr.isValid()) continue;
                if (!payloadPtr->as<UncompressedVideoPayload>()) continue;

                auto              *uvp = static_cast<UncompressedVideoPayload *>(payloadPtr.modify());
                const PixelFormat &pf = uvp->desc().pixelFormat();
                if (!pf.hasPaintEngine()) {
                        if (!_notPaintableWarned) {
                                promekiWarn("SubtitleBurnMediaIO: pixel format %s has no "
                                            "paint engine; subtitle render skipped",
                                            pf.name().cstr());
                                _notPaintableWarned = true;
                        }
                        continue;
                }
                uvp->ensureExclusive();
                Error renderErr = _renderer.render(cue, *uvp);
                if (renderErr.isError()) {
                        promekiWarn("SubtitleBurnMediaIO: render failed: %s", renderErr.name().cstr());
                        continue;
                }
                painted = true;
        }
        if (painted) _framesPainted++;

        output = std::move(outFrame);
        return Error::Ok;
}

Error SubtitleBurnMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) {
                promekiErr("SubtitleBurnMediaIO: write with null frame");
                return Error::InvalidArgument;
        }

        if (static_cast<int>(_outputQueue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("SubtitleBurnMediaIO: output queue exceeded capacity (%d >= %d)",
                            static_cast<int>(_outputQueue.size()), _capacity);
                _capacityWarned = true;
        }

        Frame outFrame;
        Error err = burnFrame(cmd.frame, outFrame);
        if (err.isError()) return err;

        _outputQueue.pushToBack(std::move(outFrame));
        _frameCount++;
        cmd.currentFrame = toFrameNumber(_frameCount);
        cmd.frameCount = _frameCount;
        return Error::Ok;
}

Error SubtitleBurnMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (_outputQueue.isEmpty()) return Error::TryAgain;

        Frame frame = std::move(_outputQueue.front());
        _outputQueue.remove(0);
        _readCount++;
        cmd.frame = std::move(frame);
        cmd.currentFrame = _readCount;
        return Error::Ok;
}

Error SubtitleBurnMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesPainted, _framesPainted);
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_outputQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(_capacity));
        return Error::Ok;
}

int SubtitleBurnMediaIO::pendingInternalWrites() const { return static_cast<int>(_outputQueue.size()); }

// ---- Introspection / negotiation ----
//
// SubtitleBurn is a pure passthrough transform; the planner only
// needs to splice a CSC ahead of us when the upstream offers a
// non-paintable format.

Error SubtitleBurnMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }
        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (pd.isValid() && !pd.isCompressed() && pd.hasPaintEngine()) {
                *preferred = offered;
                return Error::Ok;
        }
        const PixelFormat target = MediaIO::defaultUncompressedPixelFormat(pd);
        MediaDesc         want = offered;
        ImageDesc::List  &imgs = want.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelFormat(target);
        }
        *preferred = want;
        return Error::Ok;
}

Error SubtitleBurnMediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable,
                                         MediaConfig *configDelta) const {
        if (achievable == nullptr) return Error::Invalid;
        (void)configDelta;
        *achievable = requested;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
