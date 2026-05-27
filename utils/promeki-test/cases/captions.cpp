/**
 * @file      cases/captions.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * CEA-608 SubRip caption round-trip functional test.
 *
 * Three-stage end-to-end fixture for the Phase 3.5g production
 * gate (devplan/proav/ancdata.md):
 *
 *   1. TPG-driven write: a @ref MediaPipeline runs
 *      TPG (@c TpgAncCaptionsFile = @c etc/substest.srt) →
 *      @ref InspectorMediaIO with @c InspectorTest::AncData enabled,
 *      dumping every emitted frame's ANC packet array to
 *      @c <TestFolder>/anc.jsonl.  This exercises the live wire
 *      stack: SubRip parser → @ref Cea608Encoder → @ref Cea708Cdp
 *      → @ref AncTranslator → ST 291 → AncPayload → Inspector JSONL.
 *
 *   2. JSONL reconstruction: this test walks the JSONL file, parses
 *      each row's @c packets[].parsed.ccData array into a
 *      @ref Cea708Cdp::CcDataList, and feeds it through a fresh
 *      @ref Cea608Decoder one frame at a time.  After all frames
 *      are processed, @ref Cea608Decoder::finalize emits the
 *      recovered @ref SubtitleList.
 *
 *   3. Round-trip verification: the recovered list is emitted via
 *      @ref SubRip::emit to @c <TestFolder>/roundtrip.srt and
 *      compared cue-by-cue against the source @c etc/substest.srt.
 *      A cue passes when an ASCII-encodable source cue has a
 *      decoded match in its display window with the same anchor
 *      and the same flat text (whitespace-normalised).  Non-ASCII
 *      cues are still emitted by the encoder via @ref Cea608Ext
 *      but the comparison is best-effort there (lossy
 *      substitutions are common for codepoints outside the
 *      basic-G0 / Special / Extended tables).
 *
 * The case is registered as @c captions.cea608.subrip_roundtrip
 * and is selectable via the runner's regex filter.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/buffer.h>
#include <promeki/cea608.h>
#include <promeki/cea608decoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708decoder.h>
#include <promeki/dir.h>
#include <promeki/enumlist.h>
#include <promeki/enums_clock.h>
#include <promeki/enums_subtitle.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/iodevice.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/subrip.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>
#include <promeki/videoformat.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#ifndef PROMEKI_SOURCE_DIR
#define PROMEKI_SOURCE_DIR "."
#endif

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // The substest.srt fixture's last cue ends at
                // 00:02:10,000.  Run long enough that the final cue's
                // trailing EDM has comfortably fired before the
                // pipeline closes.  Auto-split sub-cues for the last
                // over-cap cue can extend the encoder's wire window
                // past the source cue's nominal end, so we add a
                // generous tail of empty frames.
                inline constexpr int32_t kCaptionRunSeconds = 145;
                inline constexpr double  kFps = 30.0;

                String resolveSubstestPath() {
                        return String(PROMEKI_SOURCE_DIR) + String("/etc/substest.srt");
                }

                /// @brief Loads @c etc/substest.srt and returns the parsed
                ///        @ref SubtitleList, or an empty list if the
                ///        file is missing / unreadable / malformed.
                SubtitleList loadSubstest(String *outErr) {
                        const String path = resolveSubstestPath();
                        File         f(path);
                        if (f.open(IODevice::ReadOnly).isError()) {
                                if (outErr) *outErr = String("cannot open ") + path;
                                return SubtitleList();
                        }
                        Result<int64_t> szR = f.size();
                        if (szR.second().isError()) {
                                f.close();
                                if (outErr) *outErr = String("cannot stat ") + path;
                                return SubtitleList();
                        }
                        const int64_t sz = szR.first();
                        Buffer        buf(static_cast<size_t>(sz));
                        buf.setSize(static_cast<size_t>(sz));
                        f.read(buf.data(), sz);
                        f.close();
                        Result<SubtitleList> r = SubRip::parse(buf);
                        if (r.second().isError()) {
                                if (outErr) *outErr = String("substest.srt parse failed");
                                return SubtitleList();
                        }
                        return r.first();
                }

                /// @brief Reads the entire contents of @p path into a
                ///        heap string.  Returns an empty string on any
                ///        error (caller checks for emptiness against
                ///        the expected JSONL non-empty content).
                String readWholeFile(const String &path, String *outErr) {
                        File f(path);
                        if (f.open(IODevice::ReadOnly).isError()) {
                                if (outErr) *outErr = String("cannot open ") + path;
                                return String();
                        }
                        Result<int64_t> szR = f.size();
                        if (szR.second().isError()) {
                                f.close();
                                if (outErr) *outErr = String("cannot stat ") + path;
                                return String();
                        }
                        const int64_t sz = szR.first();
                        if (sz <= 0) {
                                f.close();
                                return String();
                        }
                        Buffer buf(static_cast<size_t>(sz));
                        buf.setSize(static_cast<size_t>(sz));
                        f.read(buf.data(), sz);
                        f.close();
                        return String::fromUtf8(static_cast<const char *>(buf.data()),
                                                static_cast<size_t>(sz));
                }

                /// @brief Builds a TimeStamp from a frame index at the
                ///        fixed test frame rate.  Mirrors the encoder's
                ///        @c timeStampToFrame math so the decoder sees
                ///        the same per-frame timestamps the TPG emitted.
                TimeStamp tsForFrame(int64_t frameIdx) {
                        using ClockDur = TimeStamp::Value::duration;
                        const int64_t ms =
                                static_cast<int64_t>((static_cast<double>(frameIdx) * 1000.0) / kFps + 0.5);
                        return TimeStamp(TimeStamp::Value(
                                std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
                }

                /// @brief Builds the per-frame @ref Cea708Cdp::CcDataList
                ///        out of a JSONL row's @c packets array.
                ///
                /// Walks every packet whose @c transport == @c "St291"
                /// and accumulates the parsed CDP's @c ccData triples
                /// into a single list — multi-packet ANC ride on a
                /// single frame.  Packets with no @c parsed.ccData
                /// (non-CDP transports / parse failures) contribute
                /// nothing.
                Cea708Cdp::CcDataList ccDataFromJsonRow(const JsonObject &row) {
                        Cea708Cdp::CcDataList out;
                        JsonArray packets = row.getArray(String("packets"));
                        const int npkt = packets.size();
                        for (int i = 0; i < npkt; ++i) {
                                JsonObject pkt = packets.getObject(i);
                                const String transport = pkt.getString(String("transport"));
                                if (transport != String("St291")) continue;
                                JsonObject parsed = pkt.getObject(String("parsed"));
                                JsonArray  ccArr = parsed.getArray(String("ccData"));
                                const int  ntr = ccArr.size();
                                for (int j = 0; j < ntr; ++j) {
                                        JsonObject t = ccArr.getObject(j);
                                        Cea708Cdp::CcData c;
                                        c.valid = t.getBool(String("valid"));
                                        c.type = static_cast<uint8_t>(t.getInt(String("type")));
                                        c.b1 = static_cast<uint8_t>(t.getInt(String("b1")));
                                        c.b2 = static_cast<uint8_t>(t.getInt(String("b2")));
                                        out.pushToBack(c);
                                }
                        }
                        return out;
                }

                /// @brief Splits @p text into JSON-Lines rows and feeds
                ///        every row's caption byte pairs through a
                ///        @ref Cea608Decoder.  Returns the recovered
                ///        @ref SubtitleList from @ref Cea608Decoder::finalize.
                SubtitleList reconstructFromJsonl(const String &text, int64_t *outRowsParsed) {
                        Cea608Decoder dec;
                        int64_t       rows = 0;
                        const char   *p = text.cstr();
                        const size_t  sz = text.byteCount();
                        size_t        i = 0;
                        while (i < sz) {
                                size_t j = i;
                                while (j < sz && p[j] != '\n') ++j;
                                if (j > i) {
                                        // Strip optional trailing CR for CRLF
                                        // inputs.
                                        size_t end = j;
                                        if (end > i && p[end - 1] == '\r') --end;
                                        if (end > i) {
                                                String line(p + i, end - i);
                                                Error  perr;
                                                JsonObject row = JsonObject::parse(line, &perr);
                                                if (perr.isOk()) {
                                                        const int64_t frameIdx = row.getInt(String("frame"));
                                                        Cea708Cdp::CcDataList cc =
                                                                ccDataFromJsonRow(row);
                                                        dec.pushFrame(FrameNumber(frameIdx),
                                                                      tsForFrame(frameIdx), cc);
                                                        ++rows;
                                                }
                                        }
                                }
                                i = j + 1;
                        }
                        if (outRowsParsed) *outRowsParsed = rows;
                        return dec.finalize();
                }

                /// @brief 708 sibling of @ref reconstructFromJsonl.
                ///        Feeds every JSONL row's CcDataList through a
                ///        @ref Cea708Decoder configured for the named
                ///        DTVCC service.  Cue boundaries are emitted on
                ///        visible-text transitions inside the decoder's
                ///        internal @ref Cea708WindowState.
                SubtitleList reconstructFromJsonl708(const String &text, uint8_t serviceNumber,
                                                       int64_t *outRowsParsed) {
                        Cea708Decoder::Config dcfg;
                        dcfg.serviceNumber = serviceNumber;
                        Cea708Decoder dec(dcfg);
                        int64_t       rows = 0;
                        const char   *p = text.cstr();
                        const size_t  sz = text.byteCount();
                        size_t        i = 0;
                        while (i < sz) {
                                size_t j = i;
                                while (j < sz && p[j] != '\n') ++j;
                                if (j > i) {
                                        size_t end = j;
                                        if (end > i && p[end - 1] == '\r') --end;
                                        if (end > i) {
                                                String line(p + i, end - i);
                                                Error  perr;
                                                JsonObject row = JsonObject::parse(line, &perr);
                                                if (perr.isOk()) {
                                                        const int64_t frameIdx = row.getInt(String("frame"));
                                                        Cea708Cdp::CcDataList cc =
                                                                ccDataFromJsonRow(row);
                                                        dec.pushFrame(FrameNumber(frameIdx),
                                                                      tsForFrame(frameIdx), cc);
                                                        ++rows;
                                                }
                                        }
                                }
                                i = j + 1;
                        }
                        if (outRowsParsed) *outRowsParsed = rows;
                        return dec.finalize();
                }

                /// @brief Returns @c true when every codepoint in @p s
                ///        has a basic-ASCII representation (so the
                ///        encoder's 608 wire round-trip is byte-exact
                ///        rather than going through Special / Extended
                ///        placeholders or the no-mapping space
                ///        substitution).
                bool textIsAsciiOnly(const String &s) {
                        for (size_t i = 0; i < s.byteCount(); ++i) {
                                const unsigned char c = static_cast<unsigned char>(s.cstr()[i]);
                                if (c == '\n' || c == '\t' || c == '\r') continue;
                                if (c > 0x7E) return false;
                        }
                        return true;
                }

                /// @brief Collapses runs of whitespace to single spaces
                ///        and trims leading / trailing whitespace.
                ///
                /// 608 word-wrap and the encoder's odd-length space
                /// padding can introduce trailing / interleaved
                /// whitespace differences that don't change the
                /// rendered cue content; the comparison normalises
                /// these away so it focuses on semantic preservation.
                std::string normaliseWs(const String &s) {
                        std::string out;
                        out.reserve(s.byteCount());
                        bool inWs = false;
                        for (size_t i = 0; i < s.byteCount(); ++i) {
                                const unsigned char c = static_cast<unsigned char>(s.cstr()[i]);
                                const bool         isWs = (c == ' ' || c == '\n' || c == '\t' || c == '\r');
                                if (isWs) {
                                        if (!inWs && !out.empty()) out.push_back(' ');
                                        inWs = true;
                                } else {
                                        out.push_back(static_cast<char>(c));
                                        inWs = false;
                                }
                        }
                        while (!out.empty() && out.back() == ' ') out.pop_back();
                        return out;
                }

                /// @brief Looks for decoded cues whose starts landed in
                ///        the source cue's display window.  Returns
                ///        every matching index in order — a single
                ///        source cue may produce multiple decoded
                ///        sub-cues when the encoder's auto-split
                ///        machinery breaks an over-cap cue into
                ///        time-displaced chunks.
                List<int64_t> findMatchingCues(const SubtitleList &decoded, const Subtitle &src) {
                        List<int64_t> out;
                        const int64_t srcStartMs =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                        src.start().value().time_since_epoch())
                                        .count();
                        const int64_t srcEndMs =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                        src.end().value().time_since_epoch())
                                        .count();
                        for (size_t i = 0; i < decoded.size(); ++i) {
                                const int64_t s =
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                                decoded[i].start().value().time_since_epoch())
                                                .count();
                                if (s + 100 < srcStartMs) continue;
                                if (s > srcEndMs) continue;
                                out.pushToBack(static_cast<int64_t>(i));
                        }
                        return out;
                }

                MediaPipelineConfig::Stage makeCaptionsTpgStage(uint32_t streamId, const String &srtPath) {
                        MediaPipelineConfig::Stage s = makeTpgStage(streamId, PixelFormat(),
                                                                     /*videoEnabled=*/true,
                                                                     /*audioEnabled=*/false);
                        // Override the default 720p59.94 — the captions
                        // round-trip case anchors its per-frame
                        // TimeStamp math at @ref kFps, and the
                        // @ref Cea608Encoder's frame schedule is in the
                        // same units.  720p59.94's 59.94 fps would put
                        // every encoder-scheduled frame at twice the
                        // index our reconstructor expects.  1080p30 is
                        // the smallest standard 30 fps progressive
                        // format the registry advertises.
                        s.config.set(MediaConfig::VideoFormat,
                                      VideoFormat(VideoFormat::Smpte1080p30));
                        // Enable the 608 caption injection path —
                        // TpgMediaIO loads @p srtPath at open() and
                        // schedules every cue through the in-process
                        // Cea608Encoder.  Captions land in every
                        // emitted Frame's AncPayload list.
                        s.config.set(MediaConfig::TpgAncCaptionsEnabled, true);
                        s.config.set(MediaConfig::TpgAncCaptionsFile, srtPath);
                        return s;
                }

                /// @brief 708 sibling of @ref makeCaptionsTpgStage —
                ///        same TPG stage but with
                ///        @ref MediaConfig::TpgAncCaptionsCodec pinned to
                ///        @ref CaptionCodec::Cea708 so the per-frame
                ///        @c CcDataList holds DTVCC triples (cc_type=2/3)
                ///        instead of line-21 byte pairs (cc_type=0/1).
                MediaPipelineConfig::Stage makeCaptions708TpgStage(uint32_t streamId,
                                                                     const String &srtPath,
                                                                     uint8_t       serviceNumber) {
                        MediaPipelineConfig::Stage s = makeCaptionsTpgStage(streamId, srtPath);
                        s.config.set(MediaConfig::TpgAncCaptionsCodec,
                                      CaptionCodec(CaptionCodec::Cea708));
                        s.config.set(MediaConfig::TpgAncCaptions708Service,
                                      static_cast<int32_t>(serviceNumber));
                        return s;
                }

                MediaPipelineConfig::Stage makeAncInspectorStage(const String &ancJsonlPath) {
                        MediaPipelineConfig::Stage s = makeInspectorStage();
                        // Limit to the AncData check — the picture /
                        // audio / continuity checks would just spam
                        // their per-frame entries on a stream the test
                        // doesn't validate via the inspector snapshot.
                        EnumList tests = EnumList::forType<InspectorTest>();
                        tests.append(InspectorTest::AncData);
                        s.config.set(MediaConfig::InspectorTests, tests);
                        s.config.set(MediaConfig::InspectorAncDataFile, ancJsonlPath);
                        return s;
                }

                /// @brief Runs the SubRip caption round-trip case.
                ///
                /// Stages:
                ///  - Verify the substest.srt fixture is available.
                ///  - Build TPG → Inspector pipeline, run for
                ///    @c kCaptionRunSeconds at 30 fps so every cue's
                ///    display window completes before close.
                ///  - Parse the resulting JSONL through @ref Cea608Decoder.
                ///  - Emit the recovered list as roundtrip.srt for
                ///    after-the-fact diff inspection.
                ///  - Walk every source ASCII cue: each must match a
                ///    decoded cue in its display window with the same
                ///    anchor and the same whitespace-normalised flat
                ///    text.
                void runCea608SubRipRoundtripCase(TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 30000);

                        const FilePath testFolder = ctx.testFolder();
                        const String   ancJsonl =
                                (testFolder / String("anc.jsonl")).toString();
                        const String   roundtripPath =
                                (testFolder / String("roundtrip.srt")).toString();

                        ctx.setDetail(String("ancJsonl"), ancJsonl);
                        ctx.setDetail(String("roundtripSrt"), roundtripPath);

                        String         loadErr;
                        SubtitleList source = loadSubstest(&loadErr);
                        if (source.isEmpty()) {
                                ctx.setSkip(String("substest.srt unavailable: ") + loadErr);
                                return;
                        }
                        ctx.setDetail(String("sourceCueCount"),
                                       static_cast<int64_t>(source.size()));
                        const String srtPath = resolveSubstestPath();
                        ctx.setDetail(String("sourceSrt"), srtPath);

                        // ---- Pipeline: TPG (with SRT captions) -> Inspector AncData ----
                        const uint32_t streamId = 0xCEA60800u;
                        const int32_t frames =
                                static_cast<int32_t>(kCaptionRunSeconds * static_cast<int32_t>(kFps));
                        ctx.setDetail(String("framesScheduled"), int64_t(frames));

                        MediaPipelineConfig cfg;
                        cfg.addStage(makeCaptionsTpgStage(streamId, srtPath));
                        cfg.addStage(makeAncInspectorStage(ancJsonl));
                        cfg.addRoute(String("tpg"), String("insp"));
                        cfg.setFrameCount(FrameCount(frames));

                        JsonObject pipelineDump;
                        {
                                MediaPipeline pipe;
                                PhaseOutcome  p =
                                        runPhase(pipe, cfg, loop, static_cast<unsigned int>(timeoutMs));
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("captions", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
                                }
                                if (!p.built) {
                                        ctx.setFail(String("pipeline build failed: ") +
                                                    p.buildError.desc());
                                        return;
                                }
                                if (!p.opened) {
                                        ctx.setFail(String("pipeline open failed: ") +
                                                    p.openError.desc());
                                        return;
                                }
                                if (!p.started) {
                                        ctx.setFail(String("pipeline start failed: ") +
                                                    p.startError.desc());
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("pipeline deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        ctx.setFail(String("pipeline error: ") + p.errorDetail);
                                        return;
                                }
                        }

                        // ---- Stage 2: walk JSONL through Cea608Decoder ----
                        String   readErr;
                        const String jsonl = readWholeFile(ancJsonl, &readErr);
                        if (jsonl.isEmpty()) {
                                ctx.setFail(String("anc.jsonl is empty: ") + readErr);
                                return;
                        }
                        int64_t      rowsParsed = 0;
                        SubtitleList recovered = reconstructFromJsonl(jsonl, &rowsParsed);
                        ctx.setDetail(String("jsonlRowsParsed"), rowsParsed);
                        ctx.setDetail(String("recoveredCueCount"),
                                       static_cast<int64_t>(recovered.size()));

                        // ---- Stage 3a: emit roundtrip.srt for inspection ----
                        {
                                Buffer rtBuf = SubRip::emit(recovered);
                                File   rtFile(roundtripPath);
                                Error  oe = rtFile.open(IODevice::WriteOnly,
                                                          File::Create | File::Truncate);
                                if (oe.isOk()) {
                                        rtFile.write(rtBuf.data(),
                                                      static_cast<int64_t>(rtBuf.size()));
                                        rtFile.close();
                                } else {
                                        // Non-fatal — the JSONL is the
                                        // canonical artifact, the SRT
                                        // emit is just a convenience for
                                        // post-mortem diffing.  Log it
                                        // and keep going so the cue
                                        // comparison can still report
                                        // the real verdict.
                                        promekiWarn(
                                                "captions: failed to write roundtrip.srt: %s",
                                                oe.desc().cstr());
                                }
                        }

                        // ---- Stage 3b: cue-by-cue comparison ----
                        int64_t asciiSourceCues = 0;
                        int64_t matchedCues = 0;
                        int64_t textMismatchCount = 0;
                        int64_t anchorMismatchCount = 0;
                        int64_t unmatchedCues = 0;
                        String  firstFailDetail;
                        for (size_t i = 0; i < source.size(); ++i) {
                                const Subtitle &src = source[i];
                                if (src.text().isEmpty()) continue;
                                if (!textIsAsciiOnly(src.text())) continue;
                                ++asciiSourceCues;

                                const List<int64_t> idxs = findMatchingCues(recovered, src);
                                if (idxs.isEmpty()) {
                                        ++unmatchedCues;
                                        if (firstFailDetail.isEmpty()) {
                                                firstFailDetail = String("source cue ") +
                                                                  String::number(static_cast<int64_t>(i)) +
                                                                  String(" (\"") + src.text() +
                                                                  String("\") has no decoded match");
                                        }
                                        continue;
                                }

                                // Concatenate all decoded sub-cues
                                // before comparing.  The encoder's
                                // auto-split breaks an over-cap cue
                                // (one that wraps to more rows than
                                // @c maxRows) into time-displaced
                                // chunks; the recovered list shows one
                                // cue per chunk.  For round-trip text
                                // verification we want the union of
                                // every chunk's text.
                                String concatGot;
                                for (size_t k = 0; k < idxs.size(); ++k) {
                                        const Subtitle &part =
                                                recovered[static_cast<size_t>(idxs[k])];
                                        if (k > 0) concatGot += String(" ");
                                        concatGot += part.text();
                                }
                                const Subtitle &got = recovered[static_cast<size_t>(idxs[0])];
                                ++matchedCues;
                                const std::string srcN = normaliseWs(src.text());
                                const std::string gotN = normaliseWs(concatGot);
                                if (srcN != gotN) {
                                        ++textMismatchCount;
                                        if (firstFailDetail.isEmpty()) {
                                                firstFailDetail = String("source cue ") +
                                                                  String::number(static_cast<int64_t>(i)) +
                                                                  String(" text mismatch: src=\"") +
                                                                  String(srcN.c_str()) +
                                                                  String("\" got=\"") +
                                                                  String(gotN.c_str()) + String("\"");
                                        }
                                }
                                // Anchor preservation — vertical half
                                // only.  Horizontal recovery in 608
                                // is intrinsically imprecise for cues
                                // whose width approaches the row's 32
                                // cells: a Center anchor on a 30-char
                                // cue resolves to PAC indent col 0 +
                                // Tab Offset T1, which the decoder's
                                // @c col < 4 → Left threshold pulls
                                // back to the Left variant.  The
                                // round-trip therefore asserts only
                                // that the vertical band (Top /
                                // Middle / Bottom) is preserved;
                                // horizontal preservation is covered
                                // by the @c Cea608Encoder unit tests
                                // which run with cue widths well
                                // inside the centering window.
                                auto verticalBand = [](const SubtitleAnchor &a) {
                                        const int v = a.value();
                                        if (v == SubtitleAnchor::TopLeft.value()
                                            || v == SubtitleAnchor::TopCenter.value()
                                            || v == SubtitleAnchor::TopRight.value()) return 0;
                                        if (v == SubtitleAnchor::MiddleLeft.value()
                                            || v == SubtitleAnchor::MiddleCenter.value()
                                            || v == SubtitleAnchor::MiddleRight.value()) return 1;
                                        return 2; // Bottom* or Default
                                };
                                if (src.anchor().value() != SubtitleAnchor::Default.value()
                                    && verticalBand(got.anchor()) != verticalBand(src.anchor())) {
                                        ++anchorMismatchCount;
                                        if (firstFailDetail.isEmpty()) {
                                                firstFailDetail = String("source cue ") +
                                                                  String::number(static_cast<int64_t>(i)) +
                                                                  String(" vertical band mismatch: src=") +
                                                                  String::number(src.anchor().value()) +
                                                                  String(" got=") +
                                                                  String::number(got.anchor().value());
                                        }
                                }
                        }

                        ctx.setDetail(String("asciiSourceCues"), asciiSourceCues);
                        ctx.setDetail(String("matchedCues"), matchedCues);
                        ctx.setDetail(String("textMismatchCount"), textMismatchCount);
                        ctx.setDetail(String("anchorMismatchCount"), anchorMismatchCount);
                        ctx.setDetail(String("unmatchedCues"), unmatchedCues);

                        if (asciiSourceCues == 0) {
                                ctx.setFail(String("substest.srt has no ASCII-only cues to "
                                                    "compare; fixture mis-configured"));
                                return;
                        }
                        if (unmatchedCues > 0 || textMismatchCount > 0
                            || anchorMismatchCount > 0) {
                                ctx.setFail(firstFailDetail);
                                return;
                        }

                        ctx.setPass();
                }

                /// @brief 708 sibling of @ref runCea608SubRipRoundtripCase.
                ///
                /// Identical pipeline shape (TPG → Inspector AncData JSONL)
                /// but the TPG is pinned to @ref CaptionCodec::Cea708 so
                /// the per-frame CDP rides DTVCC triples instead of line-21
                /// pairs.  The reconstructor uses @ref Cea708Decoder and
                /// the comparison logic is the same whitespace-normalised
                /// flat-text check used for the 608 case — vertical anchor
                /// preservation isn't asserted because 708 DefineWindow
                /// uses an anchor_point/anchor_v/anchor_h tuple in
                /// percent-space units rather than a discrete row group,
                /// and the test fixture's broadcast-bottom convention
                /// always lands on the bottom band.
                void runCea708SubRipRoundtripCase(TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 30000);

                        const FilePath testFolder = ctx.testFolder();
                        const String   ancJsonl =
                                (testFolder / String("anc.jsonl")).toString();
                        const String   roundtripPath =
                                (testFolder / String("roundtrip.srt")).toString();

                        ctx.setDetail(String("ancJsonl"), ancJsonl);
                        ctx.setDetail(String("roundtripSrt"), roundtripPath);

                        String         loadErr;
                        SubtitleList source = loadSubstest(&loadErr);
                        if (source.isEmpty()) {
                                ctx.setSkip(String("substest.srt unavailable: ") + loadErr);
                                return;
                        }
                        ctx.setDetail(String("sourceCueCount"),
                                       static_cast<int64_t>(source.size()));
                        const String srtPath = resolveSubstestPath();
                        ctx.setDetail(String("sourceSrt"), srtPath);

                        const uint8_t serviceNumber = 1;
                        ctx.setDetail(String("serviceNumber"),
                                       static_cast<int64_t>(serviceNumber));

                        const uint32_t streamId = 0xCEA70800u;
                        const int32_t  frames =
                                static_cast<int32_t>(kCaptionRunSeconds * static_cast<int32_t>(kFps));
                        ctx.setDetail(String("framesScheduled"), int64_t(frames));

                        MediaPipelineConfig cfg;
                        cfg.addStage(makeCaptions708TpgStage(streamId, srtPath, serviceNumber));
                        cfg.addStage(makeAncInspectorStage(ancJsonl));
                        cfg.addRoute(String("tpg"), String("insp"));
                        cfg.setFrameCount(FrameCount(frames));

                        JsonObject pipelineDump;
                        {
                                MediaPipeline pipe;
                                PhaseOutcome  p =
                                        runPhase(pipe, cfg, loop, static_cast<unsigned int>(timeoutMs));
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("captions", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
                                }
                                if (!p.built) {
                                        ctx.setFail(String("pipeline build failed: ") +
                                                    p.buildError.desc());
                                        return;
                                }
                                if (!p.opened) {
                                        ctx.setFail(String("pipeline open failed: ") +
                                                    p.openError.desc());
                                        return;
                                }
                                if (!p.started) {
                                        ctx.setFail(String("pipeline start failed: ") +
                                                    p.startError.desc());
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("pipeline deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        ctx.setFail(String("pipeline error: ") + p.errorDetail);
                                        return;
                                }
                        }

                        String       readErr;
                        const String jsonl = readWholeFile(ancJsonl, &readErr);
                        if (jsonl.isEmpty()) {
                                ctx.setFail(String("anc.jsonl is empty: ") + readErr);
                                return;
                        }
                        int64_t      rowsParsed = 0;
                        SubtitleList recovered =
                                reconstructFromJsonl708(jsonl, serviceNumber, &rowsParsed);
                        ctx.setDetail(String("jsonlRowsParsed"), rowsParsed);
                        ctx.setDetail(String("recoveredCueCount"),
                                       static_cast<int64_t>(recovered.size()));

                        {
                                Buffer rtBuf = SubRip::emit(recovered);
                                File   rtFile(roundtripPath);
                                Error  oe = rtFile.open(IODevice::WriteOnly,
                                                          File::Create | File::Truncate);
                                if (oe.isOk()) {
                                        rtFile.write(rtBuf.data(),
                                                      static_cast<int64_t>(rtBuf.size()));
                                        rtFile.close();
                                } else {
                                        promekiWarn(
                                                "captions(708): failed to write roundtrip.srt: %s",
                                                oe.desc().cstr());
                                }
                        }

                        int64_t asciiSourceCues = 0;
                        int64_t matchedCues = 0;
                        int64_t textMismatchCount = 0;
                        int64_t unmatchedCues = 0;
                        String  firstFailDetail;
                        for (size_t i = 0; i < source.size(); ++i) {
                                const Subtitle &src = source[i];
                                if (src.text().isEmpty()) continue;
                                if (!textIsAsciiOnly(src.text())) continue;
                                ++asciiSourceCues;

                                const List<int64_t> idxs = findMatchingCues(recovered, src);
                                if (idxs.isEmpty()) {
                                        ++unmatchedCues;
                                        if (firstFailDetail.isEmpty()) {
                                                firstFailDetail = String("source cue ") +
                                                                  String::number(static_cast<int64_t>(i)) +
                                                                  String(" (\"") + src.text() +
                                                                  String("\") has no decoded match");
                                        }
                                        continue;
                                }
                                String concatGot;
                                for (size_t k = 0; k < idxs.size(); ++k) {
                                        const Subtitle &part =
                                                recovered[static_cast<size_t>(idxs[k])];
                                        if (k > 0) concatGot += String(" ");
                                        concatGot += part.text();
                                }
                                ++matchedCues;
                                const std::string srcN = normaliseWs(src.text());
                                const std::string gotN = normaliseWs(concatGot);
                                if (srcN != gotN) {
                                        ++textMismatchCount;
                                        if (firstFailDetail.isEmpty()) {
                                                firstFailDetail = String("source cue ") +
                                                                  String::number(static_cast<int64_t>(i)) +
                                                                  String(" text mismatch: src=\"") +
                                                                  String(srcN.c_str()) +
                                                                  String("\" got=\"") +
                                                                  String(gotN.c_str()) + String("\"");
                                        }
                                }
                        }

                        ctx.setDetail(String("asciiSourceCues"), asciiSourceCues);
                        ctx.setDetail(String("matchedCues"), matchedCues);
                        ctx.setDetail(String("textMismatchCount"), textMismatchCount);
                        ctx.setDetail(String("unmatchedCues"), unmatchedCues);

                        if (asciiSourceCues == 0) {
                                ctx.setFail(String("substest.srt has no ASCII-only cues to "
                                                    "compare; fixture mis-configured"));
                                return;
                        }
                        if (unmatchedCues > 0 || textMismatchCount > 0) {
                                ctx.setFail(firstFailDetail);
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerCaptionsCases() {
                TestRunner::registerCase(TestCase(
                        String("captions.cea608.subrip_roundtrip"),
                        String("CEA-608 SubRip caption round-trip: TPG → AncJsonl → Cea608Decoder "
                               "→ SubRip::emit"),
                        [](TestContext &ctx) { runCea608SubRipRoundtripCase(ctx); }));
                TestRunner::registerCase(TestCase(
                        String("captions.cea708.subrip_roundtrip"),
                        String("CEA-708 SubRip caption round-trip: TPG → AncJsonl → Cea708Decoder "
                               "→ SubRip::emit"),
                        [](TestContext &ctx) { runCea708SubRipRoundtripCase(ctx); }));
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
