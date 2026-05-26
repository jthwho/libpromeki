/**
 * @file      audiofile_codecs.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Round-trip tests for the libsndfile-fronted lossy/lossless codecs
 * (FLAC, MP3, Vorbis).  Each codec is gated by its PROMEKI_ENABLE_*
 * flag so the test compiles to a no-op (the section is just absent)
 * when the codec is off.
 *
 * Reader checks lean on the optional @c testmedia/ tree (symlinked in
 * from the testmedia repo).  When the tree is absent the cases skip
 * with a DOCTEST MESSAGE instead of failing, since testmedia is an
 * optional opt-in that not every developer has cloned.
 */

#include <cstdint>
#include <cstdio>

#include <doctest/doctest.h>
#include <promeki/audiodatadecoder.h>
#include <promeki/audiodataencoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audiofile.h>
#include <promeki/audiofilefactory.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/config.h>
#include <promeki/dir.h>
#include <promeki/error.h>
#include <promeki/file.h>
#include <promeki/fileinfo.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/string.h>

using namespace promeki;

namespace {

/// @brief Returns the absolute path to a clip under testmedia/audio/speech-to-text/en-US/.
String testMediaSpeechPath(const char *clip) {
        // PROMEKI_SOURCE_DIR is injected by the test target's
        // target_compile_definitions; we treat the testmedia/ subtree
        // as repo-relative because that's how the symlink lands.
        return String(PROMEKI_SOURCE_DIR) + "/testmedia/audio/speech-to-text/en-US/" + clip;
}

/// @brief Whether @p path is a regular file we can stat.
bool fileExists(const String &path) {
        return FileInfo(path).isFile();
}

/// @brief Open + read a single clip, return its descriptor and total frames.
struct DecodeSummary {
        AudioDesc desc;
        size_t    declaredSampleCount = 0;
        size_t    framesRead = 0;
        bool      ok = false;
};

DecodeSummary decodeWholeClip(const String &path) {
        DecodeSummary s;
        AudioFile     af = AudioFile::createReader(path);
        REQUIRE(af.isValid());
        Error e = af.open();
        REQUIRE_MESSAGE(e.isOk(), "open(", path.cstr(), "): ", e.name().cstr());
        s.desc = af.desc();
        s.declaredSampleCount = af.sampleCount();
        // Pull the file in modest chunks; we don't care about the
        // sample values for the descriptor-level sanity test, only
        // that read() reaches EOF without an error.
        const size_t       kChunk = 8192;
        PcmAudioPayload::Ptr buf;
        for (;;) {
                Error re = af.read(buf, kChunk);
                if (re == Error::EndOfFile) break;
                if (!re.isOk()) {
                        FAIL("read(", path.cstr(), ") at frame ", s.framesRead, " failed: ", re.name().cstr());
                        return s;
                }
                if (!buf) break;
                s.framesRead += buf->sampleCount();
        }
        s.ok = true;
        return s;
}

} // namespace

#if PROMEKI_ENABLE_FLAC
TEST_CASE("AudioFile FLAC: reader opens librispeech clip with expected descriptor") {
        const String path = testMediaSpeechPath("librispeech-260-123286-0001.flac");
        if (!fileExists(path)) {
                MESSAGE("testmedia/ not populated; skipping (", path.cstr(), ")");
                return;
        }
        // Per the sidecar JSON: 16 kHz mono 16-bit PCM, ~3.07 s.
        DecodeSummary s = decodeWholeClip(path);
        REQUIRE(s.ok);
        CHECK(s.desc.sampleRate() == doctest::Approx(16000.0f));
        CHECK(s.desc.channels() == 1u);
        CHECK(s.declaredSampleCount > 0);
        // ~3.07 s × 16 kHz ≈ 49120 samples, allow generous slop.
        CHECK(s.declaredSampleCount > 40000u);
        CHECK(s.declaredSampleCount < 60000u);
        // Streaming read must reach the declared total.
        CHECK(s.framesRead == s.declaredSampleCount);
}

TEST_CASE("AudioFileFactory: flac extension routes to libsndfile") {
        AudioFileFactory::Context ctx;
        ctx.operation = AudioFile::Writer;
        ctx.filename = "test.flac";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "libsndfile");
}
#endif // PROMEKI_ENABLE_FLAC

#if PROMEKI_ENABLE_MP3
TEST_CASE("AudioFile MP3: reader opens commonvoice clip with expected descriptor") {
        const String path = testMediaSpeechPath("commonvoice-us-7.mp3");
        if (!fileExists(path)) {
                MESSAGE("testmedia/ not populated; skipping (", path.cstr(), ")");
                return;
        }
        // Per the sidecar JSON: 48 kHz mono 64 kbps MP3, ~4.08 s.
        DecodeSummary s = decodeWholeClip(path);
        REQUIRE(s.ok);
        CHECK(s.desc.sampleRate() == doctest::Approx(48000.0f));
        CHECK(s.desc.channels() == 1u);
        CHECK(s.declaredSampleCount > 0);
        // ~4.08 s × 48 kHz ≈ 195840 samples, allow generous slop.
        CHECK(s.declaredSampleCount > 160000u);
        CHECK(s.declaredSampleCount < 230000u);
        // MP3 decoded as float (no native-PCM passthrough).
        CHECK(s.desc.format().id() == AudioFormat::PCMI_Float32LE);
}

TEST_CASE("AudioFileFactory: mp3 extension routes to libsndfile") {
        AudioFileFactory::Context ctx;
        ctx.operation = AudioFile::Writer;
        ctx.filename = "test.mp3";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "libsndfile");
}
#endif // PROMEKI_ENABLE_MP3

#if PROMEKI_ENABLE_VORBIS
TEST_CASE("AudioFileFactory: ogg extension routes to libsndfile (Vorbis)") {
        AudioFileFactory::Context ctx;
        ctx.operation = AudioFile::Writer;
        ctx.filename = "test.ogg";
        const AudioFileFactory *f = AudioFileFactory::lookup(ctx);
        REQUIRE(f != nullptr);
        CHECK(f->name() == "libsndfile");
}
#endif // PROMEKI_ENABLE_VORBIS

// ============================================================================
// Round-trip write+read tests
//
// AudioDataEncoder stamps a Manchester-coded 64-bit codeword (sync +
// payload + CRC-8/AUTOSAR) into a single channel of a PCM payload.  Its
// companion AudioDataDecoder recovers the codeword even after lossy
// passes — the integrate-and-compare demodulator was designed to tolerate
// the ±25 % per-bit drift that sample-rate conversion introduces, and
// the same robustness covers MP3 / Vorbis lossy quantisation.  That
// makes the encoder/decoder pair an ideal end-to-end probe for these
// codecs: write a known 64-bit value, run it through encode + libsndfile
// + decode, and the codeword either round-trips intact (Ok + matching
// payload) or doesn't.
// ============================================================================

#if PROMEKI_ENABLE_FLAC || PROMEKI_ENABLE_VORBIS || PROMEKI_ENABLE_MP3
namespace {

/// @brief Encoder samples-per-bit used for codec round-trips.
///
/// At the maximum @c AudioDataEncoder::MaxSamplesPerBit (64), each
/// half-bit spans 32 samples and the Manchester fundamental sits at
/// ~375 Hz at 48 kHz — squarely in the band MP3/Vorbis preserve
/// faithfully.  The minRun threshold the decoder hunts for becomes
/// 32 consecutive same-sign samples, comfortably above the ~17-sample
/// pre-echo wobble runs that Vorbis/MP3 generate in the silence pad
/// around a transient.  Smaller SPB values let the decoder lock onto
/// those pre-echo wobbles as a false sync edge.
constexpr uint32_t kCodecSamplesPerBit = 64;
/// @brief Encoder amplitude used for codec round-trips.
///
/// 0.9 (~ -1 dBFS) is loud enough that MP3/Vorbis psy-models cannot
/// classify the codeword as masked noise; the default 0.1 (-20 dBFS)
/// can fall below the masking threshold relative to its own decoded
/// neighbours.
constexpr float kCodecAmplitude = 0.9f;
/// @brief Encoder lead-in length in bit cells.
///
/// MP3/Vorbis psychoacoustic codecs erode the leading edge of any
/// transient onset by ~6 samples — enough to eat into the
/// @c samplesPerBit/2 sustained-positive run the decoder's sync
/// localiser hunts for.  Prepending @c kLeadInBits bit cells of
/// constant @c +A absorbs that onset transient: by the time the wire
/// crosses into the actual sync nibble, the codec has stabilised and
/// the @c +→- transition at the sync nibble's mid-bit lands cleanly.
/// 4 bit cells is enough headroom for the default lame ABR 128 kbps
/// encoder; smaller values trade safety margin for codeword size.
constexpr uint32_t kCodecLeadInBits = 4;

/// @brief Build a single-channel PCM payload large enough for one
///        AudioDataEncoder codeword, with the codeword already stamped
///        into channel 0.
PcmAudioPayload::Ptr makeStampedPayload(const AudioDesc &desc, uint64_t value) {
        AudioDataEncoder enc(desc, kCodecSamplesPerBit, kCodecAmplitude, kCodecLeadInBits);
        REQUIRE(enc.isValid());
        // Pad both before and after the codeword so the decoder has room
        // to localise the sync edge even after an encoder-delay shift
        // (MP3 in particular can pad ~1152 samples on read).
        const size_t kPadHead = 4096;
        const size_t kPadTail = 4096;
        const size_t samples = kPadHead + enc.packetSamples() + kPadTail;
        const size_t bytes = desc.bufferSize(samples);
        Buffer       buf(bytes);
        // Zero the entire buffer so non-codeword regions are silent.
        buf.fill(static_cast<char>(0));
        buf.setSize(bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto                   pl = PcmAudioPayload::Ptr::create(desc, samples, planes);
        AudioDataEncoder::Item item{kPadHead, enc.packetSamples(), 0, value};
        // The CoW Ptr exposes a const dereference by default; modify()
        // returns the mutable T& we need to stamp the codeword.
        REQUIRE(enc.encode(*pl.modify(), item).isOk());
        return pl;
}

/// @brief Drain an open AudioFile reader into one contiguous payload.
PcmAudioPayload::Ptr drainReader(AudioFile &reader) {
        // Read in one large chunk; sampleCount() reflects the file's
        // declared total.  libsndfile may pad MP3 with encoder delay,
        // so over-allocate by a frame to be safe.
        PcmAudioPayload::Ptr out;
        Error                e = reader.read(out, reader.sampleCount() + 8192);
        REQUIRE(e.isOk());
        REQUIRE(out);
        return out;
}

/// @brief End-to-end round trip: write @p value through libsndfile then
///        recover it via AudioDataDecoder.  Asserts the decoded payload
///        matches @p value and the CRC validated.
void roundTripCodec(const String &filename, const AudioDesc &writeDesc, uint64_t value) {
        // Stamp the codeword into a PCM payload.
        auto pl = makeStampedPayload(writeDesc, value);

        // Write through libsndfile.
        AudioFile writer = AudioFile::createWriter(filename);
        REQUIRE(writer.isValid());
        writer.setDesc(writeDesc);
        REQUIRE_MESSAGE(writer.open().isOk(), "open writer ", filename.cstr());
        REQUIRE(writer.write(*pl).isOk());
        writer.close();

        // Read it back.
        AudioFile reader = AudioFile::createReader(filename);
        REQUIRE(reader.isValid());
        REQUIRE_MESSAGE(reader.open().isOk(), "open reader ", filename.cstr());
        CHECK(reader.desc().sampleRate() == doctest::Approx(writeDesc.sampleRate()));
        CHECK(reader.desc().channels() == writeDesc.channels());
        auto decoded = drainReader(reader);

        // Decode the codeword from the recovered PCM.  Use the
        // read-back descriptor (which may differ in element type, e.g.
        // Float32 for MP3 / Vorbis even when we wrote S16) and pass
        // the encoder's samples-per-bit + amplitude hints so the
        // ±50 % acceptance band is centered on our actual signal and
        // the sustained-positive filter rejects codec pre-echo.
        AudioDataDecoder dec(decoded->desc(), kCodecSamplesPerBit, kCodecAmplitude);
        REQUIRE(dec.isValid());
        AudioDataDecoder::Band band{0, decoded->sampleCount(), 0};
        auto                   item = dec.decode(*decoded, band);
        CHECK_MESSAGE(item.error.isOk(), "decode error: ", item.error.name().cstr());
        CHECK(item.decodedSync == AudioDataEncoder::SyncNibble);
        CHECK(item.payload == value);
        CHECK(item.decodedCrc == item.expectedCrc);

        // Clean up the artifact so successive runs don't accrete files
        // in the temp directory.  Tolerant of missing files (Dir::temp()
        // is usually tmpfs and will sweep on reboot anyway).
        std::remove(filename.cstr());
}

} // namespace
#endif // any-codec

#if PROMEKI_ENABLE_FLAC
TEST_CASE("AudioFile FLAC: AudioDataEncoder codeword round-trips intact (lossless)") {
        const String tmpPath = Dir::temp().path().toString() + "/promeki_flac_roundtrip.flac";
        AudioDesc    desc(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        roundTripCodec(tmpPath, desc, 0x0123456789ABCDEFull);
}
#endif // PROMEKI_ENABLE_FLAC

#if PROMEKI_ENABLE_VORBIS
TEST_CASE("AudioFile Vorbis: AudioDataEncoder codeword round-trips intact (lossy)") {
        const String tmpPath = Dir::temp().path().toString() + "/promeki_vorbis_roundtrip.ogg";
        AudioDesc    desc(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        roundTripCodec(tmpPath, desc, 0xCAFEBABEDEADBEEFull);
}
#endif // PROMEKI_ENABLE_VORBIS

#if PROMEKI_ENABLE_MP3
TEST_CASE("AudioFile MP3: AudioDataEncoder codeword round-trips intact (lossy + lead-in)") {
        const String tmpPath = Dir::temp().path().toString() + "/promeki_mp3_roundtrip.mp3";
        AudioDesc    desc(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        roundTripCodec(tmpPath, desc, 0xFEEDFACEC0FFEE00ull);
}
#endif // PROMEKI_ENABLE_MP3
