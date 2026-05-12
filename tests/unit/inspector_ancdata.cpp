/**
 * @file      inspector_ancdata.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end coverage for the Inspector's AncData JSONL dump.  Pipes a
 * TPG configured to inject CEA-708 captions into an Inspector
 * configured to dump ANC packets, then reads the JSONL output and
 * verifies each line contains the expected fields and decoded text.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <promeki/dir.h>
#include <promeki/elapsedtimer.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/eventloop.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/json.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaioportconnection.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

        template <typename Pred> bool pumpUntil(EventLoop &loop, Pred pred, int64_t timeoutMs = 5000) {
                ElapsedTimer t;
                t.start();
                while (t.elapsed() < timeoutMs) {
                        loop.processEvents();
                        if (pred()) return true;
                        Thread::sleepMs(5);
                }
                return false;
        }

        // Picks a unique JSONL path under Dir::temp() — same convention
        // the inspector's auto-name path uses, but explicit so the test
        // can read it back.
        String uniqueAncJsonlPath() {
                const int64_t ns = TimeStamp::now().nanoseconds();
                FilePath      p = Dir::temp().path() /
                             String::sprintf("promeki_inspector_anc_test_%lld.jsonl",
                                             static_cast<long long>(ns));
                return p.toString();
        }

        StringList readLines(const String &path) {
                StringList out;
                File       f(path);
                if (f.open(IODevice::ReadOnly).isError()) return out;
                String all;
                Buffer buf(1 << 20);
                buf.setSize(0);
                while (!f.atEnd()) {
                        char    chunk[4096];
                        int64_t got = f.read(chunk, sizeof(chunk));
                        if (got <= 0) break;
                        all += String(chunk, static_cast<size_t>(got));
                }
                f.close();
                // Manual newline split — String doesn't expose split-by-char
                // in the same convenient form across the codebase, so we
                // walk byte-by-byte.
                String cur;
                for (size_t i = 0; i < all.size(); ++i) {
                        char c = all.cstr()[i];
                        if (c == '\n') {
                                if (!cur.isEmpty()) out.pushToBack(cur);
                                cur = String();
                        } else {
                                cur += c;
                        }
                }
                if (!cur.isEmpty()) out.pushToBack(cur);
                return out;
        }

} // namespace

TEST_CASE("Inspector: AncData test dumps per-frame JSONL with CEA-708 packets from TPG") {
        EventLoop loop;

        const String  ancJsonlPath = uniqueAncJsonlPath();
        const String  captionText = "PROMEKI";

        // Write a tiny SubRip fixture carrying the caption.  Cue start
        // is placed at 0.5s so the encoder has enough pre-roll
        // headroom at 59.94 fps (firstFrame ~ 22).
        const int64_t ns = TimeStamp::now().nanoseconds();
        const String  srtPath = (Dir::temp().path()
                                / String::sprintf("promeki_inspector_anc_test_%lld.srt",
                                                   static_cast<long long>(ns)))
                                       .toString();
        {
                File f(srtPath);
                REQUIRE(f.open(IODevice::WriteOnly, File::Create | File::Truncate).isOk());
                String body = "1\r\n00:00:00,500 --> 00:00:02,000\r\nPROMEKI\r\n\r\n";
                f.write(body.cstr(), static_cast<int64_t>(body.byteCount()));
                f.close();
        }

        // Source: TPG with CEA-708 captions enabled (SubRip-driven).
        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        srcCfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        srcCfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        srcCfg.set(MediaConfig::TpgAncCaptionsLine, int32_t(11));
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        // Sink: Inspector with only AncData enabled so the rest of the
        // checks don't drown the JSONL with unrelated config noise.
        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Inspector");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::InspectorDropFrames, false);
        sinkCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        sinkCfg.set(MediaConfig::InspectorAncDataFile, ancJsonlPath);
        EnumList tests = EnumList::forType<InspectorTest>();
        tests.append(InspectorTest::AncData);
        sinkCfg.set(MediaConfig::InspectorTests, tests);
        MediaIO *sink = MediaIO::create(sinkCfg);
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        MediaIOPortConnection conn(src->source(0), sink->sink(0));
        REQUIRE(conn.start().isOk());

        // 40 frames at 59.94 fps spans the cue's pre-roll (RCL @ frame 22)
        // through the EOC pair (@ 30, 31) and a few null-pair frames after
        // the cue is fully loaded — enough to verify the encoder's byte
        // stream lands intact in the JSONL.
        const int kFrames = 40;
        REQUIRE(pumpUntil(loop, [&]() { return conn.framesTransferred() >= kFrames; }, 5000));

        conn.stop();
        (void)src->close().wait();
        (void)sink->close().wait();
        delete src;
        delete sink;

        // Inspect the JSONL file.
        StringList lines = readLines(ancJsonlPath);
        REQUIRE(lines.size() >= static_cast<size_t>(kFrames));

        for (size_t i = 0; i < static_cast<size_t>(kFrames); ++i) {
                Error      err;
                JsonObject row = JsonObject::parse(lines[i], &err);
                INFO("row[", i, "] = ", lines[i].cstr());
                REQUIRE(err.isOk());
                CHECK(row.getInt("frame") == static_cast<int64_t>(i));
                CHECK(row.getInt("payloadCount") == 1);
                JsonArray packets = row.getArray("packets");
                REQUIRE(packets.size() == 1);
                JsonObject pkt = packets.at(0).toObject();
                CHECK(pkt.getString("format") == "Cea708");
                CHECK(pkt.getString("transport") == "St291");
                CHECK(pkt.getInt("line") == 11);

                JsonObject parsed = pkt.getObject("parsed");
                REQUIRE(parsed.size() > 0);
                CHECK(parsed.getBool("ccDataPresent") == true);
                JsonArray ccArr = parsed.getArray("ccData");
                // The Cea608Encoder emits exactly one CcData triple
                // per frame (either a control-code pair, a character
                // pair, or the null filler).
                CHECK(ccArr.size() == 1);
                // Sequence counter advances each frame.
                CHECK(parsed.getInt("sequenceCounter") == static_cast<int64_t>(i));
        }

        // Round-trip the visible text by walking every row's cc_data
        // and concatenating printable ASCII bytes (parity-stripped).
        // The Cea608Encoder emits control codes (0x00..0x1F after
        // parity strip) interspersed with character pairs; we filter
        // those out and accumulate only the printable bytes.  The
        // resulting string must contain @c captionText somewhere.
        String reconstructed;
        for (size_t r = 0; r < lines.size(); ++r) {
                Error      err;
                JsonObject row = JsonObject::parse(lines[r], &err);
                if (err.isError() || row.getArray("packets").size() == 0) continue;
                JsonObject pkt = row.getArray("packets").at(0).toObject();
                if (!pkt.contains("parsed")) continue;
                JsonObject parsed = pkt.getObject("parsed");
                if (!parsed.contains("ccData")) continue;
                JsonArray cc = parsed.getArray("ccData");
                for (int i = 0; i < cc.size(); ++i) {
                        JsonObject t = cc.at(i).toObject();
                        uint8_t    b1 = static_cast<uint8_t>(t.getInt("b1")) & 0x7F;
                        uint8_t    b2 = static_cast<uint8_t>(t.getInt("b2")) & 0x7F;
                        if (b1 >= 0x20 && b1 <= 0x7E) reconstructed += static_cast<char>(b1);
                        if (b2 >= 0x20 && b2 <= 0x7E) reconstructed += static_cast<char>(b2);
                }
        }
        // captionText is odd-length so the encoder pads the final
        // pair with a space; the prefix match still holds.
        CHECK(reconstructed.contains(captionText));

        (void)std::remove(ancJsonlPath.cstr());
        (void)std::remove(srtPath.cstr());
}
