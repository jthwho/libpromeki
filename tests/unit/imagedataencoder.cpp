/**
 * @file      imagedataencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/imagedataencoder.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/crc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/timecode.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

        // ----------------------------------------------------------------------
        // Decoder helpers
        // ----------------------------------------------------------------------
        //
        // The encoder writes adjacent bit cells of equal pixel width: each
        // cell is either the "white" primer or the "black" primer.  To verify
        // the wire format from a unit test, we read the first byte of each
        // cell from a chosen plane line and compare it against a known
        // "white" or "black" reference value to recover the bit pattern.
        //
        // This is more robust than memcmp-against-primers because the encoder
        // writes whole cells of varying byte widths; we only need *one* byte
        // per cell to recover the bit, so a single sentinel value per cell is
        // enough.

        struct DecodedRow {
                        uint8_t  sync; // 4 bits in low nibble
                        uint64_t payload;
                        uint8_t  crc;
        };

        // Read N bits from the line, MSB-first.
        // `whiteByte` is the byte value found at offset 0 of a "white" cell;
        // any cell whose byte differs is treated as "black".
        DecodedRow decodeRow(const uint8_t *line, size_t cellBytes, uint8_t whiteByte) {
                DecodedRow out{0, 0, 0};
                size_t     cellIdx = 0;
                // Sync (4 bits)
                for (int i = 0; i < 4; i++) {
                        bool b = line[cellIdx * cellBytes] == whiteByte;
                        out.sync = static_cast<uint8_t>((out.sync << 1) | (b ? 1u : 0u));
                        cellIdx++;
                }
                // Payload (64 bits)
                for (int i = 0; i < 64; i++) {
                        bool b = line[cellIdx * cellBytes] == whiteByte;
                        out.payload = (out.payload << 1) | (b ? 1u : 0u);
                        cellIdx++;
                }
                // CRC (8 bits)
                for (int i = 0; i < 8; i++) {
                        bool b = line[cellIdx * cellBytes] == whiteByte;
                        out.crc = static_cast<uint8_t>((out.crc << 1) | (b ? 1u : 0u));
                        cellIdx++;
                }
                return out;
        }

        // Build an empty payload of the given format and clear it.
        UncompressedVideoPayload::Ptr makePayload(size_t w, size_t h, PixelFormat::ID id) {
                auto p = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat(id)));
                for (size_t i = 0; i < p->planeCount(); ++i) {
                        std::memset(p.modify()->data()[i].data(), 0, p->plane(i).size());
                }
                return p;
        }

} // namespace

// ============================================================================
// Construction / geometry
// ============================================================================

TEST_CASE("ImageDataEncoder constructs for RGBA8 1920x1080") {
        ImageDesc        desc(1920, 1080, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        // 1920 / 76 = 25 px per cell, alignment 1 → 25.
        CHECK(enc.bitWidth() == 25);
        CHECK(enc.patternWidth() == 25u * 76u);
        CHECK(enc.padWidth() == 1920u - 25u * 76u);
}

TEST_CASE("ImageDataEncoder picks v210-aligned cell width") {
        ImageDesc        desc(1920, 1080, PixelFormat::YUV10_422_v210_Rec709);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        // 1920 / 76 = 25 px raw, rounded down to v210's 6-pixel block → 24.
        CHECK(enc.bitWidth() == 24);
        // 24 * 76 = 1824 px used; 1920 - 1824 = 96 px pad — also a
        // multiple of 6.
        CHECK(enc.patternWidth() == 24u * 76u);
        CHECK(enc.padWidth() == 96u);
}

TEST_CASE("ImageDataEncoder picks 4:2:2-planar-aligned cell width") {
        // P_422_3x8 has chroma hSubsampling = 2, so cell width must be
        // even.  The greedy max is floor(1920/76) = 25 → 24.
        ImageDesc        desc(1920, 1080, PixelFormat::YUV8_422_Planar_Rec709);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        CHECK(enc.bitWidth() == 24);
}

TEST_CASE("ImageDataEncoder fails when image too narrow") {
        // 75 pixels can't even hold a single 76-bit cell at width 1.
        ImageDesc        desc(75, 8, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        CHECK_FALSE(enc.isValid());
}

// ============================================================================
// Encoding round-trip — RGBA8 (simplest case)
// ============================================================================

TEST_CASE("ImageDataEncoder RGBA8 single-item round-trip") {
        ImageDesc        desc(1920, 64, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        auto                   img = makePayload(1920, 64, PixelFormat::RGBA8_sRGB);
        const uint64_t         payload = 0x0123456789ABCDEFull;
        ImageDataEncoder::Item item{0, 16, payload};
        REQUIRE(enc.encode(*img.modify(), item).isOk());

        // For RGBA8_sRGB, "white" is component 0xff, "black" is 0x00.
        const uint8_t *line = img->plane(0).data();
        const size_t   cellBytes = enc.bitWidth() * 4; // 4 bytes per RGBA8 pixel
        DecodedRow     row = decodeRow(line, cellBytes, 0xff);

        CHECK(row.sync == ImageDataEncoder::SyncNibble);
        CHECK(row.payload == payload);

        // Verify CRC matches CRC-8/AUTOSAR over the 8 payload bytes.
        uint8_t bytes[8];
        for (int i = 0; i < 8; i++) bytes[i] = static_cast<uint8_t>((payload >> ((7 - i) * 8)) & 0xffu);
        Crc8 crc(CrcParams::Crc8Autosar);
        crc.update(bytes, 8);
        CHECK(row.crc == crc.value());

        // Lines outside the band must remain untouched (still all-zero).
        const size_t   stride0 = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        const uint8_t *outside = img->plane(0).data() + 32 * stride0;
        bool           allZero = true;
        for (size_t i = 0; i < 16; i++)
                if (outside[i] != 0) {
                        allZero = false;
                        break;
                }
        CHECK(allZero);
}

TEST_CASE("ImageDataEncoder RGBA8 two-item produces distinct rows") {
        ImageDesc        desc(1920, 64, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        auto           img = makePayload(1920, 64, PixelFormat::RGBA8_sRGB);
        const uint64_t pa = 0xAAAAAAAAAAAAAAAAull;
        const uint64_t pb = 0x5555555555555555ull;

        List<ImageDataEncoder::Item> items;
        items.pushToBack({0, 16, pa});
        items.pushToBack({16, 16, pb});
        REQUIRE(enc.encode(*img.modify(), items).isOk());

        const size_t cellBytes = enc.bitWidth() * 4;

        const size_t   stride0 = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        const uint8_t *lineA = img->plane(0).data();
        const uint8_t *lineB = img->plane(0).data() + 16 * stride0;

        DecodedRow rowA = decodeRow(lineA, cellBytes, 0xff);
        DecodedRow rowB = decodeRow(lineB, cellBytes, 0xff);

        CHECK(rowA.payload == pa);
        CHECK(rowB.payload == pb);

        // Both rows have the same sync nibble.
        CHECK(rowA.sync == ImageDataEncoder::SyncNibble);
        CHECK(rowB.sync == ImageDataEncoder::SyncNibble);
}

// ============================================================================
// Encoding for a YCbCr interleaved format (luma plane only — bit 0 of the
// first byte of each YUYV macropixel is the Y0 sample, which is exactly
// what we want to read).
// ============================================================================

TEST_CASE("ImageDataEncoder YUV8_422 round-trip on luma byte 0") {
        // YUYV interleaved layout: byte 0 of each macropixel is Y0.
        // Limited-range Rec.709: Y' white = 235, Y' black = 16.
        ImageDesc        desc(1920, 32, PixelFormat::YUV8_422_Rec709);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        // pixelsPerBlock = 2 (YUYV macropixel) → 1920/76=25 → /2*2 = 24.
        CHECK(enc.bitWidth() == 24);

        auto img = makePayload(1920, 32, PixelFormat::YUV8_422_Rec709);

        const uint64_t payload = 0xDEADBEEFCAFEBABEull;
        REQUIRE(enc.encode(*img.modify(), ImageDataEncoder::Item{0, 16, payload}).isOk());

        const uint8_t *line = img->plane(0).data();
        // Cell stride in bytes: 24 px * (4 bytes / 2 px) = 48 bytes.
        const size_t cellBytes = enc.bitWidth() * 2; // YUYV: 2 bytes/pixel

        // First byte of each cell is Y0 of macropixel 0 — 235 for white,
        // 16 for black.  decodeRow only needs the first byte.
        DecodedRow row = decodeRow(line, cellBytes, 235);
        CHECK(row.sync == ImageDataEncoder::SyncNibble);
        CHECK(row.payload == payload);
}

// ============================================================================
// Multi-plane (planar 4:2:2) — verify chroma planes get neutral and
// luma plane carries the bit pattern.
// ============================================================================

TEST_CASE("ImageDataEncoder planar 4:2:2 luma carries pattern, chroma uniform") {
        ImageDesc        desc(1920, 32, PixelFormat::YUV8_422_Planar_Rec709);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        // hSub = 2 chroma → cell width must be even.
        CHECK((enc.bitWidth() % 2) == 0);

        auto           img = makePayload(1920, 32, PixelFormat::YUV8_422_Planar_Rec709);
        const uint64_t payload = 0x1122334455667788ull;
        REQUIRE(enc.encode(*img.modify(), ImageDataEncoder::Item{0, 16, payload}).isOk());

        // Plane 0 = Y, plane 1 = Cb, plane 2 = Cr.  Within the encoded
        // band the chroma planes must contain a single uniform value
        // (the format's neutral / midpoint), but the *exact* value is
        // whatever the CSC pipeline produces — float-to-integer rounding
        // can land within ±1 of the textbook midpoint of 128 for
        // limited-range YCbCr.  We assert "chroma is uniform across
        // the entire band", which is the property the encoder
        // actually guarantees (the bit pattern must only modulate
        // luma, not chroma).
        const uint8_t *cb = img->plane(1).data();
        const uint8_t *cr = img->plane(2).data();
        const size_t   cbStride = img->desc().pixelFormat().memLayout().lineStride(1, img->desc().width());
        const size_t   crStride = img->desc().pixelFormat().memLayout().lineStride(2, img->desc().width());
        const uint8_t  cbRef = cb[0];
        const uint8_t  crRef = cr[0];
        // Sanity: neutral should be near 128 for limited-range Cb/Cr.
        CHECK(cbRef >= 126);
        CHECK(cbRef <= 130);
        CHECK(crRef >= 126);
        CHECK(crRef <= 130);

        bool cbUniform = true;
        bool crUniform = true;
        for (size_t l = 0; l < 16; l++) {
                for (size_t x = 0; x < 960; x++) {
                        if (cb[l * cbStride + x] != cbRef) {
                                cbUniform = false;
                                break;
                        }
                        if (cr[l * crStride + x] != crRef) {
                                crUniform = false;
                                break;
                        }
                }
                if (!cbUniform || !crUniform) break;
        }
        CHECK(cbUniform);
        CHECK(crUniform);

        // And the Y plane must round-trip the payload.  Use the
        // encoder's actual white-byte value (read from sync bit 0,
        // which is always white).
        const uint8_t *y = img->plane(0).data();
        const uint8_t  whiteY = y[0];
        DecodedRow     row = decodeRow(y, enc.bitWidth(), whiteY);
        CHECK(row.sync == ImageDataEncoder::SyncNibble);
        CHECK(row.payload == payload);
}

// ============================================================================
// v210 round-trip (the most complex packing the encoder must handle)
// ============================================================================

TEST_CASE("ImageDataEncoder v210 single-item round-trip") {
        ImageDesc        desc(1920, 32, PixelFormat::YUV10_422_v210_Rec709);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        CHECK(enc.bitWidth() == 24); // multiple of 6 v210 block

        auto img = makePayload(1920, 32, PixelFormat::YUV10_422_v210_Rec709);

        const uint64_t payload = 0xF00DBABECAFEBEEFull;
        REQUIRE(enc.encode(*img.modify(), ImageDataEncoder::Item{0, 16, payload}).isOk());

        // v210 layout: first 32-bit word of each block is
        // (Cb0:10 | Y0:10 | Cr0:10 | xx:2), little-endian.
        // For "white" Y0 = 940 (limited-range 10-bit Y' = 235*4),
        // for "black" Y0 = 64 (16*4).  We compare bit 10..19 of the
        // first 32-bit LE word of each cell.
        //
        // 24-pixel cell = 4 v210 blocks (6 pixels each) = 64 bytes.
        // We only inspect the first block of each cell.
        const size_t   cellBytes = (enc.bitWidth() / 6) * 16; // 64 bytes
        const uint8_t *line = img->plane(0).data();

        auto y0OfCell = [&](size_t cellIdx) -> uint16_t {
                const uint8_t *p = line + cellIdx * cellBytes;
                uint32_t       word = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
                return static_cast<uint16_t>((word >> 10) & 0x3ffu);
        };

        // Sniff the white-cell luma value from a known white bit
        // position (sync bit 0 = white).  We use whatever the CSC
        // pipeline produced as the reference rather than asserting
        // a specific limited-range value, since float-to-integer
        // rounding inside the matrix can land within ±1 of the
        // textbook 940.
        uint16_t whiteY = y0OfCell(0);
        // Sanity: white should be near the limited-range 10-bit max.
        CHECK(whiteY >= 935);
        CHECK(whiteY <= 945);

        // Now decode all 76 cells using whiteY as the reference.
        DecodedRow row;
        row.sync = 0;
        row.payload = 0;
        row.crc = 0;
        size_t cellIdx = 0;
        for (int i = 0; i < 4; i++) {
                bool b = (y0OfCell(cellIdx) == whiteY);
                row.sync = static_cast<uint8_t>((row.sync << 1) | (b ? 1u : 0u));
                cellIdx++;
        }
        for (int i = 0; i < 64; i++) {
                bool b = (y0OfCell(cellIdx) == whiteY);
                row.payload = (row.payload << 1) | (b ? 1u : 0u);
                cellIdx++;
        }
        for (int i = 0; i < 8; i++) {
                bool b = (y0OfCell(cellIdx) == whiteY);
                row.crc = static_cast<uint8_t>((row.crc << 1) | (b ? 1u : 0u));
                cellIdx++;
        }
        CHECK(row.sync == ImageDataEncoder::SyncNibble);
        CHECK(row.payload == payload);
}

// ============================================================================
// Validation
// ============================================================================

TEST_CASE("ImageDataEncoder rejects out-of-range items") {
        ImageDesc        desc(1920, 32, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        auto img = makePayload(1920, 32, PixelFormat::RGBA8_sRGB);
        // Item runs past the bottom of the image.
        ImageDataEncoder::Item bad{24, 16, 0};
        Error                  err = enc.encode(*img.modify(), bad);
        CHECK(err.isError());
        CHECK(err.code() == Error::OutOfRange);
}

TEST_CASE("ImageDataEncoder rejects mismatched image descriptor") {
        ImageDesc        desc(1920, 32, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        // Build an image of a different size — encode must refuse.
        auto  other = makePayload(1280, 32, PixelFormat::RGBA8_sRGB);
        Error err = enc.encode(*other.modify(), ImageDataEncoder::Item{0, 16, 0});
        CHECK(err.isError());
        CHECK(err.code() == Error::InvalidArgument);
}

// ============================================================================
// End-to-end TPG integration: TPG produces a frame whose top band can be
// decoded back into the same frame number and timecode the TPG advertises.
// ============================================================================

TEST_CASE("ImageDataEncoder end-to-end via TPG MediaIO") {
        // Build a TPG configured with an RGB target so the test's
        // simple "white byte == 255" decoder works without YCbCr math,
        // and a known stream ID so we can match it back.
        const uint32_t  kStreamId = 0xC0FFEEAAu;
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::TimecodeEnabled, true);
        cfg.set(MediaConfig::TimecodeStart, String("01:00:00:00"));
        cfg.set(MediaConfig::TpgDataEncoderEnabled, true);
        cfg.set(MediaConfig::TpgDataEncoderRepeatLines, int32_t(16));
        cfg.set(MediaConfig::StreamID, kStreamId);
        // Burn-in draws on the *bottom* of 1080p frames by default
        // (BottomCenter), and the data encoder writes to the *top* of
        // the frame, so leaving burn-in at its default doesn't
        // collide with the encoded band.

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame.isValid());
        auto vids = frame->videoPayloads();
        REQUIRE(vids.size() == 1);
        const auto *uvp = vids[0]->as<UncompressedVideoPayload>();
        REQUIRE(uvp != nullptr);
        REQUIRE(uvp->desc().size().width() == 1920);
        REQUIRE(uvp->desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);

        // Reconstruct the encoder's geometry parameters so the
        // decoder loop can walk cells at the right pitch.  These must
        // match exactly what ImageDataEncoder computed at construction.
        ImageDataEncoder enc(uvp->desc());
        REQUIRE(enc.isValid());
        const size_t bitWidth = enc.bitWidth();
        const size_t cellBytes = bitWidth * 4; // RGBA8: 4 bytes/pixel
        const size_t lineStride = uvp->desc().pixelFormat().lineStride(0, uvp->desc());

        // Decode the first band (lines 0..15) — frame ID payload.
        const uint8_t *line0 = uvp->plane(0).data();
        DecodedRow     row0 = decodeRow(line0, cellBytes, 0xff);
        CHECK(row0.sync == ImageDataEncoder::SyncNibble);

        const uint64_t frameId = row0.payload;
        const uint32_t decodedStreamId = static_cast<uint32_t>(frameId >> 32);
        const uint32_t decodedFrameNo = static_cast<uint32_t>(frameId & 0xffffffffu);
        CHECK(decodedStreamId == kStreamId);
        CHECK(decodedFrameNo == 0u); // first frame

        // Verify CRC.
        uint8_t bytes[8];
        for (int i = 0; i < 8; i++) bytes[i] = static_cast<uint8_t>((frameId >> ((7 - i) * 8)) & 0xffu);
        Crc8 crc(CrcParams::Crc8Autosar);
        crc.update(bytes, 8);
        CHECK(row0.crc == crc.value());

        // Decode the second band (lines 16..31) — BCD timecode payload.
        const uint8_t *line16 = line0 + 16 * lineStride;
        DecodedRow     row1 = decodeRow(line16, cellBytes, 0xff);
        CHECK(row1.sync == ImageDataEncoder::SyncNibble);

        // TimecodeGenerator::advance() returns the pre-advance value
        // and *then* increments, so the first frame the TPG produces
        // carries the configured start timecode 01:00:00:00 — the
        // *second* frame would carry 01:00:00:01.
        Timecode::Mode ndf30(&VTC_FORMAT_30_NDF);
        auto           tcResult = Timecode::fromBcd64(row1.payload, TimecodePackFormat::Vitc, ndf30);
        REQUIRE(tcResult.second().isOk());
        const Timecode &decodedTc = tcResult.first();
        CHECK(decodedTc.hour() == 1);
        CHECK(decodedTc.min() == 0);
        CHECK(decodedTc.sec() == 0);
        CHECK(decodedTc.frame() == 0);

        io->close();
        delete io;
}
