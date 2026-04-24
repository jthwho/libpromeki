/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/frame.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiocodec.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/stringlist.h>
#include <promeki/videoformat.h>
#include <promeki/variantlookup.h>
#include <promeki/mediatimestamp.h>
#include <promeki/duration.h>
#include <promeki/timestamp.h>
#include <promeki/clockdomain.h>

using namespace promeki;

namespace {

// Returns true when @p haystack contains a line that names @p key —
// either as a structural header (@c "Key:" / @c "Key[idx]:") or as a
// scalar in the typed dump format (@c "Key [Type]: value").  Covers
// both shapes in one helper so existing scalar-check call sites
// don't need to care that scalars now carry their type in brackets.
bool containsKeyLine(const StringList &haystack, const String &key) {
        const String headerNeedle = key + String(":");
        const String scalarNeedle = key + String(" [");
        for(const String &line : haystack) {
                if(line.find(headerNeedle) != String::npos) return true;
                if(line.find(scalarNeedle) != String::npos) return true;
        }
        return false;
}

// Returns true when @p haystack contains a line with @p needle anywhere.
// Shared by every dump-content test below.
bool dumpContains(const StringList &lines, const String &needle) {
        for(const String &l : lines) {
                if(l.find(needle) != String::npos) return true;
        }
        return false;
}

} // namespace

// Regression: Frame::dump used to emit "Video[0]:" /
// "Audio[0]:" header lines with no per-payload detail
// underneath.  pmdf-inspect therefore printed what looked like
// "incomplete frames".  The fix walks VariantLookup<VideoPayload> /
// VariantLookup<AudioPayload> and emits every registered scalar —
// this test pins a handful of those scalars so a future regression
// (e.g. silently dropping the subdump again) is caught here rather
// than in pmdf-inspect's output.
TEST_CASE("Frame::dump: video + audio payload subdumps") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_30));

        UncompressedVideoPayload::Ptr vp = UncompressedVideoPayload::allocate(
                ImageDesc(Size2Du32(16, 8), PixelFormat(PixelFormat::RGB8_sRGB)));
        REQUIRE(vp.isValid());
        f.modify()->addPayload(vp);

        AudioDesc adesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        const size_t samples = 32;
        Buffer::Ptr abuf = Buffer::Ptr::create(adesc.bufferSize(samples));
        abuf.modify()->setSize(adesc.bufferSize(samples));
        BufferView aview(abuf, 0, abuf->size());
        PcmAudioPayload::Ptr ap = PcmAudioPayload::Ptr::create(
                adesc, samples, aview);
        REQUIRE(ap.isValid());
        f.modify()->addPayload(ap);

        const StringList lines = f->dump();

        // Header lines still show up.
        CHECK(containsKeyLine(lines, String("Video[0]")));
        CHECK(containsKeyLine(lines, String("Audio[0]")));

        // Video subdump — scalars registered in
        // uncompressedvideopayload.cpp's PROMEKI_LOOKUP_REGISTER block.
        CHECK(containsKeyLine(lines, String("Width")));
        CHECK(containsKeyLine(lines, String("Height")));
        CHECK(containsKeyLine(lines, String("PixelFormat")));
        CHECK(containsKeyLine(lines, String("PlaneCount")));

        // Audio subdump — scalars registered in
        // pcmaudiopayload.cpp's PROMEKI_LOOKUP_REGISTER block.
        // SampleCount is on the concrete leaf and reaches Frame::dump
        // through the virtual variantLookupScalarNames() enumerator.
        CHECK(containsKeyLine(lines, String("SampleRate")));
        CHECK(containsKeyLine(lines, String("Channels")));
        CHECK(containsKeyLine(lines, String("Format")));
        CHECK(containsKeyLine(lines, String("SampleCount")));

        // Common fields from MediaPayload cascade into the concrete
        // leaf's enumerator too — pin a couple to regression-guard
        // the upward inherit path.
        CHECK(containsKeyLine(lines, String("PTS")));
        CHECK(containsKeyLine(lines, String("Kind")));
        CHECK(containsKeyLine(lines, String("PlaneCount")));
}

// ---------------------------------------------------------------
// Helper: construct a small UncompressedVideoPayload
// ---------------------------------------------------------------
namespace {

UncompressedVideoPayload::Ptr makeVideoPayload(uint32_t w = 16, uint32_t h = 8) {
        return UncompressedVideoPayload::allocate(
                ImageDesc(Size2Du32(w, h), PixelFormat(PixelFormat::RGB8_sRGB)));
}

PcmAudioPayload::Ptr makeAudioPayload(size_t samples = 32) {
        AudioDesc adesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        Buffer::Ptr buf = Buffer::Ptr::create(adesc.bufferSize(samples));
        buf.modify()->setSize(adesc.bufferSize(samples));
        BufferView view(buf, 0, buf->size());
        return PcmAudioPayload::Ptr::create(adesc, samples, view);
}

} // namespace

TEST_CASE("Frame: default construction is empty") {
        Frame::Ptr f = Frame::Ptr::create();
        CHECK(f->payloadList().isEmpty());
        CHECK(f->videoPayloads().isEmpty());
        CHECK(f->audioPayloads().isEmpty());
}

TEST_CASE("Frame::addPayload / payloadList: round-trips payloads") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload();
        auto ap = makeAudioPayload();
        REQUIRE(vp.isValid());
        REQUIRE(ap.isValid());

        f.modify()->addPayload(vp);
        f.modify()->addPayload(ap);

        CHECK(f->payloadList().size() == 2u);
        CHECK(f->payloadList()[0].ptr() == vp.ptr());
        CHECK(f->payloadList()[1].ptr() == ap.ptr());
}

TEST_CASE("Frame::videoPayloads: filters only video-kind entries") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp1 = makeVideoPayload(16, 8);
        auto vp2 = makeVideoPayload(32, 16);
        auto ap  = makeAudioPayload();
        REQUIRE(vp1.isValid());
        REQUIRE(vp2.isValid());
        REQUIRE(ap.isValid());

        f.modify()->addPayload(vp1);
        f.modify()->addPayload(ap);
        f.modify()->addPayload(vp2);

        auto vids = f->videoPayloads();
        CHECK(vids.size() == 2u);
        // Order is preserved.
        CHECK(vids[0].ptr() == vp1.ptr());
        CHECK(vids[1].ptr() == vp2.ptr());

        auto auds = f->audioPayloads();
        CHECK(auds.size() == 1u);
        CHECK(auds[0].ptr() == ap.ptr());
}

TEST_CASE("Frame::audioPayloads: empty when no audio present") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->addPayload(makeVideoPayload());
        CHECK(f->audioPayloads().isEmpty());
        CHECK(f->videoPayloads().size() == 1u);
}

TEST_CASE("Frame::videoFormat: valid when frame-rate metadata is set") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_30));
        f.modify()->addPayload(makeVideoPayload(1920, 1080));

        VideoFormat vf = f->videoFormat(0);
        CHECK(vf.isValid());
        CHECK(vf.raster() == Size2Du32(1920, 1080));
        CHECK(vf.frameRate() == FrameRate(FrameRate::FPS_30));
}

TEST_CASE("Frame::videoFormat: out-of-range index returns invalid VideoFormat") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_30));
        f.modify()->addPayload(makeVideoPayload(1920, 1080));

        CHECK_FALSE(f->videoFormat(1).isValid());
        CHECK_FALSE(f->videoFormat(99).isValid());
}

TEST_CASE("Frame::videoFormat: returns invalid when no frame-rate in metadata") {
        Frame::Ptr f = Frame::Ptr::create();
        // No FrameRate set in metadata.
        f.modify()->addPayload(makeVideoPayload(1920, 1080));

        // A missing frame rate makes the VideoFormat invalid.
        CHECK_FALSE(f->videoFormat(0).isValid());
}

// ---------------------------------------------------------------
// VariantLookup cascade + polymorphic dispatch through Frame
// ---------------------------------------------------------------

TEST_CASE("Frame VariantLookup: resolves MediaPayload base scalars via video payload") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload(1920, 1080);
        MediaTimeStamp pts(TimeStamp::now(),
                           ClockDomain(ClockDomain::SystemMonotonic));
        vp.modify()->setPts(pts);
        vp.modify()->setDuration(Duration::fromNanoseconds(40000000));
        vp.modify()->setStreamIndex(7);
        f.modify()->addPayload(vp);

        // PTS / Duration / StreamIndex / Kind live on MediaPayload;
        // reaching them through Video[0] exercises both the
        // Frame.indexedChild lookup and the VariantLookup<VideoPayload>
        // inheritsFrom<MediaPayload> cascade.
        auto ptsVal = VariantLookup<Frame>::resolve(*f, "Video[0].PTS");
        REQUIRE(ptsVal.has_value());
        MediaTimeStamp ptsResolved = ptsVal->get<MediaTimeStamp>();
        CHECK(ptsResolved.isValid());

        auto si = VariantLookup<Frame>::resolve(*f, "Video[0].StreamIndex");
        REQUIRE(si.has_value());
        CHECK(si->get<int32_t>() == 7);

        auto kind = VariantLookup<Frame>::resolve(*f, "Video[0].Kind");
        REQUIRE(kind.has_value());
        CHECK(kind->get<String>() == "Video");
}

TEST_CASE("Frame VariantLookup: resolves VideoPayload intermediate scalars") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload(1920, 1080);
        f.modify()->addPayload(vp);

        auto w = VariantLookup<Frame>::resolve(*f, "Video[0].Width");
        REQUIRE(w.has_value());
        CHECK(w->get<uint32_t>() == 1920u);

        auto pf = VariantLookup<Frame>::resolve(*f, "Video[0].PixelFormat");
        REQUIRE(pf.has_value());
        CHECK(pf->get<PixelFormat>().id() == PixelFormat::RGB8_sRGB);
}

TEST_CASE("Frame VariantLookup: ImageDesc fields are flat on the payload") {
        // ImageDesc fields are re-surfaced directly on VideoPayload
        // rather than hidden behind a Desc.* composition — pipeline
        // queries stay one hop deep.
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload(1280, 720);
        f.modify()->addPayload(vp);

        auto w = VariantLookup<Frame>::resolve(*f, "Video[0].Width");
        REQUIRE(w.has_value());
        CHECK(w->get<uint32_t>() == 1280u);

        auto fpc = VariantLookup<Frame>::resolve(*f, "Video[0].FormatPlaneCount");
        REQUIRE(fpc.has_value());
        CHECK(fpc->get<uint64_t>() == 1u);   // RGB8 packed = one format plane
}

TEST_CASE("Frame VariantLookup: polymorphic dispatch picks up CompressedVideoPayload keys") {
        // A VideoPayload base reference that is actually a
        // CompressedVideoPayload — the concrete leaf's FrameType /
        // IsParameterSet must be reachable through the base lookup
        // because VariantLookup<VideoPayload>::resolve dispatches
        // through variantLookupResolve.
        Frame::Ptr f = Frame::Ptr::create();
        ImageDesc desc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264));
        auto pkt = CompressedVideoPayload::Ptr::create(desc);
        pkt.modify()->setFrameType(FrameType::IDR);
        pkt.modify()->markParameterSet(true);
        f.modify()->addPayload(pkt);

        auto ft = VariantLookup<Frame>::resolve(*f, "Video[0].FrameType");
        REQUIRE(ft.has_value());
        CHECK(ft->get<String>() == "IDR");

        auto ps = VariantLookup<Frame>::resolve(*f, "Video[0].IsParameterSet");
        REQUIRE(ps.has_value());
        CHECK(ps->get<bool>() == true);
}

TEST_CASE("Frame VariantLookup: PcmAudioPayload.SampleCount via Audio[0]") {
        Frame::Ptr f = Frame::Ptr::create();
        auto ap = makeAudioPayload(/*samples=*/128);
        f.modify()->addPayload(ap);

        auto sc = VariantLookup<Frame>::resolve(*f, "Audio[0].SampleCount");
        REQUIRE(sc.has_value());
        CHECK(sc->get<uint64_t>() == 128u);

        // Descriptor-level fields are flat on the payload.
        auto sr = VariantLookup<Frame>::resolve(*f, "Audio[0].SampleRate");
        REQUIRE(sr.has_value());
        CHECK(sr->get<float>() == doctest::Approx(48000.0f));
}

TEST_CASE("Frame VariantLookup: Buffer[N] exposes per-slice details") {
        Frame::Ptr f = Frame::Ptr::create();
        // An uncompressed 16x8 RGB8 payload has a single 384-byte
        // plane backed by one Buffer.  Buffer[0].{Offset,Size,Index}
        // should resolve deterministically.
        auto vp = makeVideoPayload(16, 8);
        f.modify()->addPayload(vp);

        auto off = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[0].Offset");
        REQUIRE(off.has_value());
        CHECK(off->get<uint64_t>() == 0u);

        auto size = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[0].Size");
        REQUIRE(size.has_value());
        CHECK(size->get<uint64_t>() == 16u * 8u * 3u);

        auto idx = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[0].Index");
        REQUIRE(idx.has_value());
        CHECK(idx->get<uint64_t>() == 0u);

        auto valid = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[0].IsValid");
        REQUIRE(valid.has_value());
        CHECK(valid->get<bool>() == true);

        // Out-of-range index cleanly reports OutOfRange.
        Error err;
        auto miss = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[5].Size", &err);
        CHECK_FALSE(miss.has_value());
        CHECK(err == Error::OutOfRange);
}

TEST_CASE("Frame VariantLookup: Buffer[N] sees sliced multi-plane payloads") {
        // Manually compose a three-plane BufferView where slices 0
        // and 1 share one backing buffer (different offsets) and
        // slice 2 lives in a second buffer.  Buffer[N].Index should
        // report 0 / 0 / 1 — the user's primary motivation for
        // exposing bufferIdx.
        Buffer::Ptr sharedBuf = Buffer::Ptr::create(256);
        sharedBuf.modify()->setSize(256);
        Buffer::Ptr secondBuf = Buffer::Ptr::create(128);
        secondBuf.modify()->setSize(128);

        BufferView view;
        view.pushToBack(sharedBuf, 0,   100);
        view.pushToBack(sharedBuf, 100, 50);
        view.pushToBack(secondBuf, 0,   128);

        auto vp = UncompressedVideoPayload::Ptr::create(
                ImageDesc(Size2Du32(16, 8), PixelFormat(PixelFormat::RGB8_sRGB)),
                view);

        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->addPayload(vp);

        for(size_t i = 0; i < 3; ++i) {
                String key = String::sprintf("Video[0].Buffer[%zu].Index", i);
                auto v = VariantLookup<Frame>::resolve(*f, key);
                REQUIRE(v.has_value());
                CHECK(v->get<uint64_t>() == (i < 2 ? 0u : 1u));
        }

        // Sizes round-trip the slice records.
        auto sz0 = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[0].Size");
        auto sz1 = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[1].Size");
        auto sz2 = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[2].Size");
        REQUIRE(sz0.has_value());
        REQUIRE(sz1.has_value());
        REQUIRE(sz2.has_value());
        CHECK(sz0->get<uint64_t>() == 100u);
        CHECK(sz1->get<uint64_t>() == 50u);
        CHECK(sz2->get<uint64_t>() == 128u);

        auto off1 = VariantLookup<Frame>::resolve(*f, "Video[0].Buffer[1].Offset");
        REQUIRE(off1.has_value());
        CHECK(off1->get<uint64_t>() == 100u);
}

TEST_CASE("Frame::dump: Buffer[N] sub-sections appear for every plane") {
        Buffer::Ptr b = Buffer::Ptr::create(64);
        b.modify()->setSize(64);
        BufferView view;
        view.pushToBack(b, 0, 32);
        view.pushToBack(b, 32, 32);

        auto vp = UncompressedVideoPayload::Ptr::create(
                ImageDesc(Size2Du32(4, 4), PixelFormat(PixelFormat::RGB8_sRGB)),
                view);
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->addPayload(vp);

        StringList lines = f->dump();
        CHECK(dumpContains(lines, String("Buffer:")));
        // Both slices' byte sizes should render through the
        // recursive dump of the indexedChildByValue binding.
        int sizeLines = 0;
        for(const String &l : lines) {
                if(l.find("Size [uint64_t]: 32") != String::npos) ++sizeLines;
        }
        CHECK(sizeLines >= 2);
}

TEST_CASE("Frame VariantLookup: Meta on video payload reaches descriptor metadata") {
        // MediaPayload::metadata is virtual; VideoPayload overrides
        // it to forward to desc().metadata, so the Meta.* binding
        // registered on the base cascade-resolves to the
        // descriptor's metadata on a VideoPayload.  Setting via
        // payload.metadata() (the virtual) and reading via the
        // lookup path must land on the same store.
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload(16, 8);
        vp.modify()->metadata().set(Metadata::FrameNumber,
                Variant(FrameNumber(static_cast<int64_t>(42))));
        f.modify()->addPayload(vp);

        auto fn = VariantLookup<Frame>::resolve(*f, "Video[0].Meta.FrameNumber");
        REQUIRE(fn.has_value());
        CHECK(fn->get<FrameNumber>().value() == 42);

        // And the same reference on the descriptor side — virtual
        // metadata() is a single store, not a split.
        CHECK(vp->desc().metadata().getAs<FrameNumber>(Metadata::FrameNumber).value() == 42);
}

// ---------------------------------------------------------------
// Frame::dump — recursive / complete coverage
// ---------------------------------------------------------------

TEST_CASE("Frame::dump: video payload section has single Meta block") {
        Frame::Ptr f = Frame::Ptr::create();
        auto vp = makeVideoPayload(1920, 1080);
        // Both keys land on the same store (desc metadata) — virtual
        // metadata() collapses payload + descriptor into one.
        vp.modify()->metadata().set(Metadata::FrameNumber,
                Variant(FrameNumber(static_cast<int64_t>(7))));
        vp.modify()->desc().metadata().set(Metadata::FrameRate,
                Variant(FrameRate(FrameRate::FPS_30)));
        f.modify()->addPayload(vp);

        StringList lines = f->dump();

        // Flat MediaPayload scalars (cascade).
        CHECK(dumpContains(lines, String("PTS [")));
        CHECK(dumpContains(lines, String("Kind [")));
        CHECK(dumpContains(lines, String("PlaneCount [")));
        // VideoPayload scalars — all the ImageDesc fields are flat.
        CHECK(dumpContains(lines, String("Width [")));
        CHECK(dumpContains(lines, String("PixelFormat [")));
        CHECK(dumpContains(lines, String("FormatPlaneCount [")));

        // One Meta block carrying both keys (FrameNumber was set via
        // payload.metadata(), FrameRate via desc.metadata() — they
        // hit the same store).
        CHECK(dumpContains(lines, String("Meta:")));
        CHECK(dumpContains(lines, String("FrameNumber [")));
        CHECK(dumpContains(lines, String("FrameRate [")));

        // No secondary StreamMeta / Desc headers.
        CHECK_FALSE(dumpContains(lines, String("StreamMeta:")));
        CHECK_FALSE(dumpContains(lines, String("Desc:")));
}

TEST_CASE("Frame::dump: compressed video payload section includes leaf scalars") {
        Frame::Ptr f = Frame::Ptr::create();
        ImageDesc desc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264));
        auto pkt = CompressedVideoPayload::Ptr::create(desc);
        pkt.modify()->setFrameType(FrameType::IDR);
        pkt.modify()->markParameterSet(true);
        f.modify()->addPayload(pkt);

        StringList lines = f->dump();

        // Concrete-leaf fields visible via dump too.
        CHECK(dumpContains(lines, String("FrameType [")));
        CHECK(dumpContains(lines, String("IsParameterSet [")));
        CHECK(dumpContains(lines, String("HasInBandCodecData [")));
}

TEST_CASE("Frame::dump: audio payload section is flat (SampleCount + AudioDesc fields)") {
        Frame::Ptr f = Frame::Ptr::create();
        auto ap = makeAudioPayload(/*samples=*/64);
        ap.modify()->desc().metadata().set(Metadata::FrameRate,
                Variant(FrameRate(FrameRate::FPS_48)));
        f.modify()->addPayload(ap);

        StringList lines = f->dump();

        // Uncompressed-leaf specific.
        CHECK(dumpContains(lines, String("SampleCount [")));
        // AudioDesc fields flat on the payload.
        CHECK(dumpContains(lines, String("SampleRate [")));
        CHECK(dumpContains(lines, String("Channels [")));
        CHECK(dumpContains(lines, String("IsNative [")));

        // Single Meta block — descriptor metadata is the shared
        // store for audio payloads.
        CHECK(dumpContains(lines, String("Meta:")));
        CHECK(dumpContains(lines, String("FrameRate [")));

        CHECK_FALSE(dumpContains(lines, String("StreamMeta:")));
        CHECK_FALSE(dumpContains(lines, String("Desc:")));
}

// -----------------------------------------------------------------
// Non-video / non-audio payload dump coverage.
//
// Defines a synthetic payload whose kind falls outside of Video /
// Audio so Frame::dump's fallback label branch is exercised end-to-
// end.  The payload carries one common scalar (exposed via
// VariantLookup) so the section is non-empty.
// -----------------------------------------------------------------
namespace {

class SyntheticPayload : public MediaPayload {
        public:
                PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH(SyntheticPayload)

                virtual SyntheticPayload *_promeki_clone() const override {
                        return new SyntheticPayload(*this);
                }
                using Ptr = SharedPtr<SyntheticPayload, /*CoW=*/true, SyntheticPayload>;

                SyntheticPayload() = default;

                const MediaPayloadKind &kind() const override {
                        // Subtitle is the only non-Video / non-Audio
                        // value in the enum today; picking it avoids
                        // inventing a new well-known kind.
                        return MediaPayloadKind::Subtitle;
                }
                bool isCompressed() const override { return false; }
                uint32_t subclassFourCC() const override {
                        return 0x53796E74u; // 'Synt'
                }
                void serialisePayload(DataStream &) const override {}
                void deserialisePayload(DataStream &) override {}

                // Non-AV payloads own their own metadata store.
                const Metadata &metadata() const override { return _meta; }
                Metadata &metadata() override { return _meta; }

                String tag() const { return _tag; }
                void setTag(const String &s) { _tag = s; }

        private:
                String   _tag = "hello";
                Metadata _meta;
};

} // namespace

PROMEKI_LOOKUP_REGISTER(SyntheticPayload)
        .inheritsFrom<MediaPayload>()
        .scalar("Tag",
                [](const SyntheticPayload &p) -> std::optional<Variant> {
                        return Variant(p.tag());
                });

TEST_CASE("Frame::dump: non-video / non-audio payload section is detailed") {
        Frame::Ptr f = Frame::Ptr::create();
        auto sp = SyntheticPayload::Ptr::create();
        sp.modify()->setTag("world");
        f.modify()->addPayload(sp);

        StringList lines = f->dump();

        // Header uses the kind name, not a hard-coded Video / Audio label.
        CHECK(dumpContains(lines, String("Subtitle[0]:")));
        // Leaf-specific scalar shows up.
        CHECK(dumpContains(lines, String("Tag [String]: world")));
        // And the cascade still delivers the common MediaPayload
        // scalars — pin a couple.
        CHECK(dumpContains(lines, String("PlaneCount [")));
        CHECK(dumpContains(lines, String("Kind [")));
}

TEST_CASE("Frame::videoFormat: skips audio payloads to find the nth video") {
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_25));
        f.modify()->addPayload(makeAudioPayload());
        f.modify()->addPayload(makeVideoPayload(640, 480));
        f.modify()->addPayload(makeAudioPayload());
        f.modify()->addPayload(makeVideoPayload(1280, 720));

        // index 0 is the 640x480 video, index 1 is 1280x720.
        VideoFormat vf0 = f->videoFormat(0);
        CHECK(vf0.isValid());
        CHECK(vf0.raster() == Size2Du32(640, 480));

        VideoFormat vf1 = f->videoFormat(1);
        CHECK(vf1.isValid());
        CHECK(vf1.raster() == Size2Du32(1280, 720));

        CHECK_FALSE(f->videoFormat(2).isValid());
}
