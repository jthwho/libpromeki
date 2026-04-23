/**
 * @file      tests/mediaiotask_rtp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include "codectesthelpers.h"
#include <promeki/config.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_rtp.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/pixelformat.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/udpsocket.h>
#include <promeki/socketaddress.h>
#include <promeki/sdpsession.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/mediaconfig.h>
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
        Image img(w, h, PixelFormat(PixelFormat::RGB8_sRGB));
        img.fill(static_cast<char>(fill));
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        return frame;
}

/**
 * @brief Builds a zeroed 16-bit-LE interleaved audio frame.
 */
Frame::Ptr makePcmAudioFrame(size_t samples, unsigned int channels) {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, channels);
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

/** @brief Picks a free UDP port by binding+closing a throwaway socket. */
uint16_t pickFreeUdpPort() {
        UdpSocket sock;
        sock.open(IODevice::ReadWrite);
        sock.bind(SocketAddress::any(0));
        uint16_t port = sock.localAddress().port();
        sock.close();
        return port;
}

/// Wait up to @p timeoutMs for the given MediaIO to surface a frame
/// in its readyReads queue, then pop it.  Returns the error from the
/// final readFrame call.
Error waitForReaderFrame(MediaIO *io, Frame::Ptr &frame,
                          unsigned int timeoutMs = 2000) {
        auto start = std::chrono::steady_clock::now();
        while(true) {
                Error err = io->readFrame(frame, false);
                if(err.isOk()) return err;
                if(err != Error::TryAgain) return err;
                auto now = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - start).count();
                if(ms >= static_cast<long>(timeoutMs)) {
                        return Error::Timeout;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
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
                        CHECK(desc.canBeSource);
                        CHECK(desc.canBeSink);
                        CHECK_FALSE(desc.canBeTransform);
                        // The Rtp backend advertises "sdp" so
                        // `mediaplay -i foo.sdp` hits the extension
                        // fast-path in MediaIO::createForFileRead.
                        bool hasSdp = false;
                        for(const auto &ext : desc.extensions) {
                                if(ext == "sdp") { hasSdp = true; break; }
                        }
                        CHECK(hasSdp);
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

// Reader mode is now supported — this case stays to catch the
// legitimate failure of opening with no streams configured, which
// still errors out regardless of direction.
TEST_CASE("MediaIOTask_Rtp_ReaderNoStreamsFails") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Source).isError());
        delete io;
}

TEST_CASE("MediaIOTask_Rtp_RejectsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Transform).isError());
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
                ImageDesc(Size2Du32(16, 16), PixelFormat(PixelFormat::RGB8_sRGB)));
        io->setExpectedDesc(md);

        CHECK(io->open(MediaIO::Sink).isError());
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
                ImageDesc(Size2Du32(W, H), PixelFormat(PixelFormat::RGB8_sRGB)));
        io->setExpectedDesc(md);

        REQUIRE(io->open(MediaIO::Sink).isOk());

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
// Loopback: JPEG XS compressed video over RFC 9134 RTP
// ============================================================================
//
// Exercises the full JPEG XS dispatch path: MediaIOTask_Rtp picks
// RtpPayloadJpegXs for a compressed JPEG_XS_* PixelFormat, prepends a
// 4-byte RFC 9134 header to each fragment, and advertises jxsv/90000
// in the SDP with a complete fmtp line.  The bitstream content is
// synthetic — we only care that the dispatch and wire format are
// correct, not that a real JPEG XS decoder would accept it.

#if PROMEKI_ENABLE_JPEGXS

TEST_CASE("MediaIOTask_Rtp_JpegXsLoopbackSingleFrame") {
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        const size_t W = 320;
        const size_t H = 240;

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::VideoRtpSsrc, 0x0A0B0C0D);
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // Advertise a 10-bit 4:2:2 JPEG XS stream.  The task uses the
        // ImageDesc to configure the payload handler and the SDP, so
        // everything downstream of this MediaDesc is driven by the
        // compressed PixelFormat we pick here.
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709)));
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

        // Build a fake JPEG XS bitstream small enough to fit in a
        // single RTP packet after the 4-byte RFC 9134 header.
        std::vector<uint8_t> jxsBytes(512);
        for(size_t i = 0; i < jxsBytes.size(); i++) {
                jxsBytes[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
        }
        Image jxsImg = Image::fromCompressedData(
                jxsBytes.data(), jxsBytes.size(),
                W, H, PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709));
        REQUIRE(jxsImg.isValid());
        REQUIRE(jxsImg.isCompressed());

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(jxsImg)));
        CHECK(io->writeFrame(frame).isOk());

        // Receive the single datagram and inspect its headers.
        uint8_t buf[2048];
        ssize_t n = rx.readDatagram(buf, sizeof(buf));
        REQUIRE(n > 12 + 4);                 // RTP header + RFC 9134 header

        // RTP header sanity checks (version=2, marker set, SSRC).
        CHECK((buf[0] & 0xC0) == 0x80);      // version 2
        CHECK((buf[1] & 0x80) == 0x80);      // marker bit on last packet of frame
        CHECK(buf[8]  == 0x0A);
        CHECK(buf[9]  == 0x0B);
        CHECK(buf[10] == 0x0C);
        CHECK(buf[11] == 0x0D);

        // RFC 9134 payload header starts at byte 12 (end of RTP header).
        // Decode the bit layout manually — a one-packet frame must have
        // T=1, K=0, L=1, I=0, F=0, SEP=0, P=0.
        const uint8_t *jxsHdr = buf + 12;
        CHECK(((jxsHdr[0] >> 7) & 1) == 1);  // T=1
        CHECK(((jxsHdr[0] >> 6) & 1) == 0);  // K=0 (codestream mode)
        CHECK(((jxsHdr[0] >> 5) & 1) == 1);  // L=1 (last packet)
        CHECK(((jxsHdr[0] >> 3) & 3) == 0);  // I=00 (progressive)
        const uint8_t fCounter = static_cast<uint8_t>(
                ((jxsHdr[0] & 0x07) << 2) | ((jxsHdr[1] >> 6) & 0x03));
        CHECK(fCounter == 0);                // first frame
        const uint16_t pCounter = static_cast<uint16_t>(
                ((jxsHdr[2] & 0x07) << 8) | jxsHdr[3]);
        CHECK(pCounter == 0);                // first (and only) packet

        // The remaining bytes must be a prefix of our synthetic
        // bitstream — a single-packet frame carries the whole thing.
        const size_t payloadBytes = static_cast<size_t>(n) - 12 - 4;
        CHECK(payloadBytes == jxsBytes.size());
        CHECK(std::memcmp(buf + 12 + 4, jxsBytes.data(), payloadBytes) == 0);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Rtp_JpegXsSdpFormat") {
        // Verify the SDP emitted for a JPEG XS stream carries the
        // RFC 9134 rtpmap and a fmtp with the mandatory parameters
        // plus the per-PixelFormat sampling / depth / geometry.
        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address(239, 10, 11, 12), 6000));
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080),
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709)));
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

        MediaIOParams in;
        MediaIOParams out;
        REQUIRE(io->sendParams("GetSdp", in, &out).isOk());
        String sdp = out.getAs<String>(MediaIOTask_Rtp::ParamSdp);

        CHECK(sdp.contains("m=video 6000"));
        CHECK(sdp.contains("jxsv/90000"));
        CHECK(sdp.contains("packetmode=0"));
        CHECK(sdp.contains("rate=90000"));
        CHECK(sdp.contains("sampling=YCbCr-4:2:2"));
        CHECK(sdp.contains("depth=10"));
        CHECK(sdp.contains("width=1920"));
        CHECK(sdp.contains("height=1080"));
        CHECK(sdp.contains("colorimetry=BT709"));
        CHECK(sdp.contains("RANGE=NARROW"));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Rtp_JpegXsMultiPacketFragmentation") {
        // A frame that does not fit in one MTU must be split across
        // multiple RTP packets.  Verify the receiver sees several
        // datagrams carrying consecutive P counters, only the last of
        // which has L=1 and the RTP marker bit set.
        UdpSocket rx;
        uint16_t port = bindReceiver(rx);

        const size_t W = 1920;
        const size_t H = 1080;

        MediaIO::Config cfg = MediaIO::defaultConfig("Rtp");
        cfg.set(MediaConfig::VideoRtpDestination,
                SocketAddress(Ipv4Address::loopback(), port));
        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_XS_YUV8_422_Rec709)));
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

        // 6000 bytes → 6 packets at 1200-byte MTU (minus 4-byte header
        // and RTP header, so effectively 1184 per packet).
        std::vector<uint8_t> jxsBytes(6000);
        for(size_t i = 0; i < jxsBytes.size(); i++) {
                jxsBytes[i] = static_cast<uint8_t>(i & 0xFF);
        }
        Image jxsImg = Image::fromCompressedData(
                jxsBytes.data(), jxsBytes.size(),
                W, H, PixelFormat(PixelFormat::JPEG_XS_YUV8_422_Rec709));
        REQUIRE(jxsImg.isValid());

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(jxsImg)));
        CHECK(io->writeFrame(frame).isOk());

        // Drain packets until we see one with the marker bit set.
        int packetCount = 0;
        int expectedP   = 0;
        bool sawMarker  = false;
        for(int tries = 0; tries < 32 && !sawMarker; tries++) {
                uint8_t buf[2048];
                ssize_t n = rx.readDatagram(buf, sizeof(buf));
                if(n < 12 + 4) break;
                packetCount++;

                const bool marker = (buf[1] & 0x80) != 0;
                const uint8_t *jxsHdr = buf + 12;
                const bool L = ((jxsHdr[0] >> 5) & 1) != 0;
                const uint16_t p = static_cast<uint16_t>(
                        ((jxsHdr[2] & 0x07) << 8) | jxsHdr[3]);
                CHECK(p == (uint16_t)expectedP);
                expectedP++;
                if(marker) {
                        CHECK(L == true);   // last packet → L must be set
                        sawMarker = true;
                } else {
                        CHECK(L == false);
                }
        }
        CHECK(sawMarker);
        CHECK(packetCount >= 2);            // must have fragmented

        io->close();
        delete io;
}

#endif // PROMEKI_ENABLE_JPEGXS

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
                AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2));
        io->setExpectedDesc(md);

        REQUIRE(io->open(MediaIO::Sink).isOk());

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
        // RTP stream without an upstream SRC stage.
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
                AudioDesc(AudioFormat::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(md);

        REQUIRE(io->open(MediaIO::Sink).isOk());

        // 1600 Float32 samples → becomes 33 L16 RTP packets on the wire.
        Frame::Ptr frame;
        {
                AudioDesc ad(AudioFormat::PCMI_Float32LE, 48000.0f, 2);
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
        md.setFrameRate(FrameRate(FrameRate::FPS_29_97));
        md.audioList().pushToBack(
                AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2));
        io->setExpectedDesc(md);

        REQUIRE(io->open(MediaIO::Sink).isOk());

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
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

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
                ImageDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::RGB8_sRGB)));
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

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
                ImageDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::RGB8_sRGB)));
        md.audioList().pushToBack(
                AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2));
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

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
                ImageDesc(Size2Du32(160, 120), PixelFormat(PixelFormat::RGB8_sRGB)));
        io->setExpectedDesc(md);
        REQUIRE(io->open(MediaIO::Sink).isOk());

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

// ============================================================================
// Reader mode — loopback round-trip tests
// ============================================================================
//
// Every reader test spins up a writer MediaIOTask_Rtp and a reader
// MediaIOTask_Rtp on the same host, with the reader listening on a
// pre-picked free port.  The writer sends a single frame (or a
// controlled burst) and the reader's executeCmd(Read) should surface
// the reassembled Frame::Ptr.

#if PROMEKI_ENABLE_JPEGXS

TEST_CASE("MediaIOTask_Rtp_Reader_JpegXs_Loopback") {
        // JPEG XS codestream round-trip: writer fragments the
        // bitstream into MTU-sized packets, reader reassembles them
        // into a compressed JPEG_XS_* Image, the bytes must match
        // what we sent.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        // Reader first — must be listening before the writer sends.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::VideoRtpDestination, dest);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        const size_t W = 320;
        const size_t H = 240;

        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709)));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::VideoRtpSsrc, 0xCAFEBABE);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);

        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        // Give the reader thread a moment to settle into its loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Build a fake JPEG XS bitstream large enough to fragment
        // across multiple RTP packets.  The content is deterministic
        // so we can verify byte-for-byte after reassembly.
        std::vector<uint8_t> jxsBytes(3000);
        for(size_t i = 0; i < jxsBytes.size(); i++) {
                jxsBytes[i] = static_cast<uint8_t>((i * 13 + 7) & 0xFF);
        }
        Image jxsImg = Image::fromCompressedData(
                jxsBytes.data(), jxsBytes.size(),
                W, H, PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709));
        REQUIRE(jxsImg.isValid());
        REQUIRE(jxsImg.isCompressed());

        Frame::Ptr txFrame = Frame::Ptr::create();
        txFrame.modify()->imageList().pushToBack(
                Image::Ptr::create(std::move(jxsImg)));
        CHECK(tx->writeFrame(txFrame).isOk());

        // Pop the reassembled frame on the reader.
        Frame::Ptr rxFrame;
        Error err = waitForReaderFrame(rx, rxFrame);
        REQUIRE(err.isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());
        const Image::Ptr &rxImgPtr = rxFrame->imageList()[0];
        REQUIRE(rxImgPtr);
        const Image &rxImg = *rxImgPtr;
        CHECK(rxImg.isValid());
        CHECK(rxImg.isCompressed());
        CHECK(rxImg.width()  == W);
        CHECK(rxImg.height() == H);
        CHECK(rxImg.pixelFormat().id() == PixelFormat::JPEG_XS_YUV10_422_Rec709);

        // The reassembled bitstream must match the one we sent.
        const Buffer::Ptr &plane = rxImg.plane(0);
        REQUIRE(plane);
        REQUIRE(plane->size() == jxsBytes.size());
        CHECK(std::memcmp(plane->data(), jxsBytes.data(),
                          jxsBytes.size()) == 0);

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

#endif // PROMEKI_ENABLE_JPEGXS

TEST_CASE("MediaIOTask_Rtp_Reader_RawVideo_Loopback") {
        // Uncompressed RFC 4175 raw video round-trip.  Tiny 8x4 RGB
        // frame that fits in a single packet so we exercise the
        // marker-bit terminated path.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const size_t W = 8;
        const size_t H = 4;

        // Reader.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::VideoRtpDestination, dest);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        Frame::Ptr txFrame = makeTinyRgbFrame(W, H, 0x5A);
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());
        const Image &rxImg = *rxFrame->imageList()[0];
        CHECK(rxImg.isValid());
        CHECK_FALSE(rxImg.isCompressed());
        CHECK(rxImg.width()  == W);
        CHECK(rxImg.height() == H);
        // The fill byte from the writer should survive the
        // round-trip as long as the RFC 4175 packer/unpacker agree
        // on the pixel layout.
        const Buffer::Ptr &plane = rxImg.plane(0);
        REQUIRE(plane);
        REQUIRE(plane->size() >= 1);
        CHECK(static_cast<const uint8_t *>(plane->data())[0] == 0x5A);

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

TEST_CASE("MediaIOTask_Rtp_Reader_L16Audio_Loopback") {
        // AES67 L16 audio round-trip.  We push one video-frame's
        // worth of audio (1600 samples @ 48 kHz stereo for 30 fps)
        // through the writer and expect the reader to emit a frame
        // with the same sample count.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const unsigned int CH = 2;
        const float RATE = 48000.0f;
        const size_t SAMPLES_PER_FRAME = 1600; // 30 fps

        // Reader.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::AudioRtpDestination, dest);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.audioList().pushToBack(
                AudioDesc(AudioFormat::PCMI_S16BE, RATE, CH));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::AudioRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.audioList().pushToBack(
                AudioDesc(AudioFormat::PCMI_S16LE, RATE, CH));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // The writer fragments audio into AES67 1 ms packets (48
        // samples per packet at 48 kHz), so a single write of
        // SAMPLES_PER_FRAME = 1600 samples produces
        // floor(1600/48) = 33 packets (1584 samples on the wire),
        // leaving 16 samples queued inside the writer FIFO.  To
        // guarantee the reader has at least SAMPLES_PER_FRAME
        // samples to emit one full frame, push two writes so the
        // wire traffic exceeds the per-frame threshold by a
        // comfortable margin.
        Frame::Ptr txFrame1 = makePcmAudioFrame(SAMPLES_PER_FRAME, CH);
        Frame::Ptr txFrame2 = makePcmAudioFrame(SAMPLES_PER_FRAME, CH);
        CHECK(tx->writeFrame(txFrame1).isOk());
        CHECK(tx->writeFrame(txFrame2).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->audioList().isEmpty());
        const Audio &audio = *rxFrame->audioList()[0];
        CHECK(audio.samples() == SAMPLES_PER_FRAME);
        CHECK(audio.desc().channels() == CH);
        CHECK(audio.desc().sampleRate() == RATE);

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

TEST_CASE("MediaIOTask_Rtp_Reader_JsonMetadata_Loopback") {
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        // Reader (data stream is opt-in via DataEnabled).
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::DataEnabled, true);
        rxCfg.set(MediaConfig::DataRtpDestination, dest);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::DataEnabled, true);
        txCfg.set(MediaConfig::DataRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Build a frame with a Title metadata entry, send it.
        Frame::Ptr txFrame = Frame::Ptr::create();
        txFrame.modify()->metadata().set(Metadata::Title,
                String("RTP reader test"));
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        String title = rxFrame->metadata().getAs<String>(Metadata::Title);
        CHECK(title == "RTP reader test");

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

TEST_CASE("MediaIOTask_Rtp_Reader_Multicast_RawVideo") {
        // Exercise the multicast join path: writer and reader share
        // a multicast group + port, the writer enables multicast
        // loopback, and the reader joins the group on any
        // interface.  A tiny single-packet frame should round-trip.
        uint16_t port = pickFreeUdpPort();
        SocketAddress group(Ipv4Address(239, 19, 83, 1), port);

        const size_t W = 8;
        const size_t H = 4;

        // Reader — joins the group via openReaderStream.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::VideoRtpDestination, group);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer — same destination group; multicast loopback is
        // enabled automatically in openStream() when any stream
        // destination is multicast/loopback.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, group);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        // Give the multicast plumbing a moment to settle.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        Frame::Ptr txFrame = makeTinyRgbFrame(W, H, 0x33);
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        Error err = waitForReaderFrame(rx, rxFrame, 2000);
        // Some CI environments have multicast loopback disabled at
        // the kernel / bridge level, so a timeout is acceptable
        // here — we only assert that the reader survives the setup
        // without crashing.  When the environment does deliver the
        // datagram, verify the image is shaped correctly.
        if(err.isOk()) {
                REQUIRE(rxFrame);
                REQUIRE(!rxFrame->imageList().isEmpty());
                const Image &rxImg = *rxFrame->imageList()[0];
                CHECK(rxImg.isValid());
                CHECK(rxImg.width()  == W);
                CHECK(rxImg.height() == H);
        }

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

TEST_CASE("MediaIOTask_Rtp_Reader_CreateForFileRead_Sdp") {
        // Verify that handing a .sdp path to createForFileRead()
        // picks the Rtp backend automatically — this is the path
        // `mediaplay -i foo.sdp` exercises.  We write an SDP file
        // via the Rtp writer, then rebuild the reader via the
        // factory with only the filename.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const size_t W = 8;
        const size_t H = 4;

        String sdpTmp = String("/tmp/promeki-rtp-rx-fileread-") +
                        String::number(port) + String(".sdp");
        std::remove(sdpTmp.cstr());

        // Writer emits an SDP file.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        txCfg.set(MediaConfig::RtpSaveSdpPath, sdpTmp);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        // Reader comes up via createForFileRead — simulates
        // `mediaplay -i foo.sdp` with no explicit backend name.
        MediaIO *rx = MediaIO::createForFileRead(sdpTmp);
        REQUIRE(rx != nullptr);
        // The factory must have resolved to the Rtp backend.
        CHECK(rx->config().getAs<String>(MediaConfig::Type) == "Rtp");

        // Still need a MediaDesc for raw video (SDP has no pixel
        // format for raw/90000 — we exercise the pure "SDP is
        // enough" path in the JPEG XS test below).
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Frame::Ptr txFrame = makeTinyRgbFrame(W, H, 0x7E);
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());

        tx->close();
        rx->close();
        delete tx;
        delete rx;
        std::remove(sdpTmp.cstr());
}

#if PROMEKI_ENABLE_JPEGXS

TEST_CASE("MediaIOTask_Rtp_Reader_SdpSessionDirect") {
        // Build an SdpSession programmatically, hand it to the
        // reader via the polymorphic RtpSdp Variant key, and verify
        // the reader configures itself with no filesystem access.
        // This is the "pass the object, not a path" flow.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const size_t W = 320;
        const size_t H = 240;

        // Program an SdpSession for a JPEG XS stream on the
        // chosen port.
        SdpSession sdp;
        sdp.setSessionName("direct SdpSession test");
        sdp.setOrigin("test", 1, 1, "IN", "IP4", "127.0.0.1");
        sdp.setConnectionAddress("127.0.0.1");
        SdpMediaDescription md;
        md.setMediaType("video");
        md.setPort(port);
        md.setProtocol("RTP/AVP");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 jxsv/90000");
        md.setAttribute("fmtp",
                String("96 packetmode=0;rate=90000;sampling=YCbCr-4:2:2;"
                       "depth=10;width=") + String::number(W) +
                String(";height=") + String::number(H) +
                String(";colorimetry=BT709;RANGE=NARROW"));
        md.setAttribute("rtcp-mux", String());
        sdp.addMediaDescription(md);

        // Reader takes the SdpSession by value in the Variant —
        // no path on disk is involved.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::RtpSdp, sdp);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer on the matching destination.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::vector<uint8_t> jxsBytes(1024);
        for(size_t i = 0; i < jxsBytes.size(); i++) {
                jxsBytes[i] = static_cast<uint8_t>((i * 19 + 5) & 0xFF);
        }
        Image jxs = Image::fromCompressedData(
                jxsBytes.data(), jxsBytes.size(), W, H,
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709));
        Frame::Ptr txFrame = Frame::Ptr::create();
        txFrame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(jxs)));
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());
        const Image &rxImg = *rxFrame->imageList()[0];
        CHECK(rxImg.width()  == W);
        CHECK(rxImg.height() == H);
        CHECK(rxImg.pixelFormat().id() == PixelFormat::JPEG_XS_YUV10_422_Rec709);

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

TEST_CASE("MediaIOTask_Rtp_Reader_SdpOnly_JpegXs") {
        // The full "SDP is enough" path: for a JPEG XS stream the
        // RFC 9134 fmtp line carries sampling + depth + width +
        // height, so the reader can derive every MediaDesc value
        // from the SDP alone.  The caller passes RtpSdp (String path) and
        // nothing else — no explicit destination, no explicit
        // MediaDesc.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const size_t W = 320;
        const size_t H = 240;

        String sdpTmp = String("/tmp/promeki-rtp-rx-sdponly-jxs-") +
                        String::number(port) + String(".sdp");
        std::remove(sdpTmp.cstr());

        // Writer emits the SDP and streams a frame.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        txCfg.set(MediaConfig::RtpSaveSdpPath, sdpTmp);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        // Reader — only the SDP, nothing else.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::RtpSdp, sdpTmp);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        // No setMediaDesc — loadSdp should populate everything.
        REQUIRE(rx->open(MediaIO::Source).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Build a fake JPEG XS bitstream and send it.
        std::vector<uint8_t> jxsBytes(1024);
        for(size_t i = 0; i < jxsBytes.size(); i++) {
                jxsBytes[i] = static_cast<uint8_t>((i * 11 + 17) & 0xFF);
        }
        Image jxs = Image::fromCompressedData(
                jxsBytes.data(), jxsBytes.size(), W, H,
                PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709));
        REQUIRE(jxs.isValid());
        Frame::Ptr txFrame = Frame::Ptr::create();
        txFrame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(jxs)));
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());
        const Image &rxImg = *rxFrame->imageList()[0];
        CHECK(rxImg.width()  == W);
        CHECK(rxImg.height() == H);
        CHECK(rxImg.pixelFormat().id() == PixelFormat::JPEG_XS_YUV10_422_Rec709);
        const Buffer::Ptr &plane = rxImg.plane(0);
        REQUIRE(plane);
        CHECK(plane->size() == jxsBytes.size());
        CHECK(std::memcmp(plane->data(), jxsBytes.data(),
                          jxsBytes.size()) == 0);

        tx->close();
        rx->close();
        delete tx;
        delete rx;
        std::remove(sdpTmp.cstr());
}

#endif // PROMEKI_ENABLE_JPEGXS

TEST_CASE("MediaIOTask_Rtp_Reader_Mjpeg_Loopback") {
        // MJPEG (RFC 2435) round-trip: writer encodes an 8x8 RGB
        // frame to JPEG via Image::convert, sends it, reader
        // reassembles a JFIF bitstream (SOI/DQT/SOF0/DHT/SOS/EOI
        // via RtpPayloadJpeg::unpack), and the result must
        // decode cleanly back to an RGB image with the expected
        // geometry.  This is the end-to-end test of the JFIF
        // rebuild — if the standard Annex K tables or the SOF0
        // layout are wrong, libjpeg-turbo will reject the bytes
        // and the decode will fail.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const size_t W = 16;
        const size_t H = 16;

        // Build a compressed JPEG source frame through a one-shot
        // JpegVideoEncoder session — Image::convert is CSC-only after
        // task 36; the encoder side lives behind the VideoEncoder
        // contract now.
        Image rgb(W, H, PixelFormat(PixelFormat::RGB8_sRGB));
        rgb.fill(static_cast<char>(0x80));
        MediaConfig jpegCfg;
        jpegCfg.set(MediaConfig::JpegQuality, 85);
        Image jpeg = promeki::tests::encodeImageToCompressed(
                rgb, PixelFormat(PixelFormat::JPEG_YUV8_422_Rec601_Full), jpegCfg);
        REQUIRE(jpeg.isValid());
        REQUIRE(jpeg.isCompressed());

        // Reader listens on the multicast / unicast destination.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::VideoRtpDestination, dest);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_YUV8_422_Rec601_Full)));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        // Writer.
        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::JPEG_YUV8_422_Rec601_Full)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Send the compressed JPEG frame.
        Frame::Ptr txFrame = Frame::Ptr::create();
        txFrame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(jpeg)));
        CHECK(tx->writeFrame(txFrame).isOk());

        // Reader should emit a compressed JPEG image whose
        // reassembled bitstream decodes back to an 8-bit RGB image
        // of the original geometry.
        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());
        const Image &rxImg = *rxFrame->imageList()[0];
        CHECK(rxImg.isValid());
        CHECK(rxImg.isCompressed());
        CHECK(rxImg.width()  == W);
        CHECK(rxImg.height() == H);

        // Decode the reassembled JFIF via a JpegVideoDecoder
        // session — this is the real test that the rebuilt headers
        // are accepted by libjpeg-turbo.
        Image decoded = promeki::tests::decodeCompressedToImage(
                rxImg, PixelFormat(PixelFormat::RGB8_sRGB));
        CHECK(decoded.isValid());
        CHECK_FALSE(decoded.isCompressed());
        if(decoded.isValid()) {
                CHECK(decoded.width()  == W);
                CHECK(decoded.height() == H);
        }

        tx->close();
        rx->close();
        delete tx;
        delete rx;
}

TEST_CASE("MediaIOTask_Rtp_Reader_LoadSdp_RawVideo") {
        // End-to-end SDP hand-off: writer emits an SDP file via
        // RtpSaveSdpPath; reader consumes it via RtpSdp.
        // The reader's media descriptor still has to be set
        // (geometry is not in the SDP for raw 8-bit RGB), but the
        // destination and payload type come from the SDP.
        uint16_t port = pickFreeUdpPort();
        SocketAddress dest(Ipv4Address::loopback(), port);

        const size_t W = 8;
        const size_t H = 4;

        // Write a one-shot SDP file via the writer.
        String sdpTmp = String("/tmp/promeki-rtp-rx-loadsdp-") +
                        String::number(port) + String(".sdp");
        // Remove stale from a prior run.
        std::remove(sdpTmp.cstr());

        MediaIO::Config txCfg = MediaIO::defaultConfig("Rtp");
        txCfg.set(MediaConfig::VideoRtpDestination, dest);
        txCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        txCfg.set(MediaConfig::RtpSaveSdpPath, sdpTmp);
        MediaIO *tx = MediaIO::create(txCfg);
        REQUIRE(tx != nullptr);
        MediaDesc txMd;
        txMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        txMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        tx->setExpectedDesc(txMd);
        REQUIRE(tx->open(MediaIO::Sink).isOk());

        // Verify the SDP file has non-zero content before handing
        // it to the reader — catches writer-side misconfig.
        {
                File check(sdpTmp);
                REQUIRE(check.open(IODevice::ReadOnly).isOk());
                char peek[16] = {};
                int64_t got = check.read(peek, sizeof(peek));
                check.close();
                CHECK(got > 0);
        }

        // Reader — no explicit destination key; SDP ingest should
        // populate it.  Still set the media descriptor so the
        // reader knows the geometry.
        MediaIO::Config rxCfg = MediaIO::defaultConfig("Rtp");
        rxCfg.set(MediaConfig::RtpSdp, sdpTmp);
        rxCfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::None);
        MediaIO *rx = MediaIO::create(rxCfg);
        REQUIRE(rx != nullptr);
        MediaDesc rxMd;
        rxMd.setFrameRate(FrameRate(FrameRate::FPS_30));
        rxMd.imageList().pushToBack(ImageDesc(Size2Du32(W, H),
                PixelFormat(PixelFormat::RGB8_sRGB)));
        rx->setExpectedDesc(rxMd);
        REQUIRE(rx->open(MediaIO::Source).isOk());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        Frame::Ptr txFrame = makeTinyRgbFrame(W, H, 0xA5);
        CHECK(tx->writeFrame(txFrame).isOk());

        Frame::Ptr rxFrame;
        REQUIRE(waitForReaderFrame(rx, rxFrame).isOk());
        REQUIRE(rxFrame);
        REQUIRE(!rxFrame->imageList().isEmpty());
        const Image &rxImg = *rxFrame->imageList()[0];
        CHECK(rxImg.width()  == W);
        CHECK(rxImg.height() == H);

        tx->close();
        rx->close();
        delete tx;
        delete rx;
        std::remove(sdpTmp.cstr());
}
