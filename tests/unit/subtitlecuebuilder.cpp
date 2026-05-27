/**
 * @file      tests/subtitlecuebuilder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the SubtitleCueBuilder policy: maxCharsPerLine wrap,
 * min/max display duration clamping, partial-cue gating
 * (SubtitleCueEmitPartials), anchor stamping, and the static
 * batch buildCues helper.  Uses hand-built Transcript inputs (no
 * real engine) so the cue-shaping math is testable in isolation.
 */

#include <doctest/doctest.h>
#include <promeki/duration.h>
#include <promeki/enums_subtitle.h>
#include <promeki/frame.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/subtitle.h>
#include <promeki/subtitlecuebuilder.h>
#include <promeki/timestamp.h>
#include <promeki/transcript.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // Build a Transcript covering [startMs, startMs+durationMs)
        // with one word per token in @p tokens.  Word timestamps
        // distribute evenly across the duration so the resulting
        // Transcript::start / Transcript::end match.
        Transcript makeTranscript(int64_t startMs, int64_t durationMs, const StringList &tokens,
                                  bool partial = false, const String &speaker = String(),
                                  const String &language = String()) {
                const int64_t startNs = startMs * 1'000'000;
                const int64_t durNs   = durationMs * 1'000'000;
                TranscriptWord::List words;
                const size_t n = tokens.size();
                for (size_t i = 0; i < n; ++i) {
                        int64_t wStartNs = startNs + (durNs * static_cast<int64_t>(i)) / static_cast<int64_t>(n);
                        int64_t wEndNs = startNs + (durNs * static_cast<int64_t>(i + 1)) / static_cast<int64_t>(n);
                        words.pushToBack(TranscriptWord(tokens[i], TimeStamp(wStartNs), TimeStamp(wEndNs), 0.9f));
                }
                return Transcript(std::move(words), speaker, language, 0.9f, partial);
        }

        Frame frameWithTranscript(const Transcript &t) {
                Frame f;
                f.metadata().set(Metadata::Transcript, Variant(t));
                return f;
        }

} // namespace

TEST_CASE("SubtitleCueBuilder: batch buildCues emits one cue per finalised transcript") {
        TranscriptList transcripts;
        transcripts.append(makeTranscript(0, 1500, {"Hello", "world."}));
        transcripts.append(makeTranscript(2000, 1500, {"How", "are", "you?"}));

        MediaConfig cfg;
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);

        REQUIRE(cues.size() == 2);
        CHECK(cues[0].text() == "Hello world.");
        CHECK(cues[1].text() == "How are you?");
        // Anchor defaults to BottomCenter.
        CHECK(cues[0].anchor() == SubtitleAnchor::BottomCenter);
}

TEST_CASE("SubtitleCueBuilder: batch buildCues skips partials by default") {
        TranscriptList transcripts;
        transcripts.append(makeTranscript(0, 1000, {"Interim..."}, /*partial=*/true));
        transcripts.append(makeTranscript(1000, 1500, {"Final."}, /*partial=*/false));

        MediaConfig cfg;
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);

        REQUIRE(cues.size() == 1);
        CHECK(cues[0].text() == "Final.");
        CHECK_FALSE(cues[0].partial());
}

TEST_CASE("SubtitleCueBuilder: SubtitleCueEmitPartials surfaces interim cues with partial set") {
        TranscriptList transcripts;
        transcripts.append(makeTranscript(0, 1000, {"Interim"}, /*partial=*/true));
        transcripts.append(makeTranscript(1000, 1500, {"Final."}, /*partial=*/false));

        MediaConfig cfg;
        cfg.set(MediaConfig::SubtitleCueEmitPartials, true);
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);

        REQUIRE(cues.size() == 2);
        CHECK(cues[0].partial());
        CHECK_FALSE(cues[1].partial());
}

TEST_CASE("SubtitleCueBuilder: SubtitleCueMinDuration extends short cues") {
        TranscriptList transcripts;
        // 200 ms cue — way under the 1 s default minimum.
        transcripts.append(makeTranscript(/*startMs=*/500, /*durationMs=*/200, {"Hi."}));

        MediaConfig cfg;
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);
        REQUIRE(cues.size() == 1);
        // Default min is 1 s; expect end == start + 1 s.
        CHECK(cues[0].end().nanoseconds() == cues[0].start().nanoseconds() + 1'000'000'000);
}

TEST_CASE("SubtitleCueBuilder: SubtitleCueMaxDuration truncates long cues") {
        TranscriptList transcripts;
        // 12 s cue — over the 7 s default max.
        transcripts.append(makeTranscript(0, 12'000, {"Long", "sentence", "goes", "on."}));

        MediaConfig cfg;
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);
        REQUIRE(cues.size() == 1);
        // Default max is 7 s; expect end == start + 7 s.
        CHECK(cues[0].end().nanoseconds() == cues[0].start().nanoseconds() + 7'000'000'000);
}

TEST_CASE("SubtitleCueBuilder: custom anchor flows from MediaConfig") {
        TranscriptList transcripts;
        transcripts.append(makeTranscript(0, 1500, {"Hello."}));

        MediaConfig cfg;
        cfg.set(MediaConfig::SubtitleCueAnchor, SubtitleAnchor(SubtitleAnchor::TopCenter));
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);
        REQUIRE(cues.size() == 1);
        CHECK(cues[0].anchor() == SubtitleAnchor::TopCenter);
}

TEST_CASE("SubtitleCueBuilder: speaker label propagates from Transcript to Subtitle") {
        TranscriptList transcripts;
        transcripts.append(makeTranscript(0, 1500, {"Yes."}, /*partial=*/false, String("S1")));
        MediaConfig cfg;
        SubtitleList cues = SubtitleCueBuilder::buildCues(transcripts, cfg);
        REQUIRE(cues.size() == 1);
        CHECK(cues[0].speaker() == "S1");
}

TEST_CASE("SubtitleCueBuilder: streaming submitFrame / receiveFrame round-trip") {
        SubtitleCueBuilder builder;
        MediaConfig        cfg;
        builder.configure(cfg);

        Transcript tr = makeTranscript(0, 1500, {"Streaming."});
        REQUIRE(builder.submitFrame(frameWithTranscript(tr)) == Error::Ok);

        Frame out = builder.receiveFrame();
        REQUIRE(out.isValid());
        REQUIRE(out.metadata().contains(Metadata::Subtitle));
        Subtitle cue = out.metadata().get(Metadata::Subtitle).get<Subtitle>();
        CHECK(cue.text() == "Streaming.");

        // No more output pending.
        CHECK_FALSE(builder.receiveFrame().isValid());
}

TEST_CASE("SubtitleCueBuilder: partial transcripts are filtered in streaming mode (default)") {
        SubtitleCueBuilder builder;
        MediaConfig        cfg;
        builder.configure(cfg);

        Transcript partial = makeTranscript(0, 1000, {"Interim"}, /*partial=*/true);
        REQUIRE(builder.submitFrame(frameWithTranscript(partial)) == Error::Ok);
        // Filter dropped the cue; nothing on receive.
        CHECK_FALSE(builder.receiveFrame().isValid());

        // Now a final — should come through.
        Transcript fin = makeTranscript(1000, 1500, {"Final."}, /*partial=*/false);
        REQUIRE(builder.submitFrame(frameWithTranscript(fin)) == Error::Ok);
        Frame out = builder.receiveFrame();
        REQUIRE(out.isValid());
        CHECK(out.metadata().get(Metadata::Subtitle).get<Subtitle>().text() == "Final.");
}

TEST_CASE("SubtitleCueBuilder: Frames without Metadata::Transcript pass through") {
        SubtitleCueBuilder builder;
        MediaConfig        cfg;
        builder.configure(cfg);

        Frame plain;
        // Set some unrelated metadata so the Frame is non-empty.
        plain.metadata().set(Metadata::Title, String("not a transcript"));
        REQUIRE(builder.submitFrame(plain) == Error::Ok);

        Frame out = builder.receiveFrame();
        REQUIRE(out.isValid());
        // No Subtitle stamped — pass-through.
        CHECK_FALSE(out.metadata().contains(Metadata::Subtitle));
        // Original metadata preserved.
        CHECK(out.metadata().get(Metadata::Title).get<String>() == "not a transcript");
}
