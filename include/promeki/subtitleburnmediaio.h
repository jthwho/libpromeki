/**
 * @file      subtitleburnmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/anctranslator.h>
#include <promeki/captiondecoder.h>
#include <promeki/enumlist.h>
#include <promeki/enums_subtitle.h>
#include <promeki/list.h>
#include <promeki/mediaiofactory.h>
#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/string.h>
#include <promeki/subtitlerenderer.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that paints @ref Subtitle cues onto video.
 * @ingroup proav
 *
 * Transform-mode MediaIO that consumes frames carrying a
 * @c Metadata::Subtitle cue (or, when configured, decodes a CEA-608
 * cue from the frame's @c AncPayloads via @ref Cea608Decoder) and
 * paints the cue onto the paired video payload using
 * @ref SubtitleRenderer.  Frames with no active cue pass through
 * unchanged.
 *
 * @par Cue sources
 *
 * The set of sources (and their priority) is driven by the ordered
 * @ref MediaConfig::VideoSubtitleBurnSources @c EnumList.  The
 * renderer queries each source in the order listed and paints the
 * first cue it finds.  Available sources:
 *
 *  - @ref SubtitleSource::Metadata — @c Metadata::Subtitle on the
 *    frame.  Producers like @ref TpgMediaIO stamp this every frame
 *    a cue is active.
 *  - @ref SubtitleSource::Cea608Anc — CEA-608 in @c AncPayloads.
 *    Each frame's CEA-708 CDP packets are walked and the
 *    @c cc_data triples fed into a stateful @ref CaptionDecoder for
 *    @ref CaptionCodec::Cea608.  The decoder's @c displayedCue is
 *    painted with its style + anchor recovered from PAC + mid-row
 *    codes.
 *  - @ref SubtitleSource::Cea708Anc — CEA-708 DTVCC in
 *    @c AncPayloads.  Each frame's CDP packets are walked and the
 *    @c cc_data triples fed into a stateful @ref CaptionDecoder for
 *    @ref CaptionCodec::Cea708 targeting the primary service (1).
 *    Coexists with @c Cea608Anc — both decoders ride in the same
 *    @ref CaptionDecoder::PtrList and the first one to surface a
 *    non-empty cue wins (per the ordered preference rule).
 *
 * The default value is @c [Metadata] — frame-stamped cues only.
 * To prefer in-band ANC captions and fall back to frame metadata:
 * @code
 * VideoSubtitleBurnSources=Cea708Anc,Cea608Anc,Metadata
 * @endcode
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform.
 *
 * @par Back-pressure
 *
 * Mirrors @ref BurnMediaIO: an internal output FIFO; writes always
 * succeed; reads return @c Error::TryAgain when the FIFO is empty;
 * capacity is configurable via @ref MediaConfig::Capacity.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoSubtitleBurnEnabled    | bool                | true        | Enable subtitle burn-in. |
 * | @ref MediaConfig::VideoSubtitleBurnFontPath   | String              | ""          | TrueType font path. |
 * | @ref MediaConfig::VideoSubtitleBurnFontSize   | int                 | 0           | Font size (0 = auto-scale from frame height). |
 * | @ref MediaConfig::VideoSubtitleBurnTextColor  | Color               | White       | Default text colour. |
 * | @ref MediaConfig::VideoSubtitleBurnBgColor    | Color               | Black       | Background colour. |
 * | @ref MediaConfig::VideoSubtitleBurnDrawBg     | bool                | true        | Draw bg rectangle. |
 * | @ref MediaConfig::VideoSubtitleBurnAnchor     | Enum SubtitleAnchor | Default     | Anchor override (Default = honour cue). |
 * | @ref MediaConfig::VideoSubtitleBurnSources    | EnumList SubtitleSource | [Metadata] | Ordered list of cue sources to consult. |
 * | @ref MediaConfig::Capacity                    | int                 | 4           | Output FIFO depth. |
 *
 * @par Allocator policy
 *
 * Same as @ref BurnMediaIO — no allocator override.  The pass
 * mutates the upstream @c UncompressedVideoPayload via
 * @c MediaPayload::Ptr::modify() + @c ensureExclusive(), so the
 * upstream's allocator picks the residency and CoW behaviour.
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 *
 * @see Subtitle, SubtitleRenderer, BurnMediaIO, TpgMediaIO
 */
class SubtitleBurnMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(SubtitleBurnMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — total frames with a subtitle painted. */
                static inline const MediaIOStats::ID StatsFramesPainted{"FramesPainted"};

                SubtitleBurnMediaIO(ObjectBase *parent = nullptr);
                ~SubtitleBurnMediaIO() override;

                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable,
                                    MediaConfig *configDelta = nullptr) const override;
                int   pendingInternalWrites() const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

        private:
                Error burnFrame(const Frame &input, Frame &output);

                /// @brief Picks the active cue for @p input.  Returns
                ///        an invalid (empty) Subtitle when nothing is
                ///        active.
                Subtitle pickCue(const Frame &input);

                /// @brief Returns @c true when @p src is listed in
                ///        the configured source preferences.
                bool sourceEnabled(const SubtitleSource &src) const;

                /// @brief Tries one specific cue source.  Returns an
                ///        invalid (empty) Subtitle when nothing fires.
                Subtitle tryMetadataSource(const Frame &input);

                /// @brief Pushes the frame's CDP @c cc_data into the
                ///        @ref _ancDecoders entry whose @ref
                ///        CaptionDecoder::codec matches @p codec, and
                ///        returns that decoder's @c displayedCue.
                ///        Returns an empty @ref Subtitle when no
                ///        decoder for @p codec is configured.
                Subtitle tryAncSource(const Frame &input, CaptionCodec codec);

                SubtitleRenderer _renderer;
                bool             _enabled = false;
                EnumList         _sources; ///< Ordered preference list (EnumList<SubtitleSource>).
                int              _capacity = 4;

                Frame::List _outputQueue;
                FrameCount  _frameCount{0};
                int64_t     _readCount = 0;
                FrameCount  _framesPainted{0};
                bool        _capacityWarned = false;
                bool        _notPaintableWarned = false;

                // Lazily-constructed ANC helpers (only used when the
                // corresponding @c SubtitleSource is listed in
                // @c VideoSubtitleBurnSources).  Translator is a plain
                // value shared across both decoders; each decoder is
                // stateful and pimpl'd.  One @ref CaptionDecoder per
                // codec rides in @c _ancDecoders, looked up by
                // @ref CaptionDecoder::codec.
                AncTranslator           _ancTranslator;
                CaptionDecoder::PtrList _ancDecoders;
};

/**
 * @brief @ref MediaIOFactory for the SubtitleBurn backend.
 * @ingroup proav
 */
class SubtitleBurnFactory : public MediaIOFactory {
        public:
                SubtitleBurnFactory() = default;

                String name() const override { return String("SubtitleBurn"); }
                String displayName() const override { return String("Subtitle Burn-in"); }
                String description() const override {
                        return String("Renders the active Metadata::Subtitle cue (or a CEA-608 ANC-decoded "
                                      "cue) onto each video frame.");
                }
                bool canBeTransform() const override { return true; }

                Config::SpecMap configSpecs() const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
