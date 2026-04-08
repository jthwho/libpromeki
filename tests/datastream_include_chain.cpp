/**
 * @file      datastream_include_chain.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * This file verifies that the DataStream operators exposed by each
 * type's own header work standalone — i.e. including only
 * `<promeki/audiodesc.h>` (for example) is sufficient to serialize
 * an AudioDesc. No direct include of `<promeki/datastream.h>` here
 * beyond what's needed for the BufferIODevice test fixture, so if a
 * transitive include chain breaks, these tests stop compiling.
 */

#include <doctest/doctest.h>

// The BufferIODevice header is required to set up a test fixture. It
// does not pull in DataStream, so its presence does not help the
// include-chain test.
#include <promeki/bufferiodevice.h>

// Each test below includes ONLY the type header it's testing, so any
// missing transitive dependency will surface as a compile error here.

// ----------------------------------------------------------------------
// audiodesc.h chain
// ----------------------------------------------------------------------
#include <promeki/audiodesc.h>

using namespace promeki;

namespace {
constexpr size_t TestBufSize = 1024;
} // namespace

TEST_CASE("include chain: AudioDesc serializes through audiodesc.h alone") {
        Buffer buf(TestBufSize);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        AudioDesc in(AudioDesc::PCMI_S24LE, 48000.0f, 6);
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << in;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                AudioDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.dataType() == AudioDesc::PCMI_S24LE);
                CHECK(out.sampleRate() == 48000.0f);
                CHECK(out.channels() == 6);
        }
}

// ----------------------------------------------------------------------
// imagedesc.h chain
// ----------------------------------------------------------------------
#include <promeki/imagedesc.h>

TEST_CASE("include chain: ImageDesc serializes through imagedesc.h alone") {
        Buffer buf(TestBufSize);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        ImageDesc in(1280, 720, PixelDesc::RGBA8_sRGB);
        in.setLinePad(8);
        in.setInterlaced(false);
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << in;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                ImageDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.width() == 1280);
                CHECK(out.height() == 720);
                CHECK(out.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
                CHECK(out.linePad() == 8);
                CHECK(out.interlaced() == false);
        }
}

// ----------------------------------------------------------------------
// mediadesc.h chain
// ----------------------------------------------------------------------
#include <promeki/mediadesc.h>

TEST_CASE("include chain: MediaDesc serializes through mediadesc.h alone") {
        Buffer buf(TestBufSize);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        MediaDesc in;
        in.setFrameRate(FrameRate(FrameRate::RationalType(25u, 1u)));
        in.imageList().pushToBack(ImageDesc(640, 480, PixelDesc::RGB8_sRGB));
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << in;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                MediaDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.frameRate().numerator() == 25u);
                REQUIRE(out.imageList().size() == 1);
                CHECK(out.imageList()[0].width() == 640);
        }
}

// ----------------------------------------------------------------------
// xyzcolor.h chain
// ----------------------------------------------------------------------
//
// XYZColor's stream operators live inside datastream.h (the circular
// include between colormodel → ciepoint → xyzcolor and datastream.h
// forced this placement), so users must include datastream.h to
// serialize XYZColor. This test documents that contract.
#include <promeki/xyzcolor.h>
#include <promeki/datastream.h>

TEST_CASE("include chain: XYZColor requires datastream.h") {
        Buffer buf(TestBufSize);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        XYZColor in(0.4, 0.5, 0.6);
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << in;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                XYZColor out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.data()[0] == doctest::Approx(0.4));
                CHECK(out.data()[1] == doctest::Approx(0.5));
                CHECK(out.data()[2] == doctest::Approx(0.6));
        }
}
