/**
 * @file      debugmediafile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/debugmediafile.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/buffer.h>
#include <promeki/pixeldesc.h>
#include <promeki/audiodesc.h>
#include <promeki/mediapacket.h>
#include <promeki/metadata.h>
#include <promeki/dir.h>
#include <promeki/filepath.h>
#include <promeki/file.h>
#include <promeki/enums.h>

using namespace promeki;

namespace {

FilePath scratchDir() {
        FilePath dir = Dir::temp().path() / "promeki_test_pmdf";
        Dir d(dir);
        if(!d.exists()) d.mkdir();
        return dir;
}

FilePath scratchFile(const String &name) {
        return scratchDir() / name;
}

Frame::Ptr makeTestFrame(int64_t frameNumber) {
        Frame::Ptr f = Frame::Ptr::create();
        Frame *raw = f.modify();

        raw->metadata().set(Metadata::FrameNumber, frameNumber);
        raw->metadata().set(Metadata::Timecode,
                            Timecode(Timecode::NDF24, 1, 0, 0,
                                     static_cast<uint8_t>(frameNumber)));
        raw->metadata().set(Metadata::Title, String("pmdf-test"));

        // One RGB image with a frame-number-dependent pattern.
        ImageDesc idesc(Size2Du32(16, 8), PixelDesc::RGB8_sRGB);
        idesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(idesc);
        uint8_t *d = static_cast<uint8_t *>(img->data(0));
        size_t bytes = img->plane(0)->size();
        for(size_t b = 0; b < bytes; ++b) {
                d[b] = static_cast<uint8_t>((b + frameNumber * 13) & 0xFF);
        }

        // Add image-level metadata too.
        img.modify()->metadata().set(Metadata::FrameNumber, frameNumber);
        raw->imageList().pushToBack(img);

        // One PCM audio track.
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio::Ptr aud = Audio::Ptr::create(adesc, 32);
        int16_t *s = aud->data<int16_t>();
        for(size_t i = 0; i < 32 * 2; ++i) {
                s[i] = static_cast<int16_t>((i + frameNumber * 7) & 0x7FFF);
        }
        raw->audioList().pushToBack(aud);

        return f;
}

bool imagesEqual(const Image &a, const Image &b) {
        if(a.desc().size() != b.desc().size()) return false;
        if(a.desc().pixelDesc() != b.desc().pixelDesc()) return false;
        if(a.planes().size() != b.planes().size()) return false;
        for(size_t i = 0; i < a.planes().size(); ++i) {
                if(a.plane(i)->size() != b.plane(i)->size()) return false;
                if(std::memcmp(a.plane(i)->data(), b.plane(i)->data(),
                               a.plane(i)->size()) != 0) return false;
        }
        return true;
}

bool audioEqual(const Audio &a, const Audio &b) {
        if(a.desc().sampleRate() != b.desc().sampleRate()) return false;
        if(a.desc().channels()   != b.desc().channels())   return false;
        if(a.desc().dataType()   != b.desc().dataType())   return false;
        if(a.samples()           != b.samples())           return false;
        if(!a.buffer().isValid() || !b.buffer().isValid())
                return a.buffer().isValid() == b.buffer().isValid();
        if(a.buffer()->size() != b.buffer()->size()) return false;
        return std::memcmp(a.buffer()->data(), b.buffer()->data(),
                           a.buffer()->size()) == 0;
}

} // namespace

TEST_CASE("DebugMediaFile: round-trips a single frame") {
        String fn = scratchFile("roundtrip_single.pmdf").toString();

        Frame::Ptr original = makeTestFrame(0);

        {
                DebugMediaFile w;
                DebugMediaFile::OpenOptions opts;
                opts.sessionInfo.set(Metadata::Software, String("pmdf-test"));
                REQUIRE(w.open(fn, DebugMediaFile::Write, opts).isOk());
                CHECK(w.writeFrame(original).isOk());
                CHECK(w.framesWritten() == 1);
                CHECK(w.close().isOk());
        }

        {
                DebugMediaFile r;
                REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
                CHECK(r.sessionInfo().get(Metadata::Software).get<String>() == "pmdf-test");
                CHECK(r.hasFooter());
                CHECK(r.frameCount() == 1);

                Frame::Ptr got;
                CHECK(r.readFrame(got).isOk());
                REQUIRE(got.isValid());
                CHECK(got->imageList().size() == 1);
                CHECK(got->audioList().size() == 1);
                CHECK(imagesEqual(*got->imageList()[0], *original->imageList()[0]));
                CHECK(audioEqual(*got->audioList()[0], *original->audioList()[0]));
                CHECK(got->metadata().getAs<String>(Metadata::Title)
                      == original->metadata().getAs<String>(Metadata::Title));
                CHECK(got->metadata().getAs<int64_t>(Metadata::FrameNumber)
                      == original->metadata().getAs<int64_t>(Metadata::FrameNumber));

                // Next read hits EOF.
                Frame::Ptr eof;
                CHECK(r.readFrame(eof) == Error::EndOfFile);
                CHECK(r.close().isOk());
        }

}

TEST_CASE("DebugMediaFile: round-trips multiple frames") {
        String fn = scratchFile("roundtrip_many.pmdf").toString();

        constexpr int N = 6;
        Frame::PtrList originals;
        {
                DebugMediaFile w;
                REQUIRE(w.open(fn, DebugMediaFile::Write).isOk());
                for(int i = 0; i < N; ++i) {
                        Frame::Ptr f = makeTestFrame(i);
                        originals.pushToBack(f);
                        CHECK(w.writeFrame(f).isOk());
                }
                CHECK(w.framesWritten() == N);
                CHECK(w.close().isOk());
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
        CHECK(r.frameCount() == N);
        CHECK(r.index().size() == N);

        for(int i = 0; i < N; ++i) {
                Frame::Ptr got;
                CHECK(r.readFrame(got).isOk());
                REQUIRE(got.isValid());
                CHECK(imagesEqual(*got->imageList()[0],
                                  *originals[i]->imageList()[0]));
                CHECK(audioEqual(*got->audioList()[0],
                                 *originals[i]->audioList()[0]));
                CHECK(got->metadata()
                          .getAs<int64_t>(Metadata::FrameNumber) == i);
        }

        Frame::Ptr eof;
        CHECK(r.readFrame(eof) == Error::EndOfFile);
}

TEST_CASE("DebugMediaFile: seek and readFrameAt jump around") {
        String fn = scratchFile("seek.pmdf").toString();

        {
                DebugMediaFile w;
                REQUIRE(w.open(fn, DebugMediaFile::Write).isOk());
                for(int i = 0; i < 5; ++i) {
                        CHECK(w.writeFrame(makeTestFrame(i)).isOk());
                }
                CHECK(w.close().isOk());
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());

        Frame::Ptr got;
        CHECK(r.readFrameAt(3, got).isOk());
        CHECK(got->metadata().getAs<int64_t>(Metadata::FrameNumber) == 3);

        CHECK(r.readFrameAt(0, got).isOk());
        CHECK(got->metadata().getAs<int64_t>(Metadata::FrameNumber) == 0);

        CHECK(r.readFrameAt(4, got).isOk());
        CHECK(got->metadata().getAs<int64_t>(Metadata::FrameNumber) == 4);

        CHECK(r.seek(99) == Error::IllegalSeek);
}

TEST_CASE("DebugMediaFile: linear scan index when footer is missing") {
        // Produce a PMDF, then rewrite the file-flags field to clear
        // HasFooter so buildIndex must fall back to the linear scan.
        String fn = scratchFile("nofooter.pmdf").toString();
        {
                DebugMediaFile w;
                REQUIRE(w.open(fn, DebugMediaFile::Write).isOk());
                for(int i = 0; i < 3; ++i) {
                        CHECK(w.writeFrame(makeTestFrame(i)).isOk());
                }
                CHECK(w.close().isOk());
        }

        // Corrupt the signature flags byte to force the reader to ignore
        // the footer.  The flags live at offset 12 (kSigOffFlags).
        {
                File f(fn);
                REQUIRE(f.open(IODevice::ReadWrite).isOk());
                REQUIRE(f.seek(12).isOk());
                uint8_t zero[4] = {0, 0, 0, 0};
                REQUIRE(f.write(zero, 4) == 4);
                f.close();
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
        CHECK_FALSE(r.hasFooter());
        CHECK(r.frameCount() == 3);

        Frame::Ptr got;
        for(int i = 0; i < 3; ++i) {
                CHECK(r.readFrameAt(i, got).isOk());
                CHECK(got->metadata()
                          .getAs<int64_t>(Metadata::FrameNumber) == i);
        }
}

TEST_CASE("DebugMediaFile: truncated trailing frame is skipped") {
        String fn = scratchFile("truncated.pmdf").toString();

        // Write 5 frames cleanly.
        int64_t truncateAt = 0;
        {
                DebugMediaFile w;
                REQUIRE(w.open(fn, DebugMediaFile::Write).isOk());
                for(int i = 0; i < 5; ++i) {
                        CHECK(w.writeFrame(makeTestFrame(i)).isOk());
                }
                // Note the TOC offset before the footer is written so we
                // know where the last frame ends: the TOC chunk starts
                // immediately after the last FRAM.  Capturing the
                // framesWritten and the last index entry lets us pick a
                // truncation point inside the last FRAM.
                const auto &idx = w.index() ;
                REQUIRE(idx.size() == 5);
                // Midway through the last chunk (add chunk-header +
                // half of the payload — approximate by truncating a
                // handful of bytes past the last chunk's header).
                truncateAt = idx[4].fileOffset + 32;
                CHECK(w.close().isOk());
        }

        // Truncate the file partway through the last FRAM.
        {
                File f(fn);
                REQUIRE(f.open(IODevice::ReadWrite).isOk());
                REQUIRE(f.truncate(truncateAt).isOk());
                // Also clear HasFooter since we invalidated the footer.
                REQUIRE(f.seek(12).isOk());
                uint8_t zero[4] = {0, 0, 0, 0};
                REQUIRE(f.write(zero, 4) == 4);
                f.close();
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
        // Linear scan should recover the first 4 frames.
        CHECK(r.frameCount() == 4);
}

TEST_CASE("DebugMediaFile: preserves MediaPacket on compressed images") {
        String fn = scratchFile("packet.pmdf").toString();

        // Build a compressed "JPEG" image whose plane(0) holds the
        // encoded bytes and whose packet carries pts/dts/flags.
        const char payload[] = "fake encoded bytes";
        constexpr size_t sz = sizeof(payload) - 1;
        auto buf = Buffer::Ptr::create(sz);
        std::memcpy(buf->data(), payload, sz);
        buf->setSize(sz);

        Image img = Image::fromBuffer(buf, 16, 16,
                                      PixelDesc(PixelDesc::JPEG_RGB8_sRGB));
        REQUIRE(img.isValid());
        auto pkt = MediaPacket::Ptr::create(buf, PixelDesc(PixelDesc::JPEG_RGB8_sRGB));
        pkt.modify()->addFlag(MediaPacket::Keyframe);
        img.setPacket(pkt);

        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));

        {
                DebugMediaFile w;
                REQUIRE(w.open(fn, DebugMediaFile::Write).isOk());
                CHECK(w.writeFrame(f).isOk());
                CHECK(w.close().isOk());
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
        Frame::Ptr got;
        CHECK(r.readFrame(got).isOk());
        REQUIRE(got.isValid());
        REQUIRE(got->imageList().size() == 1);
        const Image &gi = *got->imageList()[0];
        CHECK(gi.isCompressed());
        REQUIRE(gi.packet().isValid());
        CHECK(gi.packet()->isKeyframe());
        CHECK(gi.packet()->pixelDesc() == PixelDesc(PixelDesc::JPEG_RGB8_sRGB));
        CHECK(gi.plane(0)->size() == sz);
        CHECK(std::memcmp(gi.plane(0)->data(), payload, sz) == 0);

}

TEST_CASE("DebugMediaFile: auto-stamps session details on write") {
        String fn = scratchFile("session_details.pmdf").toString();
        {
                DebugMediaFile w;
                // No sessionInfo supplied; all details must be auto-stamped.
                REQUIRE(w.open(fn, DebugMediaFile::Write).isOk());
                CHECK(w.writeFrame(makeTestFrame(0)).isOk());
                CHECK(w.close().isOk());
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
        const Metadata &s = r.sessionInfo();

        // MediaIO write defaults.
        CHECK_FALSE(s.getAs<String>(Metadata::Software).isEmpty());
        CHECK_FALSE(s.getAs<String>(Metadata::Date).isEmpty());
        CHECK_FALSE(s.getAs<String>(Metadata::OriginationDateTime).isEmpty());
        CHECK_FALSE(s.getAs<String>(Metadata::Originator).isEmpty());
        CHECK(s.contains(Metadata::OriginatorReference));
        CHECK(s.contains(Metadata::UMID));

        // PMDF-specific session details.
        CHECK_FALSE(s.getAs<String>(Metadata::SessionHostname).isEmpty());
        CHECK(s.getAs<int64_t>(Metadata::SessionProcessId) > 0);
        CHECK_FALSE(s.getAs<String>(Metadata::LibraryBuildInfo).isEmpty());
        CHECK_FALSE(s.getAs<String>(Metadata::LibraryPlatform).isEmpty());
}

TEST_CASE("DebugMediaFile: caller-supplied session keys are preserved over auto-stamp") {
        String fn = scratchFile("session_override.pmdf").toString();
        {
                DebugMediaFile w;
                DebugMediaFile::OpenOptions opts;
                opts.sessionInfo.set(Metadata::Software, String("overridden-app"));
                opts.sessionInfo.set(Metadata::SessionHostname, String("fake-host"));
                REQUIRE(w.open(fn, DebugMediaFile::Write, opts).isOk());
                CHECK(w.close().isOk());
        }

        DebugMediaFile r;
        REQUIRE(r.open(fn, DebugMediaFile::Read).isOk());
        CHECK(r.sessionInfo().getAs<String>(Metadata::Software) == "overridden-app");
        CHECK(r.sessionInfo().getAs<String>(Metadata::SessionHostname) == "fake-host");
        // Keys the caller didn't set are still filled in.
        CHECK_FALSE(r.sessionInfo().getAs<String>(Metadata::LibraryBuildInfo).isEmpty());
}

TEST_CASE("DebugMediaFile: open rejects garbage files") {
        String fn = scratchFile("garbage.pmdf").toString();
        {
                File f(fn);
                REQUIRE(f.open(IODevice::WriteOnly,
                               File::Create | File::Truncate).isOk());
                const char *trash = "not a pmdf file";
                f.write(trash, std::strlen(trash));
                f.close();
        }
        DebugMediaFile r;
        Error e = r.open(fn, DebugMediaFile::Read);
        CHECK(e.isError());
}
