/**
 * @file      enums_subtitle.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Subtitle / closed-caption rendering and codec enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Well-known nine-position anchor for subtitle placement.
 *
 * The displayed-block anchor used by every subtitle format that
 * exposes positioning.  Values 1..9 match the ASS / SSA
 * @c {\anN} numpad convention exactly so SRT files carrying the
 * ASS extension can value-cast directly:
 *
 * @code
 * 1 = BottomLeft   2 = BottomCenter   3 = BottomRight
 * 4 = MiddleLeft   5 = MiddleCenter   6 = MiddleRight
 * 7 = TopLeft      8 = TopCenter      9 = TopRight
 * @endcode
 *
 * @c Default (0) means "no anchor explicitly specified by the
 * source file" — renderers fall back to their own default (almost
 * universally @c BottomCenter for caption-style subtitles).  Used
 * by @ref Subtitle::anchor.
 */
class SubtitleAnchor : public TypedEnum<SubtitleAnchor> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitleAnchor", "Subtitle Anchor Position", 0,
                                           {"Default", 0, "Default (Unspecified)"},
                                           {"BottomLeft", 1, "Bottom Left"},
                                           {"BottomCenter", 2, "Bottom Center"},
                                           {"BottomRight", 3, "Bottom Right"},
                                           {"MiddleLeft", 4, "Middle Left"},
                                           {"MiddleCenter", 5, "Middle Center"},
                                           {"MiddleRight", 6, "Middle Right"},
                                           {"TopLeft", 7, "Top Left"},
                                           {"TopCenter", 8, "Top Center"},
                                           {"TopRight", 9, "Top Right"}); // default: Default

                using TypedEnum<SubtitleAnchor>::TypedEnum;

                static const SubtitleAnchor Default;
                static const SubtitleAnchor BottomLeft;
                static const SubtitleAnchor BottomCenter;
                static const SubtitleAnchor BottomRight;
                static const SubtitleAnchor MiddleLeft;
                static const SubtitleAnchor MiddleCenter;
                static const SubtitleAnchor MiddleRight;
                static const SubtitleAnchor TopLeft;
                static const SubtitleAnchor TopCenter;
                static const SubtitleAnchor TopRight;
};

inline const SubtitleAnchor SubtitleAnchor::Default{0};
inline const SubtitleAnchor SubtitleAnchor::BottomLeft{1};
inline const SubtitleAnchor SubtitleAnchor::BottomCenter{2};
inline const SubtitleAnchor SubtitleAnchor::BottomRight{3};
inline const SubtitleAnchor SubtitleAnchor::MiddleLeft{4};
inline const SubtitleAnchor SubtitleAnchor::MiddleCenter{5};
inline const SubtitleAnchor SubtitleAnchor::MiddleRight{6};
inline const SubtitleAnchor SubtitleAnchor::TopLeft{7};
inline const SubtitleAnchor SubtitleAnchor::TopCenter{8};
inline const SubtitleAnchor SubtitleAnchor::TopRight{9};

/**
 * @brief Where a subtitle renderer should look for the active cue.
 *
 * Used by @ref SubtitleBurnMediaIO's
 * @c MediaConfig::VideoSubtitleBurnSources key as an *ordered*
 * preference list — the renderer queries each source in turn and
 * paints the first cue it finds.  An empty list disables rendering
 * entirely.
 *
 *  - @c Metadata — read @c Metadata::Subtitle off the frame.  Cheap
 *    and zero-coupling; works for any upstream that stamps cues
 *    (the TPG SubRip path, future file readers, etc.).
 *  - @c Cea608Anc — decode CEA-608 captions from the frame's
 *    @c AncPayloads via the stateful @ref Cea608Decoder.  Useful
 *    when subtitles arrive embedded in ANC (broadcast capture,
 *    RTP-40, NDI).  v1 supports the @c CC1 pop-on subset that
 *    @ref Cea608Decoder implements.
 *  - @c Cea708Anc — decode CEA-708 DTVCC captions from the frame's
 *    @c AncPayloads via the stateful @ref Cea708Decoder.  Walks
 *    the @c cc_type=2/3 triples of every CDP, runs the configured
 *    service block through the 8-window state machine, and surfaces
 *    @ref Cea708Decoder::displayedCue.  Defaults to service 1 (the
 *    primary English caption service); other services need the
 *    decoder configured explicitly (out of scope for v1 — exposed
 *    later through a renderer config key when a real multi-service
 *    stream lands).
 *
 * Future sources (@c HlsSei, @c RtmpAmf, @c NdiXml, file-driven
 * SubRip side-channel) slot in by adding new enum values and
 * matching handlers in @ref SubtitleBurnMediaIO.
 */
class SubtitleSource : public TypedEnum<SubtitleSource> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitleSource", "Subtitle Source", 1,
                                           {"Metadata", 1, "Frame Metadata"},
                                           {"Cea608Anc", 2, "CEA-608 from ANC"},
                                           {"Cea708Anc", 3, "CEA-708 from ANC"});

                using TypedEnum<SubtitleSource>::TypedEnum;

                static const SubtitleSource Metadata;
                static const SubtitleSource Cea608Anc;
                static const SubtitleSource Cea708Anc;
};

inline const SubtitleSource SubtitleSource::Metadata{1};
inline const SubtitleSource SubtitleSource::Cea608Anc{2};
inline const SubtitleSource SubtitleSource::Cea708Anc{3};

/**
 * @brief Caption display-mode selector (per-cue).
 * @ingroup proav
 *
 * Both CEA-608 and CEA-708 wire formats support three caption display
 * modes; this enum carries the mode in a codec-agnostic way so the
 * @ref Subtitle data model can round-trip the producer's choice
 * across format adapters and back to the encoders.
 *
 *  - @c Default — "let the encoder decide".  Encoders fall back to
 *    their @c Config-level default (typically @c PopOn).
 *  - @c PopOn — pre-recorded mode.  Cue text is loaded into hidden
 *    memory and swapped to display at @c cue.start, cleared at
 *    @c cue.end.  Standard for offline-authored captions.
 *  - @c PaintOn — live mode.  Cue text is written directly to
 *    displayed memory character-by-character.  No swap — chars
 *    appear as transmitted.
 *  - @c RollUp — continuous scrolling captions.  Each cue is
 *    appended as a new row at the bottom; existing rows scroll up.
 *    Common in live broadcast.
 */
class CaptionMode : public TypedEnum<CaptionMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("CaptionMode", "Caption Display Mode", 0,
                                           {"Default", 0, "Default (Encoder Decides)"},
                                           {"PopOn", 1, "Pop-On"},
                                           {"PaintOn", 2, "Paint-On"},
                                           {"RollUp", 3, "Roll-Up"}); // default: Default

                using TypedEnum<CaptionMode>::TypedEnum;

                static const CaptionMode Default;
                static const CaptionMode PopOn;
                static const CaptionMode PaintOn;
                static const CaptionMode RollUp;
};

inline const CaptionMode CaptionMode::Default{0};
inline const CaptionMode CaptionMode::PopOn{1};
inline const CaptionMode CaptionMode::PaintOn{2};
inline const CaptionMode CaptionMode::RollUp{3};

/**
 * @brief Per-span edge style (CEA-708 SetPenAttributes).
 * @ingroup proav
 *
 * Modelled directly after the 708 @c edge_type field in
 * @c SetPenAttributes — six broadcast-defined edge effects plus
 * @c None (no edge).  CEA-608 has no edge concept; 608 encoders
 * drop the field with a one-shot warning.
 */
class SubtitleEdgeStyle : public TypedEnum<SubtitleEdgeStyle> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitleEdgeStyle", "Subtitle Edge Style", 0,
                                           {"None", 0, "None"},
                                           {"Raised", 1, "Raised"},
                                           {"Depressed", 2, "Depressed"},
                                           {"Uniform", 3, "Uniform"},
                                           {"ShadowLeft", 4, "Shadow Left"},
                                           {"ShadowRight", 5, "Shadow Right"}); // default: None

                using TypedEnum<SubtitleEdgeStyle>::TypedEnum;

                static const SubtitleEdgeStyle None;
                static const SubtitleEdgeStyle Raised;
                static const SubtitleEdgeStyle Depressed;
                static const SubtitleEdgeStyle Uniform;
                static const SubtitleEdgeStyle ShadowLeft;
                static const SubtitleEdgeStyle ShadowRight;
};

inline const SubtitleEdgeStyle SubtitleEdgeStyle::None{0};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::Raised{1};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::Depressed{2};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::Uniform{3};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::ShadowLeft{4};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::ShadowRight{5};

/**
 * @brief Per-component opacity selector (CEA-708 SetPenColor).
 * @ingroup proav
 *
 * Mirrors the 708 @c fg_opacity / @c bg_opacity / @c edge_opacity
 * fields.  @c Solid is the conventional default (opaque).
 * @c Flash blinks the component at ~1 Hz on the wire.
 * @c Translucent is ~50% alpha.  @c Transparent omits the
 * component entirely.
 *
 * CEA-608 has no opacity wire field; 608 encoders treat every
 * component as @c Solid and warn-and-drop on @c Translucent /
 * @c Transparent / @c Flash.
 */
class SubtitleOpacity : public TypedEnum<SubtitleOpacity> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitleOpacity", "Subtitle Opacity", 0,
                                           {"Solid", 0, "Solid (Opaque)"},
                                           {"Flash", 1, "Flashing"},
                                           {"Translucent", 2, "Translucent"},
                                           {"Transparent", 3, "Transparent"}); // default: Solid

                using TypedEnum<SubtitleOpacity>::TypedEnum;

                static const SubtitleOpacity Solid;
                static const SubtitleOpacity Flash;
                static const SubtitleOpacity Translucent;
                static const SubtitleOpacity Transparent;
};

inline const SubtitleOpacity SubtitleOpacity::Solid{0};
inline const SubtitleOpacity SubtitleOpacity::Flash{1};
inline const SubtitleOpacity SubtitleOpacity::Translucent{2};
inline const SubtitleOpacity SubtitleOpacity::Transparent{3};

/**
 * @brief Per-span font-face tag (CEA-708 SetPenAttributes).
 * @ingroup proav
 *
 * Eight font tags from the 708 @c font_tag field.  Renderers map
 * these to concrete TrueType faces at paint time.  CEA-608 has no
 * font-face concept; 608 encoders drop the field with a one-shot
 * warning when set to anything other than @c Default.
 */
class SubtitleFontFace : public TypedEnum<SubtitleFontFace> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitleFontFace", "Subtitle Font Face", 0,
                                           {"Default", 0, "Default"},
                                           {"MonoSerif", 1, "Monospaced Serif"},
                                           {"ProportionalSerif", 2, "Proportional Serif"},
                                           {"MonoSans", 3, "Monospaced Sans-Serif"},
                                           {"ProportionalSans", 4, "Proportional Sans-Serif"},
                                           {"Casual", 5, "Casual"},
                                           {"Cursive", 6, "Cursive"},
                                           {"SmallCaps", 7, "Small Caps"}); // default: Default

                using TypedEnum<SubtitleFontFace>::TypedEnum;

                static const SubtitleFontFace Default;
                static const SubtitleFontFace MonoSerif;
                static const SubtitleFontFace ProportionalSerif;
                static const SubtitleFontFace MonoSans;
                static const SubtitleFontFace ProportionalSans;
                static const SubtitleFontFace Casual;
                static const SubtitleFontFace Cursive;
                static const SubtitleFontFace SmallCaps;
};

inline const SubtitleFontFace SubtitleFontFace::Default{0};
inline const SubtitleFontFace SubtitleFontFace::MonoSerif{1};
inline const SubtitleFontFace SubtitleFontFace::ProportionalSerif{2};
inline const SubtitleFontFace SubtitleFontFace::MonoSans{3};
inline const SubtitleFontFace SubtitleFontFace::ProportionalSans{4};
inline const SubtitleFontFace SubtitleFontFace::Casual{5};
inline const SubtitleFontFace SubtitleFontFace::Cursive{6};
inline const SubtitleFontFace SubtitleFontFace::SmallCaps{7};

/**
 * @brief Pen-size attribute for a styled subtitle / caption span.
 * @ingroup proav
 *
 * Mirrors the @c pen_size field of the CEA-708-E @c SetPenAttributes
 * command (§8.10.5.9 / §8.5.1): three discrete sizes the caption
 * author can request, with the receiver free to substitute its own
 * sizing.  CEA-608 has no concept of pen size — 608 encoders ignore
 * the field.
 *
 *  - @c Standard — the receiver's default size (the only size 708
 *    receivers are required to implement).
 *  - @c Small / @c Large — author hints; receivers may honour or
 *    fall back to Standard.
 */
class SubtitlePenSize : public TypedEnum<SubtitlePenSize> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitlePenSize", "Subtitle Pen Size", 1,
                                           {"Small", 0, "Small"},
                                           {"Standard", 1, "Standard"},
                                           {"Large", 2, "Large"}); // default: Standard

                using TypedEnum<SubtitlePenSize>::TypedEnum;

                static const SubtitlePenSize Small;
                static const SubtitlePenSize Standard;
                static const SubtitlePenSize Large;
};

inline const SubtitlePenSize SubtitlePenSize::Small{0};
inline const SubtitlePenSize SubtitlePenSize::Standard{1};
inline const SubtitlePenSize SubtitlePenSize::Large{2};

/**
 * @brief Pen-offset (subscript / normal / superscript) attribute.
 * @ingroup proav
 *
 * Mirrors the @c offset field of the CEA-708-E @c SetPenAttributes
 * command (§8.10.5.9 / §8.5.4): three discrete positions for the
 * character cell relative to the row baseline.  CEA-608 has no
 * concept of subscript / superscript — 608 encoders ignore the
 * field.
 *
 *  - @c Subscript — text offset downward from the baseline.
 *  - @c Normal — default, no offset.
 *  - @c Superscript — text offset upward from the baseline.
 */
class SubtitlePenOffset : public TypedEnum<SubtitlePenOffset> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitlePenOffset", "Subtitle Pen Offset", 1,
                                           {"Subscript", 0, "Subscript"},
                                           {"Normal", 1, "Normal"},
                                           {"Superscript", 2, "Superscript"}); // default: Normal

                using TypedEnum<SubtitlePenOffset>::TypedEnum;

                static const SubtitlePenOffset Subscript;
                static const SubtitlePenOffset Normal;
                static const SubtitlePenOffset Superscript;
};

inline const SubtitlePenOffset SubtitlePenOffset::Subscript{0};
inline const SubtitlePenOffset SubtitlePenOffset::Normal{1};
inline const SubtitlePenOffset SubtitlePenOffset::Superscript{2};

/**
 * @brief Semantic role tag for a styled subtitle / caption span.
 * @ingroup proav
 *
 * Mirrors the @c text_tag field of the CEA-708-E @c SetPenAttributes
 * command (§8.10.5.9 / §8.5.9): a 4-bit hint describing what the
 * upcoming text *represents*, independent of its visual styling.
 * Renderers and accessibility tools can use the tag to (e.g.) speak
 * source-ID prefixes in a different voice, hide lyrics from a captions
 * filter, or suppress invisible metadata.  Receivers that ignore the
 * field still display the text correctly — this is a hint, not a
 * format directive.
 *
 *  - @c Dialog — default; ordinary spoken dialog.
 *  - @c SourceId — speaker identification (e.g. "JOHN:").
 *  - @c ElectronicallyReproduced — phone, robot, PA system, etc.
 *  - @c DialogOtherLanguage — speech in a non-program language.
 *  - @c Voiceover — narration over scene audio.
 *  - @c AudibleTranslation — voiceover of foreign dialog.
 *  - @c SubtitleTranslation — written translation of foreign dialog.
 *  - @c VoiceDescription — descriptive video service (DVS).
 *  - @c Lyrics — song lyrics.
 *  - @c EffectDescription — sound effect description (e.g. "[barking]").
 *  - @c ScoreDescription — music description (e.g. "[ominous music]").
 *  - @c Expletive — bleeped or censored word.
 *  - @c Reserved12 / @c Reserved13 / @c Reserved14 — undefined by the spec.
 *  - @c NotDisplayed — metadata payload; receivers should not render it
 *    (used for hidden control / search-index data).
 *
 * CEA-608 has no text-tag concept; 608 encoders drop the field.
 */
class SubtitleTextTag : public TypedEnum<SubtitleTextTag> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SubtitleTextTag", "Subtitle Text Tag", 0,
                                           {"Dialog", 0, "Dialog"},
                                           {"SourceId", 1, "Speaker Identification"},
                                           {"ElectronicallyReproduced", 2, "Electronically Reproduced Voice"},
                                           {"DialogOtherLanguage", 3, "Dialog (Other Language)"},
                                           {"Voiceover", 4, "Voiceover"},
                                           {"AudibleTranslation", 5, "Audible Translation"},
                                           {"SubtitleTranslation", 6, "Subtitle Translation"},
                                           {"VoiceDescription", 7, "Voice Description (DVS)"},
                                           {"Lyrics", 8, "Lyrics"},
                                           {"EffectDescription", 9, "Sound Effect Description"},
                                           {"ScoreDescription", 10, "Music Description"},
                                           {"Expletive", 11, "Expletive"},
                                           {"Reserved12", 12, "Reserved (12)"},
                                           {"Reserved13", 13, "Reserved (13)"},
                                           {"Reserved14", 14, "Reserved (14)"},
                                           {"NotDisplayed", 15, "Not Displayed (Metadata)"}); // default: Dialog

                using TypedEnum<SubtitleTextTag>::TypedEnum;

                static const SubtitleTextTag Dialog;
                static const SubtitleTextTag SourceId;
                static const SubtitleTextTag ElectronicallyReproduced;
                static const SubtitleTextTag DialogOtherLanguage;
                static const SubtitleTextTag Voiceover;
                static const SubtitleTextTag AudibleTranslation;
                static const SubtitleTextTag SubtitleTranslation;
                static const SubtitleTextTag VoiceDescription;
                static const SubtitleTextTag Lyrics;
                static const SubtitleTextTag EffectDescription;
                static const SubtitleTextTag ScoreDescription;
                static const SubtitleTextTag Expletive;
                static const SubtitleTextTag Reserved12;
                static const SubtitleTextTag Reserved13;
                static const SubtitleTextTag Reserved14;
                static const SubtitleTextTag NotDisplayed;
};

inline const SubtitleTextTag SubtitleTextTag::Dialog{0};
inline const SubtitleTextTag SubtitleTextTag::SourceId{1};
inline const SubtitleTextTag SubtitleTextTag::ElectronicallyReproduced{2};
inline const SubtitleTextTag SubtitleTextTag::DialogOtherLanguage{3};
inline const SubtitleTextTag SubtitleTextTag::Voiceover{4};
inline const SubtitleTextTag SubtitleTextTag::AudibleTranslation{5};
inline const SubtitleTextTag SubtitleTextTag::SubtitleTranslation{6};
inline const SubtitleTextTag SubtitleTextTag::VoiceDescription{7};
inline const SubtitleTextTag SubtitleTextTag::Lyrics{8};
inline const SubtitleTextTag SubtitleTextTag::EffectDescription{9};
inline const SubtitleTextTag SubtitleTextTag::ScoreDescription{10};
inline const SubtitleTextTag SubtitleTextTag::Expletive{11};
inline const SubtitleTextTag SubtitleTextTag::Reserved12{12};
inline const SubtitleTextTag SubtitleTextTag::Reserved13{13};
inline const SubtitleTextTag SubtitleTextTag::Reserved14{14};
inline const SubtitleTextTag SubtitleTextTag::NotDisplayed{15};

/**
 * @brief Closed-caption codec selector for ANC emission paths.
 * @ingroup proav
 *
 * Selects which CEA caption stream(s) a producer (e.g. the TPG) emits
 * into a @ref Cea708Cdp.  The CDP's @c cc_data list can carry both
 * line-21 byte pairs (CEA-608, @c cc_type=0/1) and DTVCC triples
 * (CEA-708, @c cc_type=2/3) in the same packet, which is how real
 * broadcast captioning rides — so all three values are first-class:
 *
 *  - @c Cea608 — line-21 only.  @ref Cea608Encoder drives one
 *    byte-pair per frame; consumers fall back to legacy 608
 *    decoders.  This is the default to preserve the historical
 *    TPG output shape.
 *  - @c Cea708 — DTVCC only.  @ref Cea708Encoder emits per-cue
 *    Define/Display/Hide window transactions; consumers use
 *    @ref Cea708Decoder.
 *  - @c Both — both encoders feed the same per-frame @c CcDataList.
 *    The 608 byte pair and the 708 triples ride together in a
 *    single CDP, mirroring SDI broadcast practice.
 */
class CaptionCodec : public TypedEnum<CaptionCodec> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("CaptionCodec", "Caption Codec", 0,
                                           {"Cea608", 0, "CEA-608 (Line 21)"},
                                           {"Cea708", 1, "CEA-708 (DTVCC)"},
                                           {"Both", 2, "CEA-608 and CEA-708"}); // default: Cea608

                using TypedEnum<CaptionCodec>::TypedEnum;

                static const CaptionCodec Cea608;
                static const CaptionCodec Cea708;
                static const CaptionCodec Both;
};

inline const CaptionCodec CaptionCodec::Cea608{0};
inline const CaptionCodec CaptionCodec::Cea708{1};
inline const CaptionCodec CaptionCodec::Both{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
