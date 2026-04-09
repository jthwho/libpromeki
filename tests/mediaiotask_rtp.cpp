/**
 * @file      tests/mediaiotask_rtp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_rtp.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/udpsocket.h>
#include <promeki/socketaddress.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <cstdio>
#include <cstring>

using namespace promeki;

namespace {

/**
 * @brief Builds a tiny filled RGB8 frame with a chosen fill byte.
 *
 * The frame carries one image plane big enough to fit inside a
 * single RTP payload (so the loopback test sees exactly one packet
 * per frame).
 */
Frame::Ptr makeTinyRgbFrame(size_t w, size_t h, uint8_t fill) {
        Image img(w, h, PixelDesc(PixelDesc::RGB8_sRGB));
        img.fill(static_cast<char>(fill));
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        return frame;
}

/**
 * @brief Builds a zeroed 16-bit-LE interleaved audio frame.
 */
Frame::Ptr makePcmAudioFrame(size_t samples, unsigned int channels) {
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, channels);
        Audio audio(desc, samples);
        audio.zero();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        return frame;
}

/** @brief Convenience: binds a loopback UDP receiver and returns its port. */
uint16_t bindReceiver(UdpSocket &rx) {
        rx.open(IODevice::ReadWrite);
        rx.setReceiveTimeout(2000);
        rx.bind(SocketAddress::any(0));
        return rx.localAddress().port();
}

} // namespace

// ============================================================================
// Registry and default config
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_Registry") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "Rtp") {
                        CHECK_FALSE(desc.canRead);
                        CHECK(desc.canWrite);
                        CHECK_FALSE(desc.canReadWrite);
                        CHECK(desc.extensions.isEmpty());
                        found = true;
                        break;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIOTask_Rtp_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "Rtp");
        CHECK(cfg.getAs<SocketAddress>(MediaConfig::RtpLocalAddress) == SocketAddress::any(0));
        // No destination set by default -> no streams active yet.
        CHECK(cfg.getAs<SocketAddress>(MediaConfig::VideoRtpDestination).isNull());
        CHECK(cfg.getAs<SocketAddress>(MediaConfig::AudioRtpDestination).isNull());
        CHECK(cfg.getAs<SocketAddress>(MediaConfig::DataRtpDestination).isNull());
        CHECK(cfg.getAs<int>(MediaConfig::VideoRtpPayloadType) == 96);
        CHECK(cfg.getAs<int>(MediaConfig::VideoRtpClockRate) == 90000);
        Enum pm = cfg.get(MediaConfig::RtpPacingMode).asEnum(RtpPacingMode::Type);
        CHECK(pm == RtpPacingMode::Auto);
}

// ============================================================================
// Mode validation
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_RejectsReaderMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Reader).isError());
        delete io;
}

TEST_CASE("MediaIOTask_Rtp_RejectsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::ReadWrite).isError());
        delete io;
}

// ============================================================================
// Open with no active streams fails
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_NoActiveStreamsFails") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        // Default destinations are all empty — nothing to transmit.
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(16, 16), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setMediaDesc(md);

        CHECK(io->open(MediaIO::Writer).isError());
        delete io;
}

// ============================================================================
// Loopback: raw video goes out as RTP packets with correct header
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_VideoLoopbackSingleFrame") {
        // Receiver bound to a loopback port.
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        // Pick a tiny frame so the payload fits in a single RTP
        // packet even with the RFC 4175 per-line headers.
        const size_t W = 8;
        const size_t H = 4;

        // Configure the RTP sink: video only, destination = our
        // loopback receiver.  Disable KernelFq so the test does not
        // depend on a working fq qdisc.
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::VideoRtpSsrc, 0x01020304);
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // Tell the task what format we'll be sending.
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(W, H), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setMediaDesc(md);

        REQUIRE(io->open(MediaIO::Writer).isOk());

        Frame::Ptr frame = makeTinyRgbFrame(W, H, 0xAB);
        CHECK(io->writeFrame(frame).isOk());

        // Receive packets until we get one.  Tiny frames fragment
        // into one packet for the whole image.
        uint8_t buf[2048];
        ssize_t n = rx.readDatagram(buf, sizeof(buf));
        REQUIRE(n > 12);

        // Verify the RTP header: version 2, SSRC matches, marker set
        // on the last packet of the frame.
        CHECK((buf[0] & 0xC0) == 0x80);
        CHECK(buf[8]  == 0x01);
        CHECK(buf[9]  == 0x02);
        CHECK(buf[10] == 0x03);
        CHECK(buf[11] == 0x04);

        io->close();
        delete io;
}

// ============================================================================
// Loopback: audio PCM_S16LE -> L16 wire bytes
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_AudioLoopbackS16") {
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::AudioRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::AudioRtpSsrc, 0xABCDEF01);
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_S16LE, 48000.0f, 2));
        io->setMediaDesc(md);

        REQUIRE(io->open(MediaIO::Writer).isOk());

        // 1600 stereo samples = one video frame of audio at 48 kHz /
        // 30 fps.  With the default 1ms AES67 packet time that drains
        // the FIFO into 33 complete RTP packets (48 samples each,
        // plus a 16-sample remainder carried to the next frame).
        const size_t kSamples = 1600;
        Frame::Ptr frame = makePcmAudioFrame(kSamples, 2);
        CHECK(io->writeFrame(frame).isOk());

        // First packet of the burst: verify version, SSRC, and the
        // initial RTP timestamp (zero for the very first sample).
        uint8_t buf[2048];
        ssize_t n = rx.readDatagram(buf, sizeof(buf));
        REQUIRE(n > 12);
        CHECK((buf[0] & 0xC0) == 0x80);
        // PT masked (marker bit should be OFF for audio per RFC 3551).
        CHECK((buf[1] & 0x80) == 0);
        // Sequence number = 0 (first packet of the stream).
        CHECK(buf[2] == 0);
        CHECK(buf[3] == 0);
        // Timestamp = 0 (first sample of the stream).
        CHECK(buf[4] == 0);
        CHECK(buf[5] == 0);
        CHECK(buf[6] == 0);
        CHECK(buf[7] == 0);
        // SSRC matches the configured override.
        CHECK(buf[8]  == 0xAB);
        CHECK(buf[9]  == 0xCD);
        CHECK(buf[10] == 0xEF);
        CHECK(buf[11] == 0x01);
        // Payload is 48 samples × 2 channels × 2 bytes = 192 bytes.
        CHECK(n == 12 + 192);

        // Second packet: sequence += 1, timestamp += 48 samples.
        n = rx.readDatagram(buf, sizeof(buf));
        REQUIRE(n > 12);
        CHECK((buf[1] & 0x80) == 0);
        CHECK(buf[3] == 1); // sequence number low byte
        // Timestamp = 48 (big-endian on the wire).
        CHECK(buf[4] == 0);
        CHECK(buf[5] == 0);
        CHECK(buf[6] == 0);
        CHECK(buf[7] == 48);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Rtp_AudioAcceptsFloat32") {
        // The AudioBuffer inside the backend converts on-the-fly, so
        // a Float32LE source (TPG default) should flow into an L16
        // RTP stream without an upstream Converter stage.
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::AudioRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setMediaDesc(md);

        REQUIRE(io->open(MediaIO::Writer).isOk());

        // 1600 Float32 samples → becomes 33 L16 RTP packets on the wire.
        Frame::Ptr frame;
        {
                AudioDesc ad(AudioDesc::PCMI_Float32LE, 48000.0f, 2);
                Audio audio(ad, 1600);
                audio.zero();
                frame = Frame::Ptr::create();
                frame.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        }
        CHECK(io->writeFrame(frame).isOk());

        uint8_t buf[2048];
        ssize_t n = rx.readDatagram(buf, sizeof(buf));
        REQUIRE(n > 12);
        // Payload is still 192 bytes per packet (L16 stereo, 1ms @ 48kHz).
        CHECK(n == 12 + 192);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Rtp_AudioNoDriftAcrossFrames") {
        // Fractional frame rates yield a non-integer number of
        // samples per video frame.  The RTP audio timestamp must
        // still advance by exactly packetSamples per packet across
        // arbitrary numbers of writeFrame calls.  Verify by sending
        // multiple frames of 29.97 audio and inspecting the sequence
        // number and timestamp stride.
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::AudioRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_2997));
        md.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_S16LE, 48000.0f, 2));
        io->setMediaDesc(md);

        REQUIRE(io->open(MediaIO::Writer).isOk());

        // NTSC 48 kHz cadence: 1602, 1601, 1602, 1601, 1602 (sums
        // to 8008 over 5 video frames).  This exercises the
        // AudioBuffer accumulator path because every frame leaves a
        // different remainder in the FIFO.
        const size_t cadence[5] = { 1602, 1601, 1602, 1601, 1602 };
        for(int i = 0; i < 5; i++) {
                Frame::Ptr frame = makePcmAudioFrame(cadence[i], 2);
                CHECK(io->writeFrame(frame).isOk());
        }

        // Collect all packets sent, extract their timestamps, and
        // verify that the stride between consecutive timestamps is
        // exactly packetSamples (48) for every pair.  Also verify
        // sequence numbers increase monotonically by 1.
        uint16_t lastSeq = 0;
        uint32_t lastTs = 0;
        bool     first  = true;
        int      count  = 0;
        for(;;) {
                uint8_t buf[2048];
                rx.setReceiveTimeout(50);
                ssize_t n = rx.readDatagram(buf, sizeof(buf));
                if(n <= 0) break;
                REQUIRE(n >= 12);
                uint16_t seq = (static_cast<uint16_t>(buf[2]) << 8) | buf[3];
                uint32_t ts  = (static_cast<uint32_t>(buf[4]) << 24) |
                               (static_cast<uint32_t>(buf[5]) << 16) |
                               (static_cast<uint32_t>(buf[6]) << 8)  |
                                static_cast<uint32_t>(buf[7]);
                if(first) {
                        CHECK(seq == 0);
                        CHECK(ts == 0);
                        first = false;
                } else {
                        CHECK(static_cast<uint16_t>(lastSeq + 1) == seq);
                        CHECK(ts - lastTs == 48);
                }
                lastSeq = seq;
                lastTs  = ts;
                count++;
        }
        // 5 video frames @ 1602/1601/1602/1601/1602 = 8008 total
        // samples = floor(8008 / 48) = 166 complete packets, with a
        // 40-sample remainder left in the FIFO.
        CHECK(count == 166);

        io->close();
        delete io;
}

// ============================================================================
// Loopback: metadata JSON stream
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_MetadataJsonLoopback") {
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::DataEnabled, true);
        cfg.set(MediaConfig::DataRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::DataRtpSsrc, 0x11223344);
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        // Data-only needs a media descriptor so open() has at least
        // a frame rate to work with.
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        io->setMediaDesc(md);
        REQUIRE(io->open(MediaIO::Writer).isOk());

        // Build a frame with a metadata payload.
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->metadata().set(Metadata::Timecode,
                Timecode());

        CHECK(io->writeFrame(frame).isOk());

        uint8_t buf[2048];
        ssize_t n = rx.readDatagram(buf, sizeof(buf));
        REQUIRE(n > 12);
        // SSRC override applied.
        CHECK(buf[8]  == 0x11);
        CHECK(buf[9]  == 0x22);
        CHECK(buf[10] == 0x33);
        CHECK(buf[11] == 0x44);
        // Payload starts at byte 12 — should look like JSON.
        const char *payload = reinterpret_cast<const char *>(buf + 12);
        // JsonObject::toString(0) produces a compact object starting
        // with '{'.  The metadata contained at least a Timecode key.
        CHECK(payload[0] == '{');

        io->close();
        delete io;
}

// ============================================================================
// SDP export via config file
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_SaveSdpToFile") {
        String sdpPath("/tmp/promeki_test_rtp.sdp");
        // Clean up any stale file from a previous run.
        std::remove(sdpPath.cstr());

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address(239, 1, 2, 3), 5004));
        cfg.set(MediaConfig::RtpSessionName, String("test session"));
        cfg.set(MediaConfig::RtpSaveSdpPath, sdpPath);
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setMediaDesc(md);
        REQUIRE(io->open(MediaIO::Writer).isOk());

        // SDP file exists and is non-empty.
        File f(sdpPath);
        REQUIRE(f.open(IODevice::ReadOnly).isOk());
        Buffer all = f.readAll();
        f.close();
        CHECK(all.size() > 0);

        // SDP must mention our session name and the destination group.
        String sdpText(static_cast<const char *>(all.data()), all.size());
        CHECK(sdpText.contains("test session"));
        CHECK(sdpText.contains("239.1.2.3"));
        CHECK(sdpText.contains("m=video"));
        // rtcp-mux must be present so downstream receivers do not
        // try to bind a separate RTCP socket on RTP port + 1, which
        // would collide when two adjacent ports are in use.
        CHECK(sdpText.contains("a=rtcp-mux"));

        io->close();
        delete io;
        std::remove(sdpPath.cstr());
}

TEST_CASE("MediaIOTask_Rtp_AdjacentPortsNoCollision") {
        // Regression: video on 10000 + audio on 10001 would
        // previously make ffplay fail to bind because without
        // rtcp-mux, ffplay tries to open a video RTCP socket on
        // 10001 that collides with the audio RTP socket on 10001.
        // With a=rtcp-mux emitted per stream, the SDP explicitly
        // tells consumers to share a single port per stream, so
        // the setup is unambiguous.  This test just verifies the
        // SDP text that drives that behaviour.
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address::loopback(), 10000));
        cfg.set(MediaConfig::AudioRtpDestination,
                SocketAddress(Ipv4Address::loopback(), 10001));
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        md.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_S16LE, 48000.0f, 2));
        io->setMediaDesc(md);
        REQUIRE(io->open(MediaIO::Writer).isOk());

        MediaIOParams in;
        MediaIOParams out;
        Error err = io->sendParams("GetSdp", in, &out);
        CHECK(err.isOk());
        String sdp = out.getAs<String>(MediaIOTask_Rtp::ParamSdp);
        // Both streams must carry the rtcp-mux attribute.  A naive
        // count of the substring across the SDP text is enough:
        // there should be one per m= line, so two total for
        // video + audio.
        size_t firstMux = sdp.find("a=rtcp-mux");
        REQUIRE(firstMux != String::npos);
        size_t secondMux = sdp.find("a=rtcp-mux", firstMux + 1);
        CHECK(secondMux != String::npos);
        CHECK(sdp.contains("m=video 10000"));
        CHECK(sdp.contains("m=audio 10001"));

        io->close();
        delete io;
}

// ============================================================================
// SDP export via GetSdp params command
// ============================================================================

TEST_CASE("MediaIOTask_Rtp_GetSdpParams") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address(239, 7, 8, 9), 5010));
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(160, 120), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setMediaDesc(md);
        REQUIRE(io->open(MediaIO::Writer).isOk());

        MediaIOParams in;
        MediaIOParams out;
        Error err = io->sendParams("GetSdp", in, &out);
        CHECK(err.isOk());
        String sdpText = out.getAs<String>(MediaIOTask_Rtp::ParamSdp);
        CHECK(sdpText.contains("m=video"));
        CHECK(sdpText.contains("239.7.8.9"));

        io->close();
        delete io;
}
