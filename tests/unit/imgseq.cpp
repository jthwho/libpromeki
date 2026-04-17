/**
 * @file      imgseq.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <doctest/doctest.h>
#include <promeki/imgseq.h>
#include <promeki/dir.h>
#include <promeki/file.h>
#include <promeki/numname.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/json.h>

using namespace promeki;

static void makeDummyFile(const FilePath &path, char ch = 'x') {
        File f(path.toString());
        REQUIRE(f.open(IODevice::WriteOnly, File::Create | File::Truncate).isOk());
        f.write(&ch, 1);
        f.close();
}

// ============================================================================
// Construction and accessors
// ============================================================================

TEST_CASE("ImgSeq: default construction is invalid") {
        ImgSeq seq;
        CHECK_FALSE(seq.isValid());
        CHECK(seq.head() == 0);
        CHECK(seq.tail() == 0);
        CHECK(seq.length() == 0);
        CHECK_FALSE(seq.frameRate().isValid());
        CHECK_FALSE(seq.pixelDesc().isValid());
}

TEST_CASE("ImgSeq: populated accessors round-trip") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(10);
        seq.setTail(20);
        seq.setFrameRate(FrameRate(FrameRate::FPS_24));
        seq.setVideoSize(Size2Du32(1920, 1080));
        seq.setPixelDesc(PixelDesc(PixelDesc::RGB8_sRGB));

        CHECK(seq.isValid());
        CHECK(seq.name().prefix() == "shot_");
        CHECK(seq.name().suffix() == ".dpx");
        CHECK(seq.name().digits() == 4);
        CHECK(seq.name().isPadded());
        CHECK(seq.head() == 10);
        CHECK(seq.tail() == 20);
        CHECK(seq.length() == 11);
        CHECK(seq.frameRate() == FrameRate(FrameRate::FPS_24));
        CHECK(seq.videoSize() == Size2Du32(1920, 1080));
        CHECK(seq.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("ImgSeq: frameFileName computes correct filename") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(100);
        seq.setTail(200);

        CHECK(seq.frameFileName(0) == "shot_0100.dpx");
        CHECK(seq.frameFileName(1) == "shot_0101.dpx");
        CHECK(seq.frameFileName(42) == "shot_0142.dpx");
}

TEST_CASE("ImgSeq: length handles inverted range") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("x_####.dpx"));
        seq.setHead(5);
        seq.setTail(5);
        CHECK(seq.length() == 1);

        seq.setHead(10);
        seq.setTail(3);
        CHECK(seq.length() == 0);
}

// ============================================================================
// JSON serialization
// ============================================================================

TEST_CASE("ImgSeq: toJson has expected fields") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(1);
        seq.setTail(100);
        seq.setFrameRate(FrameRate(FrameRate::FPS_24));

        JsonObject root = seq.toJson();
        CHECK(root.contains("type"));
        CHECK(root.getString("type") == "imgseq");
        CHECK(root.contains("name"));
        CHECK(root.getString("name") == "shot_####.dpx");
        CHECK(root.contains("head"));
        CHECK(root.getInt("head") == 1);
        CHECK(root.contains("tail"));
        CHECK(root.getInt("tail") == 100);
        CHECK(root.contains("frameRate"));
}

TEST_CASE("ImgSeq: toJson omits empty optional fields") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(1);
        seq.setTail(10);

        JsonObject root = seq.toJson();
        // No frame rate, video size, pixel desc, metadata set — should
        // not appear in the JSON.
        CHECK_FALSE(root.contains("frameRate"));
        CHECK_FALSE(root.contains("videoSize"));
        CHECK_FALSE(root.contains("pixelDesc"));
        CHECK_FALSE(root.contains("metadata"));
}

TEST_CASE("ImgSeq: fromJson round-trips") {
        ImgSeq orig;
        orig.setName(NumName::fromMask("shot_####.dpx"));
        orig.setHead(5);
        orig.setTail(25);
        orig.setFrameRate(FrameRate(FrameRate::FPS_24));
        orig.setVideoSize(Size2Du32(1920, 1080));

        JsonObject json = orig.toJson();
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(json, &err);
        CHECK(err.isOk());
        CHECK(parsed.isValid());
        CHECK(parsed.name().prefix() == "shot_");
        CHECK(parsed.name().suffix() == ".dpx");
        CHECK(parsed.head() == 5);
        CHECK(parsed.tail() == 25);
        CHECK(parsed.frameRate() == FrameRate(FrameRate::FPS_24));
        CHECK(parsed.videoSize() == Size2Du32(1920, 1080));
}

TEST_CASE("ImgSeq: fromJson rejects missing type") {
        JsonObject root;
        root.set("name", String("shot_####.dpx"));
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(root, &err);
        CHECK(err.isError());
        CHECK_FALSE(parsed.isValid());
}

TEST_CASE("ImgSeq: fromJson rejects wrong type") {
        JsonObject root;
        root.set("type", String("other_format"));
        root.set("name", String("shot_####.dpx"));
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(root, &err);
        CHECK(err.isError());
        CHECK_FALSE(parsed.isValid());
}

TEST_CASE("ImgSeq: fromJson rejects missing name") {
        JsonObject root;
        root.set("type", String("imgseq"));
        root.set("head", 1);
        root.set("tail", 10);
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(root, &err);
        CHECK(err.isError());
        CHECK_FALSE(parsed.isValid());
}

TEST_CASE("ImgSeq: fromJson accepts printf-style mask") {
        JsonObject root;
        root.set("type", String("imgseq"));
        root.set("name", String("shot_%04d.dpx"));
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(root, &err);
        CHECK(err.isOk());
        CHECK(parsed.isValid());
        CHECK(parsed.name().digits() == 4);
        CHECK(parsed.name().isPadded());
}

TEST_CASE("ImgSeq: fromJson accepts plain filename and parses it") {
        // If the user wrote a concrete filename rather than a mask,
        // fall back to NumName::parse so we still get a usable pattern.
        JsonObject root;
        root.set("type", String("imgseq"));
        root.set("name", String("shot_0001.dpx"));
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(root, &err);
        CHECK(err.isOk());
        CHECK(parsed.isValid());
        CHECK(parsed.name().prefix() == "shot_");
        CHECK(parsed.name().digits() == 4);
        CHECK(parsed.name().isPadded());
}

TEST_CASE("ImgSeq: isImgSeqJson detects valid sidecar") {
        JsonObject root;
        root.set("type", String("imgseq"));
        root.set("name", String("shot_####.dpx"));
        String text = root.toString();
        CHECK(ImgSeq::isImgSeqJson(text));
}

TEST_CASE("ImgSeq: isImgSeqJson rejects non-imgseq JSON") {
        String text = "{\"type\": \"other\", \"data\": 42}";
        CHECK_FALSE(ImgSeq::isImgSeqJson(text));
}

TEST_CASE("ImgSeq: isImgSeqJson rejects garbage") {
        CHECK_FALSE(ImgSeq::isImgSeqJson("not json at all"));
        CHECK_FALSE(ImgSeq::isImgSeqJson(""));
        CHECK_FALSE(ImgSeq::isImgSeqJson("[1, 2, 3]"));
}

// ============================================================================
// File I/O
// ============================================================================

TEST_CASE("ImgSeq: save and load round-trip") {
        Dir t = Dir::temp();
        FilePath scratch = t.path() / "promeki_imgseq_file";
        Dir d(scratch);
        if(d.exists()) d.removeRecursively();
        d.mkdir();
        FilePath sidecar = scratch / "clip.imgseq";

        ImgSeq orig;
        orig.setName(NumName::fromMask("clip_####.dpx"));
        orig.setHead(1);
        orig.setTail(100);
        orig.setFrameRate(FrameRate(FrameRate::FPS_24));
        orig.setVideoSize(Size2Du32(1920, 1080));
        orig.metadata().set(Metadata::Title, String("Shot 1"));

        CHECK(orig.save(sidecar).isOk());
        CHECK(sidecar.exists());

        Error err;
        ImgSeq loaded = ImgSeq::load(sidecar, &err);
        CHECK(err.isOk());
        CHECK(loaded.isValid());
        CHECK(loaded.name().prefix() == "clip_");
        CHECK(loaded.name().digits() == 4);
        CHECK(loaded.head() == 1);
        CHECK(loaded.tail() == 100);
        CHECK(loaded.frameRate() == FrameRate(FrameRate::FPS_24));
        CHECK(loaded.videoSize() == Size2Du32(1920, 1080));
        CHECK(loaded.metadata().contains(Metadata::Title));
        CHECK(loaded.metadata().getAs<String>(Metadata::Title) == "Shot 1");
        CHECK(loaded.sidecarPath() == sidecar);

        d.removeRecursively();
}

TEST_CASE("ImgSeq: load fails on missing file") {
        Error err;
        ImgSeq loaded = ImgSeq::load(FilePath("/nonexistent_path_imgseq_12345.imgseq"), &err);
        CHECK(err.isError());
        CHECK_FALSE(loaded.isValid());
}

TEST_CASE("ImgSeq: load fails on non-JSON file") {
        Dir t = Dir::temp();
        FilePath scratch = t.path() / "promeki_imgseq_badjson";
        Dir d(scratch);
        if(d.exists()) d.removeRecursively();
        d.mkdir();
        FilePath sidecar = scratch / "bad.imgseq";
        File f(sidecar.toString());
        REQUIRE(f.open(IODevice::WriteOnly, File::Create | File::Truncate).isOk());
        const char *junk = "this is not JSON at all";
        f.write(junk, std::strlen(junk));
        f.close();

        Error err;
        ImgSeq loaded = ImgSeq::load(sidecar, &err);
        CHECK(err.isError());
        CHECK_FALSE(loaded.isValid());

        d.removeRecursively();
}

TEST_CASE("ImgSeq: save fails on invalid sequence") {
        ImgSeq seq;  // default-constructed — no pattern set
        Dir t = Dir::temp();
        FilePath sidecar = t.path() / "promeki_imgseq_should_not_exist.imgseq";
        CHECK(seq.save(sidecar).isError());
}

// ============================================================================
// Range detection
// ============================================================================

TEST_CASE("ImgSeq: detectRange scans folder") {
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_imgseq_detect";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        makeDummyFile(dir / "shot_0005.dpx");
        makeDummyFile(dir / "shot_0010.dpx");
        makeDummyFile(dir / "shot_0100.dpx");
        makeDummyFile(dir / "readme.txt");  // non-matching

        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        CHECK(seq.detectRange(dir).isOk());
        CHECK(seq.head() == 5);
        CHECK(seq.tail() == 100);

        d.removeRecursively();
}

TEST_CASE("ImgSeq: detectRange ignores other sequences") {
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_imgseq_detect_mixed";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        // Two different sequences sharing a directory.
        makeDummyFile(dir / "shotA_0001.dpx");
        makeDummyFile(dir / "shotA_0002.dpx");
        makeDummyFile(dir / "shotA_0003.dpx");
        makeDummyFile(dir / "shotB_0010.dpx");
        makeDummyFile(dir / "shotB_0020.dpx");

        ImgSeq seqA;
        seqA.setName(NumName::fromMask("shotA_####.dpx"));
        CHECK(seqA.detectRange(dir).isOk());
        CHECK(seqA.head() == 1);
        CHECK(seqA.tail() == 3);

        ImgSeq seqB;
        seqB.setName(NumName::fromMask("shotB_####.dpx"));
        CHECK(seqB.detectRange(dir).isOk());
        CHECK(seqB.head() == 10);
        CHECK(seqB.tail() == 20);

        d.removeRecursively();
}

TEST_CASE("ImgSeq: detectRange leaves range unchanged when no matches") {
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_imgseq_detect_empty";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        makeDummyFile(dir / "unrelated.txt");

        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(99);
        seq.setTail(99);
        // detectRange returns Ok even with no matches and leaves head/tail alone.
        CHECK(seq.detectRange(dir).isOk());
        CHECK(seq.head() == 99);
        CHECK(seq.tail() == 99);

        d.removeRecursively();
}

TEST_CASE("ImgSeq: detectRange on invalid pattern fails") {
        ImgSeq seq;  // no pattern
        Dir t = Dir::temp();
        CHECK(seq.detectRange(t.path()).isError());
}

// ============================================================================
// dir field (image directory separate from sidecar location)
// ============================================================================

TEST_CASE("ImgSeq: dir field defaults to empty") {
        ImgSeq seq;
        CHECK(seq.dir().isEmpty());
}

TEST_CASE("ImgSeq: setDir / dir round-trip") {
        ImgSeq seq;
        FilePath d("/media/renders/shot001");
        seq.setDir(d);
        CHECK(seq.dir() == d);
}

TEST_CASE("ImgSeq: toJson omits dir when empty") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(1);
        seq.setTail(10);
        JsonObject root = seq.toJson();
        CHECK_FALSE(root.contains("dir"));
}

TEST_CASE("ImgSeq: toJson includes dir when set") {
        ImgSeq seq;
        seq.setName(NumName::fromMask("shot_####.dpx"));
        seq.setHead(1);
        seq.setTail(10);
        seq.setDir(FilePath("/media/renders/shot001"));
        JsonObject root = seq.toJson();
        REQUIRE(root.contains("dir"));
        CHECK(root.getString("dir") == "/media/renders/shot001");
}

TEST_CASE("ImgSeq: fromJson parses dir field") {
        JsonObject root;
        root.set("type", String("imgseq"));
        root.set("name", String("shot_####.dpx"));
        root.set("dir", String("/media/renders/shot001"));
        Error err;
        ImgSeq seq = ImgSeq::fromJson(root, &err);
        CHECK(err.isOk());
        CHECK(seq.isValid());
        CHECK(seq.dir() == FilePath("/media/renders/shot001"));
}

TEST_CASE("ImgSeq: dir round-trips through JSON") {
        ImgSeq orig;
        orig.setName(NumName::fromMask("shot_####.dpx"));
        orig.setHead(1);
        orig.setTail(5);
        orig.setDir(FilePath("../images/shot001"));
        JsonObject json = orig.toJson();
        Error err;
        ImgSeq parsed = ImgSeq::fromJson(json, &err);
        CHECK(err.isOk());
        CHECK(parsed.dir() == FilePath("../images/shot001"));
}

TEST_CASE("ImgSeq: dir round-trips through save/load") {
        Dir t = Dir::temp();
        FilePath scratch = t.path() / "promeki_imgseq_dir_field";
        Dir d(scratch);
        if(d.exists()) d.removeRecursively();
        d.mkdir();
        FilePath sidecar = scratch / "clip.imgseq";

        ImgSeq orig;
        orig.setName(NumName::fromMask("clip_####.exr"));
        orig.setHead(1);
        orig.setTail(24);
        orig.setDir(FilePath("../renders/clip"));

        CHECK(orig.save(sidecar).isOk());

        Error err;
        ImgSeq loaded = ImgSeq::load(sidecar, &err);
        CHECK(err.isOk());
        CHECK(loaded.dir() == FilePath("../renders/clip"));

        d.removeRecursively();
}

TEST_CASE("ImgSeq: save writes human-readable JSON with indentation") {
        Dir t = Dir::temp();
        FilePath scratch = t.path() / "promeki_imgseq_readable";
        Dir d(scratch);
        if(d.exists()) d.removeRecursively();
        d.mkdir();
        FilePath sidecar = scratch / "test.imgseq";

        ImgSeq seq;
        seq.setName(NumName::fromMask("t_####.dpx"));
        seq.setHead(1);
        seq.setTail(10);
        CHECK(seq.save(sidecar).isOk());

        // Read back the raw text and confirm it's multi-line (indented).
        File f(sidecar.toString());
        REQUIRE(f.open(IODevice::ReadOnly).isOk());
        Buffer data = f.readAll();
        f.close();
        String text(static_cast<const char *>(data.data()), data.size());
        CHECK(text.contains("\n"));
        CHECK(text.contains("\"type\""));
        CHECK(text.contains("\"imgseq\""));

        d.removeRecursively();
}
