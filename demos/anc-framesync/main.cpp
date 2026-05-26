/**
 * @file      anc-framesync/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Demonstrates @ref AncFrameSync end-to-end with a fictional ST 2110-40
 * RTP source and consumer.  No real network, no video, no audio — just
 * Frames carrying ANC packets, pushed through an @ref AncFrameSync that
 * applies a CLI-driven schedule of Play / Drop / Repeat dispositions.
 *
 * The set of ANC formats emitted per source frame is driven by
 * @c --anc.  The defaults keep the output compact (ATC LTC + CEA-708
 * CDP); pass @c --anc all to enable every format the demo knows about
 * — useful for an "aggressive" demonstration of how each per-codec
 * sync policy reacts to Drop / Repeat.
 *
 * Per-codec sync-policy effects you'll see, by format:
 *   - @b ATC LTC / VITC1 / VITC2 — wire timecode advances by
 *     repeatIndex on every Repeat output.
 *   - @b CEA-708 CDP — sequence_counter advances by repeatIndex and
 *     every cc_data triple is flipped @c cc_valid=false (the
 *     "framing-only CDP" technique that maintains the per-frame CDP
 *     cadence without re-firing caption commands).
 *   - @b AFD, @b HDR-Static (ST 2086), @b HDR-Dynamic (HDR10+ /
 *     ST 2094-40) — all sticky / per-frame samples; the sync policy
 *     copies them through verbatim on Repeat.
 *
 * Usage:
 *   anc-framesync-demo [options]
 *     --frames N        Number of source frames to generate (default 12)
 *     --drop-every N    Drop every N-th source frame (default 0 = never)
 *     --repeat-every N  Repeat every N-th source frame (default 0 = never)
 *     --repeat-count K  How many times to repeat (default 2)
 *     --start-frame N   First source frame index (default 0)
 *     --anc LIST        Comma-separated list of ANC formats to emit.
 *                       Available: atc-ltc, vitc1, vitc2, cea708, afd,
 *                                  hdr-static, hdr-dynamic.
 *                       Special: 'all' enables every format.
 *                       (default: atc-ltc,cea708)
 *     --help            Show this usage and exit
 *
 * Examples:
 *   # Default minimal:
 *   anc-framesync-demo
 *
 *   # Aggressive — every supported format, mix of drops and repeats:
 *   anc-framesync-demo --anc all --drop-every 5 --repeat-every 3
 */

#include <cstdint>
#include <cstdio>

#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancframesync.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/application.h>
#include <promeki/cea708cdp.h>
#include <promeki/ciepoint.h>
#include <promeki/cmdlineparser.h>
#include <promeki/contentlightlevel.h>
#include <promeki/enums_anc.h>
#include <promeki/enums_color.h>
#include <promeki/frame.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/hdrdynamic2094_40.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/masteringdisplay.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>

using namespace promeki;

// ---------------------------------------------------------------------------
// Demo constants
// ---------------------------------------------------------------------------

// NDF30 keeps timecode arithmetic obvious — frame index N maps directly to
// digits hh:mm:ss:ff with no drop-frame skips.  AncFrameSync's ATC sync
// policy correctly handles DF too (see the unit test
// `ATC sync policy: Repeat across the DF30 minute-rollover boundary`),
// but for a console-readable demo NDF is the friendlier choice.
static constexpr Timecode::DigitType kFramesPerSecond = 30;

// Hour digit for every emitted timecode — keeps the printed TC stable in
// the 01:00:xx:xx range so it's visually obvious which TCs came from which
// source frame.
static constexpr Timecode::DigitType kBaseHour = 1;

// CEA-708 cdp_frame_rate code 4 = 30 fps (SMPTE 334-2 §5.1.4).
static constexpr uint8_t kCdpFrameRateCode30 = 4;

// AFD packed byte: bit 7 = AR flag, bits 3..6 = AFD code (4 bits).
// Code 0x0A = "16:9 image, full frame, no protected area", AR = 1.
static constexpr uint8_t kAfdAr        = 1;
static constexpr uint8_t kAfdCode16x9  = 0x0A;
static constexpr uint8_t kAfdPacked    = static_cast<uint8_t>((kAfdAr << 7) | (kAfdCode16x9 << 3));

// Format keys the demo knows about, in display order.  Used both as the
// canonical names accepted by --anc and as the iteration order for the
// source/decoder loops.
static const StringList &ancFormatNames() {
        static const StringList names{"atc-ltc", "vitc1", "vitc2", "cea708",
                                       "afd", "hdr-static", "hdr-dynamic"};
        return names;
}

// ---------------------------------------------------------------------------
// CLI options
// ---------------------------------------------------------------------------

struct Options {
        int  frameCount  = 12;
        int  dropEvery   = 0;
        int  repeatEvery = 0;
        int  repeatCount = 2;
        int  startFrame  = 0;
        bool wantHelp    = false;

        // Per-format toggles, keyed on the names from ancFormatNames().
        Map<String, bool> formats;
};

static void initDefaultFormats(Options &opts) {
        for (const String &n : ancFormatNames()) opts.formats.insert(n, false);
        // Defaults match the original demo behaviour: ATC LTC + CEA-708 CDP.
        opts.formats.insert(String("atc-ltc"), true);
        opts.formats.insert(String("cea708"), true);
}

static int parseAncFormatsArg(const String &raw, Options &opts) {
        // Reset every toggle — the option re-specifies the full set each
        // time it appears.
        for (const String &n : ancFormatNames()) opts.formats.insert(n, false);

        StringList tokens = raw.split(",");
        for (const String &tok : tokens) {
                const String name = tok.trim().toLower();
                if (name.isEmpty()) continue;
                if (name == "all") {
                        for (const String &n : ancFormatNames()) opts.formats.insert(n, true);
                        continue;
                }
                if (!opts.formats.contains(name)) {
                        std::fprintf(stderr, "Unknown ANC format: '%s'\n", name.cstr());
                        return 1;
                }
                opts.formats.insert(name, true);
        }
        return 0;
}

// ---------------------------------------------------------------------------
// Source-side payload builders (one per format)
// ---------------------------------------------------------------------------

static AncPacket buildAtcPacket(AncTranslator &t, AncFormat::ID id, int frameIndex) {
        const int fps = static_cast<int>(kFramesPerSecond);
        Timecode  tc(Timecode::Mode(Timecode::NDF30), kBaseHour,
                     static_cast<Timecode::DigitType>((frameIndex / (fps * 60)) % 60),
                     static_cast<Timecode::DigitType>((frameIndex / fps) % 60),
                     static_cast<Timecode::DigitType>(frameIndex % fps));
        return t.build(Variant(tc), AncFormat(id), AncTransport::St291).first().front();
}

static AncPacket buildCdpPacket(AncTranslator &t, int frameIndex) {
        Cea708Cdp::CcDataList triples;
        Cea708Cdp::CcData     cc;
        cc.valid = true;
        cc.type  = 0;
        cc.b1    = static_cast<uint8_t>(frameIndex & 0xFF);
        cc.b2    = 0x00;
        triples.pushToBack(cc);
        Cea708Cdp cdp(kCdpFrameRateCode30, triples,
                       static_cast<uint16_t>(frameIndex & 0xFFFF));
        return t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                        AncTransport::St291).first().front();
}

static AncPacket buildAfdPacket(AncTranslator &t) {
        return t.build(Variant(kAfdPacked), AncFormat(AncFormat::Afd),
                        AncTransport::St291).first().front();
}

static AncPacket buildHdrStaticPacket(AncTranslator &t) {
        // Standard HDR10 source — Rec.2020 primaries, 0.005 / 1000 cd/m²
        // mastering display, 1000 maxCLL / 400 maxFALL.
        MasteringDisplay md(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797),
                            CIEPoint(0.131, 0.046), CIEPoint(0.3127, 0.3290),
                            0.005, 1000.0);
        HdrStaticMetadata sample(TransferCharacteristics::SMPTE2084, std::move(md),
                                  ContentLightLevel(1000, 400));
        return t.build(Variant(sample), AncFormat(AncFormat::HdrStatic2086),
                        AncTransport::St291).first().front();
}

static List<AncPacket> buildHdrDynamicPackets(AncTranslator &t) {
        // Minimal single-window HDR10+ descriptor — fits in one ANC packet
        // so the consumer can call parse() (which wraps the single packet
        // in a one-elem list and dispatches to the MultiParser).
        HdrDynamic2094_40 md;
        md.setApplicationVersion(1);
        md.setNumWindows(1);
        md.setTargetedSystemDisplayMaximumLuminance(10'000'000u); // 1000 cd/m²
        return t.build(Variant(md), AncFormat(AncFormat::HdrDynamic2094_40),
                        AncTransport::St291).first();
}

static Frame buildSourceFrame(AncTranslator &t, int frameIndex, const Options &opts) {
        AncDesc         desc;
        AncPayload::Ptr ancP = AncPayload::Ptr::create(desc);

        // Order matters only for printed-line readability: we add packets
        // in display order so the consumer's per-frame summary lines them
        // up consistently.
        if (opts.formats.value(String("atc-ltc"), false)) {
                ancP.modify()->addPacket(buildAtcPacket(t, AncFormat::AtcLtc, frameIndex));
        }
        if (opts.formats.value(String("vitc1"), false)) {
                ancP.modify()->addPacket(buildAtcPacket(t, AncFormat::AtcVitc1, frameIndex));
        }
        if (opts.formats.value(String("vitc2"), false)) {
                ancP.modify()->addPacket(buildAtcPacket(t, AncFormat::AtcVitc2, frameIndex));
        }
        if (opts.formats.value(String("cea708"), false)) {
                ancP.modify()->addPacket(buildCdpPacket(t, frameIndex));
        }
        if (opts.formats.value(String("afd"), false)) {
                ancP.modify()->addPacket(buildAfdPacket(t));
        }
        if (opts.formats.value(String("hdr-static"), false)) {
                ancP.modify()->addPacket(buildHdrStaticPacket(t));
        }
        if (opts.formats.value(String("hdr-dynamic"), false)) {
                for (const AncPacket &pkt : buildHdrDynamicPackets(t)) {
                        ancP.modify()->addPacket(pkt);
                }
        }

        Frame f;
        f.addPayload(ancP);
        return f;
}

// ---------------------------------------------------------------------------
// Schedule
// ---------------------------------------------------------------------------

static FrameSyncDisposition decideDisposition(int frameIndex, const Options &opts) {
        // 1-based "every N-th frame" indexing — matches what most readers
        // expect ("every 4th frame" = 4th, 8th, 12th, …).
        const int idx1 = frameIndex + 1;
        if (opts.dropEvery > 0 && idx1 % opts.dropEvery == 0) {
                return FrameSyncDisposition::drop();
        }
        if (opts.repeatEvery > 0 && idx1 % opts.repeatEvery == 0) {
                return FrameSyncDisposition::repeat(static_cast<uint8_t>(opts.repeatCount));
        }
        return FrameSyncDisposition::play();
}

static String dispositionLabel(FrameSyncDisposition d) {
        switch (d.kind()) {
                case FrameSyncDisposition::Play:   return String("Play");
                case FrameSyncDisposition::Drop:   return String("Drop");
                case FrameSyncDisposition::Repeat: return String::format("Repeat[{}]", d.repeatCount());
        }
        return String("?");
}

// ---------------------------------------------------------------------------
// Consumer-side decoders
// ---------------------------------------------------------------------------

struct DecodedFrame {
        // ATC: combine all three variants into a single TC + a count, since
        // they always carry the same value when emitted together.
        bool                    hasAtc = false;
        Timecode                atc;
        StringList              atcVariants;     // "LTC", "V1", "V2"

        bool                    hasCdp = false;
        Cea708Cdp               cdp;

        bool                    hasAfd      = false;
        uint8_t                 afdPacked   = 0;

        bool                    hasHdrStatic = false;
        HdrStaticMetadata       hdrStatic;

        int                     hdrDynamicPacketCount = 0;
};

static DecodedFrame decodeFrame(AncTranslator &t, const Frame &f) {
        DecodedFrame d;
        AncPayload::PtrList ancList = f.ancPayloads();
        for (size_t i = 0; i < ancList.size(); ++i) {
                if (!ancList[i].isValid()) continue;
                for (const AncPacket &pkt : ancList[i]->packets()) {
                        Result<Variant> parsed = t.parse(pkt);
                        if (parsed.second().isError()) continue;
                        switch (pkt.format().id()) {
                                case AncFormat::AtcLtc:
                                        d.hasAtc = true;
                                        d.atc    = parsed.first().get<Timecode>();
                                        d.atcVariants.pushToBack(String("LTC"));
                                        break;
                                case AncFormat::AtcVitc1:
                                        d.hasAtc = true;
                                        d.atc    = parsed.first().get<Timecode>();
                                        d.atcVariants.pushToBack(String("V1"));
                                        break;
                                case AncFormat::AtcVitc2:
                                        d.hasAtc = true;
                                        d.atc    = parsed.first().get<Timecode>();
                                        d.atcVariants.pushToBack(String("V2"));
                                        break;
                                case AncFormat::Cea708:
                                        d.hasCdp = true;
                                        d.cdp    = parsed.first().get<Cea708Cdp>();
                                        break;
                                case AncFormat::Afd:
                                        d.hasAfd    = true;
                                        d.afdPacked = parsed.first().get<uint8_t>();
                                        break;
                                case AncFormat::HdrStatic2086:
                                        d.hasHdrStatic = true;
                                        d.hdrStatic    = parsed.first().get<HdrStaticMetadata>();
                                        break;
                                case AncFormat::HdrDynamic2094_40:
                                        // Count the packets here rather than parsing — the
                                        // descriptor itself is identical across copies.
                                        ++d.hdrDynamicPacketCount;
                                        break;
                                default: break;
                        }
                }
        }
        return d;
}

static String describeDecoded(const DecodedFrame &d) {
        StringList tokens;

        if (d.hasAtc) {
                String variants = d.atcVariants.join(String("+"));
                tokens.pushToBack(String::format("ATC={} ({})", d.atc.toString(), variants));
        }
        if (d.hasCdp) {
                unsigned validCount = 0;
                unsigned totalCount = static_cast<unsigned>(d.cdp.ccData.size());
                for (const Cea708Cdp::CcData &cc : d.cdp.ccData) {
                        if (cc.valid) ++validCount;
                }
                String marker("(none)");
                if (totalCount > 0) {
                        const Cea708Cdp::CcData &first = d.cdp.ccData[0];
                        marker = first.valid ? String::format("0x{:02X}", first.b1)
                                             : String("blank");
                }
                tokens.pushToBack(String::format("CDP[seq={:5},cc={}/{},m={}]",
                                                  d.cdp.sequenceCounter,
                                                  validCount, totalCount, marker));
        }
        if (d.hasAfd) {
                const uint8_t code = static_cast<uint8_t>((d.afdPacked >> 3) & 0x0F);
                const uint8_t ar   = static_cast<uint8_t>((d.afdPacked >> 7) & 0x01);
                tokens.pushToBack(String::format("AFD=0x{:02X}(code={:X},ar={})",
                                                  d.afdPacked, code, ar));
        }
        if (d.hasHdrStatic) {
                tokens.pushToBack(String::format("HdrS[CLL={}]",
                                                  d.hdrStatic.contentLightLevel().maxCLL()));
        }
        if (d.hdrDynamicPacketCount > 0) {
                tokens.pushToBack(String::format("HdrD[{}p]", d.hdrDynamicPacketCount));
        }

        if (tokens.isEmpty()) return String("(no decoded ANC)");
        return tokens.join(String(" "));
}

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------

static void printLine(const String &line) {
        std::printf("%s\n", line.cstr());
}

static String enabledFormatsList(const Options &opts) {
        StringList enabled;
        for (const String &n : ancFormatNames()) {
                if (opts.formats.value(n, false)) enabled.pushToBack(n);
        }
        if (enabled.isEmpty()) return String("(none)");
        return enabled.join(String(","));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
        Application app(argc, argv);
        Application::setAppName("anc-framesync-demo");

        Options opts;
        initDefaultFormats(opts);

        CmdLineParser parser;
        parser.registerOptions({
                {0, "frames", "Number of source frames to generate (default 12)",
                 CmdLineParser::OptionIntCallback([&opts](int v) {
                         opts.frameCount = v;
                         return 0;
                 })},
                {0, "drop-every", "Drop every N-th source frame (default 0 = never)",
                 CmdLineParser::OptionIntCallback([&opts](int v) {
                         opts.dropEvery = v;
                         return 0;
                 })},
                {0, "repeat-every", "Repeat every N-th source frame (default 0 = never)",
                 CmdLineParser::OptionIntCallback([&opts](int v) {
                         opts.repeatEvery = v;
                         return 0;
                 })},
                {0, "repeat-count", "How many times to repeat (default 2)",
                 CmdLineParser::OptionIntCallback([&opts](int v) {
                         opts.repeatCount = v;
                         return 0;
                 })},
                {0, "start-frame", "First source frame index (default 0)",
                 CmdLineParser::OptionIntCallback([&opts](int v) {
                         opts.startFrame = v;
                         return 0;
                 })},
                {0, "anc",
                 "Comma-separated ANC formats: atc-ltc, vitc1, vitc2, cea708, afd, "
                 "hdr-static, hdr-dynamic. Use 'all' for every format. "
                 "(default: atc-ltc,cea708)",
                 CmdLineParser::OptionStringCallback([&opts](const String &s) {
                         return parseAncFormatsArg(s, opts);
                 })},
                {'h', "help", "Show this usage and exit",
                 CmdLineParser::OptionCallback([&opts]() {
                         opts.wantHelp = true;
                         return 0;
                 })},
        });

        if (parser.parseMain(argc, argv) != 0) return 1;
        if (opts.wantHelp) {
                printLine(String("Usage: anc-framesync-demo [options]"));
                printLine(String());
                printLine(String("Options:"));
                for (const String &line : parser.generateUsage()) {
                        printLine(String::format("  {}", line));
                }
                return 0;
        }

        const String dropStr = opts.dropEvery > 0
                ? String::number(opts.dropEvery)
                : String("never");
        const String repeatStr = opts.repeatEvery > 0
                ? String::format("{} (count={})", opts.repeatEvery, opts.repeatCount)
                : String("never");

        printLine(String("AncFrameSync demo — fictional ST 2110-40 RTP source/consumer"));
        printLine(String::format("  source frames:  {} (starting at index {})",
                                  opts.frameCount, opts.startFrame));
        printLine(String::format("  drop-every:     {}", dropStr));
        printLine(String::format("  repeat-every:   {}", repeatStr));
        printLine(String::format("  anc formats:    {}", enabledFormatsList(opts)));
        printLine(String());

        AncTranslator builderTranslator;
        AncFrameSync  fs;

        int outputCounter = 0;
        int playCount     = 0;
        int dropCount     = 0;
        int repeatCount   = 0;

        for (int slot = 0; slot < opts.frameCount; ++slot) {
                const int            frameIndex = opts.startFrame + slot;
                Frame                in         = buildSourceFrame(builderTranslator, frameIndex, opts);
                FrameSyncDisposition disp       = decideDisposition(slot, opts);

                printLine(String::format("IN[{:3}] disp={:<12} {}", slot,
                                          dispositionLabel(disp),
                                          describeDecoded(decodeFrame(builderTranslator, in))));

                Result<List<Frame>> res = fs.apply(in, disp);
                if (res.second().isError()) {
                        printLine(String::format("        apply() error: {}", res.second().name()));
                        continue;
                }

                if (disp.kind() == FrameSyncDisposition::Drop)        ++dropCount;
                else if (disp.kind() == FrameSyncDisposition::Repeat) ++repeatCount;
                else                                                  ++playCount;

                if (res.first().isEmpty()) {
                        printLine(String("        (no output emitted)"));
                } else {
                        for (const Frame &outF : res.first()) {
                                printLine(String::format("        OUT[{:3}]             {}",
                                                          outputCounter++,
                                                          describeDecoded(decodeFrame(builderTranslator, outF))));
                        }
                }
        }

        printLine(String());
        printLine(String::format("Summary: {} input frames", opts.frameCount));
        printLine(String::format("  Play     : {}", playCount));
        printLine(String::format("  Drop     : {}", dropCount));
        printLine(String::format("  Repeat   : {}", repeatCount));
        printLine(String::format("  Output   : {} frames emitted", outputCounter));
        return 0;
}
