/**
 * @file      jpeggeometryprobe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/jpeggeometryprobe.h>
#include <promeki/list.h>
#include <promeki/pixelformat.h>
#include <promeki/string.h>

using namespace promeki;

namespace {

        // Minimal synthetic JFIF byte sequence carrying just a SOF
        // marker.  The probe only inspects markers, so we can skip
        // SOI / DQT / DHT / SOS / EOI altogether and feed a tightly-
        // scoped stream.  Layout for SOF0 / SOF2:
        //
        //   FFC0  Lf(2)  P  Y(2)  X(2)  Nf  [Ci Hi/Vi Tqi]*Nf
        //
        // Lf and P are not consulted by the probe.  We emit the full
        // canonical layout anyway so the test data stays close to a
        // real bitstream.
        Buffer makeSofBuffer(uint8_t marker, uint16_t height, uint16_t width, uint8_t nf, uint8_t firstSf) {
                List<uint8_t> bytes;
                bytes.pushToBack(0xFFu); // SOI start
                bytes.pushToBack(0xD8u);
                bytes.pushToBack(0xFFu);
                bytes.pushToBack(marker);
                const uint16_t lf = static_cast<uint16_t>(8 + nf * 3); // standard SOF length
                bytes.pushToBack(static_cast<uint8_t>(lf >> 8));
                bytes.pushToBack(static_cast<uint8_t>(lf & 0xFFu));
                bytes.pushToBack(8u); // precision
                bytes.pushToBack(static_cast<uint8_t>(height >> 8));
                bytes.pushToBack(static_cast<uint8_t>(height & 0xFFu));
                bytes.pushToBack(static_cast<uint8_t>(width >> 8));
                bytes.pushToBack(static_cast<uint8_t>(width & 0xFFu));
                bytes.pushToBack(nf);
                if (nf >= 1) {
                        bytes.pushToBack(1u);       // Ci
                        bytes.pushToBack(firstSf);  // Hi/Vi
                        bytes.pushToBack(0u);       // Tqi
                }
                for (uint8_t i = 1; i < nf; i++) {
                        bytes.pushToBack(static_cast<uint8_t>(i + 1));
                        bytes.pushToBack(0x11u); // chroma 1x1
                        bytes.pushToBack(1u);
                }
                Buffer out(bytes.size());
                std::memcpy(out.data(), bytes.data(), bytes.size());
                out.setSize(bytes.size());
                return out;
        }

} // namespace

TEST_CASE("JpegGeometryProbe::parseSof: SOF0 4:2:2 dimensions") {
        Buffer                          buf = makeSofBuffer(0xC0u, 1080u, 1920u, 3u, 0x21u);
        JpegGeometryProbe::SofData      sof = JpegGeometryProbe::parseSof(
                static_cast<const uint8_t *>(buf.data()), buf.size());
        CHECK(sof.isValid());
        CHECK(sof.width == 1920u);
        CHECK(sof.height == 1080u);
        CHECK(sof.nf == 3);
        CHECK(sof.ySf == 0x21u);
}

TEST_CASE("JpegGeometryProbe::parseSof: SOF2 progressive 4:2:0") {
        Buffer                          buf = makeSofBuffer(0xC2u, 720u, 1280u, 3u, 0x22u);
        JpegGeometryProbe::SofData      sof = JpegGeometryProbe::parseSof(
                static_cast<const uint8_t *>(buf.data()), buf.size());
        CHECK(sof.isValid());
        CHECK(sof.width == 1280u);
        CHECK(sof.height == 720u);
        CHECK(sof.nf == 3);
        CHECK(sof.ySf == 0x22u);
}

TEST_CASE("JpegGeometryProbe::parseSof: grayscale single-component") {
        Buffer                          buf = makeSofBuffer(0xC0u, 240u, 320u, 1u, 0x11u);
        JpegGeometryProbe::SofData      sof = JpegGeometryProbe::parseSof(
                static_cast<const uint8_t *>(buf.data()), buf.size());
        CHECK(sof.isValid());
        CHECK(sof.width == 320u);
        CHECK(sof.height == 240u);
        CHECK(sof.nf == 1);
}

TEST_CASE("JpegGeometryProbe::parseSof: no SOF marker returns invalid") {
        // Just SOI / EOI — no SOF in the stream.
        const uint8_t bytes[] = { 0xFFu, 0xD8u, 0xFFu, 0xD9u };
        JpegGeometryProbe::SofData sof = JpegGeometryProbe::parseSof(bytes, sizeof(bytes));
        CHECK_FALSE(sof.isValid());
}

TEST_CASE("JpegGeometryProbe::parseSof: empty / null inputs are safe") {
        CHECK_FALSE(JpegGeometryProbe::parseSof(nullptr, 0).isValid());
        const uint8_t one = 0xFFu;
        CHECK_FALSE(JpegGeometryProbe::parseSof(&one, 1).isValid());
}

TEST_CASE("JpegGeometryProbe::probe: 4:2:2 default colorimetry") {
        JpegGeometryProbe probe;
        CHECK_FALSE(probe.hasGeometry());

        Buffer buf = makeSofBuffer(0xC0u, 1080u, 1920u, 3u, 0x21u);
        const JpegGeometryProbe::Result &r = probe.probe(buf, /*rfc2435Type=*/0u, String());
        CHECK(r.valid);
        CHECK(r.size.width() == 1920u);
        CHECK(r.size.height() == 1080u);
        CHECK_FALSE(r.is420);
        CHECK_FALSE(r.isRgb);
        // No colorimetry / range in fmtp → JFIF default (BT.601 full).
        CHECK(r.pixelFormat.id() == PixelFormat::JPEG_YUV8_422_Rec601_Full);
        CHECK(probe.hasGeometry());
        CHECK(probe.lastResult().valid);
        CHECK(probe.lastResult().size.width() == 1920u);
}

TEST_CASE("JpegGeometryProbe::probe: 4:2:0 SOF sampling overrides Type byte") {
        JpegGeometryProbe                probe;
        Buffer                           buf = makeSofBuffer(0xC0u, 720u, 1280u, 3u, 0x22u);
        const JpegGeometryProbe::Result &r = probe.probe(buf, /*rfc2435Type=*/0u, String());
        CHECK(r.valid);
        CHECK(r.is420);
        CHECK_FALSE(r.isRgb);
        CHECK(r.pixelFormat.id() == PixelFormat::JPEG_YUV8_420_Rec601_Full);
}

TEST_CASE("JpegGeometryProbe::probe: RFC 2435 Type 2 selects RGB when sampling is 1x1") {
        JpegGeometryProbe                probe;
        Buffer                           buf = makeSofBuffer(0xC0u, 720u, 1280u, 3u, 0x11u);
        const JpegGeometryProbe::Result &r = probe.probe(buf, /*rfc2435Type=*/2u, String());
        CHECK(r.valid);
        CHECK(r.isRgb);
}

TEST_CASE("JpegGeometryProbe::probe: SDP fmtp colorimetry / range applied") {
        JpegGeometryProbe                probe;
        Buffer                           buf = makeSofBuffer(0xC0u, 1080u, 1920u, 3u, 0x21u);
        const String                     fmtp("colorimetry=BT709-2;RANGE=NARROW");
        const JpegGeometryProbe::Result &r = probe.probe(buf, /*rfc2435Type=*/0u, fmtp);
        CHECK(r.valid);
        CHECK(r.pixelFormat.id() == PixelFormat::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegGeometryProbe::probe: cache hit returns same Result without re-resolving") {
        JpegGeometryProbe probe;
        Buffer            buf = makeSofBuffer(0xC0u, 1080u, 1920u, 3u, 0x21u);
        const JpegGeometryProbe::Result &r1 = probe.probe(buf, 0u, String());
        const JpegGeometryProbe::Result &r2 = probe.probe(buf, 0u, String());
        // Same reference — both calls should hit the same cached
        // _last instance.
        CHECK(&r1 == &r2);
        CHECK(r2.valid);
        CHECK(r2.size.width() == 1920u);
}

TEST_CASE("JpegGeometryProbe::probe: geometry change re-resolves PixelFormat") {
        JpegGeometryProbe probe;
        Buffer            first = makeSofBuffer(0xC0u, 1080u, 1920u, 3u, 0x21u);
        (void)probe.probe(first, 0u, String());
        CHECK(probe.lastResult().size.width() == 1920u);
        CHECK_FALSE(probe.lastResult().is420);

        // New stream geometry — 4:2:0 at 720p.  Same probe instance
        // should detect the geometry change and re-resolve the
        // PixelFormat.
        Buffer                           second = makeSofBuffer(0xC0u, 720u, 1280u, 3u, 0x22u);
        const JpegGeometryProbe::Result &r = probe.probe(second, 0u, String());
        CHECK(r.size.width() == 1280u);
        CHECK(r.size.height() == 720u);
        CHECK(r.is420);
}

TEST_CASE("JpegGeometryProbe::reset: clears geometry state") {
        JpegGeometryProbe probe;
        Buffer            buf = makeSofBuffer(0xC0u, 1080u, 1920u, 3u, 0x21u);
        (void)probe.probe(buf, 0u, String());
        CHECK(probe.hasGeometry());
        probe.reset();
        CHECK_FALSE(probe.hasGeometry());
        CHECK_FALSE(probe.lastResult().valid);
}
